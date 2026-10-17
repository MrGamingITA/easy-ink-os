#include "gb_frame.h"

#include <string.h>

#include "gb_emu.h"

namespace cal {
namespace gb {

namespace {

const int kCropTop = (kRowGroups - kWinH) / 2;  // 2
const int kB0 = kWinX >> 3, kB1 = (kWinX + kWinW - 1) >> 3;  // byte di schermo toccati dalla finestra (5–25)

// Mappa delle righe e maschere dei byte della finestra, calcolate una volta.
struct Tables {
  RowPair map[kWinH];
  uint8_t mask[kScreenStride];  // 1 = bit della finestra
  Tables() {
    RowPair g[kLcdH];
    const int n = rowGroups(g);
    for (int i = 0; i < kWinH && i + kCropTop < n; i++) map[i] = g[i + kCropTop];
    memset(mask, 0, sizeof mask);
    for (int x = kWinX; x < kWinX + kWinW; x++) mask[x >> 3] |= uint8_t(0x80 >> (x & 7));
  }
};

const Tables &tables() {
  static const Tables t;
  return t;
}

}  // namespace

int rowGroups(RowPair *out) {
  int n = 0;
  for (int y = 0; y < kLcdH;) {
    if ((y & 7) == 7) {
      if (y + 1 < kLcdH) out[n++] = {uint8_t(y), uint8_t(y + 1)};
      y += 2;  // la 143 si scarta
    } else {
      out[n++] = {uint8_t(y), uint8_t(y)};
      y++;
    }
  }
  return n;
}

RowPair rowMap(int oy) { return tables().map[oy]; }

void scaleDither(const uint8_t *f, uint8_t *screen) {
  const Tables &t = tables();
  for (int oy = 0; oy < kWinH; oy++) {
    const uint8_t *ra = f + t.map[oy].a * kFrameStride, *rb = f + t.map[oy].b * kFrameStride;
    // disegno della riga: per ciascuna tonalità, nero nelle x pari e dispari
    bool pat[4][2];
    for (int v = 0; v < 4; v++)
      for (int p = 0; p < 2; p++) pat[v][p] = inkFor(uint8_t(v), p, oy);
    uint8_t line[kScreenStride] = {0};
    int X = kWinX;
    for (int i = 0; i < kFrameStride; i++) {
      const uint8_t ba = ra[i], bb = rb[i];
      for (int sh = 6; sh >= 0; sh -= 2, X++) {
        const uint8_t va = (ba >> sh) & 3, vb = (bb >> sh) & 3;
        if (pat[va > vb ? va : vb][X & 1]) line[X >> 3] |= uint8_t(0x80 >> (X & 7));
      }
    }
    uint8_t *row = screen + oy * kScreenStride;
    for (int b = kB0; b <= kB1; b++) row[b] = uint8_t((row[b] & ~t.mask[b]) | line[b]);
  }
}

void rotate180(uint8_t *s) {
  const int n = kScreenW * kScreenH;
  for (int i = 0; i < n / 2; i++) {
    const int j = n - 1 - i;
    const int xi = i % kScreenW, yi = i / kScreenW, xj = j % kScreenW, yj = j / kScreenW;
    const bool a = screenBit(s, xi, yi), b = screenBit(s, xj, yj);
    screenSet(s, xi, yi, b);
    screenSet(s, xj, yj, a);
  }
}

void composeFrame(const uint8_t *bands, const uint8_t *gbFrame, bool rot, uint8_t *out) {
  memcpy(out, bands, kScreenBytes);
  scaleDither(gbFrame, out);
  if (rot) rotate180(out);
}

}  // namespace gb
}  // namespace cal
