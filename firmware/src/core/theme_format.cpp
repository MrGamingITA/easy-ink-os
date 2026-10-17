#include "core/theme_format.h"

#include <string.h>

#include "gfx/text.h"

namespace cal {
namespace theme {

namespace {

uint16_t rd16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
int16_t rds16(const uint8_t *p) { return int16_t(rd16(p)); }
uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool utf8ok(const uint8_t *s, int n) {
  for (int i = 0; i < n;) {
    const uint8_t c = s[i];
    // byte iniziale: ASCII, 2 byte (mai 0xC0 e 0xC1, sovralunghe), 3 o 4 byte
    const int k = c < 0x80 ? 0 : (c >= 0xC2 && c <= 0xDF) ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c >= 0xF0 && c <= 0xF4) ? 3 : -1;
    if (k < 0 || i + k >= n) return false;
    for (int j = 1; j <= k; j++)
      if ((s[i + j] & 0xC0) != 0x80) return false;
    i += k + 1;
  }
  return true;
}

// Campo di testo a lunghezza fissa completato con zeri: n byte, almeno uno a zero alla fine del contenuto.
bool padded(const uint8_t *p, int n, char *out, bool kebab) {
  int len = 0;
  while (len < n && p[len]) len++;
  if (len == 0 || len > 24) return false;
  for (int i = len; i < n; i++)
    if (p[i]) return false;
  if (kebab) {
    for (int i = 0; i < len; i++) {
      const char c = char(p[i]);
      const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
      if (!alnum && !(c == '-' && i > 0 && i < len - 1 && p[i - 1] != '-')) return false;
    }
  } else {
    if (!utf8ok(p, len)) return false;
    for (int i = 0; i < len; i++)
      if (p[i] < 0x20 || p[i] == 0x7F) return false;
  }
  memcpy(out, p, len);
  out[len] = 0;
  return true;
}

// Combinazione esatta di f_registry.h: hasFont() normalizza il peso, qui no (Chakra 500/600/700, Silkscreen 400/700).
bool fontExists(int size, uint8_t wp) {
  const bool pix = wp & 0x80;
  const int w = (wp & 0x7F) * 100;
  if (pix ? (w != 400 && w != 700) : (w < 500 || w > 700)) return false;
  return hasFont(size, w, pix);
}

bool coordOk(int v, bool size) { return size ? v >= 0 && v <= kCoordMax : v >= -kCoordMax && v <= kCoordMax; }

Error checkSegs(const uint8_t *b, size_t n, const uint8_t *p, int nseg, bool inRow) {
  for (int i = 0; i < nseg; i++, p += 3) {
    const uint8_t kf = p[0];
    if ((kf & 0xF0) || (kf & 3) > 1 || ((kf >> 2) & 3) > 2) return Error::Payload;
    const uint16_t arg = rd16(p + 1);
    if ((kf & 3) == 0) {
      const char *s;
      uint8_t len;
      if (!stringAt(b, n, arg, s, len)) return Error::String;
      if (!utf8ok(reinterpret_cast<const uint8_t *>(s), len)) return Error::Utf8;
    } else {
      if (!validValue(arg)) return Error::Value;
      if (arg >= kValRow && arg < kValClock && !inRow) return Error::Nesting;
    }
  }
  return Error::Ok;
}

Error checkCond(uint8_t op, uint8_t arg) {
  switch (CondOp(op)) {
    case CondOp::Perm: case CondOp::Charging: case CondOp::Night: case CondOp::Pc: case CondOp::Workday:
    case CondOp::Game: case CondOp::Blink: return arg <= 1 ? Error::Ok : Error::Cond;
    case CondOp::BattBelow: return arg >= 1 && arg <= 100 ? Error::Ok : Error::Cond;
    case CondOp::SessionsMin: case CondOp::SessionsMax: case CondOp::State: return arg <= 6 ? Error::Ok : Error::Cond;
    default: return Error::Cond;
  }
}

Error checkRef(uint8_t r, bool inRow) {
  if (r > uint8_t(Ref::Variant)) return Error::Payload;
  if (r == uint8_t(Ref::Row) && !inRow) return Error::Nesting;
  return Error::Ok;
}

// Un elemento (con le righe, se è una lista) da *pos. end: fine dell'area elementi (so), per i limiti degli elementi;
// n: lunghezza del blocco, perché le stringhe stanno dopo so. count: elementi visti finora.
Error checkElem(const uint8_t *b, size_t end, size_t n, size_t &pos, bool inRow, int &count) {
  Elem e;
  if (!readElem(b, end, b + pos, e)) return Error::Element;
  if (++count > kMaxElements) return Error::Variant;
  if (!coordOk(e.x, false) || !coordOk(e.y, false) || !coordOk(e.w, true) || !coordOk(e.h, true)) return Error::Element;
  for (int i = 0; i < e.nConds; i++) {
    const Error c = checkCond(e.conds[2 * i], e.conds[2 * i + 1]);
    if (c != Error::Ok) return c;
  }
  const uint8_t *p = e.payload;
  const int pl = e.plen;
  Error r = Error::Ok;
  switch (e.type) {
    case Type::Mascot:
      if (pl != 4 || p[0] < 1 || p[0] > 4 || (p[1] > 8 && p[1] != 0xFF) || (p[3] & ~3)) return Error::Payload;
      r = checkRef(p[2], inRow);
      break;
    case Type::Text: {
      if (pl < 7 || p[6] > kMaxSegs || pl != 7 + 3 * p[6]) return Error::Payload;
      if (p[2] > 2 || p[3] < 1 || p[3] > 8 || p[4] < 1 || p[4] > 64 || (p[5] & ~1)) return Error::Payload;
      if (!fontExists(p[0], p[1])) return Error::Font;
      r = checkSegs(b, n, p + 7, p[6], inRow);
      break;
    }
    case Type::Battery: if (pl != 2 || p[0] < 1 || p[0] > 2 || p[1] > 1) return Error::Payload; break;
    case Type::Wifi: if (pl != 1 || p[0] < 1 || p[0] > 2) return Error::Payload; break;
    case Type::StateIcon:
      if (pl != 2 || p[0] < 1 || p[0] > 3) return Error::Payload;
      r = checkRef(p[1], inRow);
      break;
    case Type::Bar: if (pl != 2 || p[0] > 5 || p[1] > 1) return Error::Payload; break;
    case Type::Qr:
      if (pl < 1 || p[0] > kMaxSegs || pl != 1 + 3 * p[0]) return Error::Payload;
      r = checkSegs(b, n, p + 1, p[0], inRow);
      break;
    case Type::Sym: if (pl != 2 || p[0] > 7 || p[1] < 1 || p[1] > 3) return Error::Payload; break;
    case Type::Line: if (pl != 2 || p[0] > 1 || p[1] > 1) return Error::Payload; break;
    case Type::Rect: if (pl != 3 || p[0] > 8 || p[1] > 1 || p[2] > 8) return Error::Payload; break;
    case Type::Circle: if (pl != 2 || p[0] > 8 || p[1] > 1) return Error::Payload; break;
    case Type::List: {
      if (inRow) return Error::Nesting;
      if (pl != 6 || p[0] < 1 || p[0] > 122 || p[1] > 60 || p[2] < 1 || p[2] > 60 || p[3] > 1 || p[4] < 1 || p[4] > kMaxRow || p[5])
        return Error::Payload;
      pos = size_t(e.next - b);
      for (int i = 0; i < p[4]; i++) {
        const Error rr = checkElem(b, end, n, pos, true, count);
        if (rr != Error::Ok) return rr;
      }
      return Error::Ok;
    }
    default: return Error::Element;
  }
  if (r != Error::Ok) return r;
  pos = size_t(e.next - b);
  return Error::Ok;
}

}  // namespace

const char *screenName(Screen s) {
  static const char *const k[] = {"", "home", "detail", "sleep", "offline", "riepilogo", "oggi", "batteria", "stato", "limiti",
                                  "focus", "ambiente", "pet"};
  return int(s) < int(Screen::Count) ? k[int(s)] : "";
}

const char *errorText(Error e) {
  static const char *const k[] = {
      "ok", "file troppo corto", "non è un tema (magia)", "versione del tema non supportata", "intestazione non valida",
      "lunghezza sbagliata", "CRC sbagliato", "tema per un altro schermo", "id non valido", "nome non valido",
      "indice delle schermate non valido", "blocco di una schermata non valido", "variante non valida", "elemento non valido",
      "condizione non valida", "dati di un elemento non validi", "carattere che la scheda non ha", "valore sconosciuto",
      "testo fuori dal blocco", "testo non UTF-8", "riquadro lista annidato o valore di riga fuori da una lista"};
  // 1b lo manda in rete (theme-ack): un codice fuori dalla tabella dà un testo generico, mai una lettura fuori.
  return size_t(e) < sizeof k / sizeof k[0] ? k[size_t(e)] : "errore sconosciuto";
}

bool validValue(uint16_t id) { return id <= 9 || (id >= kValRow && id <= kValRow + 9) || (id >= kValClock && id <= 0x95); }

uint32_t crc32Update(uint32_t crc, const uint8_t *p, size_t n) {
  crc = ~crc;
  for (size_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

Error parseHeader(const uint8_t *p, size_t n, Header &out) {
  memset(&out, 0, sizeof out);
  if (n < size_t(kHeaderLen)) return Error::Short;
  if (memcmp(p, "CTHM", 4)) return Error::Magic;
  if (p[4] != 1) return Error::Version;
  if (p[5] != kHeaderLen || p[6] || p[7] || p[69] || p[70] || p[71]) return Error::Header;
  out.crc = rd32(p + 8);
  out.length = rd32(p + 12);
  if (out.length < uint32_t(kHeaderLen) || out.length > kMaxFile) return Error::Length;
  if (rd16(p + 16) != 250 || rd16(p + 18) != 122) return Error::Size;
  if (!padded(p + 20, 24, out.id, true)) return Error::Id;
  if (!padded(p + 44, 24, out.name, false)) return Error::Name;
  out.nScreens = p[68];
  if (out.nScreens > kMaxScreens || kHeaderLen + 8u * out.nScreens > out.length) return Error::Index;
  return Error::Ok;
}

Error parseIndex(const uint8_t *idx, size_t n, const Header &h, IndexEntry *out, int cap) {
  if (n < 8u * h.nScreens || cap < h.nScreens) return Error::Index;
  uint32_t next = kHeaderLen + 8u * h.nScreens;
  int prev = 0;
  for (int i = 0; i < h.nScreens; i++) {
    const uint8_t *e = idx + 8 * i;
    const uint16_t len = rd16(e + 2);
    const uint32_t off = rd32(e + 4);
    if (e[0] <= prev || e[0] >= uint8_t(Screen::Count) || e[1] || len < 4 || len > kMaxBlock || off < next ||
        off > h.length || len > h.length - off)
      return Error::Index;
    out[i].screen = Screen(e[0]);
    out[i].len = len;
    out[i].off = off;
    prev = e[0];
    next = off + len;
  }
  return Error::Ok;
}

int variantCount(const uint8_t *b) { return b[0]; }

bool variantAt(const uint8_t *b, size_t n, int k, Variant &out) {
  if (n < 4 || k < 0 || k >= b[0] || 4u + 6u * (k + 1) > n) return false;
  const uint8_t *v = b + 4 + 6 * k;
  out.when = When(v[0]);
  out.ref = Ref(v[1]);
  out.off = rd16(v + 2);
  out.count = rd16(v + 4);
  return true;
}

bool readElem(const uint8_t *b, size_t n, const uint8_t *p, Elem &out) {
  if (p < b || size_t(p - b) + 12 > n) return false;
  out.type = Type(p[0]);
  if (p[0] < 1 || p[0] > uint8_t(Type::List) || (p[1] & ~1)) return false;
  out.invert = p[1] & 1;
  out.x = rds16(p + 2);
  out.y = rds16(p + 4);
  out.w = rds16(p + 6);
  out.h = rds16(p + 8);
  out.nConds = p[10];
  out.plen = p[11];
  if (out.nConds > kMaxConds) return false;
  const size_t sz = 12u + 2u * out.nConds + out.plen;
  if (size_t(p - b) + sz > n) return false;
  out.conds = p + 12;
  out.payload = p + 12 + 2 * out.nConds;
  out.next = p + sz;
  return true;
}

bool stringAt(const uint8_t *b, size_t n, uint16_t rel, const char *&s, uint8_t &len) {
  if (n < 4) return false;
  const size_t at = size_t(rd16(b + 2)) + rel;
  if (at + 1 > n) return false;
  len = b[at];
  if (at + 1 + len > n) return false;
  s = reinterpret_cast<const char *>(b + at + 1);
  return true;
}

Error checkBlock(const uint8_t *b, size_t n) {
  if (n < 4 || n > kMaxBlock) return Error::Block;
  const int nv = b[0];
  if (nv < 1 || nv > kMaxVariants || b[1]) return Error::Block;
  const size_t head = 4u + 6u * nv, so = rd16(b + 2);
  if (head > so || so > n) return Error::Block;
  for (int k = 0; k < nv; k++) {
    Variant v;
    if (!variantAt(b, n, k, v)) return Error::Block;
    if (uint8_t(v.when) > 3 || uint8_t(v.ref) > 1 || v.off < head || v.off > so || v.count > kMaxElements) return Error::Variant;
    size_t pos = v.off;
    int count = 0;
    for (int i = 0; i < v.count; i++) {
      const Error e = checkElem(b, so, n, pos, false, count);
      if (e != Error::Ok) return e;
    }
  }
  for (size_t at = so; at < n;) {  // tabella delle stringhe: tutta ben formata fino alla fine
    const size_t len = b[at];
    if (at + 1 + len > n) return Error::String;
    if (!utf8ok(b + at + 1, int(len))) return Error::Utf8;
    at += 1 + len;
  }
  return Error::Ok;
}

Error checkFile(const uint8_t *p, size_t n, Header *hOut) {
  Header h;
  Error e = parseHeader(p, n, h);
  if (hOut) *hOut = h;
  if (e != Error::Ok) return e;
  if (h.length != n) return Error::Length;
  if (crc32Update(0, p + 12, n - 12) != h.crc) return Error::Crc;
  IndexEntry idx[kMaxScreens];
  e = parseIndex(p + kHeaderLen, n - kHeaderLen, h, idx, kMaxScreens);
  if (e != Error::Ok) return e;
  for (int i = 0; i < h.nScreens; i++) {
    e = checkBlock(p + idx[i].off, idx[i].len);
    if (e != Error::Ok) return e;
  }
  return Error::Ok;
}

bool findScreen(const uint8_t *file, size_t n, Screen s, const uint8_t *&block, uint16_t &len) {
  if (n < size_t(kHeaderLen)) return false;
  const int ns = file[68];
  for (int i = 0; i < ns && kHeaderLen + 8u * (i + 1) <= n; i++) {
    const uint8_t *e = file + kHeaderLen + 8 * i;
    if (e[0] != uint8_t(s)) continue;
    const uint32_t off = rd32(e + 4);
    len = rd16(e + 2);
    if (off > n || len > n - off) return false;  // senza somme che girano su uint32_t
    block = file + off;
    return true;
  }
  return false;
}

}  // namespace theme
}  // namespace cal
