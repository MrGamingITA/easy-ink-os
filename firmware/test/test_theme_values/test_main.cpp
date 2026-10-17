// Temi: valori nei testi, condizioni, sessione di riferimento, scelta della variante (docs/TEMI.md §1.2, §3, §4).
#include <string.h>
#include <unity.h>

#include "core/model.h"
#include "core/strbuf.h"
#include "core/theme_values.h"

using namespace cal;
using namespace cal::theme;

void setUp() {}
void tearDown() {}

static Model gM;
static ExpandScratch gEx;  // come l'arena del Runtime (ui/theme_draw)
static const Ms kNow = 1791707400000LL;

static Model &fresh(int nSessions) {
  newModel(gM, kNow, WallClock{kNow});
  gM.nSessions = 0;
  for (int k = 0; k < nSessions; k++) {
    Session &s = gM.sessions[gM.nSessions++];
    memset(&s, 0, sizeof s);
    Sb(s.name, sizeof s.name).s("sessione-").i(k);
    s.state = SessionState::Working;
    scopy(s.detail, "Bash: npm run test");
    s.since = kNow - 12 * 60000;
    s.ctx = 42;
    s.total = 7;
    s.done = 3;
  }
  // newModel riempie anche i dati finti del simulatore: qui si parte da zero
  gM.sys.wallOk = true;
  gM.sys.batt = 78;
  gM.sys.usb = false;
  gM.ui.selUntil = 0;
  gM.ui.sel = 0;
  memset(&gM.limits, 0, sizeof gM.limits);
  memset(&gM.stats, 0, sizeof gM.stats);
  gM.pet.known = false;
  gM.acc = false;
  return gM;
}

static Ctx ctx(Screen s = Screen::Home) {
  Ctx c;
  c.m = &gM;
  c.now = kNow;
  c.wc = wallFromCivil(2026, 10, 11, 10, 30, 5);
  c.frame = 0;
  c.screen = s;
  c.ref = refIndex(gM, kNow, s, Ref::Selected);
  return c;
}

static const char *val(uint16_t id, const Session *row = nullptr) {
  static char out[256];
  valueText(id, ctx(), row, out, sizeof out);
  return out;
}

static void test_session_values() {
  fresh(2);
  gM.sessions[1].state = SessionState::Permission;  // la più urgente
  TEST_ASSERT_EQUAL_STRING("sessione-1", val(0x00));   // selezionata = urgente quando non c'è selezione
  TEST_ASSERT_EQUAL_STRING("Chiede un permesso", val(0x01));
  TEST_ASSERT_EQUAL_STRING("Bash: npm run test", val(0x02));
  TEST_ASSERT_EQUAL_STRING("42%", val(0x05));
  TEST_ASSERT_EQUAL_STRING("3/7", val(0x06));
  TEST_ASSERT_EQUAL_STRING("12 min", val(0x07));
  TEST_ASSERT_EQUAL_STRING("", val(0x08));             // nessun agente
  TEST_ASSERT_EQUAL_STRING("", val(0x09));             // nessun test
  gM.sessions[1].test = TestResult::Ko;
  TEST_ASSERT_EQUAL_STRING("\xe2\x9c\x97", val(0x09));
  TEST_ASSERT_EQUAL_STRING("sessione-0", val(0x40, &gM.sessions[0]));
  TEST_ASSERT_EQUAL_STRING("", val(0x40));             // riga senza riga: vuoto
  fresh(0);
  TEST_ASSERT_EQUAL_STRING("", val(0x00));             // senza sessioni: vuoto
}

static void test_general_values() {
  fresh(1);
  TEST_ASSERT_EQUAL_STRING("10:30", val(0x80));
  TEST_ASSERT_EQUAL_STRING("10:30:05", val(0x81));
  TEST_ASSERT_EQUAL_STRING("domenica 11 ottobre", val(0x82));
  TEST_ASSERT_EQUAL_STRING("domenica", val(0x83));
  TEST_ASSERT_EQUAL_STRING("1", val(0x84));
  TEST_ASSERT_EQUAL_STRING("0", val(0x85));
  TEST_ASSERT_EQUAL_STRING("78%", val(0x86));
  TEST_ASSERT_EQUAL_STRING("—", val(0x8F));            // limiti senza dati
  gM.limits.hasH5 = true;
  gM.limits.h5 = 36.5;
  TEST_ASSERT_EQUAL_STRING("37%", val(0x8F));
  gM.stats.linesAdd = 120;
  gM.stats.linesDel = 30;
  gM.stats.cost = 1.2;
  TEST_ASSERT_EQUAL_STRING("+120 \xe2\x88\x92" "30", val(0x8D));
  TEST_ASSERT_EQUAL_STRING("1,20 $", val(0x8E));
  TEST_ASSERT_EQUAL_STRING("", val(0x94));             // senza sensore
  TEST_ASSERT_EQUAL_STRING(kFwVersion, val(0x95));
  TEST_ASSERT_EQUAL_STRING("—", val(0x91));            // gioco: nessun dato
  gM.sys.wallOk = false;
  TEST_ASSERT_EQUAL_STRING("--:--", val(0x80));
  TEST_ASSERT_EQUAL_STRING("--:--:--", val(0x81));
  TEST_ASSERT_EQUAL_STRING("", val(0x82));
}

static void test_refs_and_variants() {
  fresh(3);
  gM.sessions[2].state = SessionState::Input;
  TEST_ASSERT_EQUAL(2, refIndex(gM, kNow, Screen::Home, Ref::Urgent));
  gM.ui.sel = 1;
  gM.ui.selUntil = kNow + 1000;
  TEST_ASSERT_EQUAL(1, refIndex(gM, kNow, Screen::Home, Ref::Selected));
  gM.ui.selUntil = 0;
  TEST_ASSERT_EQUAL(1, refIndex(gM, kNow, Screen::Detail, Ref::Selected));  // nel Dettaglio: quella aperta
  TEST_ASSERT_TRUE(whenTrue(gM, When::Many));
  TEST_ASSERT_FALSE(whenTrue(gM, When::One));
  fresh(0);
  TEST_ASSERT_EQUAL(-1, refIndex(gM, kNow, Screen::Home, Ref::Urgent));
  TEST_ASSERT_TRUE(whenTrue(gM, When::None));
  TEST_ASSERT_TRUE(whenTrue(gM, When::Always));
}

static void test_conditions() {
  fresh(2);
  const Ctx c = ctx();
  const Session *s0 = &gM.sessions[0];
  TEST_ASSERT_FALSE(condTrue(1, 1, c, s0));             // perm: nessuna in permesso
  gM.sessions[1].state = SessionState::Permission;
  TEST_ASSERT_TRUE(condTrue(1, 1, c, s0));
  TEST_ASSERT_TRUE(condTrue(2, 80, c, s0));             // batteria 78 < 80
  TEST_ASSERT_FALSE(condTrue(2, 78, c, s0));
  TEST_ASSERT_TRUE(condTrue(3, 0, c, s0));              // non in carica
  TEST_ASSERT_TRUE(condTrue(5, 1, c, s0) == gM.sys.pc);
  TEST_ASSERT_TRUE(condTrue(8, 2, c, s0));              // almeno 2
  TEST_ASSERT_FALSE(condTrue(9, 1, c, s0));             // al massimo 1
  TEST_ASSERT_TRUE(condTrue(10, 0, c, s0));             // state working
  TEST_ASSERT_FALSE(condTrue(10, 0, c, nullptr));       // senza sessione: falso
  TEST_ASSERT_TRUE(condTrue(11, 0, c, s0));             // fotogramma 0: pari
  Ctx odd = c;
  odd.frame = -3;
  TEST_ASSERT_TRUE(condTrue(11, 1, odd, s0));
  const uint8_t two[] = {3, 0, 8, 2};
  TEST_ASSERT_TRUE(condsTrue(two, 2, c, s0));
  const uint8_t no[] = {3, 0, 8, 3};
  TEST_ASSERT_FALSE(condsTrue(no, 2, c, s0));
}

// Blocco minimo con una tabella delle stringhe: «Ciao » e i pezzi passati a mano.
static void test_expand_and_filters() {
  fresh(1);
  scopy(gM.sessions[0].name, "àbc-ÈFG");
  uint8_t b[64] = {1, 0, 10, 0};
  b[10] = 5;
  memcpy(b + 11, "Ciao ", 5);
  const uint8_t segs[] = {0, 0, 0, 1 | (1 << 2), 0x00, 0x00, 1 | (2 << 2), 0x00, 0x00};
  char out[256];
  expand(b, 16, segs, 3, ctx(), nullptr, false, out, sizeof out, gEx);
  TEST_ASSERT_EQUAL_STRING("Ciao ÀBC-ÈFGàbc-èfg", out);
  expand(b, 16, segs, 1, ctx(), nullptr, true, out, sizeof out, gEx);
  TEST_ASSERT_EQUAL_STRING("CIAO ", out);
}

// Più di 255 byte: troncato su un confine UTF-8, mai fuori dal buffer.
static void test_expand_truncates_utf8() {
  fresh(1);
  char big[kSummaryLen];
  memset(big, 0, sizeof big);
  for (int i = 0; i + 2 < kSummaryLen - 1; i += 2) memcpy(big + i, "\xc3\xa8", 2);  // «è» ripetuta
  scopy(gM.sessions[0].summary, big);
  uint8_t b[16] = {1, 0, 10, 0};
  const uint8_t segs[] = {1, 0x03, 0x00, 1, 0x03, 0x00, 1, 0x03, 0x00};
  char out[256];
  out[255] = 0x55;
  expand(b, 10, segs, 3, ctx(), nullptr, false, out, 255, gEx);
  const size_t n = strlen(out);
  TEST_ASSERT_TRUE(n <= 254);
  TEST_ASSERT_EQUAL(0, int(n % 2));                     // nessuna «è» spezzata
  TEST_ASSERT_EQUAL_HEX8(0x55, uint8_t(out[255]));      // il byte dopo cap non si tocca
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_session_values);
  RUN_TEST(test_general_values);
  RUN_TEST(test_refs_and_variants);
  RUN_TEST(test_conditions);
  RUN_TEST(test_expand_and_filters);
  RUN_TEST(test_expand_truncates_utf8);
  return UNITY_END();
}
