# AudioLightControl

This repository contains an audio-reactive lighting system split into a Windows host application suite and ESP32 device
firmware.

## Projects

* `AudioLightSuite`: Windows-side tools built with Visual Studio.
  * `Streamer`: Captures or prepares audio-driven lighting data and sends it to ESP32 devices.
  * `Preprocessor`: Analyzes audio files offline and generates precomputed data for later playback or streaming.

* `AudioLightNode5050`: ESP-IDF firmware for an ESP32-C3 device that receives lighting data and drives a 5050 LED strip.

## High-Level Flow

The `AudioLightSuite` applications run on Windows and act as the host side of the system. They either process audio offline or generate lighting control data in real time, then send that data to one or more ESP32 nodes.

The `AudioLightNode5050` firmware runs on the target device and applies the received data to the connected LED strip.

## License

[MIT](./LICENSE)
Copyright (C) 2026, Mauro H. Leggieri
