// Fuzzes the LiveKit token-source parsers: the token-server JSON response, the JWT validity check
// on a participant token, and the JSON request builder.
//
// Input layout: [mode byte][payload]. mode % 3: 0 = response JSON, 1 = JWT, 2 = request options.
#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "token_source_internal.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) return -1;
  const uint8_t mode = data[0] % 3;
  const std::string payload(reinterpret_cast<const char*>(data) + 1, size - 1);

  switch (mode) {
    case 0: {
      auto result = livekit::parseTokenSourceResponseJson(payload);
      if (result) {
        (void)result.value().server_url.size();
      }
      break;
    }
    case 1:
      (void)livekit::isParticipantTokenValid(payload);
      break;
    default: {
      FuzzedDataProvider fdp(data + 1, size - 1);
      livekit::TokenRequestOptions options;
      if (fdp.ConsumeBool()) options.room_name = fdp.ConsumeRandomLengthString(64);
      if (fdp.ConsumeBool()) options.participant_name = fdp.ConsumeRandomLengthString(64);
      if (fdp.ConsumeBool()) options.participant_identity = fdp.ConsumeRandomLengthString(64);
      if (fdp.ConsumeBool()) options.participant_metadata = fdp.ConsumeRandomLengthString(64);
      (void)livekit::buildTokenSourceRequestJson(options);
      break;
    }
  }
  return 0;
}
