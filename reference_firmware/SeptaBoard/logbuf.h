// Serial log mirror: everything logged with LOG() also lands in a small ring buffer the web UI can read.
#pragma once
#include <Arduino.h>
#include <stdarg.h>

namespace septa {

class LogRing {
 public:
  static constexpr size_t CAP = 3072;
  void printf(const char* fmt, ...) {
    char line[200];
    va_list ap; va_start(ap, fmt); int n = vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof line) n = sizeof line - 1;
    Serial.write((const uint8_t*)line, n);
    unsigned long s = millis() / 1000;
    char stamp[16]; int sn = snprintf(stamp, sizeof stamp, "%6lu ", s);
    put(stamp, sn); put(line, n);
  }
  // Copies the buffer in chronological order into `out` (null-terminated). Returns length.
  size_t read(char* out, size_t cap) const {
    size_t n = 0;
    if (full_) for (size_t i = head_; i < CAP && n + 1 < cap; ++i) out[n++] = buf_[i];
    for (size_t i = 0; i < head_ && n + 1 < cap; ++i) out[n++] = buf_[i];
    out[n] = 0; return n;
  }
 private:
  char buf_[CAP]; size_t head_ = 0; bool full_ = false;
  void put(const char* s, size_t n) { for (size_t i = 0; i < n; ++i) { buf_[head_++] = s[i]; if (head_ == CAP) { head_ = 0; full_ = true; } } }
};

extern LogRing LOGR;
#define LOG(...) septa::LOGR.printf(__VA_ARGS__)

}  // namespace septa
