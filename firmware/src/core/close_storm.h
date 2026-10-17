// 0.14.0 (R4): chiusure del WebSocket a raffica. Dal vivo (0.13.0-ed, avatar del Gufo): il messaggio `avatar` chiede
// un blocco da 9,5 KB che dopo il Bluetooth non c'è, la libreria chiude con 1011, la scheda si ricollega subito e il
// servizio rimanda l'avatar: 100 connessioni in 10 minuti. Qui la regola pura (provata in test_close_storm); il task
// di rete la chiama a ogni connessione e chiusura e usa l'attesa per il primo tentativo (gWsHoldMs).
#pragma once
#include <stdint.h>

namespace cal {

class CloseStorm {
 public:
  static constexpr uint32_t kShortMs = 20000;   // connessione «breve»: chiusa prima di 20 s
  static constexpr uint32_t kWindowMs = 60000;  // brevi entro 60 s l'una dall'altra (più l'attesa data) = raffica
  static constexpr uint32_t kStableMs = 60000;  // una connessione vissuta 60 s chiude la raffica
  static constexpr uint32_t kFirstMs = 5000;    // prima attesa, poi doppia fino a kMaxMs
  static constexpr uint32_t kMaxMs = 30000;     // = cal::kRetrySlowMs: mai oltre i tentativi lenti
  static constexpr int kFreeShort = 2;          // le prime due brevi riprovano subito, come prima

  void opened(uint32_t now);
  // Chiusura di una connessione aperta: attesa prima del primo tentativo (0 = subito). allocFailed = un'allocazione
  // fallita durante la connessione (la 1011 della libreria non arriva fino a noi: si vede dal contatore).
  uint32_t closed(uint32_t now, bool allocFailed);
  int shortCloses() const { return n_; }

 private:
  uint32_t openedAt_ = 0, last_ = 0, hold_ = 0;
  bool open_ = false;
  uint8_t n_ = 0;
};

}  // namespace cal
