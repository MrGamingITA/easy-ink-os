#include "vault.h"

#include <string.h>

#include <initializer_list>

#include "crypto.h"
#include "strbuf.h"

namespace cal {

namespace {
const char *const kSalt = "salt";
const char *const kWifiSsid = "wifi-ssid";
const char *const kWifiPass = "wifi-pass";  // segreto: testo + terminatore (anche vuoto: rete aperta)
const char *const kPairKey = "pair-key";    // segreto: 32 byte
const char *const kPairHost = "pair-host";
const char *const kOtaKey = "ota-key";      // segreto: 32 byte (chiave pubblica Ed25519 del PC)
const size_t kMetaLen = 12;  // seq u32 + ultimo uso i64, little endian
const size_t kSaltLen = 16;
const uint8_t kRecVersion = 1;
const size_t kRecHead = 1 + cry::kGcmIv + cry::kGcmTag;
const size_t kSecretMax = 96;

// Nome della versione cifrata: «wifi-pass.e» (le chiavi NVS stanno sotto i 15 caratteri).
void encName(const char *name, char *out, size_t cap) { Sb(out, cap).s(name).s(".e"); }

// Nomi dei record della rete nello spazio k: «n0-ssid», «n0-meta», «n0-pass» (segreto, «n0-pass.e» cifrato).
void slotName(int slot, const char *what, char *out, size_t cap) { Sb(out, cap).s("n").i(slot).s("-").s(what); }

void putMeta(uint8_t *b, uint32_t seq, int64_t used) {
  for (int k = 0; k < 4; k++) b[k] = uint8_t(seq >> (8 * k));
  for (int k = 0; k < 8; k++) b[4 + k] = uint8_t(uint64_t(used) >> (8 * k));
}
void getMeta(const uint8_t *b, uint32_t &seq, int64_t &used) {
  seq = 0;
  uint64_t u = 0;
  for (int k = 0; k < 4; k++) seq |= uint32_t(b[k]) << (8 * k);
  for (int k = 0; k < 8; k++) u |= uint64_t(b[4 + k]) << (8 * k);
  used = int64_t(u);
}
}  // namespace

const char *protectResultText(ProtectResult r) {
  switch (r) {
    case ProtectResult::Ok: return "chiavi protette";
    case ProtectResult::Already: return "chiave già presente nel chip";
    case ProtectResult::NoFreeBlock: return "nessuno spazio chiave libero";
    case ProtectResult::BlockChanged: return "spazio non più libero";
    case ProtectResult::WriteFailed: return "scrittura della eFuse non riuscita";
    case ProtectResult::HmacFailed: return "la periferica HMAC non risponde";
    default: return "salvataggio non riuscito";
  }
}

Vault::~Vault() { cry::wipe(ke_, sizeof ke_); }

bool Vault::deriveKe() {
  haveKe_ = false;
  if (blk_ < 0) return false;
  uint8_t salt[kSaltLen];
  if (kv_.get(kSalt, salt, sizeof salt) != kSaltLen) {
    // Sale nuovo: al primo avvio dopo la protezione o dopo il ripristino di fabbrica.
    hw_.random(salt, sizeof salt);
    if (!kv_.put(kSalt, salt, sizeof salt)) return false;
  }
  cry::Msg m;
  m.str("calamaio-nvs").raw(salt, sizeof salt);
  haveKe_ = hw_.hmac(blk_, m.data(), m.size(), ke_);
  return haveKe_;
}

void Vault::begin() {
  blk_ = hw_.hmacKeyBlock();
  haveKe_ = false;
  if (blk_ < 0) return;  // chiavi non protette: segreti in chiaro come prima di M5
  if (deriveKe()) migrate();  // finisce una migrazione interrotta (segreti ancora in chiaro)
}

bool Vault::putSecret(const char *name, const uint8_t *d, size_t n) {
  char en[24];
  encName(name, en, sizeof en);
  if (!haveKe_) {
    kv_.erase(en);  // record cifrato di un'altra scheda o di una chiave persa: non serve più
    return kv_.put(name, d, n);
  }
  uint8_t rec[kRecHead + kSecretMax], back[kSecretMax];
  if (n > kSecretMax) return false;
  rec[0] = kRecVersion;
  hw_.random(rec + 1, cry::kGcmIv);
  const uint8_t *aad = reinterpret_cast<const uint8_t *>(name);
  if (!cry::gcmEncrypt(ke_, rec + 1, aad, strlen(name), d, n, rec + kRecHead, rec + 1 + cry::kGcmIv)) return false;
  bool ok = kv_.put(en, rec, kRecHead + n);
  // Prima si rilegge il cifrato, poi si cancella il chiaro: un'interruzione non perde niente.
  ok = ok && getSecret(name, back, sizeof back) == n && memcmp(back, d, n) == 0;
  cry::wipe(back, sizeof back);
  if (ok) kv_.erase(name);
  return ok;
}

size_t Vault::getSecret(const char *name, uint8_t *out, size_t cap) {
  char en[24];
  encName(name, en, sizeof en);
  uint8_t rec[kRecHead + kSecretMax];
  const size_t rn = kv_.get(en, rec, sizeof rec);
  if (rn) {
    // Cifrato: si legge solo con la stessa eFuse e lo stesso sale. Altrimenti è come se non ci fosse.
    if (!haveKe_ || rn < kRecHead || rec[0] != kRecVersion || rn - kRecHead > cap) return 0;
    const size_t n = rn - kRecHead;
    const uint8_t *aad = reinterpret_cast<const uint8_t *>(name);
    if (!cry::gcmDecrypt(ke_, rec + 1, aad, strlen(name), rec + kRecHead, n, out, rec + 1 + cry::kGcmIv)) return 0;
    return n;
  }
  return kv_.get(name, out, cap);  // in chiaro (prima della protezione, o migrazione da finire)
}

void Vault::eraseSecret(const char *name) {
  char en[24];
  encName(name, en, sizeof en);
  kv_.erase(name);
  kv_.erase(en);
}

bool Vault::migrate() {
  bool ok = true;
  char names[3 + kNetMax][16] = {};
  scopy(names[0], kWifiPass);
  scopy(names[1], kPairKey);
  scopy(names[2], kOtaKey);
  for (int k = 0; k < kNetMax; k++) slotName(k, "pass", names[3 + k], sizeof names[3 + k]);
  for (const char *name : names) {
    uint8_t plain[kSecretMax];
    const size_t n = kv_.get(name, plain, sizeof plain);
    if (n) ok = putSecret(name, plain, n) && ok;
    cry::wipe(plain, sizeof plain);
  }
  return ok;
}

// ---------- più reti ----------

bool Vault::readSlot(int slot, NetMeta &out, char *pass, size_t passCap) {
  char nm[16];
  uint8_t s[kSsidMax + 1], meta[kMetaLen], p[kPassMax + 1];
  slotName(slot, "ssid", nm, sizeof nm);
  const size_t sn = kv_.get(nm, s, sizeof s);
  if (!sn || sn > kSsidMax) return false;
  slotName(slot, "pass", nm, sizeof nm);
  const size_t pn = getSecret(nm, p, sizeof p);
  // Senza password leggibile (anche vuota, per le reti aperte) la rete non c'è: backup di un'altra scheda.
  const bool ok = pn > 0 && p[pn - 1] == 0 && (!pass || pn <= passCap);
  if (ok) {
    memset(&out, 0, sizeof out);
    memcpy(out.ssid, s, sn);
    out.ssid[sn] = 0;
    out.slot = slot;
    slotName(slot, "meta", nm, sizeof nm);
    if (kv_.get(nm, meta, sizeof meta) == kMetaLen) getMeta(meta, out.seq, out.lastUsed);
    if (pass) memcpy(pass, p, pn);
  }
  cry::wipe(p, sizeof p);
  return ok;
}

bool Vault::slotUsed(int slot) {
  NetMeta m;
  return readSlot(slot, m, nullptr, 0);
}

void Vault::eraseSlot(int slot) {
  char nm[16];
  slotName(slot, "ssid", nm, sizeof nm);
  kv_.erase(nm);
  slotName(slot, "meta", nm, sizeof nm);
  kv_.erase(nm);
  slotName(slot, "pass", nm, sizeof nm);
  eraseSecret(nm);
}

int Vault::loadNets(NetMeta *out, int cap) {
  NetMeta all[kNetMax];
  int n = 0;
  for (int k = 0; k < kNetMax; k++)
    if (readSlot(k, all[n], nullptr, 0)) n++;
  for (int a = 1; a < n; a++) {  // ordine di inserimento (seq; a parità lo spazio)
    const NetMeta x = all[a];
    int b = a - 1;
    while (b >= 0 && (all[b].seq > x.seq || (all[b].seq == x.seq && all[b].slot > x.slot))) { all[b + 1] = all[b]; b--; }
    all[b + 1] = x;
  }
  const int c = n < cap ? n : cap;
  for (int k = 0; k < c; k++) out[k] = all[k];
  return c;
}

int Vault::findNet(const char *ssid) {
  for (int k = 0; k < kNetMax; k++) {
    NetMeta m;
    if (readSlot(k, m, nullptr, 0) && !strcmp(m.ssid, ssid)) return k;
  }
  return -1;
}

bool Vault::loadNetPass(const char *ssid, char *pass, size_t cap) {
  const int k = findNet(ssid);
  NetMeta m;
  return k >= 0 && readSlot(k, m, pass, cap);
}

Vault::AddNet Vault::addNet(const char *ssid, const char *pass, int64_t lastUsed) {
  const size_t sn = ssid ? strlen(ssid) : 0, pn = (pass ? strlen(pass) : 0) + 1;
  if (!sn || sn > kSsidMax || pn > kPassMax + 1) return AddNet::Invalid;
  char nm[16];
  const int have = findNet(ssid);
  if (have >= 0) {  // stessa rete: nuova password, stesso posto nella lista
    slotName(have, "pass", nm, sizeof nm);
    return putSecret(nm, reinterpret_cast<const uint8_t *>(pass ? pass : ""), pn) ? AddNet::Updated : AddNet::Failed;
  }
  NetMeta list[kNetMax];
  const int n = loadNets(list, kNetMax);
  if (n >= kNetMax) return AddNet::Full;
  int slot = -1;
  for (int k = 0; k < kNetMax && slot < 0; k++)
    if (!slotUsed(k)) slot = k;
  if (slot < 0) return AddNet::Full;
  eraseSlot(slot);  // resti illeggibili di un'altra scheda
  uint32_t seq = 1;
  for (int k = 0; k < n; k++)
    if (list[k].seq >= seq) seq = list[k].seq + 1;
  uint8_t meta[kMetaLen];
  putMeta(meta, seq, lastUsed);
  // Prima la password, poi meta e SSID: senza SSID lo spazio non vale come rete (interruzione = niente a metà).
  slotName(slot, "pass", nm, sizeof nm);
  bool ok = putSecret(nm, reinterpret_cast<const uint8_t *>(pass ? pass : ""), pn);
  slotName(slot, "meta", nm, sizeof nm);
  ok = ok && kv_.put(nm, meta, sizeof meta);
  slotName(slot, "ssid", nm, sizeof nm);
  ok = ok && kv_.put(nm, reinterpret_cast<const uint8_t *>(ssid), sn);
  if (!ok) { eraseSlot(slot); return AddNet::Failed; }
  return AddNet::Added;
}

bool Vault::forgetNet(const char *ssid) {
  // Una copia della rete unica (tenuta durante la prova dopo un OTA) tornerebbe nella lista al prossimo avvio.
  uint8_t s[kSsidMax + 1];
  const size_t sn = kv_.get(kWifiSsid, s, sizeof s);
  const bool legacy = sn && sn == strlen(ssid) && !memcmp(s, ssid, sn);
  if (legacy) forgetWifi();
  const int k = findNet(ssid);
  if (k < 0) return legacy;
  eraseSlot(k);
  return true;
}

void Vault::forgetNets() {
  for (int k = 0; k < kNetMax; k++) eraseSlot(k);
  forgetWifi();
}

bool Vault::touchNet(const char *ssid, int64_t lastUsed) {
  const int k = findNet(ssid);
  NetMeta m;
  if (k < 0 || !readSlot(k, m, nullptr, 0)) return false;
  uint8_t meta[kMetaLen];
  putMeta(meta, m.seq, lastUsed);
  char nm[16];
  slotName(k, "meta", nm, sizeof nm);
  return kv_.put(nm, meta, sizeof meta);
}

Vault::NetMigration Vault::migrateNets(bool keepOld) {
  char ssid[kSsidMax + 1], pass[kPassMax + 1];
  uint8_t s[kSsidMax + 1];
  if (!kv_.get(kWifiSsid, s, sizeof s)) return NetMigration::None;  // niente rete unica
  if (!loadWifi(ssid, sizeof ssid, pass, sizeof pass)) {
    // SSID senza password leggibile (backup di un'altra scheda): non è una rete, via la vecchia copia.
    forgetWifi();
    return NetMigration::None;
  }
  NetMigration r;
  if (findNet(ssid) >= 0) {
    r = NetMigration::AlreadyThere;
  } else {
    // Prima della lista: numero d'ordine 0 (le reti aggiunte dopo partono da 1).
    NetMeta list[kNetMax];
    const int n = loadNets(list, kNetMax);
    int slot = -1;
    for (int k = 0; k < kNetMax && slot < 0; k++)
      if (!slotUsed(k)) slot = k;
    bool ok = n < kNetMax && slot >= 0;
    char nm[16];
    if (ok) {
      eraseSlot(slot);
      uint8_t meta[kMetaLen];
      putMeta(meta, 0, 0);
      slotName(slot, "pass", nm, sizeof nm);
      ok = putSecret(nm, reinterpret_cast<const uint8_t *>(pass), strlen(pass) + 1);
      slotName(slot, "meta", nm, sizeof nm);
      ok = ok && kv_.put(nm, meta, sizeof meta);
      slotName(slot, "ssid", nm, sizeof nm);
      ok = ok && kv_.put(nm, reinterpret_cast<const uint8_t *>(ssid), strlen(ssid));
    }
    char back[kPassMax + 1];
    ok = ok && loadNetPass(ssid, back, sizeof back) && !strcmp(back, pass);
    cry::wipe(back, sizeof back);
    if (!ok) {
      if (slot >= 0) eraseSlot(slot);
      cry::wipe(pass, sizeof pass);
      return NetMigration::Failed;
    }
    r = NetMigration::Migrated;
  }
  cry::wipe(pass, sizeof pass);
  if (!keepOld) forgetWifi();
  return r;
}

bool Vault::loadWifi(char *ssid, size_t ssidCap, char *pass, size_t passCap) {
  uint8_t s[kSsidMax + 1], p[kPassMax + 1];
  const size_t sn = kv_.get(kWifiSsid, s, sizeof s);
  const size_t pn = getSecret(kWifiPass, p, sizeof p);
  // Senza password leggibile (anche vuota, per le reti aperte) la rete è da rifare: backup di un'altra scheda.
  const bool ok = sn > 0 && sn <= kSsidMax && pn > 0 && p[pn - 1] == 0 && sn < ssidCap && pn <= passCap;
  if (ok) {
    memcpy(ssid, s, sn);
    ssid[sn] = 0;
    memcpy(pass, p, pn);
  }
  cry::wipe(p, sizeof p);
  return ok;
}

bool Vault::saveWifi(const char *ssid, const char *pass) {
  const size_t sn = strlen(ssid), pn = strlen(pass) + 1;
  if (!sn || sn > kSsidMax || pn > kPassMax + 1) return false;
  return kv_.put(kWifiSsid, reinterpret_cast<const uint8_t *>(ssid), sn) &&
         putSecret(kWifiPass, reinterpret_cast<const uint8_t *>(pass), pn);
}

void Vault::forgetWifi() {
  kv_.erase(kWifiSsid);
  eraseSecret(kWifiPass);
}

bool Vault::loadPair(uint8_t K[32], char *host, size_t hostCap) {
  uint8_t k[kSecretMax];
  const size_t n = getSecret(kPairKey, k, sizeof k);
  const bool ok = n == 32;
  if (ok) memcpy(K, k, 32);
  cry::wipe(k, sizeof k);
  if (host && hostCap) {
    uint8_t h[kHostMax + 1];
    const size_t hn = kv_.get(kPairHost, h, sizeof h);
    const size_t c = hn < hostCap - 1 ? hn : hostCap - 1;
    memcpy(host, h, c);
    host[c] = 0;
  }
  return ok;
}

bool Vault::savePair(const uint8_t K[32], const char *host) {
  const size_t hn = host ? strlen(host) : 0;
  if (hn) kv_.put(kPairHost, reinterpret_cast<const uint8_t *>(host), hn > kHostMax ? kHostMax : hn);
  else kv_.erase(kPairHost);
  eraseSecret(kOtaKey);  // abbinamento nuovo: la chiave OTA si riprende dal servizio (ota-key)
  return putSecret(kPairKey, K, 32);
}

void Vault::forgetPair() {
  eraseSecret(kPairKey);
  kv_.erase(kPairHost);
  eraseSecret(kOtaKey);
}

bool Vault::loadOtaKey(uint8_t pub[32]) {
  uint8_t k[kSecretMax];
  const size_t n = getSecret(kOtaKey, k, sizeof k);
  const bool ok = n == 32;
  if (ok) memcpy(pub, k, 32);
  cry::wipe(k, sizeof k);
  return ok;
}

bool Vault::saveOtaKey(const uint8_t pub[32]) { return putSecret(kOtaKey, pub, 32); }

void Vault::forgetOtaKey() { eraseSecret(kOtaKey); }

ProtectResult Vault::protect(char *log, size_t cap) {
  Sb L(log, cap);
  const int have = hw_.hmacKeyBlock();
  if (have >= 0) {
    blk_ = have;
    L.s("chiave HMAC già nello spazio ").i(have).s(": niente da scrivere. ");
    const bool ok = deriveKe() && migrate();
    L.s(ok ? "segreti cifrati" : "segreti NON ricifrati");
    return ok ? ProtectResult::Already : ProtectResult::SaveFailed;
  }
  const int b = hw_.freeBlock();
  if (b < 0) { L.s("nessuno spazio chiave libero con scopo USER: niente scritto"); return ProtectResult::NoFreeBlock; }
  L.s("spazio scelto BLOCK_KEY").i(b).s(". ");
  if (!hw_.blockFree(b)) { L.s("al ricontrollo non è più libero: niente scritto"); return ProtectResult::BlockChanged; }
  uint8_t key[32], probe[32], want[32], got[32];
  uint8_t any = 0;
  for (int tries = 0; tries < 3 && !any; tries++) {
    hw_.random(key, sizeof key);
    for (uint8_t v : key) any |= v;
  }
  if (!any) { L.s("generatore casuale fermo: niente scritto"); return ProtectResult::WriteFailed; }
  // HMAC atteso calcolato qui con la chiave, prima di dimenticarla: dopo la scrittura la periferica deve dare lo stesso.
  hw_.random(probe, sizeof probe);
  if (!cry::hmacSha256(key, sizeof key, probe, sizeof probe, want)) {  // memoria: senza l'atteso non si scrive l'eFuse
    cry::wipe(key, sizeof key);
    L.s("HMAC atteso non calcolato (memoria): niente scritto");
    return ProtectResult::WriteFailed;
  }
  char err[64] = "";
  const bool written = hw_.program(b, key, err, sizeof err);
  cry::wipe(key, sizeof key);
  if (!written) { L.s("scrittura non riuscita: ").s(err); return ProtectResult::WriteFailed; }
  L.s("scritta (scopo HMAC_UP, protetta in lettura). ");
  if (hw_.hmacKeyBlock() != b || !hw_.hmac(b, probe, sizeof probe, got) || !cry::ctEqual(got, want, sizeof want)) {
    L.s("la verifica HMAC non torna");
    return ProtectResult::HmacFailed;
  }
  blk_ = b;
  if (!deriveKe()) { L.s("sale o KE non disponibili"); return ProtectResult::SaveFailed; }
  if (!migrate()) { L.s("segreti NON ricifrati (restano in chiaro, si riprova all'avvio)"); return ProtectResult::SaveFailed; }
  L.s("verifica HMAC ok, segreti ricifrati");
  return ProtectResult::Ok;
}

void Vault::factoryReset() {
  forgetNets();
  forgetPair();
  kv_.erase(kSalt);
  haveKe_ = false;
  cry::wipe(ke_, sizeof ke_);
}

}  // namespace cal
