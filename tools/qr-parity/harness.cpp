// Banco di parità: legge testi da stdin (uno per riga, in esadecimale; riga vuota = testo vuoto) e stampa per ognuno
// «versione size maschera moduli-in-hex» di firmware/src/gfx/qr.cpp (qr::encode) + qrcode_ricmoo.cpp, o «-» se non entra.
// La maschera viene da qrcode_initText con la stessa versione e lo stesso livello: deve dare la stessa matrice.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "gfx/qr.h"
#include "gfx/qr/qrcode_ricmoo.h"

static std::string unhex(const char *s) { std::string o; for (; s[0] && s[1]; s += 2) { char b[3] = {s[0], s[1], 0}; o += char(strtol(b, 0, 16)); } return o; }
int main() {
  static char line[8192];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\r\n")] = 0;
    const std::string s = unhex(line);
    static qr::Bits b;
    if (!qr::encode(s.c_str(), b)) { printf("-\n"); continue; }
    uint8_t buf[qr::kBufBytes];
    QRCode q;
    qrcode_initText(&q, buf, b.version, ECC_MEDIUM, s.c_str());
    if (q.size != b.size || memcmp(buf, b.buf, qrcode_getBufferSize(b.version))) { printf("?\n"); continue; }
    printf("%d %d %d ", b.version, b.size, q.mask);
    for (int i = 0; i < qrcode_getBufferSize(b.version); i++) printf("%02x", b.buf[i]);
    printf("\n");
  }
}
