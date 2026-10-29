# CMD_SET_DEVICE_CONFIG

Sets the persistent lighting configuration and applies it immediately.

## Command

- Command id: `0x7002`
- Transport: WebSocket custom command only
- Authorization: administrator session required

The payload is binary, versioned, and stored as one encrypted NVS blob. It is not JSON; `config-sample.json` is human-facing documentation only.

## Payload version 1

1. `uint8` version: `1`
2. `uint8` output type: `0` none, `1` DMX, `2` RGB5050
3. Output-specific payload, if output type is not `none`

The IR receiver GPIO is firmware-defined and is not included in this payload.

The output configuration is encoded and validated by the selected output controller. For RGB5050, append its payload: three one-byte GPIO values and an active-low flag.

For DMX, append its payload: universe size (`128`), one-byte TX GPIO, fixture count, then each fixture's null-terminated type, start channel, channel count, and coloring entries. Fixture `type` is persisted descriptive metadata; it does not select output behavior. Dynamic coloring entries and the client pipe-delimited representation are defined in [DMX Fixture Channel Configuration](dmx-fixture-configuration.md).

`CMD_GET_DEVICE_CONFIG` returns this complete versioned payload after its reply header.

GPIO values `0x00` through `0x7F` are valid wire values. `0xFF` represents `GPIO_NUM_NC`, but configured output GPIOs cannot be disabled. GPIOs must be valid, distinct where required, and cannot conflict with firmware-defined pins or another configured function.

## Result

The firmware validates the entire packet before writing. On success it commits the configuration to encrypted NVS, stops the old output runtime, initializes the new one, clears temporary color overrides, and replies with `ESP_OK`.

Malformed data closes the WebSocket session with `WS_CLOSE_INVALID_PAYLOAD`. Non-admin sessions and storage/runtime failures receive an error reply. UDP does not support this command.
