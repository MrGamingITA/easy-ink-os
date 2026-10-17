// 0.9.0: Giornata di lavoro (core/workday, Ui): ore e testi, regola A, fine con «GIORNATA FINITA», mezzanotte, timer
// di risveglio del sonno profondo, NVS e status.workday contro protocol/examples. Ora locale finta: 2026-10-11.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>

#include "core/menu.h"
#include "core/ota.h"
#include "core/power.h"
#include "core/strbuf.h"
#include "core/ui.h"
#include "core/workday.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

std::string readFile(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  std::string s = ss.str();
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  return s;
}

bool near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }
int64_t at(int h, int mi, int s = 0, int ms = 0) { return wallFromCivil(2026, 10, 11, h, mi, s).localMs + ms; }

// Ui con il tempo monotono che avanza insieme all'ora locale (stessa scala, come nel simulatore).
struct Bench {
  Ui ui;
  Ms now;
  int64_t local;
  explicit Bench(int64_t start) : ui(kHeltec213), now(1000000), local(start - 2700) {  // dopo l'avvio: start
    ui.begin(now, wc());
    go(2700);
  }
  WallClock wc() const { WallClock w; w.localMs = local; return w; }
  void go(Ms ms) {  // tick ogni 100 ms, come il simulatore
    const Ms end = now + ms;
    while (now < end) {
      const Ms d = end - now < 100 ? end - now : 100;
      now += d;
      local += d;
      ui.tick(now, wc());
    }
  }
  void jump(int64_t toLocal) {  // salto senza tick (scheda in sonno profondo o spenta), poi un tick
    now += toLocal - local;
    local = toLocal;
    ui.tick(now, wc());
  }
  Model &m() { return ui.model(); }
  std::string line() { char t[24]; workLine(m(), wc(), t, sizeof t); return t; }
  std::string err;
  bool start() { char e[96]; ui.workdayRemote(true, now, wc(), e, sizeof e); err = e; return !e[0]; }
};

}  // namespace

static void test_parse_and_format_hours() {
  int v = -1;
  TEST_ASSERT_TRUE(parseHm("09:00", v)); TEST_ASSERT_EQUAL(540, v);
  TEST_ASSERT_TRUE(parseHm("00:00", v)); TEST_ASSERT_EQUAL(0, v);
  TEST_ASSERT_TRUE(parseHm("23:59", v)); TEST_ASSERT_EQUAL(1439, v);
  for (const char *b : {"24:00", "9:00", "09:60", "0900", "09:0", "09-00", "", "ab:cd"}) TEST_ASSERT_FALSE_MESSAGE(parseHm(b, v), b);
  TEST_ASSERT_FALSE(parseHm(nullptr, v));
  char t[8];
  minHm(540, t, sizeof t); TEST_ASSERT_EQUAL_STRING("09:00", t);
  minHm(1439, t, sizeof t); TEST_ASSERT_EQUAL_STRING("23:59", t);
  minHm(5, t, sizeof t); TEST_ASSERT_EQUAL_STRING("00:05", t);
  TEST_ASSERT_TRUE(workTimesOk(540, 1080));
  TEST_ASSERT_FALSE(workTimesOk(1080, 1080));
  TEST_ASSERT_FALSE(workTimesOk(600, 540));
  TEST_ASSERT_FALSE(workTimesOk(0, 1440));
}

// Regola A: la fine è fissa alle 18:00; iniziando alle 9:40 mancano 8 h 20, anche un attimo dopo il ridisegno.
static void test_rule_a_fixed_end() {
  static Bench b(at(9, 40, 0, 200));
  TEST_ASSERT_TRUE(b.start());
  TEST_ASSERT_TRUE(b.m().work.on);
  TEST_ASSERT_EQUAL_INT64(at(18, 0), b.m().work.endLocal);
  TEST_ASSERT_EQUAL(500, workLeftMin(b.m(), b.wc()));
  TEST_ASSERT_EQUAL_STRING("8 h 20", b.line().c_str());
  char t[24];
  workLeftText(b.m(), b.wc(), true, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("8 H 20", t);
  TEST_ASSERT_EQUAL_STRING("Giornata fino alle 18:00", b.m().toast.msg);
  TEST_ASSERT_EQUAL_STRING("Dal PC: Giornata di lavoro fino alle 18:00: mancano 8 h 20", b.ui.lastAction());
  // Iniziando alle 13:00 la fine resta alle 18:00 (giornata più corta, non 9 ore da adesso).
  static Bench c(at(13, 0));
  TEST_ASSERT_TRUE(c.start());
  TEST_ASSERT_EQUAL_INT64(at(18, 0), c.m().work.endLocal);
  TEST_ASSERT_EQUAL_STRING("5 h 00", c.line().c_str());
  // Sotto l'ora: minuti. Per eccesso: 25 min e 0,2 s fanno 26, 24 min e 59 s fanno 25.
  c.jump(at(17, 35));
  TEST_ASSERT_EQUAL_STRING("25 min", c.line().c_str());
  workLeftText(c.m(), c.wc(), true, t, sizeof t);
  TEST_ASSERT_EQUAL_STRING("25 MIN", t);
  c.jump(at(17, 35, 1));
  TEST_ASSERT_EQUAL_STRING("25 min", c.line().c_str());
  c.jump(at(17, 59, 59, 500));
  TEST_ASSERT_EQUAL_STRING("1 min", c.line().c_str());
  // Già attiva: niente cambia, nessun errore.
  TEST_ASSERT_TRUE(c.start());
  TEST_ASSERT_EQUAL_STRING("Giornata già attiva", c.m().toast.msg);
  TEST_ASSERT_EQUAL_INT64(at(18, 0), c.m().work.endLocal);
}

static void test_start_after_end_refused() {
  static Bench b(at(18, 0));
  TEST_ASSERT_FALSE(b.start());
  TEST_ASSERT_EQUAL_STRING(kWorkPastError, b.err.c_str());
  TEST_ASSERT_EQUAL_STRING("orario di fine già passato", b.err.c_str());
  TEST_ASSERT_EQUAL_STRING("Orario di fine già passato", b.m().toast.msg);
  TEST_ASSERT_FALSE(b.m().work.on);
  TEST_ASSERT_EQUAL_STRING("", b.line().c_str());
  // Senza l'ora vera non parte (sulla scheda prima di welcome/SNTP).
  static Bench c(at(10, 0));
  c.m().sys.wallOk = false;
  TEST_ASSERT_FALSE(c.start());
  TEST_ASSERT_EQUAL_STRING(kWorkNoClock, c.err.c_str());
  TEST_ASSERT_FALSE(c.m().work.on);
}

// Alla fine: «GIORNATA FINITA» (toast 5 s), festa di 6 s, riga «finita» per un'ora, poi niente; la voce torna «spenta».
static void test_end_of_day() {
  static Bench b(at(17, 0));
  TEST_ASSERT_TRUE(b.start());
  b.go(3000);
  b.jump(at(17, 59, 59, 900));
  TEST_ASSERT_TRUE(b.m().work.on);
  const Ms before = b.now;
  b.go(200);  // passa le 18:00
  TEST_ASSERT_FALSE(b.m().work.on);
  TEST_ASSERT_EQUAL_STRING("GIORNATA FINITA", b.m().toast.msg);
  TEST_ASSERT_TRUE(b.m().toast.until >= b.now + 4800);
  TEST_ASSERT_TRUE(workParty(b.m(), b.now));
  TEST_ASSERT_TRUE(b.m().burstUntil >= before + kWorkPartyMs);  // festa: animazione (non silenziato)
  TEST_ASSERT_EQUAL_STRING("finita", b.line().c_str());
  TEST_ASSERT_EQUAL_INT64(at(19, 0), b.m().work.doneUntilLocal);
  TEST_ASSERT_EQUAL_STRING("Fine della giornata di lavoro", b.ui.lastAction());
  // Home: la mascotte della sessione mostrata esulta se non aspetta qualcuno (lo decide homePose, provato in test_ui).
  b.go(6100);
  TEST_ASSERT_FALSE(workParty(b.m(), b.now));
  static MenuList L;
  menuList(MenuId::Tools, kHeltec213, b.m(), b.now, b.wc(), L);
  TEST_ASSERT_EQUAL_STRING("Giornata di lavoro", L.items[5].label);
  TEST_ASSERT_EQUAL_STRING("spenta", L.items[5].val);
  b.jump(at(19, 0, 0, 50));
  TEST_ASSERT_EQUAL_STRING("", b.line().c_str());
  TEST_ASSERT_EQUAL_INT64(0, b.m().work.doneUntilLocal);
  // Silenziato: niente animazione della festa (il toast sì).
  static Bench q(at(17, 59));
  TEST_ASSERT_TRUE(q.start());
  q.ui.act(Action::Mute, q.now, q.wc());
  q.go(61000);
  TEST_ASSERT_FALSE(q.m().work.on);
  TEST_ASSERT_EQUAL_STRING("GIORNATA FINITA", q.m().toast.msg);
  TEST_ASSERT_TRUE(workParty(q.m(), q.now));          // la mascotte esulta lo stesso (posa, non animazione)
  TEST_ASSERT_TRUE(q.m().burstUntil < q.m().work.party);  // solo le raffiche di sempre, non la festa
}

// A mezzanotte si azzera comunque; «GIORNATA FINITA» non va mai oltre la mezzanotte.
static void test_midnight() {
  static Bench b(at(23, 0));
  b.m().set.workEnd = 23 * 60 + 30;
  TEST_ASSERT_TRUE(b.start());
  b.jump(at(23, 30, 0, 100));
  TEST_ASSERT_FALSE(b.m().work.on);
  TEST_ASSERT_EQUAL_INT64(wallFromCivil(2026, 10, 12, 0, 0, 0).localMs, b.m().work.doneUntilLocal);  // non 00:30
  b.jump(wallFromCivil(2026, 10, 12, 0, 0, 1).localMs);
  TEST_ASSERT_EQUAL_STRING("", b.line().c_str());
  // Attiva e scheda ferma oltre la mezzanotte (nessun tick alla fine): al primo tick del giorno dopo si spegne in
  // silenzio, senza toast né «GIORNATA FINITA».
  static Bench c(at(10, 0));
  TEST_ASSERT_TRUE(c.start());
  c.go(3000);
  c.jump(wallFromCivil(2026, 10, 12, 7, 30, 0).localMs);
  TEST_ASSERT_FALSE(c.m().work.on);
  TEST_ASSERT_EQUAL_INT64(0, c.m().work.doneUntilLocal);
  TEST_ASSERT_EQUAL_STRING("", c.m().toast.msg[0] && c.m().toast.until >= c.now ? c.m().toast.msg : "");
  TEST_ASSERT_EQUAL(WorkStep::None, workStep(c.m(), c.wc()));
  // Fine passata da più di un'ora nello stesso giorno (scheda spenta alle 18): si spegne senza festa.
  static Bench d(at(10, 0));
  TEST_ASSERT_TRUE(d.start());
  d.go(3000);
  d.jump(at(19, 30));
  TEST_ASSERT_FALSE(d.m().work.on);
  TEST_ASSERT_FALSE(workParty(d.m(), d.now));
  TEST_ASSERT_EQUAL_STRING("", d.line().c_str());
  // Fine passata da poco (riavvio alle 18:20): «GIORNATA FINITA» fino alle 19.
  static Bench e(at(10, 0));
  TEST_ASSERT_TRUE(e.start());
  e.go(3000);
  e.jump(at(18, 20));
  TEST_ASSERT_EQUAL_STRING("finita", e.line().c_str());
  TEST_ASSERT_EQUAL_STRING("GIORNATA FINITA", e.m().toast.msg);
}

// Sonno profondo nel Riposo: il timer diventa min(15 min, tempo alla fine).
static void test_sleep_timer() {
  static Bench b(at(17, 50));
  TEST_ASSERT_EQUAL_INT64(kTimerWakeMs, workSleepMs(b.m(), b.wc(), kTimerWakeMs));  // spenta
  TEST_ASSERT_TRUE(b.start());
  TEST_ASSERT_EQUAL_INT64(10 * 60000, workSleepMs(b.m(), b.wc(), kTimerWakeMs));    // alle 18:00 esatte
  b.jump(at(17, 59, 59, 700));
  TEST_ASSERT_EQUAL_INT64(1000, workSleepMs(b.m(), b.wc(), kTimerWakeMs));          // almeno 1 s
  static Bench c(at(9, 0));
  TEST_ASSERT_TRUE(c.start());
  TEST_ASSERT_EQUAL_INT64(kTimerWakeMs, workSleepMs(c.m(), c.wc(), kTimerWakeMs));  // 9 ore: 15 min
  c.jump(at(17, 47, 30));
  TEST_ASSERT_EQUAL_INT64(12 * 60000 + 30000, workSleepMs(c.m(), c.wc(), kTimerWakeMs));
  c.m().sys.wallOk = false;
  TEST_ASSERT_EQUAL_INT64(kTimerWakeMs, workSleepMs(c.m(), c.wc(), kTimerWakeMs));
  // Fine in ms epoch per status.workday: epoch adesso + (fine − ora locale).
  c.m().sys.wallOk = true;
  TEST_ASSERT_EQUAL_INT64(1791820800000LL, workEndEpoch(c.m(), c.wc(), 1791820800000LL - (at(18, 0) - c.local)));
  TEST_ASSERT_TRUE(workStop(c.m()));
  TEST_ASSERT_EQUAL_INT64(-1, workEndEpoch(c.m(), c.wc(), 1791820800000LL));
}

// Barra della giornata dall'orario d'inizio alla fine; il nuovo inizio da remoto vale subito, la fine in corso no.
static void test_bar_and_remote_hours() {
  static Bench b(at(10, 30));
  TEST_ASSERT_TRUE(b.start());
  TEST_ASSERT_TRUE(near(90.0 / 540.0, workFrac(b.m(), b.wc())));
  SettingsPatch p;
  memset(&p, 0, sizeof p);
  p.has = (1u << static_cast<int>(RemoteKey::WorkStart)) | (1u << static_cast<int>(RemoteKey::WorkEnd));
  p.v.workStart = 10 * 60;
  p.v.workEnd = 17 * 60;
  b.ui.applyRemote(p, b.now, b.wc());
  TEST_ASSERT_EQUAL_INT64(at(18, 0), b.m().work.endLocal);  // regola A: la giornata in corso tiene la sua fine
  TEST_ASSERT_TRUE(near(30.0 / 480.0, workFrac(b.m(), b.wc())));
  // Inizio dopo l'avvio della fine (sim «ultimi minuti»): la barra parte dall'attivazione.
  b.m().set.workStart = 23 * 60;
  b.m().work.fromLocal = at(10, 0);
  TEST_ASSERT_TRUE(near(30.0 / 480.0, workFrac(b.m(), b.wc())));
  // Il prossimo avvio usa la fine nuova.
  b.m().set.workStart = 10 * 60;
  TEST_ASSERT_TRUE(workStop(b.m()));
  TEST_ASSERT_TRUE(b.start());
  TEST_ASSERT_EQUAL_INT64(at(17, 0), b.m().work.endLocal);
}

static void test_nvs_roundtrip() {
  static Bench b(at(9, 40));
  TEST_ASSERT_TRUE(b.start());
  b.m().set.workStart = 8 * 60 + 15;
  uint8_t blob[kWorkBlobLen];
  TEST_ASSERT_EQUAL(kWorkBlobLen, packWork(storedWork(b.m()), blob, sizeof blob));
  StoredWork w;
  TEST_ASSERT_TRUE(unpackWork(blob, sizeof blob, w));
  TEST_ASSERT_EQUAL(495, w.start);
  TEST_ASSERT_EQUAL(1080, w.end);
  TEST_ASSERT_TRUE(w.on);
  TEST_ASSERT_EQUAL_INT64(at(18, 0), w.endLocal);
  TEST_ASSERT_EQUAL_INT64(at(9, 40), w.fromLocal);
  // Dopo un riavvio (o il sonno profondo) torna uguale; il conto riparte dall'ora vera.
  static Bench c(at(11, 0));
  restoreWork(c.m(), w);
  c.go(100);
  TEST_ASSERT_TRUE(c.m().work.on);
  TEST_ASSERT_EQUAL_STRING("7 h 00", c.line().c_str());
  // Blocchi rovinati: somma, versione, lunghezza, orari impossibili.
  uint8_t bad[kWorkBlobLen];
  memcpy(bad, blob, sizeof bad);
  bad[7] ^= 1;
  TEST_ASSERT_FALSE(unpackWork(bad, sizeof bad, w));
  memcpy(bad, blob, sizeof bad);
  bad[0] = 2;
  TEST_ASSERT_FALSE(unpackWork(bad, sizeof bad, w));
  TEST_ASSERT_FALSE(unpackWork(blob, sizeof blob - 1, w));
  StoredWork x = storedWork(c.m());
  x.start = 1100;  // dopo la fine
  packWork(x, bad, sizeof bad);
  TEST_ASSERT_FALSE(unpackWork(bad, sizeof bad, w));
}

// status.workday (PROTOCOLLO.md, «Giornata di lavoro»): esempi del protocollo e invio subito a ogni cambiamento.
static void test_status_workday() {
  char out[160];
  ota::PowerStatus s;
  memset(&s, 0, sizeof s);
  s.batt = 78; s.usb = 0; s.cal = false; s.hasWork = true; s.workOn = true; s.workEnd = 1791820800000LL;
  ota::encodePowerStatus(s, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(readFile("../protocol/examples/status-giornata.json").c_str(), out);
  s.workOn = false;
  s.usb = 1;
  ota::encodePowerStatus(s, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"status\",\"v\":1,\"batt\":78,\"usb\":true,\"cal\":false,\"workday\":{\"on\":false,\"end\":null}}", out);
  s.hasWork = false;  // mai null: assente
  ota::encodePowerStatus(s, out, sizeof out);
  TEST_ASSERT_NULL(strstr(out, "workday"));
  ota::PowerReporter r;
  ota::PowerStatus a;
  memset(&a, 0, sizeof a);
  a.batt = 78; a.hasWork = true;
  TEST_ASSERT_TRUE(r.due(a, 0));
  a.batt = 77;
  TEST_ASSERT_FALSE(r.due(a, 100));   // batteria: aspetta 10 s
  a.workOn = true; a.workEnd = 5;
  TEST_ASSERT_TRUE(r.due(a, 200));    // giornata avviata: subito
  a.workEnd = 6;
  TEST_ASSERT_TRUE(r.due(a, 300));    // fine diversa: subito
  a.workOn = false;
  TEST_ASSERT_TRUE(r.due(a, 400));    // fermata o finita: subito
}

// Menu › Strumenti › Giornata di lavoro: Inizia ora (2×) e Ferma (2×), Inizio e Fine in sola lettura.
static void test_menu_flow() {
  static Bench b(at(10, 30));
  Ui &u = b.ui;
  u.act(Action::Menu, b.now, b.wc());
  u.act(Action::Next, b.now, b.wc());
  u.act(Action::Ok, b.now, b.wc());  // Strumenti
  for (int k = 0; k < 5; k++) u.act(Action::Next, b.now, b.wc());
  u.act(Action::Ok, b.now, b.wc());  // Giornata di lavoro
  TEST_ASSERT_EQUAL(MenuId::Work, top(b.m()).menu);
  const MenuList &L = u.items(b.now, b.wc());
  TEST_ASSERT_EQUAL(4, L.n);
  TEST_ASSERT_FALSE(L.truncated);
  TEST_ASSERT_EQUAL_STRING("Inizia ora", L.items[0].label);
  TEST_ASSERT_EQUAL_STRING("09:00", L.items[2].val);
  TEST_ASSERT_EQUAL_STRING("18:00", L.items[3].val);
  TEST_ASSERT_EQUAL(ItemKind::Info, L.items[2].kind);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, u.act(Action::Ok, b.now, b.wc()));
  TEST_ASSERT_TRUE(b.m().work.on);
  TEST_ASSERT_EQUAL_STRING("attiva", u.items(b.now, b.wc()).items[0].val);
  u.act(Action::Next, b.now, b.wc());
  u.act(Action::Ok, b.now, b.wc());  // Ferma
  TEST_ASSERT_FALSE(b.m().work.on);
  TEST_ASSERT_EQUAL_STRING("Giornata fermata", b.m().toast.msg);
  u.act(Action::Ok, b.now, b.wc());
  TEST_ASSERT_EQUAL_STRING("Giornata non attiva", b.m().toast.msg);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_and_format_hours);
  RUN_TEST(test_rule_a_fixed_end);
  RUN_TEST(test_start_after_end_refused);
  RUN_TEST(test_end_of_day);
  RUN_TEST(test_midnight);
  RUN_TEST(test_sleep_timer);
  RUN_TEST(test_bar_and_remote_hours);
  RUN_TEST(test_nvs_roundtrip);
  RUN_TEST(test_status_workday);
  RUN_TEST(test_menu_flow);
  return UNITY_END();
}
