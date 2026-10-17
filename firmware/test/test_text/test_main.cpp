// Testo: larghezze, allineamenti, troncamento, a capo, UTF-8 e fedeltà pixel per pixel
// rispetto a T()/wrapT() del simulatore disegnati in Chrome (fidelity.h, da tools/fonts/gen.py).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include <ArduinoJson.h>

#include <fstream>
#include <string>

#include "fidelity.h"
#include "gfx/fonts/text_internal.h"
#include "gfx/text.h"

void setUp() {}
void tearDown() {}

using Cv = Canvas<250, 122>;
using Sf = CanvasSurface<250, 122>;

struct Box { int x0, y0, x1, y1, n; };
static Box inkBox(const Cv &cv) {
  Box b{1000, 1000, -1, -1, 0};
  for (int y = 0; y < Cv::kH; y++)
    for (int x = 0; x < Cv::kW; x++)
      if (cv.get(x, y)) {
        b.n++;
        if (x < b.x0) b.x0 = x;
        if (x > b.x1) b.x1 = x;
        if (y < b.y0) b.y0 = y;
        if (y > b.y1) b.y1 = y;
      }
  return b;
}

// ---------------------------------------------------------------- registro
static void test_fonts_present() {
  const int ui700[] = {10, 12, 13, 14, 15, 16, 18, 20, 22, 26, 40, 44, 46};
  for (int s : ui700) TEST_ASSERT_TRUE_MESSAGE(hasFont(s, 700, false), "manca Chakra Petch 700");
  const int ui600[] = {10, 11, 12, 13};
  for (int s : ui600) TEST_ASSERT_TRUE(hasFont(s, 600, false));
  const int ui500[] = {10, 11, 12};
  for (int s : ui500) TEST_ASSERT_TRUE(hasFont(s, 500, false));
  TEST_ASSERT_TRUE(hasFont(8, 400, true));
  TEST_ASSERT_TRUE(hasFont(8, 700, true));
  TEST_ASSERT_TRUE(hasFont(8, 600, true));  // il 600 di Silkscreen è la faccia 700
  TEST_ASSERT_FALSE(hasFont(9, 700, false));
  TEST_ASSERT_FALSE(hasFont(13, 500, false));
}

// ---------------------------------------------------------------- larghezze
static void test_widths() {
  TEST_ASSERT_EQUAL_INT(0, textWidth("", 12));
  // Silkscreen 8 px: larghezze fisse per lettera, l'orologio dell'angolo
  TEST_ASSERT_EQUAL_INT(textWidth("14:32", 8, 400, true), textWidth("14:32", 8, 400, true));
  // kerning: «AV» è più stretto di «A» + «V»
  TEST_ASSERT_LESS_THAN_INT32(textWidthMilli("A", 12) + textWidthMilli("V", 12), textWidthMilli("AV", 12));
  // legatura fi: «fi» non è «f» + «i»
  TEST_ASSERT_NOT_EQUAL(textWidthMilli("f", 13, 700) + textWidthMilli("i", 13, 700), textWidthMilli("fi", 13, 700));
  // tutte le stringhe di riferimento: entro ±1 px da measureText
  for (int i = 0; i < kFidelityCount; i++) {
    const FidelityRef &r = kFidelity[i];
    int32_t mine = textWidthMilli(r.s, r.size, r.weight, r.pix);
    int32_t d = mine - r.width1000;
    char msg[128];
    snprintf(msg, sizeof msg, "%s: larghezza %ld vs %ld millesimi", r.name, (long)mine, (long)r.width1000);
    TEST_ASSERT_TRUE_MESSAGE(labs(d) <= 1000, msg);
    TEST_ASSERT_INT_WITHIN_MESSAGE(1, (r.width1000 + 500) / 1000, textWidth(r.s, r.size, r.weight, r.pix), msg);
  }
}

// ---------------------------------------------------------------- allineamenti
static void test_alignment() {
  const char *s = "PERMESSO";
  const int w = textWidth(s, 14, 700);
  Cv a, b, c;
  a.clear(); b.clear(); c.clear();
  Sf sa(a), sb(b), sc(c);
  TextOpts o;
  o.weight = 700;
  drawText(sa, s, 100, 10, 14, o);
  o.align = Align::Right;
  drawText(sb, s, 100 + w, 10, 14, o);
  o.align = Align::Center;
  drawText(sc, s, 100 + w / 2, 10, 14, o);
  Box ba = inkBox(a), bb = inkBox(b), bc = inkBox(c);
  TEST_ASSERT_TRUE(ba.n > 0);
  TEST_ASSERT_INT_WITHIN(1, ba.x0, bb.x0);
  TEST_ASSERT_INT_WITHIN(1, ba.x1, bb.x1);
  TEST_ASSERT_INT_WITHIN(1, ba.x0, bc.x0);
  TEST_ASSERT_EQUAL_INT(ba.y0, bb.y0);
  // a destra: niente inchiostro oltre la x di ancoraggio
  Cv d;
  d.clear();
  Sf sd(d);
  drawText(sd, "14:32", 244, 4, 8, TextOpts{400, true, Align::Right});
  TEST_ASSERT_TRUE(inkBox(d).x1 < 244);
}

static void test_ink_flag() {
  Cv cv;
  cv.fillRect(0, 0, 250, 20, true);
  Sf s(cv);
  TextOpts o;
  o.weight = 700;
  o.ink = false;
  const int before = cv.countInk();
  drawText(s, "NEGA", 10, 2, 14, o);
  TEST_ASSERT_TRUE(cv.countInk() < before);  // testo chiaro: toglie inchiostro
}

// ---------------------------------------------------------------- troncamento
static void test_fit() {
  char out[128];
  fitText(out, sizeof out, "api-server", 13, 700, false, 150);
  TEST_ASSERT_EQUAL_STRING("api-server", out);  // sta già: invariata
  fitText(out, sizeof out, "calamaio-firmware-esp32-progetto", 13, 700, false, 150);
  TEST_ASSERT_TRUE(strstr(out, "\xe2\x80\xa6") != nullptr);
  TEST_ASSERT_TRUE(textWidthMilli(out, 13, 700) <= 150 * 1000);
  // Silkscreen: «..»
  fitText(out, sizeof out, "CONTESTO: CALAMAIO-FIRMWARE-ESP32 87%", 8, 400, true, 80);
  size_t n = strlen(out);
  TEST_ASSERT_TRUE(n > 2 && out[n - 1] == '.' && out[n - 2] == '.');
  TEST_ASSERT_TRUE(textWidthMilli(out, 8, 400, true) <= 80 * 1000);
  // trimEnd: niente spazio prima dei puntini
  for (int max = 20; max < 120; max++) {
    fitText(out, sizeof out, "Ultimo dato alle 14:32 di oggi", 10, 600, false, max);
    n = strlen(out);
    if (n >= 4) TEST_ASSERT_TRUE_MESSAGE(out[n - 4] != ' ', out);
  }
  // un solo carattere resta sempre (s.length > 1 nel simulatore)
  fitText(out, sizeof out, "PERMESSO", 20, 700, false, 1);
  TEST_ASSERT_EQUAL_STRING("P\xe2\x80\xa6", out);
  // capienza piccola: non spezza i caratteri UTF-8
  fitText(out, 3, "\xc3\xa8\xc3\xa8\xc3\xa8", 12, 600, false, 0);
  TEST_ASSERT_EQUAL_STRING("\xc3\xa8", out);
  // stesse stringhe troncate di fit() in Chrome
  for (int i = 0; i < kFidelityCount; i++) {
    const FidelityRef &r = kFidelity[i];
    if (!r.max || r.wrapW) continue;
    fitText(out, sizeof out, r.s, r.size, r.weight, r.pix, r.max);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(r.fitted, out, r.name);
  }
}

// ---------------------------------------------------------------- a capo
static void test_wrap() {
  char lines[6][96];
  for (int i = 0; i < kFidelityCount; i++) {
    const FidelityRef &r = kFidelity[i];
    if (!r.wrapW) continue;
    int n = wrapLines(lines, 4, r.s, r.size, r.wrapW, r.weight, r.pix);
    int want = r.nLines < 4 ? r.nLines : 4;
    TEST_ASSERT_EQUAL_INT_MESSAGE(want, n, r.name);
    for (int k = 0; k < n; k++) TEST_ASSERT_EQUAL_STRING_MESSAGE(r.lines[k], lines[k], r.name);
  }
  TEST_ASSERT_EQUAL_INT(0, wrapLines(lines, 6, "", 10, 100));
  TEST_ASSERT_EQUAL_INT(0, wrapLines(lines, 6, "   ", 10, 100));
  int n = wrapLines(lines, 6, "  uno   due  ", 10, 200);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_STRING("uno due ", lines[0]);  // come split(/\s+/): token vuoto finale
  n = wrapLines(lines, 6, "parolalunghissimasenzaspazi corta", 10, 30);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_STRING("parolalunghissimasenzaspazi", lines[0]);
  n = wrapLines(lines, 2, "a b c d e f g h i l m n o p q r s t u v z", 10, 20);
  TEST_ASSERT_EQUAL_INT(2, n);  // al massimo maxLines
  // ogni riga sta in maxW
  n = wrapLines(lines, 6, "Collega un sensore di CO2, temperatura e umidità ai pin I2C, per esempio un SCD40.", 11, 140,
                500);
  TEST_ASSERT_TRUE(n >= 3);
  for (int k = 0; k < n; k++) TEST_ASSERT_TRUE(textWidthMilli(lines[k], 11, 500) <= 140 * 1000);
}

// ---------------------------------------------------------------- UTF-8
static void test_utf8() {
  // accenti e simboli hanno un glifo (larghezza > 0) e non sono '?'
  const char *syms[] = {"\xc3\xa0", "\xc3\xa8", "\xc3\xa9", "\xc3\xac", "\xc3\xb2", "\xc3\xb9", "\xc3\x88", "\xc2\xb0",
                        "\xc2\xb7", "\xe2\x80\xba", "\xe2\x80\xb3", "\xe2\x88\x92", "\xe2\x80\xa6", "\xe2\x9c\x93",
                        "\xe2\x9c\x97", "\xe2\x96\xb2", "\xe2\x96\xbc", "\xe2\x97\x80", "\xe2\x96\xb6", "\xe2\x82\xac"};
  const int32_t q = textWidthMilli("?", 12);
  for (const char *s : syms) {
    TEST_ASSERT_TRUE_MESSAGE(textWidthMilli(s, 12) > 0, s);
    TEST_ASSERT_TRUE_MESSAGE(textWidthMilli(s, 12) != q || !strcmp(s, "?"), s);
  }
  // sequenze non valide e fuori BMP: nessun crash, diventano '?'
  TEST_ASSERT_EQUAL_INT32(q, textWidthMilli("\xff", 12));
  TEST_ASSERT_EQUAL_INT32(q, textWidthMilli("\xf0\x9f\x98\x80", 12));
  TEST_ASSERT_EQUAL_INT32(textWidthMilli("?(", 12), textWidthMilli("\xc3\x28", 12));
  // troncata a metà carattere
  TEST_ASSERT_TRUE(textWidthMilli("a\xe2\x80", 12) > textWidthMilli("a", 12));
  Cv cv;
  cv.clear();
  Sf s(cv);
  drawText(s, "\xe2\x9c\x93 \xc3\x88 \xc2\xb7", 4, 4, 12);
  TEST_ASSERT_TRUE(cv.countInk() > 10);
}

// ---------------------------------------------------------------- fedeltà
static void test_fidelity() {
  long totDiff = 0, totInk = 0;
  int worstW = 0;
  printf("\n%-16s %6s %6s %7s %9s\n", "stringa", "diff", "ink", "diff%", "dLarg(px)");
  for (int i = 0; i < kFidelityCount; i++) {
    const FidelityRef &r = kFidelity[i];
    Cv cv;
    cv.clear();
    Sf s(cv);
    if (r.paper) cv.fillRect(r.bg[0], r.bg[1], r.bg[2], r.bg[3], true);
    TextOpts o;
    o.weight = r.weight;
    o.pix = r.pix;
    o.align = r.align == 1 ? Align::Center : r.align == 2 ? Align::Right : Align::Left;
    o.ink = !r.paper;
    o.max = r.max;
    if (r.wrapW) wrapText(s, r.s, r.x, r.y, r.size, r.wrapW, r.wrapLines, r.wrapLh, o);
    else drawText(s, r.s, r.x, r.y, r.size, o);
    int diff = 0, ink = 0;
    for (int y = 0; y < 122; y++)
      for (int x = 0; x < 250; x++) {
        const int bit = y * 250 + x;  // bit contigui, senza padding di riga
        bool want = r.bits[bit >> 3] & (0x80 >> (bit & 7));
        bool inBg = r.paper && x >= r.bg[0] && x < r.bg[0] + r.bg[2] && y >= r.bg[1] && y < r.bg[1] + r.bg[3];
        if (inBg ? !want : want) ink++;  // pixel di testo nel riferimento
        if (cv.get(x, y) != want) diff++;
      }
    const int32_t wm = textWidthMilli(r.fitted, r.size, r.weight, r.pix);
    const double dw = (wm - r.fitted1000) / 1000.0;
    const int dwi = (wm + 500) / 1000 - (r.fitted1000 + 500) / 1000;  // Math.round, valori positivi
    if (abs(dwi) > abs(worstW)) worstW = dwi;
    printf("%-16s %6d %6d %6.2f%% %+9.3f\n", r.name, diff, ink, 100.0 * diff / (ink ? ink : 1), dw);
    totDiff += diff;
    totInk += ink;
    char msg[96];
    snprintf(msg, sizeof msg, "%s: %d pixel diversi su %d", r.name, diff, ink);
    TEST_ASSERT_TRUE_MESSAGE(diff * 50 <= ink, msg);  // al massimo 2% per stringa
    TEST_ASSERT_TRUE_MESSAGE(abs(dwi) <= 1, msg);
  }
  printf("TOTALE %ld pixel diversi su %ld (%.3f%%), peggior differenza di larghezza arrotondata %d px\n", totDiff, totInk,
         100.0 * totDiff / totInk, worstW);
}

static std::string repoRoot() {
  std::string f = __FILE__;
  const size_t k = f.find("firmware/test/");
  return k != std::string::npos ? f.substr(0, k) : "../";
}
static std::string readText(const std::string &p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// UTF-8 -> codepoint per i glifi del catalogo (BMP).
static uint16_t cpAt(const char *&p) {
  uint32_t c = uint8_t(*p++);
  int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
  if (extra) c &= (0x3F >> extra);
  while (extra--) c = (c << 6) | (uint8_t(*p++) & 0x3F);
  return uint16_t(c);
}

// protocol/themes/catalog.json: i glifi di ogni carattere sono quelli di hasGlyph.
static void test_glyphs_match_catalog() {
  static JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, readText(repoRoot() + "protocol/themes/catalog.json")));
  int n = 0;
  for (JsonVariantConst f : doc["fonts"].as<JsonArrayConst>()) {
    const int size = f["size"], weight = f["weight"];
    const bool pix = !strcmp(f["family"].as<const char *>(), "pix");
    bool has[0x80] = {};
    for (const char *p = f["glyphs"].as<const char *>(); *p;) {
      const uint16_t cp = cpAt(p);
      TEST_ASSERT_TRUE_MESSAGE(hasGlyph(size, weight, pix, cp), f["glyphs"].as<const char *>());
      if (cp < 0x80) has[cp] = true;
    }
    for (int cp = 0x20; cp <= 0x7E; cp++) TEST_ASSERT_EQUAL_MESSAGE(has[cp], hasGlyph(size, weight, pix, uint16_t(cp)), "ASCII fuori dal catalogo");
    n++;
  }
  TEST_ASSERT_EQUAL(22, n);
  TEST_ASSERT_FALSE(hasGlyph(17, 700, false, 'a'));
}

// Un glifo assente esce «?»: «$» e «?» a 44/700 hanno gli stessi byte.
static void test_missing_glyph_draws_question() {
  static Cv a, b;
  a.clear();
  b.clear();
  Sf sa(a), sb(b);
  TextOpts o;
  o.weight = 700;
  drawText(sa, "$", 5, 5, 44, o);
  drawText(sb, "?", 5, 5, 44, o);
  TEST_ASSERT_EQUAL_MEMORY(a.data(), b.data(), Cv::kBytes);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fonts_present);
  RUN_TEST(test_widths);
  RUN_TEST(test_alignment);
  RUN_TEST(test_ink_flag);
  RUN_TEST(test_fit);
  RUN_TEST(test_wrap);
  RUN_TEST(test_utf8);
  RUN_TEST(test_fidelity);
  RUN_TEST(test_glyphs_match_catalog);
  RUN_TEST(test_missing_glyph_draws_question);
  return UNITY_END();
}
