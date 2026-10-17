// SPIKE usa e getta · blepad: min/media/max su una finestra (intervalli fra notifiche, RTT del ping).
// Logica pura (niente Arduino): provata in env:native.
#pragma once
#include <stdint.h>

namespace pad {

struct Window {
  uint32_t count = 0;
  uint32_t min = 0, max = 0;
  uint64_t sum = 0;
  uint32_t over = 0;  // campioni oltre la soglia
  uint32_t threshold = 0xffffffffu;

  void add(uint32_t v);
  uint32_t avg() const { return count ? uint32_t(sum / count) : 0; }
  void clear();  // azzera i valori, tiene la soglia
};

// Ping con numero di sequenza: tiene l'istante d'invio di ogni ping in volo e conta i persi.
class PingTracker {
 public:
  static const int kSlots = 64;
  explicit PingTracker(uint32_t timeoutUs = 1000000) : timeout_(timeoutUs) {}

  uint32_t send(uint32_t nowUs);  // nuovo numero di sequenza
  // Risposta: true se attesa (RTT in *rtt); false se sconosciuta, doppia o già data per persa.
  bool reply(uint32_t seq, uint32_t nowUs, uint32_t *rtt);
  // Ping senza risposta oltre il limite: contati come persi (restituisce quanti nuovi).
  int expire(uint32_t nowUs);
  void dropAll();  // connessione caduta: tutti i ping in volo sono persi

  uint32_t sent = 0, received = 0, lost = 0;

 private:
  struct Slot { uint32_t seq; uint32_t at; bool busy; };
  Slot slots_[kSlots] = {};
  uint32_t next_ = 1;
  uint32_t timeout_;
};

}  // namespace pad
