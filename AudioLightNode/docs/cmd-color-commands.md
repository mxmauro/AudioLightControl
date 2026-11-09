# Targeted color commands

`CMD_SET_COLOR` (`1`), `CMD_FADE_TO_COLOR` (`2`), and `CMD_SET_AND_FADE_TO_COLOR` (`3`) work over WebSocket and UDP. Each payload now starts with `uint8 target`.

`target` is an active lighting-slot index (`0` or `1`) or `0xFF` for every active lighting slot. Inactive indexes, truncated payloads, and trailing bytes are invalid. The remaining fields retain their former big-endian layout:

- set: target, `uint16 R`, `uint16 G`, `uint16 B`
- fade: target, `uint16 R`, `uint16 G`, `uint16 B`, `uint16 milliseconds`
- set-and-fade: target, start RGB, end RGB, `uint16 milliseconds`

Global power and IR override behavior is unchanged. An all-target command updates every active lighting slot; each slot retains its own current color for targeted commands.

For a WS281x slot, these commands address the entire strip. Set color is uniform immediately; fade-to interpolates
each currently displayed pixel, including a prior raw RGB frame, toward the requested uniform color.
