// Fuzzes the path every Rust->C++ notification takes: a serialized proto::FfiEvent is decoded by
// ffiEventCallback(), fanned out by FfiClient::pushEvent() to the registered listener, and handled
// by Room::onEvent() (participants, tracks, data streams, RPC, stats, conversions to the public
// event structs).
//
// Input: one serialized FfiEvent. The Rust FFI always sends fully-populated messages, so input that
// does not parse (missing required fields) is rejected. A `panic` event makes ffiEventCallback()
// raise SIGTERM by design, so it is rejected too. Raw buffer pointers are repointed at harness memory. The Room is pre-populated with a remote
// participant ("alice") that has an audio and a video publication/subscription so events that name
// existing participants and tracks reach the deeper handlers.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/stubs/common.h>

#include "ffi.pb.h"
#include "ffi_client.h"
#include "livekit/local_participant.h"
#include "livekit/logging.h"
#include "livekit/room.h"
#include "livekit/room_delegate.h"
#include "participant.pb.h"
#include "room.pb.h"
#include "track.pb.h"

namespace livekit {

struct RoomTestAccess {
  static void attach(Room& room, std::uint64_t room_handle, std::uint64_t local_handle) {
    const std::scoped_lock<std::mutex> guard(room.lock_);
    room.room_handle_ = std::make_shared<FfiHandle>(static_cast<uintptr_t>(room_handle));
    room.local_participant_ = std::make_shared<LocalParticipant>(
        FfiHandle(static_cast<uintptr_t>(local_handle)), "PA_local", "local", "local", "",
        std::unordered_map<std::string, std::string>{}, ParticipantKind::Standard, DisconnectReason::Unknown);
  }

  static void onEvent(Room& room, const proto::FfiEvent& event) { room.onEvent(event); }
};

}  // namespace livekit

namespace {

using livekit::proto::FfiEvent;

void roomEvent(livekit::Room& room, std::uint64_t room_handle, const std::function<void(livekit::proto::RoomEvent*)>& fill) {
  FfiEvent event;
  auto* re = event.mutable_room_event();
  re->set_room_handle(room_handle);
  fill(re);
  livekit::RoomTestAccess::onEvent(room, event);
}

void populate(livekit::Room& room, std::uint64_t room_handle) {
  roomEvent(room, room_handle, [](livekit::proto::RoomEvent* re) {
    auto* owned = re->mutable_participant_connected()->mutable_info();
    owned->mutable_handle()->set_id(100);
    auto* info = owned->mutable_info();
    info->set_sid("PA_alice");
    info->set_name("Alice");
    info->set_identity("alice");
    info->set_state(livekit::proto::PARTICIPANT_STATE_ACTIVE);
    info->set_metadata("");
    info->set_kind(livekit::proto::PARTICIPANT_KIND_STANDARD);
    info->set_disconnect_reason(livekit::proto::UNKNOWN_REASON);
    info->set_joined_at(0);
    info->set_client_protocol(0);
  });

  const struct {
    const char* sid;
    livekit::proto::TrackKind kind;
    std::uint64_t handle;
  } tracks[] = {{"TR_audio", livekit::proto::KIND_AUDIO, 200}, {"TR_video", livekit::proto::KIND_VIDEO, 201}};

  for (const auto& t : tracks) {
    roomEvent(room, room_handle, [&](livekit::proto::RoomEvent* re) {
      auto* tp = re->mutable_track_published();
      tp->set_participant_identity("alice");
      auto* pub = tp->mutable_publication();
      pub->mutable_handle()->set_id(t.handle);
      auto* info = pub->mutable_info();
      info->set_sid(t.sid);
      info->set_name(t.sid);
      info->set_kind(t.kind);
      info->set_source(t.kind == livekit::proto::KIND_AUDIO ? livekit::proto::SOURCE_MICROPHONE
                                                            : livekit::proto::SOURCE_CAMERA);
      info->set_simulcasted(false);
      info->set_width(0);
      info->set_height(0);
      info->set_mime_type("");
      info->set_muted(false);
      info->set_remote(true);
      info->set_encryption_type(livekit::proto::NONE);
    });
    roomEvent(room, room_handle, [&](livekit::proto::RoomEvent* re) {
      auto* ts = re->mutable_track_subscribed();
      ts->set_participant_identity("alice");
      auto* track = ts->mutable_track();
      track->mutable_handle()->set_id(t.handle + 100);
      auto* info = track->mutable_info();
      info->set_sid(t.sid);
      info->set_name(t.sid);
      info->set_kind(t.kind);
      info->set_stream_state(livekit::proto::STATE_ACTIVE);
      info->set_muted(false);
      info->set_remote(true);
    });
  }
}


// Buffer pointers in FfiEvents are raw addresses of memory the Rust side owns. A mutated input would
// carry arbitrary addresses (the real FFI never does), so repoint every pointer at a buffer owned by
// the harness and clamp the lengths that describe it. Message structure and all other values stay
// fuzzed.
constexpr std::size_t kBufSize = 1 << 16;
std::uint8_t g_buf[kBufSize];

void clampBuffers(google::protobuf::Message* msg) {
  namespace pb = google::protobuf;
  const pb::Descriptor* desc = msg->GetDescriptor();
  const pb::Reflection* refl = msg->GetReflection();
  for (int i = 0; i < desc->field_count(); ++i) {
    const pb::FieldDescriptor* f = desc->field(i);
    if (f->type() == pb::FieldDescriptor::TYPE_MESSAGE) {
      if (f->is_repeated()) {
        for (int j = 0; j < refl->FieldSize(*msg, f); ++j) clampBuffers(refl->MutableRepeatedMessage(msg, f, j));
      } else if (refl->HasField(*msg, f)) {
        clampBuffers(refl->MutableMessage(msg, f));
      }
    } else if (f->type() == pb::FieldDescriptor::TYPE_UINT64 && !f->is_repeated()) {
      const std::string& name = f->name();
      if (name == "data_ptr" || name == "output_ptr") {
        refl->SetUInt64(msg, f, reinterpret_cast<std::uint64_t>(g_buf));
      } else if (name == "data_len") {
        refl->SetUInt64(msg, f, refl->GetUInt64(*msg, f) % (kBufSize + 1));
      }
    } else if (f->type() == pb::FieldDescriptor::TYPE_UINT32 && !f->is_repeated()) {
      const std::string& name = f->name();
      if (name == "num_channels") refl->SetUInt32(msg, f, refl->GetUInt32(*msg, f) % 9);
      if (name == "samples_per_channel") refl->SetUInt32(msg, f, refl->GetUInt32(*msg, f) % 1024);
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static const bool initialized = [] {
    livekit::setLogLevel(livekit::LogLevel::Off);
    google::protobuf::SetLogHandler([](google::protobuf::LogLevel, const char*, int, const std::string&) {});
    return livekit::FfiClient::instance().initialize(false);
  }();
  (void)initialized;

  FfiEvent event;
  if (!event.ParseFromArray(data, static_cast<int>(size))) return -1;
  if (event.has_panic()) return -1;
  clampBuffers(&event);
  std::string bytes;
  if (!event.SerializeToString(&bytes)) return -1;

  const std::uint64_t room_handle = event.has_room_event() ? event.room_event().room_handle() : 1;
  const std::uint64_t local_handle =
      event.has_rpc_method_invocation() ? event.rpc_method_invocation().local_participant_handle() : 2;

  livekit::RoomDelegate delegate;
  livekit::Room room;
  room.setDelegate(&delegate);
  livekit::RoomTestAccess::attach(room, room_handle, local_handle);
  populate(room, room_handle);

  auto& client = livekit::FfiClient::instance();
  const auto listener = client.addListener(
      [&room](const livekit::proto::FfiEvent& e) { livekit::RoomTestAccess::onEvent(room, e); });
  livekit::ffiEventCallback(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
  client.removeListener(listener);
  return 0;
}
