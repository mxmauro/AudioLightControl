---
name: audiolight-msbuild-maintenance
description: "Maintain AudioLightSuite MSBuild project maps, build targets, and vcpkg registry metadata."
---

# AudioLight MSBuild Maintenance

## Workflow

1. Use `$windows-msbuild-maintenance` for file-list, build validation, dependency, and metadata rules.
2. Read [maintenance-checklist.md](references/maintenance-checklist.md) before changing AudioLightSuite project metadata or dependencies.
3. Preserve the repository's UTF-8/CRLF conventions and `x64-windows-static` triplet.
4. Route functional code changes to the domain skill first.
   Use `$audiolight-streamer-runtime`, `$audiolight-preprocessor-pipeline`, or `$audiolight-iotcomm-stack` for behavioral
   work, then return here for metadata and validation.

## References

- Read [maintenance-checklist.md](references/maintenance-checklist.md) for the solution map, build targets, and dependency update script.
