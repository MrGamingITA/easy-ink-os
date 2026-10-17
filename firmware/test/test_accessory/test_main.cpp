// Accessori della mascotte (gfx/accessory): drawAccessory pixel per pixel contro il disegno di riferimento
// tools/avatar/lib/draw-accessory.mjs (golden.h, generato da tools/golden/accessory-golden.mjs), e parser dei
// blocchi CACC e CANC con dati rovinati.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <vector>

#include "gfx/accessory.h"
#include "gfx/accessory_builtin.h"
#include "gfx/avatar_builtin.h"
#include "golden.h"

void setUp() {}
void tearDown() {}

namespace {

std::string rows(const uint8_t *g) {
  std::string s;
  for (int k = 0; k < kAvatarCells; k++) s += ".#+"[g[k] < 3 ? g[k] : 0];
  return s;
}

// Primo punto diverso tra due griglie, per un messaggio leggibile.
std::string firstDiff(const std::string &want, const std::string &got) {
  for (size_t k = 0; k < want.size() && k < got.size(); k++)
    if (want[k] != got[k]) {
      char b[96];
      snprintf(b, sizeof b, "(%d,%d): atteso '%c', disegnato '%c'", int(k % kAvatarW), int(k / kAvatarW), want[k], got[k]);
      return b;
    }
  return "lunghezze diverse";
}

void fixCrc(std::vector<uint8_t> &b) {
  const uint32_t c = avatarCrc32(b.data() + 12, b.size() - 12);
  for (int k = 0; k < 4; k++) b[8 + k] = uint8_t(c >> (8 * k));
}

const BuiltinAccessory &builtinById(const char *id) {
  for (int i = 0; i < kBuiltinAccessoryCount; i++)
    if (!strcmp(kBuiltinAccessories[i].id, id)) return kBuiltinAccessories[i];
  TEST_FAIL_MESSAGE("accessorio del catalogo mancante");
  return kBuiltinAccessories[0];
}

}  // namespace

static void test_catalog_compiled() {
  // Gli 8 del catalogo, nell'ordine della specifica, validi e con il CRC dichiarato.
  TEST_ASSERT_EQUAL(8, kBuiltinAccessoryCount);
  for (int i = 0; i < kBuiltinAccessoryCount; i++) {
    const Accessory *a = builtinAccessory(i);
    TEST_ASSERT_NOT_NULL_MESSAGE(a, kBuiltinAccessories[i].id);
    TEST_ASSERT_EQUAL_STRING(kGoldenAccessories[i], a->id);
    TEST_ASSERT_EQUAL_HEX32(kBuiltinAccessories[i].crc, a->crc);
  }
  TEST_ASSERT_EQUAL_STRING("Berretto da mago", builtinAccessoryById("mago")->name);
  TEST_ASSERT_TRUE(builtinAccessoryById("sciarpa")->frames == 2);
  TEST_ASSERT_TRUE(builtinAccessoryById("papillon")->outline);
  TEST_ASSERT_NULL(builtinAccessoryById("cappellino", 0x12345678u, true));  // un'altra versione: non è quella compilata
  TEST_ASSERT_NOT_NULL(builtinAccessoryById("cappellino", kBuiltinAccessories[0].crc, true));
  TEST_ASSERT_NULL(builtinAccessoryById(""));
  // Ancore compilate dei 4 predefiniti, legate al CRC del loro CAVT.
  TEST_ASSERT_EQUAL(4, kBuiltinAnchorsCount);
  for (const char *id : kGoldenAvatars) {
    const Avatar *av = builtinAvatarById(id);
    TEST_ASSERT_NOT_NULL(av);
    const Anchors *n = builtinAnchorsFor(*av);
    TEST_ASSERT_NOT_NULL_MESSAGE(n, id);
    TEST_ASSERT_EQUAL_HEX32(av->crc, n->avatarCrc);
    // Lo stesso id con un altro disegno (CRC diverso): nessuna ancora, mai posizioni inventate.
    Avatar other = *av;
    other.crc ^= 1;
    TEST_ASSERT_NULL(builtinAnchorsFor(other));
  }
}

static void test_catalog_matches_reference() {
  // 8 accessori × 4 avatar × 10 pose × fotogrammi 0..3: CRC dei 700 codici e «disegnato» come il riferimento.
  int drawn = 0, changed = 0;
  const int n = sizeof kGoldenCrc / sizeof kGoldenCrc[0];
  for (int i = 0; i < n; i++) {
    const GoldenCrc &c = kGoldenCrc[i];
    const Avatar *av = builtinAvatarById(kGoldenAvatars[c.av]);
    const Accessory *acc = builtinAccessoryById(kGoldenAccessories[c.acc]);
    uint8_t g[kAvatarCells], g0[kAvatarCells];
    TEST_ASSERT_TRUE(avatarGrid(*av, Pose(c.pose), c.f, g));
    memcpy(g0, g, sizeof g);
    const bool d = drawAccessory(g, *av, builtinAnchorsFor(*av), *acc, Pose(c.pose), c.f);
    char what[96];
    snprintf(what, sizeof what, "%s su %s, posa %d, f %d", acc->id, av->id, c.pose, c.f);
    TEST_ASSERT_EQUAL_MESSAGE(c.drawn, d, what);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(c.crc, avatarCrc32(g, kAvatarCells), what);
    drawn += d;
    changed += memcmp(g, g0, sizeof g) != 0;
  }
  // Prova di presenza: il disegno c'è davvero (1 136 casi disegnati nel riferimento, e cambiano la griglia).
  TEST_ASSERT_EQUAL(1136, drawn);
  TEST_ASSERT_EQUAL(1136, changed);
  // Griglie intere (lavora f0, finito f2): punto per punto, con il primo punto diverso nel messaggio.
  for (const GoldenGrid &G : kGoldenGrids) {
    const Avatar *av = builtinAvatarById(kGoldenAvatars[G.av]);
    const Accessory *acc = builtinAccessoryById(kGoldenAccessories[G.acc]);
    uint8_t g[kAvatarCells];
    avatarGrid(*av, Pose(G.pose), G.f, g);
    drawAccessory(g, *av, builtinAnchorsFor(*av), *acc, Pose(G.pose), G.f);
    const std::string got = rows(g);
    TEST_ASSERT_TRUE_MESSAGE(got == G.rows, (std::string(acc->id) + " su " + av->id + ": " + firstDiff(G.rows, got)).c_str());
  }
}

static void test_built_cases() {
  // x sui tre fondi, contorno, pose nascoste, punto assente, avatar senza ancore, ritocchi, 2 fotogrammi, ripiego, taglio.
  Avatar mini;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(parseAvatar(kMiniCavt, sizeof kMiniCavt, mini)));
  Anchors anc[3];
  for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL(int(AncError::Ok), int(parseAnchors(kMiniAnc[i], kMiniAncLen[i], anc[i])));
  for (const GoldenCase &c : kGoldenCases) {
    Accessory a;
    TEST_ASSERT_EQUAL_MESSAGE(int(AccError::Ok), int(parseAccessory(kTestAcc[c.acc], kTestAccLen[c.acc], a)), kTestAccName[c.acc]);
    uint8_t g[kAvatarCells];
    avatarGrid(mini, Pose(c.pose), c.f, g);
    const bool d = drawAccessory(g, mini, c.anc < 0 ? nullptr : &anc[c.anc], a, Pose(c.pose), c.f);
    char what[96];
    snprintf(what, sizeof what, "%s, ancore %d, posa %d, f %d", kTestAccName[c.acc], c.anc, c.pose, c.f);
    TEST_ASSERT_EQUAL_MESSAGE(c.drawn, d, what);
    const std::string got = rows(g);
    TEST_ASSERT_TRUE_MESSAGE(got == c.rows, (std::string(what) + ": " + firstDiff(c.rows, got)).c_str());
  }
}

static void test_without_anchors_nothing() {
  // Un avatar senza ancore (avatar dell'utente vecchio): niente accessorio, griglia intatta, nessun crash.
  const Avatar *av = builtinAvatarById("gatto");
  for (int i = 0; i < kBuiltinAccessoryCount; i++)
    for (int p = 0; p < int(Pose::Count); p++) {
      uint8_t g[kAvatarCells], g0[kAvatarCells];
      avatarGrid(*av, Pose(p), 1, g);
      memcpy(g0, g, sizeof g);
      TEST_ASSERT_FALSE(drawAccessory(g, *av, nullptr, *builtinAccessory(i), Pose(p), 1));
      TEST_ASSERT_EQUAL_MEMORY(g0, g, sizeof g);
    }
  int x = -1, y = -1;
  TEST_ASSERT_FALSE(anchorPoint(*av, nullptr, Pose::Working, 0, 0, x, y));
  TEST_ASSERT_TRUE(anchorPoint(*av, builtinAnchorsFor(*av), Pose::Working, 0, 0, x, y));
  TEST_ASSERT_TRUE(x >= 0 && x < kAvatarW && y >= 0 && y < kAvatarH);
}

static void test_cacc_corrupted() {
  // Il più complicato del catalogo (occhiali: ritocchi con pixel) e quello a 2 fotogrammi (sciarpa).
  for (const char *id : {"occhiali", "sciarpa", "papillon"}) {
    const BuiltinAccessory &b = builtinById(id);
    const std::vector<uint8_t> ok(b.data, b.data + b.len);
    Accessory a;
    TEST_ASSERT_EQUAL(int(AccError::Ok), int(parseAccessory(ok.data(), ok.size(), a)));
    // Ogni byte cambiato (un bit alla volta) e ogni troncamento: rifiutato.
    for (size_t k = 0; k < ok.size(); k++)
      for (int bit = 0; bit < 8; bit++) {
        std::vector<uint8_t> v = ok;
        v[k] ^= uint8_t(1u << bit);
        TEST_ASSERT_TRUE(parseAccessory(v.data(), v.size(), a) != AccError::Ok);
      }
    for (size_t n = 0; n < ok.size(); n++) TEST_ASSERT_TRUE(parseAccessory(ok.data(), n, a) != AccError::Ok);
    TEST_ASSERT_EQUAL(int(AccError::Short), int(parseAccessory(nullptr, 0, a)));
  }
  // Campi sbagliati con il CRC giusto: ogni controllo ha il suo errore.
  const BuiltinAccessory &b = builtinById("occhiali");
  const std::vector<uint8_t> ok(b.data, b.data + b.len);
  Accessory a;
  struct Bad { int off; uint8_t val; AccError want; };
  const Bad bad[] = {
      {0, 'X', AccError::Magic},        {4, 2, AccError::Version},      {5, 71, AccError::Header},
      {12, 15, AccError::Size},         {12, 0, AccError::Size},        {13, 11, AccError::Size},
      {14, 3, AccError::Frames},        {15, 5, AccError::Anchor},      {16, 14, AccError::Origin},
      {17, 10, AccError::Origin},       {18, 4, AccError::Options},     {21, 0x04, AccError::Poses},
      {22, 34, AccError::FrameBytes},   {24, 'O', AccError::Id},        {24, 0, AccError::Id},
      {47, 'x', AccError::Id},          {48, 0, AccError::Name},        {48, 0x07, AccError::Name},
      {19, 3, AccError::Retouch},       {72, 0, AccError::Retouch},     {72, 30, AccError::Retouch},
      {73, 'D', AccError::Retouch},
  };
  for (const Bad &x : bad) {
    std::vector<uint8_t> v = ok;
    v[x.off] = x.val;
    if (x.off >= 12) fixCrc(v);
    char what[64];
    snprintf(what, sizeof what, "byte %d = %d", x.off, x.val);
    TEST_ASSERT_EQUAL_MESSAGE(int(x.want), int(parseAccessory(v.data(), v.size(), a)), what);
  }
  {  // lunghezza dichiarata diversa da quella reale
    std::vector<uint8_t> v = ok;
    v[6]++;
    TEST_ASSERT_EQUAL(int(AccError::Length), int(parseAccessory(v.data(), v.size(), a)));
  }
  {  // oltre 1 024 byte (dichiarati e reali)
    std::vector<uint8_t> v = ok;
    v.resize(1100, 0);
    v[6] = uint8_t(1100 & 0xFF);
    v[7] = uint8_t(1100 >> 8);
    fixCrc(v);
    TEST_ASSERT_EQUAL(int(AccError::TooBig), int(parseAccessory(v.data(), v.size(), a)));
  }
  {  // un byte in più in coda (con lunghezza e CRC giusti): dimensione incoerente
    std::vector<uint8_t> v = ok;
    v.push_back(0);
    v[6] = uint8_t(v.size() & 0xFF);
    v[7] = uint8_t(v.size() >> 8);
    fixCrc(v);
    TEST_ASSERT_EQUAL(int(AccError::Total), int(parseAccessory(v.data(), v.size(), a)));
  }
  {  // due ritocchi con lo stesso id: fiocco ha calamaro, demone, gatto, goccia; «goccia» diventa «demone»
    const BuiltinAccessory &f = builtinById("fiocco");
    std::vector<uint8_t> v(f.data, f.data + f.len);
    Accessory fa;
    TEST_ASSERT_EQUAL(int(AccError::Ok), int(parseAccessory(v.data(), v.size(), fa)));
    TEST_ASSERT_EQUAL(4, fa.nRetouch);
    size_t off = 72, at = 0;
    for (int k = 0; k < fa.nRetouch; k++) {
      const uint8_t len = v[off];
      if (len == 6 && !memcmp(&v[off + 1], "goccia", 6)) at = off + 1;
      off += 1 + len + 3 + (v[off + 1 + len + 2] ? size_t(fa.frames) * fa.fb : 0);
    }
    TEST_ASSERT_TRUE(at > 0);
    memcpy(&v[at], "demone", 6);
    fixCrc(v);
    TEST_ASSERT_EQUAL(int(AccError::Retouch), int(parseAccessory(v.data(), v.size(), fa)));
  }
}

static void test_canc_corrupted() {
  const BuiltinAnchors &b = kBuiltinAnchors[1];  // gatto
  const std::vector<uint8_t> ok(b.data, b.data + b.len);
  Anchors n;
  TEST_ASSERT_EQUAL(int(AncError::Ok), int(parseAnchors(ok.data(), ok.size(), n)));
  for (size_t k = 0; k < ok.size(); k++)
    for (int bit = 0; bit < 8; bit++) {
      std::vector<uint8_t> v = ok;
      v[k] ^= uint8_t(1u << bit);
      TEST_ASSERT_TRUE(parseAnchors(v.data(), v.size(), n) != AncError::Ok);
    }
  for (size_t len = 0; len < ok.size(); len++) TEST_ASSERT_TRUE(parseAnchors(ok.data(), len, n) != AncError::Ok);
  struct Bad { int off; uint8_t val; AncError want; };
  const Bad bad[] = {
      {0, 'X', AncError::Magic}, {4, 2, AncError::Version}, {5, 21, AncError::Header}, {19, 4, AncError::Points},
      {18, 3, AncError::Frames}, {17, 0x04, AncError::Mask}, {16, 0x7F, AncError::Size},
      {20, 28, AncError::Range}, {21, 25, AncError::Range}, {20, 0xFF, AncError::Range},
  };
  for (const Bad &x : bad) {
    std::vector<uint8_t> v = ok;
    v[x.off] = x.val;
    if (x.off >= 12) fixCrc(v);
    char what[64];
    snprintf(what, sizeof what, "byte %d = %d", x.off, x.val);
    TEST_ASSERT_EQUAL_MESSAGE(int(x.want), int(parseAnchors(v.data(), v.size(), n)), what);
  }
  {  // un punto assente (0xFF 0xFF) è valido e anchorPoint dice «assente»
    std::vector<uint8_t> v = ok;
    v[20] = v[21] = 0xFF;  // working[0].head-top
    fixCrc(v);
    TEST_ASSERT_EQUAL(int(AncError::Ok), int(parseAnchors(v.data(), v.size(), n)));
    int x, y;
    TEST_ASSERT_FALSE(anchorPoint(*builtinAvatarById("gatto"), &n, Pose::Working, 0, 0, x, y));
    TEST_ASSERT_TRUE(anchorPoint(*builtinAvatarById("gatto"), &n, Pose::Working, 0, 1, x, y));  // eyes c'è
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_catalog_compiled);
  RUN_TEST(test_catalog_matches_reference);
  RUN_TEST(test_built_cases);
  RUN_TEST(test_without_anchors_nothing);
  RUN_TEST(test_cacc_corrupted);
  RUN_TEST(test_canc_corrupted);
  return UNITY_END();
}
