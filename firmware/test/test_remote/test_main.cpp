// Impostazioni remote (core/remote_settings, Ui::applyRemote): set, full-refresh, settings, ack e il
// salvataggio in NVS. Gli esempi si leggono da protocol/examples/ (la stessa fonte del servizio):
// pio test gira con la cartella firmware/ come cwd.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>

#include "core/apply_state.h"
#include "core/remote_settings.h"
#include "core/strbuf.h"
#include "core/ui.h"
#include "core/workday.h"
#include "gfx/text.h"

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

bool utf8Valid(const char *s) {
  const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
  while (*p) {
    int n = *p < 0x80 ? 0 : (*p >> 5) == 6 ? 1 : (*p >> 4) == 14 ? 2 : (*p >> 3) == 30 ? 3 : -1;
    if (n < 0) return false;
    p++;
    for (int k = 0; k < n; k++, p++)
      if ((*p >> 6) != 2) return false;
  }
  return true;
}

JsonDocument doc;
RemoteCmd cmd;

// parseMessage (lo stesso filtro del task di rete) + decodeSet.
bool set(const std::string &json) {
  doc.clear();
  if (!parseMessage(json.data(), json.size(), doc)) return false;
  return decodeSet(doc.as<JsonVariantConst>(), cmd);
}
std::string setJson(const std::string &settings, const char *id = "c1") {
  return std::string("{\"t\":\"set\",\"v\":1,\"id\":\"") + id + "\",\"settings\":" + settings + "}";
}

const Ms kNow = 5000000;
const int64_t kLocal = 1791714600000LL;  // 2026-10-11 10:30 come ora locale (WallClock.localMs)

struct Bench {
  Ui ui;
  Ms now;
  Bench() : ui(kHeltec213), now(kNow) {
    ui.begin(now, wc());
    ui.goLive();
    now += 2601;
    ui.tick(now, wc());
  }
  WallClock wc() const { return WallClock{kLocal + (now - kNow)}; }
  RefreshKind apply(const std::string &settings) {
    TEST_ASSERT_TRUE_MESSAGE(set(setJson(settings)), settings.c_str());
    TEST_ASSERT_TRUE_MESSAGE(cmd.ok, cmd.error);
    return ui.applyRemote(cmd.patch, now, wc());
  }
};

}  // namespace

// ---------------------------------------------------------------- esempi del protocollo

static void test_example_set() {
  TEST_ASSERT_TRUE(set(readFile("../protocol/examples/set.json")));
  TEST_ASSERT_EQUAL(MsgType::Set, msgType(doc.as<JsonVariantConst>()));
  TEST_ASSERT_TRUE(cmd.ok);
  TEST_ASSERT_FALSE(cmd.fullRefresh);
  TEST_ASSERT_EQUAL_STRING("c7", cmd.id);
  TEST_ASSERT_TRUE(cmd.patch.on(RemoteKey::HomeStyle));
  TEST_ASSERT_TRUE(cmd.patch.on(RemoteKey::PermOn));
  TEST_ASSERT_FALSE(cmd.patch.on(RemoteKey::Anim));
  TEST_ASSERT_FALSE(cmd.patch.on(RemoteKey::Mute));
  TEST_ASSERT_EQUAL(HomeStyle::Righe, cmd.patch.v.homeStyle);
  TEST_ASSERT_FALSE(cmd.patch.v.permOn);
}

static void test_example_full_refresh() {
  const std::string js = readFile("../protocol/examples/full-refresh.json");
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_EQUAL(MsgType::FullRefresh, msgType(doc.as<JsonVariantConst>()));
  TEST_ASSERT_TRUE(decodeFullRefresh(doc.as<JsonVariantConst>(), cmd));
  TEST_ASSERT_TRUE(cmd.fullRefresh);
  TEST_ASSERT_EQUAL_STRING("c9", cmd.id);
  // un set non è un full-refresh e viceversa
  TEST_ASSERT_FALSE(decodeSet(doc.as<JsonVariantConst>(), cmd));
  TEST_ASSERT_FALSE(set("{\"t\":\"full-refresh\",\"v\":1}"));
}

static void test_example_settings_encoding() {
  // Valori predefiniti del modello = protocol/examples/settings-giornata.json, byte per byte (0.9.0: con workStart e
  // workEnd, sempre tutti e due).
  static Model m;
  newModel(m, kNow, WallClock{kLocal});
  char out[320];
  const size_t n = encodeSettings(remoteValues(m.set, kNow), out, sizeof out);
  TEST_ASSERT_EQUAL(strlen(out), n);
  // 0.10.0: in più "game" (gioco della mascotte) dopo workEnd. protocol/examples lo aggiorna chi possiede il protocollo:
  // finché l'esempio non ha "game" si confronta con l'esempio più la chiave nuova.
  std::string want = readFile("../protocol/examples/settings-giornata.json");
  if (want.find("\"game\"") == std::string::npos) {
    const size_t at = want.find("},\"remote\"");
    TEST_ASSERT_TRUE(at != std::string::npos);
    want.insert(at, ",\"game\":false");
  }
  TEST_ASSERT_EQUAL_STRING(want.c_str(), out);
  // Tutti gli altri valori, nello stesso ordine delle chiavi.
  RemoteValues v = {HomeStyle::Una, AnimMode::Spente, SpeedMode::Massima, true, true, true, false, false, false, MuteKind::Night, true, 5, 23 * 60 + 59, true};
  encodeSettings(v, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(
      "{\"t\":\"settings\",\"v\":1,\"settings\":{\"homeStyle\":\"una\",\"anim\":\"spente\",\"speed\":\"massima\",\"invert\":true,"
      "\"rot\":true,\"night\":true,\"permOn\":false,\"sound\":false,\"led\":false,\"mute\":\"night\",\"save\":true,"
      "\"workStart\":\"00:05\",\"workEnd\":\"23:59\",\"game\":true},\"remote\":true}",
      out);
  // M5: senza auth-ok il display non accetta set (remote: false).
  encodeSettings(v, out, sizeof out, false);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"remote\":false"));
  v.mute = MuteKind::Hour;
  v.homeStyle = HomeStyle::Contatori;
  v.anim = AnimMode::Raffiche;
  encodeSettings(v, out, sizeof out);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"mute\":\"1h\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"homeStyle\":\"contatori\""));
  TEST_ASSERT_NOT_NULL(strstr(out, "\"anim\":\"raffiche\""));
  // Buffer corto: niente JSON a metà.
  char small[40];
  TEST_ASSERT_EQUAL(0, encodeSettings(v, small, sizeof small));
  TEST_ASSERT_EQUAL_STRING("", small);
}

static void test_example_ack_encoding() {
  char out[200];
  encodeAck("c7", true, nullptr, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(readFile("../protocol/examples/ack.json").c_str(), out);
  encodeAck("c8", false, kPermOnRemoteError, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(readFile("../protocol/examples/ack-error.json").c_str(), out);
  encodeAck("c9", true, "ignorato con ok", out, sizeof out);  // con ok: true niente error (schema)
  TEST_ASSERT_NULL(strstr(out, "error"));
  encodeAck("c10", false, "", out, sizeof out);  // error vuoto vietato dallo schema (minLength 1)
  TEST_ASSERT_NOT_NULL(strstr(out, "\"error\":\"rifiutato\""));
  encodeAck("a\"b", true, nullptr, out, sizeof out);  // id con virgolette: JSON valido
  TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":\"a\\\"b\""));
}

// ---------------------------------------------------------------- casi limite del set

static void test_set_permon_true_always_refused() {
  TEST_ASSERT_TRUE(set(setJson("{\"permOn\":true}")));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_EQUAL_STRING(kPermOnRemoteError, cmd.error);
  TEST_ASSERT_EQUAL(0, cmd.patch.has);
  // insieme a valori validi: niente applicato, stesso motivo
  TEST_ASSERT_TRUE(set(setJson("{\"homeStyle\":\"righe\",\"permOn\":true,\"invert\":true}")));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_EQUAL_STRING(kPermOnRemoteError, cmd.error);
  TEST_ASSERT_EQUAL(0, cmd.patch.has);
  // anche se ci sono altri errori: la regola dei permessi ha sempre lo stesso motivo
  TEST_ASSERT_TRUE(set(setJson("{\"boh\":1,\"permOn\":true}")));
  TEST_ASSERT_EQUAL_STRING(kPermOnRemoteError, cmd.error);
  // permOn: false si applica
  TEST_ASSERT_TRUE(set(setJson("{\"permOn\":false}")));
  TEST_ASSERT_TRUE(cmd.ok);
  TEST_ASSERT_TRUE(cmd.patch.on(RemoteKey::PermOn));
}

static void test_set_unknown_key_refused() {
  TEST_ASSERT_TRUE(set(setJson("{\"homeStyle\":\"righe\",\"luminosita\":3}")));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_EQUAL_STRING("chiave sconosciuta: luminosita", cmd.error);
  TEST_ASSERT_EQUAL(0, cmd.patch.has);  // la chiave valida prima non resta applicata
  TEST_ASSERT_TRUE(set(setJson("{\"hookDirect\":true}")));  // c'è nel modello ma non nel protocollo
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_TRUE(set(setJson("{\"save\":true}")));  // Risparmio energetico: sì, anche da remoto
  TEST_ASSERT_TRUE(cmd.ok);
  TEST_ASSERT_TRUE(cmd.patch.on(RemoteKey::Save));
  TEST_ASSERT_TRUE(cmd.patch.v.save);
  TEST_ASSERT_TRUE(set(setJson("{\"save\":1}")));  // solo booleani
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_TRUE(set(setJson("{\"HomeStyle\":\"righe\"}")));  // le chiavi distinguono le maiuscole
  TEST_ASSERT_FALSE(cmd.ok);
}

static void test_set_wrong_types_refused() {
  const char *bad[][2] = {
      {"{\"homeStyle\":3}", "homeStyle: valore non valido"},
      {"{\"homeStyle\":\"Righe\"}", "homeStyle: valore non valido"},
      {"{\"anim\":\"veloce\"}", "anim: valore non valido"},
      {"{\"speed\":null}", "speed: valore non valido"},
      {"{\"invert\":1}", "invert: valore non valido"},
      {"{\"rot\":\"true\"}", "rot: valore non valido"},
      {"{\"night\":[true]}", "night: valore non valido"},
      {"{\"sound\":{}}", "sound: valore non valido"},
      {"{\"led\":0}", "led: valore non valido"},
      {"{\"permOn\":\"false\"}", "permOn: valore non valido"},
      {"{\"mute\":\"2h\"}", "mute: valore non valido"},
      {"{\"mute\":true}", "mute: valore non valido"},
      {"{\"mute\":\"Night\"}", "mute: valore non valido"},
      {"{\"invert\":true,\"mute\":60}", "mute: valore non valido"},
  };
  for (auto &b : bad) {
    TEST_ASSERT_TRUE_MESSAGE(set(setJson(b[0])), b[0]);
    TEST_ASSERT_FALSE_MESSAGE(cmd.ok, b[0]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(b[1], cmd.error, b[0]);
    TEST_ASSERT_EQUAL_MESSAGE(0, cmd.patch.has, b[0]);
  }
}

static void test_set_settings_missing_or_empty() {
  TEST_ASSERT_TRUE(set("{\"t\":\"set\",\"v\":1,\"id\":\"c1\"}"));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_EQUAL_STRING("settings: manca o non è un oggetto", cmd.error);
  TEST_ASSERT_TRUE(set(setJson("[\"homeStyle\"]")));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_TRUE(set(setJson("\"righe\"")));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_TRUE(set(setJson("{}")));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_EQUAL_STRING("settings: nessuna impostazione da cambiare", cmd.error);
}

static void test_set_id_rules() {
  // senza id valido non si può rispondere: decodeSet false (il task di rete non manda ack)
  TEST_ASSERT_FALSE(set("{\"t\":\"set\",\"v\":1,\"settings\":{\"rot\":true}}"));
  TEST_ASSERT_FALSE(set(setJson("{\"rot\":true}", "")));
  TEST_ASSERT_FALSE(set("{\"t\":\"set\",\"v\":1,\"id\":7,\"settings\":{\"rot\":true}}"));
  const std::string id32(32, 'x'), id33(33, 'x');
  TEST_ASSERT_TRUE(set(setJson("{\"rot\":true}", id32.c_str())));
  TEST_ASSERT_EQUAL_STRING(id32.c_str(), cmd.id);
  TEST_ASSERT_FALSE(set(setJson("{\"rot\":true}", id33.c_str())));
  // un id troppo lungo con un valore sbagliato: nemmeno l'ack d'errore (non lo si può ripetere)
  TEST_ASSERT_FALSE(set(setJson("{\"permOn\":true}", id33.c_str())));
  // id ripetuto nell'ack, al limite dei 32 caratteri
  char out[200];
  encodeAck(id32.c_str(), false, kPermOnRemoteError, out, sizeof out);
  TEST_ASSERT_NOT_NULL(strstr(out, ("\"id\":\"" + id32 + "\"").c_str()));
  // versione sbagliata: non è un set
  TEST_ASSERT_FALSE(set("{\"t\":\"set\",\"v\":2,\"id\":\"c1\",\"settings\":{\"rot\":true}}"));
}

static void test_ack_error_max_95_bytes() {
  // chiave sconosciuta lunghissima e con accenti: l'errore resta ≤ 95 byte e UTF-8 valido
  std::string key;
  for (int k = 0; k < 60; k++) key += "è";
  TEST_ASSERT_TRUE(set(setJson("{\"" + key + "\":1}")));
  TEST_ASSERT_FALSE(cmd.ok);
  TEST_ASSERT_TRUE(strlen(cmd.error) <= 95);
  TEST_ASSERT_TRUE(utf8Valid(cmd.error));
  TEST_ASSERT_EQUAL(0, strncmp(cmd.error, "chiave sconosciuta: ", 20));
  char out[200];
  std::string longErr(300, 'a');
  encodeAck("c1", false, longErr.c_str(), out, sizeof out);
  doc.clear();
  TEST_ASSERT_FALSE(deserializeJson(doc, out));
  TEST_ASSERT_EQUAL(95, (int)strlen(doc["error"].as<const char *>()));
}

static void test_set_all_keys() {
  TEST_ASSERT_TRUE(set(setJson("{\"homeStyle\":\"una\",\"anim\":\"continue\",\"speed\":\"massima\",\"invert\":true,\"rot\":false,"
                               "\"night\":true,\"permOn\":false,\"sound\":false,\"led\":true,\"mute\":\"night\"}")));
  TEST_ASSERT_TRUE(cmd.ok);
  TEST_ASSERT_EQUAL_HEX16(0x3FF, cmd.patch.has);
  const RemoteValues &v = cmd.patch.v;
  TEST_ASSERT_EQUAL(HomeStyle::Una, v.homeStyle);
  TEST_ASSERT_EQUAL(AnimMode::Continue, v.anim);
  TEST_ASSERT_EQUAL(SpeedMode::Massima, v.speed);
  TEST_ASSERT_TRUE(v.invert);
  TEST_ASSERT_FALSE(v.rot);
  TEST_ASSERT_TRUE(v.night);
  TEST_ASSERT_FALSE(v.sound);
  TEST_ASSERT_TRUE(v.led);
  TEST_ASSERT_EQUAL(MuteKind::Night, v.mute);
  // corrispondenze del brief: goccia/righe/contatori/una = 0–3, auto/raffiche/continue/spente = 0–3
  const char *homes[] = {"goccia", "righe", "contatori", "una"};
  const char *anims[] = {"auto", "raffiche", "continue", "spente"};
  for (int k = 0; k < 4; k++) {
    TEST_ASSERT_TRUE(set(setJson(std::string("{\"homeStyle\":\"") + homes[k] + "\",\"anim\":\"" + anims[k] + "\"}")));
    TEST_ASSERT_EQUAL(k, static_cast<int>(cmd.patch.v.homeStyle));
    TEST_ASSERT_EQUAL(k, static_cast<int>(cmd.patch.v.anim));
  }
  const char *mutes[] = {"off", "1h", "night"};
  for (int k = 0; k < 3; k++) {
    TEST_ASSERT_TRUE(set(setJson(std::string("{\"mute\":\"") + mutes[k] + "\"}")));
    TEST_ASSERT_EQUAL(k, static_cast<int>(cmd.patch.v.mute));  // None, Hour, Night
  }
}

// ---------------------------------------------------------------- applicazione nel modello

static void test_apply_refresh_like_menu() {
  static Bench b;
  // Inverti e ruota: quick (come doAct); il resto: partial.
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.apply("{\"invert\":true}"));
  TEST_ASSERT_TRUE(b.ui.model().set.invert);
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.apply("{\"rot\":true}"));
  TEST_ASSERT_TRUE(b.ui.model().set.rot);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.apply("{\"homeStyle\":\"contatori\"}"));
  TEST_ASSERT_EQUAL(HomeStyle::Contatori, b.ui.model().set.homeStyle);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.apply("{\"anim\":\"spente\",\"speed\":\"massima\",\"night\":true,\"sound\":false,\"led\":false}"));
  TEST_ASSERT_EQUAL(RefreshKind::Quick, b.apply("{\"anim\":\"auto\",\"invert\":false}"));  // il più forte vince
  const Settings &st = b.ui.model().set;
  TEST_ASSERT_EQUAL(AnimMode::Auto, st.anim);
  TEST_ASSERT_EQUAL(SpeedMode::Massima, st.speed);
  TEST_ASSERT_TRUE(st.night);
  TEST_ASSERT_FALSE(st.sound);
  TEST_ASSERT_FALSE(st.led);
  TEST_ASSERT_FALSE(st.invert);
  TEST_ASSERT_TRUE(st.rot);  // le chiavi assenti non cambiano
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.apply("{\"permOn\":false}"));
  TEST_ASSERT_FALSE(st.permOn);
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.apply("{\"save\":true}"));
  TEST_ASSERT_TRUE(st.save);
  TEST_ASSERT_TRUE(powerSave(b.ui.model()));
  TEST_ASSERT_TRUE(remoteValues(st, b.now).save);
  // Stesso refresh della voce del menu: Display › Inverti colori.
  static Bench menu;
  menu.ui.act(Action::Menu, menu.now, menu.wc());
  for (int k = 0; k < 4; k++) menu.ui.act(Action::Next, menu.now, menu.wc());
  menu.ui.act(Action::Ok, menu.now, menu.wc());  // Display
  for (int k = 0; k < 4; k++) menu.ui.act(Action::Next, menu.now, menu.wc());  // dopo Risparmio energetico
  TEST_ASSERT_EQUAL(RefreshKind::Quick, menu.ui.act(Action::Ok, menu.now, menu.wc()));
  TEST_ASSERT_TRUE(menu.ui.model().set.invert);
}

static void test_apply_mute() {
  static Bench b;
  const Settings &st = b.ui.model().set;
  b.apply("{\"mute\":\"1h\"}");
  TEST_ASSERT_EQUAL(MuteKind::Hour, st.muteKind);
  TEST_ASSERT_EQUAL_INT64(b.now + kMuteMs, st.mute);  // un'ora da adesso
  TEST_ASSERT_TRUE(muted(b.ui.model(), b.now));
  TEST_ASSERT_EQUAL(MuteKind::Hour, remoteValues(st, b.now).mute);
  // night alle 10:30 di ora locale: fino alle 8 di domani (21 h 30)
  b.apply("{\"mute\":\"night\"}");
  TEST_ASSERT_EQUAL(MuteKind::Night, st.muteKind);
  TEST_ASSERT_EQUAL_INT64(b.now + (21 * 60 + 30) * 60000LL - 2601, st.mute);
  b.apply("{\"mute\":\"off\"}");
  TEST_ASSERT_EQUAL(MuteKind::None, st.muteKind);
  TEST_ASSERT_EQUAL_INT64(0, st.mute);
  TEST_ASSERT_FALSE(muted(b.ui.model(), b.now));
  // prima delle 8: fino alle 8 di oggi
  static Bench early;
  early.now = kNow - 4 * 3600000LL;  // 06:30 locali (stesso Ui, solo l'ora cambia)
  early.apply("{\"mute\":\"night\"}");
  TEST_ASSERT_EQUAL_INT64(early.now + 90 * 60000LL, early.ui.model().set.mute);
}

static void test_mute_expiry_reported_off() {
  static Bench b;
  b.apply("{\"mute\":\"1h\"}");
  TEST_ASSERT_EQUAL(MuteKind::Hour, remoteValues(b.ui.model().set, b.now).mute);
  b.now += kMuteMs;  // ultimo istante: ancora silenziato? no, mute > now è falso
  TEST_ASSERT_EQUAL(MuteKind::None, remoteValues(b.ui.model().set, b.now).mute);
  b.now += 1;
  b.ui.tick(b.now, b.wc());
  TEST_ASSERT_EQUAL(MuteKind::None, b.ui.model().set.muteKind);  // tick lo toglie: il loop manda settings
}

static void test_apply_permoff_closes_open_permission() {
  // Fino a M5 un permesso non si apre con la rete (manca l'abbinamento): qui senza rete, come nel finto.
  static Ui ui(kHeltec213);
  Ms now = kNow;
  ui.begin(now, WallClock{kLocal});
  now += 2700;
  ui.tick(now, WallClock{kLocal});
  ui.act(Action::Ok, now, WallClock{kLocal});  // Dettaglio della sessione in PERMESSO
  ui.act(Action::Ok, now, WallClock{kLocal});  // Permesso
  TEST_ASSERT_EQUAL(ScreenId::Perm, top(ui.model()).id);
  SettingsPatch p = {};
  p.has = 1u << static_cast<int>(RemoteKey::PermOn);
  p.v.permOn = false;
  TEST_ASSERT_EQUAL(RefreshKind::Quick, ui.applyRemote(p, now, WallClock{kLocal}));
  TEST_ASSERT_EQUAL(ScreenId::Detail, top(ui.model()).id);
  TEST_ASSERT_FALSE(ui.model().set.permOn);
}

// ---------------------------------------------------------------- NVS

static void test_nvs_roundtrip() {
  Settings a = {};
  a.homeStyle = HomeStyle::Una;
  a.anim = AnimMode::Raffiche;
  a.speed = SpeedMode::Massima;
  a.invert = true;
  a.night = true;
  a.permOn = false;
  a.sound = true;
  a.save = true;
  a.hookDirect = true;
  a.mute = 123;  // il tempo monotono non si salva
  a.muteKind = MuteKind::Hour;
  const StoredMute sm = {MuteKind::Hour, 1791714600000LL};
  uint8_t blob[kStoredLen];
  TEST_ASSERT_EQUAL(kStoredLen, packSettings(a, sm, blob, sizeof blob));
  Settings b = {};
  b.permOn = true;
  StoredMute got;
  TEST_ASSERT_TRUE(unpackSettings(blob, sizeof blob, b, got));
  TEST_ASSERT_EQUAL(HomeStyle::Una, b.homeStyle);
  TEST_ASSERT_EQUAL(AnimMode::Raffiche, b.anim);
  TEST_ASSERT_EQUAL(SpeedMode::Massima, b.speed);
  TEST_ASSERT_TRUE(b.invert);
  TEST_ASSERT_FALSE(b.rot);
  TEST_ASSERT_TRUE(b.night);
  TEST_ASSERT_FALSE(b.permOn);
  TEST_ASSERT_TRUE(b.sound);
  TEST_ASSERT_FALSE(b.led);
  TEST_ASSERT_TRUE(b.save);
  TEST_ASSERT_TRUE(b.hookDirect);
  TEST_ASSERT_EQUAL_INT64(0, b.mute);  // il silenzio torna solo con restoreMute
  TEST_ASSERT_EQUAL(MuteKind::None, b.muteKind);
  TEST_ASSERT_EQUAL(MuteKind::Hour, got.kind);
  TEST_ASSERT_EQUAL_INT64(1791714600000LL, got.endEpochMs);
}

static void test_nvs_rejects_bad_blobs() {
  Settings a = {};
  uint8_t blob[kStoredLen];
  packSettings(a, StoredMute{MuteKind::None, 0}, blob, sizeof blob);
  Settings b = {};
  b.homeStyle = HomeStyle::Righe;  // resta com'è se il blocco non va
  StoredMute sm;
  uint8_t bad[kStoredLen];
  memcpy(bad, blob, sizeof bad);
  bad[4] ^= 1;  // somma di controllo sbagliata
  TEST_ASSERT_FALSE(unpackSettings(bad, sizeof bad, b, sm));
  memcpy(bad, blob, sizeof bad);
  bad[0] = 2;  // altra versione
  TEST_ASSERT_FALSE(unpackSettings(bad, sizeof bad, b, sm));
  TEST_ASSERT_FALSE(unpackSettings(blob, kStoredLen - 1, b, sm));  // corto
  TEST_ASSERT_FALSE(unpackSettings(nullptr, kStoredLen, b, sm));
  // valore fuori elenco con somma giusta
  a.homeStyle = static_cast<HomeStyle>(7);
  packSettings(a, StoredMute{MuteKind::None, 0}, bad, sizeof bad);
  TEST_ASSERT_FALSE(unpackSettings(bad, sizeof bad, b, sm));
  TEST_ASSERT_EQUAL(HomeStyle::Righe, b.homeStyle);
  TEST_ASSERT_EQUAL(0, packSettings(a, StoredMute{MuteKind::None, 0}, bad, kStoredLen - 1));
}

static void test_nvs_restore_mute() {
  const int64_t nowEpoch = 1791714600000LL;
  Settings s = {};
  // 1h salvato 20 minuti fa: ne restano 40
  TEST_ASSERT_TRUE(restoreMute(s, StoredMute{MuteKind::Hour, nowEpoch + 40 * 60000LL}, nowEpoch, kNow));
  TEST_ASSERT_EQUAL(MuteKind::Hour, s.muteKind);
  TEST_ASSERT_EQUAL_INT64(kNow + 40 * 60000LL, s.mute);
  // scaduto (anche di un millisecondo): non si ripristina
  s = {};
  TEST_ASSERT_FALSE(restoreMute(s, StoredMute{MuteKind::Hour, nowEpoch - 1}, nowEpoch, kNow));
  TEST_ASSERT_FALSE(restoreMute(s, StoredMute{MuteKind::Hour, nowEpoch}, nowEpoch, kNow));
  TEST_ASSERT_EQUAL(MuteKind::None, s.muteKind);
  TEST_ASSERT_EQUAL_INT64(0, s.mute);
  // fine sconosciuta (silenzio iniziato senza ora valida) o più di 24 h: orologio sbagliato
  TEST_ASSERT_FALSE(restoreMute(s, StoredMute{MuteKind::Hour, 0}, nowEpoch, kNow));
  TEST_ASSERT_FALSE(restoreMute(s, StoredMute{MuteKind::Night, nowEpoch + kMaxRestoreMs + 1}, nowEpoch, kNow));
  TEST_ASSERT_TRUE(restoreMute(s, StoredMute{MuteKind::Night, nowEpoch + 9 * 3600000LL}, nowEpoch, kNow));
  TEST_ASSERT_EQUAL(MuteKind::Night, s.muteKind);
  TEST_ASSERT_FALSE(restoreMute(s, StoredMute{MuteKind::None, nowEpoch + 1000}, nowEpoch, kNow));
}

// ---------------------------------------------------------------- Archivia

namespace {
struct Live {
  Ui ui;
  Ms now;
  NetRequest reqs[16];
  int nReqs;
  StateMsg msg;
  Live() : ui(kHeltec213), now(kNow), nReqs(0) {
    ui.setNetListener(&Live::onNet, this);
    ui.begin(now, wc());
    ui.goLive();
    now += 2601;
    ui.tick(now, wc());
    send();
  }
  WallClock wc() const { return WallClock{kLocal + (now - kNow)}; }
  static void onNet(const NetRequest &r, void *ctx) {
    Live *b = static_cast<Live *>(ctx);
    if (b->nReqs < 16) b->reqs[b->nReqs++] = r;
  }
  // state di esempio (a1 working, b2 permission, c3 working, d4 done); bump cambia l'impronta
  void send(int bump = 0, SessionState d4 = SessionState::Done, bool withD4 = true) {
    const std::string js = readFile("../protocol/examples/state.json");
    doc.clear();
    TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
    TEST_ASSERT_TRUE(decodeState(doc.as<JsonVariantConst>(), msg));
    msg.today.tasks += bump;
    msg.sessions[3].state = d4;
    if (!withD4) msg.nSessions = 3;
    ui.netState(msg, now);
  }
  int find(const char *id) const {
    for (int k = 0; k < ui.model().nSessions; k++)
      if (streq(ui.model().sessions[k].id, id)) return k;
    return -1;
  }
  RefreshKind archiveD4() {
    ui.model().ui.sel = find("d4");
    ui.model().ui.selUntil = now + 15000;
    ui.act(Action::Ok, now, wc());  // Dettaglio
    TEST_ASSERT_EQUAL(ScreenId::Detail, top(ui.model()).id);
    return ui.act(Action::Ok, now, wc());  // 2×: Archivia
  }
};
}  // namespace

static void test_archive_only_stopped_sessions() {
  // Senza rete (finto): docs-site FINITA si archivia, le altre no.
  static Ui ui(kHeltec213);
  Ms now = kNow;
  const WallClock wc{kLocal};
  ui.begin(now, wc);
  now += 2700;
  ui.tick(now, wc);
  Model &m = ui.model();
  TEST_ASSERT_EQUAL(4, m.nSessions);
  ui.act(Action::Next, now, wc);  // firmware-esp32 (LAVORA)
  ui.act(Action::Ok, now, wc);
  TEST_ASSERT_EQUAL(ScreenId::Detail, top(m).id);
  TEST_ASSERT_EQUAL(RefreshKind::None, ui.act(Action::Ok, now, wc));
  TEST_ASSERT_EQUAL_STRING("Niente da confermare per questa sessione", ui.lastAction());
  TEST_ASSERT_EQUAL(4, m.nSessions);
  ui.act(Action::Next, now, wc);  // docs-site (FINITA)
  TEST_ASSERT_EQUAL_STRING("docs-site", m.sessions[m.ui.sel].name);
  TEST_ASSERT_EQUAL(RefreshKind::Quick, ui.act(Action::Ok, now, wc));
  TEST_ASSERT_EQUAL_STRING("Archiviata: docs-site", ui.lastAction());
  TEST_ASSERT_EQUAL_STRING("Archiviata", m.toast.msg);
  TEST_ASSERT_EQUAL(ScreenId::Home, top(m).id);
  TEST_ASSERT_EQUAL(1, m.ui.depth);
  TEST_ASSERT_EQUAL(3, m.nSessions);
  TEST_ASSERT_EQUAL(1, m.nArchived);
  for (int k = 0; k < m.nSessions; k++) TEST_ASSERT_FALSE(streq(m.sessions[k].name, "docs-site"));
  TEST_ASSERT_TRUE(m.ui.sel < m.nSessions);
  // idle ed error si archiviano, permission/input/working/compact no
  Session s = {};
  const SessionState yes[] = {SessionState::Done, SessionState::Idle, SessionState::Error};
  const SessionState no[] = {SessionState::Working, SessionState::Permission, SessionState::Input, SessionState::Compact};
  for (SessionState st : yes) { s.state = st; TEST_ASSERT_TRUE(canArchive(s)); }
  for (SessionState st : no) { s.state = st; TEST_ASSERT_FALSE(canArchive(s)); }
}

static void test_archive_hint_fits() {
  // Riga in fondo al Dettaglio: Silkscreen 8, peso 400, al massimo W − 8 = 242 px.
  const int w = textWidth("1X SESSIONE   2X ARCHIVIA   LUNGA INDIETRO", 8, 400, true);
  TEST_ASSERT_TRUE_MESSAGE(w <= 242, "la riga di aiuto non sta in 242 px");
  printf("larghezza riga Archivia: %d px su 242\n", w);
}

static void test_archive_live_dismiss_and_refusal() {
  static Live b;
  TEST_ASSERT_EQUAL(4, b.ui.model().nSessions);
  b.archiveD4();
  TEST_ASSERT_EQUAL(3, b.ui.model().nSessions);
  TEST_ASSERT_EQUAL(-1, b.find("d4"));
  TEST_ASSERT_EQUAL(NetCmd::Dismiss, b.reqs[b.nReqs - 1].cmd);
  TEST_ASSERT_EQUAL_STRING("d1", b.reqs[b.nReqs - 1].id);
  TEST_ASSERT_EQUAL_STRING("d4", b.reqs[b.nReqs - 1].session);
  char out[160];
  encodeDismiss(b.reqs[b.nReqs - 1].id, b.reqs[b.nReqs - 1].session, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"dismiss\",\"v\":1,\"id\":\"d1\",\"session\":\"d4\"}", out);
  // uno state che arriva prima dell'ack la contiene ancora: resta nascosta
  b.now += 300;
  b.send(1);
  TEST_ASSERT_EQUAL(3, b.ui.model().nSessions);
  TEST_ASSERT_EQUAL(-1, b.find("d4"));
  // ack con un id sconosciuto: ignorato
  TEST_ASSERT_EQUAL(RefreshKind::None, b.ui.netAck("c99", false, b.now));
  TEST_ASSERT_EQUAL(3, b.ui.model().nSessions);
  // ack negativo: torna, con il toast
  b.ui.netAck("d1", false, b.now);
  TEST_ASSERT_EQUAL(4, b.ui.model().nSessions);
  TEST_ASSERT_TRUE(b.find("d4") >= 0);
  TEST_ASSERT_EQUAL_STRING("Non archiviata", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(0, b.ui.model().nArchived);
  b.ui.netAck("d1", true, b.now);  // tardivo: ignorato
  TEST_ASSERT_EQUAL(4, b.ui.model().nSessions);
}

static void test_archive_live_timeout_restores() {
  static Live b;
  b.archiveD4();
  TEST_ASSERT_EQUAL(3, b.ui.model().nSessions);
  b.now += kDismissMs;  // al limite: si aspetta ancora
  b.ui.tick(b.now, b.wc());
  TEST_ASSERT_EQUAL(3, b.ui.model().nSessions);
  b.now += 1;
  b.ui.tick(b.now, b.wc());
  TEST_ASSERT_EQUAL(4, b.ui.model().nSessions);
  TEST_ASSERT_EQUAL_STRING("Non archiviata", b.ui.model().toast.msg);
  TEST_ASSERT_EQUAL(0, b.ui.model().nArchived);
}

static void test_archive_live_ack_ok_then_state() {
  static Live b;
  b.archiveD4();
  b.ui.netAck("d1", true, b.now);
  TEST_ASSERT_EQUAL(1, b.ui.model().nArchived);
  b.now += kDismissMs + 1000;  // con l'ack ok non scade
  b.ui.tick(b.now, b.wc());
  TEST_ASSERT_EQUAL(3, b.ui.model().nSessions);
  b.send(1);  // state vecchio, ancora con d4 ferma: nascosta
  TEST_ASSERT_EQUAL(-1, b.find("d4"));
  b.send(2, SessionState::Done, false);  // il servizio l'ha tolta: archivio locale chiuso
  TEST_ASSERT_EQUAL(0, b.ui.model().nArchived);
  TEST_ASSERT_EQUAL(3, b.ui.model().nSessions);
  b.send(3, SessionState::Working);  // torna attiva dal PC: si vede
  TEST_ASSERT_TRUE(b.find("d4") >= 0);
}

static void test_archive_live_back_to_work_while_waiting() {
  static Live b;
  b.archiveD4();
  b.send(1, SessionState::Working);  // nuovo prompt prima dell'ack: si vede subito
  TEST_ASSERT_TRUE(b.find("d4") >= 0);
  TEST_ASSERT_EQUAL(0, b.ui.model().nArchived);
  b.ui.netAck("d1", false, b.now);  // niente da ripristinare, niente doppioni
  TEST_ASSERT_EQUAL(4, b.ui.model().nSessions);
}

static void test_decode_ack() {
  AckMsg a;
  doc.clear();
  std::string js = "{\"t\":\"ack\",\"v\":1,\"id\":\"d1\",\"ok\":false,\"error\":\"sessione sconosciuta\"}";
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_TRUE(decodeAck(doc.as<JsonVariantConst>(), a));
  TEST_ASSERT_EQUAL_STRING("d1", a.id);
  TEST_ASSERT_FALSE(a.ok);
  TEST_ASSERT_EQUAL_STRING("sessione sconosciuta", a.error);
  js = readFile("../protocol/examples/ack.json");
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_TRUE(decodeAck(doc.as<JsonVariantConst>(), a));
  TEST_ASSERT_TRUE(a.ok);
  js = "{\"t\":\"ack\",\"v\":1,\"id\":\"d1\"}";  // senza ok
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_FALSE(decodeAck(doc.as<JsonVariantConst>(), a));
}

// ---------------------------------------------------------------- 0.9.0: orari della giornata di lavoro

static void test_set_work_hours() {
  TEST_ASSERT_TRUE(set(readFile("../protocol/examples/set-orari.json")));  // {"workStart":"08:30","workEnd":"17:30"}
  TEST_ASSERT_TRUE_MESSAGE(cmd.ok, cmd.error);
  TEST_ASSERT_TRUE(cmd.patch.on(RemoteKey::WorkStart));
  TEST_ASSERT_TRUE(cmd.patch.on(RemoteKey::WorkEnd));
  TEST_ASSERT_EQUAL(8 * 60 + 30, cmd.patch.v.workStart);
  TEST_ASSERT_EQUAL(17 * 60 + 30, cmd.patch.v.workEnd);
  // formato fisso a due cifre, 00:00–23:59, solo stringhe
  const char *bad[] = {"\"9:00\"", "\"24:00\"", "\"12:60\"", "\"0900\"", "\"09.00\"", "\"09:00 \"", "540", "null", "\"\""};
  for (const char *v : bad) {
    TEST_ASSERT_TRUE(set(setJson(std::string("{\"workStart\":") + v + "}")));
    TEST_ASSERT_FALSE_MESSAGE(cmd.ok, v);
    TEST_ASSERT_EQUAL_STRING("workStart: valore non valido", cmd.error);
  }
  TEST_ASSERT_TRUE(set(setJson("{\"workEnd\":\"23:59\"}")));
  TEST_ASSERT_TRUE(cmd.ok);
  TEST_ASSERT_EQUAL(23 * 60 + 59, cmd.patch.v.workEnd);
  TEST_ASSERT_FALSE(cmd.patch.on(RemoteKey::WorkStart));
}

// Il loop ricontrolla con gli orari attuali: un set con uno solo dei due deve dare inizio prima della fine.
static void test_check_patch_work_order() {
  static Model m;
  newModel(m, kNow, WallClock{kLocal});  // 09:00–18:00
  char err[96];
  TEST_ASSERT_TRUE(set(setJson("{\"workStart\":\"17:59\"}")));
  TEST_ASSERT_TRUE(checkPatch(cmd.patch, m.set, err, sizeof err));
  TEST_ASSERT_TRUE(set(setJson("{\"workStart\":\"18:00\"}")));
  TEST_ASSERT_FALSE(checkPatch(cmd.patch, m.set, err, sizeof err));
  TEST_ASSERT_EQUAL_STRING("l'inizio (18:00) deve venire prima della fine (18:00)", err);
  TEST_ASSERT_TRUE(set(setJson("{\"workEnd\":\"08:00\"}")));
  TEST_ASSERT_FALSE(checkPatch(cmd.patch, m.set, err, sizeof err));
  TEST_ASSERT_TRUE(set(setJson("{\"workStart\":\"07:00\",\"workEnd\":\"08:00\"}")));
  TEST_ASSERT_TRUE(checkPatch(cmd.patch, m.set, err, sizeof err));
  TEST_ASSERT_TRUE(set(setJson("{\"night\":true}")));  // senza orari: niente da controllare
  TEST_ASSERT_TRUE(checkPatch(cmd.patch, m.set, err, sizeof err));
  TEST_ASSERT_EQUAL_STRING("", err);
}

static void test_apply_work_hours() {
  static Bench b;
  TEST_ASSERT_EQUAL(RefreshKind::Partial, b.apply("{\"workStart\":\"08:30\",\"workEnd\":\"17:30\"}"));
  const RemoteValues v = remoteValues(b.ui.model().set, b.now);
  TEST_ASSERT_EQUAL(510, v.workStart);
  TEST_ASSERT_EQUAL(1050, v.workEnd);
  char out[320];
  encodeSettings(v, out, sizeof out);
  TEST_ASSERT_NOT_NULL(strstr(out, "\"workStart\":\"08:30\",\"workEnd\":\"17:30\",\"game\":false}"));
}

static void test_workday_command_decode() {
  WorkdayCmd w;
  std::string js = readFile("../protocol/examples/workday.json");
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_EQUAL(MsgType::Workday, msgType(doc.as<JsonVariantConst>()));
  TEST_ASSERT_TRUE(decodeWorkday(doc.as<JsonVariantConst>(), w));
  TEST_ASSERT_TRUE(w.ok);
  TEST_ASSERT_TRUE(w.start);
  TEST_ASSERT_EQUAL_STRING("c12", w.id);
  js = readFile("../protocol/examples/workday-stop.json");
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_TRUE(decodeWorkday(doc.as<JsonVariantConst>(), w));
  TEST_ASSERT_TRUE(w.ok);
  TEST_ASSERT_FALSE(w.start);
  js = "{\"t\":\"workday\",\"v\":1,\"id\":\"c3\",\"action\":\"pausa\"}";
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_TRUE(decodeWorkday(doc.as<JsonVariantConst>(), w));
  TEST_ASSERT_FALSE(w.ok);
  TEST_ASSERT_EQUAL_STRING("action: valore non valido", w.error);
  js = "{\"t\":\"workday\",\"v\":1,\"action\":\"start\"}";  // senza id: niente ack
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  TEST_ASSERT_FALSE(decodeWorkday(doc.as<JsonVariantConst>(), w));
  // ack negativo con il testo del firmware
  char out[200];
  encodeAck("c12", false, kWorkPastError, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"ack\",\"v\":1,\"id\":\"c12\",\"ok\":false,\"error\":\"orario di fine già passato\"}", out);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_example_set);
  RUN_TEST(test_example_full_refresh);
  RUN_TEST(test_example_settings_encoding);
  RUN_TEST(test_example_ack_encoding);
  RUN_TEST(test_set_permon_true_always_refused);
  RUN_TEST(test_set_unknown_key_refused);
  RUN_TEST(test_set_wrong_types_refused);
  RUN_TEST(test_set_settings_missing_or_empty);
  RUN_TEST(test_set_id_rules);
  RUN_TEST(test_ack_error_max_95_bytes);
  RUN_TEST(test_set_all_keys);
  RUN_TEST(test_apply_refresh_like_menu);
  RUN_TEST(test_apply_mute);
  RUN_TEST(test_mute_expiry_reported_off);
  RUN_TEST(test_apply_permoff_closes_open_permission);
  RUN_TEST(test_nvs_roundtrip);
  RUN_TEST(test_nvs_rejects_bad_blobs);
  RUN_TEST(test_nvs_restore_mute);
  RUN_TEST(test_archive_only_stopped_sessions);
  RUN_TEST(test_archive_hint_fits);
  RUN_TEST(test_archive_live_dismiss_and_refusal);
  RUN_TEST(test_archive_live_timeout_restores);
  RUN_TEST(test_archive_live_ack_ok_then_state);
  RUN_TEST(test_archive_live_back_to_work_while_waiting);
  RUN_TEST(test_decode_ack);
  RUN_TEST(test_set_work_hours);
  RUN_TEST(test_check_patch_work_order);
  RUN_TEST(test_apply_work_hours);
  RUN_TEST(test_workday_command_decode);
  return UNITY_END();
}
