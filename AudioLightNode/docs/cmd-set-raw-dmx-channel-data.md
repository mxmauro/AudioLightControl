# CMD_SET_RAW_DMX_CHANNEL_DATA

This document describes how client libraries should send `CMD_SET_RAW_DMX_CHANNEL_DATA` to write raw DMX bytes into the active DMX universe.

## Command

- Command id: `4`
- Symbol in firmware: `CMD_SET_RAW_DMX_CHANNEL_DATA`
- Supported transports:
  - WebSocket custom command
  - UDP custom command
- Request payload:
  1. `uint16` start channel, big-endian
  2. `uint16` data length, big-endian
  3. `data length` raw DMX bytes

## Semantics

- This command writes raw channel values directly into the current 128-channel DMX universe.
- It is only supported when the active output controller type is `dmx`.
- Channels are 1-based.
- `start channel` must be in the inclusive range `1..128`.
- `data length` must be in the inclusive range `1..128`.
- `start channel + data length - 1` must not exceed `128`.

## Behavior on unsupported or inactive states

The firmware silently ignores the command when any of the following is true:

- the active output controller is not DMX
- fixed IR color override is active
- device power state is off
- runtime enqueue/apply fails after packet validation

Clients should treat this command as best-effort and should not expect an explicit success reply.

## Behavior on invalid packets

The firmware closes the session only when the packet itself is invalid.

Invalid packet conditions are:

- payload too short or too long for the declared fields
- extra trailing bytes after the declared payload
- `data length == 0`
- `data length > 128`
- `start channel == 0`
- `start channel + data length - 1 > 128`

For WebSocket, invalid packets cause a close with:

- close code: `WS_CLOSE_INVALID_PAYLOAD`
- reason text:
  - `SET RAW DMX CHANNEL DATA command: Invalid packet`
  - or `SET RAW DMX CHANNEL DATA command: Invalid channel range`

UDP clients should still validate locally before sending, because there is no reliable session-close signal.

## Parsing / encoding notes

- All multibyte integers are big-endian.
- The raw DMX byte array is not terminated and is not length-prefixed beyond the explicit `uint16 data length` field.
- This command is independent from fixture definitions returned by `CMD_GET_DEVICE_CONFIG`; it writes absolute DMX channel values.

## Example

To write three bytes starting at DMX channel `10`:

- start channel: `0x000A`
- data length: `0x0003`
- data bytes: `0xFF 0x80 0x00`

Wire payload:

`00 0A 00 03 FF 80 00`
