# AudioLightNode5050 Agent Notes

## Scope
- These instructions apply to the whole repository.
- Treat `managed_components/` as vendored dependencies. Do not edit them unless the task explicitly requires it.
- If a task does require changes under `managed_components/mxmauro__esp-utils/`, also follow that component's local `AGENTS.md`.

## Repository shape
- The application code is intentionally small and concentrated in `main/`.
- Keep orchestration and product behavior in `main/main.cpp`.
- Keep RGB PWM output and fade queue behavior in `main/ledstrip/5050_controller.cpp`.
- Keep build and flash workflow changes aligned with `idf-build.ps1`, `sdkconfig.defaults`, `sdkconfig.debug`, `sdkconfig.release`, and `partitions/partition_table.csv`.

## Build and configuration
- Prefer the repository wrapper script over raw `idf.py` when the task is just build, rebuild, flash, or monitor.
- `idf-build.ps1 build` uses `build/release` with `sdkconfig.defaults;sdkconfig.release`.
- `idf-build.ps1 build --debug` uses `build/debug` with `sdkconfig.defaults;sdkconfig.debug`.
- `idf-build.ps1 rebuild` also runs `set-target esp32c3` and `update-dependencies`.
- Keep the target as `esp32c3` unless the task explicitly changes hardware support.

## Application behavior
- Provisioning, Wi-Fi lifecycle, mDNS, reset flow, and IoT command dispatch all live in `main/main.cpp`.
- Custom command payloads are parsed with the existing binary reader helpers. Keep that pattern instead of introducing ad hoc parsing.
- WebSocket and UDP commands intentionally share handlers where possible. Extend both transports deliberately.
- Reset is a factory reset: it stops services, deinitializes subsystems, erases all NVS, and reboots.

## LED strip controller
- The 5050 controller is a task-and-queue design on top of LEDC PWM, not the `espressif/led_strip` component.
- Keep public API changes in `main/ledstrip/5050_controller.h` minimal and C-compatible.
- Preserve the queue-driven worker model for animation changes instead of writing LED state directly from unrelated tasks.
- Be careful with task shutdown and queue lifetime. Deinit currently depends on the worker consuming `CMD_STOP` and notifying the caller.

## Editing style
- Match the existing mixed C/C++ style already present in `main/`.
- Preserve the file-local separator comments:

```cpp
// -----------------------------------------------------------------------------
```

- Prefer narrow behavioral changes over cleanup passes.
