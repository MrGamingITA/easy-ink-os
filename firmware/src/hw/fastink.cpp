// Driver veloce: comandi SSD1680 (Rev 0.14) sul controller della E0213A367, che usa Y da 1 byte (dal vivo, spike).
#include "hw/fastink.h"

#include <stdlib.h>
#include <string.h>

namespace cal {
namespace fastink {
namespace {

// Pin del pannello (docs/HARDWARE.md, come heltec-eink-modules): CS 4, DC 5, RST 6, BUSY 7.
const int kCs = 4, kDc = 5, kRst = 6, kBusy = 7;
// 0x22 (SSD1680 pag. 26)
const uint8_t kUpdFullOtp = 0xF7;      // completo di serie: forma d'onda e tensioni dall'OTP, poi spegne l'analogico
const uint8_t kUpdAnalogOn = 0xC0;     // accende clock e analogico e li lascia accesi
const uint8_t kUpdAnalogOff = 0x03;    // spegne analogico e oscillatore
const uint8_t kTickCtlDefault = 0x04;  // solo display (modo 1), analogico già acceso: ~40 ms fissi dal vivo

SPIClass *gBus = nullptr;
void (*gOnReturn)() = nullptr;
Print *gLog = &Serial;
SPISettings gSpi(20000000, MSBFIRST, SPI_MODE0);  // 20 MHz: massimo del datasheet, provato nello spike
Engine gEng;
Params gP = paramsFor(Profile::Clean);
// Piani 0x24 e 0x26 del tick: 2 × 4000 byte. Fra due tick gBw fa da appoggio per il fotogramma convertito
// (submit) e per l'immagine del completo (begin): 0.12.0, un piano da 4000 byte in meno durante la partita.
uint8_t *gPlanes = nullptr;
uint8_t *gBw = nullptr, *gRed = nullptr;
bool gOn = false, gAnalog = false;
uint8_t gCtl = kTickCtlDefault;
uint32_t gLastUs = 0;

void cmd(uint8_t c) {
  gBus->beginTransaction(gSpi);
  digitalWrite(kDc, LOW);
  digitalWrite(kCs, LOW);
  gBus->transfer(c);
  digitalWrite(kCs, HIGH);
  gBus->endTransaction();
}

void data(const uint8_t *d, size_t n) {
  gBus->beginTransaction(gSpi);
  digitalWrite(kDc, HIGH);
  digitalWrite(kCs, LOW);
  gBus->writeBytes(d, n);
  digitalWrite(kCs, HIGH);
  gBus->endTransaction();
}

void cmdData(uint8_t c, const uint8_t *d, size_t n) {
  cmd(c);
  data(d, n);
}

void cmd1(uint8_t c, uint8_t v) { cmdData(c, &v, 1); }

// 0.12.0: dopo 2 ms di attesa si cede il core (un completo dura ~2–3,5 s e gira sul core 0, dove il watchdog
// guarda il task idle); i tick veloci (~40–90 ms) perdono al più ~1 ms.
bool waitIdle(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (digitalRead(kBusy) == HIGH) {
    const uint32_t el = millis() - t0;
    if (el > timeoutMs) return false;
    if (el >= 2) vTaskDelay(1);
  }
  return true;
}

bool update(uint8_t ctl, uint32_t timeoutMs) {
  cmd1(0x22, ctl);
  cmd(0x20);
  return waitIdle(timeoutMs);
}

void hwReset() {
  digitalWrite(kRst, LOW);
  delay(10);
  digitalWrite(kRst, HIGH);
  delay(10);
  waitIdle(2000);
  cmd(0x12);  // reset software
  delay(10);
  waitIdle(2000);
}

void setWindow() {
  cmd1(0x11, 0x03);  // X+, Y+
  const uint8_t x[2] = {0x00, static_cast<uint8_t>(kRowBytes - 1)};
  cmdData(0x44, x, 2);
  const uint8_t y[2] = {0x00, static_cast<uint8_t>(kRamH - 1)};  // Y da 1 byte, non 2 come l'SSD1680
  cmdData(0x45, y, 2);
}

void writePlane(uint8_t ramCmd, const uint8_t *buf) {
  cmd1(0x4E, 0x00);
  cmd1(0x4F, 0x00);
  cmdData(ramCmd, buf, kRamBytes);
}

void analogOff() {
  if (!gAnalog) return;
  update(kUpdAnalogOff, 2000);
  gAnalog = false;
}

// Completo di serie con l'immagine ram: carica dall'OTP forma d'onda e tensioni (VCOM compreso) e bilancia la carica.
bool fullOtp(const uint8_t *ram) {
  analogOff();
  const uint8_t opt[4] = {0x00, 0x80, 0x03, 0x0E};  // 0x37 come heltec-eink-modules (E0213A367/mode.cpp)
  cmdData(0x37, opt, 4);
  cmd1(0x3C, 0x01);  // bordo come configFull()
  writePlane(0x24, ram);
  writePlane(0x26, ram);
  const bool ok = update(kUpdFullOtp, 10000);
  if (!ok) gLog->println("[fastink] BUSY bloccato nel completo");
  return ok;
}

// Dopo un completo: la nostra LUT al posto di quella di serie (tensioni intatte), bordo fermo, analogico acceso.
bool prepareFast() {
  uint8_t lut[kLutLen];
  buildTickLut(gP, lut);
  cmdData(0x32, lut, kLutLen);
  waitIdle(1000);
  cmd1(0x3C, 0x80);  // bordo a VCOM: fermo
  gAnalog = update(kUpdAnalogOn, 2000);
  return gAnalog;
}

bool gKeep = false;  // memoria riservata con reserve(): end() non la libera

void freeAll() {
  gEng.release();
  free(gPlanes);
  gPlanes = gBw = gRed = nullptr;
}

}  // namespace

void attachBus(SPIClass *bus, void (*onReturn)(), Print *log) {
  gBus = bus;
  gOnReturn = onReturn;
  if (log) gLog = log;
  pinMode(kRst, OUTPUT);
  digitalWrite(kRst, HIGH);
}

bool active() { return gOn; }

bool reserve() {
  gKeep = true;
  if (!gPlanes) gPlanes = static_cast<uint8_t *>(malloc(2 * kRamBytes));
  if (!gPlanes || !gEng.init(gP.budget)) {
    gKeep = false;
    if (!gOn) freeAll();
    gLog->printf("[fastink] riserva non riuscita (heap libero %u, blocco max %u)\n",
                 static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return false;
  }
  return true;
}

void unreserve() {
  gKeep = false;
  if (!gOn) freeAll();
}

bool begin(Profile p) {
  if (gOn) return true;
  if (!gBus) {
    gLog->println("[fastink] manca attachBus()");
    return false;
  }
  gP = paramsFor(p);
  if (!gPlanes) gPlanes = static_cast<uint8_t *>(malloc(2 * kRamBytes));
  if (!gPlanes || !gEng.init(gP.budget)) {
    freeAll();
    gLog->printf("[fastink] memoria insufficiente (heap libero %u)\n", static_cast<unsigned>(ESP.getFreeHeap()));
    return false;
  }
  gBw = gPlanes;
  gRed = gPlanes + kRamBytes;
  memset(gBw, 0xFF, kRamBytes);
  hwReset();
  setWindow();
  if (!fullOtp(gBw) || !prepareFast()) {
    freeAll();
    return false;
  }
  gEng.syncSaturated(gBw);
  gOn = true;
  gLog->printf("[fastink] acceso: profilo %s, N%u F%u P%u FR%u, bordi %u, pulizia %u, 0x22=%02X, heap libero %u\n",
               profileName(p), gP.budget, gP.tp, gP.phases, gP.fr, gP.edge, gP.polish, gCtl,
               static_cast<unsigned>(ESP.getFreeHeap()));
  return true;
}

void submit(const uint8_t *frame) {
  if (!gOn) return;
  frameToRam(frame, gBw);  // gBw è libero fra due tick
  gEng.setTarget(gBw, gP.edge);
}

bool tick() {
  if (!gOn) return false;
  TickStats st;
  if (!gEng.next(gBw, gRed, gP.polish, st)) return false;
  writePlane(0x24, gBw);
  writePlane(0x26, gRed);
  const uint32_t t0 = micros();
  if (!update(gCtl, 3000)) {
    gLog->println("[fastink] BUSY bloccato nel tick: completo di sicurezza");
    cleanFull();
    return false;
  }
  gLastUs = micros() - t0;
  vTaskDelay(1);  // cede il core 0 dopo ogni passo: il task idle (watchdog) deve girare anche durante la partita
  return true;
}

bool settled() { return gOn ? gEng.idle() : true; }

void cleanFull() {
  if (!gOn) return;
  memcpy(gBw, gEng.target(), kRamBytes);
  fullOtp(gBw);
  gEng.syncSaturated(gBw);
  prepareFast();
}

void end() {
  if (!gOn) return;
  analogOff();
  if (!gKeep) freeAll();  // riservata per la partita: resta fino a unreserve()
  gOn = false;
  gLog->println("[fastink] spento: pannello restituito alla libreria");
  if (gOnReturn) gOnReturn();  // riconfigura la libreria; il suo completo è la pulizia finale
}

void setParams(const Params &p) {
  gP = p;
  if (gP.budget > Engine::kMaxBudget) gP.budget = Engine::kMaxBudget;  // 0.12.0: livelli in 4 bit
  if (!gOn) return;
  gEng.setBudget(p.budget);
  analogOff();
  prepareFast();
}

const Params &params() { return gP; }
void setTickCtl(uint8_t ctl) { gCtl = ctl; }
uint8_t tickCtl() { return gCtl; }
uint32_t lastTickUs() { return gLastUs; }

uint32_t measureHoldTick() {
  if (!gOn) return 0;
  memcpy(gBw, gEng.target(), kRamBytes);
  memset(gRed, 0xFF, kRamBytes);  // LUT2/LUT3 ovunque: nessun pixel spinto
  writePlane(0x24, gBw);
  writePlane(0x26, gRed);
  const uint32_t t0 = micros();
  if (!update(gCtl, 3000)) return 0;
  return micros() - t0;
}

}  // namespace fastink
}  // namespace cal
