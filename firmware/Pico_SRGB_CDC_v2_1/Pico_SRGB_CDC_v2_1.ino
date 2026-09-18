// Fast replacement for the SRGBmods RP2040 controller, protocol v2 pair only.
#if !defined(ARDUINO_ARCH_RP2040) || !defined(USE_TINYUSB)
#error Select Raspberry Pi Pico with the Adafruit TinyUSB USB stack.
#endif
#include <Adafruit_TinyUSB.h>
#include <Adafruit_NeoPXL8.h>
#include <EEPROM.h>
#include "pico/util/queue.h"
#include "hardware/sync.h"
#include "FrameAssembler.h"
#include "CdcProtocol.h"

using namespace FastRGB;
constexpr uint8_t PortCount = 7, Stride = 48, QueueDepth = 32;
constexpr uint8_t PortLengths[PortCount] = {24, 40, 32, 20, 48, 12, 25};
int8_t pins[8] = {12, 13, 14, 15, 16, 17, 18, -1};
constexpr uint8_t BootBrightness = 255;//Boot Animation Brightness
constexpr uint32_t BootDelayMs = 3000;//Boot Animation Delay
constexpr uint8_t Version[3] = {2, 1, 3};
const uint8_t descriptor[] = {TUD_HID_REPORT_DESC_GENERIC_INOUT(64)};
Adafruit_USBD_HID hid;
Adafruit_NeoPXL8 leds(Stride, pins, NEO_GRB);
queue_t receiveQueue;
FrameAssembler frames;
CdcParser cdc;
uint32_t cdcReceived = 0;
bool replyViaCdc = false;
uint16_t cdcReplyId = 0, txSize = 0, txOffset = 0;
uint8_t tx[76];
struct Report { uint8_t bytes[64]; };
volatile uint32_t received = 0, queueDrops = 0, shortReports = 0;
uint32_t shown = 0, badControls = 0;
uint16_t physical[LedCount];
bool outputReady = false, eepromReady = false, suspended = false;
bool bootActive = true, streamActive = false, hardwareActive = false, blackPending = false;
bool settingsDirty = false, replyPending = false;
uint8_t reply[64];
// The first ten EEPROM bytes retain the original firmware's layout.
uint8_t settings[10] = {0, 0, 10, 1, 6, 127, 128, 0, 128, 0};
uint32_t bootStart = 0, bootTick = 0, lastFrame = 0, lastEffect = 0, settingsChanged = 0;
uint16_t hue = 0;
uint8_t breath = 1, cycleColor = 0;
bool dimming = false;

void receiveReport(uint8_t reportId, hid_report_type_t type, const uint8_t *buffer, uint16_t size) {
  if (reportId != 0 || type != HID_REPORT_TYPE_OUTPUT || size != 64) {
    ++shortReports;
    return;
  }
  Report report;
  memcpy(report.bytes, buffer, 64);
  ++received;
  if (!queue_try_add(&receiveQueue, &report)) ++queueDrops;
}

void put32(uint8_t *destination, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i) destination[i] = uint8_t(value >> (i * 8));
}

void makeReply(uint8_t command, uint8_t token, uint8_t status) {
  memset(reply, 0, sizeof(reply));
  reply[0] = ControlMagic;
  reply[1] = command | 0x80;
  reply[2] = token;
  reply[3] = status;
  memcpy(reply + 4, Version, 3);
  reply[7] = ProtocolVersion;
  reply[8] = LedCount & 255;
  reply[9] = LedCount >> 8;
  reply[10] = PortCount;
  reply[11] = Stride;
  reply[12] = outputReady;
  reply[13] = settingsDirty;
  reply[14] = eepromReady;
  reply[15] = 1; // CDC whole-frame capability.
  const uint32_t irqState = save_and_disable_interrupts();
  const uint32_t rx = received, drops = queueDrops, shorts = shortReports;
  restore_interrupts(irqState);
  put32(reply + 16, rx + cdcReceived);
  put32(reply + 20, drops);
  put32(reply + 24, frames.invalid + shorts + badControls + cdc.invalid);
  put32(reply + 28, frames.complete);
  put32(reply + 32, shown);
  put32(reply + 36, frames.replaced);
  put32(reply + 40, frames.incomplete);
  put32(reply + 44, millis());
  memcpy(reply + 48, PortLengths, PortCount);
  replyPending = true;
}

bool validSettings(const uint8_t *s) {
  return s[0] <= 1 && s[1] <= 1 && s[2] >= 1 && s[2] <= 60 &&
         s[3] >= 1 && s[3] <= 4 && s[4] >= 1 && s[4] <= 20 && s[5] >= 10 && s[9] <= 1;
}

void processControl(const uint8_t *p, uint32_t now) {
  if (p[3] != 0 || p[1] < 1 || p[1] > 3) {
    ++badControls;
    makeReply(p[1], p[2], 1);
    return;
  }
  if (p[1] == 2) {
    if (!validSettings(p + 4)) {
      ++badControls;
      makeReply(2, p[2], 1);
      return;
    }
    if (memcmp(settings, p + 4, sizeof(settings)) != 0) {
      memcpy(settings, p + 4, sizeof(settings));
      settingsDirty = true;
      settingsChanged = now;
      hue = 0;
      breath = 1;
      dimming = false;
      cycleColor = 0;
      if (!bootActive && !streamActive) {
        hardwareActive = settings[0];
        if (!hardwareActive) blackPending = true;
      }
    }
  }
  makeReply(p[1], p[2], outputReady ? 0 : 2);
}
//Init Led and record bootstart
void setup() {
  Serial.begin(115200);
  queue_init(&receiveQueue, sizeof(Report), QueueDepth);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  hid.enableOutEndpoint(true);
  hid.setPollInterval(1);
  hid.setReportDescriptor(descriptor, sizeof(descriptor));
  TinyUSBDevice.setID(0x16D0, 0x1205);
  TinyUSBDevice.setManufacturerDescriptor("SRGBmods.net");
  TinyUSBDevice.setProductDescriptor("LED Controller CDC v2.1");
  hid.setReportCallback(NULL, receiveReport);
  hid.begin();
  EEPROM.begin(256);
  eepromReady = EEPROM.getConstDataPtr() != NULL;
  if (eepromReady) {
    uint8_t saved[10];
    for (uint8_t i = 0; i < sizeof(saved); ++i) saved[i] = EEPROM.read(i);
    if (validSettings(saved)) memcpy(settings, saved, sizeof(settings));
  }
  uint16_t logical = 0;
  for (uint8_t port = 0; port < PortCount; ++port) {
    for (uint8_t i = 0; i < PortLengths[port]; ++i) physical[logical++] = port * Stride + i;
  }
  outputReady = logical == LedCount && leds.begin();
  if (outputReady) { leds.clear(); blackPending = true; }
  bootStart = millis();
}

void fillSolid(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  leds.clear();
  for (uint16_t i = 0; i < LedCount; ++i) {
    leds.setPixelColor(physical[i], (uint16_t(r) * brightness + 127) / 255,
                       (uint16_t(g) * brightness + 127) / 255,
                       (uint16_t(b) * brightness + 127) / 255);
  }
}
//Boot Animation
void renderBoot(uint32_t now) {
  const uint32_t bootAge = now - bootStart;
  if (bootAge < BootDelayMs) return;
  if (now - bootTick < 10 || !leds.canShow()) return;
  bootTick = now;
  const uint32_t elapsed = bootAge - BootDelayMs;
  const uint16_t completed = elapsed / 80 > LedCount ? LedCount : uint16_t(elapsed / 80);
  for (uint16_t i = 0; i < LedCount; ++i) {
    const uint8_t value = i < completed ? BootBrightness :
      i == completed ? ((elapsed % 80) / 10 + 1) * BootBrightness / 8 : 0;
    leds.setPixelColor(physical[i], value, value, value);
  }
  leds.show();
  if (elapsed >= uint32_t(LedCount) * 80 + 600) {
    bootActive = false;
    hardwareActive = settings[0];
  }
}

void renderHardware(uint32_t now) {
  if (!hardwareActive || !leds.canShow() || now - lastEffect < uint32_t(300 / settings[4])) return;
  lastEffect = now;
  const uint8_t mode = settings[3];
  if (mode == 1) {
    hue -= 256;
    for (uint16_t i = 0; i < LedCount; ++i) {
      const uint16_t h = uint16_t(hue + uint32_t(physical[i] % Stride) * 10 * 65536 / Stride);
      leds.setPixelColor(physical[i], leds.ColorHSV(h, 255, settings[5]));
    }
  } else {
    const uint8_t colors[7][3] = {{255,0,0},{255,37,0},{255,255,0},{0,128,0},{128,128,0},{0,0,200},{75,0,130}};
    if (mode == 2 || mode == 4) {
      if (dimming) {
        if (breath > 1) --breath;
        else { dimming = false; if (mode == 2) cycleColor = (cycleColor + 1) % 7; }
      } else {
        if (breath < settings[5]) ++breath;
        else dimming = true;
      }
    }
    const uint8_t *color = mode == 2 ? colors[cycleColor] : settings + 6;
    fillSolid(color[0], color[1], color[2], mode == 3 ? settings[5] : breath);
  }
  leds.show();
}
//loop:If bootActive are ture,Are use renderBoot
void loop() {
  const uint32_t now = millis();
  if (TinyUSBDevice.suspended()) {
    cdc.reset(); txSize = txOffset = 0; replyPending = false;
    for (uint16_t i = 0; i < 1024 && Serial.available(); ++i) Serial.read();
    Report discarded;
    while (queue_try_remove(&receiveQueue, &discarded)) {}
    if (!suspended) {
      suspended = true;
      frames.reset();
      bootActive = streamActive = hardwareActive = false;
      blackPending = true;
    }
    if (blackPending && outputReady && leds.canShow()) {
      leds.clear(); leds.show(); blackPending = false;
    }
    digitalWrite(LED_BUILTIN, LOW);
    return;
  }
  suspended = false;
  cdc.expire(now);
  if (!TinyUSBDevice.mounted()) { cdc.reset(); txSize = txOffset = 0; replyPending = false; }
  for (uint16_t i = 0; i < 1024 && Serial.available(); ++i) {
    const int value = Serial.read();
    if (value < 0) break;
    if (!TinyUSBDevice.mounted()) continue;
    cdc.feed(uint8_t(value), now, [now](uint8_t type, uint16_t id, const uint8_t *payload, uint16_t) {
      ++cdcReceived;
      if (type == 1) {
        frames.publishRgb(payload);
        bootActive = hardwareActive = false;
        streamActive = true; blackPending = false; lastFrame = now;
      } else if (payload[0] == ControlMagic) {
        replyViaCdc = true; cdcReplyId = id;
        processControl(payload, now);
      } else ++badControls;
    });
  }
  Report report;
  // Bound each batch so display/housekeeping cannot be starved by USB traffic.
  for (uint8_t i = 0; i < QueueDepth && queue_try_remove(&receiveQueue, &report); ++i) {
    if (report.bytes[0] == FrameMagic) {
      if (frames.accept(report.bytes, sizeof(report.bytes))) {
        bootActive = hardwareActive = false;
        streamActive = true;
        blackPending = false;
        lastFrame = now;
      }
    } else if (report.bytes[0] == ControlMagic) { replyViaCdc = false; processControl(report.bytes, now); }
    else ++badControls;
  }
  if (replyPending && replyViaCdc && !txSize && TinyUSBDevice.mounted()) {
    txSize = encodeCdc(tx, 3, cdcReplyId, reply, sizeof(reply)); txOffset = 0; replyPending = false;
  }
  if (txSize && TinyUSBDevice.mounted()) {
    // Serial.write() requires DTR, which some serial hosts never assert.
    // Respect FIFO capacity and use the nonblocking TinyUSB CDC API directly.
    const uint32_t available = tud_cdc_n_write_available(0);
    const uint16_t remaining = txSize - txOffset;
    const uint16_t count = available < remaining ? uint16_t(available) : remaining;
    if (count) txOffset += uint16_t(tud_cdc_n_write(0, tx + txOffset, count));
    tud_cdc_n_write_flush(0);
    if (txOffset == txSize) txSize = txOffset = 0;
  }
  if (replyPending && !replyViaCdc && hid.ready() && hid.sendReport(0, reply, sizeof(reply))) replyPending = false;
  if (frames.hasPending && outputReady && leds.canShow()) {
    for (uint16_t i = 0; i < LedCount; ++i) {
      const uint8_t *rgb = frames.pending + i * 3;
      leds.setPixelColor(physical[i], rgb[0], rgb[1], rgb[2]);
    }
    leds.show();
    frames.hasPending = false;
    ++shown;
  }
  digitalWrite(LED_BUILTIN, settings[9] && streamActive && now - lastFrame < 500 ? HIGH : LOW);
  if (streamActive && settings[0] && settings[1] && now - lastFrame >= uint32_t(settings[2]) * 1000) {
    streamActive = false;
    frames.reset();
    hardwareActive = true;
  }
  if (outputReady) {
    if (blackPending && leds.canShow()) { leds.clear(); leds.show(); blackPending = false; }
    else if (bootActive) renderBoot(now);
    else if (!streamActive) renderHardware(now);
  }
  // Flash writes are postponed until idle; never intentionally stall an active stream.
  if (settingsDirty && eepromReady && now - settingsChanged >= 5000 && now - lastFrame >= 1000 && queue_is_empty(&receiveQueue) && !cdc.used && !Serial.available()) {
    for (uint8_t i = 0; i < sizeof(settings); ++i) EEPROM.write(i, settings[i]);
    if (EEPROM.commit()) settingsDirty = false;
    else settingsChanged = now;
  }
}
