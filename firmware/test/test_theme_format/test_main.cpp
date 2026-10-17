// Temi (docs/TEMI.md §5): lettura e controllo di CTHM. I file veri vengono dal compilatore del servizio
// (protocol/themes/fixtures/*.cthm); i casi rovinati si costruiscono cambiando i byte e rifacendo il CRC.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

#include "core/theme_format.h"

using namespace cal::theme;

void setUp() {}
void tearDown() {}

static std::string repoRoot() {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  if (k != std::string::npos) return f.substr(0, k);
  return "../";
}

static std::vector<uint8_t> load(const char *name) {
  std::ifstream in(repoRoot() + "protocol/themes/fixtures/" + name, std::ios::binary);
  TEST_ASSERT_TRUE_MESSAGE(in.good(), name);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void reseal(std::vector<uint8_t> &f) {
  const uint32_t c = crc32Update(0, f.data() + 12, f.size() - 12);
  f[8] = c; f[9] = c >> 8; f[10] = c >> 16; f[11] = c >> 24;
}

static void test_fixtures_ok() {
  for (const char *n : {"prova.cthm", "elementi.cthm", "lista.cthm"}) {
    std::vector<uint8_t> f = load(n);
    Header h;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", errorText(checkFile(f.data(), f.size(), &h)), n);
    TEST_ASSERT_EQUAL_UINT32(f.size(), h.length);
  }
  std::vector<uint8_t> f = load("prova.cthm");
  Header h;
  checkFile(f.data(), f.size(), &h);
  TEST_ASSERT_EQUAL_STRING("prova", h.id);
  TEST_ASSERT_EQUAL_STRING("Mascotte grande", h.name);
  TEST_ASSERT_EQUAL(2, h.nScreens);
  const uint8_t *b;
  uint16_t len;
  TEST_ASSERT_TRUE(findScreen(f.data(), f.size(), Screen::Home, b, len));
  TEST_ASSERT_EQUAL(3, variantCount(b));
  Variant v;
  TEST_ASSERT_TRUE(variantAt(b, len, 0, v));
  TEST_ASSERT_EQUAL(int(When::None), int(v.when));
  TEST_ASSERT_EQUAL(4, v.count);
  TEST_ASSERT_FALSE(findScreen(f.data(), f.size(), Screen::Oggi, b, len));
}

static void test_header_rejects() {
  std::vector<uint8_t> f = load("prova.cthm");
  struct { int off; uint8_t v; Error want; } cases[] = {
      {0, 'X', Error::Magic}, {4, 2, Error::Version}, {5, 64, Error::Header}, {16, 144, Error::Size},
      {20, 'P', Error::Id}, {44, 0, Error::Name}, {68, 13, Error::Index}, {69, 1, Error::Header},
  };
  for (auto &c : cases) {
    std::vector<uint8_t> g = f;
    g[c.off] = c.v;
    reseal(g);
    TEST_ASSERT_EQUAL_STRING(errorText(c.want), errorText(checkFile(g.data(), g.size())));
  }
  std::vector<uint8_t> g = f;
  g[g.size() - 1] ^= 0x01;  // senza rifare il CRC
  TEST_ASSERT_EQUAL(int(Error::Crc), int(checkFile(g.data(), g.size())));
  TEST_ASSERT_EQUAL(int(Error::Short), int(checkFile(f.data(), 40)));
  TEST_ASSERT_EQUAL(int(Error::Length), int(checkFile(f.data(), f.size() - 1)));
}

// Ogni troncamento di ogni blocco: mai una lettura fuori (copie a misura esatta sull'heap: le letture fuori le vede ASan, env native_asan), mai Ok.
static void test_check_block_every_truncation() {
  for (const char *n : {"prova.cthm", "elementi.cthm", "lista.cthm"}) {
    std::vector<uint8_t> f = load(n);
    for (int s = 1; s < int(Screen::Count); s++) {
      const uint8_t *b;
      uint16_t len;
      if (!findScreen(f.data(), f.size(), Screen(s), b, len)) continue;
      TEST_ASSERT_EQUAL(int(Error::Ok), int(checkBlock(b, len)));
      for (uint16_t k = 0; k < len; k++) {
        std::vector<uint8_t> cut(b, b + k);  // copia esatta: fuori = fuori dal vettore
        TEST_ASSERT_NOT_EQUAL(int(Error::Ok), int(checkBlock(cut.data(), cut.size())));
      }
    }
  }
}

// Byte cambiati uno alla volta (tutti i valori su ogni posizione di un blocco): può essere Ok o no, mai un crash.
static void test_check_block_byte_flips() {
  std::vector<uint8_t> f = load("elementi.cthm");
  const uint8_t *b;
  uint16_t len;
  TEST_ASSERT_TRUE(findScreen(f.data(), f.size(), Screen::Detail, b, len));
  std::vector<uint8_t> blk(b, b + len);
  int bad = 0;
  for (size_t i = 0; i < blk.size(); i++) {
    const uint8_t keep = blk[i];
    for (int v = 0; v < 256; v += 7) {
      blk[i] = uint8_t(v);
      bad += checkBlock(blk.data(), blk.size()) != Error::Ok;
    }
    blk[i] = keep;
  }
  TEST_ASSERT_TRUE(bad > 0);
}

// Blocchi costruiti a mano: un elemento per caso.
static std::vector<uint8_t> one(const std::vector<uint8_t> &elem, const std::vector<uint8_t> &strs = {}) {
  std::vector<uint8_t> b = {1, 0, 0, 0, 0 /*always*/, 1 /*selected*/, 10, 0, 1, 0};
  b.insert(b.end(), elem.begin(), elem.end());
  const uint16_t so = uint16_t(b.size());
  b[2] = so; b[3] = so >> 8;
  b.insert(b.end(), strs.begin(), strs.end());
  return b;
}
static std::vector<uint8_t> el(uint8_t type, const std::vector<uint8_t> &pl, uint8_t flags = 0, std::vector<uint8_t> conds = {}) {
  std::vector<uint8_t> e = {type, flags, 0, 0, 0, 0, 10, 0, 10, 0, uint8_t(conds.size() / 2), uint8_t(pl.size())};
  e.insert(e.end(), conds.begin(), conds.end());
  e.insert(e.end(), pl.begin(), pl.end());
  return e;
}

static std::vector<uint8_t> cat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// Casi validi (e i limiti esatti): liste, riga.*, 64 elementi, variante vuota.
static void test_check_block_accepts() {
  auto ok = [](const std::vector<uint8_t> &b) { return int(checkBlock(b.data(), b.size())); };
  const std::vector<uint8_t> s = {4, 'c', 'i', 'a', 'o'};
  const std::vector<uint8_t> rowText = el(2, {10, 5, 0, 1, 12, 0, 1, 1, 0x40, 0});  // testo col valore riga.0
  const std::vector<uint8_t> rowMascot = el(1, {2, 0xFF, 2, 3});                    // ref = row
  // lista di 3 righe: mascot con ref row, testo riga.*, testo fisso
  std::vector<uint8_t> list = cat(el(12, {10, 0, 5, 1, 3, 0}), cat(rowMascot, cat(rowText, el(2, {10, 5, 0, 1, 12, 0, 1, 0, 0, 0}))));
  TEST_ASSERT_EQUAL(int(Error::Ok), ok(one(list, s)));
  // lista con piu' righe dello stesso tipo
  std::vector<uint8_t> five = el(12, {10, 0, 5, 1, 5, 0});
  for (int i = 0; i < 5; i++) five = cat(five, rowText);
  TEST_ASSERT_EQUAL(int(Error::Ok), ok(one(five)));
  // 64 elementi in tutto: la lista (1) + 32 righe + 31 elementi di primo livello; con uno in piu' no
  std::vector<uint8_t> big = el(12, {10, 0, 5, 1, 32, 0});
  for (int i = 0; i < 32; i++) big = cat(big, rowText);
  std::vector<uint8_t> top64 = big;
  for (int i = 0; i < 31; i++) top64 = cat(top64, el(4, {1}));
  std::vector<uint8_t> b64 = one(top64);
  b64[8] = 32;  // elementi di primo livello: lista + 31
  TEST_ASSERT_EQUAL(int(Error::Ok), ok(b64));
  std::vector<uint8_t> b65 = one(cat(top64, el(4, {1})));
  b65[8] = 33;
  TEST_ASSERT_EQUAL(int(Error::Variant), ok(b65));
  // variante senza elementi: off == so, count 0 (il §5 non lo vieta)
  std::vector<uint8_t> empty = {1, 0, 10, 0, 0, 1, 10, 0, 0, 0};
  TEST_ASSERT_EQUAL(int(Error::Ok), ok(empty));
}

static void test_check_block_rules() {
  auto ok = [](const std::vector<uint8_t> &b) { return checkBlock(b.data(), b.size()); };
  TEST_ASSERT_EQUAL(int(Error::Ok), int(ok(one(el(1, {2, 0xFF, 4, 3})))));
  TEST_ASSERT_EQUAL(int(Error::Payload), int(ok(one(el(1, {5, 0xFF, 4, 3})))));        // scala 5
  TEST_ASSERT_EQUAL(int(Error::Nesting), int(ok(one(el(1, {2, 0xFF, 2, 3})))));        // ref row fuori da una lista
  TEST_ASSERT_EQUAL(int(Error::Element), int(ok(one(el(13, {})))));                    // tipo sconosciuto
  TEST_ASSERT_EQUAL(int(Error::Element), int(ok(one(el(4, {1}, 0x02)))));              // opzione sconosciuta
  // testo: 10 px 500 Chakra, un pezzo fisso «ciao»
  const std::vector<uint8_t> s = {4, 'c', 'i', 'a', 'o'};
  TEST_ASSERT_EQUAL(int(Error::Ok), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 0, 0, 0}), s))));
  TEST_ASSERT_EQUAL(int(Error::Font), int(ok(one(el(2, {17, 7, 0, 1, 12, 0, 1, 0, 0, 0}), s))));
  TEST_ASSERT_EQUAL(int(Error::Font), int(ok(one(el(2, {10, 4, 0, 1, 12, 0, 1, 0, 0, 0}), s))));      // Chakra 400
  TEST_ASSERT_EQUAL(int(Error::Font), int(ok(one(el(2, {8, 134, 0, 1, 12, 0, 1, 0, 0, 0}), s))));     // Silkscreen 600
  TEST_ASSERT_EQUAL(int(Error::Font), int(ok(one(el(2, {10, 6 + 0x80, 0, 1, 12, 0, 1, 0, 0, 0}), s))));
  TEST_ASSERT_EQUAL(int(Error::Font), int(ok(one(el(2, {11, 7, 0, 1, 12, 0, 1, 0, 0, 0}), s))));      // Chakra 700 11: non c'è
  TEST_ASSERT_EQUAL(int(Error::Ok), int(ok(one(el(2, {8, 4 + 0x80, 0, 1, 12, 0, 1, 0, 0, 0}), s))));  // Silkscreen 400 8
  TEST_ASSERT_EQUAL(int(Error::Ok), int(ok(one(el(2, {46, 7, 0, 1, 12, 0, 1, 0, 0, 0}), s))));
  TEST_ASSERT_EQUAL(int(Error::String), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 0, 9, 0}), s))));   // fuori tabella
  TEST_ASSERT_EQUAL(int(Error::Value), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 1, 0x30, 0}), s)))); // valore 0x30
  TEST_ASSERT_EQUAL(int(Error::Nesting), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 1, 0x40, 0}), s))));// riga.* fuori
  TEST_ASSERT_EQUAL(int(Error::Utf8), int(ok(one(el(2, {10, 5, 0, 1, 12, 0, 1, 0, 0, 0}), {2, 0xC3, 0x28}))));
  TEST_ASSERT_EQUAL(int(Error::Cond), int(ok(one(el(4, {1}, 0, {12, 0})))));          // condizione 12
  TEST_ASSERT_EQUAL(int(Error::Cond), int(ok(one(el(4, {1}, 0, {2, 0})))));           // battBelow 0
  // lista con una riga che contiene un'altra lista
  std::vector<uint8_t> inner = el(12, {10, 0, 5, 1, 1, 0});
  std::vector<uint8_t> list = el(12, {20, 0, 5, 1, 1, 0});
  list.insert(list.end(), inner.begin(), inner.end());
  TEST_ASSERT_EQUAL(int(Error::Nesting), int(ok(one(list))));
  // coordinate fuori da ±512
  std::vector<uint8_t> far = el(4, {1});
  far[2] = 0x01; far[3] = 0x02;  // x = 513
  TEST_ASSERT_EQUAL(int(Error::Element), int(ok(one(far))));
}

// Indice rovinato: ogni caso deve essere rifiutato, mai Ok e mai una lettura fuori dal file.
static void test_index_rejects() {
  std::vector<uint8_t> f = load("prova.cthm");
  const int e0 = kHeaderLen, e1 = kHeaderLen + 8;  // due voci: home, poi un'altra
  auto put32 = [](std::vector<uint8_t> &g, int at, uint32_t v) { g[at] = v; g[at + 1] = v >> 8; g[at + 2] = v >> 16; g[at + 3] = v >> 24; };
  auto put16 = [](std::vector<uint8_t> &g, int at, uint16_t v) { g[at] = v; g[at + 1] = v >> 8; };
  const uint32_t off0 = f[e0 + 4] | f[e0 + 5] << 8 | f[e0 + 6] << 16 | uint32_t(f[e0 + 7]) << 24;
  const uint16_t len0 = f[e0 + 2] | f[e0 + 3] << 8;
  struct Mut { int kind; uint32_t a; };
  const Mut cases[] = {
      {0, 0xFFFFFFFFu}, {0, 0xFFFFF000u}, {0, uint32_t(f.size()) - 2},  // off enorme, off+len oltre la fine
      {0, kHeaderLen + 8 * 2 - 1},                                        // prima della fine dell'indice
      {0, off0 + 1},                                                      // buco: la prima voce non parte subito dopo l'indice
      {1, 0}, {1, 4097}, {1, 3},                                          // lunghezza 0, > 4096, < 4
      {2, uint32_t(len0)},                                                // len arriva oltre il file
      {3, 0},                                                             // seconda voce sovrapposta alla prima
      {4, 0},                                                             // codici duplicati
      {5, 0},                                                             // codici non crescenti
  };
  for (const Mut &c : cases) {
    std::vector<uint8_t> g = f;
    switch (c.kind) {
      case 0: put32(g, e0 + 4, c.a); break;
      case 1: put16(g, e0 + 2, uint16_t(c.a)); break;
      case 2: put16(g, e0 + 2, uint16_t(len0 + 0x8000)); break;
      case 3: put32(g, e1 + 4, off0); break;
      case 4: g[e1] = g[e0]; break;
      case 5: { const uint8_t t = g[e0]; g[e0] = g[e1]; g[e1] = t; break; }
    }
    reseal(g);
    TEST_ASSERT_NOT_EQUAL(int(Error::Ok), int(checkFile(g.data(), g.size())));
  }
  // parseIndex da solo, con la lunghezza che fa girare la somma
  Header h;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(parseHeader(f.data(), f.size(), h)));
  std::vector<uint8_t> g = f;
  put32(g, e0 + 4, 0xFFFFFFFFu);
  put16(g, e0 + 2, 4);
  IndexEntry out[kMaxScreens];
  TEST_ASSERT_EQUAL(int(Error::Index), int(parseIndex(g.data() + kHeaderLen, 8 * h.nScreens, h, out, kMaxScreens)));
  // findScreen su un indice rovinato non legge fuori
  const uint8_t *b;
  uint16_t len;
  TEST_ASSERT_FALSE(findScreen(g.data(), g.size(), Screen(g[e0]), b, len));
}

// 1b manda errorText in rete: un codice fuori dalla tabella non deve leggere fuori.
static void test_error_text_bounds() {
  TEST_ASSERT_EQUAL_STRING("ok", errorText(Error::Ok));
  TEST_ASSERT_EQUAL_STRING("riquadro lista annidato o valore di riga fuori da una lista", errorText(Error::Nesting));
  TEST_ASSERT_EQUAL_STRING("errore sconosciuto", errorText(Error(uint8_t(Error::Nesting) + 1)));
  TEST_ASSERT_EQUAL_STRING("errore sconosciuto", errorText(Error(255)));
}

static void test_crc_update_matches_whole() {
  const uint8_t d[] = "calamaio-tema";
  const uint32_t whole = crc32Update(0, d, 13);
  TEST_ASSERT_EQUAL_HEX32(whole, crc32Update(crc32Update(0, d, 5), d + 5, 8));
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Update(0, reinterpret_cast<const uint8_t *>("123456789"), 9));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fixtures_ok);
  RUN_TEST(test_header_rejects);
  RUN_TEST(test_check_block_every_truncation);
  RUN_TEST(test_check_block_byte_flips);
  RUN_TEST(test_check_block_rules);
  RUN_TEST(test_check_block_accepts);
  RUN_TEST(test_index_rejects);
  RUN_TEST(test_crc_update_matches_whole);
  RUN_TEST(test_error_text_bounds);
  return UNITY_END();
}
