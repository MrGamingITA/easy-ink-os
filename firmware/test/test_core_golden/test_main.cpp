// cal::Ui rifà lo scenario di tools/golden/core-golden.js e confronta, passo per passo, la
// fotografia del modello con quella presa dal simulatore vero (golden_core.h).
#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "core/menu.h"
#include "core/strbuf.h"
#include "core/ui.h"
#include "core/workday.h"
#include "golden_core.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static std::string I(long long v) { return std::to_string(v); }
static long long r6(double x) { return static_cast<long long>(floor(x * 1e6 + 0.5)); }
static long long jr(double x) { return static_cast<long long>(floor(x + 0.5)); }

struct Replay {
  Ui ui;
  RefreshCounter counter;
  Rng rng;
  Ms now;
  Replay() : ui(kHeltec213), counter(kHeltec213.fullEvery), rng(GOLDEN_SEED), now(GOLDEN_T0) {}
  WallClock wc() const { WallClock w; w.localMs = now; return w; }  // fuso UTC come nel golden
  long long rel(Ms t) const { return t ? t - now : 0; }
};

static const char *muteKey(MuteKind k) { return k == MuteKind::Hour ? "hour" : k == MuteKind::Night ? "night" : ""; }
static const char *testKey(TestResult t) { return t == TestResult::Ok ? "ok" : t == TestResult::Ko ? "ko" : "null"; }

static std::string screenFields(const Replay &R, const ScreenEntry &tp) {
  switch (tp.id) {
    case ScreenId::Boot: return "until=" + I(R.rel(tp.until));
    case ScreenId::Perm: return "i=" + I(tp.i) + ",choice=" + I(tp.choice) + ",dl=0";  // nessuna scadenza (M5)
    case ScreenId::List: return std::string("menu=") + menuKey(tp.menu) + ",sel=" + I(tp.sel);
    case ScreenId::WifiScan: return "sel=" + I(tp.sel) + ",scan=" + I(tp.scanUntil > R.now ? R.rel(tp.scanUntil) : 0);
    case ScreenId::Storico: case ScreenId::Servizi: return "off=" + I(tp.off);
    case ScreenId::Qr: return std::string("ssid=") + tp.ssid;
    case ScreenId::Connecting: return std::string("ssid=") + tp.ssid + ",until=" + I(R.rel(tp.until));
    default: return "";
  }
}

static std::string snapshot(Replay &R, RefreshKind kind) {
  const Model &m = R.ui.model();
  const DeviceProfile &dev = R.ui.device();
  const Ms now = R.now;
  const Settings &s = m.set;
  const Sys &y = m.sys;
  const Stats &st = m.stats;
  std::string o;
  o += std::string("r=") + refreshKey(kind) + " p=" + I(R.counter.partials()) + "\n";
  o += std::string("a=") + R.ui.lastAction() + "\n";
  o += "stack=";
  for (int k = 0; k < m.ui.depth; k++) o += std::string(k ? "," : "") + screenKey(m.ui.stack[k].id);
  o += " [" + screenFields(R, top(m)) + "]\n";
  o += "ui=" + I(m.ui.sel) + "," + I(m.ui.selUntil > now ? R.rel(m.ui.selUntil) : 0) + "," + I(m.ui.page) + "\n";
  o += "toast=";
  if (m.toast.msg[0] && m.toast.until >= now) o += std::string(m.toast.msg) + "@" + I(R.rel(m.toast.until));
  o += "\n";
  o += "fr=" + I(R.ui.frame(now)) + "," + I(R.ui.rawFrame()) + " rate=" + I(jr(animRate(dev, m, now) * 1000.0)) + "\n";
  o += std::string("set=") + homeKey(s.homeStyle) + "," + animKey(s.anim) + "," + (s.speed == SpeedMode::Massima ? "massima" : "profilo") +
       "," + I(s.invert) + "," + I(s.rot) + "," + I(s.mute > now ? R.rel(s.mute) : 0) + "," + muteKey(s.muteKind) + "," + I(s.night) +
       "," + I(s.permOn) + "," + I(s.save) + "," + I(s.hookDirect) + "\n";
  o += "sys=" + I(y.wifi) + "," + y.ssid + "," + I(y.rssi) + "," + I(y.pc) + "," + I(jr(y.batt * 1000)) + "," + I(jr(y.temp * 10)) + "," +
       I(R.rel(y.lastPc)) + "," + I(R.rel(y.bootAt)) + "\n";
  o += "lim=" + I(r6(m.limits.h5)) + "," + I(r6(m.limits.d7)) + "," + I(R.rel(m.limits.h5Reset)) + " focus=" +
       I(m.focus.until ? R.rel(m.focus.until) : 0) + " burst=" + I(R.rel(m.burstUntil)) + "," + I(R.rel(m.nextBurst)) + "\n";
  o += "stats=" + I(st.tasks) + "," + I(st.permDisplay) + "," + I(st.permTerminal) + "," + I(st.waitSum) + "," + I(st.waitN) + "," +
       I(st.linesAdd) + "," + I(st.linesDel) + "," + I(r6(st.cost)) + "\n";
  for (int i = 0; i < m.nSessions; i++) {
    const Session &x = m.sessions[i];
    char meta[256];
    metaLine(x, now, meta, sizeof meta);
    o += "s" + I(i) + "=" + x.name + "|" + stateInfo(x.state).key + "|" + x.detail + "|" + I(x.done) + "/" + I(x.total) + "|" + I(x.ctx) +
         "|" + I(x.subs) + "|" + testKey(x.test) + "|" + x.summary + "|" + I(x.ticks) + "|" + (x.hasAttnAt ? I(R.rel(x.attnAt)) : "-") +
         "|" + I(R.rel(x.since)) + "|" + poseKey(pose(m, x)) + "\n";
    o += std::string("  meta=") + meta + " tasks=";
    for (int k = 0; k < x.nTasks; k++) o += I(static_cast<int>(x.tasks[k].status));
    o += "\n";
  }
  o += "log=" + I(m.log.n) + " ";
  for (int k = 0; k < 3 && k < m.log.n; k++) {
    const LogEntry &e = m.log.at(k);
    o += std::string(k ? " / " : "") + I(R.rel(e.t)) + ":" + e.name + ":" + e.text;
  }
  o += "\n";
  o += "q=" + I(urgentIdx(m)) + "," + I(sIdx(m, now)) + "," + I(unaIdx(m, now)) + "," + I(attention(m)) + "," + I(quiet(m, now)) + "," +
       I(muted(m, now)) + "," + I(powerSave(m)) + "," + I(hasAlerts(m, now)) + "\n";
  char prof[160];
  profileText(dev, m, now, prof, sizeof prof);
  o += std::string("prof=") + prof;
  // 0.9.0: giornata di lavoro e gioco (work= e game= del golden)
  const Workday &w = m.work;
  const Game &g = m.game;
  const int64_t loc = R.wc().localMs;
  char line[24];
  workLine(m, R.wc(), line, sizeof line);
  static const char *const kLast[] = {"", "win", "tie", "lose"};
  o += "\nwork=" + I(w.on) + "," + I(w.on ? w.endLocal - loc : 0) + "," + I(w.doneUntilLocal > loc ? w.doneUntilLocal - loc : 0) + "," +
       I(w.party > now ? R.rel(w.party) : 0) + "," + line + "," + I(m.set.workStart) + "," + I(m.set.workEnd);
  o += " game=" + I(g.card) + "," + I(g.prev) + "," + I(g.streak) + "," + I(g.best) + "," + kLast[static_cast<int>(g.last)] + "," +
       I(g.over) + "," + I(g.startBest) + "," + I(g.seed);
  return o;
}

static const char *kindKey(const MenuItem &it, std::string &tmp) {
  switch (it.kind) {
    case ItemKind::Go: tmp = std::string("go:") + menuKey(it.go); break;
    case ItemKind::Open: tmp = std::string("open:") + screenKey(it.open); break;
    case ItemKind::Act: tmp = std::string("act:") + actKey(it.act); break;
    case ItemKind::Net: tmp = "net"; break;
    default: tmp = "info"; break;
  }
  return tmp.c_str();
}

static void dumpList(std::string &o, const std::string &title, const MenuList &L) {
  if (!o.empty()) o += "\n";
  o += "# " + title;
  TEST_ASSERT_FALSE_MESSAGE(L.truncated, ("testo troncato nel menu " + title).c_str());
  for (int k = 0; k < L.n; k++) {
    const MenuItem &it = L.items[k];
    std::string tmp;
    o += std::string("\n") + it.label + "|" + it.val + "|" + kindKey(it, tmp) + "|" + (it.hasBars ? "bars:" + I(it.bars) : "") + "|" +
         (it.lock ? "lock" : "") + "|" + it.help;
  }
}

static std::string menus(Replay &R) {
  static MenuList L;  // grande: fuori dallo stack
  std::string o;
  const MenuId ids[] = {MenuId::Main, MenuId::Wifi, MenuId::Display, MenuId::Avvisi, MenuId::Info, MenuId::Tools, MenuId::Work, MenuId::Gb};
  for (MenuId id : ids) {
    menuList(id, R.ui.device(), R.ui.model(), R.now, R.wc(), L);
    dumpList(o, std::string(menuKey(id)) + ":" + L.title, L);
  }
  netItems(R.ui.model(), L);
  dumpList(o, "reti", L);
  return o;
}

static Gesture parseGesture(const char *g) {
  if (!strcmp(g, "1x")) return Gesture::Press1;
  if (!strcmp(g, "2x")) return Gesture::Press2;
  if (!strcmp(g, "3x")) return Gesture::Press3;
  if (!strcmp(g, "long")) return Gesture::Long;
  return Gesture::VeryLong;
}

// Primo punto in cui due fotografie divergono, per un messaggio leggibile.
static void report(size_t k, const char *op, const std::string &want, const std::string &got) {
  size_t a = 0, line = 1;
  while (a < want.size() && a < got.size() && want[a] == got[a]) { if (want[a] == '\n') line++; a++; }
  size_t ws = want.rfind('\n', a ? a - 1 : 0), gs = got.rfind('\n', a ? a - 1 : 0);
  ws = ws == std::string::npos ? 0 : ws + 1;
  gs = gs == std::string::npos ? 0 : gs + 1;
  std::string wl = want.substr(ws, want.find('\n', ws) - ws), gl = got.substr(gs, got.find('\n', gs) - gs);
  printf("\npasso %u (%s), riga %u\n  simulatore: %s\n  firmware:   %s\n", (unsigned)k, op, (unsigned)line, wl.c_str(), gl.c_str());
}

static void test_scenario_matches_simulator() {
  static Replay R;  // il modello è grande: fuori dallo stack
  R.ui.begin(R.now, R.wc());  // il simulatore fa show(): non conta come parziale
  const size_t n = sizeof GOLDEN_OPS / sizeof GOLDEN_OPS[0];
  size_t checked = 0;
  for (size_t k = 0; k < n; k++) {
    const GoldenOp &op = GOLDEN_OPS[k];
    if (!strcmp(op.op, "adv")) { R.now += op.a; continue; }
    std::string got;
    if (!strcmp(op.op, "menus")) {
      got = menus(R);
    } else {
      RefreshKind kind = RefreshKind::None;
      if (!strncmp(op.op, "g:", 2)) kind = R.ui.gesture(parseGesture(op.op + 2), R.now, R.wc());
      else if (!strcmp(op.op, "tick")) kind = R.ui.tick(R.now, R.wc());
      else if (!strcmp(op.op, "step")) kind = R.ui.step(R.now, R.wc(), R.rng);
      else if (!strcmp(op.op, "slow")) kind = R.ui.slowTick(R.now, R.rng);
      else if (!strcmp(op.op, "raise")) kind = R.ui.raise(op.a, static_cast<SessionState>(op.b), R.now, R.rng);
      R.counter.apply(kind);
      got = snapshot(R, kind);
    }
    if (got != op.expect) {
      report(k, op.op, op.expect, got);
      TEST_FAIL_MESSAGE("la fotografia del modello diverge dal simulatore");
    }
    checked++;
  }
  printf("\n%u fotografie uguali al simulatore\n", (unsigned)checked);
  TEST_ASSERT_TRUE(checked > 700);
}

static void test_formats_match_simulator() {
  const size_t n = sizeof GOLDEN_FMT / sizeof GOLDEN_FMT[0];
  for (size_t k = 0; k < n; k++) {
    const GoldenFmt &c = GOLDEN_FMT[k];
    char out[96];
    WallClock wc;
    wc.localMs = c.a;
    if (!strcmp(c.fn, "ago")) ago(c.a, c.a - c.b, out, sizeof out);
    else if (!strcmp(c.fn, "fmt1")) fmt1(c.a / 1000.0, out, sizeof out);
    else if (!strcmp(c.fn, "date")) dateStr(wc, out, sizeof out);
    else if (!strcmp(c.fn, "hm")) hmLocal(c.a, out, sizeof out);
    else resetLabel(wc, c.a, c.a + c.b, out, sizeof out);
    char msg[160];
    snprintf(msg, sizeof msg, "%s(%lld, %lld)", c.fn, (long long)c.a, (long long)c.b);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expect, out, msg);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_formats_match_simulator);
  RUN_TEST(test_scenario_matches_simulator);
  return UNITY_END();
}
