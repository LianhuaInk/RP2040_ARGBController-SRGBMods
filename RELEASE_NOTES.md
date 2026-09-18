# CDC 2.1.2 - RGB888 and full-brightness boot

Assets:

- `Pico_SRGB_CDC_v2_1_2_RP2040.uf2`: Raspberry Pi Pico firmware.
- `Pico_SRGB_CDC_v2_1.js`: matching SignalRGB CDC plugin.
- `SHA256SUMS.txt`: asset checksums.

Seven outputs, GPIO12-18; counts 24/40/32/20/48/12/25, totaling 201 LEDs.
Only flash this configuration onto matching RP2040 hardware.

Changes: full-brightness progressive boot white, DTR-independent CDC handshake
and RGB888 whole-frame transport. The user reports about 50 FPS; performance
is not guaranteed. Requested 120 FPS was observed being applied as 60 by
SignalRGB 2.5.74. Leave target at 60 for this runtime.

Exit SignalRGB before flashing. Disable matching HID plugins when using CDC.
Provide adequate external LED power, especially for full-white boot lighting.
See README.md for installation, build and test instructions.

Licensing status: upstream authorization remains unconfirmed. This release
does not assert a project-wide MIT/GPL license. See NOTICE.md.
