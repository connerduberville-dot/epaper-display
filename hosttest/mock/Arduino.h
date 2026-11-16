#pragma once
#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
#define MSBFIRST 1
#define SPI_MODE0 0
extern int g_pinState[64]; extern int g_busyReadsLeft;
inline void pinMode(int, int) {}
inline void digitalWrite(int p, int v) { g_pinState[p] = v; }
inline int digitalRead(int p) { if (g_busyReadsLeft > 0) { --g_busyReadsLeft; return LOW; } return HIGH; }
extern unsigned long g_ms; inline unsigned long millis() { return g_ms; } inline void delay(unsigned long d) { g_ms += d; }
static const int SCK = 19, MISO = 20, MOSI = 18;
