#include "hw/blepad.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_random.h>
#include <string.h>

#ifdef USING_NIMBLE_ARDUINO_HEADERS
#include "nimble/nimble/host/include/host/ble_hs.h"
#else
#include "host/ble_hs.h"
#endif

#include "core/pad_bonds.h"
#include "core/pad_phone.h"
#include "hw/logtee.h"

// 0.14.0: NimBLE riempie la lista di risoluzione del controller con gli IRK salvati ma la accende solo con un
// indirizzo proprio privato (ble_hs_pvcy_ensure_started); la scheda usa quello pubblico. Senza, l'iPhone (che
// annuncia con un indirizzo privato che cambia) non sarebbe mai riconosciuto come associato.
extern "C" int ble_hs_pvcy_set_resolve_enabled(int enable);

namespace cal {
namespace blepad {
namespace {

static_assert(pad::kIoDisplayOnly == BLE_HS_IO_DISPLAY_ONLY, "core/pad_bonds: valore HCI");
static_assert(pad::kIoNoInputOutput == BLE_HS_IO_NO_INPUT_OUTPUT, "core/pad_bonds: valore HCI");

const uint16_t kAppearanceGamepad = 0x03C4;
const uint32_t kScanMs = 5000;
const size_t kMinReportMap = 64;  // dal vivo: 22 byte (troncato) al primo collegamento, 283 quello vero
const int kShortMapRetries = 2;
const uint16_t kItvMin = 6, kItvMax = 12;             // Xbox: 7,5–15 ms; il 1914 chiede 7,5 ms e lo si accetta
const uint16_t kPhoneItvMin = 12, kPhoneItvMax = 24;  // telefono: 15–30 ms (regole di Apple per le periferiche)
const char *const kBondNs = "nimble_bond";
const char *const kGbNs = "calamaio-gb";
const char *const kPhoneSvc = "526389E4-B789-4225-990D-0465AFB610B2";
const char *const kPhoneState = "6A34D990-D046-4834-96B8-0CBDCE4087FA";
const char *const kPhoneInfo = "9D00F77E-BB83-4667-A37F-6FBDB0F0F324";
const int kMaxBonds = CONFIG_BT_NIMBLE_MAX_BONDS;
const uint32_t kSecureMs = 35000;  // F6: attesa del codice del telefono, oltre i 30 s dello SMP

struct Rx {
  uint32_t t;     // millis() d'arrivo
  uint8_t conn;   // collegamento del report (gConn): al primo di un collegamento nuovo poll() riparte da zero
  uint8_t kind;   // pad::Kind del collegamento
  uint8_t len;
  uint8_t d[24];
};

// RAM statica (tetto base + 512 B, 169 488): prima le parole, poi i 2 byte, poi i byte singoli, senza buchi d'allineamento.
QueueHandle_t gRx = nullptr;
NimBLEClient *gClient = nullptr;
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
pad::BondBook gBook;          // protetto da gMux (lo legge la callback della ricerca)
pad::LinkMachine gMachine;    // solo nel task del controller
pad::Keepalive gKeep;         // loop
pad::PhoneIdle gIdle;         // F7: protetto da gMux (lo scrive onNotify, lo legge il task)
pad::State gLast;             // ultimo stato restituito da poll() (loop)
uint32_t gLastAt = 0;
volatile uint32_t gPasskey = 0;  // kPasskeyNew | codice (< 10^6): da mostrare (una parola sola)
const uint32_t kPasskeyNew = 0x80000000u;
const uint32_t kPasskeyRefused = 0x40000000u;  // codice chiesto fuori dalla finestra: mai a schermo, securePhone fallisce
NimBLEAddress gFoundAddr;     // protetti da gMux
pad::Kind gFoundKind = pad::Kind::Xbox;
volatile uint16_t gBatHandle = 0;
uint16_t gBadSec = 0;         // loop: secondo dell'ultima riga «report scartato» (al più una al secondo)
volatile bool gActive = false, gStop = false, gTaskDone = false;
volatile bool gScanAllowed = false, gWantPair = false, gWantCancel = false;
volatile uint8_t gWantForget = 0;  // bit 0 Xbox, bit 1 telefono, 0x80 tutti (Ripristino)
volatile pad::Kind gWantPairKind = pad::Kind::Xbox;
volatile bool gFound = false, gLost = false;
volatile uint8_t gBondLost = 0;  // kindBit degli associati persi da riassociare (takeBondLost, toast del Task 11)
bool gBookOk = false;           // libro letto dalla NVS (M1: se no, mai cancellazioni né migrazioni)
volatile pad::Link gLinkPub = pad::Link::Off;
volatile pad::Kind gKindPub = pad::Kind::Xbox;
volatile uint8_t gBondPub = 0;  // kindBit dei tipi nel libro (bonded a radio accesa)
volatile uint8_t gBatt1 = 0;    // batteria + 1: 1–101, 0 sconosciuta (zero all'avvio: in .bss, non in .data)
volatile pad::Kind gKind = pad::Kind::Xbox;  // tipo del collegamento in corso (callback di NimBLE)
volatile bool gPairFail = false;
volatile bool gPairConn = false;  // il collegamento in corso può creare un abbinamento nuovo (finestra del suo tipo)
volatile bool gAuthDone = false;  // F6: onAuthenticationComplete (cifratura asincrona del telefono)
volatile uint8_t gConn = 0;   // cresce a ogni scollegamento (task di NimBLE)
uint8_t gLastConn = 0;        // collegamento di gLast (loop)
uint8_t gShortMaps = 0;
bool gLastRelease = false;    // gLast è il «tutto rilasciato» del mantenimento (polledRelease)

// N1: tipi in quarantena dopo una rimessa (solo RAM). Scritta solo dal task del pad e da begin() prima del task,
// letta da ScanCb.
pad::Quarantine gQuar;
pad::Backoff gPhoneBack;       // protetto da gMux: telefono associato con «servizio mancante» (lo legge ScanCb)
uint8_t kindBit(pad::Kind k) { return k == pad::Kind::Phone ? 2 : 1; }

void toAddr(const NimBLEAddress &a, pad::Addr &out) {
  memcpy(out.v, a.getBase()->val, 6);
  out.type = a.getType() & 1;
}

pad::BondBook bookCopy() {
  portENTER_CRITICAL(&gMux);
  const pad::BondBook b = gBook;
  portEXIT_CRITICAL(&gMux);
  return b;
}

// ---- libro nella NVS (calamaio-gb) ----
// Riporto 3: ogni voce passa da set() (mai lo stesso indirizzo per due tipi) e `migrated` viene solo da padv: un padp
// rotto non fa mai migrare il telefono come Xbox (con padv = 1 niente migrazione; senza, migrateLegacy vuole un
// libro vuoto e un solo abbinamento). saveBook scrive padv = 1 anche quando non c'è niente da migrare.
// M1: false se la NVS non si apre; chi chiama allora non cancella e non migra niente (meglio fermi che cancellare).
bool loadBook(pad::BondBook &b, bool &migrated) {
  Preferences p;
  b = pad::BondBook();
  migrated = false;
  if (!p.begin(kGbNs, true)) return false;
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone}) {
    uint8_t blob[pad::kBondBlob];
    pad::Addr a;
    if (p.isKey(pad::bondKey(k)) && p.getBytesLength(pad::bondKey(k)) == pad::kBondBlob &&
        p.getBytes(pad::bondKey(k), blob, sizeof blob) == sizeof blob && pad::BondBook::decode(blob, sizeof blob, a))
      b.set(k, a);
  }
  migrated = p.isKey(pad::kBondVersionKey) && p.getUChar(pad::kBondVersionKey, 0) == 1;
  p.end();
  return true;
}

void saveBook(const pad::BondBook &b) {
  Preferences p;
  if (!p.begin(kGbNs, false)) {
    LogSerial.println("[pad] libro degli abbinamenti non salvato (NVS)");
    return;
  }
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone}) {
    if (b.has(k)) {
      uint8_t blob[pad::kBondBlob];
      pad::BondBook::encode(b.get(k), blob);
      p.putBytes(pad::bondKey(k), blob, sizeof blob);
    } else if (p.isKey(pad::bondKey(k))) {
      p.remove(pad::bondKey(k));
    }
  }
  p.putUChar(pad::kBondVersionKey, 1);
  p.end();
}

int bondsInNvs() {
  Preferences p;
  if (!p.begin(kBondNs, true)) return 0;
  // Chiavi di NimBLE «peer_sec_1..N» (ble_store_nvs.c): un abbinamento nuovo va nel primo indice libero e il
  // vecchio si cancella dopo, quindi l'unico rimasto può stare in peer_sec_2 o oltre.
  int n = 0;
  char key[16];
  for (int i = 1; i <= kMaxBonds; i++) {
    snprintf(key, sizeof key, "peer_sec_%d", i);
    n += p.isKey(key);
  }
  p.end();
  return n;
}

// D3: abbinamenti di NimBLE, come indirizzi di NimBLE e del libro (pruneBonds, reconcile).
int listBonds(NimBLEAddress *raw, pad::Addr *list) {
  int n = NimBLEDevice::getNumBonds();
  if (n > kMaxBonds) n = kMaxBonds;
  for (int i = 0; i < n; i++) {
    raw[i] = NimBLEDevice::getBondedAddress(i);
    toAddr(raw[i], list[i]);
  }
  return n;
}

// F5: da iniziatore NimBLE non applica sm_sc_only (ble_sm.c lo guarda solo da responder): Secure Connections si
// legge dall'archivio delle chiavi dell'abbinamento appena fatto (o ritrovato).
bool storedSc(const NimBLEAddress &id) {
  ble_store_key_sec key;
  memset(&key, 0, sizeof key);
  key.peer_addr = *id.getBase();
  ble_store_value_sec v;
  return ble_store_read_peer_sec(&key, &v) == 0 && v.sc;
}

void publish() {
  gLinkPub = gMachine.link();
  gKindPub = gMachine.kind();
  const pad::BondBook b = bookCopy();
  gBondPub = (b.has(pad::Kind::Xbox) ? kindBit(pad::Kind::Xbox) : 0) | (b.has(pad::Kind::Phone) ? kindBit(pad::Kind::Phone) : 0);
}

// Via gli abbinamenti di NimBLE che il libro non conosce (vecchio telefono dopo il nuovo, orfani, «Dimentica» a radio
// spenta). Mai prima che il libro sia scritto.
void pruneBonds(const pad::BondBook &book) {
  NimBLEAddress raw[kMaxBonds];
  pad::Addr list[kMaxBonds];
  const int n = listBonds(raw, list);
  int stale[kMaxBonds];
  const int k = pad::staleBonds(book, list, n, stale, kMaxBonds);
  for (int j = 0; j < k; j++) {
    NimBLEDevice::deleteBond(raw[stale[j]]);
    LogSerial.println("[pad] abbinamento fuori dal libro: cancellato");
  }
}

// Il nuovo associato di tipo k: libro (sostituisce solo quel tipo), NVS, poi via quello che il libro non conosce.
void remember(pad::Kind k, const pad::Addr &id) {
  portENTER_CRITICAL(&gMux);
  gBook.set(k, id);
  const pad::BondBook b = gBook;
  portEXIT_CRITICAL(&gMux);
  if (!gBookOk) {  // M1: il libro della NVS non si è letto, scriverlo cancellerebbe l'altro tipo
    LogSerial.println("[pad] libro non letto: abbinamento solo in memoria, niente cancellazioni");
    return;
  }
  saveBook(b);
  pruneBonds(b);
}

void dropKind(pad::Kind k) {
  portENTER_CRITICAL(&gMux);
  gBook.clear(k);
  const pad::BondBook b = gBook;
  portEXIT_CRITICAL(&gMux);
  if (!gBookOk) {  // M1
    LogSerial.println("[pad] libro non letto: tolto solo in memoria, niente cancellazioni");
    return;
  }
  saveBook(b);
  pruneBonds(b);
}

// Migrazione dalla 0.13.0-ed e abbinamenti orfani, all'avvio del task (serve l'host sincronizzato per cancellare).
void reconcile() {
  pad::BondBook b;
  bool migrated;
  gBookOk = loadBook(b, migrated);
  if (!gBookOk) {  // M1: niente migrazione né potatura con un libro che non si legge
    LogSerial.println("[pad] libro degli abbinamenti illeggibile (NVS calamaio-gb): nessuna cancellazione né migrazione");
    return;
  }
  NimBLEAddress raw[kMaxBonds];
  pad::Addr list[kMaxBonds];
  const int n = listBonds(raw, list);
  const bool changed = pad::migrateLegacy(b, migrated, list, n);
  portENTER_CRITICAL(&gMux);
  gBook = b;
  portEXIT_CRITICAL(&gMux);
  if (changed) LogSerial.println("[pad] abbinamento della 0.13: è il controller (padx)");
  if (changed || !migrated) saveBook(b);
  pruneBonds(b);
  LogSerial.printf("[pad] libro: controller %s, telefono %s\n", b.has(pad::Kind::Xbox) ? "sì" : "no",
                   b.has(pad::Kind::Phone) ? "sì" : "no");
}

class ScanCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (gFound) return;
    pad::ScanAd ad;
    toAddr(dev->getAddress(), ad.addr);
    ad.gamepad = dev->haveAppearance() && dev->getAppearance() == kAppearanceGamepad;
    ad.hid = dev->isAdvertisingService(NimBLEUUID("1812"));
    ad.phoneSvc = dev->isAdvertisingService(NimBLEUUID(kPhoneSvc));
    ad.connectable = dev->isConnectable();
    portENTER_CRITICAL(&gMux);
    const bool phoneBack = gPhoneBack.blocked(millis());
    portEXIT_CRITICAL(&gMux);
    const uint8_t mask = gQuar.mask | (phoneBack ? pad::Quarantine::kindMask(pad::Kind::Phone) : 0);
    const pad::ScanPick pk = pad::pickAd(ad, bookCopy(), gLinkPub, gKindPub, mask);
    if (!pk.take) return;
    portENTER_CRITICAL(&gMux);
    gFoundAddr = dev->getAddress();
    gFoundKind = pk.kind;
    portEXIT_CRITICAL(&gMux);
    gFound = true;
    NimBLEDevice::getScan()->stop();
  }
} gScanCb;

class ClientCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *, int reason) override {
    LogSerial.printf("[pad] scollegato, motivo 0x%x\n", reason);
    gConn = gConn + 1;  // i report dopo il ricollegamento non si confrontano con quelli di prima
    gLost = true;
  }
  bool onConnParamsUpdateRequest(NimBLEClient *, const ble_gap_upd_params *) override { return true; }
  // Xbox: Just Works come la 0.13. Telefono (DisplayOnly): solo il codice a schermo, mai un confronto o un codice
  // inserito dalla scheda (non avrebbero MITM: linkSecure li rifiuterebbe comunque).
  void onConfirmPasskey(NimBLEConnInfo &ci, uint32_t) override {
    NimBLEDevice::injectConfirmPasskey(ci, gKind == pad::Kind::Xbox);
  }
  void onPassKeyEntry(NimBLEConnInfo &ci) override {
    if (gKind == pad::Kind::Xbox) NimBLEDevice::injectPassKey(ci, 0);
    else LogSerial.println("[pad] telefono: la scheda non inserisce codici, abbinamento lasciato scadere");
  }
  void onAuthenticationComplete(NimBLEConnInfo &) override { gAuthDone = true; }  // F6: cifratura riuscita
  uint32_t onPassKeyDisplay(NimBLEConnInfo &) override {
    const uint32_t k = esp_random() % 1000000;  // riporto 6: mai nel registro (solo a schermo, via takePasskey)
    // Fuori dalla finestra (es. iOS ha perso le chiavi: NimBLE cancella l'abbinamento e riprova da solo) il codice
    // non si mostra e securePhone chiude: un abbinamento nuovo nasce solo da «Associa telefono».
    gPasskey = (gPairConn ? kPasskeyNew : kPasskeyRefused) | k;
    LogSerial.println(gPairConn ? "[pad] telefono: codice a schermo" : "[pad] telefono: codice chiesto fuori dall'associazione, rifiuto");
    return k;
  }
} gClientCb;

void onNotify(NimBLERemoteCharacteristic *c, uint8_t *d, size_t n, bool) {
  if (c->getHandle() == gBatHandle) {
    if (n) gBatt1 = (d[0] > 100 ? 100 : d[0]) + 1;
    return;
  }
  Rx r;
  r.t = millis();
  r.conn = gConn;
  r.kind = static_cast<uint8_t>(gKind);
  r.len = n > sizeof r.d ? sizeof r.d : static_cast<uint8_t>(n);
  memcpy(r.d, d, r.len);
  if (gKind == pad::Kind::Phone) {  // F7: report valido e primo piano, per scollegare un telefono in secondo piano
    pad::State st;
    pad::PhoneMeta meta;
    if (pad::parsePhoneReport(r.d, r.len, st, meta) == pad::PhoneParse::Ok) {
      portENTER_CRITICAL(&gMux);
      gIdle.report(r.t, meta.fg);
      portEXIT_CRITICAL(&gMux);
    }
  }
  xQueueSend(gRx, &r, 0);  // coda piena: il report si perde (64 report = ~1,5 s di stick)
}

enum class Gatt : uint8_t { Ok, NoHid, NoInput, ShortMap };

Gatt setupGatt() {
  NimBLERemoteService *hid = gClient->getService(NimBLEUUID("1812"));
  if (!hid) return Gatt::NoHid;
  size_t mapLen = 0;
  int subscribed = 0;
  for (NimBLERemoteCharacteristic *c : hid->getCharacteristics(true)) {
    if (c->getUUID() == NimBLEUUID("2a4b")) {
      mapLen = c->readValue().size();
      continue;
    }
    if (c->getUUID() != NimBLEUUID("2a4d")) continue;
    uint8_t id = 0, type = 0;
    if (NimBLERemoteDescriptor *rr = c->getDescriptor(NimBLEUUID("2908"))) {
      const NimBLEAttValue v = rr->readValue();
      if (v.size() >= 2) {
        id = v[0];
        type = v[1];
      }
    }
    if (type == 1 && id == 1 && c->canNotify() && c->subscribe(true, onNotify, true)) subscribed++;
  }
  if (NimBLERemoteService *bat = gClient->getService(NimBLEUUID("180f"))) {
    if (NimBLERemoteCharacteristic *c = bat->getCharacteristic(NimBLEUUID("2a19"))) {
      gBatHandle = c->getHandle();
      const NimBLEAttValue v = c->readValue();
      if (v.size()) gBatt1 = (v[0] > 100 ? 100 : v[0]) + 1;
      if (c->canNotify()) c->subscribe(true, onNotify, true);
    }
  }
  LogSerial.printf("[pad] report map %u byte, report d'ingresso id 1 iscritti: %d\n", static_cast<unsigned>(mapLen), subscribed);
  if (!subscribed) return Gatt::NoInput;
  if (mapLen < kMinReportMap) return Gatt::ShortMap;
  return Gatt::Ok;
}

// Telefono: servizio «Calamaio pad», informazioni (ver 1 + versione dell'app) e iscrizione allo stato dei tasti.
Gatt setupPhoneGatt() {
  NimBLERemoteService *svc = gClient->getService(NimBLEUUID(kPhoneSvc));
  if (!svc) return Gatt::NoHid;
  NimBLERemoteCharacteristic *info = svc->getCharacteristic(NimBLEUUID(kPhoneInfo));
  if (!info) {  // M5: senza informazioni non si sa la versione dei report
    LogSerial.println("[pad] telefono: caratteristica delle informazioni mancante");
    return Gatt::NoInput;
  }
  {
    const NimBLEAttValue v = info->readValue();
    char app[16] = {0};
    const size_t n = v.size() > 1 ? (v.size() - 1 < 15 ? v.size() - 1 : 15) : 0;
    if (n) memcpy(app, v.data() + 1, n);
    LogSerial.printf("[pad] telefono: informazioni ver %u, app «%s»\n", v.size() ? v[0] : 0, app);
    if (!v.size() || v[0] != 1) return Gatt::NoInput;  // versione sconosciuta: mai interpretare i report
  }
  NimBLERemoteCharacteristic *st = svc->getCharacteristic(NimBLEUUID(kPhoneState));
  if (!st || !st->canNotify() || !st->subscribe(true, onNotify, true)) return Gatt::NoInput;
  return Gatt::Ok;
}

// Sicurezza del tipo k in ble_hs_cfg: NimBLE la legge quando costruisce la richiesta di abbinamento, quindi va
// scritta prima di connect() (una connessione sola, solo questo task si collega).
void applySecurity(pad::Kind k) {
  const pad::SecCfg c = pad::secFor(k);
  NimBLEDevice::setSecurityAuth(c.bond, c.mitm, c.sc);
  NimBLEDevice::setSecurityIOCap(c.ioCap);
  ble_hs_cfg.sm_sc_only = c.scOnly ? 1 : 0;
}

// LTK salvata per id (F5 usa lo stesso archivio): false se manca.
bool peerLtk(const NimBLEAddress &id, uint8_t out[16]) {
  ble_store_key_sec key;
  memset(&key, 0, sizeof key);
  key.peer_addr = *id.getBase();
  ble_store_value_sec v;
  if (ble_store_read_peer_sec(&key, &v) != 0 || !v.ltk_present) return false;
  memcpy(out, v.ltk, 16);
  return true;
}

// Revisione finale I1 (b): copia del record delle chiavi presa prima della cifratura; se NimBLE si riabbina da sé
// fuori dalla finestra (richiesta di sicurezza con MITM) si rimette questa. Statica, niente heap: solo il task del
// pad si collega. false se manca o è senza LTK (allora secStart scollega senza toccare niente).
ble_store_value_sec gSecSnap;

bool snapKeys(const NimBLEAddress &id) {
  ble_store_key_sec key;
  memset(&key, 0, sizeof key);
  key.peer_addr = *id.getBase();
  return ble_store_read_peer_sec(&key, &gSecSnap) == 0 && gSecSnap.ltk_present;
}

// Rimette la copia (ble_store_write_peer_sec: archivio in RAM, NVS e IRK nel controller) e controlla rileggendo la
// LTK: un errore sull'IRK dopo la scrittura riuscita non conta.
// Si rilegge con l'identità della copia stessa (gSecSnap.peer_addr), cioè la chiave con cui l'archivio la tiene.
bool restoreKeys() {
  const int rc = ble_store_write_peer_sec(&gSecSnap);
  uint8_t ltk[16];
  const bool ok = peerLtk(NimBLEAddress(gSecSnap.peer_addr), ltk) && memcmp(ltk, gSecSnap.ltk, 16) == 0;
  if (rc != 0) LogSerial.printf("[pad] rimessa delle chiavi: rc %d\n", rc);
  return ok;
}

// Cifratura asincrona e annullabile per tutti e due i tipi (F6, I1). Nella finestra con NimBLEClient::
// secureConnection(true): se la chiave manca NimBLE riprova e abbina da sé, e lì va bene. Fuori dalla finestra con
// NimBLEDevice::startSecurity: nessun nuovo tentativo di NimBLE (m_asyncSecureAttempt resta 0); un abbinamento che
// nasce lo stesso (richiesta di sicurezza della periferica) lo scopre il confronto della LTK in connectFound.
// Si ferma con «LUNGA ANNULLA», end(), finestra scaduta (M2: mai un codice a finestra chiusa), kSecureMs o
// scollegamento.
pad::SecResult secureLink(bool pairing) {
  gAuthDone = false;
  gPasskey = 0;
  const bool started = pairing ? gClient->secureConnection(true) : NimBLEDevice::startSecurity(gClient->getConnHandle());
  if (!started) return pad::SecResult::Failed;
  const uint32_t t0 = millis();
  while (!gAuthDone) {
    if (gWantCancel || gStop) return pad::SecResult::Cancelled;
    if (pairing) {
      gMachine.tick(millis());
      if (gMachine.link() != pad::Link::Pairing) {  // finestra scaduta durante il codice
        gPairConn = false;
        publish();
        return pad::SecResult::Cancelled;
      }
    }
    if (gPasskey & kPasskeyRefused) return pad::SecResult::Failed;
    if (!gClient->isConnected() || millis() - t0 > kSecureMs) return pad::SecResult::Failed;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return pad::SecResult::Ok;
}

void connectLink();

// Riporto 1: l'unica via verso connect(); gFound lo alza solo ScanCb dopo pickAd, anche per i ricollegamenti degli
// associati (nessun collegamento diretto). Riporto 2: sicurezza e controlli col tipo di ScanPick.kind (gFoundKind),
// mai gMachine.kind() (dopo una finestra scaduta resta quello della finestra).
void connectFound() {
  connectLink();
  gPairConn = false;  // M2: dopo questo collegamento (riuscito o no) nessun codice si mostra più
  gPasskey = 0;
}

void connectLink() {
  NimBLEAddress a;
  pad::Kind kind;
  portENTER_CRITICAL(&gMux);
  a = gFoundAddr;
  kind = gFoundKind;
  portEXIT_CRITICAL(&gMux);
  const bool phone = kind == pad::Kind::Phone;
  const bool pairing = gMachine.link() == pad::Link::Pairing && gMachine.kind() == kind;
  gKind = kind;
  gPairConn = pairing;
  applySecurity(kind);
  if (!gClient) {
    gClient = NimBLEDevice::createClient();
    gClient->setClientCallbacks(&gClientCb, false);
    gClient->setConnectTimeout(5000);
  }
  gClient->setConnectionParams(phone ? kPhoneItvMin : kItvMin, phone ? kPhoneItvMax : kItvMax, 0, 300);
  const uint32_t t0 = millis();
  if (!gClient->connect(a, true)) {
    LogSerial.printf("[pad] %s: collegamento non riuscito\n", pad::kindName(kind));
    return;
  }
  // Indirizzo d'identità (risolto dal controller, se ha l'IRK): dice se è associato davvero. idA vale solo per la
  // fotografia di prima (wasBonded, hadLtk, snapKeys): un iPhone nuovo qui è ancora all'indirizzo privato (RPA),
  // l'identità e l'IRK arrivano con la distribuzione delle chiavi e NimBLE archivia sotto quella (ble_sm.c).
  const NimBLEAddress idA = gClient->getConnInfo().getIdAddress();
  const bool wasBonded = NimBLEDevice::isBonded(idA);
  const bool hadLtk = wasBonded && snapKeys(idA);  // con la LTK: copia del record in gSecSnap
  const pad::SecStart st = pad::secStart(pairing, wasBonded, hadLtk);
  if (st == pad::SecStart::RefuseNew) {  // fuori dalla finestra mai un abbinamento nuovo
    LogSerial.printf("[pad] %s non associato fuori dall'associazione: scollego\n", pad::kindName(kind));
    gClient->disconnect();
    return;
  }
  if (st == pad::SecStart::NoLtk) {  // R1-I1: startSecurity farebbe un abbinamento nuovo; chiavi e libro intatti
    LogSerial.printf("[pad] %s: abbinamento senza LTK: scollego senza toccare niente\n", pad::kindName(kind));
    gClient->disconnect();
    return;
  }
  pad::SecResult res = secureLink(pairing);
  // 0.12.0: l'Xbox associato ha un secondo tentativo, solo con le chiavi ancora lì (mai un abbinamento nuovo)
  if (!phone && res == pad::SecResult::Failed && wasBonded && gClient->isConnected() && NimBLEDevice::isBonded(idA)) {
    LogSerial.println("[pad] cifratura non riuscita: riprovo una volta");
    res = secureLink(pairing);
  }
  NimBLEConnInfo ci = gClient->getConnInfo();
  // Dopo la cifratura: l'identità con cui l'archivio tiene le chiavi. Tutto quello che segue (archivio, libro) usa
  // idB. Associato che si ricollega e Xbox (indirizzo pubblico, nessun RPA): idA == idB.
  const NimBLEAddress idB = ci.getIdAddress();
  const bool idChanged = !(idA == idB);
  if (!wasBonded && res == pad::SecResult::Ok)
    LogSerial.printf("[pad] %s: id prima …%02x:%02x dopo …%02x:%02x\n", pad::kindName(kind), idA.getBase()->val[1],
                     idA.getBase()->val[0], idB.getBase()->val[1], idB.getBase()->val[0]);
  pad::SecIn in;
  in.kind = kind;
  in.pairing = pairing;
  in.wasBonded = wasBonded;
  in.res = res;
  in.bondedNow = NimBLEDevice::isBonded(idB);
  in.snapshot = hadLtk;
  in.sameKeys = true;
  if (!pairing && in.bondedNow) {  // R1-I2: con qualunque esito
    uint8_t ltk1[16];
    // un'identità diversa da quella di prima fuori dalla finestra è comunque un abbinamento nuovo
    in.sameKeys = !idChanged && hadLtk && peerLtk(idB, ltk1) && memcmp(gSecSnap.ltk, ltk1, 16) == 0;
  }
  const bool sc = phone && res == pad::SecResult::Ok && storedSc(idB);
  in.linkOk = res == pad::SecResult::Ok &&
              pad::linkSecure(kind, ci.isEncrypted(), ci.isAuthenticated(), ci.getSecKeySize(), sc);
  pad::SecOut o = pad::secOutcome(in);
  if (o.restore) {  // revisione finale I1 (b): libro intatto, nessun avviso, solo scollegato
    if (restoreKeys()) {
      if (idChanged && NimBLEDevice::isBonded(idB)) NimBLEDevice::deleteBond(idB);  // l'abbinamento nuovo, altrove
      LogSerial.printf("[pad] %s: abbinamento rifatto fuori dall'associazione: rimesse le chiavi di prima, scollego\n",
                       pad::kindName(kind));
      // N1: non si riprende fino a «Associa…»/«Dimentica» o alla prossima accensione; un solo avviso, niente cancellato
      if (gQuar.restored(kind)) {
        LogSerial.printf("[pad] %s in quarantena fino ad «Associa», «Dimentica» o al prossimo avvio del Bluetooth\n",
                         pad::kindName(kind));
        __atomic_fetch_or(&gBondLost, kindBit(kind), __ATOMIC_SEQ_CST);
      }
      gClient->disconnect();
      return;
    }
    in.snapshot = false;  // copia non rimessa: le chiavi nuove non si tengono (come prima)
    o = pad::secOutcome(in);
  }
  if (!o.ready) {
    if (!pairing && in.bondedNow && !in.sameKeys) LogSerial.printf("[pad] %s: abbinamento rifatto fuori dall'associazione, rifiutato\n", pad::kindName(kind));
    else if (res == pad::SecResult::Cancelled) LogSerial.printf("[pad] %s: associazione annullata o scaduta durante la cifratura\n", pad::kindName(kind));
    else if (res == pad::SecResult::Ok && !in.sameKeys) LogSerial.printf("[pad] %s: chiavi sparite dopo la cifratura\n", pad::kindName(kind));
    else if (res == pad::SecResult::Ok)
      LogSerial.printf("[pad] %s: sicurezza insufficiente (cifrato %d, autenticato %d, chiave %u, SC %d)\n", pad::kindName(kind),
                       ci.isEncrypted(), ci.isAuthenticated(), static_cast<unsigned>(ci.getSecKeySize()), sc);
    else LogSerial.printf("[pad] %s: cifratura non riuscita\n", pad::kindName(kind));
    // m5 (accettato, corretto dalla revisione del Task 10): chi annuncia l'indirizzo d'identità di un associato senza
    // le sue chiavi non ottiene mai un abbinamento (I1: niente secondo tentativo né finestra automatica, LTK
    // confrontata). Al più scollega, e se risponde «chiave mancante» NimBLE cancella le chiavi da sé: l'associato va
    // riassociato dall'utente («Associa controller»/«Associa telefono»). Disservizio, mai un'intrusione.
    if (o.deleteBond && NimBLEDevice::isBonded(idB)) NimBLEDevice::deleteBond(idB);
    if (o.dropBook) {
      pad::Addr id;
      toAddr(idA, id);  // nel libro c'è l'identità di prima (associato: idA risolto)
      const pad::BondBook b = bookCopy();
      if (b.has(kind) && pad::sameAddr(b.get(kind), id)) {
        dropKind(kind);
        LogSerial.printf("[pad] abbinamento del %s non più valido: va riassociato\n", pad::kindName(kind));
      }
    }
    if (o.bondLost) __atomic_fetch_or(&gBondLost, kindBit(kind), __ATOMIC_SEQ_CST);
    if (o.pairFail) {  // F6: un codice sbagliato chiude la finestra, serve di nuovo «Associa telefono»
      gPairFail = true;
      gMachine.cancel();
    }
    gClient->disconnect();
    return;
  }
  const Gatt g = phone ? setupPhoneGatt() : setupGatt();
  if (g == Gatt::NoHid || g == Gatt::NoInput) {
    LogSerial.printf("[pad] %s: servizio mancante o sconosciuto, scollego\n", pad::kindName(kind));
    if (!wasBonded && NimBLEDevice::isBonded(idB)) NimBLEDevice::deleteBond(idB);  // mai un associato senza servizio
    if (phone && wasBonded) {  // app non in primo piano: niente ricollegamenti a raffica (30 s, fino a 5 min)
      portENTER_CRITICAL(&gMux);
      gPhoneBack.failed(millis());
      portEXIT_CRITICAL(&gMux);
    }
    if (phone && !wasBonded) {
      gPairFail = true;
      gMachine.cancel();
    }
    gClient->disconnect();
    return;
  }
  if (g == Gatt::ShortMap && gShortMaps < kShortMapRetries) {
    gShortMaps++;
    LogSerial.println("[pad] report map corto: ricollego subito");
    gClient->disconnect();
    return;
  }
  gShortMaps = 0;
  pad::Addr id;
  toAddr(idB, id);  // dopo la cifratura: l'identità, mai l'indirizzo privato
  remember(kind, id);  // uno per tipo: il nuovo sostituisce solo il suo tipo
  gClient->updateConnParams(phone ? kPhoneItvMin : kItvMin, phone ? kPhoneItvMax : kItvMax, 0, 300);
  gMachine.connected(true, kind);
  if (phone) {  // F7
    portENTER_CRITICAL(&gMux);
    gIdle.start(millis());
    gPhoneBack.ok();
    portEXIT_CRITICAL(&gMux);
  }
  publish();
  LogSerial.printf("[pad] PRONTO in %lu ms (%s, %s)\n", static_cast<unsigned long>(millis() - t0), pad::kindName(kind),
                   wasBonded ? "ricollegamento" : "nuovo");
}

void forgetNow(uint8_t mask) {
  if (mask & 0x80) {
    NimBLEDevice::deleteAllBonds();
    portENTER_CRITICAL(&gMux);
    gBook = pad::BondBook();
    portEXIT_CRITICAL(&gMux);
    saveBook(pad::BondBook());
    gQuar.forgotAll();
    LogSerial.println("[pad] tutti gli abbinamenti cancellati");
    return;
  }
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone})
    if (mask & kindBit(k)) {
      dropKind(k);
      gQuar.forgot(k);
      LogSerial.printf("[pad] %s dimenticato\n", pad::kindName(k));
    }
}

void padTask(void *) {
  // 0.14.0: host pronto, poi risoluzione degli indirizzi privati e riconciliazione del libro
  for (int i = 0; i < 300 && !ble_hs_synced() && !gStop; i++) vTaskDelay(pdMS_TO_TICKS(10));
  // M3: accesa dopo ogni sincronizzazione dell'host (anche dopo un reset), ritentata finché non riesce: il comando
  // non è ammesso durante la ricerca (NimBLE la riavvia da sé in onSync), quindi prima la si ferma.
  bool resolved = false, failLogged = false;
  uint32_t triedAt = 0;
  auto resolveOn = [&resolved, &failLogged, &triedAt]() {
    if (!ble_hs_synced()) {
      resolved = false;
      return;
    }
    if (resolved || (failLogged && millis() - triedAt < 1000)) return;  // al più un tentativo al secondo
    triedAt = millis();
    NimBLEScan *sc = NimBLEDevice::getScan();
    if (sc->isScanning()) sc->stop();  // il giro dopo la riavvia, se serve
    const int rc = ble_hs_pvcy_set_resolve_enabled(1);
    resolved = rc == 0;
    if (resolved) LogSerial.println("[pad] risoluzione degli indirizzi privati: accesa");
    else if (!failLogged) LogSerial.printf("[pad] risoluzione degli indirizzi privati NON accesa (rc %d): riprovo\n", rc);
    failLogged = !resolved;
  };
  resolveOn();
  reconcile();
  gMachine.setBonded(bookCopy().any());
  publish();
  while (!gStop) {
    const uint32_t now = millis();
    NimBLEScan *s = NimBLEDevice::getScan();
    resolveOn();
    if (const uint8_t mask = __atomic_exchange_n(&gWantForget, 0, __ATOMIC_SEQ_CST)) {  // M4
      if (s->isScanning()) s->stop();
      const bool current = (mask & 0x80) || (mask & kindBit(gMachine.kind()));
      if (current && gClient && gClient->isConnected()) gClient->disconnect();
      forgetNow(mask);
      const bool any = bookCopy().any();
      if (!any) gMachine.forget();
      else gMachine.setBonded(true);
    }
    if (gWantPair) {
      gWantPair = false;
      if (gClient && gClient->isConnected()) gClient->disconnect();
      gQuar.pairing(gWantPairKind);
      gMachine.pair(now, gWantPairKind);
    }
    if (gWantCancel) {
      gWantCancel = false;
      if (gMachine.link() == pad::Link::Pairing && gClient && gClient->isConnected()) gClient->disconnect();
      gMachine.cancel();
    }
    if (gLost) {
      gLost = false;
      portENTER_CRITICAL(&gMux);
      gIdle.stop();
      portEXIT_CRITICAL(&gMux);
      gBatHandle = 0;
      gBatt1 = 0;
      // I1 (revisione del Task 10): un abbinamento perso non apre mai una finestra da sé (niente lost(…, true)):
      // l'utente sceglie «Associa controller» o «Associa telefono» (takeBondLost per il toast).
      gMachine.lost(now, false);
      gMachine.setBonded(bookCopy().any());
    }
    gMachine.tick(now);
    publish();
    const pad::Link l = gMachine.link();
    if (l == pad::Link::Ready) {
      // F7: iOS tiene il collegamento anche con l'app in secondo piano; la scheda lo chiude e torna a cercare.
      // millis() dentro la sezione: onNotify può aver scritto un report più nuovo di `now` (now − last_ < 0).
      const bool phone = gMachine.kind() == pad::Kind::Phone;
      portENTER_CRITICAL(&gMux);
      const bool idle = phone && gIdle.due(millis());
      if (idle) gIdle.stop();
      portEXIT_CRITICAL(&gMux);
      if (idle && gClient && gClient->isConnected()) {
        LogSerial.println("[pad] telefono in secondo piano o muto: scollego e torno a cercare");
        gClient->disconnect();
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (gFound) {
      if (s->isScanning()) s->stop();
      connectFound();
      gFound = false;
      continue;
    }
    const bool want = gScanAllowed && (l == pad::Link::Pairing || l == pad::Link::Connecting);
    if (want && !s->isScanning()) s->start(kScanMs, false, true);
    else if (!want && s->isScanning()) s->stop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  NimBLEScan *s = NimBLEDevice::getScan();
  if (s->isScanning()) s->stop();
  if (gClient && gClient->isConnected()) gClient->disconnect();
  gTaskDone = true;
  vTaskDelete(nullptr);
}

}  // namespace

void begin() {
  if (gActive) return;
  // 0.12.0: cache dei duplicati del controller da 100 a 20 indirizzi (heap del controller, durante la partita)
  NimBLEDevice::setScanDuplicateCacheSize(20);
  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);
  applySecurity(pad::Kind::Xbox);  // 0.14.0: per tipo, riscritta prima di ogni collegamento (connectFound)
  NimBLEScan *sc = NimBLEDevice::getScan();
  sc->setScanCallbacks(&gScanCb, true);
  sc->setActiveScan(true);
  sc->setInterval(100);
  sc->setWindow(60);  // non il 100%: lascia tempo al Wi-Fi
  sc->setMaxResults(0);
  if (!gRx) gRx = xQueueCreate(64, sizeof(Rx));
  pad::BondBook b;
  bool migrated;
  loadBook(b, migrated);  // subito, per la ricerca; il task la riconcilia con NimBLE
  portENTER_CRITICAL(&gMux);
  gBook = b;
  portEXIT_CRITICAL(&gMux);
  gMachine.enable(true, NimBLEDevice::getNumBonds() > 0);
  publish();
  gStop = gTaskDone = gFound = gLost = gWantPair = gWantCancel = false;
  gBondLost = 0;
  gQuar.radioStart();
  gPhoneBack.ok();  // task non ancora partito: nessuna sezione serve
  gBookOk = false;  // lo decide reconcile() nel task
  gPasskey = 0;
  gPairFail = false;
  gBatt1 = 0;
  gShortMaps = 0;
  gKeep.stop();
  memset(&gLast, 0, sizeof gLast);
  gLast.src = pad::Kind::Xbox;  // riporto 4: src sempre scritto
  gLastRelease = false;
  gConn = gLastConn + 1;  // il primo report dopo begin() si consegna sempre
  gActive = true;
  xTaskCreatePinnedToCore(padTask, "pad", 6144, nullptr, 1, nullptr, 0);  // RAM: niente handle, il task si ferma da sé
}

bool end() {
  if (!gActive) return true;
  gStop = true;
  const uint32_t t0 = millis();
  while (!gTaskDone && millis() - t0 < 3000) delay(10);
  if (!gTaskDone) {  // mai deinit né cancellazioni col task vivo: si riprova più tardi
    LogSerial.println("[pad] il task del controller non si è fermato in 3 s: radio lasciata accesa, riprovare end()");
    return false;
  }
  // forget() subito prima di end() («Dimentica», ripristino): il task non l'ha fatto
  if (const uint8_t mask = __atomic_exchange_n(&gWantForget, 0, __ATOMIC_SEQ_CST)) {
    forgetNow(mask);
    LogSerial.println("[pad] abbinamenti cancellati prima di spegnere la radio");
  }
  NimBLEDevice::deinit(true);
  gClient = nullptr;
  gMachine.enable(false, false);
  gActive = false;
  gLinkPub = pad::Link::Off;
  gBatt1 = 0;
  gKeep.stop();
  if (gRx) xQueueReset(gRx);
  return true;
}

bool active() { return gActive; }

void startPairing(pad::Kind k) {
  if (!gActive) return;
  gWantPairKind = k;
  gWantPair = true;
}

void cancelPairing() {
  if (gActive) gWantCancel = true;
}

void forget(pad::Kind k) {
  gBondPub = gBondPub & ~kindBit(k);
  if (gActive) {
    __atomic_fetch_or(&gWantForget, kindBit(k), __ATOMIC_SEQ_CST);  // M4
    return;
  }
  // Radio spenta: solo il libro (padv = 1: la 0.13 non si migra più); l'abbinamento di NimBLE lo toglie la
  // riconciliazione alla prossima accensione, e fino ad allora la ricerca non lo prende (pickAd usa il libro).
  pad::BondBook b;
  bool migrated;
  if (!loadBook(b, migrated)) {  // M1: riscriverlo vuoto cancellerebbe anche l'altro tipo
    LogSerial.printf("[pad] libro illeggibile: %s non dimenticato\n", pad::kindName(k));
    return;
  }
  b.clear(k);
  saveBook(b);
}

void forgetAll() {
  gBondPub = 0;
  if (gActive) {
    __atomic_fetch_or(&gWantForget, 0x80, __ATOMIC_SEQ_CST);
    return;
  }
  Preferences p;
  if (p.begin(kBondNs, false)) {
    p.clear();
    p.end();
  }
  saveBook(pad::BondBook());
}

pad::Link link() { return gActive ? gLinkPub : pad::Link::Off; }
pad::Kind kind() { return gKindPub; }

bool bonded(pad::Kind k) {
  if (gActive) return (gBondPub & kindBit(k)) != 0;
  pad::BondBook b;
  bool migrated;
  if (!loadBook(b, migrated)) return false;  // M1: libro illeggibile, radio spenta
  if (b.has(k)) return true;
  return k == pad::Kind::Xbox && pad::legacyIsXbox(migrated, b, bondsInNvs());  // D2: stessa regola di migrateLegacy
}

bool bonded() { return bonded(pad::Kind::Xbox) || bonded(pad::Kind::Phone); }
int battery() { return gActive ? int(gBatt1) - 1 : -1; }
void setScan(bool on) { gScanAllowed = on; }
uint32_t polledAt() { return gLastAt; }
uint8_t connId() { return gConn; }
bool polledRelease() { return gLastRelease; }
uint32_t tickClock(uint32_t now) { return gKeep.cap(now); }

bool takePasskey(uint32_t &key) {
  const uint32_t v = gPasskey;
  if (!(v & kPasskeyNew)) return false;
  gPasskey = 0;
  key = v & ~kPasskeyNew;
  return true;
}

bool takeBondLost(pad::Kind &k) {
  const uint8_t m = gBondLost;
  if (!m) return false;
  k = (m & kindBit(pad::Kind::Xbox)) ? pad::Kind::Xbox : pad::Kind::Phone;
  __atomic_fetch_and(&gBondLost, static_cast<uint8_t>(~kindBit(k)), __ATOMIC_SEQ_CST);
  return true;
}

bool takePairFailed() {
  if (!gPairFail) return false;
  gPairFail = false;
  return true;
}

bool poll(pad::State &s) {
  if (!gRx) return false;
  if (link() != pad::Link::Ready) gKeep.stop();  // scollegato: lo stato lo azzera il loop (MenuMapper::reset)
  Rx r;
  while (xQueueReceive(gRx, &r, 0) == pdTRUE) {
    // Round 1 (I1): report di un collegamento già chiuso, rimasto in coda: dopo il reset del loop non deve premere di
    // nuovo A (un Xbox caduto farebbe scattare il tenuto dal tick).
    if (r.conn != gConn) continue;
    pad::State n;
    bool release = false;
    if (r.kind == static_cast<uint8_t>(pad::Kind::Phone)) {
      pad::PhoneMeta meta;
      const pad::PhoneParse p = pad::parsePhoneReport(r.d, r.len, n, meta);
      if (p != pad::PhoneParse::Ok) {
        if (static_cast<uint16_t>(r.t / 1000) != gBadSec) {
          gBadSec = static_cast<uint16_t>(r.t / 1000);
          LogSerial.printf("[pad] report del telefono scartato: %s (%u byte)\n", pad::phoneParseName(p), r.len);
        }
        continue;
      }
      release = pad::phoneForcedRelease(meta);  // revisione finale I2: secondo piano = rilascio forzato
      gKeep.feed(r.t);
    } else {
      if (!pad::parseXboxReport(r.d, r.len, n)) continue;
      gKeep.stop();
    }
    const bool fresh = r.conn != gLastConn;  // primo report di un collegamento nuovo: sempre consegnato
    // Riporto 5: il mantenimento del telefono (uguale all'ultimo, ogni 1 s) si ferma qui, mai fino all'Ui (disarmerebbe
    // le conferme del PRG e renderebbe impossibili Proteggi e Ripristino con l'app aperta).
    if (!fresh && pad::sameState(n, gLast) && n.src == gLast.src) continue;
    gLastConn = r.conn;
    gLast = n;
    gLastAt = r.t;
    gLastRelease = release;  // ai menu come MenuMapper::reset (pad::mapPolled): mai Select né HoldFire
    s = n;
    return true;
  }
  // Telefono: 3 s senza report = tutto rilasciato (spec §2.1), una volta sola fino al report dopo.
  uint32_t at;
  if (pad::keepaliveRelease(gKeep, gLast, millis(), s, at)) {
    gLast = s;
    gLastAt = at;
    gLastRelease = true;  // riporto 5: ai menu come MenuMapper::reset (pad::mapPolled), mai come Select
    LogSerial.println("[pad] telefono: 3 s senza report, tutto rilasciato");
    return true;
  }
  return false;
}

}  // namespace blepad
}  // namespace cal
