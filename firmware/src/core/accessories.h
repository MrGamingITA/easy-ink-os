// Accessori e ancore sul display (gioco della mascotte, docs/AVATAR.md §13–14): catalogo compilato, cache di quelli
// ricevuti (accessori CACC e ancore CANC degli avatar dell'utente) e richieste al servizio (`accessory-need`,
// `anchors-need`, ogni 30 s finché mancano, come gli avatar). Puro e provato in native (test_pet); sulla scheda la
// memoria è LittleFS accanto agli avatar (hw/avatar_fs).
//
// Regole: l'accessorio si disegna solo con il gioco acceso (petOn) e su un avatar che ha le ancore (compilate per i
// predefiniti, ricevute per gli altri, sempre legate a id e CRC del CAVT): senza ancore niente accessorio.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "gfx/accessory.h"
#include "model.h"

namespace cal {

// ---------- memoria persistente ----------
// Un file per id: l'ultimo accessorio ricevuto con quell'id, le ultime ancore ricevute per quell'avatar.
class AccStorage {
 public:
  enum Kind : uint8_t { Acc, Anc };
  virtual ~AccStorage() {}
  virtual int read(Kind k, const char *id, uint8_t *dst, int cap) = 0;  // byte letti, -1 se assente o più lungo di cap
  virtual bool write(Kind k, const char *id, const uint8_t *data, int n) = 0;  // tutto o niente
  virtual void remove(Kind k, const char *id) = 0;
  virtual int list(Kind k, char (*ids)[kAvatarRefLen], int cap) = 0;
  virtual void eraseAll() = 0;
};

// In RAM: per i test native.
class MemAccStorage : public AccStorage {
 public:
  static constexpr int kCap = 20;
  MemAccStorage() { eraseAll(); }
  int read(Kind k, const char *id, uint8_t *dst, int cap) override;
  bool write(Kind k, const char *id, const uint8_t *data, int n) override;
  void remove(Kind k, const char *id) override;
  int list(Kind k, char (*ids)[kAvatarRefLen], int cap) override;
  void eraseAll() override;
  // Per i test: byte grezzi di un file (per rovinarli), letture e scritture.
  uint8_t *raw(Kind k, const char *id, int *len = nullptr);
  bool failWrites = false;
  int reads = 0, writes = 0;

 private:
  struct E { bool used; Kind k; char id[kAvatarRefLen]; int n; uint8_t data[kAccMaxBytes]; };
  E *find(Kind k, const char *id);
  E e_[kCap];
};

// ---------- cache ----------
class AccessoryStore {
 public:
  static constexpr int kAncSlots = 8;   // ancore in RAM (≤ 420 byte ciascuna): sessioni e predefinito, con margine
  static constexpr int kAccFiles = 16;  // accessori ricevuti tenuti in memoria (oltre si toglie il più vecchio non indossato)
  AccessoryStore();
  // Legge e rivalida le ancore salvate (le rovinate si cancellano). Restituisce quante sono valide.
  int begin(AccStorage *st);
  // Accessorio (id, crc; senza crc qualunque versione): prima il catalogo compilato, poi quello ricevuto (uno in RAM,
  // gli altri dalla memoria quando servono). nullptr se non c'è.
  const Accessory *find(const AvatarRef &r);
  bool has(const AvatarRef &r) { return find(r) != nullptr; }
  // Valida e salva un accessorio ricevuto. id e crc del messaggio devono coincidere con il CACC (Id, Crc altrimenti).
  // keep: id da non togliere se la memoria è piena (l'indossato).
  AccError store(const char *id, uint32_t crc, const uint8_t *data, size_t len, const char *keep = "");
  // Ancore dell'avatar: compilate (predefiniti con lo stesso CRC), poi ricevute con lo stesso id e CRC. nullptr = nessuna.
  const Anchors *anchorsFor(const Avatar &av);
  // Valida e salva le ancore ricevute per (avatar, avatarCrc): il CANC deve dire lo stesso CRC (Range altrimenti).
  AncError storeAnchors(const char *avatar, uint32_t avatarCrc, const uint8_t *data, size_t len);
  void clear();  // Ripristino di fabbrica
  int anchorsCount() const;

 private:
  AccStorage *st_;
  uint8_t cur_[kAccMaxBytes];  // l'accessorio ricevuto in RAM (di solito l'indossato)
  Accessory curView_;
  bool curOk_;
  AvatarRef miss_;             // ultima ricerca fallita in memoria: non si rilegge finché non arriva qualcosa
  bool missSet_;
  struct AncSlot { bool used; char id[kAvatarRefLen]; uint32_t stamp; uint16_t len; uint8_t data[kAncMaxBytes]; Anchors view; };
  AncSlot anc_[kAncSlots];
  uint32_t clock_;
  bool loadAcc(const char *id);
};

// ---------- quale accessorio disegnare ----------
void setAccessoryStore(AccessoryStore *s);  // nullptr = solo il catalogo compilato (test delle schermate)
AccessoryStore *accessoryStore();
// L'accessorio indossato, solo con il gioco acceso e se c'è (catalogo o ricevuto). nullptr altrimenti.
const Accessory *wornAccessory(const Model &m);
// Ancore dell'avatar (compilate o ricevute). nullptr = l'avatar non porta accessori.
const Anchors *anchorsFor(const Avatar &av);
// Fotogramma della mascotte (posa p, f) con l'accessorio indossato sopra, come griglia di codici. false = lettura del
// CAVT fallita. drawn = l'accessorio si è disegnato.
bool figureGrid(const Model &m, const Avatar &av, Pose p, int f, uint8_t *grid, bool *drawn = nullptr);

// ---------- richieste al servizio ----------
// accessory-need: l'indossato, se il gioco è acceso e non c'è. anchors-need: gli avatar mostrati (sessioni e
// predefinito, quelli che ci sono) senza ancore, se il gioco è acceso e c'è un accessorio indossato. Una volta appena
// servono, poi ogni 30 s finché non arrivano o non servono più; reset() a ogni connessione nuova.
class AccNeeds {
 public:
  static constexpr Ms kRetryMs = 30000;
  static constexpr int kCap = kMaxSessions + 1;
  AccNeeds() { reset(); }
  int dueAccessories(const Model &m, Ms now, char (*out)[kAvatarRefLen], int cap);
  int dueAnchors(const Model &m, Ms now, char (*ids)[kAvatarRefLen], uint32_t *crcs, int cap);
  void reset();
  // Il servizio ha mandato per (id, crc) un blocco che non passa la validazione: non lo si richiede più fino alla
  // prossima connessione (come AvatarNeeds::rejected).
  void rejected(bool anchors, const char *id, uint32_t crc);

 private:
  struct E { AvatarRef ref; Ms askedAt; bool asked; };
  struct List { E e[kCap]; int n; AvatarRef rej[4]; int nRej; };
  int due(List &l, const AvatarRef *want, int nw, Ms now, char (*ids)[kAvatarRefLen], uint32_t *crcs, int cap);
  List acc_, anc_;
};

}  // namespace cal
