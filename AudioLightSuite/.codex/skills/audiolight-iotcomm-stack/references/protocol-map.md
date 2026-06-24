# Protocol Map

## File Map

- `src\Common\Http\HttpClient.*`: asynchronous WinHTTP request handling, websocket setup, websocket read and close callbacks, and transport cleanup.
- `src\Common\IotComm\IotCommClient.*`: `/ws/init` and `/ws/auth` handshake, ECDH and ECDSA auth flow, HKDF key derivation, websocket packet encryption, UDP channel setup, reply tracking, and cleanup.
- `src\Streamer\Devices\BaseIotDevice.*`: device-side comm thread, close and message callbacks, `SendIotMessage`, and UI-facing connection state.
- `src\Streamer\Devices\LedStrip5050.*`: current concrete device integration and command payload example.

## Current Handshake Shape

- `POST /ws/init` with user name, client nonce, and ECDH public key.
- `POST /ws/auth` with token, auth nonce, and ECDSA signature.
- Derive a retained session master key, then websocket transport keys and IV bases through HKDF.
- Connect `ws://<host>/ws` with token, websocket nonce, and signature query parameters.
- Optionally open a UDP channel and derive separate UDP transport state.

## Packet And State Invariants

- Websocket packets use a fixed 16-byte header plus GCM tag.
- Header fields are big-endian.
- Websocket and UDP counters advance monotonically and feed IV derivation.
- Binary websocket packets are mandatory.
- Sensitive buffers are cleared during cleanup.

## Device-Side Checklist

- If transport semantics change, review `CBaseIotDevice::InitComm`, `DeinitComm`, `SendIotMessage`, and connection-state rendering.
- If the supported device command set changes, update the concrete device class and keep payload packing explicit.
- `CLedStrip5050IotDevice` currently depends on the `SpectrumColor` processor and sends command `3` with a packed fade payload.
- If accepted host syntax changes, update both the config parser and `IotCommClient` hostname validators.

## Validation

- Prefer a project build after protocol or transport changes.
- If a reachable device or test server exists, validate the full init, auth, and websocket connect flow before trusting changes.
- If packet layout changes, inspect both websocket and UDP send paths together.
