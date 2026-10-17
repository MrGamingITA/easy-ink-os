// Prova dal vivo del driver veloce (env fastink-demo): NON è il firmware Calamaio. La libreria disegna come in Calamaio,
// poi cal::fastink anima quadrato e testo coi due profili, poi il pannello torna alla libreria.
// PRG = demo completa (~3 min); seriale 115200, «h» = aiuto.
#include <Arduino.h>
#include <heltec-eink-modules.h>
#include <stdlib.h>

#include "gfx/canvas.h"
#include "gfx/text.h"
#include "hw/fastink.h"

using cal::fastink::Params;
using cal::fastink::Profile;
namespace fi = cal::fastink;

namespace {

class Panel : public EInkDisplay_WirelessPaperV1_2 {
 public:
  SPIClass *bus() {
    begin();
    return display_spi;
  }
};

Panel display;
Canvas<fi::kScrW, fi::kScrH> cv;
CanvasSurface<fi::kScrW, fi::kScrH> surf(cv);
Profile prof = Profile::Clean;
Params tuned = fi::paramsFor(Profile::Clean);
bool custom = false;
int frames = 150;
String line;

// Disegno di serie, come Calamaio: canvas → libreria, completo.
void showLibrary(const char *big, const char *small) {
  cv.clear();
  TextOpts b;
  b.weight = 700;
  drawText(surf, big, 8, 28, 16, b);
  TextOpts s;
  s.weight = 500;
  drawText(surf, small, 8, 70, 11, s);
  display.clearMemory();
  for (int y = 0; y < fi::kScrH; y++)
    for (int x = 0; x < fi::kScrW; x++)
      if (cv.get(x, y)) display.drawPixel(x, y, BLACK);
  display.update();
}

void onReturn() { display.fastmodeOff(); }  // reset + configurazione completa; il completo lo fa showLibrary()

int bounce(int v, int range) {
  if (range <= 0) return 0;
  const int p = v % (2 * range);
  return p < range ? p : 2 * range - p;
}

void drawSquare(int f) {
  cv.clear();
  cv.fillRect(bounce(f * 6, 250 - 24), bounce(f * 4, 122 - 24), 24, 24, true);
}

void drawScroll(int f) {
  cv.clear();
  TextOpts o;
  o.weight = 700;
  const int w = textWidth("Calamaio", 26, 700);
  drawText(surf, "Calamaio", 250 - (f * 5) % (250 + w), 44, 26, o);
}

void drawBoth(int f) {
  drawScroll(f);
  cv.fillRect(bounce(f * 6, 226), bounce(f * 4, 98), 24, 24, true);
}

bool prg() { return digitalRead(0) == LOW; }

bool startFast() {
  if (!fi::begin(prof)) {
    Serial.println("[demo] begin() non riuscito");
    return false;
  }
  if (custom) fi::setParams(tuned);
  const Params &p = fi::params();
  Serial.printf("[demo] profilo %s: N%u F%u P%u FR%u, bordi %u, pulizia %u, 0x22=%02X, heap libero %u\n",
                fi::profileName(prof), p.budget, p.tp, p.phases, p.fr, p.edge, p.polish, fi::tickCtl(), ESP.getFreeHeap());
  return true;
}

// Un tick per fotogramma (come lo spike), poi fino a fermo (pulizia compresa), poi immagine ferma da guardare.
void animate(const char *name, void (*draw)(int), int holdS) {
  uint32_t maxUs = 0, sumUs = 0;
  const uint32_t t0 = millis();
  int f = 0;
  for (; f < frames; f++) {
    draw(f);
    fi::submit(cv.data());
    fi::tick();
    const uint32_t us = fi::lastTickUs();
    sumUs += us;
    if (us > maxUs) maxUs = us;
    if (prg() || Serial.available()) break;
  }
  const uint32_t ms = millis() - t0;
  int after = 0;
  while (fi::tick() && after < 100) after++;
  Serial.printf("[demo] %s, %s: %d fotogrammi in %lu ms = %.1f al secondo; tick medio %.1f ms, massimo %.1f ms; "
                "%d tick per fermarsi\n", name, fi::profileName(prof), f, static_cast<unsigned long>(ms),
                f && ms ? f * 1000.0f / ms : 0.0f, f ? sumUs / 1000.0f / f : 0.0f, maxUs / 1000.0f, after);
  Serial.printf("[demo] immagine ferma per %d s: guarda scie e frange\n", holdS);
  delay(holdS * 1000UL);
}

void demoProfile(Profile p, const char *label) {
  prof = p;
  custom = false;
  showLibrary(label, "quadrato, testo, insieme");
  delay(2000);
  if (!startFast()) return;
  animate("quadrato", drawSquare, 15);
  animate("testo", drawScroll, 10);
  animate("insieme", drawBoth, 10);
  fi::end();
}

void demo() {
  demoProfile(Profile::Clean, "PROFILO PULITO");
  demoProfile(Profile::Smooth, "PROFILO FLUIDO");
  showLibrary("FINE", "la libreria disegna di nuovo");
}

void help() {
  Serial.println("[demo] a demo completa · c/s profilo pulito/fluido · q quadrato · x testo · y insieme");
  Serial.println("[demo] e<n> spinte ai bordi · p<n> pulizia · n<n> budget · f<n> F · r<n> FR · g<n> fotogrammi");
  Serial.println("[demo] u<hex> valore di 0x22 del tick · t costo fisso (10 tick senza spinte) · z restituisci il pannello");
}

void tune(char k, int v) {
  tuned = fi::active() ? fi::params() : (custom ? tuned : fi::paramsFor(prof));
  if (k == 'e') tuned.edge = uint8_t(v);
  if (k == 'p') tuned.polish = uint8_t(v);
  if (k == 'n') tuned.budget = uint8_t(v);
  if (k == 'f') tuned.tp = uint8_t(v);
  if (k == 'r') tuned.fr = uint8_t(v);
  custom = true;
  if (fi::active()) fi::setParams(tuned);
  Serial.printf("[demo] N%u F%u FR%u bordi %u pulizia %u\n", tuned.budget, tuned.tp, tuned.fr, tuned.edge, tuned.polish);
}

void command(const String &c) {
  if (!c.length()) return;
  const char k = c[0];
  const int v = atoi(c.c_str() + 1);
  if (c == "h") help();
  else if (c == "a") demo();
  else if (c == "c" || c == "s") {
    prof = c == "c" ? Profile::Clean : Profile::Smooth;
    custom = false;
    Serial.printf("[demo] profilo %s\n", fi::profileName(prof));
  } else if (c == "q" || c == "x" || c == "y") {
    if (!fi::active() && !startFast()) return;
    animate(c == "q" ? "quadrato" : c == "x" ? "testo" : "insieme", c == "q" ? drawSquare : c == "x" ? drawScroll : drawBoth, 10);
  } else if (k == 'e' || k == 'p' || k == 'n' || k == 'f' || k == 'r') tune(k, v);
  else if (k == 'g') frames = v > 0 ? v : frames;
  else if (k == 'u') {
    fi::setTickCtl(uint8_t(strtol(c.c_str() + 1, nullptr, 16)));
    Serial.printf("[demo] 0x22 del tick = %02X\n", fi::tickCtl());
  } else if (c == "t") {
    if (!fi::active() && !startFast()) return;
    uint32_t sum = 0;
    int ok = 0;
    for (int i = 0; i < 10; i++) {
      const uint32_t us = fi::measureHoldTick();
      if (us) { sum += us; ok++; }
    }
    Serial.printf("[demo] costo fisso con 0x22=%02X: %.1f ms (media di %d); lo schermo NON deve cambiare\n", fi::tickCtl(),
                  ok ? sum / 1000.0f / ok : 0.0f, ok);
  } else if (c == "z") {
    fi::end();
    showLibrary("PANNELLO RESTITUITO", "completo di serie dalla libreria");
  } else Serial.println("[demo] comando sconosciuto (h = aiuto)");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(0, INPUT_PULLUP);
  display.landscape();
  fi::attachBus(display.bus(), onReturn, &Serial);
  Serial.println("\n[demo] FASTINK: prova del driver veloce (non è Calamaio). PRG = demo completa, h = aiuto");
  showLibrary("PROVA FASTINK", "PRG: demo completa (circa 3 minuti)");
}

void loop() {
  if (prg()) {
    delay(30);
    while (prg()) delay(10);
    demo();
  }
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      command(line);
      line = "";
    } else {
      line += ch;
    }
  }
  delay(5);
}
