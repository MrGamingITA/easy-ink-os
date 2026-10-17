// SPIKE usa e getta · fastink: prova di refresh veloce con LUT corta e budget per pixel.
// Menu seriale a 115200 (h = aiuto). PRG da fermo = demo automatica. Nessun Wi-Fi, NVS non usata.
#ifdef ARDUINO
#include <Arduino.h>
#include "canvas.h"
#include "engine.h"
#include "lut.h"
#include "panel.h"

using namespace fi;

static constexpr int kPinPrg = 0;
static constexpr uint32_t kIdleCleanMs = 5UL * 60 * 1000;  // dopo 5 min fermo: refresh completo di pulizia

struct Params {
  LutParams lut;
  uint8_t budget = 6;
  uint8_t spiMhz = 10;
  uint8_t power = 0;      // 0 = 0xC7/0xCF a ogni tick; 1 = analogico sempre acceso (0xC0 + 0x04/0x0C)
  uint8_t dmode = 1;      // modo di display 1 o 2
  uint8_t volt = 0;       // 0 = tensioni caricate da OTP; 1 = valori Waveshare 2in13_V3
  uint16_t lutLen = 153;  // 153 (datasheet) o 159 (coda accodata a 0x32)
  uint16_t frames = 100;
  bool otpAnim = false;   // animazioni col parziale OTP (riferimento)
  bool gate250 = false;   // 0x01 F9 00 00
};
static Params P;

static Engine eng(6);
static Canvas cv;
static uint8_t bwA[kRamBytes], redA[kRamBytes], bwB[kRamBytes], redB[kRamBytes];
static uint8_t onPanel[kRamBytes];  // ultima immagine nota sul pannello
static bool otpLoaded = false, lutLoaded = false, analogOn = false, dirty = false, aborted = false;
static uint32_t lastActivity = 0;

// ---------- utilità ----------
static bool abortRequested() {
  if (aborted) return true;
  if (Serial.available() || digitalRead(kPinPrg) == LOW) {
    while (Serial.available()) Serial.read();
    aborted = true;
    Serial.println("! interrotto");
  }
  return aborted;
}

static bool waitMs(uint32_t ms) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms)
    if (abortRequested()) return false;
  return true;
}

static void ensureAwake() {
  if (panel::awake()) return;
  panel::wake((uint32_t)P.spiMhz * 1000000UL);
  if (P.gate250) panel::setGateCount250();
  otpLoaded = lutLoaded = analogOn = false;
}

static void finish() {
  if (panel::awake() && analogOn) {
    panel::waitIdle(panel::trigger(panel::kUpdAnalogOff), 2000);
    analogOn = false;
  }
  panel::sleep();
  lastActivity = millis();
}

static uint8_t tickCtl() {
  if (P.power == 1) return P.dmode == 2 ? panel::kUpdDispMode2 : panel::kUpdDispMode1;
  return P.dmode == 2 ? panel::kUpdRegMode2 : panel::kUpdRegMode1;
}

static void writeLut(const LutParams &lp) {
  uint8_t lut[kLut159];
  const size_t n = buildTickLut(lp, lut, P.lutLen == 159);
  panel::cmdData(0x32, lut, n);
  panel::waitIdle(micros(), 1000);
}

static void applyVoltages() {
  if (P.volt != 1) return;  // 0: restano quelle caricate da OTP
  panel::cmd1(0x3F, kWaveshareV3Tail[0]);
  panel::cmd1(0x03, kWaveshareV3Tail[1]);
  panel::cmdData(0x04, kWaveshareV3Tail + 2, 3);
  panel::cmd1(0x2C, kWaveshareV3Tail[5]);
}

// Prepara il percorso veloce: WS da OTP (tensioni e VCOM giuste), poi solo la LUT nel registro
static bool prepareFast() {
  ensureAwake();
  if (!otpLoaded) {
    if (!panel::waitIdle(panel::trigger(panel::kUpdLoadOtpOnly), 3000)) {
      Serial.println("! BUSY bloccato su 0xB1");
      return false;
    }
    otpLoaded = true;
    lutLoaded = false;
    applyVoltages();
  }
  if (!lutLoaded) {
    writeLut(P.lut);
    lutLoaded = true;
  }
  panel::setBorder(0x80);  // bordo a VCOM: fermo
  if (P.power == 1 && !analogOn) {
    panel::waitIdle(panel::trigger(panel::kUpdAnalogOn), 2000);
    analogOn = true;
  }
  return true;
}

// ---------- refresh OTP ----------
static void fullRefresh(const uint8_t *img) {
  ensureAwake();
  if (analogOn) { panel::waitIdle(panel::trigger(panel::kUpdAnalogOff), 2000); analogOn = false; }
  panel::setOtpDisplayOption();
  panel::setBorder(0x01);  // come configFull() di heltec-eink-modules
  panel::writePlane(0x24, img);
  panel::writePlane(0x26, img);
  const uint32_t dt = panel::waitIdle(panel::trigger(panel::kUpdFullOtp), 10000);
  if (!dt) Serial.println("! BUSY bloccato nel refresh completo");
  otpLoaded = true;   // WS ricaricato da OTP
  lutLoaded = false;  // la nostra LUT è stata sovrascritta
  eng.syncSaturated(img);
  memcpy(onPanel, img, kRamBytes);
  dirty = false;
  Serial.printf("full OTP (0xF7): %.1f ms\n", dt / 1000.0f);
}

static void fullWhite() {
  cv.clear(true);
  fullRefresh(cv.buf);
}

static uint32_t otpPartial(const uint8_t *img) {
  ensureAwake();
  if (analogOn) { panel::waitIdle(panel::trigger(panel::kUpdAnalogOff), 2000); analogOn = false; }
  panel::setOtpDisplayOption();
  panel::setBorder(0x80);  // come configPartial()
  panel::writePlane(0x24, img);
  panel::writePlane(0x26, onPanel);
  const uint32_t dt = panel::waitIdle(panel::trigger(panel::kUpdPartOtp), 5000);
  otpLoaded = true;
  lutLoaded = false;
  memcpy(onPanel, img, kRamBytes);
  eng.syncSaturated(img);
  dirty = true;
  return dt;
}

// ---------- animazioni ----------
typedef void (*DrawFn)(Canvas &, int f, int frames);

static int bounce(int v, int range) {
  if (range <= 0) return 0;
  const int p = v % (2 * range);
  return p < range ? p : 2 * range - p;
}

static void drawSquare(Canvas &c, int f, int) {
  c.clear(true);
  c.rect(bounce(f * 5, kScrW - 24), bounce(f * 3, kScrH - 24), 24, 24, true);
}

static void drawBar(Canvas &c, int f, int) {
  c.clear(true);
  c.rect(0, bounce(f * 4, kScrH - 10), kScrW, 10, true);
}

static void drawChecker(Canvas &c, int f, int) {
  // ogni stato resta N tick: la transizione completa ha il tempo di finire
  const int phase = (f / (P.budget ? P.budget : 1)) & 1;
  c.clear(true);
  for (int y = 0; y < kScrH; y += 16)
    for (int x = 0; x < kScrW; x += 16)
      if (((x / 16 + y / 16 + phase) & 1) == 0) c.rect(x, y, 16, 16, true);
}

static void drawText(Canvas &c, int f, int) {
  static const char *kMsg = "Calamaio";
  const int w = Canvas::textWidth(kMsg, 4);
  c.clear(true);
  c.text(kScrW - (f * 5) % (kScrW + w), 45, kMsg, 4);
}

// Fantasmi: scritta e quadrato, poi metà sinistra nera, poi metà sinistra bianca e scacchiera a destra.
static void drawGhost(Canvas &c, int f, int frames) {
  const int seg = frames > 0 ? (f * 3) / frames : 2;
  c.clear(true);
  if (seg == 0) {
    c.text(6, 20, "GHOST", 4);
    c.rect(125 + bounce(f * 5, 100), bounce(f * 3, 98), 24, 24, true);
  } else if (seg == 1) {
    c.rect(0, 0, 125, kScrH, true);
  } else {
    for (int y = 0; y < 104; y += 8)
      for (int x = 125; x < kScrW; x += 8)
        if (((x / 8 + y / 8) & 1) == 0) c.rect(x, y, 8, 8, true);
    char s[32];
    snprintf(s, sizeof s, "F%u P%u N%u FR%u", P.lut.tp, P.lut.phases, P.budget, P.lut.fr);
    c.text(130, 110, s, 1);
  }
}

static const char *gLabel1 = "", *gLabel2 = "";
static void drawLabel(Canvas &c, int, int) {
  c.clear(true);
  c.text((kScrW - Canvas::textWidth(gLabel1, 3)) / 2, 30, gLabel1, 3);
  c.text((kScrW - Canvas::textWidth(gLabel2, 2)) / 2, 75, gLabel2, 2);
}

struct RunResult {
  float fps = 0, avgMs = 0, maxMs = 0, avgUpd = 0, avgSpi = 0;
  int frames = 0;
};

// Ciclo veloce a doppio buffer: mentre il pannello lavora si calcola il tick seguente.
// Dopo l'ultimo fotogramma si continua senza cambiare bersaglio finché tutti i pixel arrivano.
static RunResult runFast(DrawFn draw, int frames, const char *name, bool verbose) {
  RunResult R;
  if (!prepareFast()) return R;
  const uint8_t ctl = tickCtl();
  uint8_t *bw[2] = {bwA, bwB}, *rd[2] = {redA, redB};
  int cur = 0;
  draw(cv, 0, frames);
  eng.setTarget(cv.buf);
  TickStats st = eng.tick(bw[0], rd[0]);
  uint64_t sumUpd = 0, sumSpi = 0;
  const uint32_t tStart = micros();
  uint32_t tAnimEnd = 0;
  for (int f = 0;; f++) {
    const uint32_t t0 = micros();
    panel::writePlane(0x24, bw[cur]);
    panel::writePlane(0x26, rd[cur]);
    const uint32_t tSpi = micros() - t0;
    const uint32_t tu = panel::trigger(ctl);
    const uint32_t c0 = micros();
    if (f + 1 < frames) {
      draw(cv, f + 1, frames);
      eng.setTarget(cv.buf);
    }
    const TickStats nst = eng.tick(bw[cur ^ 1], rd[cur ^ 1]);
    const uint32_t tCpu = micros() - c0;
    const uint32_t tUpd = panel::waitIdle(tu, 3000);
    if (!tUpd) {
      Serial.println("! BUSY bloccato: fermo");
      break;
    }
    const uint32_t period = micros() - t0;
    if (f < frames) {
      sumUpd += tUpd;
      sumSpi += tSpi;
      if (period / 1000.0f > R.maxMs) R.maxMs = period / 1000.0f;
      R.frames = f + 1;
      if (verbose)
        Serial.printf("%s f=%d ms=%.1f spi=%.1f upd=%.1f cpu=%.1f px=%u\n", name, f, period / 1000.0f, tSpi / 1000.0f,
                      tUpd / 1000.0f, tCpu / 1000.0f, (unsigned)st.driven());
      if (f + 1 == frames) tAnimEnd = micros();
    }
    cur ^= 1;
    st = nst;
    if (f + 1 >= frames && st.driven() == 0) break;
    if (f > frames + 3 * Engine::kMaxBudget) break;
    if (abortRequested()) break;
  }
  if (R.frames > 0) {
    const uint32_t total = (tAnimEnd ? tAnimEnd : micros()) - tStart;
    R.avgMs = total / 1000.0f / R.frames;
    R.fps = 1000.0f / R.avgMs;
    R.avgUpd = sumUpd / 1000.0f / R.frames;
    R.avgSpi = sumSpi / 1000.0f / R.frames;
  }
  memcpy(onPanel, eng.target(), kRamBytes);
  dirty = true;
  if (verbose)
    Serial.printf("== %s: %d fotogrammi, media %.1f ms (%.1f fps), max %.1f ms, upd %.1f ms, spi %.1f ms | "
                  "F%u P%u q%u N%u FR%u w%u d%u v%u L%u k%u\n",
                  name, R.frames, R.avgMs, R.fps, R.maxMs, R.avgUpd, R.avgSpi, P.lut.tp, P.lut.phases, P.lut.rest,
                  P.budget, P.lut.fr, P.power, P.dmode, P.volt, P.lutLen, P.spiMhz);
  return R;
}

static RunResult runOtp(DrawFn draw, int frames, const char *name) {
  RunResult R;
  uint64_t sum = 0;
  for (int f = 0; f < frames; f++) {
    draw(cv, f, frames);
    const uint32_t t0 = micros();
    const uint32_t dt = otpPartial(cv.buf);
    const uint32_t period = micros() - t0;
    if (!dt) { Serial.println("! BUSY bloccato"); break; }
    sum += period;
    if (period / 1000.0f > R.maxMs) R.maxMs = period / 1000.0f;
    R.frames = f + 1;
    Serial.printf("%s[OTP] f=%d ms=%.1f upd=%.1f\n", name, f, period / 1000.0f, dt / 1000.0f);
    if (abortRequested()) break;
  }
  if (R.frames) {
    R.avgMs = sum / 1000.0f / R.frames;
    R.fps = 1000.0f / R.avgMs;
  }
  Serial.printf("== %s[OTP]: %d fotogrammi, media %.1f ms (%.2f fps), max %.1f ms\n", name, R.frames, R.avgMs, R.fps,
                R.maxMs);
  return R;
}

static RunResult runAnim(DrawFn draw, int frames, const char *name) {
  return P.otpAnim ? runOtp(draw, frames, name) : runFast(draw, frames, name, true);
}

static void showLabel(const char *l1, const char *l2) {
  gLabel1 = l1;
  gLabel2 = l2;
  if (P.otpAnim) runOtp(drawLabel, 1, "label");
  else runFast(drawLabel, 1, "label", false);
}

// ---------- misure senza spinta ----------
// Tutti i pixel "fermi" (LUT2/LUT3 = VSS): il pannello NON deve cambiare. Serve a misurare
// il costo fisso e la durata di un fotogramma per ogni FR.
static float timeHold(const LutParams &lp, int reps) {
  writeLut(lp);
  lutLoaded = false;
  uint64_t sum = 0;
  for (int i = 0; i < reps; i++) {
    const uint32_t dt = panel::waitIdle(panel::trigger(tickCtl()), 5000);
    if (!dt) return -1;
    sum += dt;
  }
  return sum / 1000.0f / reps;
}

static void calibrate(bool sweepFr) {
  if (!prepareFast()) return;
  Engine::holdPlanes(onPanel, bwA, redA);
  panel::writePlane(0x24, bwA);
  panel::writePlane(0x26, redA);
  Serial.println("# calibrazione senza spinta: lo schermo NON deve cambiare");
  if (!sweepFr) {
    static const uint8_t tps[] = {1, 2, 4, 8, 16, 32};
    float t1 = 0, tN = 0;
    for (uint8_t tp : tps) {
      LutParams lp = P.lut;
      lp.tp = tp; lp.phases = 1; lp.rest = 0;
      const float ms = timeHold(lp, 3);
      Serial.printf("hold FR%u TP=%u: %.2f ms\n", P.lut.fr, tp, ms);
      if (tp == 1) t1 = ms;
      if (tp == 32) tN = ms;
    }
    const float frame = (tN - t1) / 31.0f;
    Serial.printf("=> fotogramma ~%.2f ms (%.1f Hz), costo fisso ~%.2f ms (FR%u, w%u)\n", frame,
                  frame > 0 ? 1000.0f / frame : 0, t1 - frame, P.lut.fr, P.power);
  } else {
    for (uint8_t fr = 0; fr < 8; fr++) {
      LutParams lp = P.lut;
      lp.fr = fr; lp.phases = 1; lp.rest = 0;
      lp.tp = 1;
      const float a = timeHold(lp, 2);
      lp.tp = 17;
      const float b = timeHold(lp, 2);
      Serial.printf("FR%u: TP1 %.2f ms, TP17 %.2f ms => fotogramma ~%.2f ms\n", fr, a, b, (b - a) / 16.0f);
    }
  }
}

// ---------- registri (sperimentale) ----------
static void readRegs() {
  ensureAwake();
  uint8_t r[11];
  panel::readRegister(0x2D, r, 11);
  Serial.print("0x2D (VCOM OTP sel, VCOM, modo x5, versione WS x4):");
  for (uint8_t v : r) Serial.printf(" %02X", v);
  Serial.println();
  panel::readRegister(0x2E, r, 10);
  Serial.print("0x2E (User ID):");
  for (int i = 0; i < 10; i++) Serial.printf(" %02X", r[i]);
  Serial.println("\n(sperimentale: se tutto FF/00 la lettura non funziona su questa scheda)");
}

// ---------- demo automatica ----------
struct Preset {
  const char *name;
  DrawFn draw;
  uint8_t tp, phases, budget;
};

static void autoRun() {
  aborted = false;
  const Params saved = P;
  P.otpAnim = false;
  static const Preset kPresets[] = {
      {"QUADRATO", drawSquare, 1, 2, 6}, {"QUADRATO", drawSquare, 2, 1, 6}, {"BARRA", drawBar, 1, 2, 8},
      {"SCACCHI", drawChecker, 1, 2, 6}, {"TESTO", drawText, 1, 1, 8},
  };
  const int n = sizeof(kPresets) / sizeof(kPresets[0]);
  RunResult res[8];
  char l1[24], l2[32];
  Serial.println("# DEMO AUTOMATICA (un tasto o PRG per fermare)");
  fullWhite();
  calibrate(false);
  for (int i = 0; i < n && !aborted; i++) {
    const Preset &p = kPresets[i];
    P.lut.tp = p.tp; P.lut.phases = p.phases;
    eng.setBudget(p.budget); P.budget = p.budget;
    lutLoaded = false;
    snprintf(l1, sizeof l1, "%d/%d %s", i + 1, n + 2, p.name);
    snprintf(l2, sizeof l2, "F%u P%u N%u FR%u", p.tp, p.phases, p.budget, P.lut.fr);
    showLabel(l1, l2);
    if (!waitMs(2000)) break;
    res[i] = runFast(p.draw, P.frames, p.name, true);
  }
  RunResult base;
  if (!aborted) {
    snprintf(l1, sizeof l1, "%d/%d OTP", n + 1, n + 2);
    showLabel(l1, "parziale di serie");
    if (waitMs(2000)) base = runOtp(drawSquare, 10, "QUADRATO");
  }
  if (!aborted) {
    P.lut = saved.lut; P.budget = saved.budget; eng.setBudget(P.budget);
    lutLoaded = false;
    fullWhite();
    snprintf(l1, sizeof l1, "%d/%d FANTASMI", n + 2, n + 2);
    showLabel(l1, "foto alla fine");
    if (waitMs(2000)) runFast(drawGhost, P.frames, "GHOST", true);
    Serial.println("# FOTO ORA: metà sinistra deve essere bianca pulita (15 s)");
    waitMs(15000);
  }
  Serial.println("# riepilogo demo");
  for (int i = 0; i < n; i++)
    Serial.printf("  %d %-9s F%u P%u N%u: %.1f ms/fotogramma, %.1f fps (max %.1f ms)\n", i + 1, kPresets[i].name,
                  kPresets[i].tp, kPresets[i].phases, kPresets[i].budget, res[i].avgMs, res[i].fps, res[i].maxMs);
  Serial.printf("  OTP parziale: %.1f ms/fotogramma, %.2f fps\n", base.avgMs, base.fps);
  P = saved;
  eng.setBudget(P.budget);
  lutLoaded = false;
  aborted = false;
  fullWhite();  // fine sessione: pulizia con la waveform di serie
  finish();
}

// ---------- menu ----------
static void help() {
  Serial.println(
      "\nFASTINK (SPIKE usa e getta) · comandi, invio per confermare\n"
      "  h aiuto   s stato   f refresh completo OTP (bianco)   o parziale OTP singolo (tempo)\n"
      "  t calibra durata fotogramma (nessuna spinta)   T come t per FR 0..7\n"
      "  l<n> fotogrammi per fase (TP, 1..30)   p<n> fasi per tick (1..8)   q<n> pausa VSS dopo la fase (0..10)\n"
      "  b<n> budget N (1..15)   r<n> frame rate FR (0..7)   k<n> SPI MHz (1..20)\n"
      "  w<n> alimentazione 0=C7 a ogni tick 1=analogico acceso   d<n> modo display 1/2\n"
      "  v<n> tensioni 0=OTP 1=Waveshare V3   L<n> lunghezza LUT 153/159   x inverti polarita'\n"
      "  g 0x01 a 250 gate on/off   n<n> fotogrammi per animazione   O animazioni col parziale OTP on/off\n"
      "  a1 quadrato  a2 barra  a3 scacchi  a4 testo  a5 fantasmi (resta fermo per la foto)\n"
      "  A demo automatica   z fine sessione (refresh completo + sonno)   R leggi registri (sperimentale)\n"
      "  Durante un'animazione: un tasto qualsiasi o PRG la ferma.");
}

static void status() {
  LutParams &l = P.lut;
  Serial.printf("F(TP)=%u P=%u q=%u N=%u FR=%u k=%uMHz w=%u d=%u v=%u L=%u x=%u g=%u n=%u O=%u | ~%u fotogrammi/tick\n",
                l.tp, l.phases, l.rest, P.budget, l.fr, P.spiMhz, P.power, P.dmode, P.volt, P.lutLen, l.swap, P.gate250,
                P.frames, P.otpAnim, (unsigned)tickFrames(l));
}

static long clampArg(long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void handle(char *line) {
  while (*line == ' ') line++;
  if (!*line) return;
  const char c = line[0];
  const bool hasArg = line[1] != 0;
  const long v = atol(line + 1);
  aborted = false;
  switch (c) {
    case 'h': help(); return;
    case 's': status(); return;
    case 'f': fullWhite(); break;
    case 'z': fullWhite(); break;
    case 'o': {
      static bool on = false;  // alterna rettangolo nero / bianco
      on = !on;
      cv.clear(true);
      if (on) cv.rect(100, 40, 50, 40, true);
      const uint32_t dt = otpPartial(cv.buf);
      Serial.printf("parziale OTP (0xFF): %.1f ms\n", dt / 1000.0f);
      break;
    }
    case 't': calibrate(false); break;
    case 'T': calibrate(true); break;
    case 'l': P.lut.tp = clampArg(v, 1, 30); lutLoaded = false; status(); return;
    case 'p': P.lut.phases = clampArg(v, 1, 8); lutLoaded = false; status(); return;
    case 'q': P.lut.rest = clampArg(v, 0, 10); lutLoaded = false; status(); return;
    case 'b': P.budget = clampArg(v, 1, Engine::kMaxBudget); eng.setBudget(P.budget); status(); return;
    case 'r': P.lut.fr = clampArg(v, 0, 7); lutLoaded = false; status(); return;
    case 'k': P.spiMhz = clampArg(v, 1, 20); panel::setSpiHz(P.spiMhz * 1000000UL); status(); return;
    case 'w': P.power = clampArg(v, 0, 1); status(); return;
    case 'd': P.dmode = clampArg(v, 1, 2); status(); return;
    case 'v': P.volt = clampArg(v, 0, 1); otpLoaded = false; status(); return;
    case 'L': P.lutLen = (v == 159) ? 159 : 153; lutLoaded = false; status(); return;
    case 'x': P.lut.swap = !P.lut.swap; lutLoaded = false; status(); return;
    case 'g': P.gate250 = !P.gate250; finish(); status(); return;  // vale dal prossimo risveglio
    case 'n': P.frames = clampArg(v, 1, 1000); status(); return;
    case 'O': P.otpAnim = !P.otpAnim; status(); return;
    case 'R': readRegs(); break;
    case 'A': autoRun(); return;
    case 'a': {
      if (!hasArg) break;
      static const DrawFn fns[] = {drawSquare, drawBar, drawChecker, drawText, drawGhost};
      static const char *names[] = {"QUADRATO", "BARRA", "SCACCHI", "TESTO", "GHOST"};
      if (v < 1 || v > 5) { Serial.println("? a1..a5"); return; }
      runAnim(fns[v - 1], P.frames, names[v - 1]);
      if (v == 5) Serial.println("# fermo per la foto: metà sinistra deve essere bianca pulita. 'z' per pulire.");
      break;
    }
    default: Serial.println("? comando sconosciuto (h = aiuto)"); return;
  }
  finish();
}

static void bootScreen() {
  cv.clear(true);
  cv.text(10, 10, "FASTINK", 4);
  cv.text(10, 50, "SPIKE usa e getta", 2);
  cv.text(10, 80, "PRG = demo automatica", 1);
  cv.text(10, 95, "seriale 115200, h = aiuto", 1);
  fullRefresh(cv.buf);
  finish();
}

void setup() {
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  pinMode(kPinPrg, INPUT_PULLUP);
  delay(200);
  help();
  bootScreen();
  status();
}

void loop() {
  static char line[32];
  static uint8_t len = 0;
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\r' || ch == '\n') {
      line[len] = 0;
      len = 0;
      handle(line);
      lastActivity = millis();
    } else if (len < sizeof(line) - 1) {
      line[len++] = ch;
    }
  }
  // PRG da fermo: demo automatica (al rilascio, per non interromperla subito)
  if (digitalRead(kPinPrg) == LOW) {
    delay(30);
    if (digitalRead(kPinPrg) == LOW) {
      while (digitalRead(kPinPrg) == LOW) delay(5);
      autoRun();
    }
  }
  // Inattivo con immagine "veloce" sul pannello: pulizia di sicurezza
  if (dirty && millis() - lastActivity > kIdleCleanMs) {
    Serial.println("# inattivo da 5 min: refresh completo di pulizia");
    fullWhite();
    finish();
  }
}
#endif
