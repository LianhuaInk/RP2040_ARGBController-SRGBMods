# Changelog

## 2.1.3

- Keep boot lighting off for three seconds before progressive full-white animation.
- Clear physical LEDs at startup; complete host frames still take over immediately.
- Do not block USB processing or control acknowledgements during the delay.

## 2.1.2

- Increase boot animation peak white brightness from 64 to 255.
- Preserve progressive boot animation and interruption by the first full frame.

## 2.1.1

- Receive CDC messages and send responses without requiring DTR.
- Use bounded, nonblocking TinyUSB writes for control replies.
- Preserve USB suspension handling and HID fallback.

## 2.1.0

- Whole-frame RGB888 over USB CDC, with CRC16 framing and control handshake.
- Host and device timing diagnostics, partial-write handling and bounded buffers.
- Compatible plugin syntax for the observed SignalRGB runtime.
