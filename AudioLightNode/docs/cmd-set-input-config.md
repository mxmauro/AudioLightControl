# CMD_SET_INPUT_CONFIG

`CMD_SET_INPUT_CONFIG` (`0x7002`) is a WebSocket administrator command. Its complete payload is the version-1 input section returned by [CMD_GET_INPUT_CONFIG](cmd-get-input-config.md). Truncated, trailing, or invalid data is rejected.

The node merges it with current lighting, validates all GPIO roles and the onboard LED, then writes only encrypted `config/input`. It never changes legacy `general/config`. The node replies `ESP_OK`; send `CMD_REBOOT` to apply the saved configuration.
