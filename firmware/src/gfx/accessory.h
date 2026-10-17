// Accessori della mascotte (gioco, docs/AVATAR.md §13–14): lettura dei blocchi CACC v1 (accessorio) e CANC v1
// (punti di aggancio di un avatar) e disegno. drawAccessory è la copia di drawAccessory() del riferimento
// tools/avatar/lib/draw-accessory.mjs, identica a quella del simulatore: test_accessory la confronta pixel per pixel
// con i golden generati dal riferimento (tools/golden/accessory-golden.mjs).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "avatar.h"

// Costanti dei formati (AVATAR.md §13.4 e §14.3).
constexpr int kAccMaxW = 14, kAccMaxH = 10;
constexpr int kAccHeader = 72;
constexpr int kAccMaxBytes = 1024;
constexpr int kAncHeader = 20;
constexpr int kAnchorPoints = 5;                                            // head-top, eyes, mouth, neck, hand
constexpr int kAncMaxBytes = kAncHeader + 10 * 4 * kAnchorPoints * 2;      // 420: 10 pose × 4 fotogrammi
constexpr int kAvatarCells = kAvatarW * kAvatarH;                          // 700

enum class AnchorPoint : uint8_t { HeadTop, Eyes, Mouth, Neck, Hand };
const char *anchorName(int i);  // 'head-top', … (ANCHOR_NAMES)

enum class AccError : uint8_t {
  Ok, Short, Magic, Version, Header, Length, TooBig, Crc, Size, Frames, Anchor, Origin, Options, Poses, FrameBytes, Id, Name,
  Retouch, Total,
};
const char *accErrorText(AccError e);

// Vista su un CACC già validato (i byte restano dove sono: flash per il catalogo, RAM per quelli ricevuti).
struct Accessory {
  const uint8_t *data;
  uint16_t len;
  uint32_t crc;
  char id[kAvatarIdLen];
  char name[kAvatarIdLen];
  uint8_t w, h, frames, anchor, ox, oy;
  bool outline, special;
  uint16_t hide;      // bit i = posa i nascosta (ordine di Pose)
  uint16_t fb;        // byte per fotogramma = ⌈w·h/4⌉
  uint8_t nRetouch;   // ritocchi da kAccHeader in poi (si cercano per id quando si disegna)
  uint16_t px;        // primo byte dei fotogrammi comuni
};

// Validazione completa di unpackAccessory() (lib/accessory.mjs): magia, versione, intestazione di 72 byte,
// lunghezza dichiarata = reale e ≤ 1 024, CRC, dimensioni 1..14 × 1..10, fotogrammi 1 o 2, ancora 0..4, origine
// dentro l'accessorio, opzioni e pose sconosciute, byte per fotogramma, id kebab-case e name (24 byte completati con
// zeri), ritocchi (id kebab-case e non ripetuti, opzione 0/1, pixel dentro il file) e dimensione totale coerente.
AccError parseAccessory(const uint8_t *buf, size_t len, Accessory &out);

enum class AncError : uint8_t { Ok, Short, Magic, Version, Header, Length, Crc, Points, Frames, Mask, Size, Range };
const char *ancErrorText(AncError e);

// Vista su un CANC già validato: legato al CAVT con CRC avatarCrc (AVATAR.md §13.4).
struct Anchors {
  const uint8_t *data;
  uint16_t len;
  uint32_t avatarCrc;
  uint16_t poseMask;
  uint8_t frames;
};
// Come unpackAnchors() di lib/anchors.mjs: magia, versione, intestazione, lunghezza, CRC, 5 punti, fotogrammi 2 o 4,
// bit di posa oltre il 9, dimensione coerente, punti dentro 28×25 (0xFF 0xFF = assente, nient'altro con 0xFF).
AncError parseAnchors(const uint8_t *buf, size_t len, Anchors &out);

// Punto di aggancio `which` della posa p e del fotogramma f (getAnchor di lib/anchors.mjs): ripiego della posa se
// l'avatar non la disegna, f mod frames delle ancore. false = assente (anche senza ancore): mai posizioni inventate.
bool anchorPoint(const Avatar &av, const Anchors *anc, Pose p, int f, int which, int &x, int &y);

// Fotogramma della mascotte come griglia di codici (0 trasparente, 1 inchiostro, 2 carta), riga per riga.
bool avatarGrid(const Avatar &av, Pose p, int f, uint8_t *grid);

// Disegna l'accessorio sulla griglia (con la mascotte di p e f). false = non si disegna (posa chiesta nascosta,
// ancora assente). Ordine del riferimento: contorno (carta sull'inchiostro della mascotte attorno ai pixel d'inchiostro
// dell'accessorio, mai sotto l'accessorio né sul trasparente), poi l'accessorio ('x' = l'opposto di quello che c'è sotto).
// I pixel fuori dal riquadro 28×25 si tagliano.
bool drawAccessory(uint8_t *grid, const Avatar &av, const Anchors *anc, const Accessory &acc, Pose p, int f);

// ---------- predefiniti (gfx/accessory_builtin.cpp) ----------
// Accessorio del catalogo i (nell'ordine della specifica), validato la prima volta che serve. nullptr se non va
// (non succede: lo controlla test_accessory).
const Accessory *builtinAccessory(int i);
// Per id ('' o sconosciuto → nullptr); con hasCrc solo quella versione.
const Accessory *builtinAccessoryById(const char *id, uint32_t crc = 0, bool hasCrc = false);
// Ancore compilate per questo avatar: stesso id e stesso CRC del CAVT. nullptr se non ci sono.
const Anchors *builtinAnchorsFor(const Avatar &av);
