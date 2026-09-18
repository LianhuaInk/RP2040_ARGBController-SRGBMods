#pragma once
#include <stdint.h>
#include <string.h>

namespace FastRGB {
constexpr uint16_t LedCount = 201;
constexpr uint8_t FrameMagic = 0xA2;
constexpr uint8_t ControlMagic = 0xD2;
constexpr uint8_t ProtocolVersion = 1;

class FrameAssembler {
public:
  uint8_t pending[LedCount * 3] = {};
  bool hasPending = false;
  uint32_t complete = 0, invalid = 0, incomplete = 0, replaced = 0;

  void reset() {
    active = false;
    hasPending = false;
  }

  void publishRgb(const uint8_t *rgb) {
    if (active) ++incomplete;
    active = false;
    if (hasPending) ++replaced;
    memcpy(pending, rgb, sizeof(pending));
    hasPending = true;
    ++complete;
  }

  bool accept(const uint8_t *p, uint16_t length) {
    if (length != 64 || p[0] != FrameMagic || p[3] > 1) {
      ++invalid;
      active = false;
      return false;
    }
    const uint8_t capacity = p[3] ? 40 : 20;
    const uint8_t packets = (LedCount + capacity - 1) / capacity;
    if (p[2] >= packets) {
      ++invalid;
      active = false;
      return false;
    }
    if (p[2] == 0) {
      if (active) ++incomplete;
      active = true;
      frame = p[1];
      mode = p[3];
      next = 0;
    }
    if (!active || p[1] != frame || p[3] != mode || p[2] != next) {
      ++invalid;
      if (active) ++incomplete;
      active = false;
      return false;
    }
    const uint16_t start = p[2] * capacity;
    const uint16_t count = (LedCount - start < capacity) ? LedCount - start : capacity;
    for (uint16_t i = 0; i < count * 3; ++i) {
      const uint8_t value = mode ? ((p[4 + i / 2] >> ((i & 1) * 4)) & 15) << 4 : p[4 + i];
      assembly[start * 3 + i] = value;
    }
    ++next;
    if (next != packets) return false;
    if (hasPending) ++replaced;
    memcpy(pending, assembly, sizeof(pending));
    hasPending = true;
    active = false;
    ++complete;
    return true;
  }

private:
  uint8_t assembly[LedCount * 3] = {};
  uint8_t frame = 0, mode = 0, next = 0;
  bool active = false;
};
}
