// Controller (core/pad): report HID dell'Xbox 1914, mappatura dei tasti per i menu, collegamento, radio.
#include <string.h>
#include <unity.h>

#include "core/model.h"
#include "core/pad.h"

// Solo cal::pad: in cal c'è anche la classe Link (core/link.h), qui serve pad::Link.
using namespace cal::pad;

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
  TEST_ASSERT_TRUE(parseXboxReport(r, 16, s));
  TEST_ASSERT_EQUAL_UINT16(0, s.buttons);
  TEST_ASSERT_EQUAL_INT8(0, s.lx);
  TEST_ASSERT_EQUAL_INT8(0, s.ry);
  TEST_ASSERT_EQUAL_UINT16(0, s.lt);
}

static void test_parse_short_report_rejected() {
  uint8_t r[16];
  idle(r);
  State s;
  memset(&s, 0, sizeof s);
  s.buttons = B;
  TEST_ASSERT_FALSE(parseXboxReport(r, 15, s));
  TEST_ASSERT_EQUAL_UINT16(B, s.buttons);  // invariato
  TEST_ASSERT_FALSE(parseXboxReport(nullptr, 16, s));
  uint8_t longer[20];
  memset(longer, 0, sizeof longer);
  idle(longer);
  longer[13] = 0x01;
  TEST_ASSERT_TRUE(parseXboxReport(longer, 20, s));  // byte in più: si ignorano
  TEST_ASSERT_EQUAL_UINT16(A, s.buttons);
}

static void test_parse_buttons() {
  uint8_t r[16];
  State s;
  idle(r);
  r[13] = 0x01 | 0x02 | 0x08 | 0x10 | 0x40 | 0x80;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_UINT16(A | B | X | Y | LB | RB, s.buttons);
  idle(r);
  r[14] = 0x04 | 0x08 | 0x10 | 0x20 | 0x40;
  r[15] = 0x01;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_UINT16(View | Menu | Guide | LS | RS | Share, s.buttons);
  idle(r);
  r[13] = 0x04 | 0x20;  // bit non usati
  r[14] = 0x03 | 0x80;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_UINT16(0, s.buttons);
}

static void test_hat_to_bits() {
  TEST_ASSERT_EQUAL_UINT16(0, hatToButtons(0));
  TEST_ASSERT_EQUAL_UINT16(Up, hatToButtons(1));
  TEST_ASSERT_EQUAL_UINT16(Up | Right, hatToButtons(2));
  TEST_ASSERT_EQUAL_UINT16(Right, hatToButtons(3));
  TEST_ASSERT_EQUAL_UINT16(Down | Right, hatToButtons(4));
  TEST_ASSERT_EQUAL_UINT16(Down, hatToButtons(5));
  TEST_ASSERT_EQUAL_UINT16(Down | Left, hatToButtons(6));
  TEST_ASSERT_EQUAL_UINT16(Left, hatToButtons(7));
  TEST_ASSERT_EQUAL_UINT16(Up | Left, hatToButtons(8));
  TEST_ASSERT_EQUAL_UINT16(0, hatToButtons(9));  // fuori scala: niente
  uint8_t r[16];
  idle(r);
  r[12] = 5;
  r[13] = 0x01;
  State s;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_UINT16(Down | A, s.buttons);
}

static void test_parse_axes() {
  uint8_t r[16];
  idle(r);
  r[0] = 0x00; r[1] = 0x00;    // LX tutto a sinistra
  r[2] = 0xff; r[3] = 0xff;    // LY tutto giù
  r[8] = 0xff; r[9] = 0x03;    // LT 1023
  r[10] = 0x00; r[11] = 0x02;  // RT 512
  State s;
  parseXboxReport(r, 16, s);
  TEST_ASSERT_EQUAL_INT8(-100, s.lx);
  TEST_ASSERT_EQUAL_INT8(100, s.ly);
  TEST_ASSERT_EQUAL_INT8(0, s.rx);
  TEST_ASSERT_EQUAL_UINT16(1023, s.lt);
  TEST_ASSERT_EQUAL_UINT16(512, s.rt);
  TEST_ASSERT_EQUAL_INT8(0, stickPercent(32768));
  TEST_ASSERT_EQUAL_INT8(-100, stickPercent(0));
  TEST_ASSERT_EQUAL_INT8(100, stickPercent(65535));
}

static void test_same_state_and_link_keys() {
  State a, b;
  memset(&a, 0, sizeof a);
  b = a;
  TEST_ASSERT_TRUE(sameState(a, b));
  b.lx = 1;
  TEST_ASSERT_FALSE(sameState(a, b));
  TEST_ASSERT_EQUAL_STRING("off", linkKey(Link::Off));
  TEST_ASSERT_EQUAL_STRING("pairing", linkKey(Link::Pairing));
  TEST_ASSERT_EQUAL_STRING("connecting", linkKey(Link::Connecting));
  TEST_ASSERT_EQUAL_STRING("ready", linkKey(Link::Ready));
}

static State st(uint16_t buttons) {
  State s;
  memset(&s, 0, sizeof s);
  s.buttons = buttons;
  return s;
}

struct Ev {
  MenuEvent e[8];
  int n;
};
static Ev up(MenuMapper &m, uint16_t b, uint32_t t) { Ev v; v.n = m.update(st(b), t, v.e, 8); return v; }
static Ev tk(MenuMapper &m, uint32_t t) { Ev v; v.n = m.tick(t, v.e, 8); return v; }

static void test_same_hold_as_prg() {
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(cal::kHoldMs), MenuMapper::kHoldMs);      // core/model: 3000
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(cal::kHoldShowMs), MenuMapper::kTapMs);   // 700
}

static void test_dpad_b_menu_on_press() {
  MenuMapper m;
  Ev v = up(m, Up | Right, 100);  // diagonale: conta la componente verticale
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::Up, v.e[0]);
  TEST_ASSERT_EQUAL(0, up(m, Up | Right, 200).n);  // tenuta: niente ripetizioni
  TEST_ASSERT_EQUAL(0, up(m, 0, 300).n);           // rilascio: niente
  v = up(m, Down, 400);
  TEST_ASSERT_EQUAL(MenuEvent::Down, v.e[0]);
  v = up(m, B, 500);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::Back, v.e[0]);
  v = up(m, Menu, 600);
  TEST_ASSERT_EQUAL(MenuEvent::Menu, v.e[0]);
  TEST_ASSERT_EQUAL(0, up(m, View | X | Y | LB, 700).n);  // tasti senza ruolo nei menu
}

static void test_a_tap_selects_on_release() {
  MenuMapper m;
  Ev v = up(m, A, 1000);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldDown, v.e[0]);
  TEST_ASSERT_TRUE(m.holding());
  v = up(m, 0, 1120);
  TEST_ASSERT_EQUAL(2, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[0]);
  TEST_ASSERT_EQUAL(MenuEvent::Select, v.e[1]);
  TEST_ASSERT_FALSE(m.holding());
}

// Review Focus: i due report di una pressione breve arrivano in coda durante un refresh e si leggono insieme.
// Copre solo l'istante d'arrivo (i due report letti insieme): la prova vera è il test dal vivo del Task 14.
static void test_tap_during_refresh_is_select() {
  MenuMapper m;
  Ev v = up(m, A, 5000);   // istante d'arrivo del report, non di lettura
  v = up(m, 0, 5090);
  TEST_ASSERT_EQUAL(MenuEvent::Select, v.e[v.n - 1]);
  TEST_ASSERT_EQUAL(0, tk(m, 9000).n);  // niente tenuto fantasma dopo
}

static void test_a_released_between_07_and_3s() {
  MenuMapper m;
  up(m, A, 0);
  TEST_ASSERT_EQUAL(0, tk(m, 2999).n);
  Ev v = up(m, 0, 1500 + 1000);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[0]);  // né scegli né conferma
}

static void test_a_hold_fires_once_at_3s() {
  MenuMapper m;
  up(m, A, 10000);
  TEST_ASSERT_EQUAL(0, tk(m, 12999).n);
  Ev v = tk(m, 13000);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldFire, v.e[0]);
  TEST_ASSERT_EQUAL(0, tk(m, 14000).n);  // una volta sola
  v = up(m, 0, 15000);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[0]);  // rilascio dopo lo scatto: niente scegli
}

static void test_late_report_fires_before_release() {
  MenuMapper m;
  up(m, A, 0);
  Ev v = up(m, 0, 3200);  // nessun tick nel mezzo (refresh lungo): prima il tempo, poi il rilascio
  TEST_ASSERT_EQUAL(2, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldFire, v.e[0]);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[1]);
}

// Review Focus: controller scollegato a metà tenuta.
static void test_reset_mid_hold() {
  MenuMapper m;
  up(m, A, 0);
  Ev v;
  v.n = m.reset(v.e, 8);
  TEST_ASSERT_EQUAL(1, v.n);
  TEST_ASSERT_EQUAL(MenuEvent::HoldUp, v.e[0]);
  TEST_ASSERT_EQUAL(0, tk(m, 5000).n);  // niente scatto dopo
  v.n = m.reset(v.e, 8);
  TEST_ASSERT_EQUAL(0, v.n);            // a riposo: niente
  v = up(m, A, 6000);                   // dopo il ricollegamento A riparte da capo
  TEST_ASSERT_EQUAL(MenuEvent::HoldDown, v.e[0]);
}

static void test_cap_respected() {
  MenuMapper m;
  MenuEvent one[1];
  TEST_ASSERT_EQUAL(1, m.update(st(Up | B | A), 0, one, 1));
  TEST_ASSERT_EQUAL(0, m.update(st(0), 100, one, 0));
  TEST_ASSERT_EQUAL_STRING("A tenuto 3 s", menuEventName(MenuEvent::HoldFire));
  TEST_ASSERT_EQUAL_STRING("giù", menuEventName(MenuEvent::Down));
}

static void test_link_enable_and_pair() {
  LinkMachine m;
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.enable(true, false);
  TEST_ASSERT_EQUAL(Link::Off, m.link());         // niente da cercare
  m.enable(true, true);
  TEST_ASSERT_EQUAL(Link::Connecting, m.link());  // si cerca il controller associato
  m.enable(false, true);
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.pair(0);
  TEST_ASSERT_EQUAL(Link::Off, m.link());         // radio spenta: niente associazione
  m.enable(true, false);
  m.pair(1000);
  TEST_ASSERT_EQUAL(Link::Pairing, m.link());
  TEST_ASSERT_TRUE(m.acceptNew());
  m.tick(1000 + LinkMachine::kPairMs - 1);
  TEST_ASSERT_EQUAL(Link::Pairing, m.link());
  m.tick(1000 + LinkMachine::kPairMs);
  TEST_ASSERT_EQUAL(Link::Off, m.link());         // scaduta senza controller associato
  TEST_ASSERT_FALSE(m.acceptNew());
}

static void test_link_short_report_map_reconnects() {
  LinkMachine m;
  m.enable(true, false);
  m.pair(0);
  m.lost(500, false);  // dal vivo: primo collegamento con il report map troncato, scollegato apposta
  TEST_ASSERT_EQUAL(Link::Pairing, m.link());  // si continua a cercare (ora è abbinato: si ricollega)
  m.connected(true);
  TEST_ASSERT_EQUAL(Link::Ready, m.link());
  TEST_ASSERT_TRUE(m.bonded());
  m.lost(9000, false);  // controller spento
  TEST_ASSERT_EQUAL(Link::Connecting, m.link());
  m.connected(true);
  TEST_ASSERT_EQUAL(Link::Ready, m.link());
}

static void test_link_bond_invalid_reopens_pairing() {
  LinkMachine m;
  m.enable(true, true);
  m.lost(100, true);  // cifratura rifiutata con l'abbinamento salvato
  TEST_ASSERT_FALSE(m.bonded());
  TEST_ASSERT_EQUAL(Link::Pairing, m.link());
  m.tick(100 + LinkMachine::kPairMs);
  TEST_ASSERT_EQUAL(Link::Off, m.link());
}

static void test_link_forget_and_disable() {
  LinkMachine m;
  m.enable(true, true);
  m.connected(true);
  m.forget();
  TEST_ASSERT_FALSE(m.bonded());
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.enable(true, true);
  m.connected(true);
  m.enable(false, true);
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.lost(10, false);
  TEST_ASSERT_EQUAL(Link::Off, m.link());
  m.connected(true);
  TEST_ASSERT_EQUAL(Link::Off, m.link());  // radio spenta: un collegamento tardivo non conta
}

static RadioIn rin(Link l) {
  RadioIn in;
  memset(&in, 0, sizeof in);
  in.setting = true;
  in.wifiPsOk = true;
  in.link = l;
  return in;
}

static void test_radio_plan() {
  RadioIn in = rin(Link::Connecting);
  in.setting = false;
  TEST_ASSERT_FALSE(radioPlan(in).radio);
  in = rin(Link::Connecting);
  in.asleep = true;
  TEST_ASSERT_FALSE(radioPlan(in).radio);
  in = rin(Link::Connecting);
  in.wifiPsOk = false;  // il Wi-Fi è ancora in «nessun risparmio»: prima il risparmio, poi la radio
  TEST_ASSERT_FALSE(radioPlan(in).radio);
  in = rin(Link::Ready);
  TEST_ASSERT_TRUE(radioPlan(in).radio);
  TEST_ASSERT_FALSE(radioPlan(in).scan);
  in = rin(Link::Off);  // acceso ma senza controller associato
  TEST_ASSERT_TRUE(radioPlan(in).radio);
  TEST_ASSERT_FALSE(radioPlan(in).scan);
  // padOn senza abbinamento e senza associazione: il chiamante passa setting = false, radio spenta
  in = rin(Link::Off);
  in.setting = false;
  TEST_ASSERT_FALSE(radioPlan(in).radio);
  TEST_ASSERT_FALSE(radioPlan(in).scan);
  in = rin(Link::Pairing);
  in.idle = true;
  TEST_ASSERT_TRUE(radioPlan(in).scan);
  in = rin(Link::Connecting);  // Attivo
  in.now = 999999;
  TEST_ASSERT_TRUE(radioPlan(in).scan);
  in.idle = true;
  in.usb = true;
  TEST_ASSERT_TRUE(radioPlan(in).scan);
  in.usb = false;
  in.searchFrom = 0;
  in.now = kSearchMs - 1;
  TEST_ASSERT_TRUE(radioPlan(in).scan);    // primi 2 minuti
  in.now = kSearchMs;
  TEST_ASSERT_TRUE(radioPlan(in).scan);    // 120000 % 30000 = 0: finestra di 3 s
  in.now = kSearchMs + kIdleScanMs;
  TEST_ASSERT_FALSE(radioPlan(in).scan);
  in.now = kSearchMs + kIdleScanEveryMs + 500;
  TEST_ASSERT_TRUE(radioPlan(in).scan);
}

static void test_link_pair_kind() {
  LinkMachine m;
  m.enable(true, true);
  m.pair(1000, Kind::Phone);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Pairing), static_cast<int>(m.link()));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Phone), static_cast<int>(m.kind()));
  m.connected(true, Kind::Phone);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Ready), static_cast<int>(m.link()));
  m.lost(5000, false);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
  m.connected(true, Kind::Xbox);
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Xbox), static_cast<int>(m.kind()));
}

static void test_link_cancel_pairing() {
  LinkMachine m;
  m.enable(true, false);
  m.pair(0, Kind::Phone);
  m.cancel();
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Off), static_cast<int>(m.link()));
  m.setBonded(true);
  m.pair(0, Kind::Phone);
  m.cancel();
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
  m.cancel();  // fuori dall'associazione: niente
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
}

// «Annulla» tardivo con il telefono già collegato: non tocca il collegamento.
static void test_link_cancel_from_ready() {
  LinkMachine m;
  m.enable(true, true);
  m.connected(true, Kind::Phone);
  m.cancel();
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Ready), static_cast<int>(m.link()));
}

// Abbinamento del telefono non valido, Xbox ancora associato: scaduta la finestra si cerca ancora l'Xbox.
static void test_link_set_bonded_after_invalid() {
  LinkMachine m;
  m.enable(true, true);
  m.connected(true, Kind::Phone);
  m.lost(1000, true);  // riapre l'associazione del telefono
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Pairing), static_cast<int>(m.link()));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Phone), static_cast<int>(m.kind()));
  m.setBonded(true);
  m.tick(1000 + LinkMachine::kPairMs);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
}

// Revisione del Task 10, I1: un abbinamento perso (anche dell'Xbox) non apre mai una finestra da sé. hw/blepad
// chiama sempre lost(…, false): dopo una finestra del telefono scaduta (kind() ancora Phone) si cerca e basta.
static void test_link_lost_never_opens_window() {
  LinkMachine m;
  m.enable(true, true);
  m.pair(0, Kind::Phone);
  m.tick(LinkMachine::kPairMs);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
  m.lost(200000, false);
  m.setBonded(false);  // l'Xbox tolto dal libro
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Connecting), static_cast<int>(m.link()));
  m.lost(200100, false);
  TEST_ASSERT_EQUAL(static_cast<int>(Link::Off), static_cast<int>(m.link()));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_idle);
  RUN_TEST(test_parse_short_report_rejected);
  RUN_TEST(test_parse_buttons);
  RUN_TEST(test_hat_to_bits);
  RUN_TEST(test_parse_axes);
  RUN_TEST(test_same_state_and_link_keys);
  RUN_TEST(test_same_hold_as_prg);
  RUN_TEST(test_dpad_b_menu_on_press);
  RUN_TEST(test_a_tap_selects_on_release);
  RUN_TEST(test_tap_during_refresh_is_select);
  RUN_TEST(test_a_released_between_07_and_3s);
  RUN_TEST(test_a_hold_fires_once_at_3s);
  RUN_TEST(test_late_report_fires_before_release);
  RUN_TEST(test_reset_mid_hold);
  RUN_TEST(test_cap_respected);
  RUN_TEST(test_link_enable_and_pair);
  RUN_TEST(test_link_short_report_map_reconnects);
  RUN_TEST(test_link_bond_invalid_reopens_pairing);
  RUN_TEST(test_link_forget_and_disable);
  RUN_TEST(test_radio_plan);
  RUN_TEST(test_link_pair_kind);
  RUN_TEST(test_link_cancel_pairing);
  RUN_TEST(test_link_cancel_from_ready);
  RUN_TEST(test_link_set_bonded_after_invalid);
  RUN_TEST(test_link_lost_never_opens_window);
  return UNITY_END();
}
