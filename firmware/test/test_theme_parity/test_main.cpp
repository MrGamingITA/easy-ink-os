// Parità C++/JS dei temi (docs/TEMI.md §7): per ogni tema di prova × schermata × scenario l'interprete C++ (sul CTHM compilato
// dal servizio) disegna esattamente come render.js (sul JSON). Il golden lo scrive tools/golden/theme-golden.mjs.
// In caso di differenza scrive il PBM del firmware in firmware/.pio/theme-parity/ (confronto con --pbm del generatore).
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/strbuf.h"
#include "core/theme_format.h"
#include "core/theme_runtime.h"
#include "gfx/avatar.h"
#include "gfx/canvas.h"
#include "ui/theme_draw.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static std::string repoRoot() {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  return k != std::string::npos ? f.substr(0, k) : "../";
}
static std::string readText(const std::string &p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static SessionState stateOf(const char *k) {
  for (int i = 0; i < int(SessionState::Count); i++)
    if (!strcmp(kStates[i].key, k)) return SessionState(i);
  return SessionState::Idle;
}

// Orologio della fusione (base → extends → scenario): vince l'ultimo che c'è.
static void clockOf(JsonVariantConst o, Ms &now, int64_t &local, int &frame) {
  if (!o["now"].isNull()) now = o["now"].as<long long>();
  if (!o["local"].isNull()) local = o["local"].as<long long>();
  if (!o["frame"].isNull()) frame = o["frame"].as<int>();
}

// Stessa normalizzazione di makeModel() in values.js: si parte da zero, poi i campi presenti. now è quello finale
// della fusione (scenarios.json): since delle sessioni e scadenza di sel si contano da lì, come in makeModel().
static void apply(Model &m, Ms now, JsonVariantConst o) {
  if (!o["wallOk"].isNull()) m.sys.wallOk = o["wallOk"].as<bool>();
  if (!o["sel"].isNull()) { m.ui.sel = o["sel"].as<int>(); m.ui.selUntil = now + 15000; }
  JsonVariantConst y = o["sys"];
  if (!y["batt"].isNull()) m.sys.batt = y["batt"].as<double>();
  if (!y["usb"].isNull()) m.sys.usb = y["usb"].as<bool>();
  if (!y["wifi"].isNull()) m.sys.wifi = y["wifi"].as<bool>();
  if (!y["ssid"].isNull()) scopy(m.sys.ssid, y["ssid"].as<const char *>());
  if (!y["rssi"].isNull()) m.sys.rssi = y["rssi"].as<int>();
  if (!y["pc"].isNull()) m.sys.pc = y["pc"].as<bool>();
  if (!y["pcOff"].isNull()) m.sys.pcOff = y["pcOff"].as<bool>();
  if (!y["pcHost"].isNull()) scopy(m.sys.pcHost, y["pcHost"].as<const char *>());
  JsonVariantConst s = o["set"];
  if (!s["night"].isNull()) m.set.night = s["night"].as<bool>();
  if (!s["game"].isNull()) m.set.game = s["game"].as<bool>();
  if (!s["workStart"].isNull()) m.set.workStart = s["workStart"].as<int>();
  if (!s["workEnd"].isNull()) m.set.workEnd = s["workEnd"].as<int>();
  if (!s["defAvatar"].isNull()) setAvatarRef(m.set.defAvatar, s["defAvatar"].as<const char *>());
  if (o["sessions"].is<JsonArrayConst>()) {
    m.nSessions = 0;
    for (JsonVariantConst x : o["sessions"].as<JsonArrayConst>()) {
      if (m.nSessions >= kMaxSessions) break;
      Session &z = m.sessions[m.nSessions++];
      memset(&z, 0, sizeof z);
      scopy(z.name, x["name"] | "");
      z.state = stateOf(x["state"] | "idle");
      scopy(z.detail, x["detail"] | "");
      scopy(z.summary, x["summary"] | "");
      z.since = now - (x["ago"] | 0LL);
      z.ctx = x["ctx"] | 0;
      z.subs = x["subs"] | 0;
      const char *t = x["test"] | "";
      z.test = !strcmp(t, "ok") ? TestResult::Ok : !strcmp(t, "ko") ? TestResult::Ko : TestResult::None;
      z.done = x["done"] | 0;
      z.total = x["total"] | 0;
      setAvatarRef(z.avatar, x["avatar"] | "");
    }
  }
  JsonVariantConst w = o["work"];
  if (!w["on"].isNull()) m.work.on = w["on"].as<bool>();
  if (!w["endLocal"].isNull()) m.work.endLocal = w["endLocal"].as<long long>();
  if (!w["fromLocal"].isNull()) m.work.fromLocal = w["fromLocal"].as<long long>();
  if (!w["doneUntilLocal"].isNull()) m.work.doneUntilLocal = w["doneUntilLocal"].as<long long>();
  JsonVariantConst st = o["stats"];
  if (!st["tasks"].isNull()) m.stats.tasks = st["tasks"].as<int>();
  if (!st["linesAdd"].isNull()) m.stats.linesAdd = st["linesAdd"].as<long>();
  if (!st["linesDel"].isNull()) m.stats.linesDel = st["linesDel"].as<long>();
  if (!st["cost"].isNull()) m.stats.cost = st["cost"].as<double>();
  JsonVariantConst l = o["limits"];
  // null = nessun dato: è già così dopo zero(), quindi basta il numero
  if (l["h5"].is<double>()) { m.limits.hasH5 = true; m.limits.h5 = l["h5"].as<double>(); }
  if (l["d7"].is<double>()) { m.limits.hasD7 = true; m.limits.d7 = l["d7"].as<double>(); }
  JsonVariantConst p = o["pet"];
  if (!p.isNull()) {
    // campo per campo, come la fusione di makeModel()
    if (!p["known"].isNull()) m.pet.known = p["known"].as<bool>();
    if (!p["on"].isNull()) m.pet.on = p["on"].as<bool>();
    if (!p["level"].isNull()) m.pet.level = p["level"].as<int>();
    if (!p["xp"].isNull()) m.pet.xp = p["xp"].as<long>();
    if (!p["xpLevelStart"].isNull()) m.pet.xpLevelStart = p["xpLevelStart"].as<long>();
    if (!p["xpNext"].isNull()) m.pet.xpNext = p["xpNext"].as<long>();
    if (!p["energy"].isNull()) m.pet.energy = p["energy"].as<int>();
    if (!p["tired"].isNull()) m.pet.tired = p["tired"].as<bool>();
    if (!p["wear"].isNull()) setAvatarRef(m.pet.wear, p["wear"].as<const char *>());
  }
  if (o["co2"].is<double>()) { m.acc = true; m.air.co2 = o["co2"].as<double>(); }
  JsonVariantConst b = o["bat"];
  if (!b["trend"].isNull()) m.bat.trend = b["trend"].as<double>();
}

static void zero(Model &m, Ms now, int64_t local) {
  newModel(m, now, WallClock{local});
  m.nSessions = 0;
  memset(&m.limits, 0, sizeof m.limits);
  memset(&m.stats, 0, sizeof m.stats);
  memset(&m.work, 0, sizeof m.work);
  memset(&m.pet, 0, sizeof m.pet);
  memset(&m.bat, 0, sizeof m.bat);
  m.acc = false;
  m.ui.sel = 0;
  m.ui.selUntil = 0;
}

static void writePbm(const std::string &name, const Canvas<250, 122> &cv) {
  const std::string dir = repoRoot() + "firmware/.pio/theme-parity";
  std::filesystem::create_directories(dir);
  std::ofstream out(dir + "/" + name + "-cpp.pbm", std::ios::binary);
  out << "P4\n250 122\n";
  out.write(reinterpret_cast<const char *>(cv.data()), Canvas<250, 122>::kBytes);
}

// I buffer del disegno sono quelli dell'arena del Runtime, dopo un prestito alla partita che li ha sporcati tutti.
static theme::Runtime gRt;

static void test_parity() {
  uint8_t *arena = gRt.lend(theme::kLendBytes);
  TEST_ASSERT_NOT_NULL(arena);
  memset(arena, 0xA5, theme::kLendBytes);
  gRt.giveBack();
  TEST_ASSERT_NOT_NULL(gRt.drawScratch());
  static JsonDocument sc, gold;
  TEST_ASSERT_FALSE(deserializeJson(sc, readText(repoRoot() + "protocol/themes/scenarios.json")));
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(gold, readText(repoRoot() + "firmware/test/test_theme_parity/golden.json")),
                            "golden.json mancante: node tools/golden/theme-golden.mjs");
  static Model m;
  static Canvas<250, 122> cv;
  int n = 0, bad = 0;
  for (JsonVariantConst c : gold["cases"].as<JsonArrayConst>()) {
    const std::string theme = c["theme"].as<const char *>(), screen = c["screen"].as<const char *>(), name = c["scenario"].as<const char *>();
    const std::string bin = readText(repoRoot() + "protocol/themes/fixtures/" + theme + ".cthm");
    theme::Screen key = theme::Screen::None;
    for (int s = 1; s < int(theme::Screen::Count); s++)
      if (screen == theme::screenName(theme::Screen(s))) key = theme::Screen(s);
    const uint8_t *blk;
    uint16_t len;
    TEST_ASSERT_TRUE_MESSAGE(theme::findScreen(reinterpret_cast<const uint8_t *>(bin.data()), bin.size(), key, blk, len),
                             (theme + ".cthm senza la schermata " + screen).c_str());
    JsonVariantConst over, ext;
    for (JsonVariantConst s : sc["scenarios"].as<JsonArrayConst>())
      if (name == (s["name"] | "")) over = s;
    if (!over["extends"].isNull())
      for (JsonVariantConst s : sc["scenarios"].as<JsonArrayConst>())
        if (!strcmp(s["name"] | "", over["extends"].as<const char *>())) ext = s;
    TEST_ASSERT_FALSE_MESSAGE(over.isNull() || (!over["extends"].isNull() && ext.isNull()), name.c_str());
    Ms now = 0;
    int64_t local = 0;
    int frame = 0;
    for (JsonVariantConst o : {sc["base"].as<JsonVariantConst>(), ext, over}) clockOf(o, now, local, frame);
    zero(m, now, local);
    for (JsonVariantConst o : {sc["base"].as<JsonVariantConst>(), ext, over}) apply(m, now, o);
    cv.clear(false);
    CanvasSurface<250, 122> surf(cv);
    ui::ThemeDrawInfo info;
    ui::drawThemeBlock(surf, kHeltec213, m, frame, now, WallClock{local}, key, blk, len, &info, gRt.drawScratch());
    const uint32_t crc = avatarCrc32(cv.data(), Canvas<250, 122>::kBytes);
    const bool same = crc == c["crc"].as<uint32_t>() && info.drawn == c["drawn"].as<bool>() &&
                      info.nextChange == c["next"].as<long long>() &&
                      (c["mascot"].isNull() ? !info.hasMascot
                                            : info.hasMascot && info.mx == c["mascot"][0].as<int>() && info.my == c["mascot"][1].as<int>() &&
                                                  info.mw == c["mascot"][2].as<int>() && info.mh == c["mascot"][3].as<int>());
    n++;
    if (!same) {
      bad++;
      const std::string id = theme + "-" + screen + "-" + name;
      writePbm(id, cv);
      printf("diverso: %s (crc %08x, drawn %d, next %lld)\n", id.c_str(), unsigned(crc), int(info.drawn), (long long)info.nextChange);
    }
  }
  printf("parità dei temi: %d casi, %d diversi\n", n, bad);
  TEST_ASSERT_EQUAL_MESSAGE(165, n, "3 temi, 15 schermate × 11 scenari");
  TEST_ASSERT_EQUAL(0, bad);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parity);
  return UNITY_END();
}
