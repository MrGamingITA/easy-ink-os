// M5 (docs/M5-PERMESSI.md §1–3) lato display: core/link contro i messaggi d'esempio di
// protocol/vectors/crypto-vectors.json. Con lo stesso «caso» (skD, nd, nonce dei vettori) Link deve
// mandare esattamente i messaggi del file, byte per byte, e rifiutare tutto quello che non torna.
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/crypto.h"
#include "core/link.h"
#include "core/strbuf.h"

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
  std::vector<uint8_t> out(strlen(s) / 2);
  cry::fromHex(s, out.data(), out.size());
  return out;
}

// «Casuale» per i test: restituisce in ordine i byte messi in coda (poi zeri).
std::deque<std::vector<uint8_t>> gRand;
void fakeRand(uint8_t *out, size_t n, void *) {
  memset(out, 0, n);
  if (gRand.empty()) return;
  std::vector<uint8_t> b = gRand.front();
  gRand.pop_front();
  memcpy(out, b.data(), n < b.size() ? n : b.size());
}

struct Out : LinkOut {
  std::vector<std::string> sent;
  std::vector<LinkEvent> evs;
  void send(const char *json, size_t n) override { sent.emplace_back(json, n); }
  void event(const LinkEvent &e) override { evs.push_back(e); }
  void clear() { sent.clear(); evs.clear(); }
  bool has(LinkEvType t) const {
    for (const LinkEvent &e : evs)
      if (e.type == t) return true;
    return false;
  }
  const LinkEvent *find(LinkEvType t) const {
    for (const LinkEvent &e : evs)
      if (e.type == t) return &e;
    return nullptr;
  }
};

JsonDocument doc;
// Un messaggio del servizio come arriva dalla rete: parseMessage (con il filtro vero) e poi Link.
bool feed(Link &l, const std::string &json, Out &o, Ms now = 1000) {
  doc.clear();
  TEST_ASSERT_TRUE_MESSAGE(parseMessage(json.data(), json.size(), doc), json.c_str());
  return l.onMessage(doc.as<JsonVariantConst>(), now, o);
}
std::string compact(JsonVariantConst v) {
  std::string s;
  serializeJson(v, s);
  return s;
}
JsonVariantConst flow() { return V["flow"]; }
JsonVariantConst pairMsg(int k) { return flow()["pair"]["messages"][k]; }
JsonVariantConst authMsg(int k) { return flow()["auth"]["messages"][k]; }

// Abbinamento completo fino a PairReady.
void pairUntilReady(Link &l, Out &o) {
  gRand.push_back(B(flow()["pair"]["skD"]));
  feed(l, compact(pairMsg(0)), o);
  feed(l, compact(pairMsg(2)), o);
  feed(l, compact(pairMsg(3)), o);
}

// Connessione autenticata con la K dei vettori.
void authed(Link &l, Out &o) {
  std::vector<uint8_t> K = B(flow()["pair"]["K"]);
  l.setKey(K.data());
  l.onConnected();
  gRand.push_back(B(flow()["auth"]["nd"]));
  feed(l, compact(authMsg(0)), o);
  feed(l, compact(authMsg(2)), o);
}

Link *fresh() {
  static Link l;
  l = Link();
  l.begin(V["flow"]["board"].as<const char *>(), fakeRand, nullptr);
  gRand.clear();
  return &l;
}

}  // namespace

static void test_load_vectors() {
  const std::string js = readFile("../protocol/vectors/crypto-vectors.json");
  TEST_ASSERT_FALSE(deserializeJson(V, js));
  TEST_ASSERT_EQUAL_STRING("175397", flow()["pair"]["sas"]);
}

static void test_pairing_messages_match_vectors() {
  Link &l = *fresh();
  Out o;
  gRand.push_back(B(flow()["pair"]["skD"]));
  TEST_ASSERT_TRUE(feed(l, compact(pairMsg(0)), o));  // pair-begin
  TEST_ASSERT_EQUAL(1, o.sent.size());
  TEST_ASSERT_EQUAL_STRING(compact(pairMsg(1)).c_str(), o.sent[0].c_str());  // pair-key, byte per byte
  TEST_ASSERT_TRUE(l.pairing());
  o.clear();
  feed(l, compact(pairMsg(2)), o);  // pair-reveal
  const LinkEvent *code = o.find(LinkEvType::PairCode);
  TEST_ASSERT_NOT_NULL(code);
  TEST_ASSERT_EQUAL_STRING(flow()["pair"]["sas"].as<const char *>(), code->text);
  TEST_ASSERT_EQUAL(0, o.sent.size());
  TEST_ASSERT_FALSE(l.confirmPair(o));  // prima di pair-confirm non si conferma
  o.clear();
  feed(l, compact(pairMsg(3)), o);  // pair-confirm
  TEST_ASSERT_TRUE(o.has(LinkEvType::PairReady));
  TEST_ASSERT_FALSE(l.hasKey());  // K non è salvata finché l'utente non conferma
  o.clear();
  TEST_ASSERT_TRUE(l.confirmPair(o));
  TEST_ASSERT_EQUAL_STRING(compact(pairMsg(4)).c_str(), o.sent[0].c_str());  // pair-done
  TEST_ASSERT_TRUE(o.has(LinkEvType::PairDone));
  TEST_ASSERT_TRUE(l.hasKey());
  TEST_ASSERT_EQUAL_MEMORY(B(flow()["pair"]["K"]).data(), l.key(), 32);
  TEST_ASSERT_FALSE(l.pairing());
}

static void test_pairing_wrong_code_saves_nothing() {
  Link &l = *fresh();
  Out o;
  gRand.push_back(B(flow()["pair"]["skD"]));
  feed(l, compact(pairMsg(0)), o);
  feed(l, compact(pairMsg(2)), o);
  o.clear();
  feed(l, compact(flow()["pair"]["wrongCode"]), o);  // il PC: codice sbagliato
  const LinkEvent *e = o.find(LinkEvType::PairAbort);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_STRING("codice", e->text);
  TEST_ASSERT_FALSE(l.pairing());
  TEST_ASSERT_FALSE(l.hasKey());
  TEST_ASSERT_FALSE(l.confirmPair(o));
}

static void test_pairing_bad_commit_and_bad_mac() {
  Link &l = *fresh();
  Out o;
  gRand.push_back(B(flow()["pair"]["skD"]));
  feed(l, compact(pairMsg(0)), o);
  o.clear();
  // pkS diversa da quella impegnata: chi sta in mezzo non può cambiarla dopo aver visto pkD.
  feed(l, R"({"t":"pair-reveal","v":1,"id":"p7","pkS":"de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"})", o);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"pair-abort","v":1,"id":"p7","reason":"impegno"})", o.sent[0].c_str());
  TEST_ASSERT_FALSE(l.pairing());
  TEST_ASSERT_FALSE(o.has(LinkEvType::PairCode));

  Link &l2 = *fresh();
  o.clear();
  gRand.push_back(B(flow()["pair"]["skD"]));
  feed(l2, compact(pairMsg(0)), o);
  feed(l2, compact(pairMsg(2)), o);
  o.clear();
  feed(l2, R"({"t":"pair-confirm","v":1,"id":"p7","mac":"00000000000000000000000000000000"})", o);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"pair-abort","v":1,"id":"p7","reason":"mac"})", o.sent[0].c_str());
  TEST_ASSERT_FALSE(o.has(LinkEvType::PairReady));
  TEST_ASSERT_FALSE(l2.confirmPair(o));
  TEST_ASSERT_FALSE(l2.hasKey());
}

static void test_pairing_out_of_order_and_malformed() {
  Link &l = *fresh();
  Out o;
  feed(l, compact(pairMsg(2)), o);  // reveal senza begin
  feed(l, compact(pairMsg(3)), o);  // confirm senza begin
  TEST_ASSERT_EQUAL(0, o.sent.size());
  TEST_ASSERT_TRUE(o.has(LinkEvType::Rejected));
  o.clear();
  feed(l, R"({"t":"pair-begin","v":1,"id":"p7","commit":"EA9DC0CDC6917DCCA45D2DF3178FFB59934FAC89CA1F86939054444EA7E71571"})", o);  // maiuscolo
  feed(l, R"({"t":"pair-begin","v":1,"id":"","commit":"ea9dc0cdc6917dcca45d2df3178ffb59934fac89ca1f86939054444ea7e71571"})", o);
  feed(l, R"({"t":"pair-begin","v":1,"id":"p7","commit":"ea9d"})", o);
  TEST_ASSERT_EQUAL(0, o.sent.size());
  TEST_ASSERT_FALSE(l.pairing());
  o.clear();
  pairUntilReady(l, o);
  o.clear();
  feed(l, R"({"t":"pair-reveal","v":1,"id":"p8","pkS":"b3d8cf94a51157d4feff5c8b6a652c96dec112b4ca2dc71d663821b4a248857b"})", o);  // id diverso
  TEST_ASSERT_TRUE(o.has(LinkEvType::Rejected));
  TEST_ASSERT_TRUE(l.pairReady());  // l'abbinamento giusto resta
}

static void test_pairing_cancel_timeout_disconnect() {
  Link &l = *fresh();
  Out o;
  pairUntilReady(l, o);
  o.clear();
  l.cancelPair("display", o);  // pressione lunga sul display
  TEST_ASSERT_EQUAL_STRING(R"({"t":"pair-abort","v":1,"id":"p7","reason":"display"})", o.sent[0].c_str());
  TEST_ASSERT_FALSE(l.hasKey());

  o.clear();
  gRand.push_back(B(flow()["pair"]["skD"]));
  feed(l, compact(pairMsg(0)), o, 1000);
  o.clear();
  l.tick(1000 + kPairLinkMs, o);
  TEST_ASSERT_EQUAL(0, o.sent.size());
  l.tick(1000 + kPairLinkMs + 1, o);  // oltre i 3 minuti (+5 s: di solito chiude prima il servizio)
  TEST_ASSERT_EQUAL_STRING(R"({"t":"pair-abort","v":1,"id":"p7","reason":"tempo"})", o.sent[0].c_str());

  o.clear();
  pairUntilReady(l, o);
  o.clear();
  l.onDisconnected(o);
  const LinkEvent *e = o.find(LinkEvType::PairAbort);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_STRING("connessione", e->text);
  TEST_ASSERT_FALSE(l.confirmPair(o));
}

static void test_auth_messages_match_vectors() {
  Link &l = *fresh();
  Out o;
  std::vector<uint8_t> K = B(flow()["pair"]["K"]);
  l.setKey(K.data());
  l.onConnected();
  gRand.push_back(B(flow()["auth"]["nd"]));
  feed(l, compact(authMsg(0)), o);
  TEST_ASSERT_EQUAL_STRING(compact(authMsg(1)).c_str(), o.sent[0].c_str());  // auth, byte per byte
  TEST_ASSERT_FALSE(l.authed());
  feed(l, compact(authMsg(2)), o);
  TEST_ASSERT_TRUE(o.has(LinkEvType::Authed));
  TEST_ASSERT_TRUE(l.authed());
  l.onConnected();  // connessione nuova: si ricomincia
  TEST_ASSERT_FALSE(l.authed());
}

static void test_auth_failures_mean_redo() {
  Link &l = *fresh();
  Out o;
  std::vector<uint8_t> K = B(flow()["pair"]["K"]);
  feed(l, compact(authMsg(0)), o);  // sfida senza chiave sul display
  TEST_ASSERT_TRUE(o.has(LinkEvType::Redo));
  TEST_ASSERT_EQUAL(0, o.sent.size());
  o.clear();
  l.setKey(K.data());
  feed(l, compact(authMsg(2)), o);  // auth-ok senza sfida
  TEST_ASSERT_FALSE(l.authed());
  o.clear();
  gRand.push_back(B(flow()["auth"]["nd"]));
  feed(l, compact(authMsg(0)), o);
  feed(l, R"({"t":"auth-ok","v":1,"mac":"ffffffffffffffffffffffffffffffff"})", o);  // mac sbagliato
  TEST_ASSERT_FALSE(l.authed());
  TEST_ASSERT_TRUE(o.has(LinkEvType::Redo));
  o.clear();
  l.onWelcome(true, false, o);  // il display ha K, il PC no
  TEST_ASSERT_TRUE(o.has(LinkEvType::Redo));
  o.clear();
  l.onWelcome(false, false, o);  // servizio senza M5: niente da dire
  l.onWelcome(true, true, o);
  TEST_ASSERT_EQUAL(0, o.evs.size());
  feed(l, R"({"t":"auth-fail","v":1,"reason":"mac"})", o);
  TEST_ASSERT_TRUE(o.has(LinkEvType::Redo));
}

static void test_perm_request_verified_and_reply_matches_vectors() {
  Link &l = *fresh();
  Out o;
  authed(l, o);
  o.clear();
  for (JsonVariantConst r : flow()["permRequest"].as<JsonArrayConst>()) {
    feed(l, compact(r["message"]), o);
    const LinkEvent *e = o.find(LinkEvType::PermRequest);
    TEST_ASSERT_NOT_NULL_MESSAGE(e, r["fields"]["arg"].as<const char *>());
    TEST_ASSERT_EQUAL_STRING(r["fields"]["id"].as<const char *>(), e->req.id);
    TEST_ASSERT_EQUAL_STRING(r["fields"]["tool"].as<const char *>(), e->req.tool);
    TEST_ASSERT_EQUAL_STRING(r["fields"]["arg"].as<const char *>(), e->req.arg);  // anche con «città»
    TEST_ASSERT_EQUAL_STRING("web-app", e->req.name);
    TEST_ASSERT_EQUAL_INT64(r["fields"]["at"].as<int64_t>(), e->req.at);
    o.clear();
  }
  int k = 0;
  for (JsonVariantConst r : flow()["permReply"].as<JsonArrayConst>()) {
    gRand.push_back(B(r["fields"]["nonce"]));
    TEST_ASSERT_TRUE(l.reply(r["fields"]["id"], streq(r["fields"]["decision"], "allow"), o));
    TEST_ASSERT_EQUAL_STRING(compact(r["message"]).c_str(), o.sent[k++].c_str());  // perm-reply, byte per byte
  }
}

static void test_perm_request_rejections() {
  Link &l = *fresh();
  Out o;
  const std::string good = compact(flow()["permRequest"][0]["message"]);
  feed(l, good, o);  // connessione non autenticata
  TEST_ASSERT_FALSE(o.has(LinkEvType::PermRequest));
  authed(l, o);
  o.clear();
  JsonDocument m;
  auto variant = [&](const char *key, const char *val) {
    deserializeJson(m, good);
    m[key] = val;
    std::string s;
    serializeJson(m, s);
    return s;
  };
  // Ogni campo nel mac: cambiarne uno qualsiasi fa scartare la richiesta.
  for (const char *k : {"session", "tool", "arg"}) {
    feed(l, variant(k, "altro"), o);
    TEST_ASSERT_FALSE_MESSAGE(o.has(LinkEvType::PermRequest), k);
  }
  feed(l, variant("id", "00000000000000000000000000000000"), o);
  feed(l, variant("mac", "00000000000000000000000000000000"), o);
  feed(l, variant("mac", "nope"), o);
  deserializeJson(m, good);
  m["at"] = m["at"].as<int64_t>() + 1;
  std::string s;
  serializeJson(m, s);
  feed(l, s, o);
  std::string longArg(97, 'x');  // oltre 96 byte
  feed(l, variant("arg", longArg.c_str()), o);
  TEST_ASSERT_FALSE(o.has(LinkEvType::PermRequest));
  // name è nel mac (§3, corretto): cambiarlo fa scartare la richiesta, come i casi negativi dei vettori.
  feed(l, variant("name", "altro-nome"), o);
  feed(l, compact(flow()["negative"]["permRequestNameAltered"]["message"]), o);
  feed(l, compact(flow()["negative"]["permRequestNameTooLong"]["message"]), o);
  TEST_ASSERT_FALSE(o.has(LinkEvType::PermRequest));
  feed(l, good, o);
  const LinkEvent *e = o.find(LinkEvType::PermRequest);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_STRING("web-app", e->req.name);
  o.clear();
  feed(l, good, o);  // stessa richiesta ripetuta sulla stessa connessione
  TEST_ASSERT_FALSE(o.has(LinkEvType::PermRequest));
  TEST_ASSERT_TRUE(o.has(LinkEvType::Rejected));
}

static void test_perm_close_and_unpair() {
  Link &l = *fresh();
  Out o;
  feed(l, compact(flow()["permClose"]["message"]), o);  // senza auth: ignorato
  TEST_ASSERT_FALSE(o.has(LinkEvType::PermClose));
  authed(l, o);
  o.clear();
  // perm-close senza mac o con un mac sbagliato: ignorato (e nel log).
  feed(l, compact(flow()["negative"]["permCloseNoMac"]["message"]), o);
  feed(l, compact(flow()["negative"]["permCloseBadMac"]["message"]), o);
  TEST_ASSERT_FALSE(o.has(LinkEvType::PermClose));
  TEST_ASSERT_TRUE(o.has(LinkEvType::Rejected));
  o.clear();
  feed(l, compact(flow()["permClose"]["message"]), o);
  const LinkEvent *e = o.find(LinkEvType::PermClose);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_STRING("terminal", e->text);
  TEST_ASSERT_EQUAL_STRING(flow()["permClose"]["message"]["id"].as<const char *>(), e->id);
  o.clear();
  feed(l, compact(flow()["permRequest"][0]["message"]), o);  // già chiusa: non torna
  TEST_ASSERT_FALSE(o.has(LinkEvType::PermRequest));
  o.clear();
  l.unpair(o);
  TEST_ASSERT_EQUAL_STRING(compact(flow()["unpair"]["message"]).c_str(), o.sent[0].c_str());
  TEST_ASSERT_TRUE(o.has(LinkEvType::Unpaired));
  TEST_ASSERT_FALSE(l.hasKey());
  TEST_ASSERT_FALSE(l.authed());
  TEST_ASSERT_FALSE(l.reply(flow()["permRequest"][1]["fields"]["id"], true, o));  // niente più risposte
}

static void test_reply_needs_auth() {
  Link &l = *fresh();
  Out o;
  TEST_ASSERT_FALSE(l.reply("8f1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e", false, o));
  TEST_ASSERT_EQUAL(0, o.sent.size());
  authed(l, o);
  o.clear();
  TEST_ASSERT_FALSE(l.reply("NONHEX", false, o));
  TEST_ASSERT_EQUAL(0, o.sent.size());
}

static void test_other_messages_not_consumed() {
  Link &l = *fresh();
  Out o;
  TEST_ASSERT_FALSE(feed(l, R"({"t":"state","v":1,"time":1,"sessions":[]})", o));
  TEST_ASSERT_FALSE(feed(l, R"({"t":"ack","v":1,"id":"d1","ok":true})", o));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_load_vectors);
  RUN_TEST(test_pairing_messages_match_vectors);
  RUN_TEST(test_pairing_wrong_code_saves_nothing);
  RUN_TEST(test_pairing_bad_commit_and_bad_mac);
  RUN_TEST(test_pairing_out_of_order_and_malformed);
  RUN_TEST(test_pairing_cancel_timeout_disconnect);
  RUN_TEST(test_auth_messages_match_vectors);
  RUN_TEST(test_auth_failures_mean_redo);
  RUN_TEST(test_perm_request_verified_and_reply_matches_vectors);
  RUN_TEST(test_perm_request_rejections);
  RUN_TEST(test_perm_close_and_unpair);
  RUN_TEST(test_reply_needs_auth);
  RUN_TEST(test_other_messages_not_consumed);
  return UNITY_END();
}
