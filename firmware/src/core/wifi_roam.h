// Più reti Wi-Fi (firmware 0.8.0): quale rete salvata provare e quando cambiare da sola. Logica pura, provata in
// native (test_wifi); il task di rete (net/net.cpp) fa le ricerche, i collegamenti e il registro.
//
// Collegamento (un «giro» = un tentativo di cal::RetrySchedule: subito, 5 s, 5 s, poi ogni 30 s): ricerca, poi le
// reti salvate che si vedono dalla più forte; se una non va si prova la successiva. Dopo quelle visibili si provano
// le salvate che la ricerca non ha visto, nell'ordine di inserimento (reti nascoste o perse dalla ricerca).
//
// Cambio automatico (roaming): il segnale della rete in uso si legge ogni kRssiEveryMs (10 s). Ogni kRoamCheckMs
// (60 s; in Attesa kRoamCheckIdleMs, 5 min) un controllo: se le ultime kRoamWeakReadings letture di fila sono
// sotto kRoamWeakDbm, una ricerca rapida; se una rete salvata ha almeno kRoamMarginDb in più si passa a quella,
// ma solo se non c'è un permesso aperto, un aggiornamento o una calibrazione della batteria in corso.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "model.h"

namespace cal {
namespace wifi {

constexpr int kRoamWeakDbm = -80;          // sotto: segnale debole (richiesta dell'utente)
constexpr int kRoamWeakReadings = 3;       // letture deboli di fila prima di cercare
constexpr int kRoamMarginDb = 10;          // la nuova rete deve avere almeno 10 dB in più
constexpr uint32_t kRoamCheckMs = 60000;   // controllo in Attivo
constexpr uint32_t kRoamCheckIdleMs = 300000;  // controllo in Attesa (modem sleep)
constexpr uint32_t kQuickScanMsPerChan = 120;  // ricerca «rapida»: 120 ms per canale (13 canali ≈ 1,6 s)

// Una rete vista dalla ricerca (la più forte per SSID: più antenne con lo stesso nome contano una volta).
struct Seen {
  char ssid[kSsidLen];
  int rssi;
};

// Un candidato del giro: indice nella lista delle salvate (ordine di inserimento), segnale se vista.
struct Candidate {
  int idx;
  bool visible;
  int rssi;
};

// Ordine dei tentativi: salvate visibili dalla più forte (a parità di segnale nell'ordine di inserimento), poi
// le non visibili nell'ordine di inserimento. saved: SSID delle salvate. Restituisce quante voci in out (= nSaved).
int roundOrder(const char (*saved)[kSsidLen], int nSaved, const Seen *seen, int nSeen, Candidate *out);

// Segnale di una rete nella ricerca (false = non vista).
bool seenRssi(const Seen *seen, int nSeen, const char *ssid, int &rssi);

// Che cosa impedisce il cambio automatico adesso.
struct RoamBlock {
  bool permOpen;     // richiesta di permesso in coda sul display
  bool ota;          // aggiornamento remoto in corso (offerta in attesa, ricezione, verifica, scrittura)
  bool calibrating;  // calibrazione della batteria
};
const char *roamBlockText(const RoamBlock &b);  // nullptr = nessun blocco; altrimenti il motivo (per il registro)

// Rete migliore per il cambio: la salvata (non quella in uso) con il segnale più forte, se supera curRssi di almeno
// kRoamMarginDb. -1 = nessuna.
int roamTarget(const char (*saved)[kSsidLen], int nSaved, const char *current, int curRssi, const Seen *seen, int nSeen,
               int *targetRssi);

// Letture del segnale e ritmo dei controlli. millis() a 32 bit: confronti per differenza.
class RoamMonitor {
 public:
  // Nuova connessione (o rete cambiata): letture da capo, primo controllo fra un periodo.
  void reset(uint32_t now) { streak_ = 0; last_ = 0; checkedAt_ = now; active_ = true; }
  void stop() { active_ = false; streak_ = 0; }
  bool active() const { return active_; }
  // Una lettura del segnale della rete in uso (ogni 10 s).
  void reading(int rssi) {
    last_ = rssi;
    streak_ = rssi < kRoamWeakDbm ? streak_ + 1 : 0;
  }
  int streak() const { return streak_; }
  int last() const { return last_; }
  static uint32_t period(bool idle) { return idle ? kRoamCheckIdleMs : kRoamCheckMs; }
  // È ora del controllo? (in Attesa il periodo è più lungo). Non cambia niente: il controllo lo segna checked().
  bool due(uint32_t now, bool idle) const { return active_ && now - checkedAt_ >= period(idle); }
  void checked(uint32_t now) { checkedAt_ = now; }
  // Al controllo: il segnale è debole da abbastanza letture di fila da giustificare una ricerca?
  bool weak() const { return streak_ >= kRoamWeakReadings; }

 private:
  bool active_ = false;
  int streak_ = 0;
  int last_ = 0;
  uint32_t checkedAt_ = 0;
};

}  // namespace wifi
}  // namespace cal
