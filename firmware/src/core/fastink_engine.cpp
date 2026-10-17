#include "fastink_engine.h"

#include <stdlib.h>
#include <string.h>

namespace cal {
namespace fastink {

Params paramsFor(Profile p) {
  // Scelta dell'utente nello spike: F8 P1 N10 FR7 «la migliore»; F4 P1 N6 FR7 più fluido, scia evidente.
  Params r;
  if (p == Profile::Smooth) {
    r.budget = 6; r.tp = 4; r.phases = 1; r.fr = 7; r.edge = 1; r.polish = 2;
  } else {
    r.budget = 10; r.tp = 8; r.phases = 1; r.fr = 7; r.edge = 2; r.polish = 2;
  }
  return r;
}

const char *profileName(Profile p) { return p == Profile::Smooth ? "fluido" : "pulito"; }

void frameToRam(const uint8_t *frame, uint8_t *ram) {
  memset(ram, 0xFF, kRamBytes);
  for (int y = 0; y < kScrH; y++) {
    const uint8_t *row = frame + y * kFrameStride;
    const uint8_t m = static_cast<uint8_t>(0x80 >> (y & 7));
    const int xb = y >> 3;
    for (int x = 0; x < kScrW; x++)
      if (row[x >> 3] & (0x80 >> (x & 7))) ram[(kRamH - 1 - x) * kRowBytes + xb] &= static_cast<uint8_t>(~m);
  }
}

size_t buildTickLut(const Params &p, uint8_t *out) {
  memset(out, 0, kLutLen);
  out[0 * 12] = 0x1 << 6;  // LUT0, gruppo 0, fase A: VSH1
  out[1 * 12] = 0x2 << 6;  // LUT1, gruppo 0, fase A: VSL
  uint8_t *g0 = out + 60;  // TP A, TP B, SR AB, TP C, TP D, SR CD, RP
  g0[0] = p.tp < 1 ? 1 : p.tp;
  g0[6] = p.phases < 1 ? 0 : static_cast<uint8_t>(p.phases - 1);
  const uint8_t fr = p.fr & 0x7;
  for (int i = 0; i < 6; i++) out[144 + i] = static_cast<uint8_t>((fr << 4) | fr);
  return kLutLen;
}

const uint8_t Engine::kMaxBudget;
const uint8_t Engine::kMaxExtra;
const size_t Engine::kStateBytes;

namespace {
uint8_t clampBudget(uint8_t n) { return n < 1 ? 1 : (n > Engine::kMaxBudget ? Engine::kMaxBudget : n); }
bool whiteAt(const uint8_t *ram, int idx) { return ram[idx >> 3] & (0x80 >> (idx & 7)); }
bool visible(int idx) { return idx % kRamW < kVisW; }
}  // namespace

bool Engine::init(uint8_t budget) {
  if (!lv_ || !ex_ || !target_) {  // già allocati (fastink::reserve): si riusano, niente frammentazione
    release();
    lv_ = static_cast<uint8_t *>(malloc(kPixels / 2));
    ex_ = static_cast<uint8_t *>(malloc(kPixels / 4));
    target_ = static_cast<uint8_t *>(malloc(kRamBytes));
    if (!lv_ || !ex_ || !target_) {
      release();
      return false;
    }
  }
  budget_ = clampBudget(budget);
  memset(lv_, 0, kPixels / 2);
  memset(ex_, 0, kPixels / 4);
  memset(target_, 0xFF, kRamBytes);
  polished_ = true;
  return true;
}

void Engine::release() {
  free(lv_);
  free(ex_);
  free(target_);
  lv_ = ex_ = target_ = nullptr;
}

void Engine::setBudget(uint8_t n) {
  n = clampBudget(n);
  if (n == budget_) return;
  for (int i = 0; i < kPixels; i++) setLevel(i, static_cast<uint8_t>((level(i) * n + budget_ / 2) / budget_));
  budget_ = n;
}

void Engine::syncSaturated(const uint8_t *ram) {
  memcpy(target_, ram, kRamBytes);
  for (int i = 0; i < kPixels; i++) setLevel(i, whiteAt(ram, i) ? 0 : budget_);
  memset(ex_, 0, kPixels / 4);
  polished_ = true;
}

void Engine::bump(int idx, uint8_t n) {
  if (!visible(idx)) return;
  if (n > kMaxExtra) n = kMaxExtra;
  if (extra(idx) < n) setExtra(idx, n);
}

bool Engine::atTarget(int idx) const { return level(idx) == (whiteAt(target_, idx) ? 0 : budget_); }

void Engine::setTarget(const uint8_t *ram, uint8_t edge) {
  if (memcmp(target_, ram, kRamBytes) == 0) return;  // stesso fotogramma: niente da fare
  polished_ = false;
  if (edge) {
    for (int i = 0; i < kRamBytes; i++) {
      const uint8_t diff = target_[i] ^ ram[i];
      if (!diff) continue;
      for (int k = 0; k < 8; k++) {
        if (!(diff & (0x80 >> k))) continue;
        const int idx = i * 8 + k, x = idx % kRamW, y = idx / kRamW;
        bump(idx, edge);
        if (x > 0) bump(idx - 1, edge);
        if (x + 1 < kRamW) bump(idx + 1, edge);
        if (y > 0) bump(idx - kRamW, edge);
        if (y + 1 < kRamH) bump(idx + kRamW, edge);
      }
    }
  }
  memcpy(target_, ram, kRamBytes);
}

TickStats Engine::tick(uint8_t *bw, uint8_t *red) {
  TickStats s = TickStats();
  // 1) Idoneità delle spinte extra con i livelli di prima del tick (il pixel e i 4 vicini sono arrivati), scritta in
  // red: il passo 2 legge il byte i prima di riscriverlo.
  uint8_t *elig = red;
  memset(elig, 0, kRamBytes);
  for (int i = 0; i < kPixels / 4; i++) {
    if (!ex_[i]) continue;  // 4 pixel senza spinte extra
    for (int idx = i * 4; idx < i * 4 + 4; idx++) {
      if (!extra(idx)) continue;
      const int x = idx % kRamW, y = idx / kRamW;
      const bool ok = atTarget(idx) && (x == 0 || atTarget(idx - 1)) && (x + 1 >= kRamW || atTarget(idx + 1)) &&
                      (y == 0 || atTarget(idx - kRamW)) && (y + 1 >= kRamH || atTarget(idx + kRamW));
      if (ok) elig[idx >> 3] |= static_cast<uint8_t>(0x80 >> (idx & 7));
    }
  }
  // 2) Un impulso per pixel: verso il bersaglio, oppure una spinta extra, oppure fermo (LUT2/LUT3).
  const uint8_t n = budget_;
  for (int i = 0; i < kRamBytes; i++) {
    const uint8_t t = target_[i], e = elig[i];
    uint8_t b = 0, r = 0;
    for (int k = 0; k < 8; k++) {
      const int idx = i * 8 + k;
      const uint8_t m = static_cast<uint8_t>(0x80 >> k);
      const bool white = t & m;
      const uint8_t want = white ? 0 : n;
      uint8_t lv = level(idx), ex = extra(idx);
      bool toWhite;
      if (lv < want) { lv++; toWhite = false; }
      else if (lv > want) { lv--; toWhite = true; }
      else if (ex && (e & m)) { ex--; toWhite = white; s.extra++; setExtra(idx, ex); }
      else { r |= m; b |= (t & m); continue; }
      if (toWhite) { b |= m; s.toWhite++; }  // LUT1
      else s.toBlack++;                       // LUT0
      setLevel(idx, lv);
    }
    bw[i] = b;
    red[i] = r;
  }
  return s;
}

uint32_t Engine::pending() const {
  uint32_t p = 0;
  for (int idx = 0; idx < kPixels; idx++)
    if (!atTarget(idx)) p++;
  return p;
}

void Engine::polish(uint8_t pushes) {
  for (int idx = 0; idx < kPixels; idx++) bump(idx, pushes);
}

bool Engine::settled() const {
  for (int i = 0; i < kPixels / 4; i++)
    if (ex_[i]) return false;
  for (int idx = 0; idx < kPixels; idx++)
    if (!atTarget(idx)) return false;
  return true;
}

bool Engine::next(uint8_t *bw, uint8_t *red, uint8_t polishPushes, TickStats &out) {
  if (settled()) {
    if (polished_) return false;
    polished_ = true;
    if (!polishPushes) return false;
    polish(polishPushes);
  }
  out = tick(bw, red);
  return true;
}

}  // namespace fastink
}  // namespace cal
