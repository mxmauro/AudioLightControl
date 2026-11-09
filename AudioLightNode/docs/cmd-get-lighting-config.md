# CMD_GET_LIGHTING_CONFIG

`CMD_GET_LIGHTING_CONFIG` (`0x7003`) is an authenticated WebSocket command with an empty payload.

Its reply has no command-flags envelope: `uint8 version = 1`, `uint8 lightingCount`, then ordered lighting records. A record is type `1` (DMX), `2` (RGB5050), or `3` (WS281x); their binary record formats are unchanged.

The node reads encrypted `config/lighting` when present. Otherwise it reads only original V1 `general/config` as slot zero. GET never rewrites or removes legacy storage.
