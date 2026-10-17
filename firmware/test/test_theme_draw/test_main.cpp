// Temi: interprete di disegno (docs/TEMI.md §6). La parità pixel per pixel col JS sta in test_theme_parity.
#include <string.h>
#include <unity.h>

#include <fstream>
#include <string>
#include <vector>

#include "core/avatars.h"
#include "core/model.h"
#include "core/strbuf.h"
#include "core/theme_format.h"
#include "core/theme_runtime.h"
#include "gfx/canvas.h"
#include "gfx/prims.h"
#include "ui/screens.h"
#include "ui/theme_draw.h"

using namespace cal;
using theme::Screen;

void setUp() {}
void tearDown() {}

typedef Canvas<250, 122> Cv;
static Model gM;
// Buffer del disegno (come Runtime::drawScratch), sporco: il disegno non deve contare su zeri iniziali
alignas(8) static uint8_t gSc[theme::kDrawScratch];
static struct DirtySc { DirtySc() { memset(gSc, 0xA5, sizeof gSc); } } gDirtySc;
static const Ms kNow = 1791707400000LL;
static const WallClock kWc = wallFromCivil(2026, 10, 11, 10, 30, 0);

// Blocchi costruiti a mano (come in test_theme_format): un elemento per caso.
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
static void xywh(std::vector<uint8_t> &e, int x, int y, int w, int h) {
  const int v[4] = {x, y, w, h};
  for (int i = 0; i < 4; i++) { e[2 + 2 * i] = uint8_t(v[i] & 0xFF); e[3 + 2 * i] = uint8_t((v[i] >> 8) & 0xFF); }
}
// Più elementi di primo livello in una variante «always».
static std::vector<uint8_t> many(const std::vector<std::vector<uint8_t>> &els, const std::vector<uint8_t> &strs = {}) {
  std::vector<uint8_t> b = {1, 0, 0, 0, 0, 1, 10, 0, uint8_t(els.size()), 0};
  for (auto &e : els) b.insert(b.end(), e.begin(), e.end());
  b[2] = uint8_t(b.size()); b[3] = uint8_t(b.size() >> 8);
  b.insert(b.end(), strs.begin(), strs.end());
  return b;
}

static Model &fresh(int n) {
  newModel(gM, kNow, kWc);
  gM.nSessions = n;
  for (int k = 0; k < n; k++) {
    Session &s = gM.sessions[k];
    memset(&s, 0, sizeof s);
    Sb(s.name, sizeof s.name).s("s").i(k);
    s.state = SessionState::Working;
    s.total = 4;
    s.done = 1;
  }
  gM.sys.wallOk = true;
  gM.ui.selUntil = 0;
  return gM;
}

static bool drawOn(Surface &s, const std::vector<uint8_t> &b, ui::ThemeDrawInfo *info = nullptr, Ms now = kNow, int f = 0) {
  ui::ThemeDrawInfo tmp;
  return ui::drawThemeBlock(s, kHeltec213, gM, f, now, kWc, Screen::Home, b.data(), uint16_t(b.size()), info ? info : &tmp, gSc);
}
static bool draw(Cv &cv, const std::vector<uint8_t> &b, ui::ThemeDrawInfo *info = nullptr, Ms now = kNow, int f = 0) {
  cv.clear(false);
  CanvasSurface<250, 122> s(cv);
  return drawOn(s, b, info, now, f);
}

static int inkIn(const Cv &cv, int x, int y, int w, int h) {
  int n = 0;
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) n += cv.get(xx, yy);
  return n;
}

// Conta le chiamate che escono dal canvas (riquadro e cerchio si ritagliano da soli).
struct BoundsSurface : Surface {
  int calls = 0, outside = 0;
  void fillRect(int x, int y, int w, int h, bool) override {
    calls++;
    if (x < 0 || y < 0 || x + w > 250 || y + h > 122) outside++;
  }
};

static void test_rect_fill_and_radius() {
  fresh(0);
  Cv cv;
  std::vector<uint8_t> e = el(10, {0, 1, 3});  // pieno, senza bordo, raggio 3 (riquadro 0,0,10,10)
  TEST_ASSERT_TRUE(draw(cv, one(e)));
  TEST_ASSERT_FALSE(cv.get(0, 0));
  TEST_ASSERT_FALSE(cv.get(1, 1));   // dx + dy = 2 < 3
  TEST_ASSERT_TRUE(cv.get(2, 1));    // dx + dy = 3: dentro
  TEST_ASSERT_TRUE(cv.get(5, 5));
  TEST_ASSERT_FALSE(cv.get(9, 9));
  TEST_ASSERT_EQUAL(100 - 4 * 6, inkIn(cv, 0, 0, 10, 10));  // 6 pixel tagliati per angolo
  // bordo 1 senza raggio: 36 pixel, interno vuoto
  TEST_ASSERT_TRUE(draw(cv, one(el(10, {1, 0, 0}))));
  TEST_ASSERT_EQUAL(36, inkIn(cv, 0, 0, 10, 10));
}

static void test_circle() {
  fresh(0);
  Cv cv;
  TEST_ASSERT_TRUE(draw(cv, one(el(11, {0, 1}))));  // pieno, d = 10
  TEST_ASSERT_TRUE(cv.get(5, 5));
  TEST_ASSERT_FALSE(cv.get(0, 0));
  TEST_ASSERT_TRUE(cv.get(0, 5) || cv.get(0, 4));
  const int full = inkIn(cv, 0, 0, 10, 10);
  TEST_ASSERT_TRUE(full > 70 && full < 90);  // ≈ π·25
  TEST_ASSERT_TRUE(draw(cv, one(el(11, {2, 0}))));  // anello di 2
  TEST_ASSERT_FALSE(cv.get(5, 5));
  TEST_ASSERT_TRUE(inkIn(cv, 0, 0, 10, 10) < full);
}

static void test_dotted_line_and_invert() {
  fresh(0);
  Cv cv;
  std::vector<uint8_t> e = el(9, {0, 1});  // orizzontale a puntini, 10 × 10
  TEST_ASSERT_TRUE(draw(cv, one(e)));
  TEST_ASSERT_TRUE(cv.get(0, 0));
  TEST_ASSERT_FALSE(cv.get(1, 0));
  TEST_ASSERT_TRUE(cv.get(3, 0));    // dotsH: un punto ogni 3
  TEST_ASSERT_TRUE(cv.get(0, 9));    // ogni riga di h
  // invert su un riquadro pieno disegnato prima: i punti diventano carta
  std::vector<uint8_t> two = {1, 0, 0, 0, 0, 1, 10, 0, 2, 0};
  std::vector<uint8_t> r = el(10, {0, 1, 0}), l = el(9, {0, 1}, 1);
  two.insert(two.end(), r.begin(), r.end());
  two.insert(two.end(), l.begin(), l.end());
  two[2] = uint8_t(two.size());
  TEST_ASSERT_TRUE(draw(cv, two));
  TEST_ASSERT_FALSE(cv.get(0, 0));
  TEST_ASSERT_TRUE(cv.get(1, 0));
}

static void test_offscreen_elements_clip() {
  fresh(1);
  Cv cv;
  std::vector<uint8_t> e = el(10, {3, 1, 8});
  e[2] = 0x00; e[3] = 0xFF;  // x = −256
  e[6] = 0x00; e[7] = 0x02;  // w = 512: fino a x = 256, oltre il bordo destro
  e[8] = 0x00; e[9] = 0x02;  // h = 512
  TEST_ASSERT_TRUE(draw(cv, one(e)));
  TEST_ASSERT_TRUE(cv.get(0, 0));          // il riempimento copre lo schermo
  std::vector<uint8_t> m = el(1, {4, 0xFF, 4, 3});
  m[2] = 0x00; m[3] = 0xFE;                // mascotte a x = −512: niente, senza crash
  TEST_ASSERT_TRUE(draw(cv, one(m)));
  TEST_ASSERT_EQUAL(0, cv.countInk());
  // riquadri e cerchi a cavallo dei bordi e del tutto fuori: mai una chiamata fuori dal canvas
  std::vector<uint8_t> r1 = el(10, {8, 1, 8}), r2 = el(10, {2, 0, 4}), r3 = el(10, {1, 1, 0});
  std::vector<uint8_t> c1 = el(11, {3, 1}), c2 = el(11, {8, 0}), c3 = el(11, {0, 1}), c4 = el(11, {1, 1});
  xywh(r1, -512, -512, 512, 512);  // tocca solo fino a (−1, −1)
  xywh(r2, 240, 115, 512, 512);
  xywh(r3, -512, 0, 512, 122);
  xywh(c1, -10, -10, 30, 30);
  xywh(c2, 230, 100, 512, 512);
  xywh(c3, -512, 50, 512, 512);
  xywh(c4, 512, 512, 512, 512);
  BoundsSurface bs;
  TEST_ASSERT_TRUE(drawOn(bs, many({r1, r2, r3, c1, c2, c3, c4})));
  TEST_ASSERT_TRUE(bs.calls > 0);
  TEST_ASSERT_EQUAL(0, bs.outside);
  // e sul canvas: i pixel dentro ci sono
  TEST_ASSERT_TRUE(draw(cv, many({r2, c1})));
  TEST_ASSERT_TRUE(cv.get(245, 115) && cv.get(249, 116));  // bordo alto di 2 fino al margine destro
  TEST_ASSERT_FALSE(cv.get(240, 115));                      // angolo tagliato
  TEST_ASSERT_FALSE(cv.get(249, 121));                      // dentro: vuoto
  TEST_ASSERT_TRUE(cv.get(5, 5));
}

static void test_variant_choice_and_conditions() {
  fresh(0);
  Cv cv;
  // una sola variante «one»: con 0 sessioni non c'è niente da disegnare → schermata di serie
  std::vector<uint8_t> b = one(el(10, {0, 1, 0}));
  b[4] = 2;
  TEST_ASSERT_FALSE(draw(cv, b));
  fresh(1);
  TEST_ASSERT_TRUE(draw(cv, b));
  // condizione falsa: variante scelta, elemento non disegnato
  std::vector<uint8_t> c = one(el(10, {0, 1, 0}, 0, {5, 0}));  // pc: false, ma il PC c'è
  gM.sys.pc = true;
  TEST_ASSERT_TRUE(draw(cv, c));
  TEST_ASSERT_EQUAL(0, cv.countInk());
  // info nullo: ammesso
  {
    CanvasSurface<250, 122> sf(cv);
    TEST_ASSERT_TRUE(ui::drawThemeBlock(sf, kHeltec213, gM, 0, kNow, kWc, Screen::Home, b.data(), uint16_t(b.size()), nullptr, gSc));
  }
  // senza buffer del disegno (arena prestata alla partita): niente, la schermata è di serie
  {
    cv.clear(false);
    CanvasSurface<250, 122> sf(cv);
    ui::ThemeDrawInfo info;
    info.drawn = true;
    TEST_ASSERT_FALSE(ui::drawThemeBlock(sf, kHeltec213, gM, 0, kNow, kWc, Screen::Home, b.data(), uint16_t(b.size()), &info, nullptr));
    TEST_ASSERT_FALSE(info.drawn);
    TEST_ASSERT_EQUAL(0, cv.countInk());
  }
  // blocco rovinato: mai a metà
  c.resize(c.size() - 1);
  TEST_ASSERT_FALSE(draw(cv, c));
}

static void test_list_paging() {
  fresh(6);
  Cv cv;
  // lista 0,0,10,10 → righe alte 2 senza spazio: 5 righe per pagina, 2 pagine; riga = riquadro pieno 3×1
  std::vector<uint8_t> list = el(12, {2, 0, 5, 0, 1, 0});  // senza pallini: non sporcano il conto
  std::vector<uint8_t> row = el(10, {0, 1, 0});
  row[6] = 3; row[8] = 1;
  list.insert(list.end(), row.begin(), row.end());
  ui::ThemeDrawInfo info;
  TEST_ASSERT_TRUE(draw(cv, one(list), &info, 0));
  TEST_ASSERT_EQUAL(5, inkIn(cv, 0, 0, 3, 10) / 3);         // pagina 0: 5 righe
  TEST_ASSERT_EQUAL(5000, int(info.nextChange));
  TEST_ASSERT_TRUE(draw(cv, one(list), &info, 5000));
  TEST_ASSERT_EQUAL(1, inkIn(cv, 0, 0, 3, 9) / 3);          // pagina 1: la sesta
  TEST_ASSERT_EQUAL(10000, int(info.nextChange));
  fresh(0);
  TEST_ASSERT_TRUE(draw(cv, one(list), &info, 0));
  TEST_ASSERT_EQUAL(0, cv.countInk());
  TEST_ASSERT_EQUAL(0, int(info.nextChange));
}

static void test_list_page_dots() {
  fresh(6);
  Cv cv;
  std::vector<uint8_t> list = el(12, {2, 0, 5, 1, 1, 0});  // con i pallini, righe vuote (condizione falsa)
  std::vector<uint8_t> row = el(10, {0, 1, 0}, 0, {5, 1});
  list.insert(list.end(), row.begin(), row.end());
  xywh(list, 20, 20, 30, 10);  // 5 righe per pagina, 2 pagine; slot in (42, 27) e (46, 27)
  gM.sys.pc = false;
  TEST_ASSERT_TRUE(draw(cv, one(list), nullptr, 0));
  TEST_ASSERT_EQUAL(9 + 1, cv.countInk());
  TEST_ASSERT_EQUAL(9, inkIn(cv, 42, 27, 3, 3));  // pagina 0 piena
  TEST_ASSERT_TRUE(cv.get(47, 28));                // pagina 1: un punto
  TEST_ASSERT_TRUE(draw(cv, one(list), nullptr, 5000));
  TEST_ASSERT_TRUE(cv.get(43, 28));
  TEST_ASSERT_EQUAL(9, inkIn(cv, 46, 27, 3, 3));
}

static void test_qr_module_size() {
  fresh(0);
  Cv cv;
  // «ciao» → versione 1 (21 moduli); riquadro 46 × 60 → px = floor(46 / 23) = 2, quadrato di carta 46 × 46 in (5, 5)
  std::vector<uint8_t> q = el(7, {1, 0, 0, 0});
  xywh(q, 5, 5, 46, 60);
  std::vector<uint8_t> r = el(10, {0, 1, 0});
  xywh(r, 0, 0, 100, 100);
  TEST_ASSERT_TRUE(draw(cv, many({r, q}, {4, 'c', 'i', 'a', 'o'})));
  TEST_ASSERT_FALSE(cv.get(5, 5));     // bordo di carta di un modulo
  TEST_ASSERT_FALSE(cv.get(6, 6));
  TEST_ASSERT_FALSE(cv.get(50, 50));
  TEST_ASSERT_TRUE(cv.get(51, 5));     // fuori dal quadrato: il riquadro sotto
  TEST_ASSERT_TRUE(cv.get(4, 30));
  TEST_ASSERT_TRUE(cv.get(5, 51));     // h = 60 non conta: il quadrato è min(w, h)
  TEST_ASSERT_EQUAL(4 * 7, inkIn(cv, 7, 7, 14, 2));   // prima riga del cerchio di posizione: 7 moduli neri
  TEST_ASSERT_TRUE(cv.get(47, 7) && cv.get(48, 8));   // modulo (20, 0)
  TEST_ASSERT_FALSE(cv.get(49, 7));
  // testo vuoto: niente (nemmeno il quadrato di carta)
  std::vector<uint8_t> q0 = el(7, {1, 0, 0, 0});
  xywh(q0, 5, 5, 46, 46);
  TEST_ASSERT_TRUE(draw(cv, many({r, q0}, {0})));
  TEST_ASSERT_TRUE(cv.get(5, 5));
}

static std::vector<std::string> gTexts;
static void hook(const char *s, int, int, int, int, bool, Align, void *) { gTexts.push_back(s); }

static void test_text_goes_through_T() {
  fresh(1);
  Cv cv;
  // testo «{sessione.nome}» in 12/600 maiuscolo, largo 100 (C3: con la larghezza 10 di el() T() accorcerebbe a «S…»)
  std::vector<uint8_t> e = el(2, {12, 6, 0, 1, 14, 1, 1, 1, 0x00, 0x00});
  e[6] = 100;
  e[7] = 0;
  std::vector<uint8_t> b = one(e);
  ui::setTextHook(hook, nullptr);
  gTexts.clear();
  TEST_ASSERT_TRUE(draw(cv, b));
  ui::setTextHook(nullptr, nullptr);
  TEST_ASSERT_EQUAL(1, int(gTexts.size()));
  TEST_ASSERT_EQUAL_STRING("S0", gTexts[0].c_str());
}

static void test_mascot_rect_and_clock_wake() {
  fresh(1);
  Cv cv;
  ui::ThemeDrawInfo info;
  std::vector<uint8_t> m = el(1, {2, 0xFF, 4, 3});
  m[2] = 10; m[4] = 20;
  TEST_ASSERT_TRUE(draw(cv, one(m), &info));
  TEST_ASSERT_TRUE(info.hasMascot);
  TEST_ASSERT_EQUAL(10, info.mx);
  TEST_ASSERT_EQUAL(20, info.my);
  TEST_ASSERT_EQUAL(56, info.mw);
  TEST_ASSERT_EQUAL(50, info.mh);
  TEST_ASSERT_TRUE(inkIn(cv, 10, 20, 56, 50) > 0);
  TEST_ASSERT_EQUAL(0, int(info.nextChange));
  // orologio coi secondi: ridisegno al secondo dopo (kWc è a un secondo pieno)
  std::vector<uint8_t> clk = el(2, {12, 6, 0, 1, 14, 0, 1, 1, 0x81, 0x00});
  xywh(clk, 0, 0, 100, 20);
  TEST_ASSERT_TRUE(draw(cv, one(clk), &info));
  TEST_ASSERT_EQUAL(1000, int(info.nextChange - kNow));
  TEST_ASSERT_FALSE(info.hasMascot);
}

static std::vector<uint8_t> loadFixture(const char *name) {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  std::ifstream in((k != std::string::npos ? f.substr(0, k) : std::string("../")) + "protocol/themes/fixtures/" + name,
                   std::ios::binary);
  TEST_ASSERT_TRUE_MESSAGE(in.good(), name);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// I fixture del compilatore su ogni schermata, con 0..6 sessioni in tutti gli stati; poi i byte di un blocco cambiati
// uno alla volta: ogni blocco che passa checkBlock si disegna senza letture né scritture fuori (native_asan).
static void test_fixtures_and_flips_never_crash() {
  Cv cv;
  for (const char *n : {"prova.cthm", "elementi.cthm", "lista.cthm"}) {
    std::vector<uint8_t> f = loadFixture(n);
    for (int sc = 1; sc < int(theme::Screen::Count); sc++) {
      const uint8_t *b;
      uint16_t len;
      if (!theme::findScreen(f.data(), f.size(), theme::Screen(sc), b, len)) continue;
      std::vector<uint8_t> blk(b, b + len);
      for (int ns = 0; ns <= 6; ns++) {
        fresh(ns);
        for (int k = 0; k < ns; k++) gM.sessions[k].state = SessionState(k % int(SessionState::Count));
        cv.clear(false);
        CanvasSurface<250, 122> s(cv);
        ui::ThemeDrawInfo info;
        ui::drawThemeBlock(s, kHeltec213, gM, ns, kNow, kWc, theme::Screen(sc), blk.data(), len, &info, gSc);
      }
    }
  }
  std::vector<uint8_t> f = loadFixture("elementi.cthm");
  const uint8_t *b;
  uint16_t len;
  TEST_ASSERT_TRUE(theme::findScreen(f.data(), f.size(), Screen::Detail, b, len));
  std::vector<uint8_t> blk(b, b + len);
  fresh(3);
  int drawn = 0;
  for (size_t i = 0; i < blk.size(); i++) {
    const uint8_t keep = blk[i];
    for (int v = 0; v < 256; v += 5) {
      blk[i] = uint8_t(v);
      std::vector<uint8_t> exact(blk);  // copia a misura: fuori = fuori dal vettore
      cv.clear(false);
      CanvasSurface<250, 122> s(cv);
      ui::ThemeDrawInfo info;
      drawn += ui::drawThemeBlock(s, kHeltec213, gM, 1, kNow, kWc, Screen::Detail, exact.data(), uint16_t(exact.size()), &info, gSc);
    }
    blk[i] = keep;
  }
  TEST_ASSERT_TRUE(drawn > 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_rect_fill_and_radius);
  RUN_TEST(test_circle);
  RUN_TEST(test_dotted_line_and_invert);
  RUN_TEST(test_offscreen_elements_clip);
  RUN_TEST(test_variant_choice_and_conditions);
  RUN_TEST(test_list_paging);
  RUN_TEST(test_list_page_dots);
  RUN_TEST(test_qr_module_size);
  RUN_TEST(test_text_goes_through_T);
  RUN_TEST(test_mascot_rect_and_clock_wake);
  RUN_TEST(test_fixtures_and_flips_never_crash);
  return UNITY_END();
}
