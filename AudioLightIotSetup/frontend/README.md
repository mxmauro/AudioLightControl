# AudioLight IoT Setup

A browser-based setup console for AudioLight IoT devices. It uses Svelte 5, Skeleton.dev, and the encrypted browser client from
`@mxmauro/iot-comm.js`.

## Development

```sh
npm run dev
npm run check
npm run build
```

Enter the device host, user name, and Base64 private key to connect. The private key is retained only in memory for the active
session. The last host and a trusted device fingerprint are stored locally; the first connection requires confirmation and an
unexpected fingerprint change is rejected. Use **Forget device trust** before intentionally replacing a device at the same host.

## Supported setup actions

- Configure reset-button and IR-receiver GPIO inputs, including disconnected inputs.
- Configure up to two ordered lighting outputs: RGB5050, WS281x strips with an optional mirror output, and at most one 128-channel DMX universe with up to 16 fixtures.
- Validate GPIO assignments across input and lighting settings, require confirmation before saving lighting settings, then reboot explicitly to activate saved changes.
- Set the device mDNS hostname and manage users, including credential creation, reset, deletion, and self-service credential change.

Board-specific GPIO availability remains firmware-controlled and is reported as a device error.

## iot-comm.js package note

The frontend temporarily uses the local `mxmauro-iot-comm.js-0.3.1.tgz` package from the shared JavaScript libraries directory until
that release is published. It supplies the AudioLightNode V2 input, lighting, raw DMX, raw WS281x, and reboot commands.
