// Regole della partita (core/gb_session) e tasti dal controller (core/gb_input).
#include <unity.h>

#include "core/gb_emu.h"
#include "core/gb_input.h"
#include "core/gb_session.h"

using namespace cal;
using namespace cal::gb;

void setUp() {}
void tearDown() {}

namespace {
pad::State st(uint16_t buttons, int lx = 0, int ly = 0) {
  pad::State s = {};
  s.buttons = buttons;
  s.lx = int8_t(lx);
  s.ly = int8_t(ly);
  return s;
}
SessionIn ok() { return SessionIn{true, 0, 80, false, false}; }
}  // namespace

// ---------- tasti ----------
void test_map_buttons() {
  TEST_ASSERT_EQUAL_HEX8(KeyA | KeyStart, mapPad(st(pad::A | pad::Menu)).keys);
  TEST_ASSERT_EQUAL_HEX8(KeyB | KeySelect, mapPad(st(pad::B | pad::View)).keys);
  TEST_ASSERT_EQUAL_HEX8(KeyUp | KeyLeft, mapPad(st(pad::Up | pad::Left)).keys);
  TEST_ASSERT_EQUAL_HEX8(0, mapPad(st(pad::X | pad::Y | pad::LB | pad::RB)).keys);
}

void test_map_stick_with_deadzone() {
  TEST_ASSERT_EQUAL_HEX8(0, mapPad(st(0, 50, -50)).keys);           // metà corsa: niente
  TEST_ASSERT_EQUAL_HEX8(KeyRight | KeyUp, mapPad(st(0, 51, -51)).keys);  // Y negativo = su
  TEST_ASSERT_EQUAL_HEX8(KeyDown | KeyLeft, mapPad(st(0, -100, 100)).keys);
}

void test_opposite_directions_cancel() {
  TEST_ASSERT_EQUAL_HEX8(0, mapPad(st(pad::Up, 0, 100)).keys);  // croce su + levetta giù
  TEST_ASSERT_EQUAL_HEX8(KeyA, mapPad(st(pad::A | pad::Left | pad::Right)).keys);
}

void test_pause_chord_not_forwarded() {
  PadKeys k = mapPad(st(pad::Menu | pad::View | pad::A));
  TEST_ASSERT_TRUE(k.pause);
  TEST_ASSERT_EQUAL_HEX8(0, k.keys);
  k = mapPad(st(pad::Guide));
  TEST_ASSERT_TRUE(k.pause);
  TEST_ASSERT_FALSE(mapPad(st(pad::Menu)).pause);
}

void test_pause_edge() {
  PauseEdge e;
  TEST_ASSERT_TRUE(e.update(true));
  TEST_ASSERT_FALSE(e.update(true));  // ancora tenuta
  TEST_ASSERT_FALSE(e.update(false));
  e.arm(true);                         // Riprendi con la combinazione ancora giù
  TEST_ASSERT_FALSE(e.update(true));
}

// ---------- regole ----------
void test_controller_lost_pauses_immediately() {
  Session s;
  s.start(0, 0);
  SessionIn in = ok();
  in.padReady = false;
  const Verdict v = s.update(in, 10);
  TEST_ASSERT_EQUAL(int(Verdict::Pause), int(v.act));
  TEST_ASSERT_EQUAL(int(PauseWhy::Controller), int(v.pause));
  TEST_ASSERT_EQUAL(int(Phase::Paused), int(s.phase()));
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 20).act));  // una volta sola
  TEST_ASSERT_FALSE(s.resume(in, 30));                               // senza controller non si riprende
  in.padReady = true;
  TEST_ASSERT_TRUE(s.resume(in, 40));
}

void test_idle_five_minutes() {
  Session s;
  s.start(0, 0);
  s.input(60000);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(ok(), 60000 + kIdlePauseMs - 1).act));
  const Verdict v = s.update(ok(), 60000 + kIdlePauseMs);
  TEST_ASSERT_EQUAL(int(PauseWhy::Idle), int(v.pause));
}

void test_new_perm_pauses_once() {
  Session s;
  s.start(0, 3);  // tre richieste arrivate prima della partita: non contano
  SessionIn in = ok();
  in.permSeq = 3;
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 1).act));
  in.permSeq = 4;
  TEST_ASSERT_EQUAL(int(PauseWhy::Perm), int(s.update(in, 5).pause));
  TEST_ASSERT_TRUE(s.resume(in, 6));  // si riprende anche senza rispondere: la richiesta resta aperta
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 7).act));
  in.permSeq = 5;
  TEST_ASSERT_EQUAL(int(PauseWhy::Perm), int(s.update(in, 8).pause));
}

void test_perm_while_paused_is_not_lost() {
  Session s;
  s.start(0, 0);
  s.pause(PauseWhy::User, 1);
  SessionIn in = ok();
  in.permSeq = 1;  // arriva in pausa: niente da fare, è già in pausa
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 2).act));
  TEST_ASSERT_TRUE(s.resume(in, 3));
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(in, 4).act));
}

void test_paused_ten_minutes_exits() {
  Session s;
  s.start(0, 0);
  s.pause(PauseWhy::User, 1000);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(s.update(ok(), 1000 + kPausedExitMs - 1).act));
  const Verdict v = s.update(ok(), 1000 + kPausedExitMs);
  TEST_ASSERT_EQUAL(int(Verdict::Exit), int(v.act));
  TEST_ASSERT_EQUAL(int(ExitWhy::PausedTooLong), int(v.exit));
}

void test_battery_band_and_exit() {
  Session s;
  s.start(0, 0);
  SessionIn in = ok();
  in.battPct = 15;
  TEST_ASSERT_FALSE(s.update(in, 1).lowBand);
  in.battPct = 14;
  Verdict v = s.update(in, 2);
  TEST_ASSERT_TRUE(v.lowBand);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(v.act));
  in.battPct = 4;
  v = s.update(in, 3);
  TEST_ASSERT_EQUAL(int(ExitWhy::Battery), int(v.exit));
}

void test_battery_critical_exits_even_paused_and_usb_wins() {
  Session s;
  s.start(0, 0);
  s.pause(PauseWhy::User, 1);
  SessionIn in = ok();
  in.battCritical = true;  // la scheda andrebbe a dormire: prima si salva
  TEST_ASSERT_EQUAL(int(ExitWhy::Battery), int(s.update(in, 2).exit));
  in.usb = true;
  in.battPct = 2;
  const Verdict v = s.update(in, 3);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(v.act));
  TEST_ASSERT_FALSE(v.lowBand);
}

void test_unknown_battery_never_exits() {
  Session s;
  s.start(0, 0);
  SessionIn in = ok();
  in.battPct = -1;
  const Verdict v = s.update(in, 1);
  TEST_ASSERT_EQUAL(int(Verdict::None), int(v.act));
  TEST_ASSERT_FALSE(v.lowBand);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_map_buttons);
  RUN_TEST(test_map_stick_with_deadzone);
  RUN_TEST(test_opposite_directions_cancel);
  RUN_TEST(test_pause_chord_not_forwarded);
  RUN_TEST(test_pause_edge);
  RUN_TEST(test_controller_lost_pauses_immediately);
  RUN_TEST(test_idle_five_minutes);
  RUN_TEST(test_new_perm_pauses_once);
  RUN_TEST(test_perm_while_paused_is_not_lost);
  RUN_TEST(test_paused_ten_minutes_exits);
  RUN_TEST(test_battery_band_and_exit);
  RUN_TEST(test_battery_critical_exits_even_paused_and_usb_wins);
  RUN_TEST(test_unknown_battery_never_exits);
  return UNITY_END();
}
