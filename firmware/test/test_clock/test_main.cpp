// 0.10.1: l'ora nel Riposo (core/wake, Ui, ui/screens). Prossimo risveglio dal sonno profondo (minuto, 5 minuti con il
// risparmio, Wi-Fi ogni 15 minuti, fine della giornata di lavoro), ora del muro ricostruita dopo il sonno, «--:--» senza
// l'ora vera, ridisegno a ogni minuto da svegli (Riposo, Home, striscia dei menu; Attivo e Attesa) e ricostruzione
// del Riposo nel risveglio dell'ora: identico, pixel per pixel, al Riposo disegnato dal modello completo.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "core/menu.h"
#include "core/power.h"
#include "core/ui.h"
#include "core/wake.h"
#include "core/workday.h"
#include "gfx/canvas.h"
#include "ui/screens.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

const int64_t kTz = 2 * 3600000LL;  // CEST
// Ora locale e epoch dello stesso istante: 2026-10-12 alle h:mi:s.ms (CEST).
int64_t loc(int h, int mi, int s = 0, int ms = 0) { return wallFromCivil(2026, 10, 12, h, mi, s).localMs + ms; }
int64_t ep(int h, int mi, int s = 0, int ms = 0) { return loc(h, mi, s, ms) - kTz; }

WakeIn restIn(int64_t epochMs, int64_t wifiDue = 0, bool saver = false) {
  WakeIn in;
  memset(&in, 0, sizeof in);
  in.reason = SleepReason::Rest;
  in.wallOk = true;
  in.epochMs = epochMs;
  in.saver = saver;
  in.wifiDueMs = wifiDue;
  return in;
}

// Testi disegnati (ui::setTextHook).
std::vector<std::string> gTexts;
void hook(const char *s, int, int, int, int, bool, Align, void *) { gTexts.push_back(s); }
bool drew(const char *s) {
  for (auto &t : gTexts)
    if (t == s) return true;
  return false;
}

typedef Canvas<250, 122> Cv;
const int kImg = Cv::kBytes;
Cv cvA, cvB, cvC;

// Ui con il tempo monotono che avanza insieme all'ora locale.
struct Bench {
  Ui ui;
  Ms now;
  int64_t local;
  explicit Bench(int64_t start) : ui(kHeltec213), now(1000000), local(start - 2700) {
    ui.begin(now, wc());
    run(2700);  // fine dell'avvio
  }
  WallClock wc() const { WallClock w; w.localMs = local; return w; }
  Model &m() { return ui.model(); }
  // Ridisegni chiesti dal tick (ogni 100 ms) in ms millisecondi.
  int run(Ms ms) {
    int n = 0;
    const Ms end = now + ms;
    while (now < end) {
      now += 100;
      local += 100;
      if (ui.tick(now, wc()) != RefreshKind::None) n++;
    }
    return n;
  }
  void draw(Cv &cv, int f = 0) { ui::compose(cv, kHeltec213, m(), f, now, wc()); }
};

}  // namespace

// ---------- prossimo risveglio ----------

static void test_wake_next_minute() {
  const WakePlan p = planWake(restIn(ep(10, 40, 17)));
  TEST_ASSERT_EQUAL(WakeKind::Clock, p.kind);
  TEST_ASSERT_EQUAL_INT64(ep(10, 41, 0, 300), p.atMs);  // cambio di minuto + 300 ms di margine
  TEST_ASSERT_EQUAL_INT64(43300, p.sleepMs);  // 42 s 700 ms al cambio, più il margine
  TEST_ASSERT_EQUAL_INT64(ep(10, 55, 17), p.wifiDueMs);  // Wi-Fi fra 15 minuti da adesso (entrata da svegli)
  TEST_ASSERT_FALSE(p.workEnd);
  // Appena dopo il cambio (risveglio dell'ora): il minuto dopo.
  const WakePlan q = planWake(restIn(ep(10, 41, 0, 950), p.wifiDueMs));
  TEST_ASSERT_EQUAL_INT64(ep(10, 42, 0, 300), q.atMs);
  TEST_ASSERT_EQUAL_INT64(p.wifiDueMs, q.wifiDueMs);
  // Svegliati un attimo prima del cambio: mai meno di 1 s di sonno.
  const WakePlan r = planWake(restIn(ep(10, 41, 59, 900), p.wifiDueMs));
  TEST_ASSERT_EQUAL_INT64(ep(10, 42, 0, 900), r.atMs);
  TEST_ASSERT_EQUAL_INT64(kMinSleepMs, r.sleepMs);
}

static void test_wake_saver_five_minutes() {
  const WakePlan p = planWake(restIn(ep(10, 40, 17), 0, true));
  TEST_ASSERT_EQUAL(WakeKind::Clock, p.kind);
  TEST_ASSERT_EQUAL_INT64(ep(10, 45, 0, 300), p.atMs);  // multipli di 5 minuti, come da svegli (mm % 5 == 0)
  const WakePlan q = planWake(restIn(ep(10, 45, 1), p.wifiDueMs, true));
  TEST_ASSERT_EQUAL_INT64(ep(10, 50, 0, 300), q.atMs);
}

// Una catena di 3 ore: Wi-Fi ogni 15 minuti, sempre su un cambio di minuto; in mezzo solo risvegli dell'ora.
static void chain(bool saver, int expectClockPerFull) {
  int64_t now = ep(10, 40, 17);
  int64_t due = 0;
  int64_t lastFull = 0;
  int clock = 0, full = 0, run = 0;
  for (int k = 0; k < 400 && now < ep(13, 40); k++) {
    const WakePlan p = planWake(restIn(now, due, saver));
    TEST_ASSERT_TRUE(p.atMs > now);
    TEST_ASSERT_EQUAL_INT64(300, floorMod(p.atMs, 60000));  // allineato al minuto (+ margine)
    if (p.kind == WakeKind::Full) {
      if (lastFull) {
        TEST_ASSERT_EQUAL_INT64(15 * 60000, p.atMs - lastFull);  // esattamente ogni 15 minuti
        TEST_ASSERT_EQUAL(expectClockPerFull, run);
      }
      lastFull = p.atMs;
      run = 0;
      full++;
      now = p.atMs + 20000;  // il risveglio pieno cerca il PC fino a 20 s
    } else {
      clock++;
      run++;
      now = p.atMs + 400;  // avvio dal sonno (ROM, bootloader) [stimato]
    }
    due = p.wifiDueMs;
  }
  TEST_ASSERT_TRUE(full >= 11);
  TEST_ASSERT_TRUE(clock >= full * expectClockPerFull - expectClockPerFull);
}

static void test_wake_wifi_every_15() {
  chain(false, 14);  // 14 risvegli dell'ora fra due risvegli con il Wi-Fi
  chain(true, 2);    // con il risparmio: 2 (ogni 5 minuti)
}

static void test_wake_workday_end() {
  WakeIn in = restIn(ep(17, 59, 10));
  in.workOn = true;
  in.workEndMs = ep(18, 0);
  WakePlan p = planWake(in);
  TEST_ASSERT_EQUAL(WakeKind::Full, p.kind);  // la fine sveglia «pieno» (festa, PC)
  TEST_ASSERT_TRUE(p.workEnd);
  TEST_ASSERT_EQUAL_INT64(ep(18, 0, 0, 300), p.atMs);
  in.epochMs = ep(17, 47, 30);  // fine lontana: solo l'ora
  p = planWake(in);
  TEST_ASSERT_EQUAL(WakeKind::Clock, p.kind);
  TEST_ASSERT_FALSE(p.workEnd);
  TEST_ASSERT_EQUAL_INT64(ep(17, 48, 0, 300), p.atMs);
  in.epochMs = ep(17, 59, 10);  // fine fuori dal minuto: proprio allora
  in.workEndMs = ep(17, 59, 30);
  p = planWake(in);
  TEST_ASSERT_TRUE(p.workEnd);
  TEST_ASSERT_EQUAL_INT64(ep(17, 59, 30, 300), p.atMs);
  in.saver = true;  // con il risparmio la fine resta esatta
  in.epochMs = ep(17, 51, 0);
  in.workEndMs = ep(18, 0);
  p = planWake(in);
  TEST_ASSERT_EQUAL(WakeKind::Clock, p.kind);
  TEST_ASSERT_EQUAL_INT64(ep(17, 55, 0, 300), p.atMs);
  in.epochMs = ep(17, 55, 1);
  p = planWake(in);
  TEST_ASSERT_TRUE(p.workEnd);
  TEST_ASSERT_EQUAL_INT64(ep(18, 0, 0, 300), p.atMs);
}

static void test_wake_without_clock_bye_battery() {
  WakeIn in = restIn(0);
  in.wallOk = false;  // ora mai arrivata: niente risvegli dell'ora, solo Wi-Fi ogni 15 minuti
  in.workOn = true;
  in.workEndMs = 5000;
  WakePlan p = planWake(in);
  TEST_ASSERT_EQUAL(WakeKind::Full, p.kind);
  TEST_ASSERT_EQUAL_INT64(kTimerWakeMs, p.sleepMs);
  TEST_ASSERT_FALSE(p.workEnd);
  TEST_ASSERT_EQUAL_INT64(0, p.atMs);
  in = restIn(ep(10, 40, 17));
  in.reason = SleepReason::Bye;  // riepilogo «PC spento»: come prima
  p = planWake(in);
  TEST_ASSERT_EQUAL(WakeKind::Full, p.kind);
  TEST_ASSERT_EQUAL_INT64(kTimerWakeMs, p.sleepMs);
  in.reason = SleepReason::Battery;
  p = planWake(in);
  TEST_ASSERT_EQUAL(WakeKind::None, p.kind);
  TEST_ASSERT_EQUAL_INT64(0, p.sleepMs);
}

// ---------- ora del muro dopo il sonno ----------

static void test_wall_keep() {
  WallKeep k;
  wallKeepSave(k, ep(10, 40, 17), 5000000000LL);
  int64_t e = 0;
  TEST_ASSERT_TRUE(wallKeepRestore(k, 5000000000LL + 61500000LL, e));  // 61,5 s di RTC dopo
  TEST_ASSERT_EQUAL_INT64(ep(10, 41, 18, 500), e);
  TEST_ASSERT_FALSE(wallKeepRestore(k, 4000000000LL, e));  // RTC ripartito: spegnimento
  WallKeep bad = k;
  bad.epochMs += 1;  // somma sbagliata
  TEST_ASSERT_FALSE(wallKeepRestore(bad, 5000000000LL, e));
  memset(&bad, 0xA5, sizeof bad);  // memoria RTC_NOINIT a caso dopo l'accensione
  TEST_ASSERT_FALSE(wallKeepRestore(bad, 5000000000LL, e));
}

static void test_rest_keep() {
  static RestKeep k;
  memset(&k, 0, sizeof k);
  TEST_ASSERT_FALSE(restValid(k));
  k.imgOk = 1;
  k.rest = 1;
  k.nextKind = static_cast<uint8_t>(WakeKind::Clock);
  k.img[17] = 0x5A;
  restSeal(k);
  TEST_ASSERT_TRUE(restValid(k));
  TEST_ASSERT_TRUE(clockWakeOk(k, true, SleepReason::Rest));
  TEST_ASSERT_FALSE(clockWakeOk(k, false, SleepReason::Rest));  // istantanea del sonno non valida
  TEST_ASSERT_FALSE(clockWakeOk(k, true, SleepReason::Bye));
  k.img[17] = 0;  // immagine cambiata senza nuova somma
  TEST_ASSERT_FALSE(restValid(k));
  restSeal(k);
  k.nextKind = static_cast<uint8_t>(WakeKind::Full);
  restSeal(k);
  TEST_ASSERT_FALSE(clockWakeOk(k, true, SleepReason::Rest));  // risveglio previsto con il Wi-Fi
  k.nextKind = static_cast<uint8_t>(WakeKind::Clock);
  k.rest = 0;  // sul pannello non c'è il Riposo
  restSeal(k);
  TEST_ASSERT_FALSE(clockWakeOk(k, true, SleepReason::Rest));
  // 0.13.0: il tema che ha disegnato il Riposo fa parte della somma
  strcpy(k.themeId, "prova");
  k.themeCrc = 0xAABBCCDD;
  restSeal(k);
  TEST_ASSERT_TRUE(restValid(k));
  k.themeCrc = 0xAABBCCDE;  // cambiato senza nuova somma
  TEST_ASSERT_FALSE(restValid(k));
  restSeal(k);
  k.themeId[0] = 0;
  TEST_ASSERT_FALSE(restValid(k));
}

// ---------- «--:--» senza l'ora vera ----------

static void test_clock_unknown() {
  static Bench b(loc(10, 30));
  Model &m = b.m();
  ui::setTextHook(hook, nullptr);
  m.sys.wallOk = false;  // nessun welcome né SNTP da questa accensione a freddo
  gTexts.clear();
  b.draw(cvA);  // Home Goccia
  TEST_ASSERT_TRUE(drew("--:--"));
  TEST_ASSERT_FALSE(drew("10:30"));
  pushScreen(m, makeScreen(ScreenId::Sleep));
  gTexts.clear();
  b.draw(cvA);
  TEST_ASSERT_TRUE(drew("--:--"));
  TEST_ASSERT_FALSE(drew("lunedì 12 ottobre"));  // data vuota
  m.sys.wallOk = true;
  gTexts.clear();
  b.draw(cvA);
  TEST_ASSERT_TRUE(drew("10:30"));
  TEST_ASSERT_TRUE(drew("lunedì 12 ottobre"));
  popScreen(m);
  b.ui.act(Action::Menu, b.now, b.wc());  // striscia dei menu
  m.sys.wallOk = false;
  gTexts.clear();
  b.draw(cvA);
  TEST_ASSERT_EQUAL(ScreenId::List, top(m).id);
  TEST_ASSERT_TRUE(drew("--:--"));
  ui::setTextHook(nullptr, nullptr);
}

// ---------- da svegli: un ridisegno a ogni minuto ----------

static void minuteRedraws(ScreenId id) {
  for (int idle = 0; idle < 2; idle++) {
    Bench b(loc(10, 30));
    Model &m = b.m();
    m.set.anim = AnimMode::Spente;  // solo l'orologio (l'animazione chiederebbe altri parziali)
    if (id == ScreenId::Sleep) pushScreen(m, makeScreen(ScreenId::Sleep));
    if (id == ScreenId::List) b.ui.act(Action::Menu, b.now, b.wc());
    m.sys.idle = idle;  // Attesa (core/power): animazioni ferme, ma l'orologio sì
    b.run(1000);
    TEST_ASSERT_EQUAL(id, top(m).id);
    TEST_ASSERT_EQUAL_MESSAGE(3, b.run(3 * 60000), idle ? "Attesa" : "Attivo");  // 10:31, 10:32, 10:33
    m.set.save = true;  // risparmio energetico: ogni 5 minuti
    TEST_ASSERT_EQUAL(2, b.run(10 * 60000));
  }
}

static void test_minute_redraw_awake() {
  minuteRedraws(ScreenId::Sleep);  // Riposo sveglio: cavo, calibrazione
  minuteRedraws(ScreenId::Home);
  minuteRedraws(ScreenId::List);
}

// ---------- risveglio dell'ora: il Riposo ricostruito ----------

// Riposo disegnato dal modello completo (avatar Gatto, sessioni, giornata) e salvato come prima del sonno; un minuto
// dopo il risveglio dell'ora lo ricostruisce da RestKeep + SleepSnap su un modello nuovo (mascotte predefinita) e
// riprende la mascotte dall'immagine salvata: dev'essere identico al Riposo del modello completo al minuto nuovo.
static void rebuild(bool invert, bool rot, bool work) {
  static Bench a(loc(9, 40));
  static RestKeep k;
  static SleepSnap snap;
  Model &m = a.m();
  a.ui.begin(a.now, a.wc());
  a.run(2700);
  m.set.invert = invert;
  m.set.rot = rot;
  setAvatarRef(m.set.defAvatar, "gatto");
  m.sys.live = true;
  m.sys.usb = false;
  if (work) {
    char e[96];
    a.ui.workdayRemote(true, a.now, a.wc(), e, sizeof e);
    TEST_ASSERT_EQUAL_STRING("", e);
  }
  resetStack(m, makeScreen(ScreenId::Home));
  pushScreen(m, makeScreen(ScreenId::Sleep));
  a.draw(cvA, 1);
  memset(&k, 0, sizeof k);
  memcpy(k.img, cvA.data(), sizeof k.img);
  restStateFrom(k, m);
  const ui::SleepRect r = ui::sleepMascotRect(kHeltec213, m, a.wc());
  const int64_t e0 = a.local - kTz;
  memset(&snap, 0, sizeof snap);
  snapFromModel(snap, m, e0, a.now);
  // Un minuto dopo.
  a.run(60000);
  a.draw(cvC, 1);  // il Riposo del modello completo, al minuto nuovo
  static Ui fresh(kHeltec213);
  const Ms now2 = 5000;  // altro avvio: altro tempo monotono
  WallClock wc2;
  wc2.localMs = a.local;
  fresh.begin(now2, wc2);
  restModel(fresh.model(), k, snap, a.local - kTz, now2);
  TEST_ASSERT_EQUAL(ScreenId::Sleep, top(fresh.model()).id);
  TEST_ASSERT_EQUAL(WorkStep::None, workStep(fresh.model(), wc2));
  const ui::SleepRect r2 = ui::sleepMascotRect(kHeltec213, fresh.model(), wc2);
  TEST_ASSERT_TRUE(r == r2);
  ui::compose(cvB, kHeltec213, fresh.model(), 0, now2, wc2);
  TEST_ASSERT_TRUE(memcmp(cvB.data(), cvC.data(), kImg) != 0);  // senza la mascotte salvata no
  ui::keepRect(cvB, k.img, r2, rot);
  TEST_ASSERT_EQUAL_MEMORY(cvC.data(), cvB.data(), kImg);
  TEST_ASSERT_TRUE(memcmp(cvA.data(), cvB.data(), kImg) != 0);  // l'ora è cambiata davvero
}

static void test_clock_wake_rebuild() {
  rebuild(false, false, false);
  rebuild(true, false, false);
  rebuild(false, true, false);
  rebuild(true, true, true);
  rebuild(false, false, true);  // giornata attiva: la mascotte sta più in basso
}

// La mascotte si sposta (giornata finita, mezzanotte): niente risveglio dell'ora, serve quello pieno.
static void test_mascot_moves_with_workday() {
  static Bench b(loc(9, 40));
  Model &m = b.m();
  const ui::SleepRect r0 = ui::sleepMascotRect(kHeltec213, m, b.wc());
  char e[96];
  b.ui.workdayRemote(true, b.now, b.wc(), e, sizeof e);
  const ui::SleepRect r1 = ui::sleepMascotRect(kHeltec213, m, b.wc());
  TEST_ASSERT_FALSE(r0 == r1);
  TEST_ASSERT_EQUAL(r0.x, r1.x);
  TEST_ASSERT_EQUAL(56, r1.w);  // 28 × 2
  TEST_ASSERT_EQUAL(50, r1.h);  // 25 × 2
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_wake_next_minute);
  RUN_TEST(test_wake_saver_five_minutes);
  RUN_TEST(test_wake_wifi_every_15);
  RUN_TEST(test_wake_workday_end);
  RUN_TEST(test_wake_without_clock_bye_battery);
  RUN_TEST(test_wall_keep);
  RUN_TEST(test_rest_keep);
  RUN_TEST(test_clock_unknown);
  RUN_TEST(test_minute_redraw_awake);
  RUN_TEST(test_clock_wake_rebuild);
  RUN_TEST(test_mascot_moves_with_workday);
  return UNITY_END();
}
