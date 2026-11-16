#include "epd_qyeg1085.h"
#include <Arduino.h>
#include <SPI.h>

namespace septa {

// ---- Vendor LUTs (verbatim from EPAPER.c) -------------------------------------------------------
// Fast full-refresh LUT (register mode). Vendor note: contrast slightly lower than OTP full refresh.
static const uint8_t LUT_FAST_HEAD[14] = {0x22,0x00,0x3F,0x3F,0xA0,0x00,0x00,0x00, 0x3C,0x3C,0x3C,0x3C,0x3C,0x3C};
static const uint8_t LUT_VCOM_F[60] = {
0x01,0x32,0x00,0x00,0x32,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t LUT_WW_F[42] = {
0x01,0x72,0x40,0x80,0xB2,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01};
static const uint8_t LUT_BW_F[42] = {
0x01,0x72,0x40,0x80,0xB2,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01};
static const uint8_t LUT_WB_F[42] = {
0x01,0xB2,0x40,0x80,0x72,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01};
static const uint8_t LUT_BB_F[42] = {
0x01,0xB2,0x40,0x80,0x72,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01};

// Partial-refresh LUT (register mode).
static const uint8_t LUT_PART_HEAD[14] = {0x02,0x00,0x3F,0x3F,0xA0,0x00,0x00,0x00, 0x3C,0x3C,0x3C,0x3C,0x3C,0x3C};
static const uint8_t LUT_VCOM_P[60] = {
0x01,0x19,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const uint8_t LUT_WW_P[42] = {
0x01,0x19,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01};
static const uint8_t LUT_BW_P[42] = {
0x01,0x99,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01};
static const uint8_t LUT_WB_P[42] = {
0x01,0x59,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01};
static const uint8_t LUT_BB_P[42] = {
0x01,0x19,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01,
0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01, 0x01,0x00,0x00,0x00,0x00,0x01,0x01};

// ---- Bus layer -----------------------------------------------------------------------------------
void EpdQyeg1085::begin(const EpdPins& p, uint32_t spiHz) {
  p_ = p; spiHz_ = spiHz;
  pinMode(p_.cs, OUTPUT); pinMode(p_.cs2, OUTPUT); pinMode(p_.dc, OUTPUT); pinMode(p_.rst, OUTPUT);
  pinMode(p_.busy, INPUT);
  digitalWrite(p_.cs, HIGH); digitalWrite(p_.cs2, HIGH); digitalWrite(p_.dc, HIGH); digitalWrite(p_.rst, HIGH);
  // XIAO ESP32-C6 hardware SPI: SCK=D8, MOSI=D10 (MISO unused). Pins given explicitly so this doesn't
  // depend on board defaults.
  SPI.begin(SCK, MISO, MOSI, -1);
}

void EpdQyeg1085::select(Target t) {
  // Vendor always deasserts both first, then asserts the target(s).
  digitalWrite(p_.cs, HIGH); digitalWrite(p_.cs2, HIGH);
  if (t == BOTH || t == MASTER) digitalWrite(p_.cs, LOW);
  if (t == BOTH || t == SLAVE)  digitalWrite(p_.cs2, LOW);
}
void EpdQyeg1085::deselect() { digitalWrite(p_.cs, HIGH); digitalWrite(p_.cs2, HIGH); }

void EpdQyeg1085::cmd(uint8_t c, Target t) {
  SPI.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
  select(t); digitalWrite(p_.dc, LOW); SPI.transfer(c); deselect();
  SPI.endTransaction();
}
void EpdQyeg1085::data(uint8_t d, Target t) {
  SPI.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
  select(t); digitalWrite(p_.dc, HIGH); SPI.transfer(d); deselect();
  SPI.endTransaction();
}
void EpdQyeg1085::dataBuf(const uint8_t* buf, size_t n, Target t) {
  SPI.beginTransaction(SPISettings(spiHz_, MSBFIRST, SPI_MODE0));
  select(t); digitalWrite(p_.dc, HIGH);
  SPI.writeBytes(buf, n);
  deselect();
  SPI.endTransaction();
}

void EpdQyeg1085::hwReset() {
  digitalWrite(p_.rst, LOW);  delay(100);
  digitalWrite(p_.rst, HIGH); delay(100);
}

bool EpdQyeg1085::waitBusy(uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (digitalRead(p_.busy) == LOW) {          // LOW = busy
    if (millis() - t0 > timeoutMs) { busyTimedOut_ = true; return false; }
    delay(2);
  }
  return true;
}

// ---- Init sequences (verbatim order from EPD_HW_Init / EPD_Partial_Init) --------------------------
void EpdQyeg1085::fitiPreamble() {
  cmd(0x4D); data(0x55);
  cmd(0xA6); data(0x38);
  cmd(0xB4); data(0x5D);
  cmd(0xB6); data(0x80);
  cmd(0xB7); data(0x00);
  cmd(0xF7); data(0x02);
  cmd(0xAE, SLAVE); data(0xA0, SLAVE);     // slave only, as in vendor code
}

void EpdQyeg1085::initCommon(bool registerLut) {
  hwReset();
  busyTimedOut_ = false;
  waitBusy(BUSY_TIMEOUT_FULL_MS);
  fitiPreamble();
  cmd(0xE0); data(0x01);
  cmd(0x00);                               // Panel setting: BW, OTP or register LUT
  data(registerLut ? 0xBF : 0x9F); data(0x0D);
  cmd(0x06); data(0x57); data(0x24); data(0x28); data(0x32); data(0x08); data(0x48);   // boost
  if (!registerLut) { cmd(0x30); data(0x3C); }                                          // OSC (full-OTP path only)
  cmd(0x50); data(0x47);                   // VCOM / data interval
  cmd(0x60); data(0x31);                   // TCON
  cmd(0x61); data(0x02); data(0xA8); data(0x01); data(0xE0);   // 680 x 480 per controller
  cmd(0x62); data(0x00); data(0x00); data(0x00); data(0x00);
  cmd(0xE8); data(0x01);
}

void EpdQyeg1085::loadLut(const uint8_t* head, const uint8_t* vcom, const uint8_t* ww, const uint8_t* bw, const uint8_t* wb, const uint8_t* bb) {
  cmd(0x82); data(head[0]);                                        // VCOM_DC
  cmd(0x01); data(0x03); data(head[1]); data(head[2]); data(head[3]); data(head[4]);   // power
  cmd(0x2A); data(head[5]); data(head[6]); data(head[7]);          // LUT option
  cmd(0x31); data(0x00);
  cmd(0x32); for (int i = 8; i < 14; ++i) data(head[i]);           // PLL
  cmd(0x20); for (int i = 0; i < 60; ++i) data(vcom[i]);
  cmd(0x21); for (int i = 0; i < 42; ++i) data(ww[i]);
  cmd(0x22); for (int i = 0; i < 42; ++i) data(bw[i]);
  cmd(0x23); for (int i = 0; i < 42; ++i) data(wb[i]);
  cmd(0x24); for (int i = 0; i < 42; ++i) data(bb[i]);
}

// EPD_Update_And_DeepSleep, verbatim
void EpdQyeg1085::powerOnRefreshOffSleep(uint32_t busyTimeout) {
  cmd(0x01, SLAVE); data(0x00, SLAVE); data(0x00, SLAVE); data(0x3F, SLAVE); data(0x3F, SLAVE); data(0x28, SLAVE);
  cmd(0x04);                               // power on
  delay(200);
  waitBusy(busyTimeout);
  uint32_t t0 = millis();
  cmd(0x12);                               // display refresh
  waitBusy(busyTimeout);
  lastRefreshMs_ = millis() - t0;
  cmd(0x02);                               // power off
  delay(60);
  waitBusy(busyTimeout);
  cmd(0x07); data(0xA5);                   // deep sleep (wake needs hardware reset; initCommon does that)
  delay(60);
}

// ---- Image transfer ------------------------------------------------------------------------------
void EpdQyeg1085::writeFullHalf(Target t, const Framebuffer& img, int byteOffset) {
  static const uint8_t zeros[HALF_STRIDE] = {0};
  cmd(0x10, t);                             // "old" image: all zero, as vendor does for a full update
  for (int y = 0; y < H; ++y) dataBuf(zeros, HALF_STRIDE, t);
  cmd(0x13, t);                             // "new" image: this controller's 85 bytes of each row
  for (int y = 0; y < H; ++y) dataBuf(img.px + y * STRIDE + byteOffset, HALF_STRIDE, t);
}

void EpdQyeg1085::fullUpdate(const Framebuffer& image) {
  initCommon(/*registerLut=*/false);        // OTP waveform
  writeFullHalf(MASTER, image, 0);
  writeFullHalf(SLAVE, image, HALF_STRIDE);
  powerOnRefreshOffSleep(BUSY_TIMEOUT_FULL_MS);
}

void EpdQyeg1085::clearWhite() {
  static Framebuffer white; white.clear(false);
  fullUpdate(white);
}

// partial_single_Master/Slave, verbatim: 0x91 partial-in, 0x90 window, 0x10 old, 0x13 new.
void EpdQyeg1085::writeWindow(Target t, int xLocal, int y, int w, int h, const Framebuffer& oldImg, const Framebuffer& newImg, int byteOffset) {
  int xEnd = xLocal + w - 1, yEnd = y + h - 1;
  cmd(0x91, t);
  cmd(0x90, t);
  data(xLocal >> 8, t); data(xLocal & 0xFF, t); data(xEnd >> 8, t); data(xEnd & 0xFF, t);
  data(y >> 8, t);      data(y & 0xFF, t);      data(yEnd >> 8, t); data(yEnd & 0xFF, t);
  data(0x00, t);                            // PT_SCAN
  int bx = byteOffset + xLocal / 8, bw = w / 8;
  cmd(0x10, t); for (int yy = y; yy <= yEnd; ++yy) dataBuf(oldImg.px + yy * STRIDE + bx, bw, t);
  cmd(0x13, t); for (int yy = y; yy <= yEnd; ++yy) dataBuf(newImg.px + yy * STRIDE + bx, bw, t);
}

void EpdQyeg1085::partialUpdate(const Framebuffer& oldImage, const Framebuffer& newImage, Rect leftRect, Rect rightRect) {
  initCommon(/*registerLut=*/true);
  loadLut(LUT_PART_HEAD, LUT_VCOM_P, LUT_WW_P, LUT_BW_P, LUT_WB_P, LUT_BB_P);
  // Both controllers get a window (vendor demo always writes master and slave). Empty -> 8x8 no-op window.
  if (leftRect.empty())  leftRect  = {0, 0, 8, 8};
  if (rightRect.empty()) rightRect = {HALF_W, 0, 8, 8};
  // Deviation from the vendor demo (found on real glass, Nov 9): with a window shorter than the panel, the
  // JD79686BB draws a thin line in the window's first column through every row OUTSIDE the window. Making
  // every window full height (rows 0-479) leaves no outside rows, so no line; width stays tight.
  leftRect.y = 0;  leftRect.h = H;
  rightRect.y = 0; rightRect.h = H;
  writeWindow(MASTER, leftRect.x,           leftRect.y,  leftRect.w,  leftRect.h,  oldImage, newImage, 0);
  writeWindow(SLAVE,  rightRect.x - HALF_W, rightRect.y, rightRect.w, rightRect.h, oldImage, newImage, HALF_STRIDE);
  powerOnRefreshOffSleep(BUSY_TIMEOUT_PARTIAL_MS);
}

}  // namespace septa
