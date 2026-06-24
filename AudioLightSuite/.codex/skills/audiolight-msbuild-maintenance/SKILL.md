---
name: audiolight-msbuild-maintenance
description: "Maintain AudioLightSuite MSBuild and dependency metadata: add, rename, or remove C++ files, update `.vcxproj` and `.vcxproj.filters`, choose safe build validation commands, and maintain `vcpkg.json`, `vcpkg-configuration.json`, or `scripts\\update-depedencies.ps1`. Use when Codex touches project files, solution metadata, dependency manifests, or file lists for either project."
---

# AudioLight MSBuild Maintenance

## Workflow

1. Read [maintenance-checklist.md](references/maintenance-checklist.md) before editing project metadata.
   Identify whether the task is about file-list maintenance, build validation, dependency manifest changes, or custom registry baseline updates.
2. Treat project metadata as explicit source of truth.
   When adding, renaming, or removing source, header, resource, or icon files, update the affected `.vcxproj` and `.vcxproj.filters` together.
3. Validate in the right shell.
   Prefer Visual Studio or a Developer PowerShell for raw `msbuild` commands. A plain PowerShell session may not have `msbuild` on `PATH`.
4. Preserve repository conventions.
   Keep UTF-8 text, CRLF endings, and the current `x64-windows-static` vcpkg triplet.
5. Route functional code changes to the domain skill first.
   Use `$audiolight-streamer-runtime`, `$audiolight-preprocessor-pipeline`, or `$audiolight-iotcomm-stack` for the behavioral work, then return here for metadata and validation.

## References

- Read [maintenance-checklist.md](references/maintenance-checklist.md) before substantial project or dependency maintenance.
