// QR vero (M3): testo → moduli, con ricmoo/QRCode (gfx/qr/qrcode_ricmoo.*).
// Si usa per il QR della rete Calamaio-XXXX (WIFI:T:WPA;S:…;P:…;;). Correzione M, versione 1..5.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace qr {

const int kMaxVersion = 5;                       // 37×37 moduli: basta per testi fino a 84 byte
const int kMaxSize = kMaxVersion * 4 + 17;       // 37
const int kBufBytes = (kMaxSize * kMaxSize + 7) / 8;

struct Bits {
  int size;      // moduli per lato (0 = non valido)
  int version;
  uint8_t buf[kBufBytes];
  bool get(int x, int y) const;  // true = modulo nero
};

// Capienza in byte (modo byte, correzione M) della versione v (1..kMaxVersion).
int byteCapacityM(int version);
// Sceglie la versione più piccola che contiene il testo. false se è troppo lungo.
bool encode(const char *text, Bits &out);

}  // namespace qr
