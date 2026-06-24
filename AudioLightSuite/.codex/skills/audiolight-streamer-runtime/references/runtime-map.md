# Runtime Map

## Entry Flow

- `src\Streamer\AudioLightStreamer.cpp`: `wWinMain`, Winsock and COM init, CLI parsing, config load, `RunEngine`.
- `src\Streamer\CommandLine.*`: `--pid`, `--process-name`, and `--config`.
- `src\Streamer\AppConfig.*`: UTF-8 JSON parsing, device list construction, host validation, and base64 private key decoding.
- `src\Streamer\Engine.cpp`: runtime object ownership, window creation, startup order, shutdown order, tick thread, and audio callback fanout.

## Task Routing

- Startup, argument parsing, or process targeting changes: start with `AudioLightStreamer.cpp` and `CommandLine.*`.
- Config schema or device bootstrap changes: start with `AppConfig.*`, then trace consumers in `Engine.cpp` and `Devices\`.
- Audio capture or callback timing changes: read `OutputAudioCapture.*`, then review `Engine.cpp` and `..\Common\Audio\AudioAnalysis.*`.
- Analyzer or visualization changes: review `..\Common\Audio\AudioAnalysis.*`, `Processors\*`, and `Monitors\*`.
- Device wiring or manual control changes: review `Devices\BaseIotDevice.*`, the concrete device class, and `SharedManualColorControl.*`.

## Coupling Checklist

- Keep `COutputAudioCapture::Config` callbacks aligned with `Engine.cpp`.
- Keep `FirstRender()` after windows are attached and before capture starts.
- Keep `Process()` for audio-driven updates and `TimerTick()` for time-based UI or fade work.
- If adding a new device type, update `CConfig::DeviceType`, JSON string parsing, the `Engine.cpp` device factory switch, the new class files, and project metadata.
- If adding a new processor or monitor, create the object, wrap it in a `CTUiFrameWindow`, load window position, attach it to the window manager, store ownership, and include it in the render or tick path as needed.
- If changing persisted window behavior, review `LoadWindowPosition*` helpers and each component's `CPersistantSettings` usage.

## Validation

- Prefer a project build after behavioral changes.
- Smoke-test startup with the default `AudioLightStreamer.conf.json` path and with an explicit `--config`.
- If process targeting changed, test both `--pid` and `--process-name`.
- If config parsing changed, verify invalid JSON, missing fields, invalid host names, and bad private key lengths still fail cleanly.
