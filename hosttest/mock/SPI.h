#pragma once
#include "Arduino.h"
struct Byte { uint8_t v; bool dc; bool cs; bool cs2; };
extern std::vector<Byte> g_bus;
struct SPISettings { SPISettings(uint32_t, int, int) {} };
struct SPIClass {
  void begin(int, int, int, int) {}
  void beginTransaction(SPISettings) {} void endTransaction() {}
  uint8_t transfer(uint8_t v) { g_bus.push_back({v, g_pinState[2] == HIGH, g_pinState[1] == LOW, g_pinState[0] == LOW}); return 0; }
  void writeBytes(const uint8_t* b, size_t n) { for (size_t i = 0; i < n; ++i) transfer(b[i]); }
};
extern SPIClass SPI;
