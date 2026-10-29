# AGENTS.md

## Scope

These instructions apply to `c:\Fuentes\IoT\AudioLightControl\AudioLightSuite`.

## Working Principles

- Think before coding. State assumptions explicitly, surface ambiguity or tradeoffs instead of
  picking silently, and ask for clarification when requirements or nearby code are unclear.
- Simplicity first. Make the smallest change that solves the task. Do not add speculative
  abstractions, configurability, or broader refactors that were not requested.
- Surgical changes. Keep diffs limited to the requested behavior, tests, and directly required
  project or documentation updates. Do not clean up unrelated areas, and remove only code or
  declarations that your change made unused.
- Goal-driven execution. Define the verification target before editing, prefer focused build or
  test checks that prove the changed behavior, and broaden validation only as the change scope
  requires.

## Repository Shape

- This is a Windows desktop C++ solution referenced by `AudioLightSuite.slnx`.
- The real source tree is `src\`.
- Main code areas under `src\`:
  - `Streamer\`: live Windows host application, including app startup, config, engine, main
    window, audio capture, device output, and processors
  - `Preprocessor\`: offline audio preprocessing tool
  - `Common\`: shared code used by both projects, including `Audio\`, `Crypto\`, `Http\`,
    `IotComm\`, `ThirdParty\`, and `Utils\`
- `src\Common\ThirdParty\nlohmann_json\json.hpp` is vendored third-party code. Do not modify it
  unless the task is explicitly about upgrading or patching the vendored dependency.

## Files And Folders To Treat Carefully

- `keys\` contains runtime configuration and key material. Do not rewrite, reformat, or replace
  files there unless the task explicitly asks for it.
- `bin\`, `obj\`, `.vs\`, and `vcpkg_installed\` are generated or dependency output areas. Do not
  hand-edit files there.
- `src\Streamer\AudioLightStreamer.vcxproj.user` is user-local IDE state. Avoid editing it.

## Formatting And Encoding

- Follow `.editorconfig` and `.gitattributes`.
- Text files use CRLF line endings.
- Preserve a final newline at end of file.
- Source and project text files are UTF-8.
- Do not introduce trailing whitespace.
- Keep source code and documentation lines at 140 characters or fewer unless a line is effectively
  unbreakable.
- Binary extensions already called out in `.gitattributes` must remain binary.

## C++ Style

- Use Allman style as the baseline: place opening braces on their own line for declarations,
  definitions, and control-flow blocks.
- Always enclose `if`, `do`, `while`, `for`, and `else` bodies in curly braces, including
  one-line bodies.
- Indent with 4 spaces in handwritten code. Avoid tabs in handwritten files; if a file is
  generator-produced or consistently tab-aligned, preserve its existing style.
- Use compact namespace formatting in handwritten code: `namespace X {`, with no extra
  indentation solely for namespace scope. Comment every namespace closing brace with the scope it
  closes, for example `} // namespace AudioLight`.
- Keep exactly one blank line between function definitions.
- Keep a blank line between other logical sections, but avoid excessive vertical whitespace.
- Exception: keep the opening brace on the same line for `typedef struct` and `typedef enum`
  declarations, for example `typedef struct _UPPER_NAME {`.
- All project-owned structs and enums use Windows SDK-style uppercase underscore typedef names.
  Use a leading-underscore tag and no `_s` or `_e` suffixes; declare a pointer alias where useful,
  for example `typedef struct _UPPER_NAME { ... } UPPER_NAME, *LPUPPER_NAME;`.
- Keep the project's existing Win32 style:
  - `HRESULT`, `BOOL`, `DWORD`, `INT`, `UINT32`, `WCHAR`
  - SAL annotations such as `_In_`, `_Out_`, `_In_z_`
  - `noexcept` on public helpers where already used
- This codebase is Windows-native. Prefer Win32 and COM-style types and APIs where the project
  already uses them.
- Prefer wide-character Win32 APIs and wide string literals: `L"..."`, `WCHAR`, `LPCWSTR`.
- Continue returning Win32-style error codes. Use `E_POINTER`, `E_INVALIDARG`, `E_OUTOFMEMORY`,
  `E_UNEXPECTED`, `HRESULT_FROM_WIN32(...)`, and `SUCCEEDED` / `FAILED` as appropriate.
- Use `TRUE` / `FALSE` for `BOOL` values and `nullptr` for pointers.
- Naming follows the current codebase, not modern STL-only style:
  - classes commonly use `C` prefix, such as `COutputAudioCapture`
  - project-owned structs and enums use uppercase underscore typedef names, such as
    `MAIN_WINDOW_CREATE_PARAMS`
  - types, namespaces, and methods otherwise preserve established local names
  - use `i` for signed integers, `u` for unsigned/count values, `f` for floating-point values,
    `b` for booleans, `by` for bytes, `dw` for `DWORD`, `cr` for colors, and `ch` for characters
    or codepoints
  - use `h` for handles and HRESULT-style values such as `hRes`; use `lp`, `lplp`, `lpi`, and
    `lph` for pointers and out-pointers
  - use `sz...W` for wide strings and buffers, `s` for structs/value objects, and `m_` for member
    fields
  - use `str` for `std::string` values and `vec` for `std::vector` values
  - preserve established names instead of renaming solely for style
- Preserve include style:
  - project headers first
  - then external/STL headers
  - use backslashes in project include paths, matching the existing code
- File-local helper declarations should stay `static` and appear near the top of the translation
  unit before the main function bodies.
- Prefer explicit types over `auto`.
- Exception: `auto` is acceptable for iterator-style loops, `if`/`switch` init-statements, and
  similar cases where the deduced type is obvious and the alternative is noisier, matching
  existing patterns such as `for (const auto &...)` and `if (auto it = ...; ...)`.
- Initialize variables explicitly, often at declaration time or immediately before first use.
- Keep long parameter lists, multiline expressions, and declarations split with the continuation
  aligned one character after the outer opening parenthesis. Fill lines as practical up to 140
  characters rather than using one argument per line unless readability requires it.
- Prefer early returns for argument validation and failure paths.
- Preserve the existing `switch` formatting with `case` labels indented inside the block and
  `break;` on its own line.
- Preserve the existing separator comment blocks:
  - `// -----------------------------------------------------------------------------`
- Prefer sparse, functional comments. Do not add narration for obvious code.
- Avoid style-only rewrites. Match the surrounding file instead of normalizing unrelated
  formatting.

## Expression Parentheses

- Use parentheses to clarify precedence, not mechanically around every subexpression.
- Avoid redundant parentheses in simple homogeneous boolean chains such as `a && b && c` or
  `a || b || c` when precedence is obvious.
- Add parentheses when mixing `&&` and `||` in the same expression.
- Wrap unary negation subexpressions in parentheses when they appear alongside other boolean
  terms, for example `if (var1 && (!var2) && var3)`.
- A standalone negation does not need extra grouping: `if (!var)`.
- Add parentheses when a unary operator applies to only part of a larger expression and its scope
  could be misread, for example `if ((!ptr) || (*ptr == 0))`.
- Prefer `return ((a && b) || (c && d)) ? TRUE : FALSE;` when mixed boolean operators are
  involved. Do not add grouping that does not improve readability.

## Project Maintenance Rules

- `src\Streamer\AudioLightStreamer.vcxproj` and `src\Preprocessor\AudioLightPreprocessor.vcxproj`
  have explicit file lists. If you add, rename, or remove source/header/resource files, update the
  relevant project file.
- Also update the corresponding `.vcxproj.filters` file so Solution Explorer stays organized.
- Keep new files in the existing folder taxonomy when possible instead of creating new top-level
  areas casually.

## Build And Dependency Notes

- The project targets MSVC/Visual Studio, x64, C++20, and uses the static vcpkg triplet
  `x64-windows-static`.
- Dependencies are managed through `vcpkg.json` and `vcpkg-configuration.json`.
- There is a maintenance script at `scripts\update-depedencies.ps1` that updates custom registry
  baselines and dependency versions.
- Prefer validating changes with a Visual Studio/MSBuild build of `AudioLightSuite.slnx`, or build
  the specific project you changed when appropriate.

## Runtime Notes

- By default the app expects `AudioLightStreamer.conf.json` unless `--config` is supplied.
- Supported command-line options currently include `--pid`, `--process-name`, and `--config`.
- Config parsing expects UTF-8 JSON and device entries with private key material. Be conservative
  around config/schema changes.
- Keep the audio capture callback and audio `Process()` methods free of terminal/UI rendering. They may analyze PCM,
  publish a thread-safe state snapshot, and send audio-driven IoT updates; only `TimerTick()` may render that state.
  This prevents UI work from delaying capture and losing audio blocks.

## Local Skill Routing

- Use `audiolight-streamer-runtime` for live Streamer startup, configuration, audio capture, processors, monitors, and device
  wiring.
- Use `audiolight-preprocessor-pipeline` for offline feature extraction or changes to shared analysis semantics.
- Use `audiolight-iotcomm-stack` for HTTP/WebSocket transport, authentication, cryptography, or streamer device messaging.
- Use `audiolight-msbuild-maintenance` when project file lists, filters, dependency metadata, or build validation are involved.

## Practical Editing Guidance

- Do not mass-reformat files.
- Do not "modernize" naming, include style, or Win32 conventions unless the task explicitly calls
  for a broader refactor.
- When changing behavior that touches startup, device setup, config parsing, audio capture, or
  shutdown flow, review adjacent code paths because the app is strongly coupled across those areas.
