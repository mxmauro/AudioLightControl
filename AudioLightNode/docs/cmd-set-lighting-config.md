# CMD_SET_LIGHTING_CONFIG

`CMD_SET_LIGHTING_CONFIG` (`0x7004`) is a WebSocket administrator command. Its complete payload is the version-1 lighting section returned by [CMD_GET_LIGHTING_CONFIG](cmd-get-lighting-config.md): version, lighting count, and ordered lighting records. Truncated, trailing, or invalid data is rejected. WS281x records use type `3`, primary data GPIO, optional mirror GPIO, and a big-endian LED count from `1` through `300`.

The node merges the lighting section with current inputs and validates all GPIO roles, the onboard LED, and DMX constraints before writing encrypted `config/lighting`. After that commit succeeds, it best-effort removes V1 `general/config`; cleanup failure does not change the success reply. The node replies `ESP_OK`; send `CMD_REBOOT` to apply the saved configuration.
