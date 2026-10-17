// 0.14.0 (R4): chiusure del WebSocket a raffica (core/close_storm). La prima e la seconda breve riprovano subito come
// prima; dalla terza, o subito dopo un'allocazione fallita, il primo tentativo aspetta 5, 10, 20, 30 s.
#include <unity.h>

#include "core/close_storm.h"

using namespace cal;

void setUp() {}
void tearDown() {}

// Connessione aperta a t e chiusa dopo lived ms: l'attesa data.
static uint32_t cycle(CloseStorm &s, uint32_t &t, uint32_t lived, bool alloc = false) {
  s.opened(t);
  t += lived;
  return s.closed(t, alloc);
}

static void test_two_short_closes_retry_now() {
  CloseStorm s;
  uint32_t t = 1000;
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(10000, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(20000, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(30000, cycle(s, t, 800));
  TEST_ASSERT_EQUAL_UINT32(30000, cycle(s, t, 800));  // mai oltre i tentativi lenti
}

static void test_alloc_failure_waits_at_once() {
  CloseStorm s;
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 900, true));
  TEST_ASSERT_EQUAL_UINT32(10000, cycle(s, t, 900, true));
  // anche dopo una connessione di 40 s (né breve né stabile): l'allocazione fallita conta sempre
  TEST_ASSERT_EQUAL_UINT32(20000, cycle(s, t, 40000, true));
}

static void test_stable_connection_ends_storm() {
  CloseStorm s;
  uint32_t t = 0;
  for (int i = 0; i < 4; i++) cycle(s, t, 500);
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 61000));  // un minuto collegata: finita
  TEST_ASSERT_EQUAL(0, s.shortCloses());
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 500));
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 500));
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 500));
}

static void test_far_apart_closes_are_not_a_storm() {
  CloseStorm s;
  uint32_t t = 0;
  for (int i = 0; i < 10; i++) {
    TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 1000));
    t += 70000;  // senza connessione per più della finestra (Wi-Fi giù, PC spento)
  }
}

static void test_medium_connection_keeps_count() {
  CloseStorm s;
  uint32_t t = 0;
  cycle(s, t, 500);
  cycle(s, t, 500);
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 30000));  // né breve né stabile
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 500));
}

// Il ciclo visto dal vivo: ogni connessione cade dopo ~1 s per l'avatar (allocazione fallita) e la scheda si
// ricollegava subito (100 connessioni in 10 minuti). Con l'attesa: al più 25 in 10 minuti.
static void test_live_loop_slows_down() {
  CloseStorm s;
  uint32_t t = 0;
  int conns = 0;
  while (t < 600000) {
    conns++;
    t += 300;  // collegamento
    const uint32_t hold = cycle(s, t, 1000, true);
    t += hold;
  }
  TEST_ASSERT_TRUE_MESSAGE(conns <= 25, "troppe connessioni in 10 minuti");
}

// La finestra è una catena (regola del brief): brevi ogni 50 s non si azzerano, la terza aspetta.
static void test_chain_of_shorts_every_50s() {
  CloseStorm s;
  uint32_t t = 0;
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 1000));
  t += 50000;
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 1000));
  t += 50000;
  TEST_ASSERT_EQUAL_UINT32(5000, cycle(s, t, 1000));
}

// closed() senza opened() (tentativo fallito prima dell'apertura) non conta e non cambia niente.
static void test_closed_without_opened_is_noop() {
  CloseStorm s;
  uint32_t t = 0;
  for (int i = 0; i < 5; i++) {
    t += 1000;
    TEST_ASSERT_EQUAL_UINT32(0, s.closed(t, false));
  }
  TEST_ASSERT_EQUAL(0, s.shortCloses());
  TEST_ASSERT_EQUAL_UINT32(0, cycle(s, t, 500));
  TEST_ASSERT_EQUAL_UINT32(0, s.closed(t + 10, false));  // doppia chiusura della stessa connessione
  TEST_ASSERT_EQUAL(1, s.shortCloses());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_two_short_closes_retry_now);
  RUN_TEST(test_alloc_failure_waits_at_once);
  RUN_TEST(test_stable_connection_ends_storm);
  RUN_TEST(test_far_apart_closes_are_not_a_storm);
  RUN_TEST(test_medium_connection_keeps_count);
  RUN_TEST(test_live_loop_slows_down);
  RUN_TEST(test_chain_of_shorts_every_50s);
  RUN_TEST(test_closed_without_opened_is_noop);
  return UNITY_END();
}
