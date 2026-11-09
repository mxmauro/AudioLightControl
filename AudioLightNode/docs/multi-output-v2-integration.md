# Split input and lighting integration handoff

Use one authenticated WebSocket session for four commands: GET/SET input (`0x7001`/`0x7002`) and GET/SET lighting (`0x7003`/`0x7004`). Each GET returns only its version-1 section: input is reset GPIO and IR GPIO; lighting is a count and ordered lighting records.

Input is stored only as encrypted `config/input`. Lighting is stored as encrypted `config/lighting`. Original V1 `general/config` is read-only lighting fallback when the new lighting key is absent; boot and GET do not mutate it. A successful lighting SET removes it best-effort after the new record commits.

Color, fade, set-and-fade, and raw-DMX commands lead with a target: an active slot index or `0xFF` for all. Raw DMX against RGB5050 is a valid no-op. mDNS reports subtype `multi` when two slots are active.

AudioLightIotSetup should manage reset/IR separately from the ordered lighting array. AudioLightSuite should retain node identity once and use the four commands over one shared authenticated session.
