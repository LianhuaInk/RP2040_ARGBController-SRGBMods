#pragma once
#include <stdint.h>
#include <string.h>

namespace FastRGB {
inline uint16_t crc16(const uint8_t *p, uint16_t n) {
  uint16_t crc = 0xffff;
  while (n--) {
    crc ^= uint16_t(*p++) << 8;
    for (uint8_t i = 0; i < 8; ++i) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
  }
  return crc;
}
inline uint16_t encodeCdc(uint8_t *out, uint8_t type, uint16_t id, const uint8_t *p, uint16_t n) {
  const uint8_t magic[4] = {'P','R','G','B'};
  memcpy(out, magic, 4);
  out[4] = 1; out[5] = type;
  out[6] = uint8_t(id); out[7] = uint8_t(id >> 8);
  out[8] = uint8_t(n); out[9] = uint8_t(n >> 8);
  memcpy(out + 10, p, n);
  const uint16_t crc = crc16(out, n + 10);
  out[n + 10] = uint8_t(crc); out[n + 11] = uint8_t(crc >> 8);
  return n + 12;
}
class CdcParser {
public:
  uint8_t bytes[615] = {};
  uint16_t used = 0;
  uint32_t invalid = 0, lastByte = 0;
  void reset() { used = 0; }
  void expire(uint32_t now) { if (used && now - lastByte > 250) { ++invalid; reset(); } }
  template<class Consumer> void feed(uint8_t value, uint32_t now, Consumer consume) {
    lastByte = now;
    bytes[used++] = value;
    for (;;) {
      const uint8_t magic[4] = {'P','R','G','B'};
      if (memcmp(bytes, magic, used < 4 ? used : 4) != 0) { discard(); continue; }
      if (used < 10) return;
      const uint16_t n = uint16_t(bytes[8]) | uint16_t(bytes[9]) << 8;
      if (bytes[4] != 1 || !((bytes[5] == 1 && n == 603) || (bytes[5] == 2 && n == 64))) {
        ++invalid; discard(); continue;
      }
      if (used < n + 12) return;
      const uint16_t crc = uint16_t(bytes[n + 10]) | uint16_t(bytes[n + 11]) << 8;
      if (crc != crc16(bytes, n + 10)) { ++invalid; discard(); continue; }
      consume(bytes[5], uint16_t(bytes[6]) | uint16_t(bytes[7]) << 8, bytes + 10, n);
      used -= n + 12;
      memmove(bytes, bytes + n + 12, used);
      if (!used) return;
    }
  }
private:
  void discard() { if (used) { --used; memmove(bytes, bytes + 1, used); } }
};
}
