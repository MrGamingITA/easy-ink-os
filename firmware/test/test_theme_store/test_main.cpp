// Temi sulla scheda: installazione tutto o niente, ordine e nono tema, messaggi (PROTOCOLLO.md «Temi»).
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/theme_proto.h"
#include "core/theme_runtime.h"
#include "core/theme_store.h"

using namespace cal;
using namespace cal::theme;

void setUp() {}
void tearDown() {}

static std::string repoRoot() {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  return k != std::string::npos ? f.substr(0, k) : "../";
}
static std::vector<uint8_t> load(const char *name) {
  std::ifstream in(repoRoot() + "protocol/themes/fixtures/" + name, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
static uint32_t crcOf(const std::vector<uint8_t> &f) { return f[8] | f[9] << 8 | f[10] << 16 | uint32_t(f[11]) << 24; }
// Lo stesso tema con un altro id (intestazione e CRC rifatti): per riempire la scheda.
static std::vector<uint8_t> withId(const char *id) {
  std::vector<uint8_t> f = load("prova.cthm");
  memset(f.data() + 20, 0, 24);
  memcpy(f.data() + 20, id, strlen(id));
  const uint32_t c = crc32Update(0, f.data() + 12, f.size() - 12);
  f[8] = c; f[9] = c >> 8; f[10] = c >> 16; f[11] = c >> 24;
  return f;
}

static MemStorage gSt;
static uint8_t gScratch[kMaxBlock];

static bool install(const char *id, const std::vector<uint8_t> &f, const char *active = "", char *why = nullptr) {
  char w[96] = "";
  Installer in;
  bool ok = in.begin(gSt, id, crcOf(f), uint32_t(f.size()), w, sizeof w);
  for (size_t off = 0; ok && off < f.size(); off += 500)  // a pezzi, come dalla rete
    ok = in.write(f.data() + off, f.size() - off < 500 ? f.size() - off : 500, w, sizeof w);
  ok = ok && in.finish(active, gScratch, w, sizeof w);
  if (!ok) in.abort();
  if (why) strcpy(why, w);
  return ok;
}

static void test_install_ok_and_catalog() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  CatalogEntry e[kMaxThemes];
  TEST_ASSERT_EQUAL(1, catalog(gSt, e, kMaxThemes));
  TEST_ASSERT_EQUAL_STRING("Mascotte grande", e[0].name);
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());  // il temporaneo non resta
}

static void test_install_rejects() {
  gSt.eraseAll();
  char why[96];
  std::vector<uint8_t> f = load("prova.cthm");
  // offerta con un altro id: il file non è quello offerto
  TEST_ASSERT_FALSE(install("altro", f, "", why));
  TEST_ASSERT_EQUAL_STRING("il tema non è quello offerto", why);
  // un byte rovinato
  std::vector<uint8_t> bad = f;
  bad[bad.size() / 2] ^= 0x40;
  Installer in;
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(bad.data(), bad.size(), why, sizeof why));
  TEST_ASSERT_FALSE(in.finish("", gScratch, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("CRC sbagliato", why);
  in.abort();
  // troppo lungo per l'offerta
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), 100, why, sizeof why));
  TEST_ASSERT_FALSE(in.write(f.data(), 200, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("più byte di quelli offerti", why);
  in.abort();
  // offerta oltre 64 KB o id non valido: rifiutata subito
  TEST_ASSERT_FALSE(in.begin(gSt, "prova", 1, 70000, why, sizeof why));
  TEST_ASSERT_FALSE(in.begin(gSt, "Prova!", 1, 100, why, sizeof why));
  CatalogEntry none[kMaxThemes];
  TEST_ASSERT_EQUAL(0, catalog(gSt, none, kMaxThemes));
}

static void test_install_abort_keeps_old() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  Installer in;
  char why[96];
  std::vector<uint8_t> f = load("prova.cthm");
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), 300, why, sizeof why));  // la rete cade a metà
  in.abort();
  Header h;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "prova", gScratch, &h)));
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());
  // un download che si ferma prima della fine non si installa
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size() - 1, why, sizeof why));
  TEST_ASSERT_FALSE(in.finish("", gScratch, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("download incompleto", why);
  in.abort();
}

static void test_ninth_theme_evicts_oldest_not_active() {
  gSt.eraseAll();
  const char *ids[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"};
  for (const char *id : ids) TEST_ASSERT_TRUE(install(id, withId(id)));
  CatalogEntry all[kMaxThemes];
  TEST_ASSERT_EQUAL(8, catalog(gSt, all, kMaxThemes));
  TEST_ASSERT_TRUE(install("t9", withId("t9"), "t1"));  // t1 attivo: esce t2, il più vecchio non attivo
  char got[kMaxThemes + 1][theme::kIdLen];
  const int n = orderList(gSt, got, kMaxThemes + 1);
  TEST_ASSERT_EQUAL(8, n);
  TEST_ASSERT_EQUAL_STRING("t1", got[0]);
  TEST_ASSERT_EQUAL_STRING("t3", got[1]);
  TEST_ASSERT_EQUAL_STRING("t9", got[7]);
  TEST_ASSERT_EQUAL(-1, gSt.size("t2"));
  // reinstallare un tema che c'è già non toglie niente e lo porta in fondo
  TEST_ASSERT_TRUE(install("t3", withId("t3"), "t1"));
  TEST_ASSERT_EQUAL(8, orderList(gSt, got, kMaxThemes + 1));
  TEST_ASSERT_EQUAL_STRING("t3", got[7]);
}

// Tutto o niente anche col nono tema: si salva prima e si toglie dopo, quindi una rinomina che fallisce lascia gli
// otto temi com'erano (col vecchio ordine «togli, poi salva» t2 sparirebbe e t9 non ci sarebbe).
static void test_ninth_commit_fails_keeps_all() {
  gSt.eraseAll();
  const char *ids[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"};
  for (const char *id : ids) TEST_ASSERT_TRUE(install(id, withId(id)));
  std::vector<uint8_t> f = withId("t9");
  char why[96];
  Installer in;
  TEST_ASSERT_TRUE(in.begin(gSt, "t9", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size(), why, sizeof why));
  gSt.failWrites = true;  // la flash rifiuta la rinomina (tmpCommit) e l'ordine
  TEST_ASSERT_FALSE(in.finish("t1", gScratch, why, sizeof why));
  gSt.failWrites = false;
  TEST_ASSERT_EQUAL_STRING("salvataggio non riuscito", why);
  in.abort();
  char got[kMaxThemes + 1][theme::kIdLen];
  TEST_ASSERT_EQUAL(8, orderList(gSt, got, kMaxThemes + 1));
  TEST_ASSERT_EQUAL_STRING("t1", got[0]);
  TEST_ASSERT_EQUAL_STRING("t2", got[1]);  // il più vecchio non attivo è ancora lì
  TEST_ASSERT_EQUAL_STRING("t8", got[7]);
  TEST_ASSERT_EQUAL(-1, gSt.size("t9"));
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());
}

// Controllo e salvataggio separati (B5): il task di rete tiene il lucchetto solo attorno a commit(). Restano tutto o
// niente: commit senza verify rifiutato, commit fallito chiude, il nono esce solo dopo il salvataggio, e l'ordine si
// legge in commit (un tema tolto dal loop fra i due passi non fa uscire un altro tema).
static void test_verify_then_commit() {
  gSt.eraseAll();
  const char *ids[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"};
  for (const char *id : ids) TEST_ASSERT_TRUE(install(id, withId(id)));
  std::vector<uint8_t> f = withId("t9");
  char why[96];
  Installer in;
  // commit senza verify: rifiutato e chiuso
  TEST_ASSERT_TRUE(in.begin(gSt, "t9", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size(), why, sizeof why));
  TEST_ASSERT_FALSE(in.commit("t1", why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("tema non controllato", why);
  TEST_ASSERT_FALSE(in.busy());
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());
  // verify fallito (CRC): chiuso, commit dopo rifiutato
  std::vector<uint8_t> bad = f;
  bad[bad.size() / 2] ^= 0x40;
  TEST_ASSERT_TRUE(in.begin(gSt, "t9", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(bad.data(), bad.size(), why, sizeof why));
  TEST_ASSERT_FALSE(in.verify(gScratch, why, sizeof why));
  TEST_ASSERT_FALSE(in.busy());
  TEST_ASSERT_FALSE(in.commit("t1", why, sizeof why));
  // verify ok, commit fallito: chiuso, gli otto temi com'erano
  TEST_ASSERT_TRUE(in.begin(gSt, "t9", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size(), why, sizeof why));
  TEST_ASSERT_TRUE(in.verify(gScratch, why, sizeof why));
  TEST_ASSERT_TRUE(in.busy());
  gSt.failWrites = true;
  TEST_ASSERT_FALSE(in.commit("t1", why, sizeof why));
  gSt.failWrites = false;
  TEST_ASSERT_EQUAL_STRING("salvataggio non riuscito", why);
  TEST_ASSERT_FALSE(in.busy());
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());
  TEST_ASSERT_EQUAL(-1, gSt.size("t9"));
  TEST_ASSERT_TRUE(gSt.size("t2") > 0);
  // verify ok, il loop toglie t5 (tema rotto) prima del commit: niente esce, t9 in fondo
  TEST_ASSERT_TRUE(in.begin(gSt, "t9", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size(), why, sizeof why));
  TEST_ASSERT_TRUE(in.verify(gScratch, why, sizeof why));
  gSt.remove("t5");
  orderDrop(gSt, "t5");
  TEST_ASSERT_TRUE(in.commit("t1", why, sizeof why));
  TEST_ASSERT_FALSE(in.busy());
  char got[kMaxThemes + 1][theme::kIdLen];
  TEST_ASSERT_EQUAL(8, orderList(gSt, got, kMaxThemes + 1));
  TEST_ASSERT_EQUAL_STRING("t2", got[1]);
  TEST_ASSERT_EQUAL_STRING("t9", got[7]);
  // ancora uno: ora sono nove, esce t2 (il più vecchio non attivo) dopo il salvataggio
  std::vector<uint8_t> g = withId("ta");
  TEST_ASSERT_TRUE(in.begin(gSt, "ta", crcOf(g), uint32_t(g.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(g.data(), g.size(), why, sizeof why));
  TEST_ASSERT_TRUE(in.verify(gScratch, why, sizeof why));
  TEST_ASSERT_TRUE(in.commit("t1", why, sizeof why));
  TEST_ASSERT_EQUAL(8, orderList(gSt, got, kMaxThemes + 1));
  TEST_ASSERT_EQUAL(-1, gSt.size("t2"));
  TEST_ASSERT_EQUAL_STRING("t1", got[0]);
  TEST_ASSERT_EQUAL_STRING("ta", got[7]);
}

// Un archivio con più file .cthm di quelli che list() restituisce in una volta (residui, file messi a mano):
// dropAllThemes li toglie tutti, insieme al temporaneo. Simulato: list() ne dà al massimo 3 per chiamata.
class ManyStorage : public MemStorage {
 public:
  int list(char (*ids)[theme::kIdLen], int cap) override { return MemStorage::list(ids, cap < 3 ? cap : 3); }
};
static ManyStorage gMany;

static void test_drop_all_themes() {
  gMany.eraseAll();
  const char *ids[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9"};
  for (const char *id : ids) {
    std::vector<uint8_t> f = withId(id);
    TEST_ASSERT_TRUE(gMany.tmpBegin());
    TEST_ASSERT_TRUE(gMany.tmpAppend(f.data(), uint32_t(f.size())));
    TEST_ASSERT_TRUE(gMany.tmpCommit(id));
  }
  TEST_ASSERT_TRUE(gMany.tmpBegin());
  dropAllThemes(gMany);
  char got[kMaxThemes + 1][theme::kIdLen];
  TEST_ASSERT_EQUAL(0, gMany.MemStorage::list(got, kMaxThemes + 1));
  TEST_ASSERT_EQUAL(-1, gMany.tmpSize());
}

static void test_use_unknown_theme() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  TEST_ASSERT_TRUE(canUse(gSt, ""));
  TEST_ASSERT_TRUE(canUse(gSt, "prova"));
  TEST_ASSERT_FALSE(canUse(gSt, "manca"));
}

static void test_messages() {
  JsonDocument doc;
  const char *offer = R"({"t":"theme-offer","v":1,"id":"prova","crc":2864434397,"size":3120,"token":"0123456789abcdef0123456789abcdef"})";
  TEST_ASSERT_TRUE(parseMessage(offer, strlen(offer), doc));
  TEST_ASSERT_EQUAL(int(MsgType::ThemeOffer), int(msgType(doc.as<JsonVariantConst>())));
  Offer o;
  TEST_ASSERT_TRUE(decodeOffer(doc.as<JsonVariantConst>(), o));
  TEST_ASSERT_EQUAL_STRING("prova", o.id);
  TEST_ASSERT_EQUAL_UINT32(2864434397u, o.crc);
  TEST_ASSERT_EQUAL_UINT32(3120, o.size);
  const char *badTok = R"({"t":"theme-offer","v":1,"id":"prova","crc":1,"size":3120,"token":"0123"})";
  parseMessage(badTok, strlen(badTok), doc);
  TEST_ASSERT_FALSE(decodeOffer(doc.as<JsonVariantConst>(), o));
  char id[theme::kIdLen];
  const char *use = R"({"t":"theme-use","v":1,"id":null})";
  parseMessage(use, strlen(use), doc);
  TEST_ASSERT_EQUAL(int(MsgType::ThemeUse), int(msgType(doc.as<JsonVariantConst>())));
  TEST_ASSERT_TRUE(decodeUse(doc.as<JsonVariantConst>(), id, sizeof id));
  TEST_ASSERT_EQUAL_STRING("", id);
  char out[512];
  CatalogEntry e[1];
  memset(e, 0, sizeof e);
  strcpy(e[0].id, "prova");
  e[0].crc = 2864434397u;
  TEST_ASSERT_TRUE(encodeThemes(e, 1, "prova", out, sizeof out) > 0);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"themes","v":1,"have":[{"id":"prova","crc":2864434397}],"cur":"prova"})", out);
  encodeThemes(e, 0, "", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"themes","v":1,"have":[],"cur":null})", out);
  encodeAck("prova", false, "CRC sbagliato", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"theme-ack","v":1,"id":"prova","ok":false,"error":"CRC sbagliato"})", out);
  TEST_ASSERT_TRUE(downloadUrl("192.168.1.20", 8766, o.token, out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.20:8766/themes/0123456789abcdef0123456789abcdef", out);
}

// §7 lato scheda: theme-offer e theme-use valgono solo da una connessione autenticata; l'autenticazione si guarda prima
// dell'«occupato» (una connessione non autenticata non deve sapere se c'è un download in corso).
static void test_offer_only_when_authed() {
  TEST_ASSERT_EQUAL(int(OfferVerdict::NotAuthed), int(acceptOffer(false, false)));
  TEST_ASSERT_EQUAL(int(OfferVerdict::NotAuthed), int(acceptOffer(false, true)));
  TEST_ASSERT_EQUAL(int(OfferVerdict::Busy), int(acceptOffer(true, true)));
  TEST_ASSERT_EQUAL(int(OfferVerdict::Accept), int(acceptOffer(true, false)));
  TEST_ASSERT_FALSE(acceptUse(false));
  TEST_ASSERT_TRUE(acceptUse(true));
}

// Rete non fidata: theme-offer e theme-use fuori dai limiti si scartano.
static void test_offer_and_use_bounds() {
  JsonDocument doc;
  Offer o;
  const char *bad[] = {
      R"({"t":"theme-offer","v":1,"id":"prova","crc":1,"size":70000,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"prova","crc":1,"size":10,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"prova","crc":-1,"size":3120,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"prova","crc":4294967296,"size":3120,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"prova","crc":1,"size":"3120","token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"Prova","crc":1,"size":3120,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"a--b","crc":1,"size":3120,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"abcdefghijklmnopqrstuvwxy","crc":1,"size":3120,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"prova","crc":1,"size":3120,"token":"0123456789ABCDEF0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"prova","crc":1,"size":3120,"token":"0123456789abcdef0123456789abcdef0"})",
      R"({"t":"theme-offer","v":1,"id":"prova","crc":1,"size":3120,"token":"../../etc/passwd/0123456789abcdef"})",
      R"({"t":"theme-offer","v":2,"id":"prova","crc":1,"size":3120,"token":"0123456789abcdef0123456789abcdef"})",
  };
  for (const char *m : bad) {
    TEST_ASSERT_TRUE(parseMessage(m, strlen(m), doc));
    TEST_ASSERT_FALSE_MESSAGE(decodeOffer(doc.as<JsonVariantConst>(), o), m);
  }
  const char *max = R"({"t":"theme-offer","v":1,"id":"abcdefghijklmnopqrstuvwx","crc":4294967295,"size":65536,"token":"0123456789abcdef0123456789abcdef"})";
  TEST_ASSERT_TRUE(parseMessage(max, strlen(max), doc));
  TEST_ASSERT_TRUE(decodeOffer(doc.as<JsonVariantConst>(), o));
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmnopqrstuvwx", o.id);
  TEST_ASSERT_EQUAL_UINT32(4294967295u, o.crc);
  char id[theme::kIdLen];
  const char *badUse[] = {R"({"t":"theme-use","v":1,"id":"No"})", R"({"t":"theme-use","v":1,"id":7})",
                          R"({"t":"theme-use","v":1})", R"({"t":"theme-use","v":1,"id":"-a"})"};
  for (const char *m : badUse) {
    TEST_ASSERT_TRUE(parseMessage(m, strlen(m), doc));
    TEST_ASSERT_FALSE_MESSAGE(decodeUse(doc.as<JsonVariantConst>(), id, sizeof id), m);
  }
  const char *use = R"({"t":"theme-use","v":1,"id":"lista"})";
  TEST_ASSERT_TRUE(parseMessage(use, strlen(use), doc));
  TEST_ASSERT_TRUE(decodeUse(doc.as<JsonVariantConst>(), id, sizeof id));
  TEST_ASSERT_EQUAL_STRING("lista", id);
  char small[4];
  TEST_ASSERT_FALSE(decodeUse(doc.as<JsonVariantConst>(), small, sizeof small));
}

// themes deve passare il controllo rigido del servizio (parseThemes): al massimo 8 voci, id validi, cur valido o null.
static void test_themes_strict_for_service() {
  CatalogEntry e[kMaxThemes + 2];
  memset(e, 0, sizeof e);
  for (int i = 0; i < kMaxThemes + 2; i++) {
    snprintf(e[i].id, sizeof e[i].id, "t%d", i);
    e[i].crc = uint32_t(i);
  }
  strcpy(e[0].id, "Rotto!");
  char out[1024];
  TEST_ASSERT_TRUE(encodeThemes(e, kMaxThemes + 2, "Rotto!", out, sizeof out) > 0);
  TEST_ASSERT_EQUAL_STRING(
      R"({"t":"themes","v":1,"have":[{"id":"t1","crc":1},{"id":"t2","crc":2},{"id":"t3","crc":3},{"id":"t4","crc":4},)"
      R"({"id":"t5","crc":5},{"id":"t6","crc":6},{"id":"t7","crc":7},{"id":"t8","crc":8}],"cur":null})",
      out);
  // buffer troppo piccolo: 0, mai un JSON troncato
  TEST_ASSERT_EQUAL(0, int(encodeThemes(e, 2, "t1", out, 20)));
  TEST_ASSERT_EQUAL(0, int(encodeAck("prova", true, "", out, 10)));
  TEST_ASSERT_FALSE(downloadUrl("192.168.1.20", 8766, "0123456789abcdef0123456789abcdef", out, 20));
}

// Un fallimento chiude l'installazione da solo: chi dimentica abort() non resta «occupato» per sempre.
static void test_failure_closes_installer() {
  gSt.eraseAll();
  std::vector<uint8_t> f = load("prova.cthm");
  char why[96];
  Installer in;
  // finish fallito (download incompleto), nessun abort()
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size() - 1, why, sizeof why));
  TEST_ASSERT_FALSE(in.finish("", gScratch, why, sizeof why));
  TEST_ASSERT_FALSE(in.busy());
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());
  // finish fallito (CRC), nessun abort()
  std::vector<uint8_t> bad = f;
  bad[bad.size() / 2] ^= 0x40;
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(bad.data(), bad.size(), why, sizeof why));
  TEST_ASSERT_FALSE(in.finish("", gScratch, why, sizeof why));
  TEST_ASSERT_FALSE(in.busy());
  // write fallito (troppi byte), nessun abort()
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), 100, why, sizeof why));
  TEST_ASSERT_FALSE(in.write(f.data(), 200, why, sizeof why));
  TEST_ASSERT_FALSE(in.busy());
  TEST_ASSERT_EQUAL(-1, gSt.tmpSize());
  TEST_ASSERT_FALSE(in.write(f.data(), 10, why, sizeof why));  // chiusa: niente più scritture
  // la prossima offerta parte senza abort() e si installa
  TEST_ASSERT_TRUE(in.begin(gSt, "prova", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.busy());
  TEST_ASSERT_TRUE(in.write(f.data(), f.size(), why, sizeof why));
  TEST_ASSERT_TRUE(in.finish("", gScratch, why, sizeof why));
  TEST_ASSERT_FALSE(in.busy());
  TEST_ASSERT_TRUE(gSt.size("prova") > 0);
}

// theme-ack come lo schema themeAck: id valido (se no non si manda), error ≤ 127 byte senza spezzare l'UTF-8.
static void test_ack_schema() {
  char out[512];
  TEST_ASSERT_EQUAL(0, int(encodeAck("Rotto!", false, "x", out, sizeof out)));
  TEST_ASSERT_EQUAL(0, int(encodeAck("", true, "", out, sizeof out)));
  TEST_ASSERT_EQUAL(0, int(encodeAck(nullptr, true, "", out, sizeof out)));
  TEST_ASSERT_TRUE(encodeAck("prova", true, "ignorato", out, sizeof out) > 0);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"theme-ack","v":1,"id":"prova","ok":true})", out);
  // 127 byte ASCII: intero
  std::string e127(127, 'a');
  encodeAck("prova", false, e127.c_str(), out, sizeof out);
  TEST_ASSERT_EQUAL_STRING((R"({"t":"theme-ack","v":1,"id":"prova","ok":false,"error":")" + e127 + R"("})").c_str(), out);
  // 126 byte + «è» (2 byte, 127-128) + altro: si taglia prima della «è», a 126 byte
  std::string e = std::string(126, 'a') + "\xC3\xA8" + "coda";
  encodeAck("prova", false, e.c_str(), out, sizeof out);
  TEST_ASSERT_EQUAL_STRING((R"({"t":"theme-ack","v":1,"id":"prova","ok":false,"error":")" + std::string(126, 'a') + R"("})").c_str(), out);
  // 125 byte + «è» (126-127) + altro: la «è» ci sta tutta
  e = std::string(125, 'a') + "\xC3\xA8" + "coda";
  encodeAck("prova", false, e.c_str(), out, sizeof out);
  TEST_ASSERT_EQUAL_STRING((R"({"t":"theme-ack","v":1,"id":"prova","ok":false,"error":")" + std::string(125, 'a') + "\xC3\xA8" + R"("})").c_str(), out);
  // 125 byte + «✓» (3 byte, 126-128): via tutta
  e = std::string(125, 'a') + "\xE2\x9C\x93";
  encodeAck("prova", false, e.c_str(), out, sizeof out);
  TEST_ASSERT_EQUAL_STRING((R"({"t":"theme-ack","v":1,"id":"prova","ok":false,"error":")" + std::string(125, 'a') + R"("})").c_str(), out);
}

// ---------- 0.13.0: avvio, risveglio dell'ora, nono tema rimasto da un riavvio ----------
static void test_boot_plan_ok_and_empty() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  BootPlan p = themeBootPlan(gSt, "", gScratch);
  TEST_ASSERT_FALSE(p.use);
  TEST_ASSERT_FALSE(p.broken);
  p = themeBootPlan(gSt, "prova", gScratch);
  TEST_ASSERT_TRUE(p.use);
  TEST_ASSERT_EQUAL_STRING("prova", p.id);
}

static void test_boot_plan_broken_active() {
  gSt.eraseAll();
  TEST_ASSERT_TRUE(install("prova", load("prova.cthm")));
  gSt.raw("prova")[90] ^= 0x10;  // flash che cede
  BootPlan p = themeBootPlan(gSt, "prova", gScratch);
  TEST_ASSERT_FALSE(p.use);
  TEST_ASSERT_TRUE(p.broken);
  TEST_ASSERT_EQUAL_STRING("prova", p.id);
  p = themeBootPlan(gSt, "manca", gScratch);  // tolto mentre la NVS lo ricordava
  TEST_ASSERT_TRUE(p.broken);
}

// Risveglio dell'ora (ogni minuto in Riposo): niente CRC completo, solo intestazione e indice contro RestKeep.
static void test_activate_known() {
  gSt.eraseAll();
  std::vector<uint8_t> f = load("prova.cthm");
  TEST_ASSERT_TRUE(install("prova", f));
  static Runtime rt;
  rt.begin(&gSt);
  const int before = gSt.reads;
  TEST_ASSERT_TRUE(rt.activateKnown("prova", crcOf(f)));
  TEST_ASSERT_EQUAL(2, gSt.reads - before);  // intestazione + indice
  TEST_ASSERT_EQUAL_UINT32(crcOf(f), rt.activeCrc());
  TEST_ASSERT_EQUAL_STRING("prova", rt.active());
  TEST_ASSERT_FALSE(rt.activateKnown("prova", crcOf(f) ^ 1));  // tema cambiato: risveglio pieno
  TEST_ASSERT_EQUAL_STRING("", rt.active());
  TEST_ASSERT_FALSE(rt.activateKnown("manca", crcOf(f)));
  char bad[theme::kIdLen];
  TEST_ASSERT_FALSE(rt.takeBroken(bad, sizeof bad));  // non è un tema rotto: decide il risveglio pieno
  // in prestito alla partita: false, come activate()
  TEST_ASSERT_NOT_NULL(rt.lend(kMaxBlock));
  TEST_ASSERT_FALSE(rt.activateKnown("prova", crcOf(f)));
  rt.giveBack();
  // un blocco rovinato passa activateKnown, ma block() lo ricontrolla (checkBlock) e il tema risulta rotto
  TEST_ASSERT_TRUE(rt.activateKnown("prova", crcOf(f)));
  IndexEntry idx[kMaxScreens];
  Header h;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(parseHeader(gSt.raw("prova"), kHeaderLen, h)));
  TEST_ASSERT_EQUAL(int(Error::Ok), int(parseIndex(gSt.raw("prova") + kHeaderLen, 8u * h.nScreens, h, idx, kMaxScreens)));
  gSt.raw("prova")[idx[0].off] = 0xFF;  // primo byte del primo blocco
  TEST_ASSERT_NOT_EQUAL(int(Error::Ok), int(checkBlock(gSt.raw("prova") + idx[0].off, idx[0].len)));
  const uint8_t *b;
  uint16_t n;
  TEST_ASSERT_FALSE(rt.block(idx[0].screen, b, n));
  TEST_ASSERT_TRUE(rt.takeBroken(bad, sizeof bad));
  TEST_ASSERT_EQUAL_STRING("prova", bad);
}

// Riavvio fra la rinomina del nono tema e la riscrittura dell'ordine: 9 file. All'avvio si torna a 8 (esce il più
// vecchio non attivo, per orderList: prima quelli che l'ordine non conosce) e l'ordine si riscrive.
static void test_order_trim_after_crash() {
  gSt.eraseAll();
  const char *ids[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"};
  for (const char *id : ids) TEST_ASSERT_TRUE(install(id, withId(id)));
  TEST_ASSERT_EQUAL(0, orderTrim(gSt, "t1"));  // 8: niente da fare
  std::vector<uint8_t> f = withId("t9");
  TEST_ASSERT_TRUE(gSt.tmpBegin());
  TEST_ASSERT_TRUE(gSt.tmpAppend(f.data(), uint32_t(f.size())));
  TEST_ASSERT_TRUE(gSt.tmpCommit("t9"));  // rinomina fatta, ordine no (corrente che manca)
  char list[kMaxThemes + 1][theme::kIdLen];
  TEST_ASSERT_EQUAL(9, gSt.list(list, kMaxThemes + 1));
  TEST_ASSERT_EQUAL(1, orderTrim(gSt, "t9"));  // t9 attivo: esce t1, il più vecchio non attivo
  TEST_ASSERT_EQUAL(8, gSt.list(list, kMaxThemes + 1));
  TEST_ASSERT_EQUAL(-1, gSt.size("t1"));
  TEST_ASSERT_TRUE(gSt.size("t9") >= 0);
  char ord[kMaxThemes + 1][theme::kIdLen];
  TEST_ASSERT_EQUAL(8, orderList(gSt, ord, kMaxThemes + 1));
  char raw[kMaxThemes * theme::kIdLen + 16];
  const int rn = gSt.readOrder(raw, sizeof raw);
  TEST_ASSERT_TRUE(rn > 0);
  TEST_ASSERT_NOT_NULL(strstr(std::string(raw, size_t(rn)).c_str(), "t9\n"));  // l'ordine riscritto lo conosce
  // senza attivo: esce quello che l'ordine non conosce (il più vecchio per orderList)
  gSt.eraseAll();
  for (const char *id : ids) TEST_ASSERT_TRUE(install(id, withId(id)));
  TEST_ASSERT_TRUE(gSt.tmpBegin());
  TEST_ASSERT_TRUE(gSt.tmpAppend(f.data(), uint32_t(f.size())));
  TEST_ASSERT_TRUE(gSt.tmpCommit("t9"));
  TEST_ASSERT_EQUAL(1, orderTrim(gSt, ""));
  TEST_ASSERT_EQUAL(-1, gSt.size("t9"));
  TEST_ASSERT_EQUAL(8, gSt.list(list, kMaxThemes + 1));
}

// Nono tema mentre il loop sta attivando il più vecchio (scelto dal menu, ThemeCur ancora in coda): il tema protetto
// non esce, come l'attivo; esce il più vecchio fra gli altri.
static void test_commit_keeps_protected() {
  gSt.eraseAll();
  const char *ids[] = {"t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8"};
  for (const char *id : ids) TEST_ASSERT_TRUE(install(id, withId(id)));
  std::vector<uint8_t> f = withId("t9");
  char why[96] = "";
  Installer in;
  TEST_ASSERT_TRUE(in.begin(gSt, "t9", crcOf(f), uint32_t(f.size()), why, sizeof why));
  TEST_ASSERT_TRUE(in.write(f.data(), f.size(), why, sizeof why));
  TEST_ASSERT_TRUE(in.verify(gScratch, why, sizeof why));
  TEST_ASSERT_TRUE(in.commit("t2", why, sizeof why, "t1"));  // attivo t2 (vecchio), protetto t1 (scelto adesso)
  TEST_ASSERT_TRUE(gSt.size("t1") >= 0);
  TEST_ASSERT_TRUE(gSt.size("t2") >= 0);
  TEST_ASSERT_EQUAL(-1, gSt.size("t3"));
  TEST_ASSERT_TRUE(gSt.size("t9") >= 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_failure_closes_installer);
  RUN_TEST(test_ack_schema);
  RUN_TEST(test_offer_and_use_bounds);
  RUN_TEST(test_themes_strict_for_service);
  RUN_TEST(test_install_ok_and_catalog);
  RUN_TEST(test_install_rejects);
  RUN_TEST(test_install_abort_keeps_old);
  RUN_TEST(test_ninth_theme_evicts_oldest_not_active);
  RUN_TEST(test_ninth_commit_fails_keeps_all);
  RUN_TEST(test_use_unknown_theme);
  RUN_TEST(test_verify_then_commit);
  RUN_TEST(test_drop_all_themes);
  RUN_TEST(test_messages);
  RUN_TEST(test_offer_only_when_authed);
  RUN_TEST(test_boot_plan_ok_and_empty);
  RUN_TEST(test_boot_plan_broken_active);
  RUN_TEST(test_activate_known);
  RUN_TEST(test_order_trim_after_crash);
  RUN_TEST(test_commit_keeps_protected);
  return UNITY_END();
}
