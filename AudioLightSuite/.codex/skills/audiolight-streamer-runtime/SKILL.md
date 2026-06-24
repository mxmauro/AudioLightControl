---
name: audiolight-streamer-runtime
description: "Analyze and modify the AudioLightSuite Streamer app: Win32 startup, command-line parsing, UTF-8 JSON config loading, output audio capture, analyzer or processor flow, IoT device wiring, and terminal UI layout. Use when Codex is changing files under src\\Streamer or shared runtime code that affects live playback, startup or shutdown behavior, device behavior, or terminal windows."
---

# AudioLight Streamer Runtime

## Workflow

1. Route the request before editing.
   Read [runtime-map.md](references/runtime-map.md) and identify whether the task is primarily about startup and config, audio capture and analysis, processors or monitors, device integration, or terminal UI layout.
2. Preserve the runtime lifecycle.
   Keep the startup order coherent across `AudioLightStreamer.cpp` and `Engine.cpp`: parse CLI, read config, initialize Winsock and COM, create runtime objects, start the tick thread, then start capture.
   Keep shutdown symmetric: stop the tick thread, finalize audio capture, finalize the window manager, then release owned windows, devices, monitors, and processors.
3. Preserve project conventions.
   Return `HRESULT`s, keep user-facing failures on Win32 message boxes, preserve UTF-8 config parsing, and keep wide-character Win32 boundaries intact.
4. Review coupled code paths whenever behavior changes.
   If config schema changes, review `AppConfig.*`, `Engine.cpp`, and every affected device class.
   If audio frame semantics change, review `OutputAudioCapture.*`, `..\Common\Audio\AudioAnalysis.*`, processors, monitors, and device `Process()` logic.
5. Hand off cross-cutting work explicitly.
   Use `$audiolight-iotcomm-stack` for protocol, auth, or transport changes.
   Use `$audiolight-preprocessor-pipeline` if shared analyzer semantics must stay aligned with offline feature extraction.
   Use `$audiolight-msbuild-maintenance` when file lists, filters, or build validation are part of the task.

## References

- Read [runtime-map.md](references/runtime-map.md) before substantial streamer work.
