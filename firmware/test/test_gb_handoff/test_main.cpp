// Fotogramma condiviso fra i core e ritmo dell'emulazione (core/gb_handoff).
#include <string.h>
#include <unity.h>

#include <thread>

#include "core/gb_handoff.h"

using namespace cal::gb;

void setUp() {}
void tearDown() {}

static FrameMailbox gMb;  // 4 KB: fuori dallo stack

namespace {
uint8_t gSeen[kScreenBytes];
void copyOut(const uint8_t *f, void *) { memcpy(gSeen, f, kScreenBytes); }
}  // namespace

void test_take_without_publish_is_nothing() {
  gMb.reset();
  TEST_ASSERT_FALSE(gMb.take(copyOut, nullptr));
  TEST_ASSERT_FALSE(gMb.fresh());
}

void test_latest_wins_and_taken_once() {
  gMb.reset();
  for (uint8_t k = 1; k <= 3; k++) {
    memset(gMb.beginWrite(), k, kScreenBytes);
    gMb.endWrite();
  }
  TEST_ASSERT_EQUAL_UINT32(3, gMb.published());
  TEST_ASSERT_TRUE(gMb.fresh());
  TEST_ASSERT_TRUE(gMb.take(copyOut, nullptr));
  TEST_ASSERT_EQUAL_UINT8(3, gSeen[0]);
  TEST_ASSERT_EQUAL_UINT8(3, gSeen[kScreenBytes - 1]);
  TEST_ASSERT_FALSE(gMb.take(copyOut, nullptr));  // già preso
  memset(gMb.beginWrite(), 4, kScreenBytes);
  gMb.endWrite();
  TEST_ASSERT_TRUE(gMb.take(copyOut, nullptr));
  TEST_ASSERT_EQUAL_UINT8(4, gSeen[0]);
}

// Due thread veri: ogni fotogramma preso è intero (tutti i byte uguali) e i numeri crescono.
namespace {
struct Seen {
  int bad, seen, last;
};
void check(const uint8_t *f, void *ctx) {
  Seen &s = *static_cast<Seen *>(ctx);
  const uint8_t k = f[0];
  for (size_t i = 1; i < kScreenBytes; i++)
    if (f[i] != k) {
      s.bad++;
      break;
    }
  if (k < s.last) s.bad++;
  s.last = k;
  s.seen++;
}
}  // namespace

void test_threads_never_tear() {
  gMb.reset();
  Seen s = {0, 0, 0};
  std::thread cons([&s] {
    while (s.last != 250) {  // il più recente non si perde mai
      gMb.take(check, &s);
      std::this_thread::yield();
    }
  });
  for (int k = 1; k <= 250; k++) {
    memset(gMb.beginWrite(), k, kScreenBytes);
    gMb.endWrite();
    std::this_thread::yield();
  }
  cons.join();
  TEST_ASSERT_EQUAL(0, s.bad);
  TEST_ASSERT_TRUE(s.seen > 0);
  TEST_ASSERT_EQUAL(250, s.last);
}

void test_pacer_on_time_and_early() {
  Pacer p;
  p.start(1000000);
  TEST_ASSERT_EQUAL(1, p.due(1000000));
  p.ran(1);
  TEST_ASSERT_EQUAL(0, p.due(1000000 + 16000));
  TEST_ASSERT_EQUAL(743, int(p.waitUs(1000000 + 16000)));  // 16 742,706 µs arrotondati in su
  TEST_ASSERT_EQUAL(1, p.due(1000000 + 16743));
}

void test_pacer_small_delay_bursts() {
  Pacer p;
  p.start(0);
  TEST_ASSERT_EQUAL(2, p.due(40000));  // 3 dovuti, al massimo 2 di fila
  p.ran(2);
  TEST_ASSERT_EQUAL(1, p.due(40000));
  p.ran(1);
  TEST_ASSERT_EQUAL(0, p.due(40000));
  TEST_ASSERT_EQUAL(0u, p.dropped());
}

void test_pacer_long_stall_resyncs() {
  Pacer p;
  p.start(0);
  p.ran(1);
  TEST_ASSERT_EQUAL(1, p.due(500000));  // 0,5 s fermo (refresh lento, flash): niente corsa
  TEST_ASSERT_TRUE(p.dropped() >= 25);
  p.ran(1);
  TEST_ASSERT_EQUAL(0, p.due(500000));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_take_without_publish_is_nothing);
  RUN_TEST(test_latest_wins_and_taken_once);
  RUN_TEST(test_threads_never_tear);
  RUN_TEST(test_pacer_on_time_and_early);
  RUN_TEST(test_pacer_small_delay_bursts);
  RUN_TEST(test_pacer_long_stall_resyncs);
  return UNITY_END();
}
