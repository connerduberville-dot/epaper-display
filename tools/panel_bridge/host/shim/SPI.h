#pragma once
#include "Arduino.h"
struct SPISettings { SPISettings(uint32_t, int, int) {} };
void bridge_spi(const uint8_t* b, size_t n);
struct SPIClass {
  void begin(int, int, int, int) {}
  void beginTransaction(SPISettings) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t v) { bridge_spi(&v, 1); return 0; }
  void writeBytes(const uint8_t* b, size_t n) { bridge_spi(b, n); }
};
extern SPIClass SPI;
