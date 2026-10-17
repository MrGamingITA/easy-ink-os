#include "avatar.h"

#include <string.h>

#include "avatar_builtin.h"

const char *poseLabel(Pose p) {
  switch (p) {
    case Pose::Working: return "LAVORA";
    case Pose::Permission: return "PERMESSO";
    case Pose::Input: return "ATTENDE TE";
    case Pose::Done: return "FINITO";
    case Pose::Error: return "ERRORE";
    case Pose::Idle: return "IN PAUSA";
    case Pose::Compact: return "RIORDINA";
    case Pose::Tired: return "STANCA";
    case Pose::Stuffy: return "ARIA VIZIATA";
    case Pose::Run: return "CORRE";
    default: return "";
  }
}

const char *avatarErrorText(AvatarError e) {
  switch (e) {
    case AvatarError::Ok: return "ok";
    case AvatarError::Short: return "più corto dell'intestazione";
    case AvatarError::Magic: return "magia CAVT assente";
    case AvatarError::Version: return "versione non supportata";
    case AvatarError::Header: return "intestazione diversa da 72 byte";
    case AvatarError::Length: return "lunghezza dichiarata diversa da quella reale";
    case AvatarError::Crc: return "checksum errato";
    case AvatarError::Size: return "dimensione incoerente con pose e fotogrammi";
    case AvatarError::Frames: return "fotogrammi diversi da 2 o 4, o griglia diversa da 28×25";
    case AvatarError::FrameBytes: return "byte per fotogramma diversi da 175";
    case AvatarError::Mask: return "poseMask: posa obbligatoria assente o bit sconosciuto";
    case AvatarError::Reserved: return "byte riservato diverso da 0";
    case AvatarError::Id: return "id non valido";
    case AvatarError::Name: return "name non valido";
    case AvatarError::Pixel: return "codice di pixel 11 riservato";
  }
  return "?";
}

namespace {
// Passo del CRC-32 IEEE senza lo xor finale: a pezzi (CavtStream) e tutto insieme (avatarCrc32) danno lo stesso.
uint32_t crcStep(uint32_t c, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
  }
  return c;
}
// Codice 11 in qualche coppia di bit: (bit alto) & (bit basso spostato in alto).
bool badPixels(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (p[i] & 0xAA & (p[i] << 1)) return true;
  return false;
}
}  // namespace

uint32_t avatarCrc32(const uint8_t *p, size_t n) { return crcStep(0xFFFFFFFFu, p, n) ^ 0xFFFFFFFFu; }

namespace {

uint16_t u16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

int popcount(uint16_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}

// Campo di 24 byte completato con zeri: testo non vuoto, poi solo zeri.
bool fixedField(const uint8_t *p, char *out, bool kebab) {
  int n = 0;
  while (n < 24 && p[n]) n++;
  if (n == 0) return false;
  for (int k = n; k < 24; k++)
    if (p[k]) return false;
  if (kebab) {  // [a-z0-9]+(-[a-z0-9]+)*
    for (int k = 0; k < n; k++) {
      const uint8_t ch = p[k];
      const bool alnum = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
      if (ch == '-') {
        if (k == 0 || k == n - 1 || p[k - 1] == '-') return false;
      } else if (!alnum) {
        return false;
      }
    }
  } else {
    for (int k = 0; k < n; k++)
      if (p[k] < 0x20 || p[k] == 0x7F) return false;  // name UTF-8: niente caratteri di controllo
  }
  memcpy(out, p, n);
  out[n] = 0;
  return true;
}

}  // namespace

namespace {
// I controlli di parseAvatar nell'ordine di sempre, sull'intestazione (72 byte), con lunghezza, CRC dei byte dal 12
// in poi e pixel già calcolati da chi chiama (tutto il buffer o un pezzo alla volta).
AvatarError checkCavt(const uint8_t *buf, size_t len, uint32_t crc, bool pixelBad, Avatar &out) {
  if (memcmp(buf, "CAVT", 4) != 0) return AvatarError::Magic;
  if (buf[4] != 1) return AvatarError::Version;
  // v1: l'intestazione è di 72 byte esatti (come unpack() di pack.mjs, che calcola la dimensione con 72).
  if (buf[5] != kCavtHeader) return AvatarError::Header;
  const uint16_t total = u16(buf + 6);
  if (total != len) return AvatarError::Length;
  if (u32(buf + 8) != crc) return AvatarError::Crc;
  if (buf[12] != kAvatarW || buf[13] != kAvatarH) return AvatarError::Frames;
  const uint8_t frames = buf[14];
  if (frames != 2 && frames != 4) return AvatarError::Frames;
  if (buf[15] != 0) return AvatarError::Reserved;
  const uint16_t mask = u16(buf + 16);
  if ((mask & kRequiredPoses) != kRequiredPoses || (mask >> static_cast<int>(Pose::Count))) return AvatarError::Mask;
  if (u16(buf + 18) != kCavtFrameBytes) return AvatarError::FrameBytes;
  if (size_t(kCavtHeader) + size_t(popcount(mask)) * frames * kCavtFrameBytes != len) return AvatarError::Size;
  if (!fixedField(buf + 20, out.id, true)) return AvatarError::Id;
  if (!fixedField(buf + 44, out.name, false)) return AvatarError::Name;
  if (pixelBad) return AvatarError::Pixel;
  out.len = total;
  out.poseMask = mask;
  out.frames = frames;
  out.header = buf[5];
  out.crc = u32(buf + 8);
  return AvatarError::Ok;
}
}  // namespace

AvatarError parseAvatar(const uint8_t *buf, size_t len, Avatar &out) {
  memset(&out, 0, sizeof out);
  if (!buf || len < size_t(kCavtHeader)) return AvatarError::Short;
  const uint32_t crc = avatarCrc32(buf + 12, len - 12);
  const AvatarError e = checkCavt(buf, len, crc, badPixels(buf + kCavtHeader, len - kCavtHeader), out);
  if (e != AvatarError::Ok) return e;
  out.data = buf;
  return AvatarError::Ok;
}

void CavtStream::begin() {
  memset(hdr_, 0, sizeof hdr_);
  n_ = 0;
  crc_ = 0xFFFFFFFFu;
  pixel_ = false;
}

void CavtStream::feed(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n;) {
    if (n_ < size_t(kCavtHeader)) {  // intestazione: tenuta, CRC dal byte 12
      const size_t k = n - i < size_t(kCavtHeader) - n_ ? n - i : size_t(kCavtHeader) - n_;
      memcpy(hdr_ + n_, p + i, k);
      if (n_ + k > 12) {
        const size_t from = n_ > 12 ? n_ : 12;
        crc_ = crcStep(crc_, hdr_ + from, n_ + k - from);
      }
      n_ += k;
      i += k;
      continue;
    }
    crc_ = crcStep(crc_, p + i, n - i);  // fotogrammi: CRC e pixel, niente copia
    pixel_ = pixel_ || badPixels(p + i, n - i);
    n_ += n - i;
    i = n;
  }
}

AvatarError CavtStream::finish(Avatar &out) const {
  memset(&out, 0, sizeof out);
  if (n_ < size_t(kCavtHeader)) return AvatarError::Short;
  return checkCavt(hdr_, n_, crc_ ^ 0xFFFFFFFFu, pixel_, out);
}

Pose avatarPose(const Avatar &a, Pose p) {
  const int i = static_cast<int>(p);
  if (i < 0 || i >= static_cast<int>(Pose::Count)) return Pose::Working;
  if (a.poseMask & (1u << i)) return p;
  if (p == Pose::Compact || p == Pose::Run) return Pose::Working;
  if (p == Pose::Tired || p == Pose::Stuffy) return Pose::Idle;
  return Pose::Working;  // obbligatoria mancante: parseAvatar non lo permette
}

int avatarFrame(const Avatar &a, int f) {
  if (!a.frames) return 0;
  const int n = a.frames;
  return ((f % n) + n) % n;
}

bool avatarFrameBytes(const Avatar &a, Pose p, int f, uint8_t *dst) {
  if (!a.frames) return false;
  const int pi = static_cast<int>(avatarPose(a, p));
  const int idx = popcount(uint16_t(a.poseMask & ((1u << pi) - 1)));  // pose presenti prima di questa
  const uint32_t off = a.header + uint32_t(idx * a.frames + avatarFrame(a, f)) * kCavtFrameBytes;
  if (off + kCavtFrameBytes > a.len) return false;
  if (a.data) { memcpy(dst, a.data + off, kCavtFrameBytes); return true; }
  return a.read && a.read(a.ctx, off, dst, kCavtFrameBytes);
}

void drawAvatar(Surface &c, const Avatar &a, int ox, int oy, int s, Pose p, int f, bool ink) {
  uint8_t fr[kCavtFrameBytes];
  if (!avatarFrameBytes(a, p, f, fr)) return;
  for (int y = 0; y < kAvatarH; y++)
    for (int x = 0; x < kAvatarW; x++) {
      const int v = avatarPixel(fr, x, y);
      if (v == 1) c.fillRect(ox + x * s, oy + y * s, s, s, ink);
      else if (v == 2) c.fillRect(ox + x * s, oy + y * s, s, s, !ink);
    }
}

// ---------- predefiniti ----------
const Avatar *builtinAvatar(int i) {
  static Avatar views[8];
  static bool tried[8], ok[8];
  if (i < 0 || i >= kBuiltinAvatarCount || i >= 8) return nullptr;
  if (!tried[i]) {
    tried[i] = true;
    const BuiltinAvatar &b = kBuiltinAvatars[i];
    ok[i] = parseAvatar(b.data, b.len, views[i]) == AvatarError::Ok && views[i].crc == b.crc && strcmp(views[i].id, b.id) == 0;
  }
  return ok[i] ? &views[i] : nullptr;
}

const Avatar *builtinAvatarById(const char *id) {
  if (!id || !id[0]) return nullptr;
  for (int i = 0; i < kBuiltinAvatarCount; i++)
    if (strcmp(kBuiltinAvatars[i].id, id) == 0) return builtinAvatar(i);
  return nullptr;
}

const Avatar &gocciaAvatar() {
  static const Avatar empty = {};
  const Avatar *g = builtinAvatar(0);
  return g ? *g : empty;
}
