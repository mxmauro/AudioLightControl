# CMD_GET_DEVICE_CONFIG

This document describes how client libraries should send `CMD_GET_DEVICE_CONFIG` and parse the reply emitted by the current firmware.

## Command

- Command id: `0x7001`
- Transport: WebSocket custom command only
- Authorization: any WebSocket session
- Request payload: empty

## Reply header

All replies start with:

1. `uint8` reply version
2. `uint32` command flags, big-endian
3. complete versioned configuration payload accepted by `CMD_SET_DEVICE_CONFIG`

Current constants:

- Reply version: `1`
- Command flags:
  - `0x00000001`: set color
  - `0x00000002`: fade to color
  - `0x00000004`: set and fade
  - `0x00000010`: get config
- `0x00000020`: set DMX channel data
- `0x00000040`: set device configuration (`CMD_SET_DEVICE_CONFIG`, WebSocket admin only)

## Lighting configuration payload

After the header, the reply contains the complete `CMD_SET_DEVICE_CONFIG` version-1 payload:

1. `uint8` configuration version (`1`)
2. `uint8` output controller type: `0` none, `1` DMX, `2` RGB5050
3. output-specific payload

The IR receiver GPIO is firmware-defined and is not included in the configuration payload.

## RGB5050 output payload

When the embedded `output controller type == 2`, the payload is:

1. `uint8` red GPIO pin
2. `uint8` green GPIO pin
3. `uint8` blue GPIO pin
4. `uint8` active low flag

GPIO pins use `0xFF` to represent `GPIO_NUM_NC`. Values from `0x00` through `0x7F` are GPIO wire values.

## DMX output payload

When the embedded `output controller type == 1`, the payload is:

1. `uint16` DMX universe size, big-endian
2. `uint8` TX GPIO pin
3. `uint8` fixtures count
4. repeated fixture entries

Current universe size is always `128`.

### DMX fixture entry

Each fixture entry is encoded as:

1. `type` string encoded as bytes terminated by `0x00`
2. `uint16` start channel, big-endian
3. `uint8` channels count
4. `channels count` coloring entries as `uint16`, big-endian

### DMX coloring entry values

A coloring entry is either a fixed DMX value from `0x0000` through `0x00FF` or one of the dynamic fixture-channel values defined in [DMX Fixture Channel Configuration](dmx-fixture-configuration.md). That document also defines the pipe-delimited client representation.

## Parsing notes

- Strings are null-terminated, not length-prefixed.
- All integers larger than one byte are big-endian.
- For output type `none`, the output-specific payload is empty.
- Clients should parse the embedded versioned configuration payload exactly as they would for `CMD_SET_DEVICE_CONFIG`.
- For DMX fixtures, `channels count` tells how many `uint16` coloring entries follow.

## Example DMX fixture

A fixture configured as:

- type: `PAR 54x3W RGB (3CH)`
- start channel: `1`
- coloring: `Color.Red|Color.Green|Color.Blue`

is returned as:

1. bytes for `PAR 54x3W RGB (3CH)`
2. terminating `0x00`
3. `0x0001` start channel
4. `0x03` channels count
5. `0x0102 0x0104 0x0106`
