---
name: node5050-feature-workflow
description: Extend or modify AudioLightNode5050 application behavior in main/main.cpp and main/ledstrip/5050_controller.cpp. Use when Codex needs to add or change IoT commands, provisioning or mDNS behavior, reset or NVS lifecycle, or 5050 PWM fade/control logic in this repository.
---

# Node5050 Feature Workflow

Read `main/main.cpp` and `main/ledstrip/5050_controller.cpp` before editing behavior.

## Map responsibilities first

- Keep provisioning, Wi-Fi events, mDNS, command dispatch, and factory reset flow in `main/main.cpp`.
- Keep RGB PWM output, fade interpolation, and worker-task behavior in `main/ledstrip/5050_controller.cpp`.
- Keep the public LED API in `main/ledstrip/5050_controller.h` C-compatible and small.

## Extend commands by following the existing path

- Add the command id near the existing `CMD_*` constants in `main/main.cpp`.
- Route the new command through `onCustomCommand`.
- Reuse the existing transport split: WebSocket and UDP should intentionally share handlers unless the command really is transport-specific.
- Parse payloads with `binary_reader_t` helpers instead of custom byte arithmetic.
- Reply with the existing `iotCommEventReplyWithError` and `iotCommSessionClose` patterns.
- Gate privileged behavior with `iotCommIsSessionUserAdmin` when the command changes configuration or persistent state.

## Preserve lifecycle behavior

- Keep `commStart`, `commStop`, and mDNS registration in sync.
- If a change affects hostname or discovery, inspect `captivePortalProvisioningHandler`, `commStart`, `registerMdnsHttpService`, and `setMdnsHostname` together.
- Treat reset as a full factory reset. The current path deinitializes services, erases all NVS, and reboots.

## Preserve LED controller invariants

- Prefer queueing commands into the worker task rather than mutating LED state from unrelated contexts.
- Keep fade timing based on `xTaskGetTickCount()` and periodic wakeups unless the task explicitly changes animation architecture.
- Be careful with `ledStrip5050Deinit()`: queue lifetime and shutdown notification ordering matter.

## Validate the right surface

- After command changes, test both payload validation and privilege checks.
- After provisioning or mDNS changes, test cold boot, post-provisioning behavior, reconnect, and reset.
- After LED changes, test immediate set, fade-to, set-and-fade, and deinit behavior.
