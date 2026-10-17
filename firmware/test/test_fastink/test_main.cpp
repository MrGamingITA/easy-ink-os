// Driver veloce, parte pura (core/fastink_engine): geometria, profili, LUT, motore a budget.
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "core/fastink_engine.h"
#include "gfx/canvas.h"

using namespace cal::fastink;

void setUp() {}
void tearDown() {}

static Canvas<kScrW, kScrH> cv;
static uint8_t ram[kRamBytes];

// Indice di pixel nella RAM di un punto logico (rotazione 3: RAM x = y, RAM y = 249 − x).
static int pix(int x, int y) { return (kRamH - 1 - x) * kRamW + y; }
static bool bit(const uint8_t *p, int idx) { return p[idx >> 3] & (0x80 >> (idx & 7)); }

static void test_frame_to_ram() {
  TEST_ASSERT_EQUAL(32, (Canvas<kScrW, kScrH>::kStride));  // stesso formato di submit()
  cv.clear();
  frameToRam(cv.data(), ram);
  for (int i = 0; i < kRamBytes; i++) TEST_ASSERT_EQUAL_HEX8(0xFF, ram[i]);  // tutto bianco
  cv.set(0, 0, true);
  cv.set(249, 121, true);
  cv.set(10, 20, true);
  frameToRam(cv.data(), ram);
  TEST_ASSERT_EQUAL_HEX8(0x7F, ram[249 * kRowBytes + 0]);  // (0,0) → RAM x 0, y 249
  TEST_ASSERT_EQUAL_HEX8(0xBF, ram[0 * kRowBytes + 15]);   // (249,121) → RAM x 121, y 0
  TEST_ASSERT_FALSE(bit(ram, pix(10, 20)));
  TEST_ASSERT_TRUE(bit(ram, pix(11, 20)));
  TEST_ASSERT_TRUE(bit(ram, pix(10, 21)));
  cv.clear(true);  // tutto nero (anche i bit di riempimento oltre x 249)
  frameToRam(cv.data(), ram);
  for (int y = 0; y < kRamH; y++) {
    TEST_ASSERT_EQUAL_HEX8(0x00, ram[y * kRowBytes + 0]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, ram[y * kRowBytes + 15]);  // colonne 122..127 invisibili: sempre bianche
  }
}

static void test_profiles() {
  const Params c = paramsFor(Profile::Clean);
  TEST_ASSERT_EQUAL_UINT8(10, c.budget);
  TEST_ASSERT_EQUAL_UINT8(8, c.tp);
  TEST_ASSERT_EQUAL_UINT8(1, c.phases);
  TEST_ASSERT_EQUAL_UINT8(7, c.fr);
  TEST_ASSERT_EQUAL_UINT8(2, c.edge);
  TEST_ASSERT_EQUAL_UINT8(2, c.polish);
  const Params s = paramsFor(Profile::Smooth);
  TEST_ASSERT_EQUAL_UINT8(6, s.budget);
  TEST_ASSERT_EQUAL_UINT8(4, s.tp);
  TEST_ASSERT_EQUAL_UINT8(1, s.phases);
  TEST_ASSERT_EQUAL_UINT8(7, s.fr);
  TEST_ASSERT_EQUAL_UINT8(1, s.edge);
  TEST_ASSERT_EQUAL_UINT8(2, s.polish);
  TEST_ASSERT_EQUAL_STRING("pulito", profileName(Profile::Clean));
  TEST_ASSERT_EQUAL_STRING("fluido", profileName(Profile::Smooth));
}

static void test_lut_layout() {
  uint8_t l[kLutLen];
  Params p = paramsFor(Profile::Clean);
  TEST_ASSERT_EQUAL(153, buildTickLut(p, l));
  TEST_ASSERT_EQUAL_HEX8(0x40, l[0]);   // LUT0 gruppo 0 fase A = VSH1 (verso il nero)
  TEST_ASSERT_EQUAL_HEX8(0x80, l[12]);  // LUT1 gruppo 0 fase A = VSL (verso il bianco)
  for (int i = 1; i < 60; i++)
    if (i != 12) TEST_ASSERT_EQUAL_HEX8(0, l[i]);  // LUT2/3 ferme, LUT4 = DCVCOM
  TEST_ASSERT_EQUAL_UINT8(8, l[60]);    // TP[0A] = F
  for (int i = 61; i < 144; i++) TEST_ASSERT_EQUAL_HEX8(0, l[i]);  // RP[0] = P − 1 = 0, altri gruppi vuoti
  for (int i = 144; i < 150; i++) TEST_ASSERT_EQUAL_HEX8(0x77, l[i]);  // FR 7 in tutti i nibble
  for (int i = 150; i < 153; i++) TEST_ASSERT_EQUAL_HEX8(0, l[i]);     // XON
  p.tp = 0;
  p.phases = 3;
  p.fr = 9;
  buildTickLut(p, l);
  TEST_ASSERT_EQUAL_UINT8(1, l[60]);    // F almeno 1
  TEST_ASSERT_EQUAL_UINT8(2, l[66]);    // RP[0] = 3 − 1
  TEST_ASSERT_EQUAL_HEX8(0x11, l[144]); // FR limitato a 3 bit (9 & 7)
}

static Engine eng;
static uint8_t bw[kRamBytes], red[kRamBytes];

static void target(uint8_t edge = 0) {
  frameToRam(cv.data(), ram);
  eng.setTarget(ram, edge);
}
// Pannello bianco e motore pronto con budget n.
static void start(uint8_t n) {
  TEST_ASSERT_TRUE(eng.init(n));
  cv.clear();
  frameToRam(cv.data(), ram);
  eng.syncSaturated(ram);
}
static bool driven(int idx) { return !bit(red, idx); }

static void test_square_needs_exactly_budget() {
  start(6);
  TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
  cv.fillRect(100, 50, 24, 24, true);
  target();
  TEST_ASSERT_EQUAL_UINT32(24 * 24, eng.pending());
  for (int t = 0; t < 6; t++) {
    const TickStats s = eng.tick(bw, red);
    TEST_ASSERT_EQUAL_UINT32(24 * 24, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(0, s.toWhite);
    TEST_ASSERT_FALSE(bit(bw, pix(110, 60)));  // LUT0: spinto al nero
    TEST_ASSERT_FALSE(bit(red, pix(110, 60)));
    TEST_ASSERT_TRUE(bit(red, pix(10, 10)));   // fermo, bianco (LUT3)
    TEST_ASSERT_TRUE(bit(bw, pix(10, 10)));
  }
  TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
  TEST_ASSERT_EQUAL_UINT32(0, eng.tick(bw, red).driven());  // arrivato: niente di più
  TEST_ASSERT_TRUE(bit(red, pix(110, 60)));   // fermo nero (LUT2)
  TEST_ASSERT_FALSE(bit(bw, pix(110, 60)));
}

// Stato compatto (0.12.0): livello in 4 bit, budget massimo 15; oltre si ferma a 15.
static void test_budget_max_15() {
  start(20);
  TEST_ASSERT_EQUAL_UINT8(15, eng.budget());
  cv.set(30, 30, true);
  target();
  int ticks = 0;
  while (eng.pending()) {
    TEST_ASSERT_EQUAL_UINT32(1, eng.tick(bw, red).toBlack);
    ticks++;
  }
  TEST_ASSERT_EQUAL(15, ticks);
  TEST_ASSERT_EQUAL_UINT8(15, eng.level(pix(30, 30)));
}

// 0.12.0: 4 bit di livello + 2 di spinte extra per pixel e il bersaglio: 28 000 byte (prima 40 000).
static void test_state_is_compact() {
  TEST_ASSERT_EQUAL_UINT8(15, Engine::kMaxBudget);
  TEST_ASSERT_EQUAL_UINT8(3, Engine::kMaxExtra);
  TEST_ASSERT_EQUAL(28000, int(Engine::kStateBytes));
}

// Il piano 0x26 fa anche da memoria per l'idoneità delle spinte extra: quello che c'era prima non conta.
static void test_tick_ignores_old_planes() {
  start(6);
  cv.set(100, 60, true);
  target(2);
  static uint8_t bw2[kRamBytes], red2[kRamBytes];
  for (int t = 0; t < 10; t++) {
    memset(bw2, 0xA5, sizeof bw2);
    memset(red2, t & 1 ? 0xFF : 0x5A, sizeof red2);
    const TickStats s = eng.tick(bw2, red2);
    TEST_ASSERT_EQUAL_UINT32(t < 6 ? 1u : t < 8 ? 5u : 0u, s.driven());
  }
  TEST_ASSERT_TRUE(eng.settled());
}

static void test_reversal_is_symmetric() {
  start(8);
  cv.set(5, 5, true);
  target();
  for (int t = 0; t < 3; t++) eng.tick(bw, red);
  TEST_ASSERT_EQUAL_UINT8(3, eng.level(pix(5, 5)));
  cv.set(5, 5, false);  // cambia idea a metà
  target();
  int ticks = 0;
  while (eng.pending()) {
    TEST_ASSERT_EQUAL_UINT32(1, eng.tick(bw, red).toWhite);
    ticks++;
  }
  TEST_ASSERT_EQUAL(3, ticks);  // solo gli impulsi dati prima: carica netta di nuovo 0
  TEST_ASSERT_EQUAL_UINT8(0, eng.level(pix(5, 5)));
}

// Bersagli casuali per 2000 tick senza spinte extra: la carica netta di ogni pixel resta in [−N, N].
static void test_dc_bounded() {
  static int32_t net[kPixels];
  memset(net, 0, sizeof net);
  start(7);
  srand(42);
  for (int t = 0; t < 2000; t++) {
    if (t % 3 == 0) {
      for (int k = 0; k < 40; k++) cv.fillRect(rand() % 250, rand() % 122, 1 + rand() % 30, 1 + rand() % 30, rand() & 1);
      target();
    }
    eng.tick(bw, red);
    for (int i = 0; i < kPixels; i += 7) {
      if (!driven(i)) continue;
      net[i] += bit(bw, i) ? -1 : +1;
    }
  }
  for (int i = 0; i < kPixels; i += 7) {
    TEST_ASSERT_TRUE(net[i] >= -7 && net[i] <= 7);
    TEST_ASSERT_EQUAL_INT32(net[i], static_cast<int32_t>(eng.level(i)));  // partenza bianca: livello = carica netta
  }
}

static void test_invisible_columns_never_driven() {
  start(6);
  cv.clear(true);
  target(3);
  const TickStats s = eng.tick(bw, red);
  TEST_ASSERT_EQUAL_UINT32(250 * 122, s.toBlack);
  for (int y = 0; y < kRamH; y++) TEST_ASSERT_EQUAL_HEX8(0x3F, red[y * kRowBytes + 15] & 0x3F);
  for (int y = 0; y < kRamH; y++) TEST_ASSERT_EQUAL_UINT8(0, eng.extra(y * kRamW + 122));  // niente spinte extra
}

static void test_budget_rescale() {
  start(6);
  cv.set(0, 0, true);
  target();
  eng.tick(bw, red); eng.tick(bw, red); eng.tick(bw, red);  // 3/6
  eng.setBudget(12);
  TEST_ASSERT_EQUAL_UINT8(6, eng.level(pix(0, 0)));
  eng.setBudget(1);
  TEST_ASSERT_EQUAL_UINT8(1, eng.level(pix(0, 0)));
  eng.setBudget(0);
  TEST_ASSERT_EQUAL_UINT8(1, eng.budget());
  eng.setBudget(99);
  TEST_ASSERT_EQUAL_UINT8(Engine::kMaxBudget, eng.budget());
}

static void test_init_release() {
  TEST_ASSERT_TRUE(eng.init(6));
  TEST_ASSERT_TRUE(eng.ready());
  eng.release();
  TEST_ASSERT_FALSE(eng.ready());
  TEST_ASSERT_TRUE(eng.init(6));  // di nuovo, senza perdite
}

// Un pixel diventa nero: lui e i 4 vicini ricevono le spinte extra solo dopo che lui è arrivato.
static void test_edge_pushes_after_arrival() {
  start(6);
  cv.set(100, 60, true);
  target(2);
  const int p = pix(100, 60);
  const int nb[4] = {pix(99, 60), pix(101, 60), pix(100, 59), pix(100, 61)};
  TEST_ASSERT_EQUAL_UINT8(2, eng.extra(p));
  for (int k = 0; k < 4; k++) TEST_ASSERT_EQUAL_UINT8(2, eng.extra(nb[k]));
  TEST_ASSERT_EQUAL_UINT8(0, eng.extra(pix(101, 61)));  // diagonale: no
  TEST_ASSERT_EQUAL_UINT8(0, eng.extra(pix(10, 10)));
  for (int t = 0; t < 6; t++) {  // transizione: solo p
    const TickStats s = eng.tick(bw, red);
    TEST_ASSERT_EQUAL_UINT32(1, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(0, s.toWhite);
    TEST_ASSERT_EQUAL_UINT32(0, s.extra);
  }
  for (int t = 0; t < 2; t++) {  // zona ferma: p una spinta verso il nero, i vicini verso il bianco
    const TickStats s = eng.tick(bw, red);
    TEST_ASSERT_EQUAL_UINT32(1, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(4, s.toWhite);
    TEST_ASSERT_EQUAL_UINT32(5, s.extra);
    TEST_ASSERT_FALSE(bit(bw, p));
    for (int k = 0; k < 4; k++) TEST_ASSERT_TRUE(driven(nb[k]) && bit(bw, nb[k]));
  }
  TEST_ASSERT_EQUAL_UINT32(0, eng.tick(bw, red).driven());
  TEST_ASSERT_EQUAL_UINT8(6, eng.level(p));  // le spinte extra non cambiano il livello
}

// Review Focus: fotogrammi più veloci dei tick. Le spinte extra non si sommano e restano ≤ kMaxExtra.
static void test_fast_submits_do_not_stack() {
  start(6);
  for (int i = 0; i < 10; i++) {
    cv.set(5, 5, i % 2 == 0);  // nero, bianco, nero… finisce bianco
    target(3);
  }
  target(99);  // stesso bersaglio: nessun cambio, nessuna spinta in più
  TEST_ASSERT_EQUAL_UINT8(3, eng.extra(pix(5, 5)));
  TEST_ASSERT_EQUAL_UINT8(3, eng.extra(pix(6, 5)));
  cv.set(50, 50, true);
  target(99);
  TEST_ASSERT_EQUAL_UINT8(Engine::kMaxExtra, eng.extra(pix(50, 50)));
  const int c5[5] = {pix(5, 5), pix(6, 5), pix(4, 5), pix(5, 4), pix(5, 6)};
  int pushes = 0, perPixel[5] = {0, 0, 0, 0, 0};
  uint32_t extraTotal = 0;
  for (int t = 0; t < 40; t++) {
    const TickStats s = eng.tick(bw, red);
    extraTotal += s.extra;
    if (driven(pix(5, 5))) pushes++;
    for (int k = 0; k < 5; k++)
      if (driven(c5[k])) perPixel[k]++;
  }
  TEST_ASSERT_EQUAL(3, pushes);  // è già al bersaglio (bianco): solo le 3 spinte extra
  for (int k = 0; k < 5; k++) TEST_ASSERT_EQUAL(3, perPixel[k]);  // nessuna somma dei 10 invii
  TEST_ASSERT_TRUE(extraTotal <= 10u * Engine::kMaxExtra);  // 2 gruppi da 5 pixel, tetto 3 ciascuno
  TEST_ASSERT_EQUAL_UINT32(5 * 3 + 5 * Engine::kMaxExtra, extraTotal);
}

static void test_polish_once_when_settled() {
  start(6);
  TickStats s;
  TEST_ASSERT_TRUE(eng.idle());
  TEST_ASSERT_FALSE(eng.next(bw, red, 2, s));  // dopo il completo: niente
  cv.fillRect(100, 50, 24, 24, true);
  target();
  TEST_ASSERT_FALSE(eng.idle());
  for (int t = 0; t < 6; t++) {
    TEST_ASSERT_TRUE(eng.next(bw, red, 2, s));
    TEST_ASSERT_EQUAL_UINT32(24 * 24, s.toBlack);
  }
  TEST_ASSERT_TRUE(eng.settled());
  for (int t = 0; t < 2; t++) {  // pulizia: tutti i pixel visibili, ognuno verso il suo colore
    TEST_ASSERT_TRUE(eng.next(bw, red, 2, s));
    TEST_ASSERT_EQUAL_UINT32(250 * 122, s.driven());
    TEST_ASSERT_EQUAL_UINT32(24 * 24, s.toBlack);
    TEST_ASSERT_EQUAL_UINT32(250 * 122, s.extra);
    TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
    for (int y = 0; y < kRamH; y++) TEST_ASSERT_EQUAL_HEX8(0x3F, red[y * kRowBytes + 15] & 0x3F);  // invisibili ferme
  }
  TEST_ASSERT_FALSE(eng.next(bw, red, 2, s));
  TEST_ASSERT_TRUE(eng.idle());
  target();  // stesso fotogramma di nuovo (il gioco lo rimanda): nessuna nuova pulizia
  TEST_ASSERT_FALSE(eng.next(bw, red, 2, s));
}

static void test_no_polish_when_zero() {
  start(6);
  TickStats s;
  cv.set(10, 10, true);
  target();
  for (int t = 0; t < 6; t++) TEST_ASSERT_TRUE(eng.next(bw, red, 0, s));
  TEST_ASSERT_FALSE(eng.next(bw, red, 0, s));
  TEST_ASSERT_TRUE(eng.idle());
}

static void test_settled_counts_extras() {
  start(6);
  cv.set(10, 10, true);
  target(1);
  for (int t = 0; t < 6; t++) eng.tick(bw, red);
  TEST_ASSERT_EQUAL_UINT32(0, eng.pending());
  TEST_ASSERT_FALSE(eng.settled());  // spinte extra ancora da dare
  eng.tick(bw, red);
  TEST_ASSERT_TRUE(eng.settled());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_frame_to_ram);
  RUN_TEST(test_profiles);
  RUN_TEST(test_lut_layout);
  RUN_TEST(test_square_needs_exactly_budget);
  RUN_TEST(test_budget_max_15);
  RUN_TEST(test_state_is_compact);
  RUN_TEST(test_tick_ignores_old_planes);
  RUN_TEST(test_reversal_is_symmetric);
  RUN_TEST(test_dc_bounded);
  RUN_TEST(test_invisible_columns_never_driven);
  RUN_TEST(test_budget_rescale);
  RUN_TEST(test_init_release);
  RUN_TEST(test_edge_pushes_after_arrival);
  RUN_TEST(test_fast_submits_do_not_stack);
  RUN_TEST(test_polish_once_when_settled);
  RUN_TEST(test_no_polish_when_zero);
  RUN_TEST(test_settled_counts_extras);
  return UNITY_END();
}
