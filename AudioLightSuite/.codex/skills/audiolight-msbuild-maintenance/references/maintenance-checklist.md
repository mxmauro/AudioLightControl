# Maintenance Checklist

## Project Map

- `AudioLightSuite.slnx`: solution entry that includes `src\Streamer\AudioLightStreamer.vcxproj` and `src\Preprocessor\AudioLightPreprocessor.vcxproj`.
- Both projects target `x64`, C++20, Unicode, and the `x64-windows-static` vcpkg triplet.
- Both projects output to `bin\` and write intermediates under `obj\`.

## File-List Rules

- When adding, renaming, or removing `.cpp`, `.h`, `.rc`, `.ico`, or other project-tracked files, update the affected `.vcxproj`.
- Update the matching `.vcxproj.filters` so Solution Explorer stays organized.
- Do not edit generated outputs under `bin\`, `obj\`, `.vs\`, or `vcpkg_installed\`.
- Avoid editing `*.vcxproj.user` unless the task explicitly calls for user-local IDE state changes.

## Build Validation

- In this plain PowerShell environment, `msbuild` was not available on `PATH`.
- Prefer Visual Studio or a Developer PowerShell if raw `msbuild` commands are needed.
- Candidate build commands from a Developer PowerShell:
- `msbuild AudioLightSuite.slnx /p:Configuration=Debug /p:Platform=x64`
- `msbuild src\Streamer\AudioLightStreamer.vcxproj /p:Configuration=Debug /p:Platform=x64`
- `msbuild src\Preprocessor\AudioLightPreprocessor.vcxproj /p:Configuration=Debug /p:Platform=x64`

## Dependency Maintenance

- `vcpkg.json` holds manifest dependencies.
- `vcpkg-configuration.json` pins the builtin baseline and the custom git registry baseline.
- `scripts\update-depedencies.ps1` updates custom registry baselines and rewrites `version>=` entries for mapped packages.
- The script accepts `-Install` to run `vcpkg install` after updating manifests.

## Validation Checklist

- After file-list changes, verify both the `.vcxproj` and `.filters` entries reference the same relative paths.
- After dependency changes, review both manifest files together and confirm the custom registry package mapping still matches the dependency list.
- After build-system changes, prefer a project build in the proper Visual Studio environment rather than guessing from an unconfigured shell.
