// Temi: archivio astratto, tema attivo (buffer da 4 096 byte), uscita di emergenza e aggancio a ui::draw.
// Regola §7 di ISTRUZIONI-SVILUPPO: Permessi e schermate di sistema sempre di serie, anche con un tema attivo.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

#include "core/model.h"
#include "core/theme_runtime.h"
#include "gfx/canvas.h"
#include "ui/screens.h"
#include "ui/theme_draw.h"

using namespace cal;
using namespace cal::theme;

void setUp() {}
void tearDown() {}

static std::string repoRoot() {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  return k != std::string::npos ? f.substr(0, k) : "../";
}
static std::vector<uint8_t> load(const char *name) {
  std::ifstream in(repoRoot() + "protocol/themes/fixtures/" + name, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static MemStorage gSt;
static Runtime gRt;
static uint8_t gScratch[kMaxBlock];

static void install(const char *id, const std::vector<uint8_t> &f) {
  TEST_ASSERT_TRUE(gSt.tmpBegin());
  TEST_ASSERT_TRUE(gSt.tmpAppend(f.data(), uint32_t(f.size())));
  TEST_ASSERT_TRUE(gSt.tmpCommit(id));
}

static void reset() {
  gSt.eraseAll();
  install("prova", load("prova.cthm"));
  install("lista", load("lista.cthm"));
  gRt.begin(&gSt);
  gRt.suspend(false);  // test_suspend_and_sleep_rect lo lascia sospeso
}

// off + n non deve girare in uint32: una lettura che "torna" dentro il file va rifiutata.
static void test_mem_storage_offset_wrap() {
  reset();
  uint8_t b[16];
  const uint32_t len = uint32_t(gSt.size("prova"));
  TEST_ASSERT_TRUE(gSt.read("prova", len - 4, b, 4));
  TEST_ASSERT_FALSE(gSt.read("prova", len - 4, b, 5));
  TEST_ASSERT_FALSE(gSt.read("prova", 0xFFFFFFF0u, b, 16));  // 0xFFFFFFF0 + 16 = 0
  TEST_ASSERT_FALSE(gSt.read("prova", 8, b, 0xFFFFFFFCu));
  TEST_ASSERT_TRUE(gSt.tmpBegin());
  TEST_ASSERT_TRUE(gSt.tmpAppend(b, 8));
  TEST_ASSERT_TRUE(gSt.tmpRead(4, b, 4));
  TEST_ASSERT_FALSE(gSt.tmpRead(0xFFFFFFFCu, b, 8));
  TEST_ASSERT_FALSE(gSt.tmpAppend(b, 0xFFFFFFFCu));
  gSt.tmpDrop();
}

static void test_catalog_and_verify() {
  reset();
  CatalogEntry e[kMaxThemes];
  TEST_ASSERT_EQUAL(2, catalog(gSt, e, kMaxThemes));
  Header h;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "prova", gScratch, &h)));
  TEST_ASSERT_EQUAL_STRING("Mascotte grande", h.name);
  gSt.raw("lista")[200] ^= 0xFF;
  TEST_ASSERT_EQUAL(int(Error::Crc), int(verifyStored(gSt, "lista", gScratch, &h)));
  TEST_ASSERT_NOT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "manca", gScratch, &h)));
}

// Un open/read fallito (memoria sulla scheda) non è un tema rotto: io = true, e il chiamante non cancella il file.
// Contenuto rovinato o file mancante: io = false. I codici d'errore restano quelli di sempre (theme-ack, parità).
static void test_verify_io_vs_corrupt() {
  reset();
  Header h;
  bool io = true;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "prova", gScratch, &h, &io)));
  TEST_ASSERT_FALSE(io);
  // Tutte le letture del controllo di un file buono: si fa fallire ciascuna (intestazione, CRC, indice, blocchi).
  gSt.reads = 0;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "prova", gScratch, &h, &io)));
  const int total = gSt.reads;
  TEST_ASSERT_TRUE(total >= 3);
  for (int k = 0; k < total; k++) {
    gSt.reads = 0;
    gSt.failAfterReads = k;
    io = false;
    TEST_ASSERT_NOT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "prova", gScratch, &h, &io)));
    TEST_ASSERT_TRUE_MESSAGE(io, "lettura fallita trattata come contenuto rovinato");
  }
  gSt.failAfterReads = -1;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "prova", gScratch, &h, &io)));  // il file è rimasto buono
  // Il file c'è ma non si apre (Storage::size = kSizeIo).
  IndexEntry idx[kMaxScreens];
  io = false;
  TEST_ASSERT_EQUAL(int(Error::Short), int(verifyAt(gSt, "prova", kSizeIo, gScratch, &h, idx, &io)));
  TEST_ASSERT_TRUE(io);
  // Rovinato: CRC sbagliato, io falso.
  gSt.raw("lista")[200] ^= 0xFF;
  io = true;
  TEST_ASSERT_EQUAL(int(Error::Crc), int(verifyStored(gSt, "lista", gScratch, &h, &io)));
  TEST_ASSERT_FALSE(io);
  // Manca: non è un errore di I/O (si toglie dall'ordine come prima).
  io = true;
  TEST_ASSERT_EQUAL(int(Error::Short), int(verifyStored(gSt, "manca", gScratch, &h, &io)));
  TEST_ASSERT_FALSE(io);
}

static void test_activate_and_block() {
  reset();
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  TEST_ASSERT_EQUAL_STRING("prova", gRt.active());
  TEST_ASSERT_TRUE(gRt.has(Screen::Home));
  TEST_ASSERT_TRUE(gRt.has(Screen::Sleep));
  TEST_ASSERT_FALSE(gRt.has(Screen::Detail));
  const uint8_t *b;
  uint16_t n;
  TEST_ASSERT_TRUE(gRt.block(Screen::Home, b, n));
  TEST_ASSERT_EQUAL(int(Error::Ok), int(checkBlock(b, n)));
  TEST_ASSERT_FALSE(gRt.block(Screen::Detail, b, n));
  TEST_ASSERT_TRUE(gRt.activate(""));
  TEST_ASSERT_EQUAL_STRING("", gRt.active());
  TEST_ASSERT_FALSE(gRt.block(Screen::Home, b, n));
  char id[theme::kIdLen];
  TEST_ASSERT_FALSE(gRt.takeBroken(id, sizeof id));
}

static void test_broken_active_theme_falls_back() {
  reset();
  gSt.raw("prova")[100] ^= 0x01;  // CRC sbagliato: non si attiva
  TEST_ASSERT_FALSE(gRt.activate("prova"));
  TEST_ASSERT_EQUAL_STRING("", gRt.active());
  char id[theme::kIdLen];
  TEST_ASSERT_TRUE(gRt.takeBroken(id, sizeof id));
  TEST_ASSERT_EQUAL_STRING("prova", id);
  TEST_ASSERT_FALSE(gRt.takeBroken(id, sizeof id));  // una volta sola
  // rovinato dopo l'attivazione (flash che cede): al prossimo caricamento torna di serie
  reset();
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  std::vector<uint8_t> f = load("prova.cthm");
  const uint32_t off = f[76] | f[77] << 8 | f[78] << 16 | uint32_t(f[79]) << 24;  // blocco della home
  gSt.raw("prova")[off] = 0;  // 0 varianti
  const uint8_t *b;
  uint16_t n;
  TEST_ASSERT_FALSE(gRt.block(Screen::Home, b, n));
  TEST_ASSERT_EQUAL_STRING("", gRt.active());
  TEST_ASSERT_TRUE(gRt.takeBroken(id, sizeof id));
}

typedef Canvas<250, 122> Cv;
static Model gM;
static const Ms kNow = 1791707400000LL;
static const WallClock kWc = wallFromCivil(2026, 10, 11, 10, 30, 0);

static void compose(Cv &cv) { ui::compose(cv, kHeltec213, gM, 0, kNow, kWc); }

static void test_dispatch_home_and_perm() {
  reset();
  newModel(gM, kNow, kWc);
  resetStack(gM, makeScreen(ScreenId::Home));
  gM.sys.pc = true;
  Cv base, themed;
  ui::setThemeRuntime(nullptr);
  compose(base);
  ui::setThemeRuntime(&gRt);
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  compose(themed);
  TEST_ASSERT_TRUE(ui::lastThemeDraw().drawn);
  TEST_ASSERT_TRUE(memcmp(base.data(), themed.data(), Cv::kBytes) != 0);
  // Permessi: identica a quella di serie con o senza tema
  gM.sessions[0].state = SessionState::Permission;
  ScreenEntry p = makeScreen(ScreenId::Perm);
  p.i = 0;
  pushScreen(gM, p);
  ui::setThemeRuntime(nullptr);
  compose(base);
  ui::setThemeRuntime(&gRt);
  compose(themed);
  TEST_ASSERT_FALSE(ui::lastThemeDraw().drawn);
  TEST_ASSERT_EQUAL_MEMORY(base.data(), themed.data(), Cv::kBytes);
  // Boot: di serie
  resetStack(gM, makeScreen(ScreenId::Boot));
  compose(themed);
  TEST_ASSERT_FALSE(ui::lastThemeDraw().drawn);
  ui::setThemeRuntime(nullptr);
}

static void test_suspend_and_sleep_rect() {
  reset();
  newModel(gM, kNow, kWc);
  resetStack(gM, makeScreen(ScreenId::Home));
  pushScreen(gM, makeScreen(ScreenId::Sleep));
  ui::setThemeRuntime(&gRt);
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  Cv cv;
  compose(cv);  // il riquadro è quello dell'ultimo disegno
  TEST_ASSERT_TRUE(ui::lastThemeDraw().drawn);
  const ui::SleepRect r = ui::sleepMascotRect(kHeltec213, gM, kWc);
  TEST_ASSERT_EQUAL(158, r.x);  // la mascotte del Riposo di «prova»: scala 3 in (158, 30)
  TEST_ASSERT_EQUAL(30, r.y);
  TEST_ASSERT_EQUAL(84, r.w);
  TEST_ASSERT_EQUAL(75, r.h);
  gRt.suspend(true);  // PRG all'accensione: di serie per questo avvio
  compose(cv);
  TEST_ASSERT_FALSE(ui::lastThemeDraw().drawn);
  const ui::SleepRect s = ui::sleepMascotRect(kHeltec213, gM, kWc);
  TEST_ASSERT_EQUAL(56, s.w);  // di serie: scala 2
  TEST_ASSERT_EQUAL_STRING("prova", gRt.active());  // il tema scelto resta per il prossimo avvio
  ui::setThemeRuntime(nullptr);
}

// Home → Riposo → Home: il blocco della home si ricarica, nessun resto del Riposo nel buffer.
static void test_screen_switch_reloads_block() {
  reset();
  newModel(gM, kNow, kWc);
  resetStack(gM, makeScreen(ScreenId::Home));
  gM.sys.pc = true;
  ui::setThemeRuntime(&gRt);
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  Cv a, b, c;
  compose(a);
  TEST_ASSERT_TRUE(ui::lastThemeDraw().drawn);
  pushScreen(gM, makeScreen(ScreenId::Sleep));
  compose(b);
  TEST_ASSERT_TRUE(ui::lastThemeDraw().drawn);
  TEST_ASSERT_EQUAL(int(Screen::Sleep), int(ui::lastThemeDraw().screen));
  TEST_ASSERT_TRUE(memcmp(a.data(), b.data(), Cv::kBytes) != 0);
  resetStack(gM, makeScreen(ScreenId::Home));
  compose(c);
  TEST_ASSERT_TRUE(ui::lastThemeDraw().drawn);
  TEST_ASSERT_EQUAL_MEMORY(a.data(), c.data(), Cv::kBytes);
  // il riquadro del Riposo chiesto dopo ogni disegno (keepPanel) non tocca l'archivio
  const int r0 = gSt.reads;
  for (int i = 0; i < 5; i++) {
    compose(c);
    (void)ui::sleepMascotRect(kHeltec213, gM, kWc);
  }
  TEST_ASSERT_EQUAL(r0, gSt.reads);
  ui::setThemeRuntime(nullptr);
}

// Tema col Riposo che dipende dal fotogramma (blink): due mascotte, una per i fotogrammi pari e una per i dispari.
static void put16(std::vector<uint8_t> &v, size_t at, uint32_t x) { v[at] = uint8_t(x); v[at + 1] = uint8_t(x >> 8); }
static void put32(std::vector<uint8_t> &v, size_t at, uint32_t x) { put16(v, at, x); put16(v, at + 2, x >> 16); }
static std::vector<uint8_t> mascotEl(int x, int y, uint8_t blink) {
  // mascotte scala 2, posa idle, nessuno, senza accessorio; condizione blink = pari (0) o dispari (1)
  std::vector<uint8_t> e = {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 4, uint8_t(CondOp::Blink), blink, 2, 0, 3, 0};
  put16(e, 2, uint32_t(x)); put16(e, 4, uint32_t(y)); put16(e, 6, 56); put16(e, 8, 50);
  return e;
}
static std::vector<uint8_t> blinkTheme() {
  std::vector<uint8_t> blk = {1, 0, 0, 0, 0 /*always*/, 1 /*selected*/, 10, 0, 2, 0};
  for (const auto &e : {mascotEl(10, 20, 0), mascotEl(170, 60, 1)}) blk.insert(blk.end(), e.begin(), e.end());
  put16(blk, 2, uint32_t(blk.size()));  // nessuna stringa
  std::vector<uint8_t> f(kHeaderLen + 8, 0);
  memcpy(f.data(), "CTHM", 4);
  f[4] = 1; f[5] = kHeaderLen;
  put16(f, 16, 250); put16(f, 18, 122);
  memcpy(f.data() + 20, "lampo", 5);
  memcpy(f.data() + 44, "Lampo", 5);
  f[68] = 1;
  f[kHeaderLen] = uint8_t(Screen::Sleep);
  put16(f, kHeaderLen + 2, uint32_t(blk.size()));
  put32(f, kHeaderLen + 4, uint32_t(f.size()));
  f.insert(f.end(), blk.begin(), blk.end());
  put32(f, 12, uint32_t(f.size()));
  put32(f, 8, crc32Update(0, f.data() + 12, f.size() - 12));
  return f;
}
static bool inkIn(const Cv &cv, const ui::SleepRect &r) {
  for (int y = r.y; y < r.y + r.h; y++)
    for (int x = r.x; x < r.x + r.w; x++)
      if (cv.get(x, y)) return true;
  return false;
}

static void test_sleep_rect_matches_drawn_frame() {
  reset();
  install("lampo", blinkTheme());
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "lampo", gScratch, nullptr)));
  newModel(gM, kNow, kWc);
  resetStack(gM, makeScreen(ScreenId::Home));
  pushScreen(gM, makeScreen(ScreenId::Sleep));
  ui::setThemeRuntime(&gRt);
  TEST_ASSERT_TRUE(gRt.activate("lampo"));
  const ui::SleepRect even = {10, 20, 56, 50}, odd = {170, 60, 56, 50};
  for (int f = 0; f < 4; f++) {
    Cv cv;
    ui::compose(cv, kHeltec213, gM, f, kNow + f * 500, kWc);
    TEST_ASSERT_TRUE(ui::lastThemeDraw().drawn);
    const ui::SleepRect r = ui::sleepMascotRect(kHeltec213, gM, kWc);
    const ui::SleepRect other = f % 2 ? even : odd;
    TEST_ASSERT_TRUE(r == (f % 2 ? odd : even));  // il riquadro è quello del fotogramma disegnato
    TEST_ASSERT_TRUE(inkIn(cv, r));                // la mascotte sta dentro
    TEST_ASSERT_FALSE(inkIn(cv, other));           // e fuori non c'è niente
  }
  ui::setThemeRuntime(nullptr);
}

static void test_safe_boot() {
  // tenuto nel loop: scatta a 1,5 s dalla pressione, una volta sola
  SafeBoot s;
  s.edge(true, 100, true);
  TEST_ASSERT_TRUE(s.keepBoot(100));
  TEST_ASSERT_FALSE(s.step(1599));
  TEST_ASSERT_TRUE(s.step(1600));
  TEST_ASSERT_FALSE(s.step(1700));
  TEST_ASSERT_FALSE(s.keepBoot(1700));
  TEST_ASSERT_TRUE(s.swallow());   // gesti col tasto ancora giù (VeryLong a 3 s)
  s.edge(false, 4000, false);
  TEST_ASSERT_TRUE(s.swallow());   // il gesto del rilascio
  TEST_ASSERT_FALSE(s.swallow());
  // rilascio a 1,4 s: niente uscita, l'avvio non si tiene più
  SafeBoot t;
  t.edge(true, 0, true);
  t.edge(false, 1400, true);
  TEST_ASSERT_FALSE(t.step(2000));
  TEST_ASSERT_FALSE(t.keepBoot(2000));
  TEST_ASSERT_FALSE(t.swallow());
  t.edge(true, 2100, false);       // avvio finito: non vale più
  TEST_ASSERT_FALSE(t.step(5000));
  TEST_ASSERT_FALSE(t.keepBoot(5000));
}

// Pressione prima del loop o durante il completo bloccante: i fronti arrivano tutti insieme al primo giro, con gli
// istanti dell'interrupt.
static void test_safe_boot_late_loop() {
  SafeBoot a;  // giù a 300 ms (setup), su a 1 800 ms (durante il completo): 1,5 s tenuto → uscita
  a.edge(true, 300, true);
  a.edge(false, 1800, true);
  TEST_ASSERT_TRUE(a.step(2600));
  TEST_ASSERT_TRUE(a.swallow());   // il gesto di quel rilascio
  TEST_ASSERT_FALSE(a.swallow());
  SafeBoot b;  // giù a 300, su a 1 700: 1,4 s → niente
  b.edge(true, 300, true);
  b.edge(false, 1700, true);
  TEST_ASSERT_FALSE(b.step(2600));
  SafeBoot c;  // giù durante il completo (1 000), ancora giù al primo giro (2 400): l'avvio resta, poi scatta
  c.edge(true, 1000, true);
  TEST_ASSERT_FALSE(c.step(2400));
  TEST_ASSERT_TRUE(c.keepBoot(2400));
  TEST_ASSERT_TRUE(c.keepBoot(2499));
  TEST_ASSERT_TRUE(c.step(2500));
  TEST_ASSERT_FALSE(c.keepBoot(2500));
  // tetto: senza step l'avvio non resta oltre 3 s dalla pressione
  SafeBoot d;
  d.edge(true, 0, true);
  TEST_ASSERT_TRUE(d.keepBoot(2999));
  TEST_ASSERT_FALSE(d.keepBoot(3000));
}

// Tenuto oltre i 3 s (VeryLong già scattato col tasto giù): al rilascio nessun gesto; la pressione dopo non si scarta.
static void test_safe_boot_swallow_clears() {
  SafeBoot s;
  s.edge(true, 0, true);
  TEST_ASSERT_TRUE(s.step(1500));
  TEST_ASSERT_TRUE(s.swallow());   // VeryLong a 3 s, tasto giù
  s.edge(false, 3500, false);      // rilascio senza gesto (già scattato)
  s.edge(true, 5000, false);       // pressione nuova
  TEST_ASSERT_FALSE(s.swallow());
  s.edge(false, 5100, false);
  TEST_ASSERT_FALSE(s.swallow());  // il suo gesto passa
}

// Prestito alla partita: niente blocchi né attivazioni finché è prestato, il tema resta quello; dopo giveBack il
// blocco si rilegge (la partita ci ha scritto sopra).
static void test_lend_give_back() {
  reset();
  TEST_ASSERT_TRUE(gRt.activate("prova"));
  const uint8_t *b;
  uint16_t n;
  TEST_ASSERT_TRUE(gRt.block(Screen::Home, b, n));
  uint8_t *const sc = gRt.drawScratch();
  TEST_ASSERT_NOT_NULL(sc);
  // l'arena: il blocco e subito dopo i buffer del disegno, 6 144 byte = 3 pezzi da 2 KB della cartuccia
  TEST_ASSERT_EQUAL(6144, kLendBytes);
  TEST_ASSERT_EQUAL(3, kLendBytes / 2048);
  TEST_ASSERT_TRUE(b >= sc - kMaxBlock && b + n <= sc);
  TEST_ASSERT_NULL(gRt.lend(kLendBytes + 1));
  uint8_t *p = gRt.lend(kLendBytes);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_TRUE(p == sc - kMaxBlock);  // tutta l'arena, buffer del disegno compresi
  TEST_ASSERT_TRUE(gRt.lent());
  TEST_ASSERT_NULL(gRt.lend(16));  // già prestato
  memset(p, 0xA5, kLendBytes);     // la RAM della cartuccia
  TEST_ASSERT_FALSE(gRt.has(Screen::Home));  // come block(): in prestito niente schermate del tema
  TEST_ASSERT_FALSE(gRt.block(Screen::Home, b, n));
  TEST_ASSERT_NULL(gRt.drawScratch());  // e niente buffer del disegno
  // in prestito nessuna schermata si disegna a tema (né la Home né il Riposo) e l'arena resta della partita
  newModel(gM, kNow, kWc);
  resetStack(gM, makeScreen(ScreenId::Home));
  gM.sys.pc = true;
  ui::setThemeRuntime(&gRt);
  Cv cv, base;
  compose(cv);
  TEST_ASSERT_FALSE(ui::lastThemeDraw().drawn);
  pushScreen(gM, makeScreen(ScreenId::Sleep));
  compose(cv);
  TEST_ASSERT_FALSE(ui::lastThemeDraw().drawn);
  ui::setThemeRuntime(nullptr);
  compose(base);
  TEST_ASSERT_EQUAL_MEMORY(base.data(), cv.data(), Cv::kBytes);  // come di serie
  for (int i = 0; i < kLendBytes; i++) TEST_ASSERT_EQUAL_HEX8(0xA5, p[i]);
  TEST_ASSERT_FALSE(gRt.activate("lista"));
  char bad[theme::kIdLen];
  TEST_ASSERT_FALSE(gRt.takeBroken(bad, sizeof bad));  // in prestito non è un tema rotto
  TEST_ASSERT_EQUAL_STRING("prova", gRt.active());
  gRt.giveBack();
  TEST_ASSERT_FALSE(gRt.lent());
  TEST_ASSERT_TRUE(gRt.has(Screen::Home));
  const int before = gSt.reads;
  TEST_ASSERT_TRUE(gRt.block(Screen::Home, b, n));  // riletto dall'archivio, non i byte della partita
  TEST_ASSERT_EQUAL(before + 1, gSt.reads);
  TEST_ASSERT_EQUAL(int(Error::Ok), int(checkBlock(b, n)));
  TEST_ASSERT_TRUE(gRt.drawScratch() == sc);
  ui::setThemeRuntime(&gRt);  // dopo giveBack si torna a tema
  resetStack(gM, makeScreen(ScreenId::Home));
  compose(cv);
  TEST_ASSERT_TRUE(ui::lastThemeDraw().drawn);
  ui::setThemeRuntime(nullptr);
}

// verifyAt legge il CRC a pezzi da kMaxBlock nello scratch: con theme_fs ogni read è un'apertura del file.
static void test_verify_reads_in_big_chunks() {
  reset();
  const int32_t len = gSt.size("lista");
  Header h;
  const int before = gSt.reads;
  TEST_ASSERT_EQUAL(int(Error::Ok), int(verifyStored(gSt, "lista", gScratch, &h)));
  const int crcReads = int((uint32_t(len) - 12 + kMaxBlock - 1) / kMaxBlock);
  // intestazione + CRC + indice + un blocco per schermata
  TEST_ASSERT_EQUAL(1 + crcReads + 1 + h.nScreens, gSt.reads - before);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_catalog_and_verify);
  RUN_TEST(test_verify_io_vs_corrupt);
  RUN_TEST(test_activate_and_block);
  RUN_TEST(test_broken_active_theme_falls_back);
  RUN_TEST(test_dispatch_home_and_perm);
  RUN_TEST(test_suspend_and_sleep_rect);
  RUN_TEST(test_screen_switch_reloads_block);
  RUN_TEST(test_sleep_rect_matches_drawn_frame);
  RUN_TEST(test_safe_boot);
  RUN_TEST(test_safe_boot_late_loop);
  RUN_TEST(test_safe_boot_swallow_clears);
  RUN_TEST(test_mem_storage_offset_wrap);
  RUN_TEST(test_lend_give_back);
  RUN_TEST(test_verify_reads_in_big_chunks);
  return UNITY_END();
}
