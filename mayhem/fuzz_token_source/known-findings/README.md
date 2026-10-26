# `buildTokenSourceRequestJson` throws on non-UTF-8 option strings

`livekit::buildTokenSourceRequestJson()` (`src/token_source_json.cpp:82`) copies the caller's
`room_name` / `participant_name` / `participant_identity` / `participant_metadata` / attributes /
agent fields into an `nlohmann::json` and calls `dump()` with the default error handler. Any
string that is not valid UTF-8 makes `dump()` throw `nlohmann::json::type_error` 316
("invalid UTF-8 byte at index 0"). Nothing in the token-source path catches it, so
`TokenSource::fetch()` with e.g. a binary participant identity or metadata blob terminates the
caller with an uncaught exception.

Reproduce: `/mayhem/fuzz_token_source-standalone mayhem/fuzz_token_source/known-findings/invalid-utf8-request-options.bin`
(mode byte `0xda % 3 == 2` = request options; the options carry a string with an incomplete UTF-8 sequence).

Fix: `body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)` (or reject the options with a
`TokenSourceError`).

The reproducer is kept out of `testsuite/` on purpose; the harness does not filter these inputs.
