---
name: audiolightnode-build-flash
description: Build, rebuild, flash, and monitor the AudioLightNode ESP-IDF firmware using the repository's PowerShell wrapper and its debug/release sdkconfig split. Use when Codex needs the correct local build commands, serial-port flow, target assumptions, or configuration files for this repository.
---

# AudioLightNode Build And Flash

Use the repository wrapper first.

## Run the standard workflow

- Run `.\idf-build.ps1 build` for the default Debug build.
- Run `.\idf-build.ps1 build -Release` for a release build.
- Run `.\idf-build.ps1 rebuild` when the Debug build directory, target, or dependencies may be stale. Add `-Release` to rebuild the release directory instead.
- Run `.\idf-build.ps1 rebuild -Target c3` to select a target during a rebuild. `-Target` is not accepted for `build`, `flash`, or `monitor`.
- Run `.\idf-build.ps1 flash -Port COMx` to flash and start monitor; add `-Erase` to erase first.
- Run `.\idf-build.ps1 monitor -Port COMx` to attach without reflashing.

## Respect the repo's configuration split

- Treat `sdkconfig.defaults` as the shared baseline.
- Treat `sdkconfig.debug` as the debug overlay.
- Treat `sdkconfig.release` as the release overlay.
- Treat `build/debug` and `build/release` as separate outputs.

## Know what rebuild changes

- `rebuild` deletes the selected build directory before invoking `idf.py`.
- `rebuild` also runs `set-target` for the supplied target (default: `c3`, normalized to `esp32c3`) and `update-dependencies`.
- `build` does not reset the target or dependencies; when the selected profile already has an `sdkconfig`, it uses that file's configured target.

## Keep changes aligned with build assets

- If the task changes partition sizing or OTA assumptions, inspect `partitions/partition_table.csv`.
- If the task changes startup or build behavior, inspect both top-level `CMakeLists.txt` files and `idf-build.ps1`.
- If the task touches configuration-sensitive code, compare whether it is debug-only, release-only, or shared.

## Avoid common mistakes

- Do not assume raw `idf.py build` reproduces the intended config. The wrapper selects the matching Debug or Release preset.
- Do not mix debug and release artifacts in the same build directory.
- Do not change the chip target unless the hardware target is actually changing.
