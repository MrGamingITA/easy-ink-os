// Nomi e testi della rete (brief §8.5, docs/PROTOCOLLO.md): logica pura, provata in native.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {

// Ultime 4 cifre esadecimali del MAC Wi-Fi STA, minuscole: aa:bb:cc:dd:48:e8 → «48e8».
void boardIdFromMac(const uint8_t mac[6], char *out, size_t cap);
// «Calamaio-48E8» (rete della configurazione: cifre maiuscole).
void apSsid(const char *boardId, char *out, size_t cap);
// «calamaio-48e8» (nome mDNS, senza .local).
void mdnsHost(const char *boardId, char *out, size_t cap);
// Testo del QR Wi-Fi: WIFI:T:WPA;S:<ssid>;P:<pass>;; con \ ; , : " protetti da barra rovesciata.
void wifiQrText(const char *ssid, const char *pass, char *out, size_t cap);

// Password dell'AP: kApPassChars caratteri da un alfabeto senza simboli ambigui (niente 0/O, 1/I/L):
// si legge e si scrive a mano senza dubbi. rnd() deve dare 32 bit casuali veri (esp_random()).
extern const char *const kApAlphabet;
const int kApPassChars = 8;
typedef uint32_t (*Rand32)(void *ctx);
void makeApPassword(char *out, size_t cap, Rand32 rnd, void *ctx);

// Risposta mDNS a _calamaio._tcp utilizzabile per collegarsi: IPv4 scritto per intero (a.b.c.d, ogni
// parte 0–255), diverso da 0.0.0.0 e da 255.255.255.255, e porta diversa da 0. Una risposta vuota
// (ESPmDNS dà «0.0.0.0», porta 0 e host vuoto quando il servizio è appena sparito) vale «nessun PC».
// L'host non conta (brief §8.5: si cerca per tipo di servizio): vuoto, si mostra l'IP.
bool mdnsAnswerValid(const char *ip, uint16_t port);

}  // namespace cal
