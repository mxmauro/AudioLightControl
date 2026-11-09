# Client configuration update: split input and lighting

Clients must replace the combined device-configuration commands with five WebSocket commands. Use one authenticated
session for the complete read or update flow.

| Command | Value | Access | Request | Reply |
| --- | --- | --- | --- | --- |
| GET input | `0x7001` | authenticated | empty | input section |
| SET input | `0x7002` | administrator | input section | normal success/error reply |
| GET lighting | `0x7003` | authenticated | empty | lighting section |
| SET lighting | `0x7004` | administrator | lighting section | normal success/error reply |
| REBOOT | `0x7005` | administrator | empty | normal success/error reply |

GET replies contain the section directly. They do not include the former command-flags envelope or a combined
configuration wrapper.

## Input section

Input is exactly three bytes:

1. `uint8` version: `1`
2. `uint8` reset-button GPIO
3. `uint8` IR-receiver GPIO

`0xFF` represents `GPIO_NUM_NC`; other GPIO values are encoded as `0x00..0x7F`. Clients should send the complete
section for SET, including both GPIO values.

## Lighting section

The lighting section is:

1. `uint8` version: `1`
2. `uint8` lighting count: `0..2`
3. one ordered lighting record for each active slot

Each record starts with `uint8` type:

- `1`: DMX
- `2`: RGB5050
- `3`: WS281x

RGB5050 follows with red GPIO, green GPIO, blue GPIO, and `uint8 activeLow` (`0` or `1`).

WS281x follows with primary data GPIO, mirror data GPIO, and big-endian `uint16` LED count (`1..300`). The mirror
GPIO is `0xFF` when unused; otherwise it emits an exact duplicate of the primary strip frame and is not separately
targetable. WS281x client data is RGB, while the node emits WS2812/WS2812B GRB wire data.

DMX follows with big-endian `uint16 universeSize` (`128`), TX GPIO, `uint8 fixtureCount`, then each fixture:

1. NUL-terminated fixture type string
2. big-endian `uint16` start channel
3. `uint8` channel count
4. one big-endian `uint16` coloring value per channel

The fixture coloring values and constraints remain documented in
[DMX Fixture Channel Configuration](dmx-fixture-configuration.md).

## Client update flow

Read input and lighting separately. To change either section, send its complete current or edited section; do not
send a partial patch. The firmware merges the submitted section with the other current section, validates the
composed configuration, and replies success. The active runtime configuration remains unchanged until reboot.

After one or more successful SETs, send `CMD_REBOOT` with an empty payload to activate the saved configuration.
It acknowledges success before restarting; reconnect only after that command succeeds. A non-administrator receives
an authorization error. Truncated, trailing, or malformed section payloads cause WebSocket rejection.

Color, fade, set-and-fade, raw-DMX, and raw-WS281x commands retain their target byte, but it now denotes a lighting slot.

## Storage and legacy behavior

This is firmware behavior that clients should account for during migration:

- Input uses encrypted NVS `config/input`.
- Lighting uses encrypted NVS `config/lighting`.
- Original V1 `general/config` is lighting-only fallback when `config/lighting` is absent.
- A device using only V1 legacy storage reports input GPIOs as `0xFF` and its legacy lighting record as slot zero.
- GET and boot never migrate, rewrite, or remove the legacy record.
- A successful input SET writes only `config/input`.
- A successful lighting SET writes `config/lighting` and then best-effort removes legacy `general/config`.

The former combined `GET_DEVICE_CONFIG` and `SET_DEVICE_CONFIG` commands are no longer available.
