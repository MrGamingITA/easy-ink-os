// Primitive e formule di M5 contro protocol/vectors/crypto-vectors.json (generato da gen-vectors.mjs con il
// crypto di Node, cioè OpenSSL: un'implementazione indipendente). Il servizio Go legge lo stesso file.
// pio test gira con la cartella firmware/ come cwd.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/crypto.h"

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

std::string H(const uint8_t *d, size_t n) {
  std::string s(2 * n, ' ');
  std::vector<char> t(2 * n + 1);
  cry::toHex(d, n, t.data());
  return std::string(t.data());
}

void eq(JsonVariantConst want, const uint8_t *got, size_t n, const char *what) {
  TEST_ASSERT_EQUAL_STRING_MESSAGE(want.as<const char *>(), H(got, n).c_str(), what);
}

JsonVariantConst flow() { return V["flow"]; }

}  // namespace

static void test_vectors_file_loaded() {
  const std::string js = readFile("../protocol/vectors/crypto-vectors.json");
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 1000, "manca protocol/vectors/crypto-vectors.json");
  TEST_ASSERT_FALSE(deserializeJson(V, js));
  TEST_ASSERT_TRUE(V["standard"].is<JsonObjectConst>());
  TEST_ASSERT_EQUAL_STRING("calamaio-3f2a", flow()["board"]);
}

static void test_sha256_standard() {
  for (JsonVariantConst v : V["standard"]["sha256"].as<JsonArrayConst>()) {
    std::vector<uint8_t> d = B(v["data"]);
    uint8_t out[32];
    cry::sha256(d.data(), d.size(), out);
    eq(v["hash"], out, 32, v["name"]);
  }
}

static void test_hmac_standard() {
  int n = 0;
  for (JsonVariantConst v : V["standard"]["hmacSha256"].as<JsonArrayConst>()) {
    std::vector<uint8_t> k = B(v["key"]), d = B(v["data"]);
    uint8_t out[32];
    cry::hmacSha256(k.data(), k.size(), d.data(), d.size(), out);
    eq(v["mac"], out, strlen(v["mac"].as<const char *>()) / 2, v["name"]);  // #5: troncato a 128 bit
    n++;
  }
  TEST_ASSERT_EQUAL(7, n);
}

static void test_hkdf_standard() {
  for (JsonVariantConst v : V["standard"]["hkdfSha256"].as<JsonArrayConst>()) {
    std::vector<uint8_t> ikm = B(v["ikm"]), salt = B(v["salt"]), info = B(v["info"]);
    const int L = v["L"];
    std::vector<uint8_t> out(L);
    TEST_ASSERT_TRUE(cry::hkdf(salt.data(), salt.size(), ikm.data(), ikm.size(), info.data(), info.size(), out.data(), L));
    eq(v["okm"], out.data(), L, v["name"]);
  }
}

static void test_x25519_standard() {
  for (JsonVariantConst v : V["standard"]["x25519"].as<JsonArrayConst>()) {
    std::vector<uint8_t> k = B(v["scalar"]), u = B(v["u"]);
    uint8_t out[32];
    TEST_ASSERT_TRUE(cry::x25519(out, k.data(), u.data()));
    eq(v["out"], out, 32, v["name"]);
  }
  for (JsonVariantConst v : V["standard"]["x25519Base"].as<JsonArrayConst>()) {
    std::vector<uint8_t> k = B(v["scalar"]);
    uint8_t out[32];
    cry::x25519Base(out, k.data());
    eq(v["pk"], out, 32, v["name"]);
  }
}

static void test_x25519_iterated_1000() {
  JsonVariantConst v = V["standard"]["x25519Iterated"];
  uint8_t k[32] = {9}, u[32] = {9}, r[32];
  for (int i = 1; i <= 1000; i++) {
    cry::x25519(r, k, u);
    memcpy(u, k, 32);
    memcpy(k, r, 32);
    if (i == 1) eq(v["after1"], k, 32, "dopo 1");
  }
  eq(v["after1000"], k, 32, "dopo 1000");
}

static void test_x25519_low_order_rejected() {
  uint8_t sk[32], out[32];
  memset(sk, 7, sizeof sk);
  int n = 0;
  for (JsonVariantConst v : V["standard"]["x25519LowOrder"].as<JsonArrayConst>()) {
    std::vector<uint8_t> u = B(v["u"]);
    TEST_ASSERT_FALSE_MESSAGE(cry::x25519(out, sk, u.data()), v["name"]);
    n++;
  }
  TEST_ASSERT_EQUAL(3, n);
}

static void test_gcm_standard_and_tamper() {
  for (JsonVariantConst v : V["standard"]["aes256Gcm"].as<JsonArrayConst>()) {
    std::vector<uint8_t> key = B(v["key"]), iv = B(v["iv"]), aad = B(v["aad"]), pt = B(v["pt"]), ct = B(v["ct"]);
    std::vector<uint8_t> out(pt.size() + 1), back(pt.size() + 1);
    uint8_t tag[16];
    TEST_ASSERT_TRUE(cry::gcmEncrypt(key.data(), iv.data(), aad.data(), aad.size(), pt.data(), pt.size(), out.data(), tag));
    eq(v["ct"], out.data(), ct.size(), v["name"]);
    eq(v["tag"], tag, 16, v["name"]);
    TEST_ASSERT_TRUE(cry::gcmDecrypt(key.data(), iv.data(), aad.data(), aad.size(), ct.data(), ct.size(), back.data(), tag));
    TEST_ASSERT_TRUE(pt.empty() || memcmp(back.data(), pt.data(), pt.size()) == 0);
    tag[15] ^= 0x80;  // tag cambiato: rifiutato
    TEST_ASSERT_FALSE(cry::gcmDecrypt(key.data(), iv.data(), aad.data(), aad.size(), ct.data(), ct.size(), back.data(), tag));
    tag[15] ^= 0x80;
    if (!aad.empty()) {  // AAD cambiato: rifiutato
      aad[0] ^= 1;
      TEST_ASSERT_FALSE(cry::gcmDecrypt(key.data(), iv.data(), aad.data(), aad.size(), ct.data(), ct.size(), back.data(), tag));
    }
  }
}

static void test_hex_helpers() {
  uint8_t b[2];
  TEST_ASSERT_TRUE(cry::fromHex("0aff", b, 2));
  TEST_ASSERT_EQUAL_HEX8(0xff, b[1]);
  TEST_ASSERT_FALSE(cry::fromHex("0AFF", b, 2));   // solo minuscolo
  TEST_ASSERT_FALSE(cry::fromHex("0aff0", b, 2));  // lunghezza esatta
  TEST_ASSERT_FALSE(cry::fromHex("0af", b, 2));
  TEST_ASSERT_FALSE(cry::fromHex("0agf", b, 2));
  TEST_ASSERT_FALSE(cry::fromHex(nullptr, b, 2));
  char sas[7];
  m5::sasText(42, sas);
  TEST_ASSERT_EQUAL_STRING("000042", sas);
  m5::sasText(999999, sas);
  TEST_ASSERT_EQUAL_STRING("999999", sas);
  cry::Msg m;
  m.num(1791707400000LL).sep().num(-5).sep().num(0);
  TEST_ASSERT_EQUAL(18, m.size());
  TEST_ASSERT_EQUAL_MEMORY("1791707400000\x1f-5\x1f" "0", m.data(), m.size());
  std::string big(600, 'x');
  cry::Msg over;
  over.str(big.c_str());
  TEST_ASSERT_FALSE(over.ok());
}

static void test_flow_pairing() {
  JsonVariantConst p = flow()["pair"];
  const char *board = flow()["board"];
  std::vector<uint8_t> skS = B(p["skS"]), skD = B(p["skD"]);
  uint8_t pkS[32], pkD[32], c[32], Z[32], Z2[32], T[32], K[32], mac[16];
  cry::x25519Base(pkS, skS.data());
  cry::x25519Base(pkD, skD.data());
  eq(p["pkS"], pkS, 32, "pkS");
  eq(p["pkD"], pkD, 32, "pkD");
  m5::commit(pkS, c);
  eq(p["commit"], c, 32, "commit");
  TEST_ASSERT_TRUE(cry::x25519(Z, skD.data(), pkS));
  TEST_ASSERT_TRUE(cry::x25519(Z2, skS.data(), pkD));
  TEST_ASSERT_EQUAL_MEMORY(Z, Z2, 32);
  eq(p["Z"], Z, 32, "Z");
  m5::transcript(board, c, pkD, pkS, T);
  eq(p["T"], T, 32, "T");
  m5::pairKey(Z, T, K);
  eq(p["K"], K, 32, "K");
  uint8_t sb[32];
  cry::hkdf(T, 32, Z, 32, reinterpret_cast<const uint8_t *>("calamaio-sas"), 12, sb, 32);
  eq(p["sasBytes"], sb, 32, "sasBytes");
  const uint32_t code = m5::sas(Z, T);
  TEST_ASSERT_EQUAL_UINT32(p["sasInt"].as<uint32_t>() % 1000000u, code);
  char txt[7];
  m5::sasText(code, txt);
  TEST_ASSERT_EQUAL_STRING(p["sas"].as<const char *>(), txt);
  m5::pairConfirmMac(K, T, mac);
  eq(p["pairConfirmMac"], mac, 16, "pair-confirm");
  m5::pairDoneMac(K, T, mac);
  eq(p["pairDoneMac"], mac, 16, "pair-done");
  // I messaggi d'esempio portano gli stessi valori.
  JsonArrayConst msgs = p["messages"];
  TEST_ASSERT_EQUAL_STRING(p["commit"], msgs[0]["commit"]);
  TEST_ASSERT_EQUAL_STRING(p["pkD"], msgs[1]["pkD"]);
  TEST_ASSERT_EQUAL_STRING(p["pairDoneMac"], msgs[4]["mac"]);
}

static void test_flow_auth_and_session_key() {
  JsonVariantConst a = flow()["auth"];
  const char *board = flow()["board"];
  std::vector<uint8_t> K = B(flow()["pair"]["K"]), ns = B(a["ns"]), nd = B(a["nd"]);
  uint8_t mac[16], SK[32];
  m5::authMac(K.data(), board, ns.data(), nd.data(), mac);
  eq(a["authMac"], mac, 16, "auth");
  m5::authOkMac(K.data(), board, ns.data(), nd.data(), mac);
  eq(a["authOkMac"], mac, 16, "auth-ok");
  m5::sessionKey(K.data(), ns.data(), nd.data(), SK);
  eq(a["SK"], SK, 32, "SK");
}

static void test_flow_perm_macs() {
  std::vector<uint8_t> SK = B(flow()["auth"]["SK"]);
  int n = 0;
  for (JsonVariantConst r : flow()["permRequest"].as<JsonArrayConst>()) {
    JsonVariantConst f = r["fields"];
    uint8_t mac[16];
    TEST_ASSERT_TRUE(m5::reqMac(SK.data(), f["id"], f["session"], f["name"], f["tool"], f["arg"], f["at"].as<int64_t>(), mac));
    eq(r["mac"], mac, 16, "perm-request");
    // ingresso del mac byte per byte (con i separatori 0x1F)
    cry::Msg m;
    m.str("req").sep().str(f["id"]).sep().str(f["session"]).sep().str(f["name"]).sep().str(f["tool"]).sep().str(f["arg"]).sep()
        .num(f["at"].as<int64_t>());
    eq(r["macInput"], m.data(), m.size(), "ingresso perm-request");
    n++;
  }
  TEST_ASSERT_EQUAL(2, n);  // il secondo ha lettere accentate nell'argomento
  for (JsonVariantConst r : flow()["permReply"].as<JsonArrayConst>()) {
    JsonVariantConst f = r["fields"];
    uint8_t mac[16];
    TEST_ASSERT_TRUE(m5::repMac(SK.data(), f["id"], f["decision"], f["nonce"], mac));
    eq(r["mac"], mac, 16, "perm-reply");
  }
  uint8_t mac[16];
  JsonVariantConst pc = flow()["permClose"];
  TEST_ASSERT_TRUE(m5::closeMac(SK.data(), pc["fields"]["id"], pc["fields"]["reason"], mac));
  eq(pc["mac"], mac, 16, "perm-close");
  TEST_ASSERT_TRUE(m5::unpairMac(SK.data(), flow()["board"], mac));
  eq(flow()["unpair"]["mac"], mac, 16, "unpair");
}

static void test_flow_vault_records() {
  JsonVariantConst v = flow()["vault"];
  std::vector<uint8_t> ek = B(v["efuseKey"]), salt = B(v["salt"]);
  cry::Msg m;
  m.str("calamaio-nvs").raw(salt.data(), salt.size());
  uint8_t KE[32];
  cry::hmacSha256(ek.data(), ek.size(), m.data(), m.size(), KE);
  eq(v["KE"], KE, 32, "KE");
  for (JsonVariantConst r : v["records"].as<JsonArrayConst>()) {
    std::vector<uint8_t> rec = B(r["record"]), pt = B(r["plaintext"]);
    TEST_ASSERT_EQUAL(1, rec[0]);
    const char *name = r["name"];
    std::vector<uint8_t> out(rec.size() - 29);
    TEST_ASSERT_TRUE(cry::gcmDecrypt(KE, rec.data() + 1, reinterpret_cast<const uint8_t *>(name), strlen(name), rec.data() + 29,
                                     rec.size() - 29, out.data(), rec.data() + 13));
    TEST_ASSERT_EQUAL_MEMORY(pt.data(), out.data(), pt.size());
  }
}

static void test_self_test_passes() {
  char why[48] = "";
  TEST_ASSERT_TRUE_MESSAGE(cry::selfTest(why, sizeof why), why);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_vectors_file_loaded);
  RUN_TEST(test_sha256_standard);
  RUN_TEST(test_hmac_standard);
  RUN_TEST(test_hkdf_standard);
  RUN_TEST(test_x25519_standard);
  RUN_TEST(test_x25519_iterated_1000);
  RUN_TEST(test_x25519_low_order_rejected);
  RUN_TEST(test_gcm_standard_and_tamper);
  RUN_TEST(test_hex_helpers);
  RUN_TEST(test_flow_pairing);
  RUN_TEST(test_flow_auth_and_session_key);
  RUN_TEST(test_flow_perm_macs);
  RUN_TEST(test_flow_vault_records);
  RUN_TEST(test_self_test_passes);
  return UNITY_END();
}
