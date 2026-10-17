// Segreti sulla scheda (docs/M5-PERMESSI.md §4) con una eFuse e una NVS finte: prima e dopo «Proteggi le
// chiavi», spazio già occupato, scheda diversa, migrazione interrotta, ripristino di fabbrica.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "core/crypto.h"
#include "core/legacy_wifi.h"
#include "core/vault.h"

using namespace cal;

void setUp() {}
void tearDown() {}

namespace {

// eFuse finta: 6 spazi, ognuno con contenuto, scopo e protezioni. La chiave non si legge mai da fuori:
// hmac() la usa come farebbe la periferica.
struct FakeEfuse : KeyHw {
  struct Blk { uint8_t key[32]; bool used, hmacUp, readProt; } b[6];
  uint32_t seed = 1;
  int writes = 0;
  bool failWrite = false, occupyBeforeWrite = false;
  FakeEfuse() { memset(b, 0, sizeof b); }
  int hmacKeyBlock() override {
    for (int k = 0; k < 6; k++)
      if (b[k].used && b[k].hmacUp) return k;
    return -1;
  }
  int freeBlock() override {
    for (int k = 0; k < 6; k++)
      if (!b[k].used) return k;
    return -1;
  }
  bool blockFree(int k) override {
    if (occupyBeforeWrite) { b[k].used = true; memset(b[k].key, 0xAB, 32); }  // qualcuno l'ha scritto nel frattempo
    return k >= 0 && k < 6 && !b[k].used;
  }
  bool program(int k, const uint8_t key[32], char *err, size_t cap) override {
    writes++;
    if (failWrite || b[k].used) { strncpy(err, "ESP_ERR_EFUSE_REPEATED_PROG", cap); return false; }
    memcpy(b[k].key, key, 32);
    b[k].used = b[k].hmacUp = b[k].readProt = true;
    return true;
  }
  bool hmac(int k, const uint8_t *msg, size_t n, uint8_t out[32]) override {
    if (k < 0 || k >= 6 || !b[k].hmacUp) return false;
    cry::hmacSha256(b[k].key, 32, msg, n, out);
    return true;
  }
  void random(uint8_t *out, size_t n) override {
    for (size_t k = 0; k < n; k++) { seed = seed * 1103515245u + 12345u; out[k] = uint8_t(seed >> 16); }
  }
};

struct MapKv : KvStore {
  std::map<std::string, std::vector<uint8_t>> m;
  bool failPut = false;
  size_t get(const char *k, uint8_t *out, size_t cap) override {
    auto it = m.find(k);
    if (it == m.end() || it->second.size() > cap) return 0;
    memcpy(out, it->second.data(), it->second.size());
    return it->second.size();
  }
  bool put(const char *k, const uint8_t *d, size_t n) override {
    if (failPut) return false;
    m[k] = std::vector<uint8_t>(d, d + n);
    return true;
  }
  void erase(const char *k) override { m.erase(k); }
  // Ricerca di una stringa nota in tutta la «flash» (test di accettazione 9).
  bool contains(const void *needle, size_t n) const {
    for (auto &kv : m) {
      const std::vector<uint8_t> &v = kv.second;
      if (v.size() >= n)
        for (size_t i = 0; i + n <= v.size(); i++)
          if (memcmp(v.data() + i, needle, n) == 0) return true;
    }
    return false;
  }
};

const uint8_t kK[32] = {0x3d, 0x9c, 0x85, 0x2a, 0x2b, 0xcb, 0xa1, 0xe6, 0x33, 0x7f, 0x46, 0x47, 0x09, 0x98, 0xe3, 0xbd,
                        0x09, 0x3d, 0x70, 0xc3, 0xa6, 0xfc, 0xaa, 0x00, 0x6c, 0x4f, 0x78, 0x75, 0xa1, 0x5e, 0x6b, 0xa0};
const char *const kPass = "segreta-123";

}  // namespace

static void test_unprotected_keeps_plain_secrets() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  TEST_ASSERT_FALSE(v.keysProtected());
  TEST_ASSERT_TRUE(v.saveWifi("CasaWiFi", kPass));
  TEST_ASSERT_TRUE(v.savePair(kK, "mac-sviluppo.local"));
  TEST_ASSERT_TRUE(kv.contains(kPass, strlen(kPass)));  // come oggi: in chiaro finché non si protegge
  TEST_ASSERT_TRUE(kv.contains(kK, 32));
  char ssid[33], pass[65], host[48];
  uint8_t K[32];
  TEST_ASSERT_TRUE(v.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", ssid);
  TEST_ASSERT_EQUAL_STRING(kPass, pass);
  TEST_ASSERT_TRUE(v.loadPair(K, host, sizeof host));
  TEST_ASSERT_EQUAL_MEMORY(kK, K, 32);
  TEST_ASSERT_EQUAL_STRING("mac-sviluppo.local", host);
  TEST_ASSERT_EQUAL(0, ef.writes);  // mai una scrittura della eFuse da sola
}

static void test_protect_writes_once_and_reencrypts() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  v.saveWifi("CasaWiFi", kPass);
  v.savePair(kK, "pc");
  char log[256];
  TEST_ASSERT_EQUAL(ProtectResult::Ok, v.protect(log, sizeof log));
  printf("log: %s\n", log);
  TEST_ASSERT_EQUAL(1, ef.writes);
  TEST_ASSERT_TRUE(v.keysProtected());
  TEST_ASSERT_TRUE(ef.b[0].hmacUp && ef.b[0].readProt);
  // Test di accettazione 9: nel «dump» non c'è più niente in chiaro.
  TEST_ASSERT_FALSE(kv.contains(kPass, strlen(kPass)));
  TEST_ASSERT_FALSE(kv.contains(kK, 32));
  TEST_ASSERT_TRUE(kv.m.count("wifi-pass.e") && kv.m.count("pair-key.e") && kv.m.count("salt"));
  TEST_ASSERT_FALSE(kv.m.count("wifi-pass") || kv.m.count("pair-key"));
  char ssid[33], pass[65];
  uint8_t K[32];
  TEST_ASSERT_TRUE(v.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING(kPass, pass);
  TEST_ASSERT_TRUE(v.loadPair(K, nullptr, 0));
  TEST_ASSERT_EQUAL_MEMORY(kK, K, 32);
  // Riavvio: stessa eFuse, stesso sale → stessi segreti.
  Vault v2(ef, kv);
  v2.begin();
  TEST_ASSERT_TRUE(v2.keysProtected());
  TEST_ASSERT_TRUE(v2.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING(kPass, pass);
  // Una seconda «Proteggi» non scrive niente.
  TEST_ASSERT_EQUAL(ProtectResult::Already, v2.protect(log, sizeof log));
  TEST_ASSERT_EQUAL(1, ef.writes);
  // Dopo la protezione anche i salvataggi nuovi sono cifrati.
  v2.saveWifi("Officina", "altra-password");
  TEST_ASSERT_FALSE(kv.contains("altra-password", 14));
}

static void test_protect_no_free_block_or_block_taken() {
  FakeEfuse ef;
  for (auto &b : ef.b) { b.used = true; }  // tutti gli spazi occupati (scopo USER, contenuto non zero)
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  char log[256];
  TEST_ASSERT_EQUAL(ProtectResult::NoFreeBlock, v.protect(log, sizeof log));
  TEST_ASSERT_EQUAL(0, ef.writes);
  TEST_ASSERT_FALSE(v.keysProtected());

  FakeEfuse ef2;
  ef2.occupyBeforeWrite = true;  // libero alla ricerca, occupato al ricontrollo
  Vault v2(ef2, kv);
  v2.begin();
  TEST_ASSERT_EQUAL(ProtectResult::BlockChanged, v2.protect(log, sizeof log));
  TEST_ASSERT_EQUAL(0, ef2.writes);
  TEST_ASSERT_NOT_NULL(strstr(log, "non è più libero"));

  FakeEfuse ef3;
  ef3.failWrite = true;
  MapKv kv3;
  Vault v3(ef3, kv3);
  v3.begin();
  v3.saveWifi("CasaWiFi", kPass);
  TEST_ASSERT_EQUAL(ProtectResult::WriteFailed, v3.protect(log, sizeof log));
  TEST_ASSERT_FALSE(v3.keysProtected());
  char ssid[33], pass[65];
  TEST_ASSERT_TRUE(v3.loadWifi(ssid, sizeof ssid, pass, sizeof pass));  // niente perso
}

static void test_backup_on_another_board_is_unreadable() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  v.saveWifi("CasaWiFi", kPass);
  v.savePair(kK, "pc");
  char log[256];
  v.protect(log, sizeof log);
  // Scheda diversa con la sua chiave nel chip: stessa NVS copiata, segreti non decifrabili → assenti.
  FakeEfuse other;
  other.seed = 777;
  char e[8];
  uint8_t otherKey[32];
  other.random(otherKey, 32);
  other.program(2, otherKey, e, sizeof e);
  Vault w(other, kv);
  w.begin();
  char ssid[33], pass[65];
  uint8_t K[32];
  TEST_ASSERT_FALSE(w.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_FALSE(w.loadPair(K, nullptr, 0));
  // Scheda senza chiave nel chip: idem.
  FakeEfuse blank;
  Vault z(blank, kv);
  z.begin();
  TEST_ASSERT_FALSE(z.keysProtected());
  TEST_ASSERT_FALSE(z.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_FALSE(z.loadPair(K, nullptr, 0));
  // E la configurazione si rifà: la nuova rete si salva (in chiaro, su questa scheda non protetta).
  TEST_ASSERT_TRUE(z.saveWifi("Officina", "nuova"));
  TEST_ASSERT_TRUE(z.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING("nuova", pass);
}

static void test_interrupted_migration_finishes_at_boot() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  v.saveWifi("CasaWiFi", kPass);
  char log[256];
  v.protect(log, sizeof log);
  // Simula un'interruzione: il chiaro c'è ancora (scritto da una versione vecchia o prima del riavvio).
  kv.put("pair-key", kK, 32);
  Vault v2(ef, kv);
  v2.begin();
  TEST_ASSERT_FALSE(kv.contains(kK, 32));
  uint8_t K[32];
  TEST_ASSERT_TRUE(v2.loadPair(K, nullptr, 0));
  TEST_ASSERT_EQUAL_MEMORY(kK, K, 32);
}

static void test_factory_reset_and_open_network() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  char log[256];
  v.protect(log, sizeof log);
  v.saveWifi("Ospiti", "");  // rete aperta: password vuota ma presente
  v.savePair(kK, "pc");
  char ssid[33], pass[65];
  TEST_ASSERT_TRUE(v.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING("", pass);
  v.factoryReset();
  TEST_ASSERT_EQUAL(0, kv.m.size());  // segreti, SSID, host e sale: tutto via
  uint8_t K[32];
  TEST_ASSERT_FALSE(v.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_FALSE(v.loadPair(K, nullptr, 0));
  // Al riavvio la chiave nel chip è ancora lì: sale nuovo e si riparte cifrati.
  Vault v2(ef, kv);
  v2.begin();
  TEST_ASSERT_TRUE(v2.keysProtected());
  TEST_ASSERT_TRUE(kv.m.count("salt"));
  v2.saveWifi("CasaWiFi", kPass);
  TEST_ASSERT_FALSE(kv.contains(kPass, strlen(kPass)));
}

static void test_record_format_matches_vectors() {
  // KE e record di protocol/vectors/crypto-vectors.json con la stessa chiave «nel chip» e lo stesso sale.
  std::ifstream f("../protocol/vectors/crypto-vectors.json");
  std::stringstream ss;
  ss << f.rdbuf();
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, ss.str()));
  JsonVariantConst vv = d["flow"]["vault"];
  FakeEfuse ef;
  uint8_t key[32], salt[16];
  cry::fromHex(vv["efuseKey"], key, 32);
  cry::fromHex(vv["salt"], salt, 16);
  char e[8];
  ef.program(1, key, e, sizeof e);
  MapKv kv;
  kv.put("salt", salt, 16);
  for (JsonVariantConst r : vv["records"].as<JsonArrayConst>()) {
    const char *rec = r["record"];
    std::vector<uint8_t> b(strlen(rec) / 2);
    cry::fromHex(rec, b.data(), b.size());
    char en[24];
    snprintf(en, sizeof en, "%s.e", r["name"].as<const char *>());
    kv.put(en, b.data(), b.size());
  }
  kv.put("wifi-ssid", reinterpret_cast<const uint8_t *>("CasaWiFi"), 8);
  Vault v(ef, kv);
  v.begin();
  char ssid[33], pass[65];
  TEST_ASSERT_TRUE(v.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING("segreta-123", pass);
  uint8_t K[32];
  TEST_ASSERT_TRUE(v.loadPair(K, nullptr, 0));
  uint8_t want[32];
  cry::fromHex(d["flow"]["pair"]["K"], want, 32);
  TEST_ASSERT_EQUAL_MEMORY(want, K, 32);
}

// ---------- migrazione di «calamaio-net» (M6: sola lettura, il namespace non si crea più) ----------
// NVS finta per un solo namespace: openWrite() su un namespace assente lo CREA (come nvs_open READWRITE).
struct FakeLegacy : LegacyStore {
  bool exists = false, open = false, writable = false;
  int writeOpens = 0;
  std::map<std::string, std::string> kv;
  bool openRead() override { if (!exists) return false; open = true; writable = false; return true; }
  bool get(const char *k, char *out, size_t cap) override {
    auto it = kv.find(k);
    if (!open || it == kv.end()) return false;
    strncpy(out, it->second.c_str(), cap - 1);
    out[cap - 1] = 0;
    return true;
  }
  bool openWrite() override { writeOpens++; exists = true; open = writable = true; return true; }
  void remove(const char *k) override { TEST_ASSERT_TRUE(writable); kv.erase(k); }
  void close() override { open = writable = false; }
};

void test_legacy_absent_namespace_is_not_created() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  FakeLegacy st;
  TEST_ASSERT_EQUAL(int(LegacyResult::NoNamespace), int(migrateLegacyWifi(st, v)));
  TEST_ASSERT_FALSE(st.exists);  // prima: begin(..., false) lo creava vuoto a ogni avvio
  TEST_ASSERT_EQUAL(0, st.writeOpens);
  for (int boot = 0; boot < 3; boot++) migrateLegacyWifi(st, v);
  TEST_ASSERT_FALSE(st.exists);
}

void test_legacy_empty_namespace_is_left_alone() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  FakeLegacy st;
  st.exists = true;
  TEST_ASSERT_EQUAL(int(LegacyResult::Empty), int(migrateLegacyWifi(st, v)));
  TEST_ASSERT_EQUAL(0, st.writeOpens);
}

void test_legacy_network_moves_into_vault_then_removed() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  FakeLegacy st;
  st.exists = true;
  st.kv["ssid"] = "CasaWiFi";
  st.kv["pass"] = "segreta-123";
  TEST_ASSERT_EQUAL(int(LegacyResult::Migrated), int(migrateLegacyWifi(st, v)));
  char ssid[33], pass[65];
  TEST_ASSERT_TRUE(v.loadWifi(ssid, sizeof ssid, pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", ssid);
  TEST_ASSERT_EQUAL_STRING("segreta-123", pass);
  TEST_ASSERT_EQUAL(1, st.writeOpens);
  TEST_ASSERT_TRUE(st.kv.empty());
}

void test_legacy_vault_failure_keeps_old_copy() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  kv.failPut = true;
  FakeLegacy st;
  st.exists = true;
  st.kv["ssid"] = "CasaWiFi";
  st.kv["pass"] = "segreta-123";
  TEST_ASSERT_EQUAL(int(LegacyResult::Failed), int(migrateLegacyWifi(st, v)));
  TEST_ASSERT_EQUAL(0, st.writeOpens);
  TEST_ASSERT_EQUAL(2, int(st.kv.size()));
}


// ---------- più reti (firmware 0.8.0) ----------

static int netNames(Vault &v, std::string &out) {
  Vault::NetMeta m[Vault::kNetMax];
  const int n = v.loadNets(m, Vault::kNetMax);
  out.clear();
  for (int k = 0; k < n; k++) out += std::string(k ? "," : "") + m[k].ssid;
  return n;
}

static void test_nets_order_full_update_forget() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  std::string names;
  TEST_ASSERT_EQUAL(0, netNames(v, names));
  const char *ssid[5] = {"CasaWiFi", "Officina", "Casa Mare", "Laboratorio", "Ospiti"};
  for (int k = 0; k < 5; k++) TEST_ASSERT_EQUAL(Vault::AddNet::Added, v.addNet(ssid[k], k == 4 ? "" : "password-12", 1000 + k));
  TEST_ASSERT_EQUAL(5, netNames(v, names));
  TEST_ASSERT_EQUAL_STRING("CasaWiFi,Officina,Casa Mare,Laboratorio,Ospiti", names.c_str());
  // Sesta: lista piena, niente scritto.
  TEST_ASSERT_EQUAL(Vault::AddNet::Full, v.addNet("Sesta", "password-12", 0));
  TEST_ASSERT_FALSE(kv.contains("Sesta", 5));
  // Stessa rete: password nuova, stesso posto (anche con la lista piena).
  TEST_ASSERT_EQUAL(Vault::AddNet::Updated, v.addNet("Officina", "nuova-password", 0));
  netNames(v, names);
  TEST_ASSERT_EQUAL_STRING("CasaWiFi,Officina,Casa Mare,Laboratorio,Ospiti", names.c_str());
  char pass[65];
  TEST_ASSERT_TRUE(v.loadNetPass("Officina", pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING("nuova-password", pass);
  TEST_ASSERT_TRUE(v.loadNetPass("Ospiti", pass, sizeof pass));  // rete aperta: password vuota
  TEST_ASSERT_EQUAL_STRING("", pass);
  // Dimentica una rete in mezzo: le altre restano nell'ordine; la nuova va in fondo anche se riusa lo spazio libero.
  TEST_ASSERT_TRUE(v.forgetNet("Officina"));
  TEST_ASSERT_FALSE(v.forgetNet("Officina"));
  TEST_ASSERT_FALSE(v.loadNetPass("Officina", pass, sizeof pass));
  TEST_ASSERT_EQUAL(Vault::AddNet::Added, v.addNet("Sesta", "password-12", 0));
  netNames(v, names);
  TEST_ASSERT_EQUAL_STRING("CasaWiFi,Casa Mare,Laboratorio,Ospiti,Sesta", names.c_str());
  TEST_ASSERT_TRUE(kv.m.count("n1-ssid"));  // lo spazio di Officina
  // Ultimo uso
  Vault::NetMeta m[Vault::kNetMax];
  TEST_ASSERT_TRUE(v.touchNet("Laboratorio", 1791707400000LL));
  TEST_ASSERT_FALSE(v.touchNet("Nessuna", 1));
  v.loadNets(m, Vault::kNetMax);
  TEST_ASSERT_EQUAL_INT64(1791707400000LL, m[2].lastUsed);
  TEST_ASSERT_EQUAL_INT64(0, m[4].lastUsed);
  TEST_ASSERT_EQUAL_INT64(1000, m[0].lastUsed);
  // Non valide
  TEST_ASSERT_EQUAL(Vault::AddNet::Invalid, v.addNet("", "password-12", 0));
  TEST_ASSERT_EQUAL(Vault::AddNet::Invalid, v.addNet("123456789012345678901234567890123", "password-12", 0));  // 33 byte
  // Tutte via (Dimentica tutte le reti), anche la rete unica delle versioni ≤ 0.7.
  v.saveWifi("Vecchia", "password-12");
  v.forgetNets();
  TEST_ASSERT_EQUAL(0, netNames(v, names));
  char s1[33];
  TEST_ASSERT_FALSE(v.loadWifi(s1, sizeof s1, pass, sizeof pass));
  TEST_ASSERT_EQUAL(0, int(kv.m.size()));
}

static void test_nets_migrate_single_network_first() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  TEST_ASSERT_EQUAL(Vault::NetMigration::None, v.migrateNets());
  // Firmware ≤ 0.7: una rete sola. Diventa la prima della lista, la vecchia copia sparisce.
  TEST_ASSERT_TRUE(v.saveWifi("CasaWiFi", kPass));
  TEST_ASSERT_EQUAL(Vault::NetMigration::Migrated, v.migrateNets());
  std::string names;
  TEST_ASSERT_EQUAL(1, netNames(v, names));
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", names.c_str());
  char pass[65], s1[33];
  TEST_ASSERT_TRUE(v.loadNetPass("CasaWiFi", pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING(kPass, pass);
  TEST_ASSERT_FALSE(kv.m.count("wifi-ssid") || kv.m.count("wifi-pass"));
  TEST_ASSERT_FALSE(v.loadWifi(s1, sizeof s1, pass, sizeof pass));
  TEST_ASSERT_EQUAL(Vault::NetMigration::None, v.migrateNets());  // secondo avvio: niente da fare
  // Anche se nel frattempo c'erano già altre reti (giro interrotto), la vecchia rete va per prima.
  MapKv kv2;
  Vault w(ef, kv2);
  w.begin();
  w.addNet("Officina", "password-12", 0);
  w.saveWifi("CasaWiFi", kPass);
  TEST_ASSERT_EQUAL(Vault::NetMigration::Migrated, w.migrateNets());
  netNames(w, names);
  TEST_ASSERT_EQUAL_STRING("CasaWiFi,Officina", names.c_str());
  // Copiata ma vecchia copia non ancora tolta (corrente mancata): al riavvio si toglie e basta.
  w.saveWifi("CasaWiFi", kPass);
  TEST_ASSERT_EQUAL(Vault::NetMigration::AlreadyThere, w.migrateNets());
  TEST_ASSERT_EQUAL(2, netNames(w, names));
  TEST_ASSERT_FALSE(kv2.m.count("wifi-ssid"));
  // Versione nuova «in prova» dopo un OTA: copia nella lista ma la vecchia resta (un ritorno alla 0.7 la ritrova);
  // al primo avvio fuori prova la vecchia va via. Dimenticare la rete toglie anche la vecchia copia.
  MapKv kv4;
  Vault t(ef, kv4);
  t.begin();
  t.saveWifi("CasaWiFi", kPass);
  TEST_ASSERT_EQUAL(Vault::NetMigration::Migrated, t.migrateNets(true));
  TEST_ASSERT_TRUE(t.loadWifi(s1, sizeof s1, pass, sizeof pass));
  TEST_ASSERT_EQUAL(1, netNames(t, names));
  TEST_ASSERT_EQUAL(Vault::NetMigration::AlreadyThere, t.migrateNets(true));
  TEST_ASSERT_TRUE(t.loadWifi(s1, sizeof s1, pass, sizeof pass));
  TEST_ASSERT_TRUE(t.forgetNet("CasaWiFi"));
  TEST_ASSERT_FALSE(t.loadWifi(s1, sizeof s1, pass, sizeof pass));
  TEST_ASSERT_EQUAL(Vault::NetMigration::None, t.migrateNets());
  TEST_ASSERT_EQUAL(0, netNames(t, names));
  t.saveWifi("CasaWiFi", kPass);
  TEST_ASSERT_EQUAL(Vault::NetMigration::Migrated, t.migrateNets(true));
  TEST_ASSERT_EQUAL(Vault::NetMigration::AlreadyThere, t.migrateNets(false));
  TEST_ASSERT_FALSE(t.loadWifi(s1, sizeof s1, pass, sizeof pass));
  TEST_ASSERT_EQUAL(1, netNames(t, names));
  // NVS che non scrive: la vecchia resta, niente a metà.
  MapKv kv3;
  Vault z(ef, kv3);
  z.begin();
  z.saveWifi("CasaWiFi", kPass);
  kv3.failPut = true;
  TEST_ASSERT_EQUAL(Vault::NetMigration::Failed, z.migrateNets());
  kv3.failPut = false;
  TEST_ASSERT_TRUE(z.loadWifi(s1, sizeof s1, pass, sizeof pass));
  TEST_ASSERT_EQUAL(0, netNames(z, names));
}

static void test_nets_legacy_calamaio_net_to_list() {
  // M3 («calamaio-net» in chiaro) → rete unica del vault → prima della lista: la catena dell'avvio di secrets::prepare().
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  struct Legacy : LegacyStore {
    std::map<std::string, std::string> m;
    bool exists = true;
    bool openRead() override { return exists; }
    bool get(const char *k, char *out, size_t cap) override {
      auto it = m.find(k);
      if (it == m.end()) return false;
      strncpy(out, it->second.c_str(), cap - 1);
      out[cap - 1] = 0;
      return true;
    }
    bool openWrite() override { return true; }
    void remove(const char *k) override { m.erase(k); }
    void close() override {}
  } st;
  st.m["ssid"] = "CasaWiFi";
  st.m["pass"] = kPass;
  TEST_ASSERT_EQUAL(LegacyResult::Migrated, migrateLegacyWifi(st, v));
  TEST_ASSERT_EQUAL(Vault::NetMigration::Migrated, v.migrateNets());
  std::string names;
  TEST_ASSERT_EQUAL(1, netNames(v, names));
  TEST_ASSERT_EQUAL_STRING("CasaWiFi", names.c_str());
  TEST_ASSERT_TRUE(st.m.empty());
}

static void test_nets_encrypted_and_other_board() {
  FakeEfuse ef;
  MapKv kv;
  Vault v(ef, kv);
  v.begin();
  v.addNet("CasaWiFi", kPass, 0);
  v.addNet("Officina", "altra-password", 0);
  TEST_ASSERT_TRUE(kv.contains(kPass, strlen(kPass)));  // prima della protezione: in chiaro
  char log[256];
  TEST_ASSERT_EQUAL(ProtectResult::Ok, v.protect(log, sizeof log));
  // Tutte le password delle reti ricifrate (test di accettazione 9).
  TEST_ASSERT_FALSE(kv.contains(kPass, strlen(kPass)));
  TEST_ASSERT_FALSE(kv.contains("altra-password", 14));
  TEST_ASSERT_TRUE(kv.m.count("n0-pass.e") && kv.m.count("n1-pass.e"));
  TEST_ASSERT_FALSE(kv.m.count("n0-pass") || kv.m.count("n1-pass"));
  TEST_ASSERT_TRUE(kv.contains("CasaWiFi", 8));  // lo SSID resta in chiaro (M5 §4)
  char pass[65];
  TEST_ASSERT_TRUE(v.loadNetPass("Officina", pass, sizeof pass));
  TEST_ASSERT_EQUAL_STRING("altra-password", pass);
  // Reti aggiunte dopo: cifrate subito.
  v.addNet("Laboratorio", "terza-password", 0);
  TEST_ASSERT_FALSE(kv.contains("terza-password", 14));
  // Backup su un'altra scheda: le reti non si leggono, i loro spazi si riusano.
  FakeEfuse other;
  other.seed = 99;
  char e[8];
  uint8_t ok[32];
  other.random(ok, 32);
  other.program(1, ok, e, sizeof e);
  Vault w(other, kv);
  w.begin();
  std::string names;
  TEST_ASSERT_EQUAL(0, netNames(w, names));
  TEST_ASSERT_FALSE(w.loadNetPass("CasaWiFi", pass, sizeof pass));
  for (int k = 0; k < 5; k++) {
    char s1[16];
    snprintf(s1, sizeof s1, "Rete%d", k);
    TEST_ASSERT_EQUAL(Vault::AddNet::Added, w.addNet(s1, "password-12", 0));
  }
  TEST_ASSERT_EQUAL(5, netNames(w, names));
  TEST_ASSERT_EQUAL_STRING("Rete0,Rete1,Rete2,Rete3,Rete4", names.c_str());
  // Ripristino di fabbrica: tutte le reti via.
  w.factoryReset();
  TEST_ASSERT_EQUAL(0, netNames(w, names));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_unprotected_keeps_plain_secrets);
  RUN_TEST(test_protect_writes_once_and_reencrypts);
  RUN_TEST(test_protect_no_free_block_or_block_taken);
  RUN_TEST(test_backup_on_another_board_is_unreadable);
  RUN_TEST(test_interrupted_migration_finishes_at_boot);
  RUN_TEST(test_factory_reset_and_open_network);
  RUN_TEST(test_record_format_matches_vectors);
  RUN_TEST(test_legacy_absent_namespace_is_not_created);
  RUN_TEST(test_legacy_empty_namespace_is_left_alone);
  RUN_TEST(test_legacy_network_moves_into_vault_then_removed);
  RUN_TEST(test_legacy_vault_failure_keeps_old_copy);
  RUN_TEST(test_nets_order_full_update_forget);
  RUN_TEST(test_nets_migrate_single_network_first);
  RUN_TEST(test_nets_legacy_calamaio_net_to_list);
  RUN_TEST(test_nets_encrypted_and_other_board);
  return UNITY_END();
}
