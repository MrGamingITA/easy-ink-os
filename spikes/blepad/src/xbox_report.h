// SPIKE usa e getta · blepad: lettura del report HID del controller Xbox Series X|S (modello 1914) via BLE.
// Logica pura (niente Arduino): provata in env:native.
//
// Report d'ingresso (firmware 5.x del controller, BLE HID-over-GATT, 16 byte, senza l'ID del report):
//   0-1 LX, 2-3 LY, 4-5 RX, 6-7 RY   uint16 LE, 0..65535, centro ~32768 (Y: 0 = su)
//   8-9 LT, 10-11 RT                  uint16 LE, 0..1023
//   12  croce (hat): 0 = nulla, 1 = su, 2 = su-destra, … 8 = su-sinistra (in senso orario)
//   13  bit0 A, bit1 B, bit3 X, bit4 Y, bit6 LB, bit7 RB
//   14  bit2 View, bit3 Menu, bit4 Xbox, bit5 LS, bit6 RS
//   15  bit0 Share
// Fonti: Bluepad32 uni_hid_parser_xboxone.c (parse_usage_firmware_v4_v5, «Model 1914: Share button»),
// asukiaaa/arduino-XboxControllerNotificationParser (indici 12..15). Da confermare col report map letto dal vivo.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace pad {

enum Button : uint16_t {
  kA = 1u << 0, kB = 1u << 1, kX = 1u << 2, kY = 1u << 3,
  kLB = 1u << 4, kRB = 1u << 5, kView = 1u << 6, kMenu = 1u << 7,
  kXbox = 1u << 8, kLS = 1u << 9, kRS = 1u << 10, kShare = 1u << 11,
};

enum Dpad : uint8_t { kUp = 1, kRight = 2, kDown = 4, kLeft = 8 };  // maschera di bit

struct State {
  uint16_t buttons = 0;  // Button, OR
  uint8_t dpad = 0;      // Dpad, OR (le diagonali accendono due bit)
  uint16_t lx = 32768, ly = 32768, rx = 32768, ry = 32768;
  uint16_t lt = 0, rt = 0;
};

static const size_t kReportLen = 16;

// Hat 0..8 → maschera Dpad; valori fuori scala = nulla.
uint8_t hatToDpad(uint8_t hat);

// false se il report è troppo corto (lo stato resta com'era).
bool parse(const uint8_t *d, size_t n, State *out);

// Stick in -100..100 (0 = centro).
int stickPercent(uint16_t v);

// Cambiamento «da stampare»: tasti/croce sempre; assi solo oltre la soglia (in unità grezze).
bool differs(const State &a, const State &b, uint16_t stickThreshold, uint16_t trigThreshold);
bool buttonsDiffer(const State &a, const State &b);

// Riga compatta senza timestamp, es. «A.....V.......  croce=U-  L=+12,-100 R=+0,+0 LT=0 RT=1023».
// Restituisce i caratteri scritti (troncata a cap-1).
size_t format(const State &s, char *buf, size_t cap);

}  // namespace pad
