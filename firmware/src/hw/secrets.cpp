// Vedi secrets.h. API dell'IDF 4.4 (core Arduino 2.0.17), verificate sugli header del pacchetto:
//   efuse/include/esp_efuse.h          esp_efuse_find_purpose, esp_efuse_find_unused_key_block,
//                                      esp_efuse_key_block_unused, esp_efuse_get_key_purpose, esp_efuse_write_key,
//                                      esp_efuse_get_key_dis_read / _set_key_dis_read
//   esp_hw_support/.../esp32s3/esp_hmac.h  esp_hmac_calculate (libesp_hw_support.a, già linkata da Arduino)
//   esp_timer/include/esp_timer.h      esp_timer_create, esp_timer_start_once (riavvio dopo requestScrub)
#ifdef ARDUINO
#include "hw/secrets.h"

#include <Arduino.h>
#include <Preferences.h>

#include "core/legacy_wifi.h"
#include "core/strbuf.h"
#include <esp_efuse.h>
#include <esp_hmac.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdlib.h>

#include "core/crypto.h"
#include "core/nvs_scrub.h"
#include "core/strbuf.h"
#include "hw/nvs_idf.h"
#include "hw/logtee.h"
#include "hw/ota_hw.h"

namespace secrets {

namespace {

esp_efuse_block_t blockOf(int k) { return static_cast<esp_efuse_block_t>(EFUSE_BLK_KEY0 + k); }

class EspKeyHw : public cal::KeyHw {
 public:
  int hmacKeyBlock() override {
    esp_efuse_block_t b;
    if (!esp_efuse_find_purpose(ESP_EFUSE_KEY_PURPOSE_HMAC_UP, &b)) return -1;
    return static_cast<int>(b) - static_cast<int>(EFUSE_BLK_KEY0);
  }
  int freeBlock() override {
    const esp_efuse_block_t b = esp_efuse_find_unused_key_block();
    return b == EFUSE_BLK_KEY_MAX ? -1 : static_cast<int>(b) - static_cast<int>(EFUSE_BLK_KEY0);
  }
  bool blockFree(int k) override {
    if (k < 0 || k > 5) return false;
    const esp_efuse_block_t b = blockOf(k);
    // Tutte le condizioni, una per una: vuoto, scopo USER, nessuna protezione di lettura o scrittura.
    const bool unused = esp_efuse_key_block_unused(b), user = esp_efuse_get_key_purpose(b) == ESP_EFUSE_KEY_PURPOSE_USER,
               empty = esp_efuse_block_is_empty(b), rd = esp_efuse_get_key_dis_read(b), wr = esp_efuse_get_key_dis_write(b),
               pwr = esp_efuse_get_keypurpose_dis_write(b);
    LogSerial.printf("[chiavi] ricontrollo BLOCK_KEY%d: libero %d, scopo USER %d, vuoto %d, protetto in lettura %d, "
                  "in scrittura %d, scopo bloccato %d\n", k, unused, user, empty, rd, wr, pwr);
    return unused && user && empty && !rd && !wr && !pwr;
  }
  bool program(int k, const uint8_t key[32], char *err, size_t cap) override {
    const esp_efuse_block_t b = blockOf(k);
    LogSerial.printf("[chiavi] SCRITTURA IRREVERSIBILE della eFuse: BLOCK_KEY%d, scopo HMAC_UP\n", k);
    esp_err_t e = esp_efuse_write_key(b, ESP_EFUSE_KEY_PURPOSE_HMAC_UP, key, 32);
    if (e != ESP_OK) { cal::Sb(err, cap).s("esp_efuse_write_key: ").s(esp_err_to_name(e)); return false; }
    // La chiave non si deve poter rileggere dal software: se l'IDF non l'ha già protetta, lo si fa qui.
    if (!esp_efuse_get_key_dis_read(b)) {
      e = esp_efuse_set_key_dis_read(b);
      if (e != ESP_OK) { cal::Sb(err, cap).s("esp_efuse_set_key_dis_read: ").s(esp_err_to_name(e)); return false; }
    }
    LogSerial.printf("[chiavi] BLOCK_KEY%d: scopo %d, protetto in lettura %d, in scrittura %d\n", k,
                  int(esp_efuse_get_key_purpose(b)), esp_efuse_get_key_dis_read(b), esp_efuse_get_key_dis_write(b));
    return true;
  }
  bool hmac(int k, const uint8_t *msg, size_t n, uint8_t out[32]) override {
    if (k < 0 || k > 5) return false;
    return esp_hmac_calculate(static_cast<hmac_key_id_t>(HMAC_KEY0 + k), msg, n, out) == ESP_OK;
  }
  void random(uint8_t *out, size_t n) override { esp_fill_random(out, n); }  // casuale vero con la radio accesa
};

class PrefsKv : public cal::KvStore {
 public:
  void begin() { p_.begin("calamaio-sec", false); }
  void end() { p_.end(); }  // prima della pulizia: la cancellazione invalida gli handle aperti
  size_t get(const char *key, uint8_t *out, size_t cap) override {
    if (!p_.isKey(key)) return 0;
    const size_t n = p_.getBytesLength(key);
    if (!n || n > cap) return 0;
    return p_.getBytes(key, out, n);
  }
  bool put(const char *key, const uint8_t *d, size_t n) override { return p_.putBytes(key, d, n) == n; }
  void erase(const char *key) override {
    if (p_.isKey(key)) p_.remove(key);
  }

 private:
  Preferences p_;
};

EspKeyHw gHw;
PrefsKv gKv;
cal::Vault gVault(gHw, gKv);
SemaphoreHandle_t gMx = nullptr;

struct Lock {
  Lock() { xSemaphoreTake(gMx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(gMx); }
};

// Il driver Wi-Fi dell'IDF salva la configurazione (SSID e password compresi) in «nvs.net80211» quando la usa.
// Il core Arduino 2.0.17 chiama esp_wifi_init() con nvs_enable = 1 e solo DOPO esp_wifi_set_storage(RAM):
// nell'intervallo il driver apre il namespace e ci scrive (visto dal vivo: 1 voce ricreata a ogni avvio).
// Da questa versione net::wifiPreInit() inizializza il driver con nvs_enable = 0 prima del core, quindi qui
// non dovrebbe esserci più niente: la cancellazione resta per la copia del firmware di fabbrica e delle
// versioni precedenti. Prima di cancellare si registrano nome, tipo e misura di ogni voce (mai il valore),
// e se è una delle chiavi che possono contenere segreti.
bool secretKey(const char *k) {
  static const char *const kSecret[] = {"sta.ssid", "sta.pswd", "sta.pmk", "sta.apinfo", "ap.ssid", "ap.passwd", "ap.pmk", "ap.pmk_info"};
  for (const char *s : kSecret)
    if (!strcmp(k, s)) return true;
  return false;
}

const char *nvsTypeName(nvs_type_t t) {
  switch (t) {
    case NVS_TYPE_U8: return "u8";
    case NVS_TYPE_I8: return "i8";
    case NVS_TYPE_U16: return "u16";
    case NVS_TYPE_I16: return "i16";
    case NVS_TYPE_U32: return "u32";
    case NVS_TYPE_I32: return "i32";
    case NVS_TYPE_U64: return "u64";
    case NVS_TYPE_I64: return "i64";
    case NVS_TYPE_STR: return "stringa";
    case NVS_TYPE_BLOB: return "blob";
    default: return "?";
  }
}

int eraseSystemWifiCopy() {
  int n = 0, secret = 0;
  nvs_handle_t h;
  const bool open = nvs_open("nvs.net80211", NVS_READONLY, &h) == ESP_OK;
  nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, "nvs.net80211", NVS_TYPE_ANY);
  while (it) {
    nvs_entry_info_t e;
    nvs_entry_info(it, &e);
    size_t len = 0;
    if (open && e.type == NVS_TYPE_BLOB) nvs_get_blob(h, e.key, nullptr, &len);
    else if (open && e.type == NVS_TYPE_STR) nvs_get_str(h, e.key, nullptr, &len);
    const bool sec = secretKey(e.key);
    secret += sec;
    LogSerial.printf("[chiavi] nvs.net80211: voce «%s» (%s%s%u byte)%s\n", e.key, nvsTypeName(e.type), len ? ", " : "",
                  unsigned(len), sec ? " · può contenere segreti" : " · nessun segreto (configurazione)");
    n++;
    it = nvs_entry_next(it);  // NULL alla fine: niente da rilasciare
  }
  if (open) nvs_close(h);
  if (!n) return 0;
  LogSerial.printf("[chiavi] nvs.net80211: %d voci, %d con possibili segreti\n", n, secret);
  if (nvs_open("nvs.net80211", NVS_READWRITE, &h) != ESP_OK) return -1;
  esp_err_t e = nvs_erase_all(h);
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK ? n : -1;
}

// Rete salvata da M3 («calamaio-net», stringhe in chiaro): passa nel vault (core/legacy_wifi). Il namespace si apre
// in sola lettura: aperto in scrittura la NVS lo ricreava vuoto a ogni avvio (DECISIONI, prova 9). false = c'era
// una rete da spostare e la copia non è riuscita (non si cancella niente).
class PrefsLegacy : public cal::LegacyStore {
 public:
  bool openRead() override { return p_.begin("calamaio-net", true); }  // false se il namespace non esiste
  bool get(const char *key, char *out, size_t cap) override {
    if (!p_.isKey(key)) return false;
    const String v = p_.getString(key, "");
    cal::scopy(out, cap, v.c_str());
    return true;
  }
  bool openWrite() override { return p_.begin("calamaio-net", false); }
  void remove(const char *key) override { p_.remove(key); }
  void close() override { p_.end(); }

 private:
  Preferences p_;
};

bool migrateLegacyWifi() {
  PrefsLegacy st;
  const cal::LegacyResult r = cal::migrateLegacyWifi(st, gVault);
  if (r == cal::LegacyResult::Migrated || r == cal::LegacyResult::Failed)
    LogSerial.printf("[chiavi] calamaio-net: %s%s\n", cal::legacyResultText(r),
                  r == cal::LegacyResult::Migrated ? ", riletta uguale: vecchia copia tolta" : ": resta in calamaio-net");
  return r != cal::LegacyResult::Failed;
}

// ---------- pulizia profonda della NVS (core/nvs_scrub) ----------
// Bandiera: «calamaio-scrub/scrub_v1» (u8). Assente = pulizia da fare al prossimo avvio.
const char *const kScrubNs = "calamaio-scrub";
const char *const kScrubKey = "scrub_v1";
// Limiti: valori ≤ 16 KB (la partizione da 20 KB ha 4 pagine utili × 126 voci × 32 byte = 16 128 byte);
// arena = 16 KB + 4 KB per rileggere il valore più lungo; tabella da 512 voci (≥ 4 × 126 = 504).
const size_t kScrubMaxBytes = 16 * 1024, kScrubArena = 20 * 1024;
const int kScrubItems = 512;

bool gPrepared = false, gDropNet = false, gDropLegacy = false;
bool gNetsMigrated = false;  // 0.8.0: la rete unica è passata nella lista a questo avvio (la vecchia copia è fra le cancellate)
esp_timer_handle_t gRestartTimer = nullptr;

bool scrubFlag() {
  nvs_handle_t h;
  if (nvs_open(kScrubNs, NVS_READONLY, &h) != ESP_OK) return false;  // namespace mai creato
  uint8_t v = 0;
  const bool on = nvs_get_u8(h, kScrubKey, &v) == ESP_OK && v == 1;
  nvs_close(h);
  return on;
}

bool setScrubFlag(bool on) {
  nvs_handle_t h;
  esp_err_t e = nvs_open(kScrubNs, NVS_READWRITE, &h);
  if (e != ESP_OK) return false;
  e = on ? nvs_set_u8(h, kScrubKey, 1) : nvs_erase_key(h, kScrubKey);
  if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}

// Quante volte K e la password Wi-Fi compaiono in chiaro nei byte grezzi della partizione «nvs», voci
// cancellate comprese: la stessa ricerca del dump della prova 9, fatta dalla scheda. Mai i valori nel log.
void logSecretsInFlash(const char *when) {
  char host[48], pass[65];
  uint8_t K[32];
  cal::Vault::NetMeta nets[cal::Vault::kNetMax];
  const int nn = gVault.loadNets(nets, cal::Vault::kNetMax);
  const bool pair = gVault.loadPair(K, host, sizeof host);
  size_t part = 0;
  const int nk = pair ? nvsidf::countInPartition(K, sizeof K, &part) : 0;
  cal::cry::wipe(K, sizeof K);
  int withPass = 0, np = 0;  // reti con password e somma delle occorrenze in chiaro delle loro password
  for (int k = 0; k < nn; k++) {
    if (!gVault.loadNetPass(nets[k].ssid, pass, sizeof pass) || !pass[0]) continue;
    withPass++;
    np += nvsidf::countInPartition(reinterpret_cast<const uint8_t *>(pass), strlen(pass), &part);
  }
  cal::cry::wipe(pass, sizeof pass);
  if (!withPass && !pair) { LogSerial.printf("[chiavi] ricerca nella flash (%s): nessuna rete con password e nessun abbinamento\n", when); return; }
  LogSerial.printf("[chiavi] ricerca nella flash (%s, partizione nvs %u byte): K in chiaro %s%d volte, password Wi-Fi in chiaro %d volte su %d reti con password · %s\n",
                when, unsigned(part), pair ? "" : "(nessun abbinamento) ", nk, np, withPass,
                gVault.keysProtected() ? "attese 0 (chiavi protette)" : "attesa 1 per ciascuna (chiavi non protette: la copia valida è in chiaro)");
}

// La pulizia vera: nessun handle NVS deve essere aperto (gKv chiuso da chi chiama).
void scrubNow() {
  const char *drop[2];
  int nd = 0;
  if (gDropNet) drop[nd++] = "nvs.net80211";
  if (gDropLegacy) drop[nd++] = "calamaio-net";
  const cal::ScrubConfig cfg = {drop, nd, kScrubMaxBytes};
  cal::NvsItem *items = static_cast<cal::NvsItem *>(calloc(kScrubItems, sizeof(cal::NvsItem)));
  uint8_t *arena = static_cast<uint8_t *>(malloc(kScrubArena));
  if (!items || !arena) {
    free(items);
    free(arena);
    LogSerial.printf("[chiavi] pulizia NVS NON fatta: memoria insufficiente (heap libero %u), niente cancellato\n", unsigned(ESP.getFreeHeap()));
    return;
  }
  char log[256];
  const uint32_t t0 = millis();
  cal::ScrubReport r;
  {
    nvsidf::Port port;  // chiude il suo handle all'uscita
    r = cal::scrubNvs(port, cfg, items, kScrubItems, arena, kScrubArena, log, sizeof log);
  }
  const uint32_t dt = millis() - t0;
  free(items);
  free(arena);  // azzerata da scrubNvs
  LogSerial.printf("[chiavi] pulizia NVS: %d voci trovate in tutti i namespace, %d non riscritte (%s%s%s) · %s\n", r.found,
                r.dropped, gDropNet ? "nvs.net80211 " : "", gDropLegacy ? "calamaio-net" : "", nd ? "" : "nessun namespace escluso",
                log);
  if (r.outcome == cal::ScrubOutcome::Done) {
    const bool flag = setScrubFlag(true);
    LogSerial.printf("[chiavi] pulizia NVS: %d voci riscritte, %u byte, partizione cancellata fisicamente (%u ms, %d %s)%s\n",
                  r.kept, unsigned(r.bytes), unsigned(dt), r.attempts, r.attempts == 1 ? "giro" : "giri",
                  flag ? "" : " · ERRORE: bandiera scrub_v1 non scritta, si rifà al prossimo avvio");
  } else if (r.outcome == cal::ScrubOutcome::WriteFailed) {
    LogSerial.printf("[chiavi] ERRORE pulizia NVS: %s dopo la cancellazione: voci perse, la scheda riparte in configurazione\n",
                  cal::scrubOutcomeText(r.outcome));
  } else {
    LogSerial.printf("[chiavi] pulizia NVS NON fatta: %s (si riprova al prossimo avvio)\n", cal::scrubOutcomeText(r.outcome));
  }
}

void restartForScrub(void *) {
  xSemaphoreTake(gMx, portMAX_DELAY);  // nessuna operazione del vault a metà
  LogSerial.println("[chiavi] riavvio per la pulizia della NVS");
  Serial.flush();
  esp_restart();
}

void requestScrubLocked(const char *why) {
  if (!setScrubFlag(false)) {
    LogSerial.printf("[chiavi] %s: ERRORE, bandiera scrub_v1 non tolta: niente pulizia né riavvio\n", why);
    return;
  }
  if (!gRestartTimer) {
    esp_timer_create_args_t a = {};
    a.callback = restartForScrub;
    a.name = "scrub-restart";
    if (esp_timer_create(&a, &gRestartTimer) != ESP_OK) gRestartTimer = nullptr;
  }
  if (!gRestartTimer) {
    LogSerial.printf("[chiavi] %s: pulizia NVS al prossimo avvio (timer di riavvio non creato)\n", why);
    return;
  }
  esp_timer_stop(gRestartTimer);  // già armato: si riparte da adesso
  esp_timer_start_once(gRestartTimer, uint64_t(kScrubRestartMs) * 1000);
  LogSerial.printf("[chiavi] %s: le voci cancellate restano nella flash fino alla pulizia, riavvio fra %u ms\n", why,
                unsigned(kScrubRestartMs));
}

// Il lavoro di avvio che toglie voci: va fatto prima della pulizia, così le voci tolte spariscono con lei.
void prepare() {
  if (gPrepared) return;
  gPrepared = true;
  gKv.begin();
  gVault.begin();
  const bool migrated = migrateLegacyWifi();
  // 0.8.0: la rete unica delle versioni precedenti diventa la prima della lista (riletta uguale, poi tolta).
  {
    using M = cal::Vault::NetMigration;
    // Versione «in prova» dopo un OTA: la vecchia copia resta, così un ritorno alla versione di prima ritrova la rete.
    const bool trial = otahw::runningState() == cal::ota::ImgState::PendingVerify;
    const M r = gVault.migrateNets(trial);
    if (trial && r == M::Migrated) LogSerial.println("[chiavi] reti: versione in prova, la copia per la versione di prima resta fino al prossimo avvio");
    gNetsMigrated = !trial && (r == M::Migrated || r == M::AlreadyThere);
    if (r == M::Migrated) LogSerial.println("[chiavi] reti: la rete salvata dalla versione precedente è la prima della lista (riletta uguale, vecchia copia tolta)");
    else if (r == M::AlreadyThere) LogSerial.println("[chiavi] reti: rete della versione precedente già nella lista, vecchia copia tolta");
    else if (r == M::Failed) LogSerial.println("[chiavi] reti: ERRORE, la rete della versione precedente non è passata nella lista (resta dov'era, si riprova al prossimo avvio)");
  }
  // La copia di sistema si cancella solo se c'è almeno una rete nel vault e si rilegge: mai l'unica copia.
  cal::Vault::NetMeta nets[cal::Vault::kNetMax];
  const int nn = gVault.loadNets(nets, cal::Vault::kNetMax);
  const bool inVault = nn > 0;
  const char *rs = inVault ? nets[0].ssid : "";
  gDropLegacy = migrated;
  gDropNet = migrated && inVault;
  if (!migrated) {
    LogSerial.println("[chiavi] nvs.net80211 NON cancellata: la copia della rete nel vault non è riuscita");
  } else if (!inVault) {
    LogSerial.println("[chiavi] nvs.net80211 NON cancellata: nessuna rete leggibile nel vault (si riprova al prossimo avvio)");
  } else {
    const int sys = eraseSystemWifiCopy();
    if (sys > 0) LogSerial.printf("[chiavi] %d reti nel vault (prima «%s»): copia in chiaro del Wi-Fi di sistema (nvs.net80211) cancellata, %d voci\n", nn, rs, sys);
    else if (sys == 0) LogSerial.println("[chiavi] nvs.net80211: nessuna copia di sistema");
    else LogSerial.println("[chiavi] ERRORE: nvs.net80211 non cancellata (nvs_open/erase/commit)");
  }
}

}  // namespace

void bootScrub() {
  if (!gMx) gMx = xSemaphoreCreateMutex();
  Lock l;
  const bool flag = scrubFlag();
  // Corrente mancata fra la cancellazione e la riscrittura: la NVS è vuota e la bandiera non c'è.
  if (!flag && nvsidf::liveEntries() == 0)
    LogSerial.println("[chiavi] NVS vuota e bandiera scrub_v1 assente: prima accensione, oppure è mancata la corrente durante "
                   "una pulizia. Si parte in configurazione (Wi-Fi e abbinamento da rifare)");
  prepare();
  if (flag && gNetsMigrated) {
    // La vecchia copia della rete (in chiaro se le chiavi non sono protette) resta fra le voci cancellate: si pulisce ora.
    LogSerial.println("[chiavi] pulizia NVS: la rete è passata nella lista, la vecchia copia va tolta anche dalla flash");
  } else if (flag) {
    LogSerial.println("[chiavi] pulizia NVS: già fatta (scrub_v1), niente da fare");
    logSecretsInFlash("avvio");
    return;
  }
  logSecretsInFlash("prima della pulizia");
  gKv.end();
  scrubNow();
  gKv.begin();
  logSecretsInFlash("dopo la pulizia");
}

void begin() {
  if (!gMx) gMx = xSemaphoreCreateMutex();
  Lock l;
  prepare();  // niente da fare se bootScrub() l'ha già fatto
  char why[48] = "";
  const bool selfOk = cal::cry::selfTest(why, sizeof why);
  LogSerial.printf("[chiavi] crittografia (mbedTLS): autoverifica %s%s\n", selfOk ? "ok" : "NON RIUSCITA: ", why);
  if (gVault.keysProtected()) LogSerial.printf("[chiavi] chiavi protette: chiave HMAC in BLOCK_KEY%d, segreti cifrati\n", gVault.block());
  else LogSerial.println("[chiavi] chiavi non protette: segreti in chiaro come prima di M5 (Menu › Sicurezza › Proteggi le chiavi)");
}

bool keysProtected() { Lock l; return gVault.keysProtected(); }

int loadNets(cal::Vault::NetMeta *out, int cap) { Lock l; return gVault.loadNets(out, cap); }
bool loadNetPass(const char *ssid, char *pass, size_t cap) { Lock l; return gVault.loadNetPass(ssid, pass, cap); }
cal::Vault::AddNet addNet(const char *ssid, const char *pass, int64_t lastUsed) {
  Lock l;
  const cal::Vault::AddNet r = gVault.addNet(ssid, pass, lastUsed);
  if (r == cal::Vault::AddNet::Updated) {
    // La password vecchia resta fra le voci cancellate (in chiaro, se le chiavi non sono protette): pulizia al
    // prossimo avvio, senza interrompere adesso.
    if (setScrubFlag(false)) LogSerial.printf("[chiavi] rete «%s»: password cambiata, pulizia profonda della NVS al prossimo avvio\n", ssid);
    else LogSerial.printf("[chiavi] rete «%s»: password cambiata, ERRORE: bandiera scrub_v1 non tolta\n", ssid);
  }
  return r;
}
bool forgetNet(const char *ssid, const char *why) {
  Lock l;
  const bool had = gVault.forgetNet(ssid);
  if (had) requestScrubLocked(why);
  return had;
}
bool dropNet(const char *ssid) { Lock l; return gVault.forgetNet(ssid); }
void forgetWifi() {
  Lock l;
  cal::Vault::NetMeta nets[cal::Vault::kNetMax];
  char s[33], p[65];
  const bool had = gVault.loadNets(nets, cal::Vault::kNetMax) > 0 || gVault.loadWifi(s, sizeof s, p, sizeof p);
  cal::cry::wipe(p, sizeof p);
  gVault.forgetNets();
  if (had) requestScrubLocked("Dimentica tutte le reti");
}
bool touchNet(const char *ssid, int64_t lastUsed) { Lock l; return gVault.touchNet(ssid, lastUsed); }

bool loadPair(uint8_t K[32], char *host, size_t hostCap) { Lock l; return gVault.loadPair(K, host, hostCap); }
bool savePair(const uint8_t K[32], const char *host) { Lock l; return gVault.savePair(K, host); }
void forgetPair() {
  Lock l;
  uint8_t K[32];
  const bool had = gVault.loadPair(K, nullptr, 0);
  cal::cry::wipe(K, sizeof K);
  gVault.forgetPair();
  if (had) requestScrubLocked("Dimentica abbinamento");
}

bool loadOtaKey(uint8_t pub[32]) { Lock l; return gVault.loadOtaKey(pub); }
bool saveOtaKey(const uint8_t pub[32]) { Lock l; return gVault.saveOtaKey(pub); }

cal::ProtectResult protect() {
  Lock l;
  LogSerial.println("[chiavi] Proteggi le chiavi: confermato sul display (2× e tenuto 3 s)");
  char log[256];
  const cal::ProtectResult r = gVault.protect(log, sizeof log);
  LogSerial.printf("[chiavi] %s · %s\n", cal::protectResultText(r), log);
  // I segreti in chiaro ora sono voci cancellate, ma i loro byte restano nella flash (prova 9): pulizia.
  if (r == cal::ProtectResult::Ok || r == cal::ProtectResult::Already) requestScrubLocked("Proteggi le chiavi");
  return r;
}

void factoryReset() {
  Lock l;
  gVault.factoryReset();
  LogSerial.println("[chiavi] ripristino di fabbrica: Wi-Fi, abbinamento e sale cancellati (la chiave nel chip resta)");
  requestScrubLocked("Ripristino di fabbrica");  // il loop riavvia comunque dopo 2,6 s
}

void requestScrub(const char *why) { Lock l; requestScrubLocked(why); }

void scrubAtNextBoot(const char *why) {
  Lock l;
  if (setScrubFlag(false)) LogSerial.printf("[chiavi] %s: pulizia profonda della NVS al prossimo avvio\n", why);
  else LogSerial.printf("[chiavi] %s: ERRORE, bandiera scrub_v1 non tolta\n", why);
}

}  // namespace secrets
#endif  // ARDUINO
