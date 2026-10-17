// Gesti con un tasto: stessi tempi di keyUp() del simulatore (380 / 700 / 3000 ms).
#include <unity.h>
#include "core/gesture.h"

void setUp() {}
void tearDown() {}

static Gesture tap(GestureRecognizer &g, uint32_t &t, uint32_t hold = 80, uint32_t gap = 150) {
  g.down(t);
  t += hold;
  Gesture r = g.up(t);
  t += gap;
  return r;
}

static Gesture settle(GestureRecognizer &g, uint32_t t) {
  return g.poll(t + GestureRecognizer::kMultiMs + 1);
}

static void test_single() {
  GestureRecognizer g; uint32_t t = 1000;
  TEST_ASSERT_EQUAL(Gesture::None, tap(g, t, 80, 0));
  TEST_ASSERT_EQUAL(Gesture::None, g.poll(t + 100));  // finestra ancora aperta
  TEST_ASSERT_EQUAL(Gesture::Press1, settle(g, t));
  TEST_ASSERT_EQUAL(Gesture::None, settle(g, t + 1000));  // non si ripete
}

static void test_double_and_triple() {
  GestureRecognizer g; uint32_t t = 1000;
  tap(g, t); tap(g, t, 80, 0);
  TEST_ASSERT_EQUAL(Gesture::Press2, settle(g, t));
  t += 2000;
  tap(g, t); tap(g, t); tap(g, t, 80, 0);
  TEST_ASSERT_EQUAL(Gesture::Press3, settle(g, t));
  t += 2000;
  for (int i = 0; i < 5; i++) tap(g, t, 80, i == 4 ? 0 : 150);
  TEST_ASSERT_EQUAL(Gesture::Press3, settle(g, t));  // oltre 3 vale 3, come Math.min(count, 3)
}

static void test_gap_too_long_splits() {
  GestureRecognizer g; uint32_t t = 1000;
  tap(g, t, 80, 0);
  TEST_ASSERT_EQUAL(Gesture::Press1, g.poll(t + 381));
  t += 381;
  tap(g, t, 80, 0);
  TEST_ASSERT_EQUAL(Gesture::Press1, settle(g, t));
}

static void test_long_and_very_long() {
  GestureRecognizer g; uint32_t t = 1000;
  TEST_ASSERT_EQUAL(Gesture::None, tap(g, t, 699));
  TEST_ASSERT_EQUAL(Gesture::Press1, settle(g, t));
  t += 2000;
  TEST_ASSERT_EQUAL(Gesture::Long, tap(g, t, 700));
  TEST_ASSERT_EQUAL(Gesture::Long, tap(g, t, 2999));
  TEST_ASSERT_EQUAL(Gesture::VeryLong, tap(g, t, 3000));
  TEST_ASSERT_EQUAL(Gesture::None, settle(g, t));
}

static void test_long_cancels_pending_presses() {
  GestureRecognizer g; uint32_t t = 1000;
  tap(g, t, 80, 100);
  TEST_ASSERT_EQUAL(Gesture::Long, tap(g, t, 900, 0));
  TEST_ASSERT_EQUAL(Gesture::None, settle(g, t));
}

static void test_bounce_ignored() {
  GestureRecognizer g; uint32_t t = 1000;
  g.down(t);
  TEST_ASSERT_EQUAL(Gesture::None, g.up(t + 5));  // rimbalzo: ignorato, il tasto resta premuto
  g.down(t + 8);
  TEST_ASSERT_TRUE(g.pressed());
  TEST_ASSERT_EQUAL(Gesture::None, g.up(t + 90));
  TEST_ASSERT_EQUAL(Gesture::Press1, settle(g, t + 90));
}

static void test_no_resolution_while_held() {
  GestureRecognizer g; uint32_t t = 1000;
  tap(g, t, 80, 100);
  g.down(t);
  TEST_ASSERT_EQUAL(Gesture::None, g.poll(t + 1000));  // tenuto: la sequenza aspetta il rilascio
}

// Tenuto 3 s: VeryLong da poll() con il tasto giù, una volta sola; il rilascio poi non dà gesti.
static void test_very_long_fires_while_held() {
  GestureRecognizer g; uint32_t t = 1000;
  g.down(t);
  TEST_ASSERT_EQUAL(Gesture::None, g.poll(t + 2999));
  TEST_ASSERT_EQUAL(Gesture::VeryLong, g.poll(t + 3000));
  TEST_ASSERT_TRUE(g.fired());
  TEST_ASSERT_EQUAL(Gesture::None, g.poll(t + 3500));
  TEST_ASSERT_EQUAL(Gesture::None, g.up(t + 5000));  // né Long né VeryLong
  TEST_ASSERT_FALSE(g.fired());
  TEST_ASSERT_EQUAL(Gesture::None, settle(g, t + 5000));
  t += 5200;  // la pressione dopo è normale
  TEST_ASSERT_EQUAL(Gesture::Long, tap(g, t, 900));
}

static void test_pending_presses_dropped_when_hold_fires() {
  GestureRecognizer g; uint32_t t = 1000;
  tap(g, t, 80, 100);
  g.down(t);
  TEST_ASSERT_EQUAL(Gesture::VeryLong, g.poll(t + 3000));
  TEST_ASSERT_EQUAL(Gesture::None, g.up(t + 3100));
  TEST_ASSERT_EQUAL(Gesture::None, settle(g, t + 3100));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single);
  RUN_TEST(test_double_and_triple);
  RUN_TEST(test_gap_too_long_splits);
  RUN_TEST(test_long_and_very_long);
  RUN_TEST(test_long_cancels_pending_presses);
  RUN_TEST(test_bounce_ignored);
  RUN_TEST(test_no_resolution_while_held);
  RUN_TEST(test_very_long_fires_while_held);
  RUN_TEST(test_pending_presses_dropped_when_hold_fires);
  return UNITY_END();
}
