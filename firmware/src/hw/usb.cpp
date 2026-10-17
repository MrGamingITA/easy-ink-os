#include "hw/usb.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_reg.h>
#include <soc/io_mux_reg.h>

#include "boards/heltec213.h"

namespace usb {

namespace {
constexpr int kPin = PIN_USB_SENSE;  // U0RXD = GPIO44 (schema: pin 50 del modulo, dalla TXD del CP2102)
constexpr int kTx = PIN_UART_TX;     // U0TXD = GPIO43 (attraverso R2 499 Ω alla RXD del CP2102)
constexpr int kSamples = 8;   // a 115200 baud un byte dura ~87 µs: 8 campioni su ~0,5 ms ne coprono 6
constexpr int kGapUs = 60;

Pad gPad = {false, false, false};

class BoardPins : public cal::UsbPins {
 public:
  void begin(bool releaseTx) override {
    const uint32_t reg = GPIO_PIN_MUX_REG[kPin];
    saved_ = REG_READ(reg);
    gPad = {(saved_ & FUN_PU) != 0, (saved_ & FUN_PD) != 0, (saved_ & FUN_IE) != 0};
    if (releaseTx) txLow();
    // Pull-down al posto del pull-up: senza cavo (e con il CP2102 scarico) la linea va a 0; con il cavo la
    // TXD del CP2102, alimentato dal VBUS, la tiene alta.
    REG_WRITE(reg, (saved_ & ~FUN_PU) | FUN_PD);
  }
  void waitMs(uint32_t ms) override { delay(ms); }
  bool sample(int &high, int &n) override {
    high = n = 0;
    if (!(saved_ & FUN_IE)) return false;
    delayMicroseconds(50);
    for (int i = 0; i < kSamples; i++) {
      high += (REG_READ(GPIO_IN1_REG) >> (kPin - 32)) & 1;
      n++;
      delayMicroseconds(kGapUs);
    }
    return true;
  }
  void end(bool restoreTx) override {
    REG_WRITE(GPIO_PIN_MUX_REG[kPin], saved_);
    if (restoreTx) txUart();
  }
  void setTx(bool uart) override { uart ? txUart() : txLow(); }

 private:
  // GPIO43 come uscita bassa: la UART continua a trasmettere, ma i byte non escono dal pin.
  static void txLow() {
    Serial.flush();
    gpio_set_level(static_cast<gpio_num_t>(kTx), 0);
    pinMode(kTx, OUTPUT);
    digitalWrite(kTx, LOW);
  }
  // Di nuovo alla UART0 (IO_MUX, funzione U0TXD), come all'avvio.
  static void txUart() { uart_set_pin(UART_NUM_0, kTx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE); }

  uint32_t saved_ = 0;
};

BoardPins gPins;
}  // namespace

cal::UsbPins &pins() { return gPins; }
Pad lastPad() { return gPad; }

}  // namespace usb
