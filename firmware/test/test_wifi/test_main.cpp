// Più reti Wi-Fi (firmware 0.8.0): wifi-add, wifi-forget e wifi-list contro protocol/vectors/wifi-vectors.json
// (Node/OpenSSL, li usa anche il servizio Go), ordine dei tentativi e cambio automatico (core/wifi_roam).
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/crypto.h"
#include "core/retry.h"
#include "core/wifi_proto.h"
#include "core/wifi_roam.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

JsonDocument V;
uint8_t SK[32], SK2[32];

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

// Messaggio dei vettori → documento, come il task di rete (parseMessage con il filtro).
JsonDocument gDoc;
JsonVariantConst parsed(JsonVariantConst msg) {
  std::string js;
  serializeJson(msg, js);
  gDoc.clear();
  TEST_ASSERT_TRUE_MESSAGE(parseMessage(js.c_str(), js.size(), gDoc), js.c_str());
  return gDoc.as<JsonVariantConst>();
}

}  // namespace

// ---------------------------------------------------------------- vettori
static void test_vectors_loaded() {
  const std::string js = readFile("../protocol/vectors/wifi-vectors.json");
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 1000, "manca protocol/vectors/wifi-vectors.json");
  TEST_ASSERT_FALSE(deserializeJson(V, js));
  TEST_ASSERT_TRUE(cry::fromHex(V["SK"], SK, 32));
  TEST_ASSERT_TRUE(cry::fromHex(V["otherSK"], SK2, 32));
  // La SK è quella dei vettori di M5 (flow.auth.SK): stessa connessione autenticata.
  JsonDocument m5;
  TEST_ASSERT_FALSE(deserializeJson(m5, readFile("../protocol/vectors/crypto-vectors.json")));
  TEST_ASSERT_EQUAL_STRING(m5["flow"]["auth"]["SK"].as<const char *>(), V["SK"].as<const char *>());
  TEST_ASSERT_EQUAL(5, V["add"].size());
  TEST_ASSERT_EQUAL(12, V["addNegative"].size());
}

static void test_add_key_aad_and_ciphertext() {
  for (JsonVariantConst v : V["add"].as<JsonArrayConst>()) {
    const char *id = v["id"], *ssid = v["ssid"], *pw = v["password"];
    uint8_t key[32];
    wifi::addKey(SK, id, key);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v["key"].as<const char *>(), hexOf(key, 32).c_str(), v["name"]);
    cry::Msg aad;
    TEST_ASSERT_TRUE(wifi::addAad(id, ssid, aad));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v["aad"].as<const char *>(), hexOf(aad.data(), aad.size()).c_str(), v["name"]);
    // Stesso iv → stesso cifrato del servizio, tag compreso.
    std::vector<uint8_t> iv = B(v["iv"]);
    uint8_t ct[128];
    const size_t n = wifi::sealAdd(SK, id, ssid, pw, iv.data(), ct, sizeof ct);
    TEST_ASSERT_EQUAL(strlen(pw) + 16, n);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v["ct"].as<const char *>(), hexOf(ct, n).c_str(), v["name"]);
  }
}

static void test_add_decrypts_vectors() {
  for (JsonVariantConst v : V["add"].as<JsonArrayConst>()) {
    static wifi::AddCheck a;
    wifi::checkAdd(parsed(v["message"]), true, SK, a);
    TEST_ASSERT_TRUE(a.hasId);
    TEST_ASSERT_NULL_MESSAGE(a.error, v["name"]);
    TEST_ASSERT_EQUAL_STRING(v["id"].as<const char *>(), a.id);
    TEST_ASSERT_EQUAL_STRING(v["ssid"].as<const char *>(), a.ssid);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v["password"].as<const char *>(), a.pass, v["name"]);
    TEST_ASSERT_EQUAL(v["passwordBytes"].as<int>(), int(strlen(a.pass)));
  }
}

static void test_add_negative_vectors() {
  int n = 0;
  for (JsonVariantConst v : V["addNegative"].as<JsonArrayConst>()) {
    static wifi::AddCheck a;
    wifi::checkAdd(parsed(v["message"]), true, SK, a);
    TEST_ASSERT_TRUE(a.hasId);
    TEST_ASSERT_NOT_NULL_MESSAGE(a.error, v["name"]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v["error"].as<const char *>(), a.error, v["name"]);
    TEST_ASSERT_EQUAL_STRING("", a.pass);  // niente in chiaro resta in giro
    n++;
  }
  TEST_ASSERT_EQUAL(12, n);
}

static void test_add_needs_auth_and_fields() {
  JsonVariantConst ok = V["add"][0]["message"];
  static wifi::AddCheck a;
  // Senza auth-ok (o con la SK di un'altra connessione) niente password.
  wifi::checkAdd(parsed(ok), false, SK, a);
  TEST_ASSERT_TRUE(a.hasId);
  TEST_ASSERT_EQUAL_STRING(wifi::kErrAuth, a.error);
  TEST_ASSERT_EQUAL_STRING("", a.pass);
  wifi::checkAdd(parsed(ok), true, SK2, a);
  TEST_ASSERT_EQUAL_STRING(wifi::kErrDecrypt, a.error);
  // id mancante o troppo lungo: niente ack.
  JsonDocument d;
  d.set(ok);
  d.remove("id");
  wifi::checkAdd(parsed(d.as<JsonVariantConst>()), true, SK, a);
  TEST_ASSERT_FALSE(a.hasId);
  d.set(ok);
  d["id"] = "c123456789012345678901234567890123";  // 34 byte
  wifi::checkAdd(parsed(d.as<JsonVariantConst>()), true, SK, a);
  TEST_ASSERT_FALSE(a.hasId);
  // SSID vuoto o di 33 byte: «ssid non valido».
  d.set(ok);
  d["ssid"] = "";
  wifi::checkAdd(parsed(d.as<JsonVariantConst>()), true, SK, a);
  TEST_ASSERT_EQUAL_STRING(wifi::kErrSsid, a.error);
  d["ssid"] = "123456789012345678901234567890123";
  wifi::checkAdd(parsed(d.as<JsonVariantConst>()), true, SK, a);
  TEST_ASSERT_EQUAL_STRING(wifi::kErrSsid, a.error);
  // iv o ct mancanti.
  d.set(ok);
  d.remove("ct");
  wifi::checkAdd(parsed(d.as<JsonVariantConst>()), true, SK, a);
  TEST_ASSERT_EQUAL_STRING(wifi::kErrDecrypt, a.error);
  // Cifrato enorme: password non valida senza decifrare.
  d.set(ok);
  std::string big(2 * 200, 'a');
  d["ct"] = big;
  wifi::checkAdd(parsed(d.as<JsonVariantConst>()), true, SK, a);
  TEST_ASSERT_EQUAL_STRING(wifi::kErrPass, a.error);
}

static void test_forget_vectors() {
  for (JsonVariantConst v : V["forget"].as<JsonArrayConst>()) {
    uint8_t mac[16];
    TEST_ASSERT_TRUE(wifi::forgetMac(SK, v["id"], v["ssid"], mac));
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v["mac"].as<const char *>(), hexOf(mac, 16).c_str(), v["name"]);
    wifi::ForgetCheck f;
    wifi::checkForget(parsed(v["message"]), true, SK, f);
    TEST_ASSERT_TRUE(f.hasId);
    TEST_ASSERT_NULL_MESSAGE(f.error, v["name"]);
    TEST_ASSERT_EQUAL_STRING(v["ssid"].as<const char *>(), f.ssid);
    wifi::checkForget(parsed(v["message"]), false, SK, f);
    TEST_ASSERT_EQUAL_STRING(wifi::kErrAuth, f.error);
  }
  int n = 0;
  for (JsonVariantConst v : V["forgetNegative"].as<JsonArrayConst>()) {
    wifi::ForgetCheck f;
    wifi::checkForget(parsed(v["message"]), true, SK, f);
    TEST_ASSERT_TRUE(f.hasId);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(v["error"].as<const char *>(), f.error, v["name"]);
    n++;
  }
  TEST_ASSERT_EQUAL(4, n);
}

static void test_list_encoding_matches_vector() {
  wifi::ListEntry e[4] = {};
  JsonArrayConst nets = V["list"]["message"]["networks"].as<JsonArrayConst>();
  int n = 0;
  for (JsonVariantConst x : nets) {
    strcpy(e[n].ssid, x["ssid"]);
    e[n].current = x["current"];
    e[n].hasRssi = !x["rssi"].isNull();
    e[n].rssi = x["rssi"] | 0;
    e[n].lastUsed = x["lastUsed"].isNull() ? 0 : x["lastUsed"].as<int64_t>();
    n++;
  }
  char buf[wifi::kListMax];
  const size_t len = wifi::encodeList(e, n, buf, sizeof buf);
  TEST_ASSERT_TRUE(len > 0);
  TEST_ASSERT_EQUAL_STRING(V["list"]["json"].as<const char *>(), buf);
  // Mai password: il messaggio non ha campi oltre ssid, current, rssi, lastUsed.
  TEST_ASSERT_NULL(strstr(buf, "pass"));
  // 5 reti con SSID da 32 byte stanno nel buffer della rete.
  wifi::ListEntry big[kSavedMax];
  for (int k = 0; k < kSavedMax; k++) {
    memset(big[k].ssid, 'W', 32);
    big[k].ssid[32] = 0;
    big[k].current = k == 0;
    big[k].hasRssi = true;
    big[k].rssi = -100;
    big[k].lastUsed = 1791707400000LL;
  }
  TEST_ASSERT_TRUE(wifi::encodeList(big, kSavedMax, buf, sizeof buf) > 0);
  TEST_ASSERT_EQUAL(0, int(wifi::encodeList(big, kSavedMax, buf, 64)));  // buffer corto: niente a metà
}

// ---------------------------------------------------------------- ordine dei tentativi
static void test_round_order() {
  const char saved[4][kSsidLen] = {"CasaWiFi", "Officina", "Nascosta", "Ospiti"};
  const wifi::Seen seen[5] = {{"Vicini", -40}, {"Officina", -60}, {"CasaWiFi", -75}, {"Ospiti", -60}, {"CasaWiFi", -70}};
  wifi::Candidate c[4];
  TEST_ASSERT_EQUAL(4, wifi::roundOrder(saved, 4, seen, 5, c));
  // Visibili dalla più forte (a parità: ordine di inserimento), poi le non visibili.
  TEST_ASSERT_EQUAL(1, c[0].idx);
  TEST_ASSERT_EQUAL(-60, c[0].rssi);
  TEST_ASSERT_EQUAL(3, c[1].idx);
  TEST_ASSERT_EQUAL(0, c[2].idx);
  TEST_ASSERT_EQUAL(-70, c[2].rssi);  // più antenne con lo stesso nome: la più forte
  TEST_ASSERT_EQUAL(2, c[3].idx);
  TEST_ASSERT_FALSE(c[3].visible);
  // Ricerca vuota (o fallita): tutte nell'ordine di inserimento.
  TEST_ASSERT_EQUAL(4, wifi::roundOrder(saved, 4, seen, 0, c));
  for (int k = 0; k < 4; k++) { TEST_ASSERT_EQUAL(k, c[k].idx); TEST_ASSERT_FALSE(c[k].visible); }
  TEST_ASSERT_EQUAL(0, wifi::roundOrder(saved, 0, seen, 5, c));
}

// Un giro = un tentativo di RetrySchedule (subito, 5 s, 5 s, poi ogni 30 s), qualunque sia il numero di reti.
static void test_round_uses_retry_schedule() {
  RetrySchedule r;
  r.restart(0);
  TEST_ASSERT_TRUE(r.due(0));
  TEST_ASSERT_EQUAL(1, r.attempt(0));
  TEST_ASSERT_EQUAL_UINT32(5000, r.nextDelay());
  TEST_ASSERT_FALSE(r.due(4999));
  TEST_ASSERT_EQUAL(2, r.attempt(5000));
  TEST_ASSERT_EQUAL(3, r.attempt(10000));
  TEST_ASSERT_EQUAL_UINT32(30000, r.nextDelay());
  // Il giro 4 (dovuto a 40 s) parte a 75 s perché il 3° è durato a lungo (più reti da 20 s l'una): il 5° (dovuto a
  // 70 s) è già passato, quindi parte subito alla fine del 4°, ma una volta sola; il 6° torna a 30 s.
  TEST_ASSERT_TRUE(r.due(75000));
  TEST_ASSERT_EQUAL(4, r.attempt(75000));
  TEST_ASSERT_TRUE(r.due(75000));
  TEST_ASSERT_EQUAL(5, r.attempt(75000));
  TEST_ASSERT_FALSE(r.due(75001));
  TEST_ASSERT_TRUE(r.due(105000));
}

// ---------------------------------------------------------------- cambio automatico
static void test_roam_monitor() {
  wifi::RoamMonitor m;
  TEST_ASSERT_FALSE(m.due(1000000, false));  // senza connessione niente controlli
  m.reset(0);
  TEST_ASSERT_FALSE(m.due(59999, false));
  TEST_ASSERT_TRUE(m.due(60000, false));
  TEST_ASSERT_FALSE(m.due(60000, true));     // in Attesa ogni 5 minuti
  TEST_ASSERT_TRUE(m.due(300000, true));
  m.reading(-81);
  m.reading(-85);
  TEST_ASSERT_FALSE(m.weak());               // 2 letture deboli non bastano
  m.reading(-80);                            // −80 non è sotto −80: si ricomincia
  TEST_ASSERT_EQUAL(0, m.streak());
  m.reading(-81);
  m.reading(-90);
  m.reading(-84);
  TEST_ASSERT_TRUE(m.weak());
  TEST_ASSERT_EQUAL(-84, m.last());
  m.checked(60000);
  TEST_ASSERT_FALSE(m.due(119999, false));
  TEST_ASSERT_TRUE(m.due(120000, false));
  // millis() che gira dopo 49 giorni: la differenza regge.
  m.reset(0xFFFFF000u);
  TEST_ASSERT_FALSE(m.due(0x00000100u, false));
  TEST_ASSERT_TRUE(m.due(0xFFFFF000u + 60000u, false));
  m.stop();
  TEST_ASSERT_FALSE(m.due(0xFFFFFFF0u, false));
}

static void test_roam_target_and_blocks() {
  const char saved[3][kSsidLen] = {"CasaWiFi", "Officina", "Ospiti"};
  int r = 0;
  // In uso CasaWiFi a −84: Officina −75 (+9, non basta), Ospiti −74 (+10, basta).
  const wifi::Seen s1[3] = {{"CasaWiFi", -84}, {"Officina", -75}, {"Ospiti", -74}};
  TEST_ASSERT_EQUAL(2, wifi::roamTarget(saved, 3, "CasaWiFi", -84, s1, 3, &r));
  TEST_ASSERT_EQUAL(-74, r);
  const wifi::Seen s2[2] = {{"Officina", -75}, {"Estranea", -30}};
  TEST_ASSERT_EQUAL(-1, wifi::roamTarget(saved, 3, "CasaWiFi", -84, s2, 2, &r));  // una rete non salvata non conta
  const wifi::Seen s3[2] = {{"Officina", -60}, {"Ospiti", -50}};
  TEST_ASSERT_EQUAL(2, wifi::roamTarget(saved, 3, "CasaWiFi", -84, s3, 2, &r));     // la più forte
  TEST_ASSERT_EQUAL(-1, wifi::roamTarget(saved, 3, "Ospiti", -84, s3 + 1, 1, &r));  // la rete in uso non è un cambio
  // Blocchi: permesso aperto, aggiornamento, calibrazione.
  wifi::RoamBlock b = {false, false, false};
  TEST_ASSERT_NULL(wifi::roamBlockText(b));
  b.permOpen = true;
  TEST_ASSERT_EQUAL_STRING("richiesta di permesso aperta", wifi::roamBlockText(b));
  b = {false, true, false};
  TEST_ASSERT_EQUAL_STRING("aggiornamento in corso", wifi::roamBlockText(b));
  b = {false, false, true};
  TEST_ASSERT_EQUAL_STRING("calibrazione della batteria in corso", wifi::roamBlockText(b));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_vectors_loaded);
  RUN_TEST(test_add_key_aad_and_ciphertext);
  RUN_TEST(test_add_decrypts_vectors);
  RUN_TEST(test_add_negative_vectors);
  RUN_TEST(test_add_needs_auth_and_fields);
  RUN_TEST(test_forget_vectors);
  RUN_TEST(test_list_encoding_matches_vector);
  RUN_TEST(test_round_order);
  RUN_TEST(test_round_uses_retry_schedule);
  RUN_TEST(test_roam_monitor);
  RUN_TEST(test_roam_target_and_blocks);
  return UNITY_END();
}
