// The built-in screen: time and date, a how-to line, and a footer with firmware, network and update status.
// Shown whenever no content URL is set (or it can't be fetched). Pure drawing code, so it also renders on the
// PC (hosttest/render_display.cpp) for layout previews without wearing the panel.
#pragma once
#include <stdio.h>
#include "framebuffer.h"

namespace septa {

struct ScreenInfo {
  const char* time = "--:--";    // "9:41"
  const char* ampm = "";         // "AM" / "PM"
  const char* date = "";         // "SUNDAY, NOVEMBER 16"
  const char* footerLeft = "";   // "FIRMWARE 1.0.0 BUILD 12"
  const char* footerRight = "";  // "10.0.0.4   UPDATES: UP TO DATE"
  const char* hint = nullptr;    // a line of instructions under the date
  const char* notice = nullptr;  // if set: a problem line (e.g. "CONTENT: CONTENT HTTP 404")
};

inline void drawCentered(Framebuffer& fb, int cx, int baseline, const char* s, int font, bool black) {
  fb.text(cx - Framebuffer::textWidth(s, font) / 2, baseline, s, font, black);
}

inline void renderScreen(Framebuffer& fb, const ScreenInfo& s) {
  fb.clear(false);
  // Time and date at 54/38 px. A bigger clock looks nicer but its minute ticks clear 1.5k-5.8k pixels, which
  // breaks the measured ghost rule (CLAUDE.md) and would force a flashing full refresh every few minutes.
  // At 54 px the worst minute tick clears 682 px (hosttest/clock_ticks.cpp), so ticks stay partial.
  char line[64];
  snprintf(line, sizeof line, "%s%s%s", s.time, s.ampm[0] ? " " : "", s.ampm);
  drawCentered(fb, W / 2, 175, line, 3, true);
  drawCentered(fb, W / 2, 240, s.date, 2, true);
  if (s.hint && *s.hint) drawCentered(fb, W / 2, 320, s.hint, 1, true);
  if (s.notice && *s.notice) drawCentered(fb, W / 2, 370, s.notice, 1, true);

  fb.fillRect(40, 418, W - 80, 2, true);
  fb.text(40, 456, s.footerLeft, 0, true);
  fb.text(W - 40 - Framebuffer::textWidth(s.footerRight, 0), 456, s.footerRight, 0, true);
}

// A plain message screen (Wi-Fi setup, errors).
inline void renderMessage(Framebuffer& fb, const char* title, const char* detail, const char* detail2 = nullptr) {
  fb.clear(false);
  drawCentered(fb, W / 2, 200, title, 3, true);
  if (detail && *detail) drawCentered(fb, W / 2, 270, detail, 1, true);
  if (detail2 && *detail2) drawCentered(fb, W / 2, 320, detail2, 1, true);
}

// Black bar along the bottom with white text, drawn over whatever is in fb (used while installing updates).
inline void drawBanner(Framebuffer& fb, const char* text) {
  fb.fillRect(0, 408, W, H - 408, true);
  drawCentered(fb, W / 2, 458, text, 2, false);
}

}  // namespace septa
