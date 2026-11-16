// Arduino API subset for running the ESP32 EPD driver on Windows against the demo-board bridge.
#pragma once
#include <stdint.h>
#include <stddef.h>
#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
#define MSBFIRST 1
#define SPI_MODE0 0
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
unsigned long millis();
void delay(unsigned long ms);
static const int SCK = 19, MISO = 20, MOSI = 18;
