// Avatar sul display (docs/AVATAR.md §8 e §10): lettura e validazione del CAVT, ripiego delle pose,
// fotogrammi, cache con sostituzione del meno usato, quale avatar disegnare, avatar-need e messaggi.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/apply_state.h"
#include "core/avatars.h"
#include "gfx/avatar.h"
#include "gfx/avatar_builtin.h"

using namespace cal;

void setUp() { setAvatarCache(nullptr); }
void tearDown() { setAvatarCache(nullptr); }

namespace {

const char *const kPoses[] = {"working", "permission", "input", "done", "error", "idle", "compact", "tired", "stuffy", "run"};

std::string repoRoot() {
  if (const char *e = getenv("CALAMAIO_REPO")) return std::string(e) + "/";
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/test_avatar/");
  if (k != std::string::npos) return f.substr(0, k);
  const size_t k2 = f.find("test/test_avatar/");
  if (k2 != std::string::npos) return f.substr(0, k2) + "../";
  return "../";
}

std::vector<uint8_t> builtinBytes(const char *id) {
  for (int i = 0; i < kBuiltinAvatarCount; i++)
    if (!strcmp(kBuiltinAvatars[i].id, id)) return std::vector<uint8_t>(kBuiltinAvatars[i].data, kBuiltinAvatars[i].data + kBuiltinAvatars[i].len);
  return {};
}

void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
uint32_t crcOf(const std::vector<uint8_t> &b) { return b[8] | b[9] << 8 | b[10] << 16 | uint32_t(b[11]) << 24; }
void recrc(std::vector<uint8_t> &b) { put32(&b[8], avatarCrc32(b.data() + 12, b.size() - 12)); }

// Copia della Goccia con un altro id (e, volendo, un altro nome): CAVT valido con un crc diverso.
std::vector<uint8_t> variant(const char *id, const char *name = "Prova") {
  std::vector<uint8_t> b = builtinBytes("goccia");
  memset(&b[20], 0, 48);
  memcpy(&b[20], id, strlen(id));
  memcpy(&b[44], name, strlen(name));
  recrc(b);
  return b;
}

AvatarError parse(const std::vector<uint8_t> &b, Avatar &a) { return parseAvatar(b.data(), b.size(), a); }

std::string b64(const uint8_t *p, size_t n) {
  static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = uint32_t(p[i]) << 16 | (i + 1 < n ? p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    o += A[v >> 18 & 63];
    o += A[v >> 12 & 63];
    o += i + 1 < n ? A[v >> 6 & 63] : '=';
    o += i + 2 < n ? A[v & 63] : '=';
  }
  return o;
}

// Righe del disegno di avatars/<id>.avatar.json, nell'ordine del file (pose presenti × fotogrammi × 25).
// serialize() di tools/avatar/lib/format.mjs scrive una riga per stringa: si prendono le stringhe di 28
// simboli . # +.
std::vector<std::string> jsonRows(const char *id) {
  std::ifstream in(repoRoot() + "avatars/" + id + ".avatar.json");
  std::stringstream ss;
  ss << in.rdbuf();
  const std::string d = ss.str();
  std::vector<std::string> rows;
  for (size_t p = d.find('"'); p != std::string::npos; p = d.find('"', p + 1)) {
    const size_t e = d.find('"', p + 1);
    if (e == std::string::npos) break;
    const std::string s = d.substr(p + 1, e - p - 1);
    if (s.size() == size_t(kAvatarW) && s.find_first_not_of(".#+") == std::string::npos) rows.push_back(s);
    p = e;
  }
  return rows;
}

int inkOf(const uint8_t *fr) {
  int n = 0;
  for (int y = 0; y < kAvatarH; y++)
    for (int x = 0; x < kAvatarW; x++) n += avatarPixel(fr, x, y) == 1;
  return n;
}

}  // namespace

// ---------------------------------------------------------------- formato
static void test_builtins_valid() {
  TEST_ASSERT_EQUAL(4, kBuiltinAvatarCount);
  const char *ids[] = {"goccia", "gatto", "demone", "calamaro"};
  const char *names[] = {"Goccia", "Gatto", "Demone", "Calamaro"};
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_STRING(ids[i], kBuiltinAvatars[i].id);
    TEST_ASSERT_EQUAL(7072, kBuiltinAvatars[i].len);  // 72 + 10 × 4 × 175 (docs/AVATAR.md §8 e §12)
    const Avatar *a = builtinAvatar(i);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING(ids[i], a->id);
    TEST_ASSERT_EQUAL_STRING(names[i], a->name);
    TEST_ASSERT_EQUAL_HEX32(kBuiltinAvatars[i].crc, a->crc);
    TEST_ASSERT_EQUAL(4, a->frames);
    TEST_ASSERT_EQUAL_HEX16(0x3FF, a->poseMask);  // con run (bit 9)
  }
  // CRC della libreria con run (docs/AVATAR.md §12.3).
  TEST_ASSERT_EQUAL_HEX32(0x33072CEE, kBuiltinAvatars[0].crc);
  TEST_ASSERT_EQUAL_HEX32(0xB2432AD7, kBuiltinAvatars[1].crc);
  TEST_ASSERT_EQUAL_HEX32(0xA3130345, kBuiltinAvatars[2].crc);
  TEST_ASSERT_EQUAL_HEX32(0x2D86BCB3, kBuiltinAvatars[3].crc);
  TEST_ASSERT_EQUAL_PTR(builtinAvatar(0), &gocciaAvatar());
  TEST_ASSERT_NULL(builtinAvatarById("unicorno"));
  TEST_ASSERT_NULL(builtinAvatarById(""));
}

// I byte compilati sono il JSON della libreria: ogni pixel di ogni posa e fotogramma (codici 0/1/2).
static void test_builtins_match_library_json() {
  for (int i = 0; i < kBuiltinAvatarCount; i++) {
    const Avatar &a = *builtinAvatar(i);
    const std::vector<std::string> rows = jsonRows(a.id);
    TEST_ASSERT_EQUAL_MESSAGE(10 * 4 * kAvatarH, rows.size(), a.id);
    int r = 0;
    for (int p = 0; p < 10; p++)
      for (int f = 0; f < 4; f++) {
        uint8_t fr[kCavtFrameBytes];
        TEST_ASSERT_TRUE(avatarFrameBytes(a, Pose(p), f, fr));
        for (int y = 0; y < kAvatarH; y++, r++)
          for (int x = 0; x < kAvatarW; x++) {
            const char want = rows[r][x];
            const int v = avatarPixel(fr, x, y);
            if ((want == '.' ? 0 : want == '#' ? 1 : 2) != v) {
              char msg[96];
              snprintf(msg, sizeof msg, "%s %s[%d] (%d,%d)", a.id, kPoses[p], f, x, y);
              TEST_FAIL_MESSAGE(msg);
            }
          }
      }
  }
}

// Un CAVT rovinato non passa: ogni controllo di unpack() (docs/AVATAR.md §10) ha il suo caso.
static void test_corrupt_cavt_rejected() {
  Avatar a;
  const std::vector<uint8_t> good = builtinBytes("gatto");
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(parse(good, a)));
  struct Case { const char *what; AvatarError want; void (*edit)(std::vector<uint8_t> &); bool fixCrc; };
  const Case cases[] = {
      {"corto", AvatarError::Short, [](std::vector<uint8_t> &b) { b.resize(71); }, false},
      {"magia", AvatarError::Magic, [](std::vector<uint8_t> &b) { b[0] = 'X'; }, false},
      {"versione 2", AvatarError::Version, [](std::vector<uint8_t> &b) { b[4] = 2; }, false},
      {"intestazione 71", AvatarError::Header, [](std::vector<uint8_t> &b) { b[5] = 71; }, false},
      {"un byte in meno", AvatarError::Length, [](std::vector<uint8_t> &b) { b.pop_back(); }, false},
      {"un byte in più", AvatarError::Length, [](std::vector<uint8_t> &b) { b.push_back(0); }, false},
      {"pixel cambiato senza CRC", AvatarError::Crc, [](std::vector<uint8_t> &b) { b[500] ^= 0x40; }, false},
      {"CRC sbagliato", AvatarError::Crc, [](std::vector<uint8_t> &b) { b[8] ^= 1; }, false},
      {"w = 27", AvatarError::Frames, [](std::vector<uint8_t> &b) { b[12] = 27; }, true},
      {"3 fotogrammi", AvatarError::Frames, [](std::vector<uint8_t> &b) { b[14] = 3; }, true},
      {"riservato", AvatarError::Reserved, [](std::vector<uint8_t> &b) { b[15] = 1; }, true},
      {"manca input", AvatarError::Mask, [](std::vector<uint8_t> &b) { b[16] &= ~4; }, true},
      // bit oltre il 9 (run): rifiutati, come Unpack() del servizio e unpack() di pack.mjs (AVATAR.md §12.4)
      {"bit 10", AvatarError::Mask, [](std::vector<uint8_t> &b) { b[17] |= 4; }, true},
      {"bit 15", AvatarError::Mask, [](std::vector<uint8_t> &b) { b[17] |= 0x80; }, true},
      {"run dichiarata assente", AvatarError::Size, [](std::vector<uint8_t> &b) { b[17] &= ~2; }, true},
      {"174 byte per fotogramma", AvatarError::FrameBytes, [](std::vector<uint8_t> &b) { b[18] = 174; }, true},
      {"stuffy dichiarata assente", AvatarError::Size, [](std::vector<uint8_t> &b) { b[17] &= ~1; }, true},
      {"id maiuscolo", AvatarError::Id, [](std::vector<uint8_t> &b) { b[20] = 'G'; }, true},
      {"id vuoto", AvatarError::Id, [](std::vector<uint8_t> &b) { memset(&b[20], 0, 24); }, true},
      {"id con resto dopo lo zero", AvatarError::Id, [](std::vector<uint8_t> &b) { b[43] = 'x'; }, true},
      {"id con trattino in fondo", AvatarError::Id, [](std::vector<uint8_t> &b) { b[25] = '-'; }, true},
      {"name vuoto", AvatarError::Name, [](std::vector<uint8_t> &b) { memset(&b[44], 0, 24); }, true},
      {"codice 11", AvatarError::Pixel, [](std::vector<uint8_t> &b) { b[72 + 100] |= 0x0C; }, true},
      {"codice 11 nell'ultimo pixel", AvatarError::Pixel, [](std::vector<uint8_t> &b) { b.back() |= 0x03; }, true},
  };
  for (const Case &c : cases) {
    std::vector<uint8_t> b = good;
    c.edit(b);
    if (c.fixCrc) recrc(b);
    const AvatarError e = parse(b, a);
    char msg[120];
    snprintf(msg, sizeof msg, "%s: atteso %s, ottenuto %s", c.what, avatarErrorText(c.want), avatarErrorText(e));
    TEST_ASSERT_EQUAL_MESSAGE(int(c.want), int(e), msg);
    TEST_ASSERT_EQUAL(0, a.frames);  // niente vista a metà
  }
}

// Ripiego delle pose facoltative e f mod frames: un avatar 6 pose × 2 fotogrammi ricavato dalla Goccia.
static void test_fallback_and_frame_modulo() {
  const std::vector<uint8_t> g = builtinBytes("goccia");
  std::vector<uint8_t> b(g.begin(), g.begin() + 72);
  b[14] = 2;
  b[16] = 0x3F;
  b[17] = 0;
  for (int p = 0; p < 6; p++)
    for (int f = 0; f < 2; f++) {
      const size_t off = 72 + size_t(p * 4 + f) * kCavtFrameBytes;
      b.insert(b.end(), g.begin() + off, g.begin() + off + kCavtFrameBytes);
    }
  TEST_ASSERT_EQUAL(2172, b.size());  // 72 + 6 × 2 × 175 (docs/AVATAR.md §8)
  b[6] = b.size() & 0xFF;
  b[7] = b.size() >> 8;
  recrc(b);
  Avatar a;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(parse(b, a)));
  TEST_ASSERT_EQUAL(int(Pose::Working), int(avatarPose(a, Pose::Compact)));
  TEST_ASSERT_EQUAL(int(Pose::Idle), int(avatarPose(a, Pose::Tired)));
  TEST_ASSERT_EQUAL(int(Pose::Idle), int(avatarPose(a, Pose::Stuffy)));
  TEST_ASSERT_EQUAL(int(Pose::Working), int(avatarPose(a, Pose::Run)));  // run facoltativa → working
  TEST_ASSERT_EQUAL(int(Pose::Done), int(avatarPose(a, Pose::Done)));
  auto frame = [](const Avatar &av, Pose p, int f) {
    std::vector<uint8_t> v(kCavtFrameBytes);
    TEST_ASSERT_TRUE(avatarFrameBytes(av, p, f, v.data()));
    return v;
  };
  TEST_ASSERT_TRUE(frame(a, Pose::Compact, 1) == frame(a, Pose::Working, 1));
  TEST_ASSERT_TRUE(frame(a, Pose::Stuffy, 0) == frame(a, Pose::Idle, 0));
  TEST_ASSERT_TRUE(frame(a, Pose::Run, 1) == frame(a, Pose::Working, 1));
  TEST_ASSERT_TRUE(frame(a, Pose::Done, 3) == frame(a, Pose::Done, 1));   // 3 mod 2
  TEST_ASSERT_TRUE(frame(a, Pose::Done, 2) == frame(a, Pose::Done, 0));
  TEST_ASSERT_TRUE(frame(a, Pose::Done, -1) == frame(a, Pose::Done, 1));  // anche negativo (f + i·3 del Dettaglio a più sessioni)
  TEST_ASSERT_FALSE(frame(a, Pose::Done, 0) == frame(a, Pose::Done, 1));
  // I fotogrammi sono quelli della Goccia: done[1] della copia = done[1] dell'originale.
  TEST_ASSERT_TRUE(frame(a, Pose::Done, 1) == frame(gocciaAvatar(), Pose::Done, 1));
  // Con 4 fotogrammi: 5 mod 4 = 1.
  TEST_ASSERT_TRUE(frame(gocciaAvatar(), Pose::Idle, 5) == frame(gocciaAvatar(), Pose::Idle, 1));
}

// drawAvatar: '#' → ink, '+' → !ink, '.' → niente. Su un fondo d'inchiostro restano visibili solo gli '+'.
static void test_draw_ink_paper_transparent() {
  const Avatar &a = *builtinAvatarById("gatto");
  uint8_t fr[kCavtFrameBytes];
  avatarFrameBytes(a, Pose::Working, 0, fr);
  int ink = 0, paper = 0;
  for (int y = 0; y < kAvatarH; y++)
    for (int x = 0; x < kAvatarW; x++) {
      ink += avatarPixel(fr, x, y) == 1;
      paper += avatarPixel(fr, x, y) == 2;
    }
  TEST_ASSERT_TRUE(ink > 120 && paper > 0);
  Canvas<56, 50> c1;
  c1.clear();
  CanvasSurface<56, 50> s1(c1);
  drawAvatar(s1, a, 0, 0, 2, Pose::Working, 0);
  TEST_ASSERT_EQUAL(ink * 4, c1.countInk());  // fondo carta: solo l'inchiostro, scala 2
  Canvas<56, 50> c2;
  c2.clear(true);  // fondo tutto inchiostro
  CanvasSurface<56, 50> s2(c2);
  drawAvatar(s2, a, 0, 0, 2, Pose::Working, 0);
  TEST_ASSERT_EQUAL(56 * 50 - paper * 4, c2.countInk());  // la carta buca il fondo, il trasparente no
  Canvas<56, 50> c3;
  c3.clear();
  CanvasSurface<56, 50> s3(c3);
  drawAvatar(s3, a, 0, 0, 2, Pose::Working, 0, false);  // invertito: la carta diventa inchiostro
  TEST_ASSERT_EQUAL(paper * 4, c3.countInk());
}

// ---------------------------------------------------------------- cache
static MemAvatarStorage gMem;  // 8 × 7 072 byte: statica

static AvatarError storeBytes(AvatarCache &c, const std::vector<uint8_t> &b, int *slot = nullptr) {
  Avatar a;
  if (parse(b, a) != AvatarError::Ok) return c.store("x", 0, b.data(), b.size(), slot);
  return c.store(a.id, a.crc, b.data(), b.size(), slot);
}

static void test_cache_store_find_and_checks() {
  gMem.eraseAll();
  AvatarCache c;
  TEST_ASSERT_EQUAL(0, c.begin(&gMem));
  const std::vector<uint8_t> u = variant("unicorno", "Unicorno");
  const uint32_t crc = crcOf(u);
  // id o crc del messaggio diversi dall'intestazione: rifiutato
  TEST_ASSERT_EQUAL(int(AvatarError::Id), int(c.store("pinguino", crc, u.data(), u.size())));
  TEST_ASSERT_EQUAL(int(AvatarError::Crc), int(c.store("unicorno", crc ^ 1, u.data(), u.size())));
  // rovinato: rifiutato, niente scritto
  std::vector<uint8_t> bad = u;
  bad[300] ^= 0x10;
  const int w0 = gMem.writes;
  TEST_ASSERT_EQUAL(int(AvatarError::Crc), int(c.store("unicorno", crc, bad.data(), bad.size())));
  TEST_ASSERT_EQUAL(w0, gMem.writes);
  TEST_ASSERT_EQUAL(0, c.count());
  // valido
  int slot = -1;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.store("unicorno", crc, u.data(), u.size(), &slot)));
  TEST_ASSERT_EQUAL(0, slot);
  TEST_ASSERT_EQUAL(1, c.count());
  const Avatar *a = c.find("unicorno", crc, true);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_STRING("Unicorno", a->name);
  TEST_ASSERT_NULL(a->data);  // si legge a pezzi dalla memoria
  TEST_ASSERT_NULL(c.find("unicorno", crc ^ 1, true));
  TEST_ASSERT_EQUAL_PTR(a, c.find("unicorno", 0, false));
  // stesso avatar di nuovo: niente scritture
  const int w1 = gMem.writes;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.store("unicorno", crc, u.data(), u.size())));
  TEST_ASSERT_EQUAL(w1, gMem.writes);
  // dalla cache (lettura a pezzi) si disegna come dalla memoria
  Avatar mem;
  parse(u, mem);
  for (int p = 0; p < 9; p++) {
    uint8_t f1[kCavtFrameBytes], f2[kCavtFrameBytes];
    TEST_ASSERT_TRUE(avatarFrameBytes(*a, Pose(p), 3, f1));
    TEST_ASSERT_TRUE(avatarFrameBytes(mem, Pose(p), 3, f2));
    TEST_ASSERT_EQUAL_MEMORY(f2, f1, kCavtFrameBytes);
  }
  // scrittura fallita: non salvato
  gMem.failWrites = true;
  const std::vector<uint8_t> p2 = variant("pinguino");
  TEST_ASSERT_NOT_EQUAL(int(AvatarError::Ok), int(storeBytes(c, p2)));
  TEST_ASSERT_FALSE(c.has("pinguino", 0, false));
  gMem.failWrites = false;
}

static void test_cache_lru_replacement_and_versions() {
  gMem.eraseAll();
  AvatarCache c;
  c.begin(&gMem);
  char id[16];
  for (int i = 0; i < 8; i++) {
    snprintf(id, sizeof id, "av-%d", i);
    TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(storeBytes(c, variant(id))));
  }
  TEST_ASSERT_EQUAL(8, c.count());
  TEST_ASSERT_NOT_NULL(c.find("av-0", 0, false));  // av-0 usato: ora il meno usato è av-1
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(storeBytes(c, variant("av-8"))));
  TEST_ASSERT_EQUAL(8, c.count());
  TEST_ASSERT_TRUE(c.has("av-0", 0, false));
  TEST_ASSERT_FALSE(c.has("av-1", 0, false));
  TEST_ASSERT_TRUE(c.has("av-8", 0, false));
  // stesso id, crc nuovo (disegno aggiornato): sostituisce la versione vecchia, non un altro avatar
  const std::vector<uint8_t> v2 = variant("av-5", "Versione 2");
  const uint32_t old = c.find("av-5", 0, false)->crc;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(storeBytes(c, v2)));
  TEST_ASSERT_EQUAL(8, c.count());
  TEST_ASSERT_FALSE(c.has("av-5", old, true));
  TEST_ASSERT_TRUE(c.has("av-5", crcOf(v2), true));
  for (int i = 2; i < 9; i++) {
    snprintf(id, sizeof id, "av-%d", i);
    TEST_ASSERT_TRUE_MESSAGE(c.has(id, 0, false), id);
  }
}

// Al riavvio: stessa memoria, stessi avatar e stesso ordine d'uso (indice); i posti rovinati si cancellano.
static void test_cache_survives_reboot_and_drops_corrupt() {
  gMem.eraseAll();
  {
    AvatarCache c;
    c.begin(&gMem);
    char id[16];
    for (int i = 0; i < 9; i++) {  // av-8 sostituisce av-0
      snprintf(id, sizeof id, "av-%d", i);
      storeBytes(c, variant(id));
    }
  }
  AvatarCache c2;
  TEST_ASSERT_EQUAL(8, c2.begin(&gMem));
  TEST_ASSERT_FALSE(c2.has("av-0", 0, false));
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(storeBytes(c2, variant("av-9"))));  // il meno usato salvato: av-1
  TEST_ASSERT_FALSE(c2.has("av-1", 0, false));
  TEST_ASSERT_TRUE(c2.has("av-2", 0, false));
  // un posto rovinato in flash (scrittura interrotta): alla rilettura sparisce
  int bad = -1;
  for (int i = 0; i < AvatarCache::kSlots; i++)
    if (c2.slot(i) && !strcmp(c2.slot(i)->id, "av-4")) bad = i;
  TEST_ASSERT_TRUE(bad >= 0);
  gMem.raw(bad)[1000] ^= 0xFF;
  AvatarCache c3;
  TEST_ASSERT_EQUAL(7, c3.begin(&gMem));
  TEST_ASSERT_FALSE(c3.has("av-4", 0, false));
  TEST_ASSERT_EQUAL(-1, gMem.slotSize(bad));
  // Ripristino di fabbrica
  c3.clear();
  TEST_ASSERT_EQUAL(0, c3.count());
  AvatarCache c4;
  TEST_ASSERT_EQUAL(0, c4.begin(&gMem));
}

// ---------------------------------------------------------------- quale avatar
static void test_resolution_session_default_goccia() {
  Model m;
  newModel(m, 1000000, WallClock{});
  const Avatar &goccia = gocciaAvatar();
  TEST_ASSERT_EQUAL_STRING("gatto", sessionAvatar(m, m.sessions[0]).id);     // api-server
  TEST_ASSERT_EQUAL_STRING("demone", sessionAvatar(m, m.sessions[1]).id);     // web-app
  TEST_ASSERT_EQUAL_STRING("calamaro", sessionAvatar(m, m.sessions[2]).id);  // firmware-esp32
  TEST_ASSERT_EQUAL_PTR(&goccia, &sessionAvatar(m, m.sessions[3]));          // docs-site: il predefinito
  setAvatarRef(m.set.defAvatar, "demone");
  TEST_ASSERT_EQUAL_STRING("demone", sessionAvatar(m, m.sessions[3]).id);
  TEST_ASSERT_EQUAL_STRING("demone", defaultAvatar(m).id);
  setAvatarRef(m.set.defAvatar, "pinguino");  // predefinito non ancora arrivato: Goccia
  TEST_ASSERT_EQUAL_PTR(&goccia, &defaultAvatar(m));
  TEST_ASSERT_EQUAL_PTR(&goccia, &sessionAvatar(m, m.sessions[3]));
  setAvatarRef(m.sessions[0].avatar, "unicorno");  // avatar della sessione mancante: Goccia, non il predefinito
  setAvatarRef(m.set.defAvatar, "demone");
  TEST_ASSERT_EQUAL_PTR(&goccia, &sessionAvatar(m, m.sessions[0]));
  setAvatarRef(m.sessions[0].avatar, "-");  // id non valido dal servizio
  TEST_ASSERT_EQUAL_PTR(&goccia, &sessionAvatar(m, m.sessions[0]));
  // gatto della libreria dell'utente (crc diverso dal predefinito): Goccia finché non arriva, poi quello
  std::vector<uint8_t> g = builtinBytes("gatto");
  memcpy(&g[44], "Gatto mio", 9);
  recrc(g);
  setAvatarRef(m.sessions[0].avatar, "gatto");
  m.sessions[0].avatar.crc = crcOf(g);
  m.sessions[0].avatar.hasCrc = true;
  TEST_ASSERT_EQUAL_PTR(&goccia, &sessionAvatar(m, m.sessions[0]));
  gMem.eraseAll();
  AvatarCache c;
  c.begin(&gMem);
  setAvatarCache(&c);
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(storeBytes(c, g)));
  TEST_ASSERT_EQUAL_STRING("Gatto mio", sessionAvatar(m, m.sessions[0]).name);
  m.sessions[0].avatar.crc = kBuiltinAvatars[1].crc;  // crc del predefinito: quello compilato
  TEST_ASSERT_EQUAL_PTR(builtinAvatarById("gatto"), &sessionAvatar(m, m.sessions[0]));
  TEST_ASSERT_TRUE(avatarWanted(m, "gatto"));
  TEST_ASSERT_TRUE(avatarWanted(m, "demone"));
  TEST_ASSERT_FALSE(avatarWanted(m, "unicorno"));
}

// ---------------------------------------------------------------- avatar-need
static void test_needs_once_then_every_30_s() {
  Model m;
  newModel(m, 0, WallClock{});
  setAvatarRef(m.sessions[0].avatar, "unicorno");
  setAvatarRef(m.sessions[1].avatar, "unicorno");  // due sessioni, una richiesta
  setAvatarRef(m.sessions[2].avatar, "Non Valido");
  AvatarNeeds n;
  char ids[AvatarNeeds::kCap][kAvatarRefLen];
  const Ms t = 1000000;
  TEST_ASSERT_EQUAL(1, n.due(m, t, ids, AvatarNeeds::kCap));
  TEST_ASSERT_EQUAL_STRING("unicorno", ids[0]);
  TEST_ASSERT_EQUAL(0, n.due(m, t + 1, ids, AvatarNeeds::kCap));          // una volta sola…
  TEST_ASSERT_EQUAL(0, n.due(m, t + 29999, ids, AvatarNeeds::kCap));
  TEST_ASSERT_EQUAL(1, n.due(m, t + 30000, ids, AvatarNeeds::kCap));      // …poi dopo 30 s
  setAvatarRef(m.set.defAvatar, "pinguino");                             // predefinito nuovo: subito
  TEST_ASSERT_EQUAL(1, n.due(m, t + 30001, ids, AvatarNeeds::kCap));
  TEST_ASSERT_EQUAL_STRING("pinguino", ids[0]);
  TEST_ASSERT_EQUAL(2, n.pending());
  // arriva l'unicorno: non si chiede più
  gMem.eraseAll();
  AvatarCache c;
  c.begin(&gMem);
  setAvatarCache(&c);
  storeBytes(c, variant("unicorno"));
  TEST_ASSERT_EQUAL(1, n.due(m, t + 70000, ids, AvatarNeeds::kCap));
  TEST_ASSERT_EQUAL_STRING("pinguino", ids[0]);
  TEST_ASSERT_EQUAL(1, n.pending());
  // la sessione non lo vuole più: niente
  setAvatarRef(m.set.defAvatar, "goccia");
  TEST_ASSERT_EQUAL(0, n.due(m, t + 200000, ids, AvatarNeeds::kCap));
  TEST_ASSERT_EQUAL(0, n.pending());
  // connessione nuova: subito
  setAvatarRef(m.set.defAvatar, "pinguino");
  TEST_ASSERT_EQUAL(1, n.due(m, t + 200001, ids, AvatarNeeds::kCap));
  TEST_ASSERT_EQUAL(0, n.due(m, t + 200002, ids, AvatarNeeds::kCap));
  n.reset();
  TEST_ASSERT_EQUAL(1, n.due(m, t + 200003, ids, AvatarNeeds::kCap));
}

static void test_needs_rejected_and_crc() {
  Model m;
  newModel(m, 0, WallClock{});
  setAvatarRef(m.sessions[0].avatar, "gatto");  // predefinito con un crc diverso: serve quello del servizio
  m.sessions[0].avatar.crc = 0x12345678;
  m.sessions[0].avatar.hasCrc = true;
  AvatarNeeds n;
  char ids[AvatarNeeds::kCap][kAvatarRefLen];
  TEST_ASSERT_EQUAL(1, n.due(m, 0, ids, AvatarNeeds::kCap));
  TEST_ASSERT_EQUAL_STRING("gatto", ids[0]);
  n.rejected("gatto", 0x12345678);  // il servizio ha mandato un CAVT rovinato
  TEST_ASSERT_EQUAL(0, n.due(m, 60000, ids, AvatarNeeds::kCap));
  m.sessions[0].avatar.crc = 0x9ABCDEF0;  // nuova versione sul PC: si chiede
  TEST_ASSERT_EQUAL(1, n.due(m, 60001, ids, AvatarNeeds::kCap));
  m.sessions[0].avatar.crc = kBuiltinAvatars[1].crc;  // quello compilato: niente da chiedere
  TEST_ASSERT_EQUAL(0, n.due(m, 200000, ids, AvatarNeeds::kCap));
}

// ---------------------------------------------------------------- messaggi
static void test_state_and_welcome_avatar_fields() {
  const char *js =
      "{\"t\":\"state\",\"v\":1,\"time\":1791707400000,\"defaultAvatar\":\"demone\",\"defaultAvatarCrc\":2735932229,"
      "\"sessions\":[{\"id\":\"a\",\"name\":\"api\",\"state\":\"working\",\"since\":1791707400000,\"avatar\":\"gatto\",\"avatarCrc\":2990746327},"
      "{\"id\":\"b\",\"name\":\"web\",\"state\":\"done\",\"since\":1791707400000},"
      "{\"id\":\"c\",\"name\":\"x\",\"state\":\"idle\",\"since\":1791707400000,\"avatar\":\"Non-Valido\"}]}";
  JsonDocument doc;
  TEST_ASSERT_TRUE(parseMessage(js, strlen(js), doc));
  static StateMsg msg;
  TEST_ASSERT_TRUE(decodeState(doc.as<JsonVariantConst>(), msg));
  TEST_ASSERT_TRUE(msg.hasDefAvatar);
  TEST_ASSERT_EQUAL_STRING("demone", msg.defAvatar.id);
  TEST_ASSERT_TRUE(msg.defAvatar.hasCrc);
  TEST_ASSERT_EQUAL_HEX32(0xA3130345, msg.defAvatar.crc);
  TEST_ASSERT_EQUAL_STRING("gatto", msg.sessions[0].avatar.id);
  TEST_ASSERT_EQUAL_HEX32(0xB2432AD7, msg.sessions[0].avatar.crc);
  TEST_ASSERT_EQUAL_STRING("", msg.sessions[1].avatar.id);  // senza avatar: il predefinito
  TEST_ASSERT_FALSE(msg.sessions[1].avatar.hasCrc);
  TEST_ASSERT_EQUAL_STRING("-", msg.sessions[2].avatar.id);  // non valido: Goccia, non si chiede
  static Model m;
  newModel(m, 0, WallClock{});
  applyState(m, msg, 5000);
  TEST_ASSERT_EQUAL_STRING("demone", m.set.defAvatar.id);
  TEST_ASSERT_EQUAL_STRING("gatto", sessionAvatar(m, m.sessions[0]).id);  // crc del predefinito compilato
  TEST_ASSERT_EQUAL_STRING("demone", sessionAvatar(m, m.sessions[1]).id);
  TEST_ASSERT_EQUAL_PTR(&gocciaAvatar(), &sessionAvatar(m, m.sessions[2]));
  // state senza defaultAvatar: il predefinito resta quello di prima
  const char *js2 = "{\"t\":\"state\",\"v\":1,\"time\":1791707400000,\"sessions\":[]}";
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(js2, strlen(js2), doc));
  TEST_ASSERT_TRUE(decodeState(doc.as<JsonVariantConst>(), msg));
  TEST_ASSERT_FALSE(msg.hasDefAvatar);
  applyState(m, msg, 6000);
  TEST_ASSERT_EQUAL_STRING("demone", m.set.defAvatar.id);
  // welcome
  const char *jw = "{\"t\":\"welcome\",\"v\":1,\"time\":1791707400000,\"defaultAvatar\":\"calamaro\"}";
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(jw, strlen(jw), doc));
  WelcomeMsg w;
  TEST_ASSERT_TRUE(decodeWelcome(doc.as<JsonVariantConst>(), w));
  TEST_ASSERT_TRUE(w.hasDefAvatar);
  TEST_ASSERT_EQUAL_STRING("calamaro", w.defAvatar.id);
  TEST_ASSERT_FALSE(w.defAvatar.hasCrc);
  const char *jw2 = "{\"t\":\"welcome\",\"v\":1,\"time\":1791707400000}";
  doc.clear();
  TEST_ASSERT_TRUE(parseMessage(jw2, strlen(jw2), doc));
  TEST_ASSERT_TRUE(decodeWelcome(doc.as<JsonVariantConst>(), w));
  TEST_ASSERT_FALSE(w.hasDefAvatar);
}

static void test_avatar_message_roundtrip() {
  const std::vector<uint8_t> u = variant("unicorno", "Unicorno");
  const uint32_t crc = crcOf(u);
  const std::string data = b64(u.data(), u.size());
  TEST_ASSERT_EQUAL(9432, data.size());  // 7 072 byte in base64 (docs/AVATAR.md §12.4)
  char head[96];
  snprintf(head, sizeof head, "{\"t\":\"avatar\",\"v\":1,\"id\":\"unicorno\",\"crc\":%lu,\"data\":\"", (unsigned long)crc);
  const std::string js = std::string(head) + data + "\"}";
  TEST_ASSERT_TRUE(js.size() < kMaxFrame);
  JsonDocument doc;
  TEST_ASSERT_TRUE(parseMessage(js.c_str(), js.size(), doc));  // il filtro lascia passare id, crc, data
  static AvatarMsg msg;
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Ok), int(decodeAvatarMsg(doc.as<JsonVariantConst>(), msg)));
  TEST_ASSERT_EQUAL_STRING("unicorno", msg.id);
  TEST_ASSERT_EQUAL_HEX32(crc, msg.crc);
  TEST_ASSERT_EQUAL(7072, msg.len);
  TEST_ASSERT_EQUAL_MEMORY(u.data(), msg.data, u.size());
  gMem.eraseAll();
  AvatarCache c;
  c.begin(&gMem);
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.store(msg.id, msg.crc, msg.data, msg.len)));
  // messaggi sbagliati
  auto dec = [&](const std::string &s) {
    JsonDocument d;
    if (!parseMessage(s.c_str(), s.size(), d)) return AvatarMsgError::Type;
    return decodeAvatarMsg(d.as<JsonVariantConst>(), msg);
  };
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Id), int(dec("{\"t\":\"avatar\",\"v\":1,\"id\":\"Uni\",\"crc\":1,\"data\":\"QUJD\"}")));
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Crc), int(dec("{\"t\":\"avatar\",\"v\":1,\"id\":\"uni\",\"data\":\"QUJD\"}")));
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Crc), int(dec("{\"t\":\"avatar\",\"v\":1,\"id\":\"uni\",\"crc\":-1,\"data\":\"QUJD\"}")));
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Data), int(dec("{\"t\":\"avatar\",\"v\":1,\"id\":\"uni\",\"crc\":1,\"data\":\"QU*D\"}")));
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Data), int(dec("{\"t\":\"avatar\",\"v\":1,\"id\":\"uni\",\"crc\":1}")));
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Type), int(dec("{\"t\":\"avatar\",\"v\":2,\"id\":\"uni\",\"crc\":1,\"data\":\"QUJD\"}")));
  // oltre 7 072 byte: scartato prima della validazione
  std::vector<uint8_t> big(kCavtMaxBytes + 3, 'A');
  const std::string jb = "{\"t\":\"avatar\",\"v\":1,\"id\":\"uni\",\"crc\":1,\"data\":\"" + b64(big.data(), big.size()) + "\"}";
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Data), int(dec(jb)));
  // CAVT rovinato in un messaggio ben formato: lo rifiuta la cache
  std::vector<uint8_t> bad = u;
  bad[2000] ^= 1;
  const std::string jbad = std::string(head) + b64(bad.data(), bad.size()) + "\"}";
  TEST_ASSERT_EQUAL(int(AvatarMsgError::Ok), int(dec(jbad)));
  TEST_ASSERT_EQUAL(int(AvatarError::Crc), int(c.store(msg.id, msg.crc, msg.data, msg.len)));
}

static void test_avatar_need_encoding_and_base64() {
  char ids[2][kAvatarRefLen] = {"unicorno", "pinguino"};
  char out[128];
  const size_t n = encodeAvatarNeed(ids, 2, out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("{\"t\":\"avatar-need\",\"v\":1,\"ids\":[\"unicorno\",\"pinguino\"]}", out);
  TEST_ASSERT_EQUAL(strlen(out), n);
  TEST_ASSERT_EQUAL(0, encodeAvatarNeed(ids, 2, out, 20));  // non sta: niente
  uint8_t b[8];
  TEST_ASSERT_EQUAL(3, base64Decode("TWFu", 4, b, sizeof b));
  TEST_ASSERT_EQUAL_MEMORY("Man", b, 3);
  TEST_ASSERT_EQUAL(2, base64Decode("TWE=", 4, b, sizeof b));
  TEST_ASSERT_EQUAL_MEMORY("Ma", b, 2);
  TEST_ASSERT_EQUAL(1, base64Decode("TQ==", 4, b, sizeof b));
  TEST_ASSERT_EQUAL(0, base64Decode("TQ=A", 4, b, sizeof b));
  TEST_ASSERT_EQUAL(0, base64Decode("TQ==TWFu", 8, b, sizeof b));  // riempimento in mezzo
  TEST_ASSERT_EQUAL(0, base64Decode("TWF", 3, b, sizeof b));
  TEST_ASSERT_EQUAL(0, base64Decode("TR==", 4, b, sizeof b));  // bit di troppo
  TEST_ASSERT_EQUAL(0, base64Decode("TWFuTWFu", 8, b, 5));     // non sta in cap
}

// Un CAVT di prima (9 pose, 6 372 byte, senza run): resta valido; run ripiega su working. I fotogrammi delle
// pose 0-8 hanno gli stessi byte e posizioni del CAVT con run (AVATAR.md §12.4).
static void test_cavt_without_run_still_valid() {
  std::vector<uint8_t> b = builtinBytes("demone");
  b.resize(72 + 9 * 4 * kCavtFrameBytes);
  TEST_ASSERT_EQUAL(6372, b.size());
  b[6] = b.size() & 0xFF;
  b[7] = b.size() >> 8;
  b[17] &= ~2;
  recrc(b);
  Avatar a;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(parse(b, a)));
  TEST_ASSERT_EQUAL_HEX16(0x1FF, a.poseMask);
  TEST_ASSERT_EQUAL_HEX32(0xC6E4FA9B, a.crc);  // il CRC «senza run» di AVATAR.md §12.3
  TEST_ASSERT_EQUAL(int(Pose::Working), int(avatarPose(a, Pose::Run)));
  const Avatar &full = *builtinAvatarById("demone");
  uint8_t x[kCavtFrameBytes], y[kCavtFrameBytes];
  for (int p = 0; p < 9; p++)
    for (int f = 0; f < 4; f++) {
      avatarFrameBytes(a, Pose(p), f, x);
      avatarFrameBytes(full, Pose(p), f, y);
      TEST_ASSERT_EQUAL_MEMORY(y, x, kCavtFrameBytes);
    }
  avatarFrameBytes(full, Pose::Run, 0, x);  // e la corsa c'è davvero nel predefinito: run ≠ working
  avatarFrameBytes(full, Pose::Working, 0, y);
  TEST_ASSERT_TRUE(memcmp(x, y, kCavtFrameBytes) != 0);
}

// AVATAR.md §12.1: in run la colonna 27 (≥ 3 pixel d'inchiostro) e la riga 24 sono occupate in ogni fotogramma.
static void test_run_edges() {
  for (int i = 0; i < kBuiltinAvatarCount; i++) {
    const Avatar &a = *builtinAvatar(i);
    for (int f = 0; f < a.frames; f++) {
      uint8_t fr[kCavtFrameBytes];
      TEST_ASSERT_TRUE(avatarFrameBytes(a, Pose::Run, f, fr));
      int col = 0, row = 0;
      for (int y = 0; y < kAvatarH; y++) col += avatarPixel(fr, kAvatarW - 1, y) == 1;
      for (int x = 0; x < kAvatarW; x++) row += avatarPixel(fr, x, kAvatarH - 1) != 0;
      TEST_ASSERT_TRUE_MESSAGE(col >= 3, a.id);
      TEST_ASSERT_TRUE_MESSAGE(row >= 1, a.id);
    }
  }
}

// ---------- 0.14.0 (R4): avatar a pezzi ----------

// Il CAVT dato a pezzi a CavtStream dà lo stesso esito di parseAvatar sugli stessi byte, con ogni misura dei pezzi,
// anche rovinato in qualunque punto, accorciato o allungato.
static void streamSame(const std::vector<uint8_t> &b, size_t chunk, const char *what) {
  Avatar a, s;
  const AvatarError want = parse(b, a);
  CavtStream cs;
  for (size_t i = 0; i < b.size(); i += chunk) cs.feed(b.data() + i, b.size() - i < chunk ? b.size() - i : chunk);
  const AvatarError got = cs.finish(s);
  char msg[96];
  snprintf(msg, sizeof msg, "%s (pezzi da %u)", what, unsigned(chunk));
  TEST_ASSERT_EQUAL_MESSAGE(int(want), int(got), msg);
  if (want != AvatarError::Ok) return;
  TEST_ASSERT_NULL(s.data);
  TEST_ASSERT_EQUAL_STRING(a.id, s.id);
  TEST_ASSERT_EQUAL_STRING(a.name, s.name);
  TEST_ASSERT_EQUAL(a.len, s.len);
  TEST_ASSERT_EQUAL(a.poseMask, s.poseMask);
  TEST_ASSERT_EQUAL(a.frames, s.frames);
  TEST_ASSERT_EQUAL(a.header, s.header);
  TEST_ASSERT_EQUAL_UINT32(a.crc, s.crc);
}

static void test_cavt_stream_matches_parse() {
  const size_t chunks[] = {1, 7, 12, 72, 175, 512, 8000};
  for (int i = 0; i < kBuiltinAvatarCount; i++) {
    const std::vector<uint8_t> b = builtinBytes(kBuiltinAvatars[i].id);
    for (size_t c : chunks) streamSame(b, c, kBuiltinAvatars[i].id);
    for (size_t k = 0; k < b.size(); k += 37) {  // un bit cambiato ogni 37 byte: intestazione, CRC, pixel
      std::vector<uint8_t> x = b;
      x[k] ^= 0x40;
      streamSame(x, 512, "rovinato");
      recrc(x);  // CRC giusto: restano gli altri controlli (pixel 11, campi)
      streamSame(x, 175, "rovinato col CRC giusto");
    }
    for (size_t n : {size_t(0), size_t(11), size_t(71), size_t(72), b.size() - 1})
      streamSame(std::vector<uint8_t>(b.begin(), b.begin() + n), 64, "corto");
    std::vector<uint8_t> longer = b;
    longer.push_back(0);
    streamSame(longer, 512, "lungo");
  }
}

static AvatarOffer offerOf(const std::vector<uint8_t> &b, const char *id) {
  AvatarOffer o;
  memset(&o, 0, sizeof o);
  snprintf(o.id, sizeof o.id, "%s", id);
  o.crc = crcOf(b);
  o.size = uint32_t(b.size());
  snprintf(o.token, sizeof o.token, "0123456789abcdef0123456789abcdef");
  return o;
}

// Scarica b a pezzi da chunk byte: true e hdr se tutto va; altrimenti why.
static bool download(MemAvatarStorage &st, const AvatarOffer &o, const std::vector<uint8_t> &b, size_t chunk, Avatar &hdr,
                     char *why, size_t cap) {
  AvatarDownload d;
  why[0] = 0;
  bool ok = d.begin(st, o, why, cap);
  for (size_t i = 0; ok && i < b.size(); i += chunk) ok = d.write(b.data() + i, b.size() - i < chunk ? b.size() - i : chunk, why, cap);
  ok = ok && d.finish(hdr, why, cap);
  if (!ok) d.abort();
  return ok;
}

// Il Demone (10 pose, 7 072 byte) scaricato a pezzi da 512 entra nella cache e si disegna come quello compilato.
static void test_download_then_adopt() {
  static MemAvatarStorage st;
  st.eraseAll();
  AvatarCache c;
  c.begin(&st);
  const std::vector<uint8_t> b = builtinBytes("demone");
  TEST_ASSERT_EQUAL(7072, b.size());
  Avatar hdr;
  char why[96];
  TEST_ASSERT_TRUE_MESSAGE(download(st, offerOf(b, "demone"), b, 512, hdr, why, sizeof why), why);
  int slot = -1;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.adopt(hdr, &slot)));
  TEST_ASSERT_TRUE(slot >= 0);
  TEST_ASSERT_EQUAL(-1, st.tempLen());  // il temporaneo è diventato il posto
  const Avatar *a = c.find("demone", crcOf(b), true);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NULL(a->data);  // si legge a pezzi dalla memoria
  const Avatar *ref = builtinAvatarById("demone");
  uint8_t f1[kCavtFrameBytes], f2[kCavtFrameBytes];
  for (int p = 0; p < int(Pose::Count); p++)
    for (int f = 0; f < 4; f++) {
      TEST_ASSERT_TRUE(avatarFrameBytes(*a, Pose(p), f, f1));
      TEST_ASSERT_TRUE(avatarFrameBytes(*ref, Pose(p), f, f2));
      TEST_ASSERT_EQUAL_MEMORY(f2, f1, kCavtFrameBytes);
    }
  // al riavvio la cache lo rivalida dalla memoria come quelli arrivati con `avatar`
  AvatarCache again;
  TEST_ASSERT_EQUAL(1, again.begin(&st));
  TEST_ASSERT_TRUE(again.has("demone", crcOf(b), true));
  // già in cache: adopt non scrive niente e butta il temporaneo
  TEST_ASSERT_TRUE(download(st, offerOf(b, "demone"), b, 175, hdr, why, sizeof why));
  const int w = st.writes;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.adopt(hdr)));
  TEST_ASSERT_EQUAL(w, st.writes);
  TEST_ASSERT_EQUAL(-1, st.tempLen());
}

// Ogni errore ha il suo motivo per avatar-ack e la cache resta com'era.
static void test_download_rejects() {
  static MemAvatarStorage st;
  st.eraseAll();
  AvatarCache c;
  c.begin(&st);
  const std::vector<uint8_t> b = builtinBytes("gatto");
  Avatar hdr;
  char why[96];
  AvatarOffer o = offerOf(b, "gatto");
  o.crc ^= 1;
  TEST_ASSERT_FALSE(download(st, o, b, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("CRC diverso da quello offerto", why);
  o = offerOf(b, "demone");
  TEST_ASSERT_FALSE(download(st, o, b, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("l'avatar non è quello offerto", why);
  o = offerOf(b, "gatto");
  o.size += 1;
  TEST_ASSERT_FALSE(download(st, o, b, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("download incompleto", why);
  o = offerOf(b, "gatto");
  std::vector<uint8_t> longer = b;
  longer.push_back(0);
  TEST_ASSERT_FALSE(download(st, o, longer, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("più byte di quelli offerti", why);
  std::vector<uint8_t> bad = b;
  bad[500] ^= 0x40;
  TEST_ASSERT_FALSE(download(st, o, bad, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING(avatarErrorText(AvatarError::Crc), why);
  st.failWrites = true;
  TEST_ASSERT_FALSE(download(st, o, b, 512, hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("spazio per gli avatar non disponibile", why);
  st.failWrites = false;
  TEST_ASSERT_EQUAL(0, c.count());
  TEST_ASSERT_EQUAL(-1, st.tempLen());  // abort butta il temporaneo
  // rinomina fallita nella cache: «non salvato», posto libero
  TEST_ASSERT_TRUE(download(st, o, b, 512, hdr, why, sizeof why));
  st.failWrites = true;
  TEST_ASSERT_EQUAL(int(AvatarError::Short), int(c.adopt(hdr)));
  st.failWrites = false;
  TEST_ASSERT_EQUAL(0, c.count());
}

static void test_avatar_offer_and_ack_messages() {
  JsonDocument doc;
  const std::string js = [] {
    std::ifstream in(repoRoot() + "protocol/examples/avatar-offer.json");
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }();
  TEST_ASSERT_TRUE_MESSAGE(js.size() > 50, "manca protocol/examples/avatar-offer.json");
  TEST_ASSERT_TRUE(parseMessage(js.data(), js.size(), doc));
  AvatarOffer o;
  TEST_ASSERT_TRUE(decodeAvatarOffer(doc.as<JsonVariantConst>(), o));
  TEST_ASSERT_EQUAL_STRING("demone", o.id);
  TEST_ASSERT_EQUAL_UINT32(0xA3130345u, o.crc);
  TEST_ASSERT_EQUAL_UINT32(7072, o.size);
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", o.token);
  const char *bad[] = {
      R"({"t":"avatar-offer","v":1,"id":"Demone","crc":1,"size":7072,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"avatar-offer","v":1,"id":"demone","crc":1,"size":7073,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"avatar-offer","v":1,"id":"demone","crc":1,"size":2171,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"avatar-offer","v":1,"id":"demone","crc":1,"size":7072,"token":"0123456789ABCDEF0123456789abcdef"})",
      R"({"t":"avatar-offer","v":1,"id":"demone","size":7072,"token":"0123456789abcdef0123456789abcdef"})",
      R"({"t":"theme-offer","v":1,"id":"demone","crc":1,"size":7072,"token":"0123456789abcdef0123456789abcdef"})",
  };
  for (const char *b : bad) {
    doc.clear();
    TEST_ASSERT_TRUE(parseMessage(b, strlen(b), doc));
    AvatarOffer k = o;
    TEST_ASSERT_FALSE_MESSAGE(decodeAvatarOffer(doc.as<JsonVariantConst>(), k), b);
    TEST_ASSERT_EQUAL_STRING("demone", k.id);  // intatta
  }
  char buf[256];
  TEST_ASSERT_TRUE(encodeAvatarAck("demone", 0xA3130345u, true, "", buf, sizeof buf) > 0);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"avatar-ack","v":1,"id":"demone","crc":2735932229,"ok":true})", buf);
  TEST_ASSERT_TRUE(encodeAvatarAck("demone", 1, false, "download troppo lento", buf, sizeof buf) > 0);
  TEST_ASSERT_EQUAL_STRING(R"({"t":"avatar-ack","v":1,"id":"demone","crc":1,"ok":false,"error":"download troppo lento"})", buf);
  TEST_ASSERT_EQUAL(0, encodeAvatarAck("Demone", 1, true, "", buf, sizeof buf));
  TEST_ASSERT_EQUAL(0, encodeAvatarAck("demone", 1, true, "", buf, 20));  // non ci sta: niente JSON a metà
  TEST_ASSERT_TRUE(avatarUrl("192.168.1.20", 8766, o.token, buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.20:8766/avatars/0123456789abcdef0123456789abcdef", buf);
}

// ---------- giro 1 della revisione: adopt e AvatarDownload sicuri senza fidarsi di chi chiama ----------
static void test_finish_failure_then_adopt_is_refused() {
  static MemAvatarStorage st;
  st.eraseAll();
  AvatarCache c;
  c.begin(&st);
  const std::vector<uint8_t> b = builtinBytes("gatto");
  std::vector<uint8_t> bad = b;
  bad[500] ^= 0x40;
  AvatarDownload d;
  Avatar hdr;
  char why[96];
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "gatto"), why, sizeof why));
  TEST_ASSERT_TRUE(d.write(bad.data(), bad.size(), why, sizeof why));
  TEST_ASSERT_FALSE(d.finish(hdr, why, sizeof why));  // niente abort(): lo fa lui
  TEST_ASSERT_EQUAL(-1, st.tempLen());
  TEST_ASSERT_NOT_EQUAL(int(AvatarError::Ok), int(c.adopt(hdr)));  // hdr vuoto: id '' e frames 0
  TEST_ASSERT_EQUAL(0, c.count());
}

static void test_adopt_header_must_match_temp() {
  static MemAvatarStorage st;
  st.eraseAll();
  AvatarCache c;
  c.begin(&st);
  const std::vector<uint8_t> b = builtinBytes("gatto");
  Avatar hdr, hOther;
  char why[96];
  TEST_ASSERT_TRUE(download(st, offerOf(b, "gatto"), b, 512, hdr, why, sizeof why));
  hOther = hdr;
  hOther.len = uint16_t(hdr.len - 175);  // un'intestazione di altra lunghezza (altra posa/fotogrammi)
  TEST_ASSERT_NOT_EQUAL(int(AvatarError::Ok), int(c.adopt(hOther)));  // il temporaneo è di un altro avatar
  TEST_ASSERT_EQUAL(0, c.count());
  TEST_ASSERT_EQUAL(-1, st.tempLen());
  // intestazioni non valide
  Avatar z = hdr;
  z.frames = 0;
  TEST_ASSERT_TRUE(download(st, offerOf(b, "gatto"), b, 512, hdr, why, sizeof why));
  TEST_ASSERT_NOT_EQUAL(int(AvatarError::Ok), int(c.adopt(z)));
  z = hdr;
  z.len = uint16_t(kCavtMinBytes - 1);
  TEST_ASSERT_TRUE(download(st, offerOf(b, "gatto"), b, 512, hdr, why, sizeof why));
  TEST_ASSERT_NOT_EQUAL(int(AvatarError::Ok), int(c.adopt(z)));
  z = hdr;
  z.id[0] = 'G';
  TEST_ASSERT_TRUE(download(st, offerOf(b, "gatto"), b, 512, hdr, why, sizeof why));
  TEST_ASSERT_NOT_EQUAL(int(AvatarError::Ok), int(c.adopt(z)));
  TEST_ASSERT_EQUAL(0, c.count());
}

static void test_write_failure_locks_download() {
  static MemAvatarStorage st;
  st.eraseAll();
  const std::vector<uint8_t> b = builtinBytes("gatto");
  AvatarDownload d;
  Avatar hdr;
  char why[96];
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "gatto"), why, sizeof why));
  TEST_ASSERT_TRUE(d.write(b.data(), 1000, why, sizeof why));
  st.failWrites = true;
  TEST_ASSERT_FALSE(d.write(b.data() + 1000, 500, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("scrittura non riuscita", why);
  st.failWrites = false;
  TEST_ASSERT_EQUAL(-1, st.tempLen());
  TEST_ASSERT_FALSE(d.write(b.data() + 1000, b.size() - 1000, why, sizeof why));  // chi chiama insiste
  TEST_ASSERT_FALSE(d.finish(hdr, why, sizeof why));
  TEST_ASSERT_EQUAL(-1, st.tempLen());
  // anche «più byte di quelli offerti» chiude il download
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "gatto"), why, sizeof why));
  TEST_ASSERT_FALSE(d.write(b.data(), b.size() + 1 > b.size() ? b.size() + 1 : 0, why, sizeof why));
  TEST_ASSERT_EQUAL(-1, st.tempLen());
}

// Giro 1 della revisione del Task 9 (m2): solo un CAVT rotto sul PC (controlli del CAVT, id o CRC diversi
// dall'offerta) è un rifiuto da ricordare (AvatarNeeds::rejected); rete e scrittura no: si riprova.
static void test_download_bad_content_flag() {
  static MemAvatarStorage st;
  st.eraseAll();
  const std::vector<uint8_t> b = builtinBytes("gatto");
  Avatar hdr;
  char why[96];
  AvatarDownload d;
  TEST_ASSERT_FALSE(d.badContent());
  // buono
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "gatto"), why, sizeof why));
  TEST_ASSERT_TRUE(d.write(b.data(), b.size(), why, sizeof why));
  TEST_ASSERT_TRUE(d.finish(hdr, why, sizeof why));
  TEST_ASSERT_FALSE(d.badContent());
  st.tempAbort();
  // byte rovinato: errore del CAVT
  std::vector<uint8_t> bad = b;
  bad[500] ^= 0x40;
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "gatto"), why, sizeof why));
  TEST_ASSERT_TRUE(d.write(bad.data(), bad.size(), why, sizeof why));
  TEST_ASSERT_FALSE(d.finish(hdr, why, sizeof why));
  TEST_ASSERT_TRUE(d.badContent());
  // un begin nuovo lo azzera
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "gatto"), why, sizeof why));
  TEST_ASSERT_FALSE(d.badContent());
  // download incompleto: non è il contenuto
  TEST_ASSERT_TRUE(d.write(b.data(), 1000, why, sizeof why));
  TEST_ASSERT_FALSE(d.finish(hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("download incompleto", why);
  TEST_ASSERT_FALSE(d.badContent());
  // CRC diverso da quello offerto
  AvatarOffer o = offerOf(b, "gatto");
  o.crc ^= 1;
  TEST_ASSERT_TRUE(d.begin(st, o, why, sizeof why));
  TEST_ASSERT_TRUE(d.write(b.data(), b.size(), why, sizeof why));
  TEST_ASSERT_FALSE(d.finish(hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("CRC diverso da quello offerto", why);
  TEST_ASSERT_TRUE(d.badContent());
  // l'avatar non è quello offerto
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "demone"), why, sizeof why));
  TEST_ASSERT_TRUE(d.write(b.data(), b.size(), why, sizeof why));
  TEST_ASSERT_FALSE(d.finish(hdr, why, sizeof why));
  TEST_ASSERT_EQUAL_STRING("l'avatar non è quello offerto", why);
  TEST_ASSERT_TRUE(d.badContent());
  // scrittura fallita e più byte di quelli offerti: no
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "gatto"), why, sizeof why));
  st.failWrites = true;
  TEST_ASSERT_FALSE(d.write(b.data(), 512, why, sizeof why));
  st.failWrites = false;
  TEST_ASSERT_FALSE(d.finish(hdr, why, sizeof why));
  TEST_ASSERT_FALSE(d.badContent());
  TEST_ASSERT_TRUE(d.begin(st, offerOf(b, "gatto"), why, sizeof why));
  TEST_ASSERT_FALSE(d.write(b.data(), b.size() + 1, why, sizeof why));
  TEST_ASSERT_FALSE(d.badContent());
  TEST_ASSERT_EQUAL(-1, st.tempLen());
}

// Rinomina fallita: la versione vecchia dello stesso id resta com'era e si legge ancora.
static void test_failed_adopt_and_store_keep_old_slot() {
  static MemAvatarStorage st;
  st.eraseAll();
  AvatarCache c;
  c.begin(&st);
  const std::vector<uint8_t> v1 = variant("av-uno", "Uno"), v2 = variant("av-uno", "Due");
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.store("av-uno", crcOf(v1), v1.data(), v1.size())));
  Avatar hdr;
  char why[96];
  TEST_ASSERT_TRUE(download(st, offerOf(v2, "av-uno"), v2, 512, hdr, why, sizeof why));
  st.failWrites = true;
  TEST_ASSERT_EQUAL(int(AvatarError::Short), int(c.adopt(hdr)));
  st.failWrites = false;
  TEST_ASSERT_EQUAL(-1, st.tempLen());  // solo il temporaneo è andato
  TEST_ASSERT_EQUAL(1, c.count());
  const Avatar *a = c.find("av-uno", crcOf(v1), true);
  TEST_ASSERT_NOT_NULL(a);
  uint8_t f[kCavtFrameBytes];
  TEST_ASSERT_TRUE(avatarFrameBytes(*a, Pose::Working, 0, f));
  TEST_ASSERT_EQUAL_STRING("Uno", a->name);
  // lo stesso con store
  st.failWrites = true;
  TEST_ASSERT_NOT_EQUAL(int(AvatarError::Ok), int(c.store("av-uno", crcOf(v2), v2.data(), v2.size())));
  st.failWrites = false;
  TEST_ASSERT_EQUAL(1, c.count());
  TEST_ASSERT_TRUE(c.has("av-uno", crcOf(v1), true));
  AvatarCache again;
  TEST_ASSERT_EQUAL(1, again.begin(&st));
  TEST_ASSERT_TRUE(again.has("av-uno", crcOf(v1), true));
}

// Cache piena: adopt sfratta il meno usato, come store.
static void test_adopt_full_cache_evicts_lru() {
  static MemAvatarStorage st;
  st.eraseAll();
  AvatarCache c;
  c.begin(&st);
  char id[16];
  std::vector<uint8_t> v[9];
  for (int i = 0; i < 9; i++) {
    snprintf(id, sizeof id, "av-%d", i);
    v[i] = variant(id);
  }
  for (int i = 0; i < 8; i++) {
    snprintf(id, sizeof id, "av-%d", i);
    TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.store(id, crcOf(v[i]), v[i].data(), v[i].size())));
  }
  for (int i = 0; i < 8; i++)  // av-3 resta il meno usato
    if (i != 3) {
      snprintf(id, sizeof id, "av-%d", i);
      TEST_ASSERT_NOT_NULL(c.find(id, crcOf(v[i]), true));
    }
  Avatar hdr;
  char why[96];
  TEST_ASSERT_TRUE(download(st, offerOf(v[8], "av-8"), v[8], 512, hdr, why, sizeof why));
  int slot = -1;
  TEST_ASSERT_EQUAL(int(AvatarError::Ok), int(c.adopt(hdr, &slot)));
  TEST_ASSERT_EQUAL(8, c.count());
  TEST_ASSERT_FALSE(c.has("av-3", crcOf(v[3]), true));
  for (int i = 0; i < 9; i++)
    if (i != 3) {
      snprintf(id, sizeof id, "av-%d", i);
      TEST_ASSERT_TRUE_MESSAGE(c.has(id, crcOf(v[i]), true), id);
    }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_cavt_without_run_still_valid);
  RUN_TEST(test_run_edges);
  RUN_TEST(test_cavt_stream_matches_parse);
  RUN_TEST(test_download_then_adopt);
  RUN_TEST(test_download_rejects);
  RUN_TEST(test_avatar_offer_and_ack_messages);
  RUN_TEST(test_finish_failure_then_adopt_is_refused);
  RUN_TEST(test_adopt_header_must_match_temp);
  RUN_TEST(test_write_failure_locks_download);
  RUN_TEST(test_failed_adopt_and_store_keep_old_slot);
  RUN_TEST(test_adopt_full_cache_evicts_lru);
  RUN_TEST(test_builtins_valid);
  RUN_TEST(test_builtins_match_library_json);
  RUN_TEST(test_corrupt_cavt_rejected);
  RUN_TEST(test_fallback_and_frame_modulo);
  RUN_TEST(test_draw_ink_paper_transparent);
  RUN_TEST(test_cache_store_find_and_checks);
  RUN_TEST(test_cache_lru_replacement_and_versions);
  RUN_TEST(test_cache_survives_reboot_and_drops_corrupt);
  RUN_TEST(test_resolution_session_default_goccia);
  RUN_TEST(test_needs_once_then_every_30_s);
  RUN_TEST(test_needs_rejected_and_crc);
  RUN_TEST(test_state_and_welcome_avatar_fields);
  RUN_TEST(test_avatar_message_roundtrip);
  RUN_TEST(test_avatar_need_encoding_and_base64);
  RUN_TEST(test_download_bad_content_flag);
  return UNITY_END();
}
