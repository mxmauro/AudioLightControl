# AudioLightSuite MSBuild Reference

## Project Map

- `AudioLightSuite.slnx` includes `src\Streamer\AudioLightStreamer.vcxproj` and
  `src\Preprocessor\AudioLightPreprocessor.vcxproj`.
- Both projects target `x64`, C++20, Unicode, and the `x64-windows-static` vcpkg triplet.
- Both projects output to `bin\` and write intermediates under `obj\`.

## Build Targets

- In this plain PowerShell environment, `msbuild` was not available on `PATH`; use Visual Studio, a Developer PowerShell, or
  the documented Visual Studio MSBuild path.
- Candidate build commands from a Developer PowerShell:
- `msbuild AudioLightSuite.slnx /p:Configuration=Debug /p:Platform=x64`
- `msbuild src\Streamer\AudioLightStreamer.vcxproj /p:Configuration=Debug /p:Platform=x64`
- `msbuild src\Preprocessor\AudioLightPreprocessor.vcxproj /p:Configuration=Debug /p:Platform=x64`

## Dependency Update Script

- `vcpkg.json` holds manifest dependencies.
- `vcpkg-configuration.json` pins the builtin baseline and the custom git registry baseline.
- `scripts\update-depedencies.ps1` updates custom registry baselines and rewrites `version>=` entries for mapped packages.
- The script accepts `-Install` to run `vcpkg install` after updating manifests.
