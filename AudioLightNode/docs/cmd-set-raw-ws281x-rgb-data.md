# CMD_SET_RAW_WS281X_RGB_DATA

`CMD_SET_RAW_WS281X_RGB_DATA` (`5`) works over WebSocket and UDP. Its payload is:

1. `uint8` target lighting slot, or `0xFF` for all active lighting slots
2. big-endian `uint16` first LED, starting at `1`
3. big-endian `uint16` LED count
4. exactly `LED count × 3` RGB bytes

RGB data is ordered red, green, blue for each LED. The node converts it to the fixed WS2812/WS2812B GRB wire order.
For each selected strip, data past its configured LED count is truncated; data starting past its LED count is discarded.
A targeted non-WS281x slot is a valid no-op; all-target applies to every compatible WS281x slot.

Raw frames cancel an active fade. A later fade interpolates every currently displayed pixel toward its uniform target.
Commands are ignored while power is off or a fixed IR color override is active. Invalid WebSocket payloads close the
session with `WS_CLOSE_INVALID_PAYLOAD`; UDP clients must validate locally.
