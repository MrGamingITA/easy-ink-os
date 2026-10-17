// Immagine del Game Boy (core/gb_frame, V4): mappa delle righe, tinte, bande, fusione, rotazione e il fotogramma 60
// di dmg-acid2 (impronte qui sotto, 2026-10-15: controllate guardando l'immagine e con un'implementazione Python).
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/gb_emu.h"
#include "core/gb_frame.h"

using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {

void fill(uint8_t *f, uint8_t shade) { memset(f, uint8_t(shade * 0x55), kFrameBytes); }
void setShade(uint8_t *f, int x, int y, uint8_t s) {
  uint8_t &b = f[y * kFrameStride + (x >> 2)];
  const int sh = 6 - 2 * (x & 3);
  b = uint8_t((b & ~(3 << sh)) | (s << sh));
}
uint32_t fnv(const uint8_t *d, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= d[i];
    h *= 16777619u;
  }
  return h;
}
int inkIn(const uint8_t *s, int x0, int y0, int w, int h) {
  int n = 0;
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) n += screenBit(s, x, y);
  return n;
}

}  // namespace

void test_row_map() {
  RowPair g[kLcdH];
  TEST_ASSERT_EQUAL(kRowGroups, rowGroups(g));
  TEST_ASSERT_EQUAL(126, kRowGroups);
  const RowPair m0 = rowMap(0), m5 = rowMap(5), mLast = rowMap(kWinH - 1);
  TEST_ASSERT_EQUAL(2, m0.a);
  TEST_ASSERT_EQUAL(2, m0.b);
  TEST_ASSERT_EQUAL(7, m5.a);
  TEST_ASSERT_EQUAL(8, m5.b);
  TEST_ASSERT_EQUAL(140, mLast.a);
  TEST_ASSERT_EQUAL(140, mLast.b);
  for (int i = 0; i < kWinH; i++) {
    TEST_ASSERT_EQUAL(g[i + 2].a, rowMap(i).a);
    TEST_ASSERT_EQUAL(g[i + 2].b, rowMap(i).b);
  }
  TEST_ASSERT_EQUAL(45, kWinX);
  TEST_ASSERT_EQUAL(160, kWinW);
}

// Tinte piatte: 0 bianco, 1 = 25 %, 2 = 50 %, 3 tutto nero, in ogni blocco 2×2 della finestra.
void test_flat_shades() {
  uint8_t f[kFrameBytes], s[kScreenBytes];
  const int perBlock[4] = {0, 1, 2, 4};
  for (uint8_t sh = 0; sh < 4; sh++) {
    fill(f, sh);
    memset(s, 0, sizeof s);
    scaleDither(f, s);
    TEST_ASSERT_EQUAL(perBlock[sh] * kWinW * kWinH / 4, inkIn(s, kWinX, 0, kWinW, kWinH));
    for (int by = 0; by + 2 <= kWinH; by += 2)
      for (int bx = kWinX + 1; bx + 2 <= kWinX + kWinW; bx += 2) TEST_ASSERT_EQUAL(perBlock[sh], inkIn(s, bx, by, 2, 2));
  }
  // disegno fisso sullo schermo: 1 solo in (pari, pari), 2 a scacchi con x + y dispari
  fill(f, 1);
  memset(s, 0, sizeof s);
  scaleDither(f, s);
  TEST_ASSERT_TRUE(screenBit(s, 46, 0));
  TEST_ASSERT_FALSE(screenBit(s, 45, 0));
  TEST_ASSERT_FALSE(screenBit(s, 46, 1));
  fill(f, 2);
  memset(s, 0, sizeof s);
  scaleDither(f, s);
  TEST_ASSERT_TRUE(screenBit(s, 45, 0));
  TEST_ASSERT_FALSE(screenBit(s, 46, 0));
  TEST_ASSERT_TRUE(screenBit(s, 46, 1));
}

void test_bands_untouched() {
  uint8_t f[kFrameBytes], s[kScreenBytes];
  for (uint8_t sh = 0; sh < 4; sh += 3) {
    fill(f, sh);
    memset(s, 0xA5, sizeof s);
    uint8_t before[kScreenBytes];
    memcpy(before, s, sizeof s);
    scaleDither(f, s);
    for (int y = 0; y < kScreenH; y++)
      for (int x = 0; x < kScreenW; x++)
        if (x < 45 || x >= 205) TEST_ASSERT_EQUAL(screenBit(before, x, y), screenBit(s, x, y));
    TEST_ASSERT_EQUAL(sh ? kWinW * kWinH : 0, inkIn(s, kWinX, 0, kWinW, kWinH));
  }
}

// Riga 7 nera e riga 8 bianca si fondono nella riga 5 della finestra: vince il nero (e al contrario).
void test_merge_darker_wins() {
  uint8_t f[kFrameBytes], s[kScreenBytes];
  fill(f, 0);
  for (int x = 0; x < kLcdW; x++) setShade(f, x, 7, 3);
  memset(s, 0, sizeof s);
  scaleDither(f, s);
  TEST_ASSERT_EQUAL(kWinW, inkIn(s, kWinX, 5, kWinW, 1));
  TEST_ASSERT_EQUAL(kWinW, inkIn(s, kWinX, 0, kWinW, kWinH));
  fill(f, 0);
  for (int x = 0; x < kLcdW; x++) setShade(f, x, 8, 3);
  memset(s, 0, sizeof s);
  scaleDither(f, s);
  TEST_ASSERT_EQUAL(kWinW, inkIn(s, kWinX, 5, kWinW, 1));
  // righe tagliate (0, 1, 142) e scartata (143): non si vedono
  for (int y : {0, 1, 142, 143}) {
    fill(f, 0);
    for (int x = 0; x < kLcdW; x++) setShade(f, x, y, 3);
    memset(s, 0, sizeof s);
    scaleDither(f, s);
    TEST_ASSERT_EQUAL(0, inkIn(s, 0, 0, kScreenW, kScreenH));
  }
  // colonna x del Game Boy = colonna 45 + x dello schermo
  fill(f, 0);
  for (int y = 0; y < kLcdH; y++) setShade(f, 0, y, 3), setShade(f, 159, y, 3);
  memset(s, 0, sizeof s);
  scaleDither(f, s);
  TEST_ASSERT_EQUAL(kWinH, inkIn(s, 45, 0, 1, kWinH));
  TEST_ASSERT_EQUAL(kWinH, inkIn(s, 204, 0, 1, kWinH));
  TEST_ASSERT_EQUAL(2 * kWinH, inkIn(s, 0, 0, kScreenW, kScreenH));
}

void test_rotate180_is_involution() {
  uint8_t s[kScreenBytes], r[kScreenBytes];
  memset(s, 0, sizeof s);
  screenSet(s, 0, 0, true);
  screenSet(s, 3, 7, true);
  memcpy(r, s, sizeof s);
  rotate180(r);
  TEST_ASSERT_TRUE(screenBit(r, 249, 121));
  TEST_ASSERT_TRUE(screenBit(r, 246, 114));
  TEST_ASSERT_EQUAL(2, inkIn(r, 0, 0, kScreenW, kScreenH));
  rotate180(r);
  TEST_ASSERT_EQUAL_MEMORY(s, r, sizeof s);
}

void test_dmg_acid2_golden() {
  std::ifstream in("test/roms/dmg-acid2.gb", std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string str = ss.str();
  TEST_ASSERT_EQUAL_MESSAGE(32768, int(str.size()), "manca test/roms/dmg-acid2.gb");
  std::vector<uint8_t> rom(str.begin(), str.end());
  Emu e;
  TEST_ASSERT_EQUAL(int(Load::Ok), int(e.load(rom.data(), rom.size())));
  for (int i = 0; i < 60; i++) TEST_ASSERT_TRUE(e.runFrame(0));
  uint8_t bands[kScreenBytes], out[kScreenBytes];
  memset(bands, 0, sizeof bands);
  composeFrame(bands, e.frame(), false, out);
  if (const char *d = getenv("GB_FRAME_DUMP")) {  // verifica a mano: fotogramma grezzo e PBM
    std::ofstream(std::string(d) + ".raw", std::ios::binary).write((const char *)e.frame(), kFrameBytes);
    std::ofstream p(std::string(d) + ".pbm", std::ios::binary);
    p << "P4\n250 122\n";
    for (int y = 0; y < kScreenH; y++) p.write((const char *)out + y * kScreenStride, 32);
  }
  TEST_ASSERT_EQUAL_HEX32(0x35425f6du, fnv(out, sizeof out));
  composeFrame(bands, e.frame(), true, out);
  TEST_ASSERT_EQUAL_HEX32(0x8b45cce8u, fnv(out, sizeof out));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_row_map);
  RUN_TEST(test_flat_shades);
  RUN_TEST(test_bands_untouched);
  RUN_TEST(test_merge_darker_wins);
  RUN_TEST(test_rotate180_is_involution);
  RUN_TEST(test_dmg_acid2_golden);
  return UNITY_END();
}
