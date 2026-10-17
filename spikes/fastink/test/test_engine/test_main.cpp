// SPIKE usa e getta · fastink: test della parte pura (pio test -e native).
#include <unity.h>
#include <string.h>
#include <stdlib.h>
#include "engine.h"
#include "lut.h"
#include "canvas.h"

using namespace fi;

static Engine eng(6);
static Canvas cv;
static uint8_t bw[kRamBytes], red[kRamBytes];

void setUp() {}
void tearDown() {}

static bool bit(const uint8_t *p, int idx) { return p[idx >> 3] & (0x80 >> (idx & 7)); }

// indice pixel (formato RAM) di un punto logico
static int pix(int x, int y) {
  uint8_t m;
  int i = ramIndex(x, y, m);
  int k = 0;
  while ((0x80 >> k) != m) k++;
  return i * 8 + k;
}

static void test_encode() {
  bool b, r;
  encodePixel(kToBlack, true, b, r);
  TEST_ASSERT_FALSE(b); TEST_ASSERT_FALSE(r);          // LUT0
  encodePixel(kToWhite, false, b, r);
  TEST_ASSERT_TRUE(b); TEST_ASSERT_FALSE(r);           // LUT1
  encodePixel(kHold, false, b, r);
  TEST_ASSERT_FALSE(b); TEST_ASSERT_TRUE(r);           // LUT2
  encodePixel(kHold, true, b, r);
  TEST_ASSERT_TRUE(b); TEST_ASSERT_TRUE(r);            // LUT3
}

static void test_canvas_mapping() {
  uint8_t m;
  // (0,0) logico → RAM x 0, y 249
  TEST_ASSERT_EQUAL(249 * 16, ramIndex(0, 0, m));
  TEST_ASSERT_EQUAL_HEX8(0x80, m);
  // (249,121) → RAM x 121, y 0
  TEST_ASSERT_EQUAL(15, ramIndex(249, 121, m));
  TEST_ASSERT_EQUAL_HEX8(0x40, m);
  cv.clear(true);
  cv.px(10, 20, true);
  TEST_ASSERT_TRUE(cv.isBlack(10, 20));
  TEST_ASSERT_FALSE(cv.isBlack(11, 20));
  cv.px(-1, 0, true);  // fuori: ignorato
  cv.px(250, 0, true);
  cv.clear(false);     // nero: colonne invisibili restano bianche
  TEST_ASSERT_EQUAL_HEX8(0x3F, cv.buf[15]);
  TEST_ASSERT_EQUAL_HEX8(0x00, cv.buf[14]);
}

static void test_square_needs_exactly_budget() {
  cv.clear(true);
  eng.setBudget(6);
  eng.syncSaturated(cv.buf);
  TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
  cv.rect(100, 50, 24, 24, true);
  eng.setTarget(cv.buf);
  TEST_ASSERT_EQUAL_UINT32(24 * 24, eng.pending());
  for (int t = 0; t < 6; t++) {
    TickStats s = eng.tick(bw, red);
    TEST_ASSERT_EQUAL_UINT32(24 * 24, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(0, s.toWhite);
    int in = pix(110, 60), out = pix(10, 10);
    TEST_ASSERT_FALSE(bit(bw, in)); TEST_ASSERT_FALSE(bit(red, in));  // spinto al nero
    TEST_ASSERT_TRUE(bit(red, out)); TEST_ASSERT_TRUE(bit(bw, out));  // fermo, bianco
  }
  TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
  TickStats s = eng.tick(bw, red);  // arrivato: nessuna spinta in più
  TEST_ASSERT_EQUAL_UINT32(0, s.driven());
  int in = pix(110, 60);
  TEST_ASSERT_TRUE(bit(red, in)); TEST_ASSERT_FALSE(bit(bw, in));  // fermo nero (LUT2)
}

static void test_reversal_is_symmetric() {
  cv.clear(true);
  eng.setBudget(8);
  eng.syncSaturated(cv.buf);
  cv.px(5, 5, true);
  eng.setTarget(cv.buf);
  for (int t = 0; t < 3; t++) eng.tick(bw, red);
  const int p = pix(5, 5);
  TEST_ASSERT_EQUAL_UINT8(3, eng.level(p));
  cv.px(5, 5, false);  // cambia idea a metà
  eng.setTarget(cv.buf);
  int ticks = 0;
  while (eng.pending()) {
    TickStats s = eng.tick(bw, red);
    TEST_ASSERT_EQUAL_UINT32(1, s.toWhite);
    ticks++;
  }
  TEST_ASSERT_EQUAL(3, ticks);  // solo gli impulsi dati prima: carica netta di nuovo 0
  TEST_ASSERT_EQUAL_UINT8(0, eng.level(p));
}

// Bersagli casuali per 2000 tick: la carica netta di ogni pixel resta in [−N, N]
static void test_dc_bounded() {
  static int32_t net[kPixels];
  memset(net, 0, sizeof(net));
  cv.clear(true);
  eng.setBudget(7);
  eng.syncSaturated(cv.buf);
  srand(42);
  for (int t = 0; t < 2000; t++) {
    if (t % 3 == 0) {
      for (int k = 0; k < 40; k++) cv.rect(rand() % 250, rand() % 122, 1 + rand() % 30, 1 + rand() % 30, rand() & 1);
      eng.setTarget(cv.buf);
    }
    eng.tick(bw, red);
    for (int i = 0; i < kPixels; i += 7) {  // campione: abbastanza e veloce
      if (bit(red, i)) continue;
      net[i] += bit(bw, i) ? -1 : +1;
    }
  }
  for (int i = 0; i < kPixels; i += 7) {
    TEST_ASSERT_TRUE(net[i] >= -7 && net[i] <= 7);
    TEST_ASSERT_EQUAL_INT32(net[i], (int32_t)eng.level(i));  // partenza bianca: livello = carica netta
  }
}

static void test_invisible_columns_never_driven() {
  cv.clear(false);
  eng.setBudget(6);
  cv.clear(true);
  eng.syncSaturated(cv.buf);
  cv.clear(false);
  eng.setTarget(cv.buf);
  TickStats s = eng.tick(bw, red);
  TEST_ASSERT_EQUAL_UINT32(250 * 122, s.toBlack);
  for (int y = 0; y < kRamH; y++) TEST_ASSERT_EQUAL_HEX8(0x3F, red[y * 16 + 15] & 0x3F);
}

static void test_budget_rescale() {
  cv.clear(true);
  eng.setBudget(6);
  eng.syncSaturated(cv.buf);
  cv.px(0, 0, true);
  eng.setTarget(cv.buf);
  eng.tick(bw, red); eng.tick(bw, red); eng.tick(bw, red);  // 3/6
  eng.setBudget(12);
  TEST_ASSERT_EQUAL_UINT8(6, eng.level(pix(0, 0)));
  eng.setBudget(1);
  TEST_ASSERT_EQUAL_UINT8(1, eng.level(pix(0, 0)));
  eng.setBudget(0);  // limitato a 1
  TEST_ASSERT_EQUAL_UINT8(1, eng.budget());
  eng.setBudget(99);
  TEST_ASSERT_EQUAL_UINT8(Engine::kMaxBudget, eng.budget());
}

static void test_hold_planes() {
  cv.clear(true);
  cv.rect(0, 0, 10, 10, true);
  Engine::holdPlanes(cv.buf, bw, red);
  for (int i = 0; i < kRamBytes; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, red[i]);
  TEST_ASSERT_EQUAL_MEMORY(cv.buf, bw, kRamBytes);
}

static void test_lut_layout() {
  uint8_t l[kLut159];
  LutParams p;
  p.tp = 3; p.phases = 2; p.rest = 1; p.fr = 2;
  TEST_ASSERT_EQUAL(153, buildTickLut(p, l, false));
  TEST_ASSERT_EQUAL_HEX8(0x40, l[0]);    // LUT0 gruppo 0 fase A = VSH1
  TEST_ASSERT_EQUAL_HEX8(0x80, l[12]);   // LUT1 gruppo 0 fase A = VSL
  TEST_ASSERT_EQUAL_HEX8(0x00, l[24]);   // LUT2 fermo
  TEST_ASSERT_EQUAL_HEX8(0x00, l[36]);   // LUT3 fermo
  TEST_ASSERT_EQUAL_HEX8(0x00, l[48]);   // LUT4 VCOM = DCVCOM
  for (int i = 1; i < 60; i++) if (i != 12) TEST_ASSERT_EQUAL_HEX8(0, l[i]);
  TEST_ASSERT_EQUAL_UINT8(3, l[60]);     // TP[0A]
  TEST_ASSERT_EQUAL_UINT8(1, l[61]);     // TP[0B]
  TEST_ASSERT_EQUAL_UINT8(1, l[66]);     // RP[0] = 2 − 1
  for (int i = 67; i < 144; i++) TEST_ASSERT_EQUAL_HEX8(0, l[i]);  // altri gruppi vuoti
  for (int i = 144; i < 150; i++) TEST_ASSERT_EQUAL_HEX8(0x22, l[i]);
  for (int i = 150; i < 153; i++) TEST_ASSERT_EQUAL_HEX8(0, l[i]);
  TEST_ASSERT_EQUAL_UINT32(8, tickFrames(p));
  p.swap = true;
  buildTickLut(p, l, false);
  TEST_ASSERT_EQUAL_HEX8(0x80, l[0]);
  TEST_ASSERT_EQUAL_HEX8(0x40, l[12]);
  TEST_ASSERT_EQUAL(159, buildTickLut(p, l, true));
  const uint8_t tail[6] = {0x22, 0x17, 0x41, 0x00, 0x32, 0x36};
  TEST_ASSERT_EQUAL_MEMORY(tail, l + 153, 6);
  p.tp = 0; p.phases = 0;                // limiti
  buildTickLut(p, l, false);
  TEST_ASSERT_EQUAL_UINT8(1, l[60]);
  TEST_ASSERT_EQUAL_UINT8(0, l[66]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_encode);
  RUN_TEST(test_canvas_mapping);
  RUN_TEST(test_square_needs_exactly_budget);
  RUN_TEST(test_reversal_is_symmetric);
  RUN_TEST(test_dc_bounded);
  RUN_TEST(test_invisible_columns_never_driven);
  RUN_TEST(test_budget_rescale);
  RUN_TEST(test_hold_planes);
  RUN_TEST(test_lut_layout);
  return UNITY_END();
}
