// Pulizia profonda della NVS (core/nvs_scrub) con una NVS finta che si comporta come quella vera:
// una voce sovrascritta o tolta resta nei byte della «flash» finché la partizione non viene cancellata.
// Prova 9 di docs/M5-PERMESSI.md: dopo «Proteggi le chiavi» la vecchia pair-key in chiaro era nel dump.
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "core/crypto.h"
#include "core/nvs_scrub.h"
#include "core/vault.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

// Partizione nvs della Heltec: 0x9000, 20 KB (5 pagine da 4 KB).
const size_t kPartBytes = 5 * 4096;

// NVS finta, scritta in coda come quella vera: ogni set aggiunge un record all'immagine; la voce vecchia
// diventa «cancellata» (non più elencata) ma i suoi byte restano. Solo erasePartition() riporta tutto a 0xFF.
struct FakeNvs : NvsPort {
  struct Live { std::string ns, key; NvsType type; size_t off, len; };
  std::vector<uint8_t> image = std::vector<uint8_t>(kPartBytes, 0xFF);
  size_t wp = 0;
  std::vector<Live> live;
  int erases = 0, writes = 0, failWrites = 0;
  bool failErase = false, failList = false;
  std::string failRead;  // chiave la cui lettura non riesce

  Live *find(const char *ns, const char *key) {
    for (auto &l : live)
      if (l.ns == ns && l.key == key) return &l;
    return nullptr;
  }
  bool set(const char *ns, const char *key, NvsType t, const void *d, size_t n) {
    const size_t head = strlen(ns) + 1 + strlen(key) + 1 + 1 + 4;
    if (wp + head + n > image.size()) return false;
    for (const char *s : {ns, key}) { memcpy(&image[wp], s, strlen(s) + 1); wp += strlen(s) + 1; }
    image[wp++] = uint8_t(t);
    for (int b = 0; b < 4; b++) image[wp++] = uint8_t(n >> (8 * b));
    const size_t off = wp;
    if (n) memcpy(&image[wp], d, n);
    wp += n;
    eraseKey(ns, key);  // la vecchia copia: cancellata, byte al loro posto
    live.push_back({ns, key, t, off, n});
    return true;
  }
  void eraseKey(const char *ns, const char *key) {
    for (size_t i = 0; i < live.size(); i++)
      if (live[i].ns == ns && live[i].key == key) { live.erase(live.begin() + i); return; }
  }
  void eraseNs(const char *ns) {
    for (size_t i = live.size(); i-- > 0;)
      if (live[i].ns == ns) live.erase(live.begin() + i);
  }
  bool has(const void *needle, size_t n) const {
    return countOccurrences(image.data(), image.size(), static_cast<const uint8_t *>(needle), n) > 0;
  }
  bool hasStr(const char *s) const { return has(s, strlen(s)); }
  std::vector<uint8_t> value(const char *ns, const char *key) {
    Live *l = find(ns, key);
    return l ? std::vector<uint8_t>(image.begin() + l->off, image.begin() + l->off + l->len) : std::vector<uint8_t>();
  }

  int list(NvsItem *out, int cap) override {
    if (failList) return -1;
    int n = 0;
    for (auto &l : live) {
      if (n < cap) {
        memset(&out[n], 0, sizeof out[n]);
        strncpy(out[n].ns, l.ns.c_str(), 15);
        strncpy(out[n].key, l.key.c_str(), 15);
        out[n].type = l.type;
      }
      n++;
    }
    return n;
  }
  bool size(const NvsItem &it, size_t *n) override {
    Live *l = find(it.ns, it.key);
    if (!l) return false;
    *n = l->len;
    return true;
  }
  bool read(const NvsItem &it, uint8_t *out, size_t n) override {
    Live *l = find(it.ns, it.key);
    if (!l || l->len != n || failRead == it.key) return false;
    if (n) memcpy(out, &image[l->off], n);
    return true;
  }
  bool erasePartition() override {
    if (failErase) return false;
    erases++;
    std::fill(image.begin(), image.end(), 0xFF);
    wp = 0;
    live.clear();
    return true;
  }
  bool write(const NvsItem &it, const uint8_t *d) override {
    writes++;
    if (failWrites) { failWrites--; return false; }
    return set(it.ns, it.key, it.type, d, it.len);
  }
  bool commit() override { return true; }
};

// Il vault sopra la NVS finta, namespace «calamaio-sec» (come PrefsKv sulla scheda: blob).
struct NvsKv : KvStore {
  FakeNvs &n;
  explicit NvsKv(FakeNvs &f) : n(f) {}
  size_t get(const char *k, uint8_t *out, size_t cap) override {
    std::vector<uint8_t> v = n.value("calamaio-sec", k);
    if (v.empty() || v.size() > cap) return 0;
    memcpy(out, v.data(), v.size());
    return v.size();
  }
  bool put(const char *k, const uint8_t *d, size_t len) override { return n.set("calamaio-sec", k, NvsType::Blob, d, len); }
  void erase(const char *k) override { n.eraseKey("calamaio-sec", k); }
};

// eFuse finta minima (come in test_vault).
struct FakeEfuse : KeyHw {
  uint8_t key[32];
  bool used = false;
  uint32_t seed = 7;
  int hmacKeyBlock() override { return used ? 0 : -1; }
  int freeBlock() override { return used ? -1 : 0; }
  bool blockFree(int k) override { return k == 0 && !used; }
  bool program(int, const uint8_t k[32], char *, size_t) override { memcpy(key, k, 32); used = true; return true; }
  bool hmac(int, const uint8_t *m, size_t n, uint8_t out[32]) override {
    if (!used) return false;
    cry::hmacSha256(key, 32, m, n, out);
    return true;
  }
  void random(uint8_t *out, size_t n) override {
    for (size_t k = 0; k < n; k++) { seed = seed * 1103515245u + 12345u; out[k] = uint8_t(seed >> 16); }
  }
};

const char *const kDrop[] = {"nvs.net80211", "calamaio-net"};
const ScrubConfig kCfg = {kDrop, 2, 16 * 1024};

// Lavoro come sulla scheda (hw/secrets.cpp): 512 voci, arena di 20 KB.
struct Work {
  NvsItem items[512];
  uint8_t arena[20 * 1024];
  char log[256];
};

ScrubReport run(FakeNvs &n, Work &w, const ScrubConfig &cfg = kCfg, int itemCap = 512) {
  const ScrubReport r = scrubNvs(n, cfg, w.items, itemCap, w.arena, sizeof w.arena, w.log, sizeof w.log);
  printf("[pulizia] %s · %s\n", scrubOutcomeText(r.outcome), w.log);
  return r;
}

bool arenaZero(const Work &w) {
  for (uint8_t b : w.arena)
    if (b) return false;
  return true;
}

const uint8_t kK[32] = {0x3d, 0x9c, 0x85, 0x2a, 0x2b, 0xcb, 0xa1, 0xe6, 0x33, 0x7f, 0x46, 0x47, 0x09, 0x98, 0xe3, 0xbd,
                        0x09, 0x3d, 0x70, 0xc3, 0xa6, 0xfc, 0xaa, 0x00, 0x6c, 0x4f, 0x78, 0x75, 0xa1, 0x5e, 0x6b, 0xa0};
const char *const kPass = "segreta-123";

}  // namespace

// Prova 9 riprodotta: dopo Proteggi le chiavi K e password non sono più voci valide, ma i byte ci sono.
// Dopo la pulizia nell'immagine non ci sono più, e il vault li rilegge (cifrati) uguali.
static void test_ghost_secrets_gone_after_scrub() {
  FakeNvs nvs;
  NvsKv kv(nvs);
  FakeEfuse ef;
  Vault v(ef, kv);
  v.begin();
  TEST_ASSERT_TRUE(v.saveWifi("CasaWiFi", kPass));
  TEST_ASSERT_TRUE(v.savePair(kK, "mac-sviluppo.local"));
  // Copia di sistema del Wi-Fi e vecchio spazio di M3, come sulla scheda di fabbrica.
  nvs.set("nvs.net80211", "sta.pswd", NvsType::Blob, kPass, strlen(kPass));
  nvs.set("calamaio-net", "pass", NvsType::Str, kPass, strlen(kPass) + 1);
  nvs.eraseNs("nvs.net80211");  // come eraseSystemWifiCopy(): voci non più valide, byte ancora lì
  nvs.eraseKey("calamaio-net", "pass");
  char log[256];
  TEST_ASSERT_EQUAL(ProtectResult::Ok, v.protect(log, sizeof log));
  TEST_ASSERT_FALSE(nvs.find("calamaio-sec", "pair-key"));  // nessuna voce valida in chiaro...
  TEST_ASSERT_FALSE(nvs.find("calamaio-sec", "wifi-pass"));
  TEST_ASSERT_TRUE(nvs.has(kK, 32));                         // ...ma i byte sì: è il difetto della prova 9
  TEST_ASSERT_TRUE(nvs.hasStr(kPass));
  TEST_ASSERT_EQUAL(3, countOccurrences(nvs.image.data(), nvs.image.size(), (const uint8_t *)kPass, strlen(kPass)));

  Work w;
  const int before = int(nvs.live.size());
  const ScrubReport r = run(nvs, w);
  TEST_ASSERT_EQUAL(ScrubOutcome::Done, r.outcome);
  TEST_ASSERT_EQUAL(1, nvs.erases);
  TEST_ASSERT_EQUAL(before, r.kept);
  TEST_ASSERT_EQUAL(before, int(nvs.live.size()));
  TEST_ASSERT_FALSE(nvs.has(kK, 32));
  TEST_ASSERT_FALSE(nvs.hasStr(kPass));
  TEST_ASSERT_TRUE(arenaZero(w));  // nessun segreto in chiaro lasciato in RAM

  Vault v2(ef, kv);  // riavvio dopo la pulizia
  v2.begin();
  TEST_ASSERT_TRUE(v2.keysProtected());
  char ssid[33], pass[65];
  uint8_t K[32];
  TEST_ASSERT_TRUE(v2.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", ssid);
  TEST_ASSERT_EQUAL_STRING(kPass, pass);
  TEST_ASSERT_TRUE(v2.loadPair(K, nullptr, 0));
  TEST_ASSERT_EQUAL_MEMORY(kK, K, 32);
}

// Tutti i tipi, namespace sconosciuti (es. una cache degli avatar), blob grande: riscritti identici.
static void test_all_types_and_large_blob_roundtrip() {
  FakeNvs nvs;
  const uint8_t u8 = 0xA5;
  const int8_t i8 = -7;
  const uint16_t u16 = 0xBEEF;
  const int16_t i16 = -12345;
  const uint32_t u32 = 0xDEADBEEF;
  const int32_t i32 = -2000000000;
  const uint64_t u64 = 0x0123456789ABCDEFull;
  const int64_t i64 = -0x0123456789ABCDELL;
  nvs.set("calamaio-set", "u8", NvsType::U8, &u8, 1);
  nvs.set("calamaio-set", "i8", NvsType::I8, &i8, 1);
  nvs.set("calamaio-set", "u16", NvsType::U16, &u16, 2);
  nvs.set("calamaio-set", "i16", NvsType::I16, &i16, 2);
  nvs.set("phy", "u32", NvsType::U32, &u32, 4);
  nvs.set("phy", "i32", NvsType::I32, &i32, 4);
  nvs.set("avatar-cache", "u64", NvsType::U64, &u64, 8);
  nvs.set("avatar-cache", "i64", NvsType::I64, &i64, 8);
  nvs.set("avatar-cache", "name", NvsType::Str, "Calamaro", 9);
  std::vector<uint8_t> big(6000);
  for (size_t k = 0; k < big.size(); k++) big[k] = uint8_t(k * 31 + 7);
  nvs.set("avatar-cache", "bitmap", NvsType::Blob, big.data(), big.size());
  // Una versione vecchia sovrascritta: deve sparire dall'immagine.
  nvs.set("avatar-cache", "token", NvsType::Str, "vecchio-token-123", 18);
  nvs.set("avatar-cache", "token", NvsType::Str, "nuovo", 6);
  TEST_ASSERT_TRUE(nvs.hasStr("vecchio-token-123"));
  std::vector<std::vector<uint8_t>> want;
  for (auto &l : nvs.live) want.push_back(nvs.value(l.ns.c_str(), l.key.c_str()));
  std::vector<FakeNvs::Live> wantLive = nvs.live;

  Work w;
  const ScrubReport r = run(nvs, w);
  TEST_ASSERT_EQUAL(ScrubOutcome::Done, r.outcome);
  TEST_ASSERT_EQUAL(11, r.kept);
  TEST_ASSERT_EQUAL(0, r.dropped);
  TEST_ASSERT_EQUAL(1 + 1 + 2 + 2 + 4 + 4 + 8 + 8 + 9 + 6000 + 6, int(r.bytes));
  TEST_ASSERT_EQUAL(wantLive.size(), nvs.live.size());
  for (size_t k = 0; k < wantLive.size(); k++) {
    FakeNvs::Live *l = nvs.find(wantLive[k].ns.c_str(), wantLive[k].key.c_str());
    TEST_ASSERT_NOT_NULL(l);
    TEST_ASSERT_EQUAL(int(wantLive[k].type), int(l->type));
    TEST_ASSERT_TRUE(want[k] == nvs.value(l->ns.c_str(), l->key.c_str()));
  }
  TEST_ASSERT_FALSE(nvs.hasStr("vecchio-token-123"));
  TEST_ASSERT_TRUE(arenaZero(w));
}

// Una lettura non riesce: non si cancella niente (immagine identica byte per byte).
static void test_read_failure_erases_nothing() {
  FakeNvs nvs;
  nvs.set("calamaio-sec", "pair-key", NvsType::Blob, kK, 32);
  nvs.eraseKey("calamaio-sec", "pair-key");  // fantasma
  nvs.set("calamaio-set", "v1", NvsType::Blob, "0123456789abcdef", 16);
  nvs.set("avatar-cache", "rotto", NvsType::Blob, "xyz", 3);
  nvs.failRead = "rotto";
  const std::vector<uint8_t> img = nvs.image;
  Work w;
  const ScrubReport r = run(nvs, w);
  TEST_ASSERT_EQUAL(ScrubOutcome::ReadFailed, r.outcome);
  TEST_ASSERT_EQUAL(0, nvs.erases);
  TEST_ASSERT_EQUAL(0, nvs.writes);
  TEST_ASSERT_TRUE(img == nvs.image);
  TEST_ASSERT_EQUAL(2, int(nvs.live.size()));
  TEST_ASSERT_TRUE(nvs.has(kK, 32));  // il fantasma resta: si riprova al prossimo avvio
  TEST_ASSERT_TRUE(arenaZero(w));
  TEST_ASSERT_NOT_NULL(strstr(w.log, "avatar-cache/rotto"));
}

// Elenco non riuscito, troppe voci, valori oltre il limite: niente cancellato.
static void test_limits_erase_nothing() {
  Work w;
  {
    FakeNvs nvs;
    nvs.set("a", "k", NvsType::U8, "\x01", 1);
    nvs.failList = true;
    TEST_ASSERT_EQUAL(ScrubOutcome::ListFailed, run(nvs, w).outcome);
    TEST_ASSERT_EQUAL(0, nvs.erases);
  }
  {
    FakeNvs nvs;
    for (int k = 0; k < 5; k++) nvs.set("a", std::to_string(k).c_str(), NvsType::U8, "\x01", 1);
    TEST_ASSERT_EQUAL(ScrubOutcome::TooMany, run(nvs, w, kCfg, 4).outcome);
    TEST_ASSERT_EQUAL(0, nvs.erases);
  }
  {
    FakeNvs nvs;
    std::vector<uint8_t> blob(4000, 0x42);
    for (int k = 0; k < 5; k++) nvs.set("avatar-cache", std::to_string(k).c_str(), NvsType::Blob, blob.data(), blob.size());
    const std::vector<uint8_t> img = nvs.image;
    const ScrubReport r = run(nvs, w);  // 20 000 byte > 16 384
    TEST_ASSERT_EQUAL(ScrubOutcome::TooBig, r.outcome);
    TEST_ASSERT_EQUAL(0, nvs.erases);
    TEST_ASSERT_TRUE(img == nvs.image);
    TEST_ASSERT_TRUE(arenaZero(w));
  }
  {
    // Sotto il limite ma senza spazio nell'arena per la rilettura del valore più lungo.
    FakeNvs nvs;
    std::vector<uint8_t> blob(6000, 0x42);
    nvs.set("x", "a", NvsType::Blob, blob.data(), blob.size());
    nvs.set("x", "b", NvsType::Blob, blob.data(), blob.size());
    NvsItem items[8];
    uint8_t arena[14000];
    char log[200];
    const ScrubReport r = scrubNvs(nvs, kCfg, items, 8, arena, sizeof arena, log, sizeof log);
    TEST_ASSERT_EQUAL(ScrubOutcome::TooBig, r.outcome);
    TEST_ASSERT_EQUAL(0, nvs.erases);
  }
}

// I namespace da eliminare non si riscrivono, anche con voci ancora valide; il resto sì.
static void test_system_namespaces_dropped() {
  FakeNvs nvs;
  nvs.set("nvs.net80211", "sta.ssid", NvsType::Blob, "CasaWiFi", 8);
  nvs.set("nvs.net80211", "sta.pswd", NvsType::Blob, kPass, strlen(kPass));
  nvs.set("calamaio-net", "pass", NvsType::Str, kPass, strlen(kPass) + 1);
  nvs.set("calamaio-sec", "wifi-ssid", NvsType::Blob, "CasaWiFi", 8);
  nvs.set("nvs.net80211x", "simile", NvsType::U8, "\x02", 1);  // solo il nome esatto viene escluso
  Work w;
  const ScrubReport r = run(nvs, w);
  TEST_ASSERT_EQUAL(ScrubOutcome::Done, r.outcome);
  TEST_ASSERT_EQUAL(5, r.found);
  TEST_ASSERT_EQUAL(3, r.dropped);
  TEST_ASSERT_EQUAL(2, r.kept);
  TEST_ASSERT_FALSE(nvs.hasStr(kPass));
  TEST_ASSERT_FALSE(nvs.find("nvs.net80211", "sta.ssid"));
  TEST_ASSERT_NOT_NULL(nvs.find("calamaio-sec", "wifi-ssid"));
  TEST_ASSERT_NOT_NULL(nvs.find("nvs.net80211x", "simile"));
}

// Una scrittura non riesce al primo giro: secondo giro dall'arena, niente perso.
// Se non riesce neanche il secondo, l'esito lo dice (le voci sono perse: la scheda riparte in configurazione).
static void test_write_failure_retry_and_report() {
  Work w;
  {
    FakeNvs nvs;
    nvs.set("calamaio-sec", "salt", NvsType::Blob, "0123456789abcdef", 16);
    nvs.set("calamaio-set", "v1", NvsType::Blob, "fedcba9876543210", 16);
    nvs.failWrites = 1;
    const ScrubReport r = run(nvs, w);
    TEST_ASSERT_EQUAL(ScrubOutcome::Done, r.outcome);
    TEST_ASSERT_EQUAL(2, r.attempts);
    TEST_ASSERT_EQUAL(2, nvs.erases);
    TEST_ASSERT_EQUAL(2, int(nvs.live.size()));
  }
  {
    FakeNvs nvs;
    nvs.set("calamaio-sec", "salt", NvsType::Blob, "0123456789abcdef", 16);
    nvs.failWrites = 100;
    const ScrubReport r = run(nvs, w);
    TEST_ASSERT_EQUAL(ScrubOutcome::WriteFailed, r.outcome);
    TEST_ASSERT_EQUAL(2, r.attempts);
    TEST_ASSERT_TRUE(arenaZero(w));
  }
  {
    // Cancellazione fisica non riuscita: voci riscritte (nessuna perdita), ma l'esito non è Done.
    FakeNvs nvs;
    nvs.set("calamaio-sec", "pair-key", NvsType::Blob, kK, 32);
    nvs.eraseKey("calamaio-sec", "pair-key");
    nvs.set("calamaio-set", "v1", NvsType::Blob, "fedcba9876543210", 16);
    nvs.failErase = true;
    const ScrubReport r = run(nvs, w);
    TEST_ASSERT_EQUAL(ScrubOutcome::EraseFailed, r.outcome);
    TEST_ASSERT_EQUAL(1, int(nvs.live.size()));
    TEST_ASSERT_TRUE(nvs.has(kK, 32));  // per questo la bandiera non si scrive
  }
}

// NVS vuota (prima accensione o corrente mancata a metà pulizia): niente da riscrivere, nessun errore.
static void test_empty_partition() {
  FakeNvs nvs;
  Work w;
  const ScrubReport r = run(nvs, w);
  TEST_ASSERT_EQUAL(ScrubOutcome::Done, r.outcome);
  TEST_ASSERT_EQUAL(0, r.found);
  TEST_ASSERT_EQUAL(1, nvs.erases);
}

static void test_count_occurrences() {
  const uint8_t hay[] = {1, 2, 1, 2, 1, 2, 3};
  const uint8_t a[] = {1, 2}, b[] = {2, 1, 2}, c[] = {9};
  TEST_ASSERT_EQUAL(3, countOccurrences(hay, sizeof hay, a, sizeof a));
  TEST_ASSERT_EQUAL(2, countOccurrences(hay, sizeof hay, b, sizeof b));
  TEST_ASSERT_EQUAL(0, countOccurrences(hay, sizeof hay, c, sizeof c));
  TEST_ASSERT_EQUAL(0, countOccurrences(hay, sizeof hay, a, 0));
  TEST_ASSERT_EQUAL(0, countOccurrences(a, sizeof a, hay, sizeof hay));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ghost_secrets_gone_after_scrub);
  RUN_TEST(test_all_types_and_large_blob_roundtrip);
  RUN_TEST(test_read_failure_erases_nothing);
  RUN_TEST(test_limits_erase_nothing);
  RUN_TEST(test_system_namespaces_dropped);
  RUN_TEST(test_write_failure_retry_and_report);
  RUN_TEST(test_empty_partition);
  RUN_TEST(test_count_occurrences);
  return UNITY_END();
}
