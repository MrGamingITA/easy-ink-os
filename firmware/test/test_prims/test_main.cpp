// Le primitive in C++ devono coincidere pixel per pixel con quelle del simulatore.
#include <unity.h>
#include <stdio.h>
#include "gfx/prims.h"
#include "golden.h"

void setUp() {}
void tearDown() {}

typedef Canvas<PG_W, PG_H> Cv;

static void draw_case(int k, Surface &c) {
  switch (k) {
    case 0: box(c, 2, 3, 20, 10, true, 1); break;
    case 1: box(c, 1, 1, 30, 20, true, 2); break;
    case 2: dotsH(c, 0, 60, 2); dotsV(c, 5, 4, 30); break;
    case 3: strike(c, 3, 3, 6, 2, true, false); break;
    case 4: tri(c, 1, 1, 1, Dir::Up); tri(c, 10, 1, 2, Dir::Down); tri(c, 30, 1, 1, Dir::Left); tri(c, 40, 1, 2, Dir::Right); break;
    case 5: chevL(c, 1, 1, true); chevL(c, 10, 1, true, 2); break;
    case 6: {
      const char *st[] = {"done", "error", "input", "permission", "idle", "compact"};
      for (int i = 0; i < 6; i++) iconForState(c, st[i], 1 + i * 9, 1, 1, 0);
      iconForState(c, "working", 1, 12, 2, 0);
      iconForState(c, "working", 20, 12, 2, 1);
      break;
    }
    case 7: for (int i = 0; i < 8; i++) sym(c, (Sym)i, 1 + i * 8, 1, 1, true); break;
    case 8: bar(c, 1, 1, 60, 9, {3, 7, true}, 0); break;
    case 9: bar(c, 1, 1, 60, 9, {3, 7, true}, 1); break;
    case 10: bar(c, 1, 1, 60, 9, {6, 6, false}, 0); break;
    case 11: bar(c, 1, 12, 61, 7, {5, 20, true}, 2); break;
    case 12: bar(c, 1, 1, 50, 8, {0, 0, true}, 3); break;
    case 13: bar(c, 1, 1, 50, 8, {0, 0, false}, 3); break;
    case 14:
      checkbox(c, 1, 1, 0, 0); checkbox(c, 14, 1, 1, 0); checkbox(c, 27, 1, 1, 1); checkbox(c, 40, 1, 2, 0);
      checkbox(c, 1, 14, 2, 0, 14); checkbox(c, 20, 14, 2, 0, 7);
      break;
    case 15:
      wifiIcon(c, 1, 1, 1, 0, true); wifiIcon(c, 14, 1, 1, 2, true); wifiIcon(c, 27, 1, 1, 4, true);
      wifiIcon(c, 40, 1, 1, -1, true); wifiIcon(c, 1, 12, 2, 3, true);
      break;
    case 16: battIcon(c, 1, 1, 1, 78, true); battIcon(c, 20, 1, 1, 5, true); battIcon(c, 1, 12, 2, 33, true); break;
    case 17:
      scrollbar(c, 2, 1, 30, 0, 4, 10); scrollbar(c, 10, 1, 30, 3, 4, 10);
      scrollbar(c, 18, 1, 30, 6, 4, 10); scrollbar(c, 26, 1, 30, 0, 5, 5);
      break;
    case 18: bigBar(c, 1, 1, 60, 10, 62); bigBar(c, 1, 16, 41, 7, 100); break;
  }
}

static void test_all_cases() {
  TEST_ASSERT_EQUAL(19, PG_N);
  for (int k = 0; k < PG_N; k++) {
    Cv cv;
    cv.clear();
    CanvasSurface<PG_W, PG_H> s(cv);
    draw_case(k, s);
    for (int y = 0; y < PG_H; y++)
      for (int x = 0; x < PG_W; x++)
        if (cv.get(x, y) != (PG[k][y * PG_W + x] == '#')) {
          char msg[96];
          snprintf(msg, sizeof msg, "caso %s: pixel (%d,%d) diverso", PG_NAMES[k], x, y);
          TEST_FAIL_MESSAGE(msg);
        }
  }
}

static void test_js_round() {
  TEST_ASSERT_EQUAL(3, jsRound(2.5));
  TEST_ASSERT_EQUAL(-2, jsRound(-2.5));
  TEST_ASSERT_EQUAL(2, jsRound(2.49));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_all_cases);
  RUN_TEST(test_js_round);
  return UNITY_END();
}
