# AudioLightNode Agent Notes

## Scope
- These instructions apply to the whole repository.
- Treat `managed_components/` as vendored dependencies. Do not edit them unless the task explicitly requires it.
- If a task does require changes under `managed_components/mxmauro__esp-utils/`, also follow that component's local `AGENTS.md`.

## Shared skills
- Use `embedded-working-principles` for change scope and verification, `embedded-security-expectations` for security-sensitive work, and `esp-idf-guidance` for ESP-IDF and FreeRTOS work.
- Use `embedded-cpp-style` for C/C++ source and headers and `repository-file-style` for CMake, YAML, shell, Markdown, and other text files.

## Repository shape
- The application code is intentionally small and concentrated in `main/`.
- Keep orchestration and product behavior in `main/main.cpp`.
- Keep DMX transmit output and fade queue behavior in `main/dmx/dmx_controller.cpp`.
- Keep build and flash workflow changes aligned with `idf-build.ps1`, `sdkconfig.defaults`, `sdkconfig.debug`, `sdkconfig.release`, and `partitions/partition_table.csv`.

## Build and configuration
- Prefer the repository wrapper script over raw `idf.py` when the task is just build, rebuild, flash, or monitor.
- When validating application changes with a local build, Debug is the default: run `idf-build.ps1 build`, or `idf-build.ps1 rebuild` when the build directory, target, or dependencies may be stale. Use `-Release` for the release configuration.
- `idf-build.ps1 build` uses `build/debug` with `sdkconfig.defaults;sdkconfig.debug`.
- `idf-build.ps1 build -Release` uses `build/release` with `sdkconfig.defaults;sdkconfig.release`.
- `idf-build.ps1 rebuild` runs `set-target` for its `-Target` value (default: `c3`, normalized to `esp32c3`) and `update-dependencies`.
- `-Target` is accepted only for `rebuild`; a regular `build` reuses the target recorded in the selected profile's `sdkconfig`.
- Keep the target as `esp32c3` unless the task explicitly changes hardware support.

## Application behavior
- Provisioning, Wi-Fi lifecycle, mDNS, reset flow, and IoT command dispatch all live in `main/main.cpp`.
- Custom command payloads are parsed with the existing binary reader helpers. Keep that pattern instead of introducing ad hoc parsing.
- WebSocket and UDP commands intentionally share handlers where possible. Extend both transports deliberately.
- Reset is a factory reset: it stops services, deinitializes subsystems, erases all NVS, and reboots.

## DMX controller
- The DMX controller is a task-and-queue design on top of UART transmit timing and a MAX485 transceiver, not the `espressif/led_strip` component.
- Keep public API changes in `main/dmx/dmx_controller.h` minimal and C-compatible.
- Preserve the queue-driven worker model for animation changes instead of writing DMX state directly from unrelated tasks.
- Be careful with task shutdown and queue lifetime. Deinit currently depends on the worker consuming `CMD_STOP` and notifying the caller.

## Naming and API conventions
- Preserve the established C API naming: PascalCase types such as `DmxFixtureConfig_t`, `Config_t`, and `RGBColor_t`; `_s`/`_e` tags; lower-camel module functions such as `dmxControllerInit`; and existing utility helpers such as `lerp_u16` and `br_ext_read_gpio`.

## GuiTerminal integration
- When working on the Windows GuiTerminal consumer in the sibling AudioLightSuite project, start with `C:\Fuentes\IoT\AudioLightControl\AudioLightSuite\vcpkg_installed\x64-windows-static\share\guiterminal\docs\ai-index.md`, then read only the relevant linked topic. The installed headers are authoritative.
- This static package uses the C++ API: include `GuiTerminalControl.h`, link `GuiTerminal::GuiTerminal`, and integrate through `GuiTerminal::Control` and `GuiTerminal::DrawContext`. Do not use the installed `Internals` headers as normal application entry points.
- Create a control during `WM_CREATE`; pass window messages to `Control::WndProc` before application-specific handling. After `Control::Create`, the `HWND` owns the control: obtain it with `Control::GetControl` and never delete it directly.
- Treat `WM_SIZE` rendering-target changes separately from grid changes. Call `ResizeTerminal` only when the logical row or column count must change; use preferred-size methods when an initial window must fit an exact grid.
- Root and region cell coordinates are zero-based, ANSI cursor coordinates are one-based, and custom-draw coordinates are DIPs. Check `HRESULT` from creation, resize, relocation, visibility, ordering, and callback-registration operations.
- Use normal regions for clipped panels, independent ANSI/cursor state, and local cell coordinates. Regions own their descendant hierarchy; use their context and destroy callback for region-lifetime state, and the region-order APIs for overlap.
- For charts or other non-cell graphics, use custom-draw regions and invalidate them after model changes. Draw and resource-cleanup callbacks run while GuiTerminal holds its control lock, so they must not call `Control` methods. Release application model locks before invalidating to avoid lock-order inversion.
- Keep Direct2D render-target resources in callback-owned state. The callback render target is transient; recreate resources lazily, reset them in an idempotent cleanup callback, and synchronize any resource or model also accessed outside the callbacks.

