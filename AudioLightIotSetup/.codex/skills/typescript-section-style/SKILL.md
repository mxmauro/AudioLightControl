---
name: typescript-section-style
description: Organize and format TypeScript files and Svelte lang="ts" script blocks using the project's declaration and block-style conventions.
metadata:
  short-description: Organize TypeScript declaration sections
---

# TypeScript section style

Apply this style when creating or reorganizing TypeScript files and Svelte `<script lang="ts">` blocks that use the section-based
layout. Keep the change structural: preserve behavior, public APIs, imports, exports, and import behavior.

## Formatting and blocks

Follow the repository's Biome formatting: tabs, LF endings, no trailing whitespace, single quotes, and a 140-column line width.
Split code that would exceed 140 columns unless an indivisible value, such as a long literal, cannot be split without harming clarity
or behavior.

Use brace blocks for named arrow functions and every control-flow body, including `if` / `else`, loops, and `try` / `catch` / `finally`.
Do not use a single statement as an unbraced control-flow body.

Inline callback arrows may use a concise expression body when it is one clear operation, such as a Svelte event handler or an array or
promise callback. Named functions, callbacks with control flow, and callbacks with multiple operations must use a brace block.

```ts
const save = (): void => {
	if (connected) {
		connect();
	}
};

button.onclick = () => connect();
items.map((item) => item.id);
```

## Declaration layout

Order declarations as follows. Omit an empty section and the separator that would exist only for it.

1. Imports and re-exports.
2. One blank line.
3. Private, static define-like constants.
4. An 80-column separator: `// -----------------------------------------------------------------------------`.
5. Types, interfaces, and classes, with exported declarations before private declarations.
6. The separator.
7. Exported constants, exported variables, private constants, then private variables.
8. The separator.
9. Arrow functions, with exported functions before private functions.

Review section boundaries after arranging declarations. A separator must have populated sections on both sides; remove separators that
would leave an empty section. Keep exactly one blank line between logical regions, and remove duplicate blank lines.

Keep arrow-function constants in the function section, never in the constants section. Use arrow syntax for every named function;
do not introduce `function` declarations.

```ts
const MAX_PACKET_BYTES = 1024;

// -----------------------------------------------------------------------------

export type Packet = { id: number };

// -----------------------------------------------------------------------------

export let packet: Packet | undefined;
const decoder = new TextDecoder();

// -----------------------------------------------------------------------------

export const decodePacket = (data: Uint8Array): string => decoder.decode(data);
```

For reactive Svelte state, keep `$state`, `$derived`, and related reactive declarations in the constants/variables section according
to their visibility and declaration kind. Put event handlers, async actions, and other arrow callbacks in the function section.

```svelte
<script lang="ts">
	let pending = $state(false);

	// -----------------------------------------------------------------------------

	const submit = async (): Promise<void> => {
		pending = true;
	};
</script>
```

## Intent comments and internal sections

Use concise `//` comments to state the intent of non-obvious code sections. Add a comment for each distinct phase in a longer
function (for example: parse JSON, validate the decoded shape, then check duplicate identifiers). Keep each comment to one or two
lines, describing the outcome or reason rather than restating syntax.

Separate those phases with one blank line. Do not add comments to simple, self-evident assignments, getters, or one-step helpers;
comments should make multi-stage data flow, validation, persistence, or UI state transitions easier to scan.

## Preservation

Preserve tabs in TypeScript and Svelte script blocks, LF endings, no trailing whitespace, and practical 140-column lines. Do not
change behavior merely to satisfy ordering: move declarations only when their initialization, temporal-dead-zone behavior, exports,
or import evaluation behavior remain intact.
