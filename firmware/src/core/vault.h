// Segreti sulla scheda (docs/M5-PERMESSI.md §4): password Wi-Fi e chiave di abbinamento K.
//
// Prima di «Proteggi le chiavi» restano in NVS come oggi, in chiaro. Dopo, sono cifrati con AES-256-GCM e
//   KE = HMAC_eFuse("calamaio-nvs" ‖ sale)
// dove HMAC_eFuse è la periferica HMAC con una chiave scritta una volta sola in uno spazio chiave della
// eFuse (scopo HMAC_UP, non leggibile dal software). Un backup della flash o la NVS copiata su un'altra
// scheda non bastano a leggerli: senza la stessa eFuse i record non si decifrano e si trattano come assenti.
//
// Logica pura: eFuse e NVS stanno dietro due interfacce (sulla scheda hw/secrets.cpp, nei test dei finti).
// Formato del record: 0x01 ‖ iv (12) ‖ tag (16) ‖ cifrato, con AAD = nome del record («wifi-pass», «pair-key»,
// «ota-key», «n0-pass» … «n4-pass»).
//
// Più reti (firmware 0.8.0): fino a kNetMax reti nell'ordine di inserimento. Per lo spazio k (0…4) tre record:
// «nK-ssid» (testo, in chiaro), «nK-meta» (12 byte in chiaro: numero d'ordine u32 e ultimo uso i64 in ms epoch,
// little endian; 0 = mai) e il segreto «nK-pass» (password + terminatore, vuota per le reti aperte). La rete unica
// delle versioni ≤ 0.7 («wifi-ssid» + «wifi-pass») passa nella lista come prima con migrateNets().
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {

// eFuse e periferica HMAC dell'ESP32-S3. I blocchi sono gli spazi chiave 0..5 (BLOCK_KEY0..5).
class KeyHw {
 public:
  virtual ~KeyHw() {}
  virtual int hmacKeyBlock() = 0;    // spazio con scopo HMAC_UP già scritto, -1 se nessuno
  virtual int freeBlock() = 0;       // primo spazio libero (tutto zero, scopo USER, senza protezioni), -1 se nessuno
  virtual bool blockFree(int blk) = 0;  // ricontrollo subito prima di scrivere
  // Scrive key nello spazio blk con scopo HMAC_UP e protezione in lettura e scrittura. IRREVERSIBILE.
  virtual bool program(int blk, const uint8_t key[32], char *err, size_t cap) = 0;
  virtual bool hmac(int blk, const uint8_t *msg, size_t n, uint8_t out[32]) = 0;
  virtual void random(uint8_t *out, size_t n) = 0;
};

// Archivio chiave → byte (sulla scheda: Preferences, spazio «calamaio-sec»).
class KvStore {
 public:
  virtual ~KvStore() {}
  virtual size_t get(const char *key, uint8_t *out, size_t cap) = 0;  // 0 = assente (o più lungo di cap)
  virtual bool put(const char *key, const uint8_t *d, size_t n) = 0;
  virtual void erase(const char *key) = 0;
};

enum class ProtectResult : uint8_t {
  Ok,
  Already,       // c'era già una chiave HMAC_UP: si usa quella, niente da scrivere
  NoFreeBlock,   // nessuno spazio libero con scopo USER
  BlockChanged,  // lo spazio scelto non era più libero al ricontrollo: niente scritto
  WriteFailed,   // la scrittura della eFuse non è andata (vedi log)
  HmacFailed,    // scritta, ma la periferica HMAC non risponde come deve
  SaveFailed,    // NVS: sale o segreti non salvati (restano in chiaro, si riprova al prossimo avvio)
};
const char *protectResultText(ProtectResult r);

class Vault {
 public:
  static const size_t kSsidMax = 32, kPassMax = 64, kHostMax = 47;
  Vault(KeyHw &hw, KvStore &kv) : hw_(hw), kv_(kv), blk_(-1), haveKe_(false) {}
  ~Vault();

  // Avvio: c'è una chiave nella eFuse? Con il sale si ricava KE. Finisce una migrazione interrotta.
  void begin();
  bool keysProtected() const { return blk_ >= 0; }
  int block() const { return blk_; }

  // Rete unica delle versioni ≤ 0.7 (e della migrazione da «calamaio-net»): resta solo per passare nella lista.
  bool loadWifi(char *ssid, size_t ssidCap, char *pass, size_t passCap);  // false = nessuna rete (o illeggibile)
  bool saveWifi(const char *ssid, const char *pass);
  void forgetWifi();

  // ---------- più reti (0.8.0) ----------
  static const int kNetMax = 5;
  struct NetMeta {
    char ssid[kSsidMax + 1];
    int64_t lastUsed;  // ms epoch dell'ultimo collegamento, 0 = mai
    uint32_t seq;      // ordine di inserimento
    int slot;          // spazio 0…4 nella NVS
  };
  // Reti nell'ordine di inserimento. Una rete con la password illeggibile (backup di un'altra scheda, chiave persa)
  // non c'è: il suo spazio torna libero per la prossima.
  int loadNets(NetMeta *out, int cap);
  bool loadNetPass(const char *ssid, char *pass, size_t cap);  // false = rete assente o illeggibile
  enum class AddNet : uint8_t { Added, Updated, Full, Invalid, Failed };
  // Rete nuova in fondo alla lista; se lo SSID c'è già cambia solo la password (l'ordine resta). Invalid: SSID vuoto o
  // oltre 32 byte, password oltre 64 byte (il protocollo e il portale si fermano a 63). Failed: NVS (niente di mezzo salvato resta leggibile come rete).
  AddNet addNet(const char *ssid, const char *pass, int64_t lastUsed);
  bool forgetNet(const char *ssid);   // false = non c'era (toglie anche la rete unica ≤ 0.7 con lo stesso SSID)
  void forgetNets();                  // tutte, compresa la rete unica delle versioni ≤ 0.7
  bool touchNet(const char *ssid, int64_t lastUsed);  // ultimo uso (ms epoch); false = rete assente o NVS
  enum class NetMigration : uint8_t { None, Migrated, AlreadyThere, Failed };
  // Rete unica (≤ 0.7) → prima della lista, riletta uguale, poi la vecchia copia via. AlreadyThere: c'era già (giro
  // interrotto a metà, o avvio precedente in prova), si toglie solo la vecchia. Failed: la vecchia resta, si riprova al
  // prossimo avvio. keepOld: la vecchia copia resta (versione nuova «in prova» dopo un OTA: se torna la versione di
  // prima, ritrova la sua rete); si toglie al primo avvio non in prova.
  NetMigration migrateNets(bool keepOld = false);

  bool loadPair(uint8_t K[32], char *host, size_t hostCap);  // false = nessun abbinamento (o illeggibile)
  // Un nuovo abbinamento (o Dimentica abbinamento) cancella anche la chiave OTA: quella di un PC diverso vale
  // solo dopo un nuovo abbinamento (docs/OTA.md §2).
  bool savePair(const uint8_t K[32], const char *host);
  void forgetPair();

  // Chiave pubblica Ed25519 degli aggiornamenti (ota-key, docs/OTA.md §2): cifrata come gli altri segreti.
  bool loadOtaKey(uint8_t pub[32]);
  bool saveOtaKey(const uint8_t pub[32]);
  void forgetOtaKey();

  // «Proteggi le chiavi»: scrive la eFuse (solo se serve) e ricifra subito i segreti esistenti.
  // log: righe leggibili su quello che è successo (per la seriale).
  ProtectResult protect(char *log, size_t cap);
  // Ripristino di fabbrica: segreti e sale via. La eFuse non si può cancellare e resta.
  void factoryReset();

 private:
  bool deriveKe();
  bool putSecret(const char *name, const uint8_t *d, size_t n);
  size_t getSecret(const char *name, uint8_t *out, size_t cap);  // 0 = assente o non decifrabile
  void eraseSecret(const char *name);
  bool migrate();  // segreti in chiaro → cifrati (con KE)
  bool slotUsed(int slot);  // c'è una rete leggibile nello spazio
  bool readSlot(int slot, NetMeta &out, char *pass, size_t passCap);  // pass può essere nullptr
  void eraseSlot(int slot);
  int findNet(const char *ssid);  // spazio della rete, -1 se assente

  KeyHw &hw_;
  KvStore &kv_;
  int blk_;
  bool haveKe_;
  uint8_t ke_[32];
};

}  // namespace cal
