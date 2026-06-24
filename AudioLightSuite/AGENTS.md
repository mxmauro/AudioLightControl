# AGENTS.md

## Scope

These instructions apply to `c:\Fuentes\IoT\AudioLightControl\AudioLightSuite`.

## Repository Shape

- This is a Windows desktop C++ solution referenced by `AudioLightSuite.slnx`.
- The real source tree is `src\`.
- Main code areas under `src\`:
  - `Streamer\`: live Windows host application, including app startup, config, engine, main window, audio capture, device output, and processors
  - `Preprocessor\`: offline audio preprocessing tool
  - `Common\`: shared code used by both projects, including `Audio\`, `Crypto\`, `Http\`, `IotComm\`, `ThirdParty\`, and `Utils\`
- `src\Common\ThirdParty\nlohmann_json\json.hpp` is vendored third-party code. Do not modify it unless the task is explicitly about upgrading or patching the vendored dependency.

## Files And Folders To Treat Carefully

- `keys\` contains runtime configuration and key material. Do not rewrite, reformat, or replace files there unless the task explicitly asks for it.
- `bin\`, `obj\`, `.vs\`, and `vcpkg_installed\` are generated or dependency output areas. Do not hand-edit files there.
- `src\Streamer\AudioLightStreamer.vcxproj.user` is user-local IDE state. Avoid editing it.

## Formatting And Encoding

- Follow `.editorconfig` and `.gitattributes`.
- Text files use CRLF line endings.
- Preserve a final newline at end of file.
- Source and project text files are UTF-8.
- Do not introduce trailing whitespace.
- Binary extensions already called out in `.gitattributes` must remain binary.

## C++ Style

- Use 4 spaces for indentation. Do not introduce tabs.
- Use braces on their own lines for namespaces, classes, structs, functions, and control-flow blocks.
- Keep a blank line between logical sections, but avoid excessive vertical whitespace.
- Keep the project's existing Win32 style:
  - `HRESULT`, `BOOL`, `DWORD`, `INT`, `UINT32`, `WCHAR`
  - SAL annotations such as `_In_`, `_Out_`, `_In_z_`
  - `noexcept` on public helpers where already used
- This codebase is Windows-native. Prefer Win32 and COM-style types and APIs where the project already uses them.
- Prefer wide-character Win32 APIs and wide string literals: `L"..."`, `WCHAR`, `LPCWSTR`.
- Continue returning Win32-style error codes. Use `E_POINTER`, `E_INVALIDARG`, `E_OUTOFMEMORY`, `E_UNEXPECTED`, `HRESULT_FROM_WIN32(...)`, and `SUCCEEDED` / `FAILED` as appropriate.
- Use `TRUE` / `FALSE` for `BOOL` values and `nullptr` for pointers.
- Naming follows the current codebase, not modern STL-only style:
  - classes commonly use `C` prefix, such as `COutputAudioCapture`
  - structs and typedefs use uppercase or Win32-style names, such as `MAIN_WINDOW_CREATE_PARAMS`
  - locals often use Hungarian-style prefixes such as `hRes`, `dwPid`, `strArgW`
- Preserve include style:
  - project headers first
  - then external/STL headers
  - use backslashes in project include paths, matching the existing code
- File-local helper declarations should stay `static` and appear near the top of the translation unit before the main function bodies.
- Prefer explicit types over `auto`.
- Exception: `auto` is acceptable for iterator-style loops, `if`/`switch` init-statements, and similar cases where the deduced type is obvious and the alternative is noisier, matching existing patterns such as `for (const auto &...)` and `if (auto it = ...; ...)`.
- Initialize variables explicitly, often at declaration time or immediately before first use.
- Keep long parameter lists split across lines with the continuation aligned for readability, matching the current style.
- Prefer early returns for argument validation and failure paths.
- Preserve the existing `switch` formatting with `case` labels indented inside the block and `break;` on its own line.
- Preserve the existing separator comment blocks:
  - `// -----------------------------------------------------------------------------`
- Prefer sparse, functional comments. Do not add narration for obvious code.
- Avoid style-only rewrites. Match the surrounding file instead of normalizing unrelated formatting.

## Project Maintenance Rules

- `src\Streamer\AudioLightStreamer.vcxproj` and `src\Preprocessor\AudioLightPreprocessor.vcxproj` have explicit file lists. If you add, rename, or remove source/header/resource files, update the relevant project file.
- Also update the corresponding `.vcxproj.filters` file so Solution Explorer stays organized.
- Keep new files in the existing folder taxonomy when possible instead of creating new top-level areas casually.

## Build And Dependency Notes

- The project targets MSVC/Visual Studio, x64, C++20, and uses the static vcpkg triplet `x64-windows-static`.
- Dependencies are managed through `vcpkg.json` and `vcpkg-configuration.json`.
- There is a maintenance script at `scripts\update-depedencies.ps1` that updates custom registry baselines and dependency versions.
- Prefer validating changes with a Visual Studio/MSBuild build of `AudioLightSuite.slnx`, or build the specific project you changed when appropriate.

## Runtime Notes

- By default the app expects `AudioLightStreamer.conf.json` unless `--config` is supplied.
- Supported command-line options currently include `--pid`, `--process-name`, and `--config`.
- Config parsing expects UTF-8 JSON and device entries with private key material. Be conservative around config/schema changes.

## Practical Editing Guidance

- Prefer minimal, targeted edits. This is a native Windows codebase with explicit resource and project metadata.
- Do not mass-reformat files.
- Do not "modernize" naming, include style, or Win32 conventions unless the task explicitly calls for a broader refactor.
- When changing behavior that touches startup, device setup, config parsing, audio capture, or shutdown flow, review adjacent code paths because the app is strongly coupled across those areas.
