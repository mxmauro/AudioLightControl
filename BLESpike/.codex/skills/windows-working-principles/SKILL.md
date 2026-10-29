---
name: windows-working-principles
description: "Apply shared engineering principles when planning, reviewing, or changing Windows C++ projects."
---

# Windows Working Principles

Use this skill for Windows C++ work that benefits from a deliberate change plan or review. For handwritten C++ source and
headers, also use `$windows-cpp-style`.

- State material assumptions and real tradeoffs. Ask for direction when a missing choice would materially alter behavior or scope.
- Make the smallest change that solves the requested problem. Do not introduce speculative abstractions, configurability, or
  cleanup beyond the task.
- Preserve unrelated user changes in a dirty worktree. Restrict the diff to requested behavior, direct tests, and required
  project/documentation updates; remove only code made unused by the change.
- Define proportionate verification before editing. Prefer focused checks that demonstrate the changed behavior; broaden the
  build or test scope only when coupling warrants it.
- Respect the repository's layers and public/internal boundaries. Do not modify vendored code, generated output, key material,
  user-local state, or dependency directories unless the task explicitly requires it.
- Keep lifecycle changes coherent: initialize dependencies before consumers and release consumers before their dependencies.
- Preserve established text encoding, wide-character Win32 boundaries, user-facing error reporting, and public error-handling
  conventions unless the task calls for a deliberate design change.
- Read the local repository guidance and use the narrowest available domain skill for specialized work.
