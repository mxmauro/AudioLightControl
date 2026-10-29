---
name: windows-msbuild-maintenance
description: "Maintain Windows MSBuild project metadata, file lists, dependencies, and build validation."
---

# Windows MSBuild Maintenance

Use this skill when adding, renaming, or removing project-tracked files; editing MSBuild metadata; maintaining manifest
dependencies; or selecting a safe build validation command. Read local repository guidance for project-specific paths, targets,
generated directories, and dependency configuration.

## File-List Rules

- When adding, renaming, or removing `.cpp`, `.h`, `.rc`, `.ico`, or other project-tracked files, update the affected
  `.vcxproj`.
- Update the matching `.vcxproj.filters` so Solution Explorer stays organized.
- Do not edit generated outputs under `bin\`, `obj\`, `.vs\`, `lib\`, `libs\`, or `vcpkg_installed\`.
- Avoid editing `*.vcxproj.user` unless the task explicitly calls for user-local IDE state changes.

## Build Validation

- In a plain PowerShell environment, `msbuild` may not be on `PATH`.
- Prefer Visual Studio, a Developer PowerShell, or the repository's documented MSBuild path for raw build commands.

## Dependency Maintenance

- `vcpkg.json`, if present, holds manifest dependencies.
- `vcpkg-configuration.json`, if present, pins the builtin baseline and any custom registry baseline.

## Validation Checklist

- After file-list changes, verify both the `.vcxproj` and `.filters` entries reference the same relative paths.
- After dependency changes, review both manifest files together and confirm custom registry mappings still match dependencies.
- After build-system changes, prefer a project build in the proper Visual Studio environment rather than guessing from an
  unconfigured shell.
