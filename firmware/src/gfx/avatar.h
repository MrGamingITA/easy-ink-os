// Avatar delle sessioni (docs/AVATAR.md): lettura del formato binario CAVT v1 e disegno.
// La Goccia è anch'essa un CAVT (gfx/avatar_builtin.cpp, generato da avatars/goccia.avatar.json) e
// test_mascot la confronta pixel per pixel con mascot() del simulatore.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "canvas.h"

// Ordine fisso delle pose: è l'ordine dei bit di poseMask nel CAVT (docs/AVATAR.md §2.2) e di
// cal::MascotPose (verificato da test_core). Run (bit 9, AVATAR.md §12) è facoltativa: la corsa sulla barra
// dell'aggiornamento; senza, si disegna Working.
enum class Pose : uint8_t { Working, Permission, Input, Done, Error, Idle, Compact, Tired, Stuffy, Run, Count };

const char *poseLabel(Pose p);  // etichette come STATES[...].label del simulatore

// Costanti del formato v1 (docs/AVATAR.md §8).
constexpr int kAvatarW = 28, kAvatarH = 25;
constexpr int kCavtHeader = 72;
constexpr int kCavtFrameBytes = (kAvatarW * kAvatarH + 3) / 4;           // 175
constexpr int kCavtMaxBytes = kCavtHeader + 10 * 4 * kCavtFrameBytes;     // 7 072: 10 pose (con run) × 4 fotogrammi
constexpr int kAvatarIdLen = 25;                                          // 24 byte + terminatore
constexpr uint16_t kRequiredPoses = 0x3F;                                 // bit 0-5: working…idle

enum class AvatarError : uint8_t {
  Ok, Short, Magic, Version, Header, Length, Crc, Size, Frames, FrameBytes, Mask, Reserved, Id, Name, Pixel,
};
const char *avatarErrorText(AvatarError e);

// Vista su un CAVT già validato. I byte stanno in memoria (data: predefiniti in flash, test) oppure si
// leggono a pezzi con read() (cache in LittleFS): drawAvatar legge un fotogramma alla volta (175 byte).
struct Avatar {
  const uint8_t *data;
  bool (*read)(const void *ctx, uint32_t off, uint8_t *dst, uint16_t n);
  const void *ctx;
  uint16_t len;
  uint16_t poseMask;
  uint8_t frames;
  uint8_t header;
  uint32_t crc;
  char id[kAvatarIdLen];
  char name[kAvatarIdLen];
};

// CRC-32 IEEE (quello di zlib), come crc32() di tools/avatar/lib/pack.mjs.
uint32_t avatarCrc32(const uint8_t *p, size_t n);

// Validazione completa di unpack() (docs/AVATAR.md §8 e §10): magia, versione, intestazione di 72 byte,
// lunghezza dichiarata = reale, CRC, w × h = 28 × 25, fotogrammi 2 o 4, byte per fotogramma, poseMask
// (pose obbligatorie presenti, nessun bit oltre il 9: run è l'ultimo conosciuto), dimensione coerente, riservato = 0, id kebab-case,
// name non vuoto, nessun pixel con il codice 11 riservato. Ok → out punta a buf (buf deve restare vivo).
AvatarError parseAvatar(const uint8_t *buf, size_t len, Avatar &out);

// 0.14.0 (R4): gli stessi controlli di parseAvatar su un CAVT che arriva a pezzi (download nella cache in LittleFS,
// mai tutto in RAM). Tiene i 72 byte d'intestazione, calcola CRC e pixel man mano; finish() dà lo stesso esito di
// parseAvatar sugli stessi byte (stessa funzione dei controlli), con out.data nullo: la vista si legge dal file.
class CavtStream {
 public:
  CavtStream() { begin(); }
  void begin();
  void feed(const uint8_t *p, size_t n);
  AvatarError finish(Avatar &out) const;
  size_t size() const { return n_; }

 private:
  uint8_t hdr_[kCavtHeader];
  size_t n_;
  uint32_t crc_;  // CRC-32 dei byte dal 12 in poi, prima dello xor finale
  bool pixel_;    // un pixel col codice 11 riservato
};

// La posa che si disegna: se manca, il ripiego (compact e run → working, tired e stuffy → idle).
Pose avatarPose(const Avatar &a, Pose p);
// Fotogramma f (qualunque intero, anche negativo) ridotto al numero di fotogrammi: f mod frames.
int avatarFrame(const Avatar &a, int f);
// Codice del pixel (0 trasparente, 1 inchiostro, 2 carta) dal fotogramma da 175 byte.
inline int avatarPixel(const uint8_t *frame, int x, int y) {
  const int k = y * kAvatarW + x;
  return (frame[k >> 2] >> (6 - 2 * (k & 3))) & 3;
}
// Copia in dst i 175 byte del fotogramma (posa con ripiego, f mod frames). false se la lettura fallisce.
bool avatarFrameBytes(const Avatar &a, Pose p, int f, uint8_t *dst);

// Disegna come P() di mascot(): il punto (x, y) a scala s occupa ox + x·s, oy + y·s, s × s.
// Inchiostro → colore ink, carta → !ink, trasparente → niente. Lettura fallita → niente.
void drawAvatar(Surface &c, const Avatar &a, int ox, int oy, int s, Pose p, int f, bool ink = true);
