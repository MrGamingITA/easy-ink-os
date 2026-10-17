// Banco di parità: legge da stdin modelli e domande (formato di gen-fixture.mjs, testi in esadecimale) e risponde con
// firmware/src/core/theme_values.cpp e le funzioni del modello che usa. Una riga di risposta per domanda.
//   M <campi del modello>      nuovo caso (azzera il modello)
//   S <campi di una sessione>  aggiunge una sessione
//   Q <domanda>                v valueText · c condTrue · r refIndex · s refSession · w whenTrue · p pose · x expand · f workFrac
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/model.h"
#include "core/strbuf.h"
#include "core/theme_values.h"
#include "core/workday.h"

using namespace cal;
using namespace cal::theme;

// Simboli che pet.cpp e theme_format.cpp chiedono al linker ma che i valori non toccano mai.
uint32_t avatarCrc32(const uint8_t *, size_t) { abort(); }
bool hasFont(int, int, bool) { abort(); }
namespace cal {
size_t base64Decode(const char *, size_t, uint8_t *, size_t) { abort(); }
bool validAvatarId(const char *) { abort(); }
MsgType msgType(JsonVariantConst) { abort(); }
}  // namespace cal

static std::string unhex(const char *s) {
  std::string o;
  if (s[0] == '-' && !s[1]) return o;  // «-» = testo vuoto
  for (; s[0] && s[1]; s += 2) { char b[3] = {s[0], s[1], 0}; o += char(strtol(b, 0, 16)); }
  return o;
}
static void hex(const char *s) {
  if (!*s) printf("-");
  for (; *s; s++) printf("%02x", uint8_t(*s));
}

static std::vector<char *> tok;
static size_t at;
static const char *next() { return at < tok.size() ? tok[at++] : ""; }
static long long I() { return strtoll(next(), 0, 10); }
static double D() { return strtod(next(), 0); }
static std::string X() { return unhex(next()); }

static Model gM;
static Ms gNow, gLocal;

static Ctx ctx(int screen, int vref, int frame) {
  Ctx c;
  c.m = &gM;
  c.now = gNow;
  c.wc = WallClock{gLocal};
  c.frame = frame;
  c.screen = Screen(screen);
  c.ref = refIndex(gM, gNow, c.screen, Ref(vref));
  return c;
}
static const Session *rowAt(long long k) { return k < 0 || k >= gM.nSessions ? nullptr : &gM.sessions[k]; }

static void model() {
  newModel(gM, 0, WallClock{0});
  gM.nSessions = 0;
  memset(&gM.limits, 0, sizeof gM.limits);
  memset(&gM.stats, 0, sizeof gM.stats);
  memset(&gM.work, 0, sizeof gM.work);
  memset(&gM.bat, 0, sizeof gM.bat);
  gNow = I();
  gLocal = I();
  gM.sys.wallOk = I();
  gM.sys.batt = D();
  gM.sys.usb = I();
  gM.sys.wifi = I();
  scopy(gM.sys.ssid, X().c_str());
  gM.sys.pc = I();
  scopy(gM.sys.pcHost, X().c_str());
  gM.set.night = I();
  gM.set.game = I();
  gM.set.workStart = int16_t(I());
  gM.set.workEnd = int16_t(I());
  gM.ui.sel = int(I());
  gM.ui.selUntil = I();
  gM.work.on = I();
  gM.work.endLocal = I();
  gM.work.fromLocal = I();
  gM.work.doneUntilLocal = I();
  gM.stats.tasks = int(I());
  gM.stats.linesAdd = long(I());
  gM.stats.linesDel = long(I());
  gM.stats.cost = D();
  gM.limits.hasH5 = I();
  gM.limits.h5 = D();
  gM.limits.hasD7 = I();
  gM.limits.d7 = D();
  gM.pet.known = I();
  gM.pet.on = I();
  gM.pet.level = int(I());
  gM.pet.xp = int(I());
  gM.pet.xpLevelStart = int(I());
  gM.pet.xpNext = int(I());
  gM.pet.energy = int(I());
  gM.pet.tired = I();
  gM.acc = I();
  gM.air.co2 = D();
  gM.bat.cal.valid = I();
  gM.bat.cal.active = D();
  gM.bat.cal.idle = D();
  gM.bat.hasSleep = I();
  gM.bat.sleep = D();
  for (int k = 0; k < 3; k++) gM.bat.mix[k] = int(I());
  gM.bat.trend = D();
}

static void session() {
  if (gM.nSessions >= kMaxSessions) return;
  Session &s = gM.sessions[gM.nSessions++];
  memset(&s, 0, sizeof s);
  scopy(s.name, X().c_str());
  s.state = SessionState(I());
  scopy(s.detail, X().c_str());
  scopy(s.summary, X().c_str());
  s.since = I();
  s.ctx = int(I());
  s.subs = int(I());
  s.test = TestResult(I());
  s.done = int(I());
  s.total = int(I());
}

static void query() {
  static char out[256];
  const char q = next()[0];
  if (q == 'v') {  // v screen vref frame id row
    const int sc = int(I()), vr = int(I()), fr = int(I());
    const uint16_t id = uint16_t(I());
    valueText(id, ctx(sc, vr, fr), rowAt(I()), out, sizeof out);
    hex(out);
  } else if (q == 'c') {  // c op arg frame subject
    const int op = int(I()), arg = int(I()), fr = int(I());
    printf("%d", condTrue(uint8_t(op), uint8_t(arg), ctx(1, 1, fr), rowAt(I())) ? 1 : 0);
  } else if (q == 'r') {  // r screen ref
    const int sc = int(I());
    printf("%d", refIndex(gM, gNow, Screen(sc), Ref(I())));
  } else if (q == 's') {  // s screen vref ref row
    const int sc = int(I()), vr = int(I()), r = int(I());
    const Session *p = refSession(ctx(sc, vr, 0), Ref(r), rowAt(I()));
    printf("%d", p ? int(p - gM.sessions) : -1);
  } else if (q == 'w') {
    printf("%d", whenTrue(gM, When(I())) ? 1 : 0);
  } else if (q == 'p') {
    printf("%s", poseKey(pose(gM, gM.sessions[I()])));
  } else if (q == 'x') {  // x screen vref frame row upper n [kind filter arg]…  (kind 0: arg = testo in hex)
    const int sc = int(I()), vr = int(I()), fr = int(I());
    const Session *row = rowAt(I());
    const bool up = I();
    const int n = int(I());
    std::vector<uint8_t> blk = {0, 0, 4, 0}, segs;
    for (int k = 0; k < n; k++) {
      const int kind = int(I()), filter = int(I());
      int arg;
      if (kind == 0) {
        const std::string s = X();
        arg = int(blk.size()) - 4;
        blk.push_back(uint8_t(s.size()));
        blk.insert(blk.end(), s.begin(), s.end());
      } else {
        arg = int(I());
      }
      segs.push_back(uint8_t(kind | filter << 2));
      segs.push_back(uint8_t(arg & 0xFF));
      segs.push_back(uint8_t(arg >> 8));
    }
    expand(blk.data(), blk.size(), segs.data(), n, ctx(sc, vr, fr), row, up, out, sizeof out);
    hex(out);
  } else if (q == 'f') {
    const double f = workFrac(gM, WallClock{gLocal});
    uint64_t u;
    memcpy(&u, &f, 8);
    printf("%016llx", (unsigned long long)u);
  }
  printf("\n");
}

int main() {
  static char line[1 << 16];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\r\n")] = 0;
    tok.clear();
    at = 0;
    for (char *p = strtok(line, " "); p; p = strtok(nullptr, " ")) tok.push_back(p);
    if (tok.empty()) continue;
    const char k = next()[0];
    if (k == 'M') model();
    else if (k == 'S') session();
    else if (k == 'Q') query();
  }
}
