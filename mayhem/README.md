# Mayhem integration: livekit/client-sdk-cpp

The C++ SDK reaches its Rust core (`livekit_ffi`: libwebrtc + tokio, built by cargo and fetched from the
network) only through four C symbols. This integration cannot build the Rust side air-gapped, so
`build.sh` compiles the SDK's own C++ sources plus the protobuf code generated from the FFI protocol
(`mayhem/ffi-protocol/`, a copy of `client-sdk-rust/livekit-ffi/protocol` at the submodule's pinned commit
`7f3ddc08`) against distro packages, and links `ffi_stub.cpp` in place of the Rust library. The stub acts
like a Rust side that accepts every request (valid handle, response matching the request) and never emits
async callbacks.

| target | code under test |
|---|---|
| `fuzz_audio_frame` | `AudioFrame` constructor/`create()`/`fromOwnedInfo()` input validation |
| `fuzz_token_source` | token-server response JSON, JWT validity check, request JSON builder |
| `fuzz_ffi_event` | `ffiEventCallback` -> `FfiClient::pushEvent` -> `Room::onEvent` on a pre-populated room |

`fuzz_ffi_event` rewrites raw buffer pointers in the decoded event to harness-owned memory (the real FFI
never sends arbitrary addresses) and skips `panic` events (the SDK raises SIGTERM on them by design).

`test.sh` runs the upstream gtest unit tests that exercise pure C++ (no live FFI) from `src/tests/unit`,
built by `build.sh` with normal flags into `build-tests/`.
