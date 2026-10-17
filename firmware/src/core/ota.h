// Aggiornamento remoto (docs/OTA.md): logica pura, provata in native (test_ota). La parte che tocca la scheda
// (esp_ota_*, HTTP, NVS, riavvio) sta dietro le interfacce Flash e Source e in hw/ota_hw.cpp; il filo
// (WebSocket) in net/net.cpp.
//
//   ota-key   {pub, mac}                       mac = HMAC(SK, "ota-key" ␟ pub)
//   ota-offer {id, manifest, sig, url, mac}    mac = HMAC(SK, "ota-offer" ␟ id ␟ sha256)
//   ota-status {id, phase, done, total, error?}
// Manifesto: {board, device, version, size, sha256, built}; forma canonica = i sei campi in quest'ordine come
// testo (size e built in decimale), separati da 0x1F; firma Ed25519 (core/ed25519) su quei byte.
// Vettori: protocol/vectors/ota-vectors.json (generati con Node, li usa anche il servizio).
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "crypto.h"
#include "model.h"
#include "usb.h"

namespace cal {
namespace ota {

using Phase = OtaPhase;

const size_t kIdLen = 33;                  // id dell'offerta e del registro: 1–32 caratteri [A-Za-z0-9_-] (schema otaId)
const size_t kBoardLen = 32, kDeviceLen = 32, kVersionLen = kOtaVerLen, kShaHex = 64;  // ≤ 31 byte (schema)
const size_t kUrlLen = 201;                // url: http://…, al massimo 200 caratteri
const size_t kWhyLen = kOtaWhyLen;
const size_t kCanonMax = kBoardLen + kDeviceLen + kVersionLen + 10 + kShaHex + 20 + 5;
const int kStatusStepPct = 5;              // ota-status durante la ricezione: ogni 5 %
const int kMinBatteryPct = 50;             // senza cavo: batteria ≥ 50 % letta subito prima (OTA.md §1.5)
const uint32_t kStallMs = 15000;           // nessun byte da 15 s: download interrotto
const Ms kValidateMs = 120000;             // dopo il riavvio: Wi-Fi + autenticazione entro 120 s (OTA.md §3.4)
const char *const kRolledBack = "la nuova versione non si è avviata: tornato alla precedente";

const char *phaseKey(Phase p);  // ota-status.phase

struct Manifest {
  char board[kBoardLen];
  char device[kDeviceLen];
  char version[kVersionLen];
  uint32_t size;
  char sha256[kShaHex + 1];  // esadecimale minuscolo
  int64_t built;             // ms epoch
};
// Forma canonica (OTA.md §2). 0 se non sta in cap.
size_t canonical(const Manifest &m, uint8_t *out, size_t cap);
bool manifestSigned(const Manifest &m, const uint8_t sig[64], const uint8_t pub[32]);

bool keyMac(const uint8_t SK[32], const char *pubHex, uint8_t out[16]);
bool offerMac(const uint8_t SK[32], const char *id, const char *sha256Hex, uint8_t out[16]);
bool cancelMac(const uint8_t SK[32], const char *id, uint8_t out[16]);  // HMAC(SK, "ota-cancel" ␟ id)
bool validId(const char *id);  // otaId: 1–32 caratteri [A-Za-z0-9_-]

// ota-cancel {id, mac} (PROTOCOLLO.md, «OTA»): vale solo autenticato, con il mac giusto e per l'aggiornamento in
// corso (current = id dell'offerta in attesa o della ricezione, '' = nessuno). why: perché no (per il log).
bool checkCancel(JsonVariantConst root, bool authed, const uint8_t SK[32], const char *current, const char **why);
const char *const kCancelled = "annullato dal PC";

// status {batt, usb, cal} (PROTOCOLLO.md, «OTA»): subito dopo hello e a ogni cambiamento; cavo e calibrazione
// subito, la batteria quando cambia di almeno 1 punto e al massimo una volta ogni 10 s.
const Ms kPowerStatusMs = 10000;
struct PowerStatus {
  int batt;  // 0–100, −1 = non si sa (null)
  int usb;   // 1 sì, 0 no, −1 non si sa (null)
  bool cal;
  // 0.9.0: status.workday {on, end}. hasWork falso = campo assente (mai null: per il servizio null vuol dire
  // «firmware senza giornata»). Un cambiamento si manda subito, fuori dal limite di 10 s della batteria.
  bool hasWork;
  bool workOn;
  int64_t workEnd;  // ms epoch della fine (solo con workOn)
};
class PowerReporter {
 public:
  bool due(const PowerStatus &s, Ms now);  // true = da mandare adesso (e lo segna come mandato)
 private:
  bool sent_ = false;
  PowerStatus last_ = {-1, -1, false};
  Ms at_ = 0;
};
size_t encodePowerStatus(const PowerStatus &s, char *out, size_t cap);

// ---------- ota-key (OTA.md §2) ----------
// La chiave si salva nel vault; una diversa da quella salvata vale solo dopo un nuovo abbinamento (che la cancella).
enum class KeyVerdict : uint8_t { Save, Same, Rejected };
struct KeyCheck {
  KeyVerdict verdict;
  uint8_t pub[32];
  const char *why;  // Rejected: perché (solo per il log)
};
KeyCheck checkKey(JsonVariantConst root, bool authed, const uint8_t SK[32], bool hasSaved, const uint8_t saved[32]);

// ---------- ota-offer (OTA.md §3.1) ----------
struct Identity {
  const char *board;   // hello.board, es. calamaio-3f2a
  const char *device;  // hello.device, es. heltec213
  uint32_t maxSize;    // dimensione della partizione OTA libera
};
struct Offer {
  char id[kIdLen];
  Manifest m;
  uint8_t sig[64];
  char url[kUrlLen];
};
// id dell'offerta anche se il resto non è valido (per rispondere «rifiutato»). false = id assente o non valido:
// in quel caso non si risponde.
bool offerId(JsonVariantConst root, char out[kIdLen]);
// Controlli nell'ordine di OTA.md §3.1: autenticazione, mac, firma con la chiave salvata, board e device, poi
// dimensione e url. Le condizioni del §1 (permessi, calibrazione, cavo e batteria) le controlla conditionsOk().
// false → why = motivo leggibile in italiano (≤ 95 byte); out.m.size è valido se il manifesto si è letto.
bool checkOffer(JsonVariantConst root, bool authed, const uint8_t SK[32], bool hasPub, const uint8_t pub[32],
                const Identity &me, Offer &out, char *why, size_t cap);

// ---------- condizioni (OTA.md §1.4 e §1.5) ----------
struct Conditions {
  bool busy;          // un aggiornamento già in corso
  bool trial;         // la versione attuale è ancora «in prova» (da confermare entro 120 s)
  bool permOpen;      // richiesta di permesso aperta (in coda o a schermo)
  bool calibrating;   // calibrazione della batteria in corso
  UsbPresent usb;     // cavo USB: sì / no / sconosciuto
  int battPct;        // batteria letta subito prima, −1 = lettura non valida
  bool playing;       // 0.12.0: partita del Game Boy aperta (avvio, gioco o pausa): si rifà a fine partita
};
// true = si parte. Col cavo «sì» sempre; senza, batteria ≥ 50 % valida; né l'una né l'altra → no.
// Motivo come visibile (shown = true) anche sul display: batteria, cavo; gli altri solo nel servizio.
bool conditionsOk(const Conditions &c, char *why, size_t cap, bool *shown = nullptr);

// ---------- ota-status ----------
size_t encodeStatus(const char *id, Phase p, uint32_t done, uint32_t total, const char *error, char *out, size_t cap);

// ---------- scrittura: interfacce verso la scheda ----------
class Flash {  // esp_ota_* sulla partizione libera
 public:
  virtual ~Flash() {}
  virtual bool begin(uint32_t size, char *why, size_t cap) = 0;
  virtual bool write(const uint8_t *d, size_t n) = 0;
  virtual bool end(char *why, size_t cap) = 0;  // esp_ota_end: l'immagine si valida (intestazione, segmenti)
  virtual void abort() = 0;
  virtual bool setBoot() = 0;                    // esp_ota_set_boot_partition
};
class Source {  // HTTP GET dell'url
 public:
  virtual ~Source() {}
  virtual bool open(const char *url, char *why, size_t cap) = 0;
  // Byte disponibili adesso (al massimo cap): > 0 letti, 0 niente per ora, < 0 connessione chiusa o errore.
  virtual int read(uint8_t *buf, size_t cap) = 0;
  virtual void close() = 0;
};

// Registro dell'aggiornamento (NVS): scritto subito prima di cambiare la partizione di avvio, letto dopo il
// riavvio per dire al servizio com'è finita (fatto, o tornata la versione di prima).
struct Record {
  char id[kIdLen];
  char from[kVersionLen];
  char to[kVersionLen];
  char label[17];   // etichetta della partizione nuova (app0 / app1)
  uint32_t size;
};
const size_t kRecordLen = 1 + kIdLen + 2 * kVersionLen + 17 + 4 + 4;
size_t packRecord(const Record &r, uint8_t *out, size_t cap);
bool unpackRecord(const uint8_t *in, size_t n, Record &r);

class UpdaterOut {
 public:
  virtual ~UpdaterOut() {}
  virtual void status(Phase p, uint32_t done, uint32_t total, const char *error) = 0;  // → ota-status
  virtual void progress(Phase p, uint32_t done, uint32_t total, const char *error) = 0;  // → schermata (ogni 1 %)
  virtual bool saveRecord(const Record &r) = 0;
  virtual void clearRecord() = 0;
};

enum class Step : uint8_t { Idle, Busy, Reboot, Failed };

// Ricezione in streaming → flash, SHA-256 al volo, verifica, partizione di avvio. step() si chiama a ogni giro
// del task di rete: legge al massimo maxBytes, così WebSocket e HTTP restano serviti.
class Updater {
 public:
  bool active() const { return phase_ == Phase::Receive || phase_ == Phase::Verify || phase_ == Phase::Write; }
  Phase phase() const { return phase_; }
  uint32_t done() const { return done_; }
  const Offer &offer() const { return offer_; }
  // Offerta già accettata: apre la partizione e il download. false (e Failed) se non parte.
  bool start(const Offer &o, const char *fromVersion, const char *label, Flash &f, Source &s, Ms now, UpdaterOut &out);
  Step step(Ms now, UpdaterOut &out, size_t maxBytes = 16384);
  void cancel(const char *why, UpdaterOut &out);  // annullato (connessione persa…): errore, partizione abbandonata

 private:
  void fail(const char *why, UpdaterOut &out);
  void report(Phase p, UpdaterOut &out, const char *error = nullptr);
  Step finish(UpdaterOut &out);

  Phase phase_ = Phase::None;
  Offer offer_ = {};
  Record rec_ = {};
  Flash *flash_ = nullptr;
  Source *src_ = nullptr;
  cry::Sha256Stream *sha_ = nullptr;
  alignas(8) uint8_t shaBuf_[sizeof(cry::Sha256Stream)];
  uint32_t done_ = 0;
  int sentStep_ = -1, shownPct_ = -1;
  Ms lastData_ = 0;
  bool recSaved_ = false;
  char why_[kWhyLen] = "";
};

// ---------- dopo il riavvio (OTA.md §3.4) ----------
enum class ImgState : uint8_t { Unknown, New, PendingVerify, Valid, Invalid, Aborted, Undefined };
enum class BootCase : uint8_t {
  Normal,      // niente da fare
  Trial,       // nuova versione «in prova»: va confermata entro kValidateMs (Wi-Fi + autenticazione)
  Confirmed,   // nuova versione già confermata: manca solo il «fatto» al servizio
  RolledBack,  // registro presente ma gira l'altra partizione: «errore» al servizio
};
BootCase bootCase(bool hasRecord, const Record &r, const char *runningLabel, ImgState st);

// Conferma entro 120 s: onAuthed() → MarkValid; tick() oltre il tempo → Rollback. Una sola decisione.
class Trial {
 public:
  enum class Act : uint8_t { None, MarkValid, Rollback };
  void begin(Ms now) { active_ = true; start_ = now; }
  bool active() const { return active_; }
  Act onAuthed(Ms now);
  Act tick(Ms now);

 private:
  bool active_ = false;
  Ms start_ = 0;
};

}  // namespace ota
}  // namespace cal
