// Piano B per trovare il PC quando mDNS non passa (0.12.1-gb, docs/PROTOCOLLO.md «Trasporto e scoperta»):
// indirizzo manuale e ultimo PC autenticato, uno per rete (SSID). Logica pura, provata in native;
// la NVS (namespace «calamaio-pc», chiave «v1») e il WebSocket stanno in net/net.cpp.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {

const uint16_t kPcDefaultPort = 8766;

// Indirizzo del PC: ip vuoto = nessuno.
struct PcAddr {
  char ip[16];
  uint16_t port;
  char path[32];
};

// IPv4 valido (come mdnsAnswerValid), porta 1..65535, percorso che comincia con «/».
bool pcAddrValid(const PcAddr &a);
bool pcAddrSame(const PcAddr &a, const PcAddr &b);

// Indirizzo manuale: «ip» o «ip:porta» (IPv4 letterale, porta 1..65535, predefinita 8766; niente nomi DNS,
// niente spazi). Il percorso è defPath. false = non valido (out invariato).
bool parsePcHost(const char *s, const char *defPath, PcAddr &out);
// «ip» con la porta predefinita, altrimenti «ip:porta» (testo dell'indirizzo manuale e nome a schermo).
size_t pcHostText(const PcAddr &a, char *out, size_t cap);
// «ip:porta» (sempre con la porta: pcLast di /api/status, registro).
size_t pcAddrText(const PcAddr &a, char *out, size_t cap);

// Da dove viene la destinazione del WebSocket.
enum class PcSrc : uint8_t { None, Mdns, Manual, Last };
// Riserva dopo un tentativo mDNS senza risposta: la prima volta (prev None o Mdns) il manuale, poi l'ultimo noto,
// poi di nuovo il manuale e così via; con uno solo dei due, sempre quello. None = nessuna riserva.
PcSrc pickFallback(bool hasManual, bool hasLast, PcSrc prev);

// Una voce per rete.
struct PcMemEntry {
  char ssid[33];
  PcAddr manual;  // impostato dall'utente (POST /api/pc, pagina della scheda)
  PcAddr last;    // ultimo PC con WebSocket autenticato su questa rete
};

// Tabella SSID → indirizzi, al massimo kCap voci (come le reti salvate). Ogni scrittura che cambia una voce la porta
// in fondo (la più recente); piena, esce la prima (la più vecchia). I metodi che scrivono dicono se è cambiato
// qualcosa: chi chiama scrive la NVS solo allora.
class PcMemory {
 public:
  static const int kCap = 5;
  static const size_t kBlobMax = 3 + kCap * (3 + 32 + 53 + 53);  // encode(): mai di più (708 byte)

  void clear() { n_ = 0; }
  int count() const { return n_; }
  const PcMemEntry &at(int i) const { return e_[i]; }
  const PcMemEntry *find(const char *ssid) const;
  // a nullptr (o ip vuoto) = cancella il manuale.
  bool setManual(const char *ssid, const PcAddr *a);
  bool setLast(const char *ssid, const PcAddr &a);
  bool forget(const char *ssid);
  // Toglie le voci delle reti che non sono fra keep (reti salvate): fanno posto prima di quelle in uso.
  bool prune(const char (*keep)[33], int nKeep);

  // Formato della NVS: 'P', 1, n, poi per voce (dalla più vecchia) ssid, manuale e ultimo
  // (entrambi «ip:porta:percorso»), ognuno con un byte di lunghezza davanti. 0 = non ci sta.
  size_t encode(uint8_t *out, size_t cap) const;
  // false (tabella vuota) se il blob non è valido.
  bool decode(const uint8_t *in, size_t n);

 private:
  int index(const char *ssid) const;
  PcMemEntry &touch(const char *ssid);  // voce esistente portata in fondo, o nuova (piena: esce la più vecchia)
  void removeAt(int i);
  PcMemEntry e_[kCap];
  int n_ = 0;
};

}  // namespace cal
