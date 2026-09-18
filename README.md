# RP2040 ARGB Controller - SignalRGB CDC

Firmware version: **2.1.2**. An unofficial SRGBmods LC v1-derived RP2040
controller firmware and matching SignalRGB plugin, using USB CDC for RGB888.

## Hardware configuration

Raspberry Pi Pico (RP2040), WS2812B-compatible GRB LEDs, seven outputs:

| GPIO | LEDs |
| --- | --- |
| 12 | 24 |
| 13 | 40 |
| 14 | 32 |
| 15 | 20 |
| 16 | 48 |
| 17 | 12 |
| 18 | 25 |

201 logical LEDs, one SignalRGB channel, concatenated in the order above.
NeoPXL8 outputs are parallel, with a 48-pixel physical stride and one unused
eighth output. This firmware is not a configurable universal board image.

Provide adequate external LED power, a common ground and appropriate signal
level shifting. The boot animation uses full-brightness white (255); verify
the power supply and wiring can support that load. Do not power 201 LEDs
through the Pico USB supply.

## Install

1. Exit SignalRGB and any serial monitor.
2. Hold BOOTSEL while connecting the Pico USB cable.
3. Copy `dist/Pico_SRGB_CDC_v2_1_2_RP2040.uf2` onto the RPI-RP2 drive.
4. Put `plugin/Pico_SRGB_CDC_v2_1.js` in your SignalRGB user Plugins folder
   (`Documents/WhirlwindFX/Plugins` on Windows).
5. Remove matching old HID plugins from the scanned Plugins folders. Do not
   let HID and CDC plugins control this device at the same time.
6. Restart SignalRGB and configure components on Channel 1 (201 LED limit).

Expected log: `CDC connected: firmware 2.1.2`. A USB serial port must enumerate.
The firmware retains HID interface 2 for backwards compatibility; the CDC
plugin selects data interface 1.

## Performance and diagnostics

The user reports approximately 50 FPS with full RGB888 over CDC, compared with
approximately 26 FPS for uncompressed HID and 36 FPS for compressed HID on the
same setup. These are setup-specific observations, not guaranteed benchmarks.

The observed SignalRGB 2.5.74 runtime applies a requested target of 120 as
60 FPS / 16ms. Use Target FPS 60. `setFrameRateTarget` is a development API and
may change. Enable Log Host Timings and Log Device FPS for 10-second summaries.
The first device sample establishes a baseline. Device complete/displayed FPS
are distinct from plugin render-call frequency. The effect itself may be paused
or update less frequently than Render calls.

CDC sends a complete 603-byte RGB payload in one normal host write call.
The USB stack still splits it into physical packets. Partial writes are resumed
without interleaving messages; no queue of obsolete animation frames is built.
This CDC version does not implement 4-bit compression. USB-native CDC baud
settings are not a UART throughput limit.

## Build

Validated toolchain:

- Arduino-Pico 5.7.0 (`rp2040:rp2040`)
- Adafruit TinyUSB Library 3.7.7
- Adafruit NeoPXL8 1.4.1
- Adafruit NeoPixel 1.15.5 and their dependencies

Install these in your Arduino environment. Add the Arduino-Pico board index:
`https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`.

In Arduino IDE select Raspberry Pi Pico, Adafruit TinyUSB USB stack and
optimization `-O2`. Open the sketch under `firmware/Pico_SRGB_CDC_v2_1`.
Keep the sketch and folder names identical; the folder name reflects the
protocol family, while `Version` in the sketch is 2.1.2.

With Arduino CLI:

```sh
arduino-cli compile --fqbn rp2040:rp2040:rpipico:usbstack=tinyusb,opt=Optimize2 --build-path build firmware/Pico_SRGB_CDC_v2_1
```

PowerShell users can run `./scripts/build.ps1 -ArduinoCli <path-to-arduino-cli>`.
No build script flashes hardware automatically.

## Test

Node.js and a C++17 compiler are required. On Windows, run from an x64 Visual
Studio Developer terminal so `cl` is available. Linux/macOS require `c++`.

```sh
node tests/run.cjs
```

Tests exercise the actual plugin and actual firmware loop with mocked hardware:
RGB888 and tail clearing, handshake validation, fragmented messages, CRC
rejection/recovery, partial writes, DTR=false communication, full-brightness
boot output, busy-output frame replacement, USB disconnection/suspend and
recovery after 100,000 garbage bytes. They do not replace hardware testing.

## Protocol

Message: `PRGB | version:u8 | type:u8 | id:u16LE | length:u16LE | payload | crc:u16LE`.
Envelope version 1. Type 1 is RGB888 (603 bytes), type 2 is control request
(64 bytes), type 3 is response (64 bytes). CRC16-CCITT-FALSE uses polynomial
0x1021 and initial value 0xffff, covering header and payload.

Only complete, validated frames are published. A partial message expires after
250ms. The firmware reads at most 1024 bytes per loop batch. CDC communication
does not depend on DTR; response writes use nonblocking TinyUSB functions and
respect FIFO capacity. USB suspend requests a blackout. Boot lighting is
interrupted by the first complete frame. EEPROM writes are deferred until idle.
Shutdown color transmission is best effort if a partial packet is still pending.

## Attribution and licensing status

## GitHub release publishing

The tag-triggered workflow tests this source and publishes the included UF2,
matching JS, checksums and NOTICE.md. It does not rebuild the firmware in CI.
The checked-in UF2 was compiled locally from the included 2.1.2 source.
Future firmware changes require updating the binary and checksum list before
tagging. Enable Actions for the repository and push a version tag, such as
`v2.1.2`, after uploading the main branch. No personal access token is stored
in this repository; the workflow uses GitHub's built-in token.

## Attribution and licensing status

Based on [SRGBmods LC v1](https://srgbmods.net/lcv1/), associated with
FeuerSturm / SRGBmods. This is not an official SRGBmods release. See NOTICE.md.

**Upstream firmware/plugin licensing has not been confirmed. No MIT or GPL
license is asserted for the project as a whole.** Public source availability
does not establish redistribution permission. Clarify upstream authorization
before public redistribution. Dependencies retain their own licenses.

The included binary must be distributed together with its corresponding source
and applicable third-party notices when redistribution is authorized.
