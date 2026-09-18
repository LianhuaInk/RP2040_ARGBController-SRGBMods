# Attribution and licensing review

Upstream: SRGBmods LED Controller v1, https://srgbmods.net/lcv1/.
The original plugin identifies its publisher as FeuerSturm.

The local starting files were:

- SRGBmods_LC_v1.js
- SRGBmods_LED_Controller_v1_1789384559.ino

The checked source files and LC v1 web page did not contain an explicit
project-level license. SRGBmods/public has a GPL-2.0 license, but its coverage
of the separately generated LC v1 firmware has not been established.
SRGBmods/LEDControllerV2 likewise cannot establish permission for LC v1.
No permission is inferred from these other repositories.

This package contains modified firmware, a matching plugin and new tests.
Changes include bounded frame assembly, CRC-protected whole-frame CDC transport,
control acknowledgements and counters, nonblocking boot/hardware scheduling,
idle EEPROM writes, DTR-independent communication and full-brightness boot white.
The upstream image asset and bundled Arduino tools are not included.

External dependencies are not vendored. Arduino-Pico, Adafruit TinyUSB,
Adafruit NeoPXL8, Adafruit NeoPixel and transitive libraries keep their own
copyright and license notices. Review the installed dependency distributions
for binary-redistribution requirements; this document does not replace them.

Licensing review is pending. No new project-wide license or claim of ownership
over upstream code is made by this package. Do not describe it as MIT-licensed
or GPL-licensed without establishing the applicable upstream permissions.
