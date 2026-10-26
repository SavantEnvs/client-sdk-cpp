// Fuzzes livekit::AudioFrame: the validating constructor, create(), the accessors/formatters and
// fromOwnedInfo() (the copy out of a Rust-owned PCM buffer).
//
// Input layout: [i32 sample_rate][i32 num_channels][i32 samples_per_channel][int16 PCM ...]
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

#include "audio_frame.pb.h"
#include "handle.pb.h"
#include "livekit/audio_frame.h"

namespace {
// A caller-side memory budget for AudioFrame::create(): 1 Mi samples (2 MiB).
constexpr std::uint64_t kMaxCreateSamples = 1ull << 20;
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 12) return -1;

  std::int32_t sample_rate, num_channels, samples_per_channel;
  std::memcpy(&sample_rate, data, 4);
  std::memcpy(&num_channels, data + 4, 4);
  std::memcpy(&samples_per_channel, data + 8, 4);
  const uint8_t* pcm_bytes = data + 12;
  const size_t pcm_len = (size - 12) / 2;

  std::vector<std::int16_t> pcm(pcm_len);
  if (pcm_len) std::memcpy(pcm.data(), pcm_bytes, pcm_len * 2);

  try {
    livekit::AudioFrame frame(pcm, sample_rate, num_channels, samples_per_channel);
    (void)frame.duration();
    (void)frame.toString();
    (void)frame.totalSamples();
    frame.data();
  } catch (const std::invalid_argument&) {
  }

  const std::int64_t want = static_cast<std::int64_t>(num_channels) * samples_per_channel;
  if (want > 0 && static_cast<std::uint64_t>(want) <= kMaxCreateSamples) {
    try {
      livekit::AudioFrame created = livekit::AudioFrame::create(sample_rate, num_channels, samples_per_channel);
      (void)created.toString();
    } catch (const std::invalid_argument&) {
    }
  }

  // fromOwnedInfo copies num_channels * samples_per_channel samples starting at data_ptr; point it at
  // the PCM we own and only describe frames that fit in it.
  if (num_channels > 0 && samples_per_channel > 0 && want > 0 &&
      static_cast<std::uint64_t>(want) <= pcm.size()) {
    livekit::proto::OwnedAudioFrameBuffer owned;
    owned.mutable_handle()->set_id(0);
    auto* info = owned.mutable_info();
    info->set_data_ptr(reinterpret_cast<std::uint64_t>(pcm.data()));
    info->set_num_channels(static_cast<std::uint32_t>(num_channels));
    info->set_sample_rate(static_cast<std::uint32_t>(sample_rate));
    info->set_samples_per_channel(static_cast<std::uint32_t>(samples_per_channel));
    try {
      livekit::AudioFrame copied = livekit::AudioFrame::fromOwnedInfo(owned);
      (void)copied.toString();
    } catch (const std::invalid_argument&) {
    }
  }
  return 0;
}
