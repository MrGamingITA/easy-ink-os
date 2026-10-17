// Ritmo dei tentativi di collegamento (Wi-Fi, ricerca mDNS del PC, WebSocket): un tentativo subito,
// poi 2 a 5 s l'uno dall'altro, poi uno ogni 30 s per sempre (richiesta dell'utente, 2026-10-11;
// sostituisce «subito, poi ogni 30 s» del brief §8.5). Logica pura, provata in native.
#pragma once
#include <stdint.h>

namespace cal {

constexpr uint32_t kRetryFastMs = 5000;   // dopo il 1° e il 2° tentativo
constexpr uint32_t kRetrySlowMs = 30000;  // dal 3° in poi
constexpr int kRetryFastCount = 2;

class RetrySchedule {
 public:
  // Ricomincia: il tentativo 1 è dovuto subito (now). slowMs: intervallo dal 3° tentativo (M6: dopo un `bye`
  // si cerca il PC ogni 15 s per 60 s, perché anche un riavvio del servizio manda `bye`).
  void restart(uint32_t now, uint32_t slowMs = kRetrySlowMs) { active_ = true; n_ = 0; due_ = now; slow_ = slowMs; }
  // Cambia l'intervallo lento senza ricominciare (vale dal prossimo tentativo).
  void setSlow(uint32_t slowMs) { slow_ = slowMs; }
  void stop() { active_ = false; }
  bool active() const { return active_; }
  // millis() a 32 bit: il confronto per differenza regge il giro dopo 49 giorni.
  bool due(uint32_t now) const { return active_ && int32_t(now - due_) >= 0; }
  // Intervallo prima del tentativo successivo al tentativo n (1, 2, …).
  static uint32_t delayAfter(int n) { return n <= kRetryFastCount ? kRetryFastMs : kRetrySlowMs; }
  // Parte un tentativo: restituisce il suo numero e fissa il prossimo sull'orario previsto (0, 5, 10,
  // 40, 70 s…), non sulla fine del tentativo; se un tentativo bloccante ha già superato quell'orario,
  // il prossimo è dovuto subito ma una volta sola (niente raffiche per recuperare).
  int attempt(uint32_t now) {
    n_++;
    due_ += delay(n_);
    if (int32_t(now - due_) > 0) due_ = now;
    return n_;
  }
  int count() const { return n_; }
  uint32_t dueAt() const { return due_; }
  uint32_t nextDelay() const { return delay(n_); }  // dopo attempt(): intervallo appena fissato
  uint32_t delay(int n) const { return n <= kRetryFastCount ? kRetryFastMs : slow_; }

 private:
  bool active_ = false;
  int n_ = 0;
  uint32_t due_ = 0;
  uint32_t slow_ = kRetrySlowMs;
};

}  // namespace cal
