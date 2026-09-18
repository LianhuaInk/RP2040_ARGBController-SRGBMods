#pragma once
#include <stdint.h>
#include <string.h>
#include <vector>
#include <deque>
#include <array>
#include <assert.h>

#define ARDUINO_ARCH_RP2040 1
#define USE_TINYUSB 1
#define LED_BUILTIN 25
#define OUTPUT 1
#define HIGH 1
#define LOW 0
#define NEO_GRB 0
#define TUD_HID_REPORT_DESC_GENERIC_INOUT(n) 0

inline uint32_t fakeMillis=0;
inline uint8_t statusLED=0;
inline uint32_t millis() { return fakeMillis; }
inline void pinMode(int,int) {}
inline void digitalWrite(int,int state) { statusLED=uint8_t(state); }
inline uint32_t save_and_disable_interrupts() { return 0; }
inline void restore_interrupts(uint32_t) {}
enum hid_report_type_t { HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT };

struct queue_t {
  size_t size=0, capacity=0;
  std::deque<std::vector<uint8_t>> items;
};
inline void queue_init(queue_t *q,size_t size,size_t capacity) { q->size=size; q->capacity=capacity; }
inline bool queue_try_add(queue_t *q,const void *data) {
  if(q->items.size()==q->capacity) return false;
  const auto p=static_cast<const uint8_t*>(data);
  q->items.emplace_back(p,p+q->size); return true;
}
inline bool queue_try_remove(queue_t *q,void *data) {
  if(q->items.empty()) return false;
  memcpy(data,q->items.front().data(),q->size); q->items.pop_front(); return true;
}
inline bool queue_is_empty(queue_t *q) { return q->items.empty(); }

struct USBDevice {
  bool asleep=false;
  bool attached=true;
  bool mounted() { return attached; }
  bool suspended() { return asleep; }
  void setID(int,int) {}
  void setManufacturerDescriptor(const char*) {}
  void setProductDescriptor(const char*) {}
};
inline USBDevice TinyUSBDevice;

class Adafruit_USBD_HID {
public:
  bool available=true;
  std::vector<std::array<uint8_t,64>> responses;
  void enableOutEndpoint(bool) {}
  void setPollInterval(int) {}
  void setReportDescriptor(const uint8_t*,size_t) {}
  void setReportCallback(void*,void(*)(uint8_t,hid_report_type_t,const uint8_t*,uint16_t)) {}
  bool begin() { return true; }
  bool ready() { return available; }
  bool sendReport(int,const uint8_t *data,size_t n) {
    if(!available) return false;
    assert(n==64); std::array<uint8_t,64> p; memcpy(p.data(),data,64); responses.push_back(p); return true;
  }
};
class Adafruit_NeoPXL8 {
public:
  bool available=true;
  unsigned shows=0;
  std::array<uint32_t,384> pixels={};
  Adafruit_NeoPXL8(int,int8_t*,int) {}
  bool begin() { return true; }
  void clear() { pixels.fill(0); }
  bool canShow() { return available; }
  void show() { assert(available); ++shows; available=false; }
  void setPixelColor(unsigned i,uint8_t r,uint8_t g,uint8_t b) { assert(i<384); pixels[i]=(r<<16)|(g<<8)|b; }
  void setPixelColor(unsigned i,uint32_t color) { assert(i<384); pixels[i]=color; }
  uint32_t ColorHSV(uint16_t hue,uint8_t saturation,uint8_t brightness) { return (uint32_t(hue)<<8)|saturation|brightness; }
};
class EEPROMClass {
public:
  unsigned commits=0;
  uint8_t memory[256];
  EEPROMClass() { memset(memory,255,sizeof(memory)); }
  void begin(size_t) {}
  const uint8_t *getConstDataPtr() { return memory; }
  uint8_t read(unsigned i) { assert(i<256); return memory[i]; }
  void write(unsigned i,uint8_t value) { assert(i<256); memory[i]=value; }
  bool commit() { ++commits; return true; }
};
inline EEPROMClass EEPROM;
class MockSerial {
public:
  bool connected = true;
  int capacity = 256;
  std::deque<uint8_t> incoming;
  std::vector<uint8_t> outgoing;
  void begin(int) {}
  bool dtr() { return connected; }
  int available() { return int(incoming.size()); }
  int read() { if (incoming.empty()) return -1; const auto value = incoming.front(); incoming.pop_front(); return value; }
  int availableForWrite() { return capacity; }
  uint16_t write(const uint8_t *p, uint16_t n) { assert(n <= capacity); outgoing.insert(outgoing.end(), p, p+n); return n; }
  void flush() {}
};
inline MockSerial Serial;
inline uint32_t tud_cdc_n_write_available(uint8_t) { return uint32_t(Serial.availableForWrite()); }
inline uint32_t tud_cdc_n_write(uint8_t, const uint8_t *p, uint16_t n) { return Serial.write(p,n); }
inline uint32_t tud_cdc_n_write_flush(uint8_t) { Serial.flush(); return 0; }
