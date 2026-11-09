# CMD_SET_RAW_DMX_CHANNEL_DATA

`CMD_SET_RAW_DMX_CHANNEL_DATA` (`4`) works over WebSocket and UDP. The payload is:

1. `uint8` target lighting slot, or `0xFF` for all active lighting slots
2. `uint16` start channel, big-endian
3. `uint16` data length, big-endian
4. exactly `data length` raw bytes

Targets must be active lighting slots. Channels are 1-based. The whole payload must be consumed. Data that extends past channel 128 is truncated; data starting after channel 128 is discarded.

For a targeted non-DMX slot, the command is a valid no-op. An all-target command applies to every compatible DMX lighting slot (at most one is supported). Commands are also ignored while power is off or a fixed IR color override is active. Invalid packet or target closes a WebSocket session with `WS_CLOSE_INVALID_PAYLOAD`; UDP clients must validate locally.
