// SPIKE usa e getta · fastink: disegno 1 bit nel formato della RAM, coordinate orizzontali 250×122.
#pragma once
#include "geom.h"

namespace fi {

// Rotazione 3 di heltec-eink-modules (landscape(), antenna in alto):
// RAM x = y logico, RAM y = 249 − x logico. Bit 7 = primo pixel del byte; 1 = bianco.
inline int ramIndex(int x, int y, uint8_t &mask) {
  const int mx = y, my = (kRamH - 1) - x;
  mask = (uint8_t)(0x80 >> (mx & 7));
  return my * kRowBytes + (mx >> 3);
}

struct Canvas {
  uint8_t buf[kRamBytes];

  void clear(bool white = true);
  void px(int x, int y, bool black);
  bool isBlack(int x, int y) const;
  void rect(int x, int y, int w, int h, bool black);
  // Carattere 5×7 (ASCII 32..126), scala intera; ritorna la larghezza disegnata
  int text(int x, int y, const char *s, int scale, bool black = true);
  static int textWidth(const char *s, int scale);
};

}  // namespace fi
