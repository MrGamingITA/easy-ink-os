// SPIKE usa e getta · fastink: driver SPI diretto (nessuna heltec-eink-modules).
#ifdef ARDUINO
#include "panel.h"
#include <SPI.h>

namespace panel {

static constexpr int kMosi = 2, kClk = 3, kCs = 4, kDc = 5, kRst = 6, kBusy = 7, kVext = 45;

static SPIClass *spi = nullptr;
static SPISettings settings(10000000, MSBFIRST, SPI_MODE0);
static bool isAwake = false;

bool awake() { return isAwake; }

void setSpiHz(uint32_t hz) { settings = SPISettings(hz, MSBFIRST, SPI_MODE0); }

static void beginBus() {
  if (!spi) spi = new SPIClass(HSPI);  // come heltec-eink-modules (Platforms/WirelessPaper/spi.cpp)
  pinMode(kCs, OUTPUT);
  digitalWrite(kCs, HIGH);
  pinMode(kDc, OUTPUT);
  pinMode(kRst, OUTPUT);
  digitalWrite(kRst, HIGH);
  pinMode(kBusy, INPUT);  // niente pull-up: livelli diversi (nota di heltec-eink-modules)
  spi->begin(kClk, -1, kMosi, -1);
}

void cmd(uint8_t c) {
  spi->beginTransaction(settings);
  digitalWrite(kDc, LOW);
  digitalWrite(kCs, LOW);
  spi->transfer(c);
  digitalWrite(kCs, HIGH);
  spi->endTransaction();
}

void data(const uint8_t *d, size_t n) {
  spi->beginTransaction(settings);
  digitalWrite(kDc, HIGH);
  digitalWrite(kCs, LOW);
  spi->writeBytes(d, n);
  digitalWrite(kCs, HIGH);
  spi->endTransaction();
}

void cmdData(uint8_t c, const uint8_t *d, size_t n) {
  cmd(c);
  data(d, n);
}

void cmd1(uint8_t c, uint8_t v) { cmdData(c, &v, 1); }

uint32_t waitIdle(uint32_t since, uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  while (digitalRead(kBusy) == HIGH) {
    if (millis() - t0 > timeoutMs) return 0;
  }
  uint32_t dt = micros() - since;
  return dt ? dt : 1;
}

static void setWindow() {
  cmd1(0x11, 0x03);                                   // X+, Y+
  const uint8_t x[2] = {0x00, (uint8_t)(fi::kRowBytes - 1)};
  cmdData(0x44, x, 2);
  // questo controller usa Y da 1 byte (come heltec-eink-modules), non 2 come SSD1680
  const uint8_t y[2] = {0x00, (uint8_t)(fi::kRamH - 1)};
  cmdData(0x45, y, 2);
}

void writePlane(uint8_t ramCmd, const uint8_t *buf) {
  cmd1(0x4E, 0x00);
  cmd1(0x4F, 0x00);
  cmdData(ramCmd, buf, fi::kRamBytes);
}

void setBorder(uint8_t v) { cmd1(0x3C, v); }

void setOtpDisplayOption() {
  const uint8_t d[4] = {0x00, 0x80, 0x03, 0x0E};
  cmdData(0x37, d, 4);
}

void setGateCount250() {
  const uint8_t d[3] = {0xF9, 0x00, 0x00};
  cmdData(0x01, d, 3);
}

void wake(uint32_t spiHz) {
  setSpiHz(spiHz);
  beginBus();
  pinMode(kVext, OUTPUT);
  if (digitalRead(kVext) != LOW || !isAwake) {
    digitalWrite(kVext, LOW);  // Vext attivo basso
    delay(50);
  }
  digitalWrite(kRst, LOW);     // reset HW come Platform::toggleResetPin()
  delay(10);
  digitalWrite(kRst, HIGH);
  delay(10);
  waitIdle(micros(), 2000);
  cmd(0x12);                   // reset SW
  delay(10);
  waitIdle(micros(), 2000);
  setWindow();
  isAwake = true;
}

void sleep() {
  if (!isAwake) return;
  cmd1(0x10, 0x01);            // deep sleep modo 1 (RAM conservata finché c'è tensione)
  delay(5);
  spi->end();
  digitalWrite(kVext, HIGH);   // Vext spento
  // pin verso il pannello spento in alta impedenza: niente alimentazione parassita
  pinMode(kMosi, INPUT);
  pinMode(kClk, INPUT);
  pinMode(kCs, INPUT);
  pinMode(kDc, INPUT);
  pinMode(kRst, INPUT);
  isAwake = false;
}

uint32_t trigger(uint8_t ctl) {
  cmd1(0x22, ctl);
  const uint32_t t = micros();
  cmd(0x20);
  return t;
}

// Lettura: comando con DC basso, poi DC alto e SDA in ingresso; il controller scrive sul fronte di discesa
bool readRegister(uint8_t c, uint8_t *out, size_t n) {
  if (!isAwake) return false;
  spi->end();
  pinMode(kMosi, OUTPUT);
  pinMode(kClk, OUTPUT);
  digitalWrite(kClk, LOW);
  digitalWrite(kDc, LOW);
  digitalWrite(kCs, LOW);
  for (int b = 7; b >= 0; b--) {
    digitalWrite(kMosi, (c >> b) & 1);
    delayMicroseconds(1);
    digitalWrite(kClk, HIGH);
    delayMicroseconds(1);
    digitalWrite(kClk, LOW);
  }
  digitalWrite(kDc, HIGH);
  pinMode(kMosi, INPUT);
  for (size_t i = 0; i < n; i++) {
    uint8_t v = 0;
    for (int b = 0; b < 8; b++) {
      delayMicroseconds(1);
      digitalWrite(kClk, HIGH);
      delayMicroseconds(1);
      v = (uint8_t)((v << 1) | (digitalRead(kMosi) ? 1 : 0));
      digitalWrite(kClk, LOW);
    }
    out[i] = v;
  }
  digitalWrite(kCs, HIGH);
  pinMode(kMosi, OUTPUT);
  spi->begin(kClk, -1, kMosi, -1);
  return true;
}

}  // namespace panel
#endif
