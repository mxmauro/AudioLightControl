# AudioLight IoT Setup Agent Notes

## Scope
- These instructions apply to the `frontend/` directory and its descendants.
- This directory contains the Wails frontend: a Svelte 5, TypeScript, Vite, Tailwind, and Skeleton.dev application.
- Run JavaScript/TypeScript package commands from `frontend/`. Wails and Go application code resides at the repository root.
- Do not touch unrelated user changes. This repository may be dirty.

## Working principles
- Make the smallest change that solves the requested setup workflow. Do not add routing, stores, persistence, or dependencies without a concrete need.
- Keep protocol encoding, decoding, validation, and command IDs outside Svelte components. UI components should consume typed application data.
- Preserve the existing formatting: LF endings, UTF-8, tabs in TypeScript and Svelte script blocks, no trailing whitespace, and lines at most 140 characters where practical.
- Never put a function, callback, control-flow, or `try`/`catch` block on one line. Put the opening brace after the condition or
  signature, the body on following indented lines, and the closing brace on its own line even for a single statement.
- Prefer Skeleton components and utility classes before adding custom CSS. Keep interactive controls keyboard-accessible and associate inline errors with inputs.
- Form controls must be visually distinct from their containing card: use the shared `.input` and `.select` treatment, keep labels
  visible, and preserve the strong focus and invalid states defined in `src/app.css`.
- Follow Skeleton's Forms and Inputs pattern: load `@tailwindcss/forms`; page and dialog forms use
  `card bg-surface-100-900 p-4 w-full max-w-md mx-auto space-y-4` with a `header`, `.fieldset` / `.legend` for related controls,
  `.label` with a `.label-text` caption for each field, and a right-aligned footer for form actions. Use filled button presets for
  actions that must remain easy to find. Do not use legacy `variant-*` classes; use `preset-filled`,
  `preset-filled-surface-500`, `preset-filled-warning-500`, or `preset-filled-error-500` according to the action's meaning.

## Device protocol and security
- Treat all device data as untrusted. Reject malformed, unsupported, truncated, oversized, or trailing binary payloads; never guess missing fields.
- Keep protocol integers explicitly big-endian and check ranges before allocating or iterating. Match firmware limits for DMX fixture counts, channels, and UTF-8 byte lengths.
- Validate forms before a command is sent, but retain firmware validation as the authority for board-specific GPIO availability and ownership.
- Never log or persist private keys, raw public keys, session tickets, encrypted payloads, or command payloads. Private keys are session memory only.
- Use the library fingerprint callback. Store only the approved fingerprint associated with a normalized host; when a fingerprint changes,
  require an explicit warning-dialog confirmation before replacing the trusted-device entry.
- Disable duplicate submissions while a request is pending. On connection loss, clear session state and present a user-safe error without leaking protocol details.

## UI behavior
- Use clear loading, success, and failure states. Keep form values on command failure so users can correct and resubmit them.
- Destructive operations (user deletion, credential reset, and applying output configuration) require explicit confirmation with a concise summary.
- A device-required credential change takes precedence over all other workspace actions.

## Verification
- Run `npm run biome:check` to check frontend formatting and linting for every change.
- Run `npm run check` for every change and `npm run build` for release-ready work.
- Manually verify invalid connection data, first-use and changed fingerprint flows, reconnect/disconnect behavior, command errors, and RGB5050/DMX configuration boundaries.
- The temporary Vite alias for `@mxmauro/iot-comm.js` exists only until the fixed library package is released; remove it during that package upgrade.
