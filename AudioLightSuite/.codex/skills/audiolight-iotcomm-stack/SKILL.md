---
name: audiolight-iotcomm-stack
description: "Analyze and modify the AudioLightSuite IoT communication stack: HTTP and WebSocket transport, UDP messaging, ECDH and ECDSA authentication, AES and HKDF session setup, device connection lifecycle, and streamer device messaging. Use when Codex is changing src\\Common\\Http, src\\Common\\IotComm, shared crypto used by transport, or device communication code in src\\Streamer\\Devices."
---

# AudioLight IoT Comm Stack

## Workflow

1. Read [protocol-map.md](references/protocol-map.md) before editing.
   Decide whether the task is primarily about handshake flow, packet format, websocket lifecycle, UDP path, or device-side integration.
2. Preserve protocol invariants.
   Keep handshake stages, key derivation inputs, packet header layout, big-endian encoding, and counter progression coherent unless the protocol itself is intentionally changing.
3. Preserve concurrency and cleanup behavior.
   Avoid moving user callbacks under internal locks, keep rundown and abort handling intact, and continue zeroing sensitive buffers during cleanup.
4. Review duplicated validation logic.
   Host and port validation exists in both `AppConfig.cpp` and `IotCommClient.cpp`. If the accepted syntax changes, update both sides deliberately.
5. Route adjacent concerns to other skills.
   Use `$audiolight-streamer-runtime` when transport changes also require device factory, UI, or live runtime changes.
   Use `$audiolight-msbuild-maintenance` if the work adds files or requires project metadata edits.

## References

- Read [protocol-map.md](references/protocol-map.md) before substantial IoT comm work.
