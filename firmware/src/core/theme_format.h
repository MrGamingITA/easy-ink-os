// Temi (docs/TEMI.md §5): formato CTHM v1. Logica pura: nessuna allocazione, nessun accesso fuori dai byte passati
// (test_theme_format).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace theme {

constexpr int kHeaderLen = 72;
constexpr int kIdLen = 25;
constexpr int kNameLen = 25;
constexpr int kMaxFile = 65536;
constexpr int kMaxBlock = 4096;
constexpr int kMaxScreens = 12;
constexpr int kMaxThemes = 8;
constexpr int kMaxVariants = 8;
constexpr int kMaxElements = 64;
constexpr int kMaxConds = 8;
constexpr int kMaxSegs = 16;
constexpr int kMaxRow = 32;
constexpr int kCoordMax = 512;

enum class Screen : uint8_t { None, Home, Detail, Sleep, Offline, Riepilogo, Oggi, Batteria, Stato, Limiti, Focus, Ambiente, Pet, Count };
const char *screenName(Screen s);

enum class Type : uint8_t { None, Mascot, Text, Battery, Wifi, StateIcon, Bar, Qr, Sym, Line, Rect, Circle, List };
enum class When : uint8_t { Always, None, One, Many };
enum class Ref : uint8_t { Urgent, Selected, Row, Nobody, Variant };
enum class CondOp : uint8_t { None, Perm, BattBelow, Charging, Night, Pc, Workday, Game, SessionsMin, SessionsMax, State, Blink };

enum class Error : uint8_t {
  Ok, Short, Magic, Version, Header, Length, Crc, Size, Id, Name, Index, Block, Variant, Element, Cond, Payload, Font, Value,
  String, Utf8, Nesting
};
const char *errorText(Error e);

struct Header {
  uint32_t crc;
  uint32_t length;
  char id[kIdLen];
  char name[kNameLen];
  uint8_t nScreens;
};

struct IndexEntry {
  Screen screen;
  uint16_t len;
  uint32_t off;
};

struct Elem {
  Type type;
  bool invert;
  int16_t x, y, w, h;
  uint8_t nConds;
  const uint8_t *conds;
  uint8_t plen;
  const uint8_t *payload;
  const uint8_t *next;
};

struct Variant {
  When when;
  Ref ref;
  uint16_t off;
  uint16_t count;
};

constexpr uint16_t kValClock = 0x80;
constexpr uint16_t kValClockSec = 0x81;
constexpr uint16_t kValRow = 0x40;
bool validValue(uint16_t id);

// CRC-32 IEEE continuabile: si parte da 0.
uint32_t crc32Update(uint32_t crc, const uint8_t *p, size_t n);

Error parseHeader(const uint8_t *p, size_t n, Header &out);  // bastano i primi 72 byte
Error parseIndex(const uint8_t *idx, size_t n, const Header &h, IndexEntry *out, int cap);  // idx = 8·nScreens byte da 72
Error checkBlock(const uint8_t *b, size_t n);
Error checkFile(const uint8_t *p, size_t n, Header *h = nullptr);

// Solo su file già controllati.
bool findScreen(const uint8_t *file, size_t n, Screen s, const uint8_t *&block, uint16_t &len);
int variantCount(const uint8_t *b);
bool variantAt(const uint8_t *b, size_t n, int k, Variant &out);
bool readElem(const uint8_t *b, size_t n, const uint8_t *p, Elem &out);
bool stringAt(const uint8_t *b, size_t n, uint16_t rel, const char *&s, uint8_t &len);

}  // namespace theme
}  // namespace cal
