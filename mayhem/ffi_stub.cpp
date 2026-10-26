// Stand-in for the Rust livekit_ffi cdylib (libwebrtc + tokio), which cannot be built offline.
// The C++ SDK only reaches Rust through these four C symbols. This stub acts like a Rust side that
// accepts every request: it returns a valid handle and a response whose oneof matches the request's
// (same field name), with default contents. It never produces async callbacks, so the pure-C++
// layer (protobuf event decoding, room/participant state, conversions) is exercised in-process.
#include <cstddef>
#include <cstdint>
#include <string>

#include "ffi.pb.h"
#include "livekit_ffi.h"

namespace {
thread_local std::string g_response;  // the SDK parses it before the same thread sends another request
}

extern "C" {

FfiHandleId livekit_ffi_request(const uint8_t* data, size_t len, const uint8_t** res_ptr, size_t* res_len) {
  *res_ptr = nullptr;
  *res_len = 0;
  livekit::proto::FfiRequest request;
  if (!request.ParseFromArray(data, static_cast<int>(len))) {
    return INVALID_HANDLE;
  }
  livekit::proto::FfiResponse response;
  const auto* req_refl = request.GetReflection();
  const google::protobuf::FieldDescriptor* set_field = nullptr;
  for (int i = 0; i < request.GetDescriptor()->field_count(); ++i) {
    const auto* f = request.GetDescriptor()->field(i);
    if (f->containing_oneof() != nullptr && req_refl->HasField(request, f)) {
      set_field = f;
      break;
    }
  }
  if (set_field != nullptr) {
    const auto* resp_field = response.GetDescriptor()->FindFieldByName(set_field->name());
    if (resp_field != nullptr && resp_field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      response.GetReflection()->MutableMessage(&response, resp_field);
    }
  }
  if (!response.SerializeToString(&g_response) || g_response.empty()) {
    return INVALID_HANDLE;
  }
  *res_ptr = reinterpret_cast<const uint8_t*>(g_response.data());
  *res_len = g_response.size();
  return 1;
}

bool livekit_ffi_drop_handle(FfiHandleId) { return true; }

void livekit_ffi_initialize(void (*)(const uint8_t*, size_t), bool, const char*, const char*) {}

void livekit_ffi_dispose() {}

}  // extern "C"
