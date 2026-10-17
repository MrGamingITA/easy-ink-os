// Aggiornamento remoto (docs/OTA.md): Ed25519 e manifesto contro protocol/vectors/ota-vectors.json (Node/OpenSSL),
// controlli di ota-key e ota-offer, condizioni, macchina della ricezione con flash e HTTP finti, registro e
// conferma dopo il riavvio (rollback simulato), schermata e stato di alimentazione.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/crypto.h"
#include "core/ed25519.h"
#include "core/ota.h"
#include "core/power.h"
#include "core/ui.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

JsonDocument V;

std::string readFile(const char *path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

std::vector<uint8_t> B(JsonVariantConst v) {
  const char *s = v.as<const char *>();
  TEST_ASSERT_NOT_NULL_MESSAGE(s, "campo esadecimale mancante");
  std::vector<uint8_t> out(strlen(s) / 2);
  if (!out.empty()) TEST_ASSERT_TRUE_MESSAGE(cry::fromHex(s, out.data(), out.size()), s);
  return out;
}

std::string hexOf(const uint8_t *d, size_t n) {
  std::vector<char> t(2 * n + 1);
  cry::toHex(d, n, t.data());
  return std::string(t.data());
}

const ota::Identity kMe = {"calamaio-3f2a", "heltec213", 0x330000};  // app0/app1 da 3 342 336 byte

// Messaggio JSON → documento (come il task di rete: parseMessage con il filtro).
bool parse(const std::string &js, JsonDocument &d) { return parseMessage(js.c_str(), js.size(), d); }

std::string offerJson() {
  std::string s;
  serializeJson(V["otaOffer"]["message"], s);
  return s;
}

std::vector<uint8_t> image() {
  std::string one = "immagine di prova del firmware Calamaio 0.7.0\n", all;
  for (int i = 0; i < 64; i++) all += one;
  return std::vector<uint8_t>(all.begin(), all.end());
}

}  // namespace

// ---------------------------------------------------------------- vettori
static void test_vectors_loaded() {
  const std::string js = readFile("../protocol/vectors/ota-vectors.json");
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 1000, "manca protocol/vectors/ota-vectors.json");
  TEST_ASSERT_FALSE(deserializeJson(V, js));
  TEST_ASSERT_EQUAL(4, V["ed25519"].size());
}

static void test_sha512() {
  uint8_t out[64];
  ed::sha512(reinterpret_cast<const uint8_t *>("abc"), 3, out);
  // SHA-512("abc") è il messaggio del vettore «TEST SHA(abc)» di RFC 8032.
  TEST_ASSERT_EQUAL_STRING(V["ed25519"][3]["msg"].as<const char *>(), hexOf(out, 64).c_str());
  ed::sha512(nullptr, 0, out);
  TEST_ASSERT_EQUAL_STRING(
      "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e",
      hexOf(out, 64).c_str());
  // 112 byte: il caso in cui il riempimento scavalca un blocco (FIPS 180-2, 896 bit)
  const char *m896 = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
  ed::sha512(reinterpret_cast<const uint8_t *>(m896), strlen(m896), out);
  TEST_ASSERT_EQUAL_STRING(
      "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909",
      hexOf(out, 64).c_str());
}

static void test_ed25519_rfc8032() {
  for (JsonVariantConst v : V["ed25519"].as<JsonArrayConst>()) {
    std::vector<uint8_t> pub = B(v["pub"]), msg = B(v["msg"]), sig = B(v["sig"]);
    TEST_ASSERT_TRUE_MESSAGE(ed::verify(sig.data(), msg.data(), msg.size(), pub.data()), v["name"]);
  }
}

static void test_ed25519_negative() {
  int n = 0;
  for (JsonVariantConst v : V["ed25519Negative"].as<JsonArrayConst>()) {
    std::vector<uint8_t> pub = B(v["pub"]), msg = B(v["msg"]), sig = B(v["sig"]);
    TEST_ASSERT_FALSE_MESSAGE(ed::verify(sig.data(), msg.data(), msg.size(), pub.data()), v["name"]);
    n++;
  }
  TEST_ASSERT_EQUAL(6, n);
}

static void test_manifest_canonical_and_signature() {
  JsonVariantConst f = V["manifest"]["fields"];
  ota::Manifest m = {};
  strcpy(m.board, f["board"]);
  strcpy(m.device, f["device"]);
  strcpy(m.version, f["version"]);
  m.size = f["size"];
  strcpy(m.sha256, f["sha256"]);
  m.built = f["built"].as<int64_t>();
  uint8_t c[ota::kCanonMax];
  const size_t n = ota::canonical(m, c, sizeof c);
  TEST_ASSERT_EQUAL_STRING(V["manifest"]["canonical"].as<const char *>(), hexOf(c, n).c_str());
  std::vector<uint8_t> pub = B(V["manifest"]["key"]["pub"]), sig = B(V["manifest"]["sig"]);
  TEST_ASSERT_TRUE(ota::manifestSigned(m, sig.data(), pub.data()));
  // l'immagine di prova ha proprio quello SHA-256 e quella dimensione
  const std::vector<uint8_t> img = image();
  uint8_t h[32];
  cry::sha256(img.data(), img.size(), h);
  TEST_ASSERT_EQUAL_STRING(m.sha256, hexOf(h, 32).c_str());
  TEST_ASSERT_EQUAL(img.size(), m.size);
  int neg = 0;
  for (JsonVariantConst v : V["manifest"]["negative"].as<JsonArrayConst>()) {
    ota::Manifest x = {};
    JsonVariantConst g = v["manifest"];
    strcpy(x.board, g["board"]);
    strcpy(x.device, g["device"]);
    strcpy(x.version, g["version"]);
    x.size = g["size"];
    strcpy(x.sha256, g["sha256"]);
    x.built = g["built"].as<int64_t>();
    std::vector<uint8_t> s = B(v["sig"]);
    TEST_ASSERT_FALSE_MESSAGE(ota::manifestSigned(x, s.data(), pub.data()), v["name"]);
    neg++;
  }
  TEST_ASSERT_EQUAL(5, neg);
}

static void test_macs() {
  std::vector<uint8_t> SK = B(V["otaKey"]["SK"]);
  uint8_t mac[16];
  TEST_ASSERT_TRUE(ota::keyMac(SK.data(), V["otaKey"]["pub"], mac));
  TEST_ASSERT_EQUAL_STRING(V["otaKey"]["mac"].as<const char *>(), hexOf(mac, 16).c_str());
  TEST_ASSERT_TRUE(ota::offerMac(SK.data(), V["otaOffer"]["id"], V["manifest"]["fields"]["sha256"], mac));
  TEST_ASSERT_EQUAL_STRING(V["otaOffer"]["mac"].as<const char *>(), hexOf(mac, 16).c_str());
}

// ---------------------------------------------------------------- ota-key
static void test_ota_key() {
  std::vector<uint8_t> SK = B(V["otaKey"]["SK"]), pub = B(V["otaKey"]["pub"]);
  std::string js;
  serializeJson(V["otaKey"]["message"], js);
  JsonDocument d;
  TEST_ASSERT_TRUE(parse(js, d));
  TEST_ASSERT_EQUAL(int(MsgType::OtaKey), int(msgType(d.as<JsonVariantConst>())));
  ota::KeyCheck k = ota::checkKey(d.as<JsonVariantConst>(), true, SK.data(), false, nullptr);
  TEST_ASSERT_EQUAL(int(ota::KeyVerdict::Save), int(k.verdict));
  TEST_ASSERT_EQUAL_MEMORY(pub.data(), k.pub, 32);
  k = ota::checkKey(d.as<JsonVariantConst>(), true, SK.data(), true, pub.data());
  TEST_ASSERT_EQUAL(int(ota::KeyVerdict::Same), int(k.verdict));
  uint8_t other[32];
  memcpy(other, pub.data(), 32);
  other[0] ^= 1;
  k = ota::checkKey(d.as<JsonVariantConst>(), true, SK.data(), true, other);  // chiave diversa: solo dopo un nuovo abbinamento
  TEST_ASSERT_EQUAL(int(ota::KeyVerdict::Rejected), int(k.verdict));
  k = ota::checkKey(d.as<JsonVariantConst>(), false, SK.data(), false, nullptr);  // senza autenticazione
  TEST_ASSERT_EQUAL(int(ota::KeyVerdict::Rejected), int(k.verdict));
  uint8_t sk2[32];
  memcpy(sk2, SK.data(), 32);
  sk2[5] ^= 0x10;  // un'altra connessione (o un intruso): mac sbagliato
  k = ota::checkKey(d.as<JsonVariantConst>(), true, sk2, false, nullptr);
  TEST_ASSERT_EQUAL(int(ota::KeyVerdict::Rejected), int(k.verdict));
}

// ---------------------------------------------------------------- ota-offer
static bool offer(const std::string &js, bool authed, bool hasPub, char *why, ota::Offer &o, const ota::Identity &me = kMe) {
  std::vector<uint8_t> SK = B(V["otaOffer"]["SK"]), pub = B(V["manifest"]["key"]["pub"]);
  JsonDocument d;
  TEST_ASSERT_TRUE_MESSAGE(parse(js, d), js.c_str());
  return ota::checkOffer(d.as<JsonVariantConst>(), authed, SK.data(), hasPub, pub.data(), me, o, why, 96);
}

static std::string edit(void (*fn)(JsonObject)) {
  JsonDocument d;
  d.set(V["otaOffer"]["message"]);
  fn(d.as<JsonObject>());
  std::string s;
  serializeJson(d, s);
  return s;
}

static void test_offer_valid() {
  ota::Offer o;
  char why[96] = "";
  TEST_ASSERT_TRUE_MESSAGE(offer(offerJson(), true, true, why, o), why);
  TEST_ASSERT_EQUAL_STRING("c17", o.id);
  TEST_ASSERT_EQUAL_STRING("0.7.0", o.m.version);
  TEST_ASSERT_EQUAL(2944, o.m.size);
  TEST_ASSERT_EQUAL_STRING(V["otaOffer"]["message"]["url"].as<const char *>(), o.url);
}

static void test_offer_rejections_in_order() {
  ota::Offer o;
  char why[96];
  struct C { const char *name; std::string js; bool authed, hasPub; const char *want; ota::Identity me; };
  const ota::Identity small = {"calamaio-3f2a", "heltec213", 1000};
  const C cases[] = {
      {"senza autenticazione", offerJson(), false, true, "display non autenticato", kMe},
      {"mac sbagliato", edit([](JsonObject m) { m["mac"] = "00000000000000000000000000000000"; }), true, true, "mac sbagliato", kMe},
      {"id cambiato (il mac copre l'id)", edit([](JsonObject m) { m["id"] = "c18"; }), true, true, "mac sbagliato", kMe},
      {"nessuna chiave", offerJson(), true, false, "nessuna chiave per gli aggiornamenti: manca ota-key dal PC", kMe},
      {"versione cambiata", edit([](JsonObject m) { m["manifest"]["version"] = "0.7.1"; }), true, true, "firma del manifesto non valida", kMe},
      {"firma rovinata", edit([](JsonObject m) { std::string s = m["sig"].as<std::string>(); s[0] = s[0] == '0' ? '1' : '0'; m["sig"] = s; }), true, true, "firma del manifesto non valida", kMe},
      {"campo in più", edit([](JsonObject m) { m["manifest"]["extra"] = 1; }), true, true, "manifesto non valido", kMe},
      {"sha256 maiuscolo", edit([](JsonObject m) { std::string s = m["manifest"]["sha256"].as<std::string>(); for (char &ch : s) ch = toupper(ch); m["manifest"]["sha256"] = s; }), true, true, "manifesto non valido", kMe},
      {"size zero", edit([](JsonObject m) { m["manifest"]["size"] = 0; }), true, true, "manifesto non valido", kMe},
      {"https", edit([](JsonObject m) { m["url"] = "https://192.168.1.20:8766/ota/x"; }), true, true, "indirizzo dell'immagine non valido", kMe},
      {"partizione piccola", offerJson(), true, true, "immagine più grande della partizione", small},
  };
  for (const C &c : cases) {
    why[0] = 0;
    TEST_ASSERT_FALSE_MESSAGE(offer(c.js, c.authed, c.hasPub, why, o, c.me), c.name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.want, why, c.name);
  }
  // board e device: firmati, quindi un'offerta per un'altra scheda la firma il servizio per quella scheda.
  const ota::Identity other = {"calamaio-0000", "heltec213", 0x330000};
  TEST_ASSERT_FALSE(offer(offerJson(), true, true, why, o, other));
  TEST_ASSERT_EQUAL_STRING("aggiornamento per un'altra scheda (calamaio-3f2a)", why);
  const ota::Identity dev = {"calamaio-3f2a", "t5213", 0x330000};
  TEST_ASSERT_FALSE(offer(offerJson(), true, true, why, o, dev));
  TEST_ASSERT_EQUAL_STRING("aggiornamento per un altro dispositivo (heltec213)", why);
  // id assente: non si può nemmeno rispondere
  JsonDocument d;
  TEST_ASSERT_TRUE(parse("{\"t\":\"ota-offer\",\"v\":1}", d));
  char id[ota::kIdLen];
  TEST_ASSERT_FALSE(ota::offerId(d.as<JsonVariantConst>(), id));
  // un rifiuto riporta total, se il manifesto si legge
  TEST_ASSERT_FALSE(offer(offerJson(), false, true, why, o));
  TEST_ASSERT_EQUAL(2944, o.m.size);
}

// ---------------------------------------------------------------- condizioni
static void test_conditions() {
  char why[96];
  bool shown;
  ota::Conditions c = {false, false, false, false, UsbPresent::Yes, -1};
  TEST_ASSERT_TRUE(ota::conditionsOk(c, why, sizeof why, &shown));  // cavo: sempre, anche senza batteria letta
  c.usb = UsbPresent::No;
  c.battPct = 50;
  TEST_ASSERT_TRUE(ota::conditionsOk(c, why, sizeof why));
  c.battPct = 49;
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why, &shown));
  TEST_ASSERT_EQUAL_STRING("batteria al 49%: collega il cavo USB o ricarica fino al 50%", why);
  TEST_ASSERT_TRUE(shown);
  c.battPct = -1;
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("batteria non misurabile e cavo USB staccato", why);
  c.usb = UsbPresent::Unknown;
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why));  // né l'una né l'altra: non parte
  TEST_ASSERT_EQUAL_STRING("batteria non misurabile e cavo USB non rilevato", why);
  c.battPct = 80;
  TEST_ASSERT_TRUE(ota::conditionsOk(c, why, sizeof why));  // cavo sconosciuto ma batteria misurata
  c.usb = UsbPresent::Yes;
  c.permOpen = true;  // permesso aperto: mai, neanche col cavo
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why, &shown));
  TEST_ASSERT_EQUAL_STRING("c'è una richiesta di permesso aperta", why);
  TEST_ASSERT_FALSE(shown);  // non si copre la schermata del permesso
  c.permOpen = false;
  c.calibrating = true;
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("calibrazione della batteria in corso", why);
  c.calibrating = false;
  c.busy = true;
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why));
  c.busy = false;
  c.trial = true;
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why));
}

// 0.12.0: partita del Game Boy aperta (Wi-Fi spento solo durante il gioco vero; caricamento o pausa contano lo stesso).
static void test_conditions_refuse_during_game() {
  ota::Conditions c;
  memset(&c, 0, sizeof c);
  c.usb = UsbPresent::Yes;
  c.battPct = 90;
  c.playing = true;
  char why[96];
  bool shown = true;
  TEST_ASSERT_FALSE(ota::conditionsOk(c, why, sizeof why, &shown));
  TEST_ASSERT_EQUAL_STRING("partita in corso: riprova a fine partita", why);
  TEST_ASSERT_TRUE(shown);  // a schermo: «rifiutato: partita in corso: riprova a fine partita»
  c.playing = false;
  TEST_ASSERT_TRUE(ota::conditionsOk(c, why, sizeof why, &shown));
}

static void test_status_encoding() {
  char out[256];
  ota::encodeStatus("c17", OtaPhase::Receive, 1536, 2944, nullptr, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"ota-status\",\"v\":1,\"id\":\"c17\",\"phase\":\"ricevo\",\"done\":1536,\"total\":2944}", out);
  ota::encodeStatus("c17", OtaPhase::Rejected, 0, 2944, "mac sbagliato", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"ota-status\",\"v\":1,\"id\":\"c17\",\"phase\":\"rifiutato\",\"done\":0,\"total\":2944,\"error\":\"mac sbagliato\"}", out);
  // stessi testi degli esempi nei vettori (forme per il servizio)
  for (JsonVariantConst e : V["examples"]["otaStatus"].as<JsonArrayConst>()) {
    const char *ph = e["phase"];
    OtaPhase p = OtaPhase::None;
    for (int k = 1; k <= 7; k++)
      if (!strcmp(otaPhaseKey(OtaPhase(k)), ph)) p = OtaPhase(k);
    TEST_ASSERT_TRUE_MESSAGE(p != OtaPhase::None, ph);
    ota::encodeStatus(e["id"], p, e["done"], e["total"], e["error"], out, sizeof out);
    std::string want;
    serializeJson(e, want);
    TEST_ASSERT_EQUAL_STRING(want.c_str(), out);
  }
}

// ---------------------------------------------------------------- ricezione (flash e HTTP finti)
namespace {
struct FakeFlash : ota::Flash {
  std::vector<uint8_t> data;
  bool begun = false, ended = false, aborted = false, boot = false;
  bool failBegin = false, failWrite = false, failEnd = false, failBoot = false;
  bool begin(uint32_t, char *why, size_t cap) override { if (failBegin) { snprintf(why, cap, "nessuna partizione"); return false; } begun = true; return true; }
  bool write(const uint8_t *d, size_t n) override { if (failWrite) return false; data.insert(data.end(), d, d + n); return true; }
  bool end(char *why, size_t cap) override { if (failEnd) { snprintf(why, cap, "ESP_ERR_OTA_VALIDATE_FAILED"); return false; } ended = true; return true; }
  void abort() override { aborted = true; }
  bool setBoot() override { if (failBoot) return false; boot = true; return true; }
};
struct FakeSource : ota::Source {
  std::vector<uint8_t> body;
  size_t pos = 0, chunk = 700, closeAt = SIZE_MAX;
  bool opened = false, closed = false, stall = false;
  bool open(const char *, char *, size_t) override { opened = true; return true; }
  int read(uint8_t *buf, size_t cap) override {
    if (stall) return 0;
    if (pos >= closeAt || pos >= body.size()) return pos >= body.size() && closeAt == SIZE_MAX ? 0 : -1;
    size_t n = chunk < cap ? chunk : cap;
    if (n > body.size() - pos) n = body.size() - pos;
    memcpy(buf, body.data() + pos, n);
    pos += n;
    return int(n);
  }
  void close() override { closed = true; }
};
struct Out : ota::UpdaterOut {
  std::vector<std::string> st;  // «fase:done»
  std::vector<int> pct;
  std::string lastErr;
  bool saved = false, cleared = false;
  ota::Record rec = {};
  void status(OtaPhase p, uint32_t done, uint32_t, const char *e) override {
    st.push_back(std::string(otaPhaseKey(p)) + ":" + std::to_string(done));
    if (e) lastErr = e;
  }
  void progress(OtaPhase, uint32_t done, uint32_t total, const char *) override { pct.push_back(int(uint64_t(done) * 100 / total)); }
  bool saveRecord(const ota::Record &r) override { saved = true; rec = r; return true; }
  void clearRecord() override { cleared = true; }
};
ota::Offer goodOffer() {
  ota::Offer o;
  char why[96];
  offer(offerJson(), true, true, why, o);
  return o;
}
}  // namespace

static void test_updater_success() {
  FakeFlash f;
  FakeSource s;
  s.body = image();
  Out out;
  ota::Updater u;
  const ota::Offer o = goodOffer();
  TEST_ASSERT_TRUE(u.start(o, "0.6.0", "app1", f, s, 1000, out));
  TEST_ASSERT_TRUE(u.active());
  ota::Step r = ota::Step::Busy;
  Ms t = 1000;
  int steps = 0;
  while (r == ota::Step::Busy && steps < 100) { r = u.step(t += 10, out, 700); steps++; }
  TEST_ASSERT_EQUAL(int(ota::Step::Reboot), int(r));
  TEST_ASSERT_TRUE(f.data == image());
  TEST_ASSERT_TRUE(f.ended && f.boot && !f.aborted && s.closed);
  TEST_ASSERT_TRUE(out.saved);
  TEST_ASSERT_EQUAL_STRING("c17", out.rec.id);
  TEST_ASSERT_EQUAL_STRING("0.6.0", out.rec.from);
  TEST_ASSERT_EQUAL_STRING("0.7.0", out.rec.to);
  TEST_ASSERT_EQUAL_STRING("app1", out.rec.label);
  // ota-status: ricevo a 0 e a ogni 5 %, poi verifico, scrivo, riavvio
  TEST_ASSERT_EQUAL_STRING("ricevo:0", out.st.front().c_str());
  const size_t n = out.st.size();
  TEST_ASSERT_EQUAL_STRING("verifico:2944", out.st[n - 3].c_str());
  TEST_ASSERT_EQUAL_STRING("scrivo:2944", out.st[n - 2].c_str());
  TEST_ASSERT_EQUAL_STRING("riavvio:2944", out.st[n - 1].c_str());
  // 700 byte per passo su 2944: 23 %, 47 %, 71 %, 95 % → un ota-status per ciascun passo di 5 % superato
  TEST_ASSERT_EQUAL(1 + 4 + 3, n);
}

static void test_updater_status_every_5_percent() {
  FakeFlash f;
  FakeSource s;
  s.body.assign(100000, 0x5A);
  s.chunk = 100;  // 0,1 % per lettura
  ota::Offer o = goodOffer();
  o.m.size = 100000;
  uint8_t h[32];
  cry::sha256(s.body.data(), s.body.size(), h);
  cry::toHex(h, 32, o.m.sha256);
  Out out;
  ota::Updater u;
  TEST_ASSERT_TRUE(u.start(o, "0.6.0", "app1", f, s, 0, out));
  ota::Step r = ota::Step::Busy;
  for (Ms t = 1; r == ota::Step::Busy && t < 5000; t++) r = u.step(t, out, 100);
  TEST_ASSERT_EQUAL(int(ota::Step::Reboot), int(r));
  // ricevo:0, poi 5,10,…,95 (100 non è in ricezione: è «verifico») = 1 + 19, più verifico, scrivo, riavvio
  TEST_ASSERT_EQUAL(1 + 19 + 3, out.st.size());
  TEST_ASSERT_EQUAL_STRING("ricevo:5000", out.st[1].c_str());
  TEST_ASSERT_EQUAL_STRING("ricevo:95000", out.st[19].c_str());
  // la schermata riceve ogni 1 %: 0..99 e le tre fasi a 100
  TEST_ASSERT_EQUAL(100 + 3, out.pct.size());
}

static void test_updater_errors() {
  const ota::Offer o = goodOffer();
  {  // SHA-256 diverso: niente partizione di avvio, immagine abbandonata
    FakeFlash f;
    FakeSource s;
    s.body = image();
    s.body[100] ^= 1;
    Out out;
    ota::Updater u;
    u.start(o, "0.6.0", "app1", f, s, 0, out);
    ota::Step r = ota::Step::Busy;
    for (Ms t = 1; r == ota::Step::Busy && t < 100; t++) r = u.step(t, out);
    TEST_ASSERT_EQUAL(int(ota::Step::Failed), int(r));
    TEST_ASSERT_EQUAL_STRING("SHA-256 diverso dal manifesto", out.lastErr.c_str());
    TEST_ASSERT_TRUE(f.aborted && !f.boot && !out.saved);
    TEST_ASSERT_EQUAL_STRING("errore:2944", out.st.back().c_str());
  }
  {  // connessione chiusa a metà
    FakeFlash f;
    FakeSource s;
    s.body = image();
    s.closeAt = 1400;
    Out out;
    ota::Updater u;
    u.start(o, "0.6.0", "app1", f, s, 0, out);
    ota::Step r = ota::Step::Busy;
    for (Ms t = 1; r == ota::Step::Busy && t < 100; t++) r = u.step(t, out, 700);
    TEST_ASSERT_EQUAL(int(ota::Step::Failed), int(r));
    TEST_ASSERT_EQUAL_STRING("download interrotto a 1400 di 2944 byte", out.lastErr.c_str());
    TEST_ASSERT_TRUE(f.aborted && !f.boot);
  }
  {  // nessun dato per 15 s
    FakeFlash f;
    FakeSource s;
    s.body = image();
    Out out;
    ota::Updater u;
    u.start(o, "0.6.0", "app1", f, s, 0, out);
    u.step(10, out, 700);
    s.stall = true;
    TEST_ASSERT_EQUAL(int(ota::Step::Busy), int(u.step(10 + 14999, out)));
    TEST_ASSERT_EQUAL(int(ota::Step::Failed), int(u.step(10 + 15000, out)));
    TEST_ASSERT_EQUAL_STRING("download interrotto: nessun dato da 15 s", out.lastErr.c_str());
  }
  {  // la flash non scrive
    FakeFlash f;
    f.failWrite = true;
    FakeSource s;
    s.body = image();
    Out out;
    ota::Updater u;
    u.start(o, "0.6.0", "app1", f, s, 0, out);
    TEST_ASSERT_EQUAL(int(ota::Step::Failed), int(u.step(1, out)));
    TEST_ASSERT_EQUAL_STRING("scrittura nella flash non riuscita", out.lastErr.c_str());
  }
  {  // esp_ota_end rifiuta l'immagine
    FakeFlash f;
    f.failEnd = true;
    FakeSource s;
    s.body = image();
    Out out;
    ota::Updater u;
    u.start(o, "0.6.0", "app1", f, s, 0, out);
    TEST_ASSERT_EQUAL(int(ota::Step::Failed), int(u.step(1, out)));
    TEST_ASSERT_EQUAL_STRING("immagine non valida: ESP_ERR_OTA_VALIDATE_FAILED", out.lastErr.c_str());
    TEST_ASSERT_FALSE(f.aborted);  // dopo end() l'handle non c'è più
  }
  {  // partizione di avvio non impostata: il registro si toglie
    FakeFlash f;
    f.failBoot = true;
    FakeSource s;
    s.body = image();
    Out out;
    ota::Updater u;
    u.start(o, "0.6.0", "app1", f, s, 0, out);
    TEST_ASSERT_EQUAL(int(ota::Step::Failed), int(u.step(1, out)));
    TEST_ASSERT_TRUE(out.saved && out.cleared);
  }
  {  // la partizione non si apre
    FakeFlash f;
    f.failBegin = true;
    FakeSource s;
    Out out;
    ota::Updater u;
    TEST_ASSERT_FALSE(u.start(o, "0.6.0", "app1", f, s, 0, out));
    TEST_ASSERT_EQUAL_STRING("nessuna partizione", out.lastErr.c_str());
    TEST_ASSERT_FALSE(s.opened);
  }
  {  // più dati del manifesto: si leggono solo size byte
    FakeFlash f;
    FakeSource s;
    s.body = image();
    s.body.push_back('x');
    Out out;
    ota::Updater u;
    u.start(o, "0.6.0", "app1", f, s, 0, out);
    TEST_ASSERT_EQUAL(int(ota::Step::Reboot), int(u.step(1, out)));
    TEST_ASSERT_EQUAL(2944, f.data.size());
  }
}

// ---------------------------------------------------------------- dopo il riavvio
static void test_record_and_boot_cases() {
  ota::Record r = {};
  strcpy(r.id, "c17");
  strcpy(r.from, "0.6.0");
  strcpy(r.to, "0.7.0");
  strcpy(r.label, "app1");
  r.size = 1556033;
  uint8_t b[ota::kRecordLen];
  TEST_ASSERT_EQUAL(ota::kRecordLen, ota::packRecord(r, b, sizeof b));
  ota::Record back;
  TEST_ASSERT_TRUE(ota::unpackRecord(b, sizeof b, back));
  TEST_ASSERT_EQUAL_STRING("c17", back.id);
  TEST_ASSERT_EQUAL_STRING("app1", back.label);
  TEST_ASSERT_EQUAL(1556033, back.size);
  b[10] ^= 1;
  TEST_ASSERT_FALSE(ota::unpackRecord(b, sizeof b, back));
  using ota::BootCase;
  using ota::ImgState;
  TEST_ASSERT_EQUAL(int(BootCase::Trial), int(ota::bootCase(true, r, "app1", ImgState::PendingVerify)));
  TEST_ASSERT_EQUAL(int(BootCase::Confirmed), int(ota::bootCase(true, r, "app1", ImgState::Valid)));
  // il bootloader è tornato su app0 (nuova versione caduta o non confermata in 120 s)
  TEST_ASSERT_EQUAL(int(BootCase::RolledBack), int(ota::bootCase(true, r, "app0", ImgState::Valid)));
  TEST_ASSERT_EQUAL(int(BootCase::RolledBack), int(ota::bootCase(true, r, "app0", ImgState::Undefined)));
  TEST_ASSERT_EQUAL(int(BootCase::Normal), int(ota::bootCase(false, r, "app0", ImgState::Valid)));
}

static void test_trial_120_s() {
  ota::Trial t;
  t.begin(1000);
  TEST_ASSERT_EQUAL(int(ota::Trial::Act::None), int(t.tick(1000 + 120000)));
  TEST_ASSERT_EQUAL(int(ota::Trial::Act::MarkValid), int(t.onAuthed(1000 + 119999)));
  TEST_ASSERT_FALSE(t.active());
  TEST_ASSERT_EQUAL(int(ota::Trial::Act::None), int(t.onAuthed(1000 + 130000)));  // una decisione sola
  ota::Trial u;
  u.begin(0);
  TEST_ASSERT_EQUAL(int(ota::Trial::Act::Rollback), int(u.tick(120001)));
  TEST_ASSERT_EQUAL(int(ota::Trial::Act::None), int(u.onAuthed(120002)));
}

// ---------------------------------------------------------------- schermata e alimentazione
static Ui gUi(kHeltec213);

static void test_ui_redraw_policy() {
  WallClock wc{};
  gUi.begin(0, wc);
  gUi.tick(3000, wc);  // fine dell'avvio
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(gUi.otaStart("0.6.0", "0.7.0", 100000, 10000)));
  Model &m = gUi.model();
  TEST_ASSERT_EQUAL(int(ScreenId::Ota), int(top(m).id));
  TEST_ASSERT_NULL(holdLabel(m, 10000));  // niente Riposo
  // 5 % ma prima di 2 s: niente; 2 s ma meno di 5 %: niente; entrambi: parziale e un fotogramma in più
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(gUi.otaProgress(6000, 11000)));
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(gUi.otaProgress(4000, 13000)));
  TEST_ASSERT_EQUAL(0, m.ota.step);
  TEST_ASSERT_EQUAL(int(RefreshKind::Partial), int(gUi.otaProgress(6000, 13000)));
  TEST_ASSERT_EQUAL(1, m.ota.step);
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(gUi.otaProgress(30000, 14000)));  // 2 s non ancora passati
  TEST_ASSERT_EQUAL(int(RefreshKind::Partial), int(gUi.otaProgress(30000, 15000)));
  TEST_ASSERT_EQUAL(2, m.ota.step);
  // i gesti non contano durante la ricezione
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(gUi.act(Action::Menu, 15100, wc)));
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(gUi.act(Action::Sleep, 15200, wc)));
  TEST_ASSERT_EQUAL(int(ScreenId::Ota), int(top(m).id));
  PowerInputs in = powerInputs(m);
  TEST_ASSERT_TRUE(in.ota);
  PowerManager pm;
  pm.begin(0);
  pm.rest(0);
  pm.bye(0);
  in.pcOff = true;
  pm.update(in, 10 * 60000);
  TEST_ASSERT_EQUAL(int(PowerState::Active), int(pm.state()));  // né Riposo né bye addormentano
  // verifica e riavvio: parziale, la corsa avanza; errore: layout diverso, si chiude da sola dopo 15 s
  TEST_ASSERT_EQUAL(int(RefreshKind::Partial), int(gUi.otaPhase(OtaPhase::Verify, nullptr, 16000)));
  TEST_ASSERT_EQUAL(100000u, m.ota.done);
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(gUi.otaPhase(OtaPhase::Error, "SHA-256 diverso dal manifesto", 17000)));
  TEST_ASSERT_FALSE(powerInputs(m).ota);
  TEST_ASSERT_EQUAL(int(RefreshKind::None), int(gUi.tick(17000 + kOtaResultMs, wc)));
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(gUi.tick(17001 + kOtaResultMs, wc)));
  TEST_ASSERT_EQUAL(int(ScreenId::Home), int(top(m).id));
  TEST_ASSERT_EQUAL(int(OtaPhase::None), int(m.ota.phase));
  // rifiuto: un gesto chiude
  gUi.otaRejected("0.6.0", "0.7.0", "batteria al 42%: collega il cavo USB o ricarica fino al 50%", 40000);
  TEST_ASSERT_EQUAL(int(ScreenId::Ota), int(top(m).id));
  TEST_ASSERT_EQUAL(int(RefreshKind::Quick), int(gUi.act(Action::Next, 41000, wc)));
  TEST_ASSERT_EQUAL(int(ScreenId::Home), int(top(m).id));
  gUi.otaUpdated("0.7.0", 50000);
  TEST_ASSERT_EQUAL_STRING("Aggiornato alla 0.7.0", m.toast.msg);
}

// ota-cancel: mac dei vettori, solo per l'aggiornamento in corso, solo autenticato.
static void test_cancel() {
  std::vector<uint8_t> SK = B(V["otaCancel"]["SK"]);
  uint8_t mac[16];
  TEST_ASSERT_TRUE(ota::cancelMac(SK.data(), "c17", mac));
  TEST_ASSERT_EQUAL_STRING(V["otaCancel"]["mac"].as<const char *>(), hexOf(mac, 16).c_str());
  std::string js;
  serializeJson(V["otaCancel"]["message"], js);
  JsonDocument d;
  TEST_ASSERT_TRUE(parse(js, d));
  TEST_ASSERT_EQUAL(int(MsgType::OtaCancel), int(msgType(d.as<JsonVariantConst>())));
  const char *why = nullptr;
  TEST_ASSERT_TRUE(ota::checkCancel(d.as<JsonVariantConst>(), true, SK.data(), "c17", &why));
  TEST_ASSERT_FALSE(ota::checkCancel(d.as<JsonVariantConst>(), true, SK.data(), "c18", &why));  // un altro aggiornamento
  TEST_ASSERT_FALSE(ota::checkCancel(d.as<JsonVariantConst>(), true, SK.data(), "", &why));     // nessuno (o già in scrittura)
  TEST_ASSERT_FALSE(ota::checkCancel(d.as<JsonVariantConst>(), false, SK.data(), "c17", &why));
  SK[0] ^= 1;
  TEST_ASSERT_FALSE(ota::checkCancel(d.as<JsonVariantConst>(), true, SK.data(), "c17", &why));
  TEST_ASSERT_EQUAL_STRING("ota-cancel con mac sbagliato", why);
  // annullato in ricezione: errore «annullato dal PC», immagine abbandonata
  FakeFlash f;
  FakeSource src;
  src.body = image();
  Out out;
  ota::Updater u;
  u.start(goodOffer(), "0.6.0", "app1", f, src, 0, out);
  u.step(1, out, 700);
  u.cancel(ota::kCancelled, out);
  TEST_ASSERT_EQUAL(int(OtaPhase::Error), int(u.phase()));
  TEST_ASSERT_EQUAL_STRING("annullato dal PC", out.lastErr.c_str());
  TEST_ASSERT_TRUE(f.aborted && src.closed && !f.boot);
}

// otaId: [A-Za-z0-9_-]{1,32} come lo schema; versioni fino a 31 byte.
static void test_ids_and_lengths() {
  TEST_ASSERT_TRUE(ota::validId("o4c1d2e3f4a5b"));
  TEST_ASSERT_TRUE(ota::validId("A_b-9"));
  TEST_ASSERT_FALSE(ota::validId(""));
  TEST_ASSERT_FALSE(ota::validId("c 1"));
  TEST_ASSERT_FALSE(ota::validId("c\"1"));
  TEST_ASSERT_FALSE(ota::validId("123456789012345678901234567890123"));
  TEST_ASSERT_EQUAL(31, ota::kVersionLen - 1);
}

// status {batt, usb, cal}: cavo e calibrazione subito, batteria al massimo ogni 10 s; null quando non si sa.
static void test_power_status() {
  char out[96];
  ota::encodePowerStatus({78, 0, false}, out, sizeof out);
  std::string want;
  serializeJson(V["examples"]["status"], want);
  TEST_ASSERT_EQUAL_STRING(want.c_str(), out);
  ota::encodePowerStatus({-1, -1, false}, out, sizeof out);
  want.clear();
  serializeJson(V["examples"]["statusUnknown"], want);
  TEST_ASSERT_EQUAL_STRING(want.c_str(), out);
  ota::PowerReporter r;
  TEST_ASSERT_TRUE(r.due({78, 0, false}, 0));     // il primo sempre
  TEST_ASSERT_FALSE(r.due({78, 0, false}, 100));  // uguale
  TEST_ASSERT_FALSE(r.due({77, 0, false}, 5000)); // batteria: non prima di 10 s
  TEST_ASSERT_TRUE(r.due({77, 0, false}, 10000));
  TEST_ASSERT_TRUE(r.due({77, 1, false}, 10001)); // cavo: subito
  TEST_ASSERT_TRUE(r.due({77, 1, true}, 10002));  // calibrazione: subito
  TEST_ASSERT_TRUE(r.due({-1, 1, true}, 10003));  // la batteria non si sa più: subito
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_vectors_loaded);
  RUN_TEST(test_cancel);
  RUN_TEST(test_ids_and_lengths);
  RUN_TEST(test_power_status);
  RUN_TEST(test_sha512);
  RUN_TEST(test_ed25519_rfc8032);
  RUN_TEST(test_ed25519_negative);
  RUN_TEST(test_manifest_canonical_and_signature);
  RUN_TEST(test_macs);
  RUN_TEST(test_ota_key);
  RUN_TEST(test_offer_valid);
  RUN_TEST(test_offer_rejections_in_order);
  RUN_TEST(test_conditions);
  RUN_TEST(test_conditions_refuse_during_game);
  RUN_TEST(test_status_encoding);
  RUN_TEST(test_updater_success);
  RUN_TEST(test_updater_status_every_5_percent);
  RUN_TEST(test_updater_errors);
  RUN_TEST(test_record_and_boot_cases);
  RUN_TEST(test_trial_120_s);
  RUN_TEST(test_ui_redraw_policy);
  return UNITY_END();
}
