---
name: windows-cpp-style
description: "Apply a consistent Win32 C++ style when editing handwritten C++ source or headers; follow local API conventions."
---

# Windows C++ Style

Use this skill for handwritten C++ source and headers. Follow repository formatting guidance for other file types, and preserve
module-specific public names instead of renaming them for consistency.

## Layout and declarations

- Use UTF-8 with CRLF endings, four spaces, no trailing whitespace, a final newline, and lines of at most 140 characters
  unless effectively unbreakable.
- Use Allman braces for declarations and control flow. Always brace `if`, `else`, `for`, `while`, and `do` bodies. Keep the
  opening brace on the same line only for `typedef struct` and `typedef enum` declarations.
- Keep compact namespaces (`namespace X {`) without scope-only indentation and comment each namespace closing brace.
- Keep one blank line between function definitions. Use blank lines and the existing separator comments for logical sections,
  but avoid excess vertical whitespace.
- Keep calls, declarations, and expressions on one line whenever they fit. When wrapping is necessary, align continuation text
  one character after the outer opening parenthesis; keep binary operators with the preceding expression.
- Declare file-local helpers as `static` near the beginning of the translation unit, before the main function bodies. Prefer
  explicit types; use `auto` only where the deduced iterator or init-statement type is clear.

## Windows conventions

- Follow the codebase's established Win32 types and APIs. When it uses `HRESULT`, `BOOL`, SAL annotations, or wide-character
  APIs, preserve those boundaries and their associated success/failure conventions.
- Preserve local type, class, member, parameter, and variable naming conventions. Do not rename public or established symbols
  merely to make them look more modern.
- Keep headers in the established local/project, platform, and external/STL ordering. Do not reorder or modernize surrounding
  code without a task reason.

## Expressions, comments, and control flow

- Use parentheses only where they clarify precedence: add them for mixed `&&`/`||` chains and unary negation inside larger
  expressions, but not for obvious homogeneous expressions.
- Prefer early validation and failure returns. Initialize variables at declaration or immediately before first use.
- Preserve `switch` formatting: indent `case` labels inside the switch and put each `break;` on its own line.
- Write sparse functional comments that describe intent, ownership, constraints, or section purpose. Do not narrate obvious
  individual statements.
- Match the surrounding file and avoid style-only rewrites outside the code directly touched by the task.
