// SPIKE usa e getta · fastink: driver SPI diretto del pannello E0213A367 (comandi SSD1680).
// Pin da docs/HARDWARE.md: MOSI 2, CLK 3, CS 4, DC 5, RST 6, BUSY 7, Vext 45 attivo basso.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include "geom.h"

namespace panel {

// Valori di 0x22 (SSD1680 Rev 0.14, pag. 26)
constexpr uint8_t kUpdFullOtp = 0xF7;     // clock, analogico, temperatura, LUT da OTP, modo 1, display, spegni
constexpr uint8_t kUpdPartOtp = 0xFF;     // come sopra con modo 2 (lo usa heltec-eink-modules per il parziale)
constexpr uint8_t kUpdRegMode1 = 0xC7;    // LUT dal registro (0x32), modo 1, accendi e spegni l'analogico
constexpr uint8_t kUpdRegMode2 = 0xCF;    // LUT dal registro, modo 2
constexpr uint8_t kUpdLoadOtpOnly = 0xB1; // clock, temperatura, carica WS da OTP, nessun display
constexpr uint8_t kUpdAnalogOn = 0xC0;    // accende clock e analogico e li lascia accesi (Waveshare V3)
constexpr uint8_t kUpdDispMode1 = 0x04;   // solo display modo 1 (analogico già acceso)
constexpr uint8_t kUpdDispMode2 = 0x0C;   // solo display modo 2 (Waveshare V3 «fast»)
constexpr uint8_t kUpdAnalogOff = 0x03;   // spegni analogico e oscillatore

bool awake();
void wake(uint32_t spiHz);               // Vext, reset HW+SW, finestra RAM
void sleep();                            // deep sleep 0x10 01, poi Vext spento e pin in alta impedenza
void setSpiHz(uint32_t hz);

void cmd(uint8_t c);
void data(const uint8_t *d, size_t n);
void cmdData(uint8_t c, const uint8_t *d, size_t n);
void cmd1(uint8_t c, uint8_t v);

void writePlane(uint8_t ramCmd, const uint8_t *buf);  // 0x24 o 0x26, finestra intera
void setBorder(uint8_t v);
void setOtpDisplayOption();              // 0x37 come heltec-eink-modules (E0213A367/mode.cpp)
void setGateCount250();                  // 0x01 F9 00 00 (facoltativo, non usato da heltec-eink-modules)

// Avvia l'aggiornamento (0x22 v, 0x20) senza aspettare; ritorna micros() di partenza
uint32_t trigger(uint8_t ctl);
// Attende BUSY basso; ritorna µs trascorsi da 'since' o 0 se scade il tempo
uint32_t waitIdle(uint32_t since, uint32_t timeoutMs);

// Lettura sperimentale a 3 fili (SDA bidirezionale, bit-bang ≤ 1 MHz)
bool readRegister(uint8_t c, uint8_t *out, size_t n);

}  // namespace panel
#endif
