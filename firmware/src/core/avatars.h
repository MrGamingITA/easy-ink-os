// Avatar sul display (docs/AVATAR.md §6 e §10): quale avatar disegnare, cache degli avatar ricevuti,
// richieste al servizio e messaggi `avatar` / `avatar-need`. Tutto puro e provato in native (test_avatar);
// sulla scheda la memoria della cache è LittleFS (hw/avatar_fs).
//
// Precedenza: avatar della sessione → se manca, la Goccia finché non arriva; sessione senza avatar →
// avatar predefinito (set.defAvatar) → se manca, la Goccia. I predefiniti compilati (Goccia, Gatto,
// Demone, Calamaro) valgono senza rete; con un crc diverso (libreria dell'utente) serve quello del servizio.
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx/avatar.h"
#include "model.h"

namespace cal {

static_assert(kAvatarRefLen == kAvatarIdLen, "AvatarRef e CAVT: stessa misura dell'id");

// ---------- memoria persistente della cache ----------
// Un «posto» per avatar (file intero) e un piccolo indice con l'ordine d'uso.
class AvatarStorage {
 public:
  virtual ~AvatarStorage() {}
  virtual int slotSize(int slot) = 0;  // byte del posto, -1 se vuoto
  virtual bool readSlot(int slot, uint32_t off, uint8_t *dst, uint16_t n) = 0;
  virtual bool writeSlot(int slot, const uint8_t *data, uint16_t n) = 0;  // tutto o niente
  virtual void eraseSlot(int slot) = 0;
  virtual int readIndex(uint8_t *dst, int cap) = 0;  // byte letti, -1 se assente
  virtual bool writeIndex(const uint8_t *data, int n) = 0;
  virtual void eraseAll() = 0;
  // 0.14.0 (R4): un CAVT scaricato a pezzi (AvatarDownload, task di rete) in un temporaneo suo, mai quello di
  // writeSlot; adoptTemp (loop, AvatarCache::adopt) lo rinomina nel posto, tutto o niente. Di serie: non c'è.
  virtual bool tempBegin() { return false; }
  virtual bool tempAppend(const uint8_t *, uint16_t) { return false; }
  virtual bool tempEnd() { return false; }  // chiude il temporaneo dopo l'ultimo pezzo
  virtual void tempAbort() {}
  virtual bool adoptTemp(int, uint32_t) { return false; }  // len = byte attesi nel temporaneo; se non tornano o fallisce, il posto resta com'era
};

// In RAM: per i test native (e per la build senza file system).
class MemAvatarStorage : public AvatarStorage {
 public:
  static constexpr int kSlots = 8;
  MemAvatarStorage();
  int slotSize(int slot) override;
  bool readSlot(int slot, uint32_t off, uint8_t *dst, uint16_t n) override;
  bool writeSlot(int slot, const uint8_t *data, uint16_t n) override;
  void eraseSlot(int slot) override;
  int readIndex(uint8_t *dst, int cap) override;
  bool writeIndex(const uint8_t *data, int n) override;
  void eraseAll() override;
  bool tempBegin() override;
  bool tempAppend(const uint8_t *p, uint16_t n) override;
  bool tempEnd() override;
  void tempAbort() override;
  bool adoptTemp(int slot, uint32_t len) override;
  // Per i test: byte grezzi di un posto (per rovinarli) e scritture fallite.
  uint8_t *raw(int slot) { return data_[slot]; }
  bool failWrites = false;
  int writes = 0;
  int tempLen() const { return tempLen_; }  // -1 = nessun temporaneo

 private:
  uint8_t data_[kSlots][kCavtMaxBytes];
  int size_[kSlots];
  uint8_t index_[256];
  int indexLen_;
  uint8_t temp_[kCavtMaxBytes];
  int tempLen_ = -1;
  bool tempOpen_ = false;
};

// ---------- cache ----------
// Almeno 8 avatar ricevuti (docs/AVATAR.md §5), chiave (id, crc). Pieno: si sostituisce il meno usato.
// Stesso id con un crc nuovo: sostituisce la versione vecchia (è un aggiornamento dello stesso disegno).
class AvatarCache {
 public:
  static constexpr int kSlots = 8;
  AvatarCache();
  // Legge l'indice e rivalida ogni posto (CAVT completo, CRC compreso): i posti rovinati si cancellano.
  // Restituisce il numero di avatar validi.
  int begin(AvatarStorage *st);
  // Valida e salva. Ok anche se c'era già (stesso id e crc: niente scritture). id e crc sono quelli del
  // messaggio: devono coincidere con l'intestazione del CAVT, altrimenti Id o Crc.
  AvatarError store(const char *id, uint32_t crc, const uint8_t *data, size_t len, int *slotOut = nullptr);
  // 0.14.0 (R4): il CAVT già scaricato e controllato a pezzi (AvatarDownload::finish → hdr) entra nella cache
  // rinominando il temporaneo della memoria nel posto scelto come per store. Ok anche se c'era già (temporaneo buttato).
  AvatarError adopt(const Avatar &hdr, int *slotOut = nullptr);
  // Cerca (id, crc) — senza crc: la versione usata più di recente — e la segna usata.
  const Avatar *find(const char *id, uint32_t crc, bool hasCrc);
  bool has(const char *id, uint32_t crc, bool hasCrc) const;  // come find, senza segnarla
  int count() const;
  void clear();  // Ripristino di fabbrica
  // Per i test e il log.
  const Avatar *slot(int i) const { return used_[i] ? &av_[i] : nullptr; }

 private:
  struct Ctx { AvatarStorage *st; int slot; };
  static bool readCb(const void *ctx, uint32_t off, uint8_t *dst, uint16_t n);
  int lookup(const char *id, uint32_t crc, bool hasCrc) const;
  int pickSlot(const char *id) const;  // versione vecchia dello stesso id, poi un posto libero, poi il meno usato
  void saveIndex();
  AvatarStorage *st_;
  bool used_[kSlots];
  uint32_t stamp_[kSlots];
  uint32_t clock_;
  Avatar av_[kSlots];
  Ctx ctx_[kSlots];
};

// ---------- quale avatar disegnare ----------
// Cache del display (nullptr = solo i predefiniti, come nei test delle schermate).
void setAvatarCache(AvatarCache *c);
AvatarCache *avatarCache();
// Predefiniti, poi cache. nullptr se non c'è (o se r.id è vuoto).
const Avatar *lookupAvatar(const AvatarRef &r);
// L'avatar di una sessione: il suo, se c'è; '' → il predefinito; mancante → la Goccia.
const Avatar &sessionAvatar(const Model &m, const Session &s);
// Schermate senza sessione (avvio, Riposo, PC non raggiungibile, PC spento, Reti vicine…).
const Avatar &defaultAvatar(const Model &m);
// id valido (kebab-case, 1-24 byte): gli altri non si cercano e non si chiedono.
bool validAvatarId(const char *id);
// L'avatar id è di una sessione o è il predefinito? (Arrivato un avatar: si ridisegna solo se serve.)
bool avatarWanted(const Model &m, const char *id);

// ---------- richieste al servizio ----------
// Quali avatar mancanti chiedere con `avatar-need`: una volta appena servono (state con un avatar
// sconosciuto, predefinito nuovo), poi di nuovo ogni 30 s finché non arrivano o non servono più.
class AvatarNeeds {
 public:
  static constexpr Ms kRetryMs = 30000;
  static constexpr int kCap = kMaxSessions + 1;  // sessioni + predefinito
  AvatarNeeds() { reset(); }
  // Id distinti da chiedere adesso (al massimo cap); li segna chiesti a `now`.
  int due(const Model &m, Ms now, char (*out)[kAvatarRefLen], int cap);
  // Connessione nuova (o persa): tutto da richiedere subito, anche quelli rifiutati.
  void reset();
  // Il servizio ha mandato per (id, crc) un CAVT che non passa la validazione: non lo si richiede più
  // (fino alla prossima connessione), così un avatar rotto sul PC non diventa un ciclo di richieste.
  void rejected(const char *id, uint32_t crc);
  int pending() const { return n_; }

 private:
  struct E { AvatarRef ref; Ms askedAt; bool asked; };
  E e_[kCap];
  int n_;
  AvatarRef rej_[4];
  int nRej_;
};

// ---------- messaggi ----------
// `avatar` servizio → display: {t, v, id, crc, data: CAVT in base64}. Decodifica il base64 in out.data
// (al massimo kCavtMaxBytes). La validazione del CAVT la fa AvatarCache::store.
struct AvatarMsg {
  char id[kAvatarRefLen];
  uint32_t crc;
  uint16_t len;
  uint8_t data[kCavtMaxBytes];
};
enum class AvatarMsgError : uint8_t { Ok, Type, Id, Crc, Data };
AvatarMsgError decodeAvatarMsg(JsonVariantConst root, AvatarMsg &out);
// `avatar-need` display → servizio: {"t":"avatar-need","v":1,"ids":[…]}. 0 se non sta in cap.
size_t encodeAvatarNeed(const char (*ids)[kAvatarRefLen], int n, char *out, size_t cap);
// Campi `avatar`/`avatarCrc` (o `defaultAvatar`/`defaultAvatarCrc`): false se l'id manca o non è una stringa.
// Id troppo lungo o non kebab-case → id '-' (non si trova e non si chiede: si disegna la Goccia).
bool decodeAvatarRef(JsonVariantConst id, JsonVariantConst crc, AvatarRef &out);

// ---------- 0.14.0 (R4): avatar a pezzi, come i temi (PROTOCOLLO.md «Avatar») ----------
// avatar-offer {id, crc, size, token} (anche senza autenticazione, come `avatar`) → GET /avatars/<token> sulla porta
// del WebSocket, a pezzi nella cache → avatar-ack {id, crc, ok, error?}. Niente base64, niente blocco da 9,5 KB.
constexpr uint32_t kCavtMinBytes = kCavtHeader + 6 * 2 * kCavtFrameBytes;  // 2 172: sei pose obbligatorie, 2 fotogrammi
struct AvatarOffer {
  char id[kAvatarRefLen];
  uint32_t crc;
  uint32_t size;   // kCavtMinBytes..kCavtMaxBytes
  char token[33];  // 16 byte in 32 cifre esadecimali minuscole
};
// Un'offerta non valida (id, token, size fuori regola) → false, out com'era.
bool decodeAvatarOffer(JsonVariantConst root, AvatarOffer &out);
// avatar-ack: id non valido → 0 (non si manda); error al massimo 127 byte, tagliato su un confine UTF-8.
size_t encodeAvatarAck(const char *id, uint32_t crc, bool ok, const char *error, char *out, size_t cap);
// http://<host>:<port>/avatars/<token>; false se non ci sta.
bool avatarUrl(const char *host, uint16_t port, const char *token, char *out, size_t cap);

// Download a pezzi nel temporaneo della memoria, con i controlli di parseAvatar man mano (CavtStream): begin, write
// per ogni pezzo (mai oltre size), finish (lunghezza, CAVT, id e crc dell'offerta) → hdr per AvatarCache::adopt.
// Un errore lascia il motivo in why (testi di avatar-ack); chi chiama poi abort(). RAM: ~140 byte, sullo stack del task di rete.
class AvatarDownload {
 public:
  bool begin(AvatarStorage &st, const AvatarOffer &o, char *why, size_t cap);
  bool write(const uint8_t *p, size_t n, char *why, size_t cap);
  bool finish(Avatar &hdr, char *why, size_t cap);
  void abort();
  // Dopo un finish fallito: true = CAVT rotto sul PC (controlli del CAVT, id o CRC diversi dall'offerta), da
  // ricordare come rifiutato (AvatarNeeds::rejected); rete, scrittura o lunghezza no (si riprova). begin lo azzera.
  bool badContent() const { return bad_; }

 private:
  AvatarStorage *st_ = nullptr;
  bool bad_ = false;
  char id_[kAvatarRefLen] = "";
  uint32_t crc_ = 0, size_ = 0, got_ = 0;
  CavtStream cs_;
};

// Base64 standard (RFC 4648, con il riempimento '='): byte scritti, 0 se non valido o se non sta in cap.
size_t base64Decode(const char *s, size_t n, uint8_t *out, size_t cap);

}  // namespace cal
