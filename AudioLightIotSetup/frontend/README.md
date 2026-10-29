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

- Configure no output, RGB5050 GPIO output, or a 128-channel DMX universe with up to 16 fixtures.
- Validate protocol limits before submission and require confirmation before applying persistent lighting settings.
- Set the device mDNS hostname and manage users, including credential creation, reset, deletion, and self-service credential change.

Board-specific GPIO availability remains firmware-controlled and is reported as a device error.

## iot-comm.js package note

The published `@mxmauro/iot-comm.js@0.1.5` browser export points at filenames that are not present in its package. `vite.config.ts`
contains a temporary alias to the bundled browser module and `src/types/iot-comm.d.ts` supplies its declarations. Remove both after
upgrading to the corrected library release.
