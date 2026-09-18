#include "stubs/FirmwareMocks.h"
#include "../firmware/Pico_SRGB_CDC_v2_1/Pico_SRGB_CDC_v2_1.ino"
#include <stdio.h>
#include <random>

void feed(const uint8_t *p, size_t n) { Serial.incoming.insert(Serial.incoming.end(), p, p+n); }
int main(int argc, char **argv) {
  assert(argc==2);
  uint8_t packet[615];
  FILE *f=fopen(argv[1],"rb"); assert(f);assert(fread(packet,1,615,f)==615);fclose(f);
  setup(); assert(outputReady);
  static_assert(BootBrightness == 255, "Boot lighting must default to full brightness");
  fakeMillis = 80;
  renderBoot(fakeMillis);
  assert(leds.pixels[physical[0]] == 0xffffff);
  assert(leds.pixels[physical[1]] == 0x1f1f1f);
  leds.available = true;
  uint8_t request[64]={ControlMagic,1,7,0}, controlPacket[76];
  encodeCdc(controlPacket,2,9,request,64);
  Serial.connected=false; // SignalRGB may open CDC without asserting DTR.
  Serial.capacity=13;feed(controlPacket,76);loop();
  for(int i=0;i<5;++i)loop();
  assert(Serial.outgoing.size()==76 && Serial.outgoing[11]==0x81 && Serial.outgoing[25]==1);
  assert(crc16(Serial.outgoing.data(),74)==(Serial.outgoing[74]|Serial.outgoing[75]<<8));
  feed(packet,201);loop();assert(frames.complete==0 && bootActive);
  feed(packet+201,414);loop();assert(frames.complete==1 && shown==1 && streamActive && !bootActive);
  for(int i=0;i<201;++i)assert(leds.pixels[physical[i]]==uint32_t(((i*3*37+11)&255)<<16|((i*3*37+48)&255)<<8|((i*3*37+85)&255)));
  feed(packet,615);loop();assert(frames.hasPending && shown==1);
  feed(packet,615);loop();assert(frames.replaced==1);
  leds.available=true;loop();assert(shown==2);
  packet[25]^=1;feed(packet,615);loop();assert(frames.complete==3 && cdc.invalid>0);
  packet[25]^=1;feed(packet,615);loop();assert(frames.complete==4);
  feed(packet,100);loop();fakeMillis+=251;loop();assert(cdc.used==0);
  Serial.connected=false;feed(packet,615);loop();assert(frames.complete==5);
  TinyUSBDevice.attached=false;feed(packet,615);loop();assert(frames.complete==5);
  TinyUSBDevice.attached=true;leds.available=true;feed(packet,615);loop();assert(frames.complete==6);
  TinyUSBDevice.asleep=true;leds.available=true;loop();assert(!streamActive && !frames.hasPending);
  // Random garbage must never overrun the bounded receive buffer; valid frames recover.
  CdcParser parser;std::mt19937 random(42);unsigned consumed=0;
  auto consume=[&](uint8_t,uint16_t,const uint8_t*,uint16_t){++consumed;};
  for(int i=0;i<100000;++i)parser.feed(uint8_t(random()),0,consume);
  for(auto value:packet)parser.feed(value,1,consume);
  assert(consumed==1);
  puts("PASS: actual CDC firmware scheduler, split frames, CRC rejection/recovery, partial TX without DTR, physical mapping, busy-output replacement, timeout, USB disconnect, suspend and 100000 garbage bytes.");
}
