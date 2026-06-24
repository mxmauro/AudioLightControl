---
name: node5050-build-flash
description: Build, rebuild, flash, and monitor the AudioLightNode5050 ESP-IDF firmware using the repository's PowerShell wrapper and its debug/release sdkconfig split. Use when Codex needs the correct local build commands, serial-port flow, target assumptions, or configuration files for this repository.
---

# Node5050 Build And Flash

Use the repository wrapper first.

## Run the standard workflow

- Run `.\idf-build.ps1 build` for a normal release build.
- Run `.\idf-build.ps1 build --debug` for a debug build.
- Run `.\idf-build.ps1 rebuild` when the build directory, target, or dependencies may be stale.
- Run `.\idf-build.ps1 flash --port COMx` to flash and start monitor.
- Run `.\idf-build.ps1 monitor --port COMx` to attach without reflashing.

## Respect the repo's configuration split

- Treat `sdkconfig.defaults` as the shared baseline.
- Treat `sdkconfig.debug` as the debug overlay.
- Treat `sdkconfig.release` as the release overlay.
- Treat `build/debug` and `build/release` as separate outputs.

## Know what rebuild changes

- `rebuild` deletes the selected build directory before invoking `idf.py`.
- `rebuild` also runs `set-target esp32c3` and `update-dependencies`.
- `build` does not reset the target or dependencies.

## Keep changes aligned with build assets

- If the task changes partition sizing or OTA assumptions, inspect `partitions/partition_table.csv`.
- If the task changes startup or build behavior, inspect both top-level `CMakeLists.txt` files and `idf-build.ps1`.
- If the task touches configuration-sensitive code, compare whether it is debug-only, release-only, or shared.

## Avoid common mistakes

- Do not assume raw `idf.py build` reproduces the intended config. The wrapper passes `SDKCONFIG_DEFAULTS` explicitly.
- Do not mix debug and release artifacts in the same build directory.
- Do not change the chip target unless the hardware target is actually changing.
