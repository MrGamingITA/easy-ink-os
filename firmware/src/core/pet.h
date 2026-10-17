// Gioco della mascotte sulla scheda (0.10.0, specifica docs/superpowers/specs/2026-10-11-gioco-mascotte-design.md §3):
// messaggi `game`, `accessory`, `anchors` (servizio → display) e `accessory-need`, `anchors-need`, `accessory-wear`
// (display → servizio), regole della festa e salvataggio dell'ultimo riassunto. Tutto puro, provato in native (test_pet).
// Il gioco lo calcola il servizio: qui si mostra soltanto. La copia di riferimento è applyGame/petCelebrate del
// simulatore (reference/calamaio-os-browser/assets/calamaio-test.js).
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx/accessory.h"
#include "model.h"

namespace cal {

// Il gioco conta? Interruttore della scheda (set.game) e gioco acceso sul PC (pet.on).
bool petOn(const Model &m);
// Interruttore del gioco (set.game): la fonte è il servizio (game.on, PROTOCOLLO.md «Impostazione game»); il menu e un
// `set` lo cambiano qui, ogni `game` lo riallinea. Spento: niente festa e via le schermate del gioco (La mascotte,
// Accessorio) dalla pila.
void setGameSwitch(Model &m, bool on);
extern const char *const kPetSpecialToast;  // «Nuovo accessorio speciale: chiedilo a Claude»
// Festa di un traguardo in corso (3 s).
bool petParty(const Model &m, Ms now);
// La festa non parte con un permesso aperto, in silenzio (anche durante la concentrazione), di notte o in risparmio
// energetico: lì solo il toast «Traguardo: …».
bool partyBlocked(const Model &m, Ms now);
// XP per passare dal livello n al n+1 (specifica §2): round(100 × n^1,3).
int levelCost(int n);

// ---------- game (servizio → display) ----------
// {on, level, xp, xpLevelStart, xpNext, energy, tired, wear: {id, crc} | null, celebrate: {id, title} | null,
//  unlocked: [{id, name}]}. Campi mancanti o del tipo sbagliato: valori neutri (0, false, nessuno); id non validi
// (non kebab-case, oltre 24 byte) si scartano; titoli e nomi si accorciano su un confine UTF-8.
struct GameMsg {
  bool on;
  int level;
  int32_t xp, xpLevelStart, xpNext;
  int energy;
  bool tired;
  bool special;  // facoltativo: accessorio speciale da chiedere a Claude
  bool hasWear;
  AvatarRef wear;
  bool hasCelebrate;
  char celebrateId[kAvatarRefLen];
  char celebrateTitle[kPetTitleLen];
  PetUnlocked unlocked[kPetUnlockedCap];
  int nUnlocked;
};
bool decodeGame(JsonVariantConst root, GameMsg &out);  // false solo se non è un `game`
// Il riassunto di adesso come messaggio (senza celebrate): i pulsanti Simula e i test ci costruiscono sopra.
void petMsg(const Pet &p, GameMsg &out);

enum class Celebrate : uint8_t { None, Toast, Party };
// Un traguardo (celebrate): in testa agli ultimi 3, toast «Traguardo: <titolo>» di 3 s e, se partyBlocked è falso,
// festa di 3 s (pet.party, burstUntil). None: gioco spento, id vuoto o lo stesso dell'ultimo celebrate.
Celebrate petCelebrate(Model &m, const char *id, const char *title, Ms now);
// Messaggio `game`: sostituisce il riassunto (pet.known diventa vero), allinea set.game a on, poi festeggia il celebrate.
// special: toast «Nuovo accessorio speciale…» la prima volta da questa accensione (dopo quello del traguardo, se c'è).
Celebrate applyGame(Model &m, const GameMsg &g, Ms now);

// ---------- accessory e anchors (servizio → display) ----------
// {id, crc, data: CACC in base64, al massimo 1 024 byte}. La validazione del CACC la fa AccessoryStore::store.
struct AccessoryMsg {
  char id[kAvatarRefLen];
  uint32_t crc;
  uint16_t len;
  uint8_t data[kAccMaxBytes];
};
// {avatar, avatarCrc, data: CANC in base64, al massimo 420 byte}: le ancore dell'avatar (avatar, avatarCrc).
struct AnchorsMsg {
  char avatar[kAvatarRefLen];
  uint32_t avatarCrc;
  uint16_t len;
  uint8_t data[kAncMaxBytes];
};
enum class BlobMsgError : uint8_t { Ok, Type, Id, Crc, Data };
BlobMsgError decodeAccessoryMsg(JsonVariantConst root, AccessoryMsg &out);
BlobMsgError decodeAnchorsMsg(JsonVariantConst root, AnchorsMsg &out);

// ---------- display → servizio ----------
// {"t":"accessory-need","v":1,"ids":[…]} · {"t":"anchors-need","v":1,"avatars":[{"id":…,"crc":…}]} ·
// {"t":"accessory-wear","v":1,"id":"cappellino"} (id '' → null: nessun accessorio). 0 se non sta in cap.
size_t encodeAccessoryNeed(const char (*ids)[kAvatarRefLen], int n, char *out, size_t cap);
size_t encodeAnchorsNeed(const char (*ids)[kAvatarRefLen], const uint32_t *crcs, int n, char *out, size_t cap);
size_t encodeAccessoryWear(const char *id, char *out, size_t cap);

// ---------- salvataggio (LittleFS, hw/avatar_fs) ----------
// L'ultimo riassunto (on, livello, XP, energia, stanca, indossato, sbloccati), così all'avvio la mascotte indossa già
// l'accessorio anche prima della rete. Versione 1 e CRC-32 in coda. I traguardi restano in RAM (specifica §3).
const size_t kPetBlobMax = 1 + 1 + 2 + 12 + 1 + 24 + 4 + 1 + kPetUnlockedCap * 48 + 4;  // 1 010
size_t packPet(const Pet &p, uint8_t *out, size_t cap);
// false (e p intatto) se il blocco è corto, di un'altra versione o rovinato. Con true: p.known = true.
bool unpackPet(const uint8_t *in, size_t len, Pet &p);

}  // namespace cal
