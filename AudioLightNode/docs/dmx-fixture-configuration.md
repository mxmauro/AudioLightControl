# DMX Fixture Channel Configuration

This document defines the fixture channel mapping used by `CMD_GET_DEVICE_CONFIG` and `CMD_SET_DEVICE_CONFIG`.

The device configuration payload remains version `1`. Each fixture channel is a big-endian `uint16` coloring entry. Values from `0x0000` through `0x00FF` are fixed DMX values; the dynamic entries are listed below.

| Client token | Binary value | Fixed-color output |
| --- | --- | --- |
| `Dimmer` or `Dimmer.Coarse` | `0x0100` | `255` |
| `Dimmer.Fine` | `0x0101` | `255` |
| `Color.Red` | `0x0102` | red component |
| `Color.Red16` | `0x0103` | red component |
| `Color.Green` | `0x0104` | green component |
| `Color.Green16` | `0x0105` | green component |
| `Color.Blue` | `0x0106` | blue component |
| `Color.Blue16` | `0x0107` | blue component |
| `Color.White` | `0x0108` | calculated white component |
| `Color.White16` | `0x0109` | calculated white component |
| `Color.UV` | `0x010A` | `0` |
| `Color.UV16` | `0x010B` | `0` |
| `Pan` or `Pan.Coarse` | `0x010C` | `0` |
| `Pan.Fine` | `0x010D` | `0` |
| `Tilt` or `Tilt.Coarse` | `0x010E` | `0` |
| `Tilt.Fine` | `0x010F` | `0` |
| `Strobe` | `0x0110` | `0` |

`*16` and `.Fine` channels are independent 8-bit DMX entries. When a fixed RGB color is rendered, they mirror their base family; they are not the low byte of a 16-bit value.

## Client text representation

Clients should represent fixture channels as pipe-delimited entries in DMX channel order, for example:

```text
Dimmer|Color.Red|Color.Green|Color.Blue|Strobe
```

For input, clients must accept the table tokens case-insensitively, trim surrounding ASCII whitespace from each entry, and accept a fixed channel value as exactly two hexadecimal digits without a `0x` prefix. Empty, malformed, and unknown entries must be rejected.

When presenting a configuration read from the device, clients should emit the canonical names in the table (`Dimmer`, `Pan`, and `Tilt`, rather than their `.Coarse` aliases) and uppercase two-digit hexadecimal values for fixed channels.

The pipe-delimited form is a client representation only. Clients convert it to and from the binary `uint16` fixture entries carried by the device commands.

## White calculation

The RGB source is first reduced to 8-bit DMX components. For a fixture containing either White entry:

- If it contains at least one RGB-family entry, the white output is `min(R, G, B)` and that amount is subtracted from all RGB outputs.
- If it contains no RGB-family entry, the white output is BT.709 grayscale, rounded to the nearest integer: `0.2126R + 0.7152G + 0.0722B`.

Without a White entry, RGB-family entries use the source RGB values directly. Fixed entries are always written unchanged.

## Compatibility

The binary payload layout and version remain unchanged, but `0x0100` through `0x0102` have new meanings. Clients must use this table when reading or writing DMX fixture configuration. Existing DMX configurations that use the former RGB map are intentionally not compatible.
