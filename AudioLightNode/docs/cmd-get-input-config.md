# CMD_GET_INPUT_CONFIG

`CMD_GET_INPUT_CONFIG` (`0x7001`) is an authenticated WebSocket command with an empty payload.

Its reply has no command-flags envelope: `uint8 version = 1`, reset GPIO, and IR GPIO. Both GPIOs use `0xFF` for `GPIO_NUM_NC`.

Input configuration comes only from encrypted `config/input`. If absent, both GPIOs are reported as `0xFF`.
