// SPIKE usa e getta · blepad: test della parte pura (report HID, mappatura menu, statistiche).
#include <string.h>
#include <unity.h>

#include "menu_map.h"
#include "stats.h"
#include "xbox_report.h"

using namespace pad;

void setUp() {}
void tearDown() {}

// Report a riposo: stick al centro, grilletti a 0, niente premuto.
static void idle(uint8_t r[16]) {
  memset(r, 0, 16);
  r[1] = 0x80; r[3] = 0x80; r[5] = 0x80; r[7] = 0x80;
}

static void test_parse_idle() {
  uint8_t r[16];
  idle(r);
  State s;
  TEST_ASSERT_TRUE(parse(r, 16, &s));
  TEST_ASSERT_EQUAL_UINT16(0, s.buttons);
  TEST_ASSERT_EQUAL_UINT8(0, s.dpad);
  TEST_ASSERT_EQUAL_INT(0, stickPercent(s.lx));
  TEST_ASSERT_EQUAL_UINT16(0, s.lt);
}

static void test_parse_short_report_rejected() {
  uint8_t r[16];
  idle(r);
  State s;
  s.buttons = kB;
  TEST_ASSERT_FALSE(parse(r, 15, &s));
  TEST_ASSERT_EQUAL_UINT16(kB, s.buttons);  // invariato
  TEST_ASSERT_FALSE(parse(nullptr, 16, &s));
}

static void test_parse_longer_report_accepted() {  // es. Elite 2 o report con byte in più
  uint8_t r[20];
  memset(r, 0, sizeof r);
  idle(r);
  r[13] = 0x01;
  State s;
  TEST_ASSERT_TRUE(parse(r, 20, &s));
  TEST_ASSERT_EQUAL_UINT16(kA, s.buttons);
}

static void test_parse_buttons() {
  uint8_t r[16];
  idle(r);
  r[13] = 0x01 | 0x02 | 0x08 | 0x10 | 0x40 | 0x80;
  State s;
  parse(r, 16, &s);
  TEST_ASSERT_EQUAL_UINT16(kA | kB | kX | kY | kLB | kRB, s.buttons);
  idle(r);
  r[14] = 0x04 | 0x08 | 0x10 | 0x20 | 0x40;
  r[15] = 0x01;
  parse(r, 16, &s);
  TEST_ASSERT_EQUAL_UINT16(kView | kMenu | kXbox | kLS | kRS | kShare, s.buttons);
  idle(r);
  r[13] = 0x04 | 0x20;  // bit non usati
  r[14] = 0x03 | 0x80;
  parse(r, 16, &s);
  TEST_ASSERT_EQUAL_UINT16(0, s.buttons);
}

static void test_parse_axes() {
  uint8_t r[16];
  idle(r);
  r[0] = 0x00; r[1] = 0x00;  // LX tutto a sinistra
  r[2] = 0xff; r[3] = 0xff;  // LY tutto giù
  r[8] = 0xff; r[9] = 0x03;  // LT 1023
  r[10] = 0x00; r[11] = 0x02;  // RT 512
  State s;
  parse(r, 16, &s);
  TEST_ASSERT_EQUAL_UINT16(0, s.lx);
  TEST_ASSERT_EQUAL_UINT16(65535, s.ly);
  TEST_ASSERT_EQUAL_INT(-100, stickPercent(s.lx));
  TEST_ASSERT_EQUAL_INT(100, stickPercent(s.ly));
  TEST_ASSERT_EQUAL_UINT16(1023, s.lt);
  TEST_ASSERT_EQUAL_UINT16(512, s.rt);
}

static void test_hat() {
  TEST_ASSERT_EQUAL_UINT8(0, hatToDpad(0));
  TEST_ASSERT_EQUAL_UINT8(kUp, hatToDpad(1));
  TEST_ASSERT_EQUAL_UINT8(kUp | kRight, hatToDpad(2));
  TEST_ASSERT_EQUAL_UINT8(kRight, hatToDpad(3));
  TEST_ASSERT_EQUAL_UINT8(kDown, hatToDpad(5));
  TEST_ASSERT_EQUAL_UINT8(kLeft, hatToDpad(7));
  TEST_ASSERT_EQUAL_UINT8(kUp | kLeft, hatToDpad(8));
  TEST_ASSERT_EQUAL_UINT8(0, hatToDpad(15));  // «nulla» in altre convenzioni
}

static void test_format() {
  State s;
  s.buttons = kA | kView;
  s.dpad = kUp | kRight;
  s.lt = 1023;
  char b[96];
  format(s, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("A.......V....... croce=U--R L=+0,+0 R=+0,+0 LT=1023 RT=0", b);
  char tiny[8];
  TEST_ASSERT_EQUAL_UINT32(7, format(s, tiny, sizeof tiny));
  TEST_ASSERT_EQUAL_UINT32(7, strlen(tiny));
}

static void test_differs_threshold() {
  State a, b;
  TEST_ASSERT_FALSE(differs(a, b, 600, 8));
  b.lx = a.lx + 500;
  TEST_ASSERT_FALSE(differs(a, b, 600, 8));
  b.lx = a.lx + 700;
  TEST_ASSERT_TRUE(differs(a, b, 600, 8));
  b = a;
  b.dpad = kDown;
  TEST_ASSERT_TRUE(differs(a, b, 600, 8));
  TEST_ASSERT_TRUE(buttonsDiffer(a, b));
}

// ---------- menu ----------
static int run(MenuMapper &m, uint16_t btn, uint8_t dp, uint32_t t, MenuEvent *ev) {
  State s;
  s.buttons = btn;
  s.dpad = dp;
  return m.update(s, t, ev, 8);
}

static void test_menu_dpad_edges() {
  MenuMapper m;
  MenuEvent ev[8];
  TEST_ASSERT_EQUAL_INT(1, run(m, 0, kUp, 0, ev));
  TEST_ASSERT_EQUAL(MenuEvent::Up, ev[0]);
  TEST_ASSERT_EQUAL_INT(0, run(m, 0, kUp, 100, ev));  // tenuto: niente ripetizione
  TEST_ASSERT_EQUAL_INT(0, run(m, 0, kUp | kRight, 150, ev));  // passa in diagonale: niente
  TEST_ASSERT_EQUAL_INT(0, run(m, 0, 0, 200, ev));
  TEST_ASSERT_EQUAL_INT(1, run(m, 0, kDown | kLeft, 300, ev));  // diagonale giù-sinistra = giù
  TEST_ASSERT_EQUAL(MenuEvent::Down, ev[0]);
  TEST_ASSERT_EQUAL_INT(0, run(m, 0, kLeft, 400, ev));  // destra/sinistra: nessun evento
}

static void test_menu_select_back() {
  MenuMapper m;
  MenuEvent ev[8];
  int n = run(m, kA, 0, 0, ev);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL(MenuEvent::Select, ev[0]);
  TEST_ASSERT_EQUAL(MenuEvent::HoldStart, ev[1]);
  n = run(m, 0, 0, 200, ev);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldCancel, ev[0]);
  n = run(m, kB, 0, 300, ev);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL(MenuEvent::Back, ev[0]);
}

static void test_menu_hold_three_seconds_once() {
  MenuMapper m;
  MenuEvent ev[8];
  run(m, kA, 0, 1000, ev);
  TEST_ASSERT_EQUAL_INT(0, m.tick(1999, ev, 8));
  TEST_ASSERT_EQUAL_INT(1, m.tick(2000, ev, 8));
  TEST_ASSERT_EQUAL(MenuEvent::HoldProgress, ev[0]);
  TEST_ASSERT_EQUAL_INT(0, m.tick(2500, ev, 8));
  TEST_ASSERT_EQUAL_INT(1, m.tick(3000, ev, 8));
  TEST_ASSERT_EQUAL_INT(2, m.holdSeconds());
  TEST_ASSERT_EQUAL_INT(1, m.tick(4000, ev, 8));
  TEST_ASSERT_EQUAL(MenuEvent::HoldDone, ev[0]);
  TEST_ASSERT_EQUAL_INT(0, m.tick(9000, ev, 8));  // una volta sola
  // altri tasti mentre A è tenuto non toccano la tenuta
  TEST_ASSERT_EQUAL_INT(0, run(m, kA | kX, 0, 9100, ev));
  TEST_ASSERT_EQUAL_INT(0, run(m, 0, 0, 9200, ev));  // rilascio dopo la conferma: nessun annullo
  // nuova pressione: si riparte da zero
  run(m, kA, 0, 10000, ev);
  TEST_ASSERT_EQUAL_INT(0, m.tick(10500, ev, 8));
}

static void test_menu_hold_late_report() {
  // Nessun tick in mezzo: il rilascio arriva dopo 3,2 s. Prima scatta la conferma, poi niente annullo.
  MenuMapper m;
  MenuEvent ev[8];
  run(m, kA, 0, 0, ev);
  const int n = run(m, 0, 0, 3200, ev);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldDone, ev[0]);
}

static void test_menu_reset_cancels_hold() {
  MenuMapper m;
  MenuEvent ev[8];
  run(m, kA, kUp, 0, ev);
  TEST_ASSERT_EQUAL_INT(1, m.reset(ev, 8));
  TEST_ASSERT_EQUAL(MenuEvent::HoldCancel, ev[0]);
  TEST_ASSERT_EQUAL_INT(0, m.tick(5000, ev, 8));
  // dopo il reset la croce già premuta conta come nuova pressione
  TEST_ASSERT_EQUAL_INT(1, run(m, 0, kUp, 5100, ev));
}

static void test_menu_wraparound_millis() {
  MenuMapper m;
  MenuEvent ev[8];
  run(m, kA, 0, 0xFFFFF000u, ev);  // millis() vicino al giro
  TEST_ASSERT_EQUAL_INT(0, m.tick(0xFFFFF000u + 999, ev, 8));
  TEST_ASSERT_EQUAL_INT(1, m.tick(0xFFFFF000u + 3000, ev, 8));  // oltre lo zero
  TEST_ASSERT_EQUAL(MenuEvent::HoldDone, ev[0]);
}

// ---------- statistiche ----------
static void test_window() {
  Window w;
  w.threshold = 20;
  TEST_ASSERT_EQUAL_UINT32(0, w.avg());
  w.add(10);
  w.add(30);
  w.add(5);
  TEST_ASSERT_EQUAL_UINT32(3, w.count);
  TEST_ASSERT_EQUAL_UINT32(5, w.min);
  TEST_ASSERT_EQUAL_UINT32(30, w.max);
  TEST_ASSERT_EQUAL_UINT32(15, w.avg());
  TEST_ASSERT_EQUAL_UINT32(1, w.over);
  w.clear();
  TEST_ASSERT_EQUAL_UINT32(0, w.count);
  TEST_ASSERT_EQUAL_UINT32(20, w.threshold);
}

static void test_ping_tracker() {
  PingTracker p(1000);
  const uint32_t a = p.send(0), b = p.send(100);
  uint32_t rtt = 0;
  TEST_ASSERT_TRUE(p.reply(a, 250, &rtt));
  TEST_ASSERT_EQUAL_UINT32(250, rtt);
  TEST_ASSERT_FALSE(p.reply(a, 300, &rtt));    // doppia
  TEST_ASSERT_FALSE(p.reply(999, 300, &rtt));  // sconosciuta
  TEST_ASSERT_EQUAL_INT(0, p.expire(1000));
  TEST_ASSERT_EQUAL_INT(1, p.expire(1200));    // b scade
  TEST_ASSERT_FALSE(p.reply(b, 1300, &rtt));   // risposta tardiva: resta persa
  p.send(2000);
  p.dropAll();
  TEST_ASSERT_EQUAL_UINT32(3, p.sent);
  TEST_ASSERT_EQUAL_UINT32(1, p.received);
  TEST_ASSERT_EQUAL_UINT32(2, p.lost);
}

static void test_ping_tracker_overflow() {
  PingTracker p(1000000);
  for (int i = 0; i < PingTracker::kSlots + 3; i++) p.send(i);
  TEST_ASSERT_EQUAL_UINT32(3, p.lost);  // gli slot riusati contano come persi
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_idle);
  RUN_TEST(test_parse_short_report_rejected);
  RUN_TEST(test_parse_longer_report_accepted);
  RUN_TEST(test_parse_buttons);
  RUN_TEST(test_parse_axes);
  RUN_TEST(test_hat);
  RUN_TEST(test_format);
  RUN_TEST(test_differs_threshold);
  RUN_TEST(test_menu_dpad_edges);
  RUN_TEST(test_menu_select_back);
  RUN_TEST(test_menu_hold_three_seconds_once);
  RUN_TEST(test_menu_hold_late_report);
  RUN_TEST(test_menu_reset_cancels_hold);
  RUN_TEST(test_menu_wraparound_millis);
  RUN_TEST(test_window);
  RUN_TEST(test_ping_tracker);
  RUN_TEST(test_ping_tracker_overflow);
  return UNITY_END();
}
