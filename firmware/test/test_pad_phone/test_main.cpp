// Report v1 del telefono (spec app iPhone §2.1) e mantenimento 3 s: contro protocol/vectors/pad-report-v1.json.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/pad.h"
#include "core/pad_phone.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static std::string readFile(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::vector<uint8_t> hexBytes(const char *s) {
  std::vector<uint8_t> v;
  for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
    char b[3] = {s[i], s[i + 1], 0};
    v.push_back(static_cast<uint8_t>(strtoul(b, nullptr, 16)));
  }
  return v;
}

static JsonDocument gDoc;

static void test_vectors_loaded() {
  const std::string js = readFile("../protocol/vectors/pad-report-v1.json");
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 500, "manca protocol/vectors/pad-report-v1.json");
  TEST_ASSERT_FALSE(deserializeJson(gDoc, js));
  TEST_ASSERT_EQUAL_STRING("calamaio-pad-report", gDoc["format"]);
  TEST_ASSERT_EQUAL(8, gDoc["length"].as<int>());
  TEST_ASSERT_EQUAL(24, gDoc["cases"].size());
}

// I bit dei vettori sono quelli di pad::Button (un'app con bit diversi muoverebbe altri tasti).
static void test_vector_bits_match_button_enum() {
  JsonObjectConst b = gDoc["bits"];
  TEST_ASSERT_EQUAL(16, b.size());
  TEST_ASSERT_EQUAL(pad::A, 1u << b["A"].as<int>());
  TEST_ASSERT_EQUAL(pad::B, 1u << b["B"].as<int>());
  TEST_ASSERT_EQUAL(pad::X, 1u << b["X"].as<int>());
  TEST_ASSERT_EQUAL(pad::Y, 1u << b["Y"].as<int>());
  TEST_ASSERT_EQUAL(pad::LB, 1u << b["LB"].as<int>());
  TEST_ASSERT_EQUAL(pad::RB, 1u << b["RB"].as<int>());
  TEST_ASSERT_EQUAL(pad::View, 1u << b["View"].as<int>());
  TEST_ASSERT_EQUAL(pad::Menu, 1u << b["Menu"].as<int>());
  TEST_ASSERT_EQUAL(pad::Guide, 1u << b["Guide"].as<int>());
  TEST_ASSERT_EQUAL(pad::LS, 1u << b["LS"].as<int>());
  TEST_ASSERT_EQUAL(pad::RS, 1u << b["RS"].as<int>());
  TEST_ASSERT_EQUAL(pad::Share, 1u << b["Share"].as<int>());
  TEST_ASSERT_EQUAL(pad::Up, 1u << b["Up"].as<int>());
  TEST_ASSERT_EQUAL(pad::Down, 1u << b["Down"].as<int>());
  TEST_ASSERT_EQUAL(pad::Left, 1u << b["Left"].as<int>());
  TEST_ASSERT_EQUAL(pad::Right, 1u << b["Right"].as<int>());
}

static void test_vectors_decode() {
  for (JsonObjectConst c : gDoc["cases"].as<JsonArrayConst>()) {
    const char *name = c["name"];
    const std::vector<uint8_t> d = hexBytes(c["hex"]);
    pad::State s;
    memset(&s, 0x55, sizeof s);  // sporco: la decodifica deve scrivere tutto
    pad::PhoneMeta meta = {77, false};
    const pad::PhoneParse p = pad::parsePhoneReport(d.data(), d.size(), s, meta);
    if (!c["valid"].as<bool>()) {
      TEST_ASSERT_NOT_EQUAL_MESSAGE(static_cast<int>(pad::PhoneParse::Ok), static_cast<int>(p), name);
      TEST_ASSERT_EQUAL_STRING_MESSAGE(c["reason"].as<const char *>(), pad::phoneParseName(p), name);
      TEST_ASSERT_EQUAL_MESSAGE(77, meta.seq, name);  // intatto
      continue;
    }
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(pad::PhoneParse::Ok), static_cast<int>(p), name);
    TEST_ASSERT_EQUAL_MESSAGE(c["buttons"].as<int>(), s.buttons, name);
    TEST_ASSERT_EQUAL_MESSAGE(c["lx"].as<int>(), s.lx, name);
    TEST_ASSERT_EQUAL_MESSAGE(c["ly"].as<int>(), s.ly, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, s.rx, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, s.ry, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, s.lt, name);
    TEST_ASSERT_EQUAL_MESSAGE(0, s.rt, name);
    TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(pad::Kind::Phone), static_cast<int>(s.src), name);
    TEST_ASSERT_EQUAL_MESSAGE(c["seq"].as<int>(), meta.seq, name);
    TEST_ASSERT_EQUAL_MESSAGE(c["fg"].as<bool>(), meta.fg, name);
  }
}

static void test_null_report_rejected() {
  pad::State s;
  pad::PhoneMeta m;
  TEST_ASSERT_EQUAL(static_cast<int>(pad::PhoneParse::BadLen), static_cast<int>(pad::parsePhoneReport(nullptr, 8, s, m)));
}

static void test_xbox_report_has_xbox_source() {
  uint8_t r[16] = {0, 0x80, 0, 0x80, 0, 0x80, 0, 0x80, 0, 0, 0, 0, 0, 0x01, 0, 0};
  pad::State s;
  s.src = pad::Kind::Phone;
  TEST_ASSERT_TRUE(pad::parseXboxReport(r, 16, s));
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Xbox), static_cast<int>(s.src));
  TEST_ASSERT_EQUAL(pad::A, s.buttons);
}

static void test_keepalive_due_after_3s() {
  pad::Keepalive k;
  TEST_ASSERT_FALSE(k.due(10000));  // mai alimentato
  k.feed(1000);
  TEST_ASSERT_FALSE(k.due(3999));
  TEST_ASSERT_TRUE(k.due(4000));
  k.feed(3500);  // report di mantenimento
  TEST_ASSERT_FALSE(k.due(4000));
  k.stop();
  TEST_ASSERT_FALSE(k.due(100000));
}

// Orologio della tenuta: mai oltre l'ultimo report (un telefono sparito non arriva al HoldFire).
static void test_keepalive_cap() {
  pad::Keepalive k;
  TEST_ASSERT_EQUAL(50000, k.cap(50000));  // spento: nessun limite (Xbox)
  k.feed(1000);
  TEST_ASSERT_EQUAL(1000, k.cap(1000));
  TEST_ASSERT_EQUAL(1000, k.cap(1400));
  TEST_ASSERT_EQUAL(1000, k.cap(9000));
  k.feed(2000);
  TEST_ASSERT_EQUAL(2000, k.cap(2300));
}

static void test_keepalive_release_once() {
  pad::Keepalive k;
  pad::State last;
  memset(&last, 0, sizeof last);
  last.buttons = pad::A | pad::Up;
  last.src = pad::Kind::Phone;
  pad::State out;
  uint32_t at = 0;
  k.feed(1000);
  TEST_ASSERT_FALSE(pad::keepaliveRelease(k, last, 3999, out, at));
  TEST_ASSERT_TRUE(pad::keepaliveRelease(k, last, 4200, out, at));
  TEST_ASSERT_EQUAL(0, out.buttons);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(out.src));
  TEST_ASSERT_EQUAL(1000, at);  // istante d'arrivo del «rilasciato»: l'ultimo report
  TEST_ASSERT_FALSE(pad::keepaliveRelease(k, last, 9000, out, at));  // una volta sola
}

static void test_keepalive_nothing_to_release() {
  pad::Keepalive k;
  pad::State last;
  memset(&last, 0, sizeof last);
  pad::State out;
  uint32_t at = 0;
  k.feed(0);
  TEST_ASSERT_FALSE(pad::keepaliveRelease(k, last, 5000, out, at));
  TEST_ASSERT_FALSE(k.on());  // spento comunque
}

// Review Focus «tasti bloccati»: A premuto, poi il telefono sparisce. Col mappatore vero e l'orologio limitato:
// la tenuta finisce con HoldUp, mai HoldFire né Select.
static void test_keepalive_releases_hold_without_fire() {
  pad::MenuMapper map;
  pad::Keepalive k;
  pad::State s;
  memset(&s, 0, sizeof s);
  s.src = pad::Kind::Phone;
  s.buttons = pad::A;
  pad::MenuEvent ev[8];
  int fires = 0, selects = 0, ups = 0;
  int n = map.update(s, 1000, ev, 8);
  k.feed(1000);
  for (uint32_t now = 1000; now <= 8000; now += 50) {
    pad::State out;
    uint32_t at;
    if (pad::keepaliveRelease(k, s, now, out, at)) n = map.update(out, at, ev, 8);
    else n = map.tick(k.cap(now), ev, 8);
    for (int i = 0; i < n; i++) {
      fires += ev[i] == pad::MenuEvent::HoldFire;
      selects += ev[i] == pad::MenuEvent::Select;
      ups += ev[i] == pad::MenuEvent::HoldUp;
    }
  }
  TEST_ASSERT_EQUAL(0, fires);
  // Solo la pressione è arrivata: il «rilasciato» a +0 ms è indistinguibile da un tocco col rilascio perso.
  TEST_ASSERT_EQUAL(1, selects);
  TEST_ASSERT_EQUAL(1, ups);
}

// I1: A giù, un mantenimento, poi silenzio: il tenuto parte dalla pressione, ma l'orologio si ferma a ultimo report
// + 1,5 s, quindi mai HoldFire; il rilascio a 3 s chiude con HoldUp.
static void test_keepalive_then_silence_no_fire() {
  pad::MenuMapper map;
  pad::Keepalive k;
  pad::State s;
  memset(&s, 0, sizeof s);
  s.src = pad::Kind::Phone;
  s.buttons = pad::A;
  pad::MenuEvent ev[8];
  map.update(s, 1000, ev, 8);
  k.feed(1000);
  k.feed(2000);  // mantenimento con A ancora giù, poi il telefono sparisce
  int fires = 0, selects = 0, ups = 0;
  for (uint32_t now = 2000; now <= 9000; now += 50) {
    pad::State out;
    uint32_t at;
    int n;
    if (pad::keepaliveRelease(k, s, now, out, at)) n = map.update(out, at, ev, 8);
    else n = map.tick(k.cap(now), ev, 8);
    for (int i = 0; i < n; i++) {
      fires += ev[i] == pad::MenuEvent::HoldFire;
      selects += ev[i] == pad::MenuEvent::Select;
      ups += ev[i] == pad::MenuEvent::HoldUp;
    }
  }
  TEST_ASSERT_EQUAL(0, fires);
  TEST_ASSERT_EQUAL(0, selects);
  TEST_ASSERT_EQUAL(1, ups);
}

// I2: solo tick(cap), senza keepaliveRelease davanti: l'orologio limitato da solo non porta al HoldFire.
static void test_cap_alone_never_fires() {
  pad::MenuMapper map;
  pad::Keepalive k;
  pad::State s;
  memset(&s, 0, sizeof s);
  s.src = pad::Kind::Phone;
  s.buttons = pad::A;
  pad::MenuEvent ev[8];
  map.update(s, 1000, ev, 8);
  k.feed(1000);
  k.feed(2000);
  int fires = 0;
  for (uint32_t now = 2000; now <= 9000; now += 50) {
    const int n = map.tick(k.cap(now), ev, 8);
    for (int i = 0; i < n; i++) fires += ev[i] == pad::MenuEvent::HoldFire;
  }
  TEST_ASSERT_EQUAL(0, fires);
}

// Ultimo report a +0…+2999 ms dalla pressione, poi silenzio: in nessun caso HoldFire.
static void test_last_report_sweep_never_fires() {
  for (uint32_t d = 0; d < 3000; d++) {
    pad::MenuMapper map;
    pad::Keepalive k;
    pad::State s;
    memset(&s, 0, sizeof s);
    s.src = pad::Kind::Phone;
    s.buttons = pad::A;
    pad::MenuEvent ev[8];
    map.update(s, 1000, ev, 8);
    k.feed(1000);
    int fires = 0, ups = 0;
    for (uint32_t now = 1000; now <= 9000; now++) {
      if (d > 0 && now == 1000 + d) k.feed(now);  // ultimo report a +d
      pad::State out;
      uint32_t at;
      int n;
      if (pad::keepaliveRelease(k, s, now, out, at)) n = map.update(out, at, ev, 8);
      else n = map.tick(k.cap(now), ev, 8);
      for (int i = 0; i < n; i++) {
        fires += ev[i] == pad::MenuEvent::HoldFire;
        ups += ev[i] == pad::MenuEvent::HoldUp;
      }
    }
    TEST_ASSERT_EQUAL_MESSAGE(0, fires, "HoldFire con telefono sparito");
    TEST_ASSERT_EQUAL(1, ups);
  }
}

// Telefono vivo con un tasto giù: un report ogni 250 ms, HoldFire entro [3000, 3250] ms dalla pressione.
static void test_alive_250ms_fires_in_window() {
  pad::MenuMapper map;
  pad::Keepalive k;
  pad::State s;
  memset(&s, 0, sizeof s);
  s.src = pad::Kind::Phone;
  s.buttons = pad::A;
  pad::MenuEvent ev[8];
  map.update(s, 1000, ev, 8);
  k.feed(1000);
  int fires = 0;
  uint32_t fireAt = 0;
  for (uint32_t now = 1000; now <= 6000; now++) {
    if ((now - 1000) % 250 == 0) k.feed(now);
    const int n = map.tick(k.cap(now), ev, 8);
    for (int i = 0; i < n; i++)
      if (ev[i] == pad::MenuEvent::HoldFire) { fires++; fireAt = now; }
  }
  TEST_ASSERT_EQUAL(1, fires);
  TEST_ASSERT_GREATER_OR_EQUAL(4000, fireAt);
  TEST_ASSERT_LESS_OR_EQUAL(4250, fireAt);
}

// Il telefono vivo (mantenimento ogni 1 s) tiene A 3 s: HoldFire arriva come con l'Xbox.
static void test_alive_phone_hold_fires() {
  pad::MenuMapper map;
  pad::Keepalive k;
  pad::State s;
  memset(&s, 0, sizeof s);
  s.src = pad::Kind::Phone;
  s.buttons = pad::A;
  pad::MenuEvent ev[8];
  map.update(s, 1000, ev, 8);
  k.feed(1000);
  int fires = 0;
  for (uint32_t now = 1000; now <= 4500; now += 50) {
    if ((now - 1000) % 1000 == 0) k.feed(now);  // report uguale: alimenta e basta
    const int n = map.tick(k.cap(now), ev, 8);
    for (int i = 0; i < n; i++) fires += ev[i] == pad::MenuEvent::HoldFire;
  }
  TEST_ASSERT_EQUAL(1, fires);
}

// App in secondo piano (flags bit0 = 0): anche con bit accesi è «tutto rilasciato».
static void test_background_flag_releases() {
  const uint8_t r[8] = {1, 9, 0x03, 0x30, 50, -50 & 0xff, 0x00, 0};
  pad::State s;
  pad::PhoneMeta m;
  TEST_ASSERT_EQUAL(static_cast<int>(pad::PhoneParse::Ok), static_cast<int>(pad::parsePhoneReport(r, 8, s, m)));
  TEST_ASSERT_EQUAL(0, s.buttons);
  TEST_ASSERT_EQUAL(0, s.lx);
  TEST_ASSERT_FALSE(m.fg);
}

// F7: secondo piano (flags bit0 = 0) e poi silenzio 5 s, o 10 s senza report: la scheda scollega il telefono.
static void test_phone_idle_rules() {
  pad::PhoneIdle p;
  TEST_ASSERT_FALSE(p.due(100000));  // mai collegato
  p.start(1000);
  TEST_ASSERT_FALSE(p.due(10999));
  TEST_ASSERT_TRUE(p.due(11000));    // 10 s senza report
  p.report(11000, true);             // di nuovo vivo
  TEST_ASSERT_FALSE(p.due(15000));
  p.report(15000, false);            // l'app va in secondo piano: «tutto rilasciato»
  TEST_ASSERT_FALSE(p.due(19999));
  TEST_ASSERT_TRUE(p.due(20000));    // 5 s dopo
  p.report(19000, true);             // tornata in primo piano prima dei 5 s
  TEST_ASSERT_FALSE(p.due(28999));
  p.stop();
  TEST_ASSERT_FALSE(p.due(100000));
}

// Riporto 5: il rilascio per silenzio passa da mapPolled come reset: A toccato e poi telefono muto non dà mai Select
// (con update sì, vedi test_keepalive_releases_hold_without_fire), qualunque sia l'istante della pressione.
static void test_keepalive_release_never_select() {
  for (uint32_t pressAt = 1000; pressAt <= 1700; pressAt += 50) {
    pad::MenuMapper map;
    pad::Keepalive k;
    pad::State s;
    memset(&s, 0, sizeof s);
    s.src = pad::Kind::Phone;
    s.buttons = pad::A;
    pad::MenuEvent ev[8];
    int selects = 0, ups = 0, downs = 0;
    int n = pad::mapPolled(map, s, pressAt, false, ev, 8);
    for (int i = 0; i < n; i++) downs += ev[i] == pad::MenuEvent::HoldDown;
    k.feed(pressAt);
    for (uint32_t now = pressAt; now <= pressAt + 5000; now += 50) {
      pad::State out;
      uint32_t at;
      if (pad::keepaliveRelease(k, s, now, out, at)) n = pad::mapPolled(map, out, at, true, ev, 8);
      else n = map.tick(k.cap(now), ev, 8);
      for (int i = 0; i < n; i++) {
        selects += ev[i] == pad::MenuEvent::Select;
        ups += ev[i] == pad::MenuEvent::HoldUp;
        TEST_ASSERT_TRUE(ev[i] != pad::MenuEvent::HoldFire);
      }
    }
    TEST_ASSERT_EQUAL(1, downs);
    TEST_ASSERT_EQUAL(0, selects);
    TEST_ASSERT_EQUAL(1, ups);
    TEST_ASSERT_FALSE(map.holding());
  }
}

// mapPolled con un report vero è update: il tocco di A resta un Select.
static void test_map_polled_report_is_update() {
  pad::MenuMapper map;
  pad::State s;
  memset(&s, 0, sizeof s);
  s.src = pad::Kind::Phone;
  s.buttons = pad::A;
  pad::MenuEvent ev[8];
  pad::mapPolled(map, s, 1000, false, ev, 8);
  s.buttons = 0;
  const int n = pad::mapPolled(map, s, 1200, false, ev, 8);
  int selects = 0;
  for (int i = 0; i < n; i++) selects += ev[i] == pad::MenuEvent::Select;
  TEST_ASSERT_EQUAL(1, selects);
}

// Revisione finale I2: il report di secondo piano (flags bit0 = 0, l'app diventa inattiva) è un rilascio forzato:
// passa da mapPolled come reset, A toccato non sceglie mai la voce né fa scattare il tenuto (spec §2.1).
static void test_background_report_is_forced_release() {
  const uint8_t down[8] = {1, 1, 0x01, 0x00, 0, 0, pad::kPhoneFlagForeground, 0};  // A giù, primo piano
  const uint8_t bg[8] = {1, 2, 0x00, 0x00, 0, 0, 0x00, 0};                         // tutto rilasciato, secondo piano
  for (uint32_t after : {300u, 3100u}) {
    pad::MenuMapper map;
    pad::MenuEvent ev[8];
    pad::State s;
    pad::PhoneMeta m;
    TEST_ASSERT_EQUAL(static_cast<int>(pad::PhoneParse::Ok), static_cast<int>(pad::parsePhoneReport(down, 8, s, m)));
    TEST_ASSERT_FALSE(pad::phoneForcedRelease(m));  // primo piano: report vero
    int n = pad::mapPolled(map, s, 1000, pad::phoneForcedRelease(m), ev, 8);
    int downs = 0;
    for (int i = 0; i < n; i++) downs += ev[i] == pad::MenuEvent::HoldDown;
    TEST_ASSERT_EQUAL(1, downs);
    TEST_ASSERT_EQUAL(static_cast<int>(pad::PhoneParse::Ok), static_cast<int>(pad::parsePhoneReport(bg, 8, s, m)));
    TEST_ASSERT_TRUE(pad::phoneForcedRelease(m));
    n = pad::mapPolled(map, s, 1000 + after, pad::phoneForcedRelease(m), ev, 8);
    int ups = 0;
    for (int i = 0; i < n; i++) {
      TEST_ASSERT_TRUE(ev[i] != pad::MenuEvent::Select);
      TEST_ASSERT_TRUE(ev[i] != pad::MenuEvent::HoldFire);
      ups += ev[i] == pad::MenuEvent::HoldUp;
    }
    TEST_ASSERT_EQUAL(1, ups);
    TEST_ASSERT_FALSE(map.holding());
    n = map.tick(1000 + after + 5000, ev, 8);  // dopo: niente in sospeso
    for (int i = 0; i < n; i++) TEST_ASSERT_TRUE(ev[i] != pad::MenuEvent::HoldFire && ev[i] != pad::MenuEvent::Select);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_vectors_loaded);
  RUN_TEST(test_vector_bits_match_button_enum);
  RUN_TEST(test_vectors_decode);
  RUN_TEST(test_null_report_rejected);
  RUN_TEST(test_xbox_report_has_xbox_source);
  RUN_TEST(test_keepalive_due_after_3s);
  RUN_TEST(test_keepalive_cap);
  RUN_TEST(test_keepalive_release_once);
  RUN_TEST(test_keepalive_nothing_to_release);
  RUN_TEST(test_keepalive_releases_hold_without_fire);
  RUN_TEST(test_keepalive_then_silence_no_fire);
  RUN_TEST(test_cap_alone_never_fires);
  RUN_TEST(test_last_report_sweep_never_fires);
  RUN_TEST(test_alive_250ms_fires_in_window);
  RUN_TEST(test_alive_phone_hold_fires);
  RUN_TEST(test_background_flag_releases);
  RUN_TEST(test_phone_idle_rules);
  RUN_TEST(test_keepalive_release_never_select);
  RUN_TEST(test_map_polled_report_is_update);
  RUN_TEST(test_background_report_is_forced_release);
  return UNITY_END();
}
