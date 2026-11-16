// Driver for Qiyun QYEG1085BNF86BF0 (10.85" 1360x480 B/W, two JD79686BB controllers) on a DESPI-C02 adapter.
// Ported from the vendor's STM32 example (EPAPER.c, 2024-09-26). Command sequences, LUTs and timing are
// kept identical to the vendor code; only the bus layer (hardware SPI) and the framebuffer plumbing are new.
//
// Panel architecture: controller 0 (CS)  drives columns    0..679  (bytes 0..84 of each 170-byte row)
//                     controller 1 (CS2) drives columns  680..1359 (bytes 85..169)
// Shared: SCK, MOSI, D/C, RES, BUSY. BUSY is LOW while busy.
#pragma once
#include <stdint.h>
#include "framebuffer.h"

namespace septa {

struct EpdPins { int cs, cs2, dc, rst, busy; };

class EpdQyeg1085 {
 public:
  // Busy-wait upper bounds (ms). Vendor code waits forever; we bail so a wiring fault can't hang the board.
  static constexpr uint32_t BUSY_TIMEOUT_FULL_MS = 15000, BUSY_TIMEOUT_PARTIAL_MS = 6000;

  void begin(const EpdPins& p, uint32_t spiHz = 8000000);

  // Full-screen update using the controller's built-in (OTP) waveform. ~4 s, flashes. Best contrast.
  void fullUpdate(const Framebuffer& image);

  // Partial update: window per controller, old vs new image, register (fast) LUT. ~0.6 s claimed, no flash.
  // Rects are in full-panel coordinates; x and w must be multiples of 8. An empty rect for a controller is
  // replaced by a minimal 8x8 window so both controllers are in partial mode (as in the vendor demo).
  void partialUpdate(const Framebuffer& oldImage, const Framebuffer& newImage, Rect leftRect, Rect rightRect);

  // Clear to white with a full refresh, then deep sleep.
  void clearWhite();

  bool lastBusyTimedOut() const { return busyTimedOut_; }
  uint32_t lastRefreshMs() const { return lastRefreshMs_; }   // time spent in the 0x12 refresh wait

 private:
  enum Target : uint8_t { BOTH = 0, MASTER = 1, SLAVE = 2 };
  EpdPins p_{};
  uint32_t spiHz_ = 8000000;
  bool busyTimedOut_ = false;
  uint32_t lastRefreshMs_ = 0;

  void hwReset();
  bool waitBusy(uint32_t timeoutMs);
  void select(Target t); void deselect();
  void cmd(uint8_t c, Target t = BOTH);
  void data(uint8_t d, Target t = BOTH);
  void dataBuf(const uint8_t* buf, size_t n, Target t);
  void fitiPreamble();
  void initCommon(bool registerLut);
  void loadLut(const uint8_t* head, const uint8_t* vcom, const uint8_t* ww, const uint8_t* bw, const uint8_t* wb, const uint8_t* bb);
  void powerOnRefreshOffSleep(uint32_t busyTimeout);
  void writeWindow(Target t, int xLocal, int y, int w, int h, const Framebuffer& oldImg, const Framebuffer& newImg, int byteOffset);
  void writeFullHalf(Target t, const Framebuffer& img, int byteOffset);
};

}  // namespace septa
