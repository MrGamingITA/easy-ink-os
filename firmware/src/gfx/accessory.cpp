#include "accessory.h"

#include <string.h>

#include "accessory_builtin.h"

const char *anchorName(int i) {
  static const char *const k[kAnchorPoints] = {"head-top", "eyes", "mouth", "neck", "hand"};
  return i >= 0 && i < kAnchorPoints ? k[i] : "";
}

const char *accErrorText(AccError e) {
  switch (e) {
    case AccError::Ok: return "ok";
    case AccError::Short: return "più corto dell'intestazione";
    case AccError::Magic: return "magia CACC assente";
    case AccError::Version: return "versione non supportata";
    case AccError::Header: return "intestazione diversa da 72 byte";
    case AccError::Length: return "lunghezza dichiarata diversa da quella reale";
    case AccError::TooBig: return "oltre 1 024 byte";
    case AccError::Crc: return "checksum errato";
    case AccError::Size: return "dimensioni fuori da 1..14 × 1..10";
    case AccError::Frames: return "fotogrammi diversi da 1 o 2";
    case AccError::Anchor: return "ancora sconosciuta";
    case AccError::Origin: return "origine fuori dall'accessorio";
    case AccError::Options: return "bit di opzione sconosciuti";
    case AccError::Poses: return "bit di posa sconosciuti in hidePoses";
    case AccError::FrameBytes: return "byte per fotogramma incoerenti";
    case AccError::Id: return "id non valido";
    case AccError::Name: return "name non valido";
    case AccError::Retouch: return "ritocco non valido o oltre la fine del file";
    case AccError::Total: return "dimensione incoerente con ritocchi e fotogrammi";
  }
  return "?";
}

const char *ancErrorText(AncError e) {
  switch (e) {
    case AncError::Ok: return "ok";
    case AncError::Short: return "più corto dell'intestazione";
    case AncError::Magic: return "magia CANC assente";
    case AncError::Version: return "versione non supportata";
    case AncError::Header: return "intestazione diversa da 20 byte";
    case AncError::Length: return "lunghezza dichiarata diversa da quella reale";
    case AncError::Crc: return "checksum errato";
    case AncError::Points: return "punti per fotogramma diversi da 5";
    case AncError::Frames: return "fotogrammi diversi da 2 o 4";
    case AncError::Mask: return "bit di posa sconosciuti";
    case AncError::Size: return "dimensione incoerente con pose e fotogrammi";
    case AncError::Range: return "punto fuori dal riquadro 28×25";
  }
  return "?";
}

namespace {

uint16_t u16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

int popcount(uint16_t v) {
  int n = 0;
  for (; v; v &= v - 1) n++;
  return n;
}

bool kebab(const uint8_t *p, int n) {  // [a-z0-9]+(-[a-z0-9]+)*
  if (n < 1 || n > 24) return false;
  for (int k = 0; k < n; k++) {
    const uint8_t ch = p[k];
    if (ch == '-') {
      if (k == 0 || k == n - 1 || p[k - 1] == '-') return false;
    } else if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) {
      return false;
    }
  }
  return true;
}

// Campo di 24 byte completato con zeri: testo non vuoto, poi solo zeri.
bool field24(const uint8_t *p, char *out, bool isId) {
  int n = 0;
  while (n < 24 && p[n]) n++;
  if (n == 0) return false;
  for (int k = n; k < 24; k++)
    if (p[k]) return false;
  if (isId && !kebab(p, n)) return false;
  if (!isId)
    for (int k = 0; k < n; k++)
      if (p[k] < 0x20 || p[k] == 0x7F) return false;  // name UTF-8: niente caratteri di controllo
  memcpy(out, p, n);
  out[n] = 0;
  return true;
}

// Ritocco per l'avatar id: dx, dy e primo byte dei pixel sostitutivi (-1 = nessuno). false = nessun ritocco.
bool findRetouch(const Accessory &a, const char *id, int &dx, int &dy, int &px) {
  const size_t want = strlen(id);
  uint32_t off = kAccHeader;
  for (int k = 0; k < a.nRetouch; k++) {
    const uint8_t n = a.data[off++];
    const bool match = n == want && memcmp(a.data + off, id, n) == 0;
    off += n;
    const int8_t rdx = int8_t(a.data[off]), rdy = int8_t(a.data[off + 1]);
    const bool has = a.data[off + 2] != 0;
    off += 3;
    if (match) {
      dx = rdx;
      dy = rdy;
      px = has ? int(off) : -1;
      return true;
    }
    if (has) off += uint32_t(a.frames) * a.fb;
  }
  return false;
}

}  // namespace

AccError parseAccessory(const uint8_t *buf, size_t len, Accessory &out) {
  memset(&out, 0, sizeof out);
  if (!buf || len < size_t(kAccHeader)) return AccError::Short;
  if (memcmp(buf, "CACC", 4) != 0) return AccError::Magic;
  if (buf[4] != 1) return AccError::Version;
  if (buf[5] != kAccHeader) return AccError::Header;
  const uint16_t total = u16(buf + 6);
  if (total != len) return AccError::Length;
  if (total > kAccMaxBytes) return AccError::TooBig;
  if (u32(buf + 8) != avatarCrc32(buf + 12, len - 12)) return AccError::Crc;
  const uint8_t w = buf[12], h = buf[13], frames = buf[14], anchor = buf[15];
  if (w < 1 || w > kAccMaxW || h < 1 || h > kAccMaxH) return AccError::Size;
  if (frames != 1 && frames != 2) return AccError::Frames;
  if (anchor >= kAnchorPoints) return AccError::Anchor;
  if (buf[16] >= w || buf[17] >= h) return AccError::Origin;
  if (buf[18] >> 2) return AccError::Options;
  const uint16_t hide = u16(buf + 20);
  if (hide >> static_cast<int>(Pose::Count)) return AccError::Poses;
  const uint16_t fb = u16(buf + 22);
  if (fb != (w * h + 3) / 4) return AccError::FrameBytes;
  if (!field24(buf + 24, out.id, true)) return AccError::Id;
  if (!field24(buf + 48, out.name, false)) return AccError::Name;
  // Ritocchi: u8 lunghezza dell'id, id, i8 dx, i8 dy, u8 pixel (0/1), pixel.
  uint32_t off = kAccHeader;
  const uint8_t nr = buf[19];
  for (int k = 0; k < nr; k++) {
    if (off + 1 > total) return AccError::Retouch;
    const uint8_t n = buf[off++];
    if (n < 1 || n > 24 || off + n + 3 > total || !kebab(buf + off, n)) return AccError::Retouch;
    // id non ripetuto: si confronta con quelli già letti
    uint32_t q = kAccHeader;
    for (int j = 0; j < k; j++) {
      const uint8_t m = buf[q++];
      if (m == n && memcmp(buf + q, buf + off, n) == 0) return AccError::Retouch;
      q += m;
      const bool hp = buf[q + 2] != 0;
      q += 3;
      if (hp) q += uint32_t(frames) * fb;
    }
    off += n;
    const uint8_t has = buf[off + 2];
    off += 3;
    if (has > 1) return AccError::Retouch;
    if (has) {
      if (off + uint32_t(frames) * fb > total) return AccError::Retouch;
      off += uint32_t(frames) * fb;
    }
  }
  if (off + uint32_t(frames) * fb != total) return AccError::Total;
  out.data = buf;
  out.len = total;
  out.crc = u32(buf + 8);
  out.w = w;
  out.h = h;
  out.frames = frames;
  out.anchor = anchor;
  out.ox = buf[16];
  out.oy = buf[17];
  out.outline = buf[18] & 1;
  out.special = buf[18] & 2;
  out.hide = hide;
  out.fb = fb;
  out.nRetouch = nr;
  out.px = uint16_t(off);
  return AccError::Ok;
}

AncError parseAnchors(const uint8_t *buf, size_t len, Anchors &out) {
  memset(&out, 0, sizeof out);
  if (!buf || len < size_t(kAncHeader)) return AncError::Short;
  if (memcmp(buf, "CANC", 4) != 0) return AncError::Magic;
  if (buf[4] != 1) return AncError::Version;
  if (buf[5] != kAncHeader) return AncError::Header;
  const uint16_t total = u16(buf + 6);
  if (total != len) return AncError::Length;
  if (u32(buf + 8) != avatarCrc32(buf + 12, len - 12)) return AncError::Crc;
  if (buf[19] != kAnchorPoints) return AncError::Points;
  const uint8_t frames = buf[18];
  if (frames != 2 && frames != 4) return AncError::Frames;
  const uint16_t mask = u16(buf + 16);
  if (mask >> static_cast<int>(Pose::Count)) return AncError::Mask;
  if (size_t(kAncHeader) + size_t(popcount(mask)) * frames * kAnchorPoints * 2 != len) return AncError::Size;
  for (size_t o = kAncHeader; o < len; o += 2) {
    if (buf[o] == 0xFF && buf[o + 1] == 0xFF) continue;
    if (buf[o] >= kAvatarW || buf[o + 1] >= kAvatarH) return AncError::Range;
  }
  out.data = buf;
  out.len = total;
  out.avatarCrc = u32(buf + 12);
  out.poseMask = mask;
  out.frames = frames;
  return AncError::Ok;
}

bool anchorPoint(const Avatar &av, const Anchors *anc, Pose p, int f, int which, int &x, int &y) {
  if (!anc || !anc->frames || which < 0 || which >= kAnchorPoints) return false;
  int pi = static_cast<int>(p);
  if (pi < 0 || pi >= static_cast<int>(Pose::Count)) return false;
  if (!(av.poseMask & (1u << pi))) {  // ripiego della posa, come il disegno (solo se l'avatar ha quella di ripiego)
    const Pose fb = (p == Pose::Compact || p == Pose::Run) ? Pose::Working : (p == Pose::Tired || p == Pose::Stuffy) ? Pose::Idle : p;
    if (fb != p && (av.poseMask & (1u << static_cast<int>(fb)))) pi = static_cast<int>(fb);
  }
  if (!(anc->poseMask & (1u << pi))) return false;
  const int idx = popcount(uint16_t(anc->poseMask & ((1u << pi) - 1)));
  const int n = anc->frames, fr = ((f % n) + n) % n;
  const uint32_t o = kAncHeader + (uint32_t(idx * n + fr) * kAnchorPoints + which) * 2;
  if (o + 1 >= anc->len) return false;
  if (anc->data[o] == 0xFF && anc->data[o + 1] == 0xFF) return false;
  x = anc->data[o];
  y = anc->data[o + 1];
  return true;
}

bool avatarGrid(const Avatar &av, Pose p, int f, uint8_t *grid) {
  uint8_t fr[kCavtFrameBytes];
  if (!avatarFrameBytes(av, p, f, fr)) return false;
  for (int k = 0; k < kAvatarCells; k++) grid[k] = uint8_t((fr[k >> 2] >> (6 - 2 * (k & 3))) & 3);
  return true;
}

bool drawAccessory(uint8_t *grid, const Avatar &av, const Anchors *anc, const Accessory &acc, Pose p, int f) {
  const int pi = static_cast<int>(p);
  if (!acc.data || pi < 0 || pi >= static_cast<int>(Pose::Count) || (acc.hide & (1u << pi))) return false;
  int ax, ay;
  if (!anchorPoint(av, anc, p, f, acc.anchor, ax, ay)) return false;
  int dx = 0, dy = 0, rpx = -1;
  findRetouch(acc, av.id, dx, dy, rpx);
  const int n = acc.frames, fr = ((f % n) + n) % n;
  const uint32_t base = uint32_t(rpx >= 0 ? rpx : acc.px) + uint32_t(fr) * acc.fb;
  const int ox = ax - acc.ox + dx, oy = ay - acc.oy + dy;
  // Celle dell'accessorio dentro il riquadro: colore finale (con 'x' calcolato su quello che c'è sotto adesso).
  uint8_t fin[kAvatarCells];   // 0 = nessuna cella dell'accessorio
  bool any = false;
  memset(fin, 0, sizeof fin);
  for (int y = 0; y < acc.h; y++)
    for (int x = 0; x < acc.w; x++) {
      const int i = y * acc.w + x, sym = (acc.data[base + (i >> 2)] >> (6 - 2 * (i & 3))) & 3;
      const int gx = ox + x, gy = oy + y;
      if (!sym || gx < 0 || gy < 0 || gx >= kAvatarW || gy >= kAvatarH) continue;
      const int k = gy * kAvatarW + gx;
      fin[k] = uint8_t(sym == 3 ? (grid[k] == 1 ? 2 : 1) : sym);
      any = true;
    }
  if (acc.outline && any) {
    static const int8_t NB[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    uint8_t ring[(kAvatarCells + 7) / 8];  // bit per cella: meno stack del loop
    memset(ring, 0, sizeof ring);
    for (int k = 0; k < kAvatarCells; k++) {
      if (fin[k] != 1) continue;
      const int cx = k % kAvatarW, cy = k / kAvatarW;
      for (const auto &d : NB) {
        const int x = cx + d[0], y = cy + d[1];
        if (x < 0 || y < 0 || x >= kAvatarW || y >= kAvatarH) continue;
        const int q = y * kAvatarW + x;
        if (!fin[q] && grid[q] == 1) ring[q >> 3] |= uint8_t(1u << (q & 7));
      }
    }
    for (int k = 0; k < kAvatarCells; k++)
      if (ring[k >> 3] & (1u << (k & 7))) grid[k] = 2;
  }
  for (int k = 0; k < kAvatarCells; k++)
    if (fin[k]) grid[k] = fin[k];
  return true;
}

// ---------- predefiniti ----------
const Accessory *builtinAccessory(int i) {
  static Accessory views[16];
  static bool tried[16], ok[16];
  if (i < 0 || i >= kBuiltinAccessoryCount || i >= 16) return nullptr;
  if (!tried[i]) {
    tried[i] = true;
    const BuiltinAccessory &b = kBuiltinAccessories[i];
    ok[i] = parseAccessory(b.data, b.len, views[i]) == AccError::Ok && views[i].crc == b.crc && strcmp(views[i].id, b.id) == 0;
  }
  return ok[i] ? &views[i] : nullptr;
}

const Accessory *builtinAccessoryById(const char *id, uint32_t crc, bool hasCrc) {
  if (!id || !id[0]) return nullptr;
  for (int i = 0; i < kBuiltinAccessoryCount; i++)
    if (strcmp(kBuiltinAccessories[i].id, id) == 0 && (!hasCrc || kBuiltinAccessories[i].crc == crc)) return builtinAccessory(i);
  return nullptr;
}

const Anchors *builtinAnchorsFor(const Avatar &av) {
  static Anchors views[8];
  static bool tried[8], ok[8];
  for (int i = 0; i < kBuiltinAnchorsCount && i < 8; i++) {
    const BuiltinAnchors &b = kBuiltinAnchors[i];
    if (strcmp(b.avatar, av.id) != 0 || b.avatarCrc != av.crc) continue;
    if (!tried[i]) {
      tried[i] = true;
      ok[i] = parseAnchors(b.data, b.len, views[i]) == AncError::Ok && views[i].avatarCrc == b.avatarCrc;
    }
    return ok[i] ? &views[i] : nullptr;
  }
  return nullptr;
}
