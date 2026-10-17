// SPIKE usa e getta · fastink: motore a budget per pixel.
#include "engine.h"
#include <string.h>

namespace fi {

Engine::Engine(uint8_t budget) : budget_(budget < 1 ? 1 : (budget > kMaxBudget ? kMaxBudget : budget)) {
  memset(target_, 0xFF, sizeof(target_));
  memset(level_, 0, sizeof(level_));
}

void Engine::setBudget(uint8_t n) {
  if (n < 1) n = 1;
  if (n > kMaxBudget) n = kMaxBudget;
  if (n == budget_) return;
  for (int i = 0; i < kPixels; i++) level_[i] = (uint8_t)((level_[i] * n + budget_ / 2) / budget_);
  budget_ = n;
}

void Engine::syncSaturated(const uint8_t *img) {
  memcpy(target_, img, kRamBytes);
  for (int i = 0; i < kRamBytes; i++)
    for (int k = 0; k < 8; k++) level_[i * 8 + k] = (img[i] & (0x80 >> k)) ? 0 : budget_;
}

void Engine::setTarget(const uint8_t *img) { memcpy(target_, img, kRamBytes); }

TickStats Engine::tick(uint8_t *bw, uint8_t *red) {
  TickStats s;
  const uint8_t n = budget_;
  for (int i = 0; i < kRamBytes; i++) {
    const uint8_t t = target_[i];
    uint8_t *lv = &level_[i * 8];
    uint8_t b = 0, r = 0;
    for (int k = 0; k < 8; k++) {
      const uint8_t m = 0x80 >> k;
      const uint8_t want = (t & m) ? 0 : n;
      if (lv[k] < want) {          // verso il nero: LUT0 (bw 0, red 0)
        lv[k]++;
        s.toBlack++;
      } else if (lv[k] > want) {   // verso il bianco: LUT1 (bw 1, red 0)
        lv[k]--;
        b |= m;
        s.toWhite++;
      } else {                     // fermo: red 1, bw = bersaglio
        r |= m;
        b |= (t & m);
      }
    }
    bw[i] = b;
    red[i] = r;
  }
  return s;
}

void Engine::holdPlanes(const uint8_t *img, uint8_t *bw, uint8_t *red) {
  memcpy(bw, img, kRamBytes);
  memset(red, 0xFF, kRamBytes);
}

uint32_t Engine::pending() const {
  uint32_t p = 0;
  for (int i = 0; i < kRamBytes; i++)
    for (int k = 0; k < 8; k++) {
      const uint8_t want = (target_[i] & (0x80 >> k)) ? 0 : budget_;
      if (level_[i * 8 + k] != want) p++;
    }
  return p;
}

}  // namespace fi
