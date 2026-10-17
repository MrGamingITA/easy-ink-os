// Giochi Game Boy: passaggio dei fotogrammi fra i due core (core/gb_frame → pannello veloce) e ritmo dell'emulazione.
// Un solo buffer con un lucchetto (non un triplo buffer: 7,8 KB di heap in meno, vedi DECISIONI). L'emulatore
// (core 1) compone il fotogramma dentro beginWrite()/endWrite(); il task del pannello (core 0) lo passa al driver
// veloce dentro take(), che lo converte subito nel suo formato: nessuno tiene il lucchetto più di qualche ms. Il
// pannello prende sempre il più recente; quelli intermedi si perdono (il pannello va a ~11 immagini/s, il Game Boy a
// 59,7). Provati in native (test_gb_handoff).
#pragma once
#include <stdint.h>

#include <mutex>

#include "gb_frame.h"

namespace cal {
namespace gb {

class FrameMailbox {
 public:
  FrameMailbox() : fresh_(false), published_(0) {}
  uint8_t *beginWrite();  // solo il produttore: prende il lucchetto (aspetta il consumatore, se sta leggendo)
  void endWrite();        // pubblica il fotogramma e rilascia il lucchetto
  // Solo il consumatore: con un fotogramma nuovo chiama fn(fotogramma, ctx) col lucchetto preso. false = niente di nuovo.
  bool take(void (*fn)(const uint8_t *frame, void *ctx), void *ctx);
  void reset();           // con i due lati fermi
  bool fresh() const { return fresh_; }
  uint32_t published() const { return published_; }

 private:
  FrameMailbox(const FrameMailbox &);
  FrameMailbox &operator=(const FrameMailbox &);
  std::mutex mx_;
  uint8_t buf_[kScreenBytes];
  volatile bool fresh_;
  uint32_t published_;
};

// 70224 cicli a 4 194 304 Hz.
const int64_t kFrameNs = 16742706;
const int kMaxBurst = 2;       // fotogrammi di fila per recuperare un piccolo ritardo
const int kResyncFrames = 6;   // oltre (~100 ms) si riparte da adesso: il gioco rallenta, non corre

class Pacer {
 public:
  Pacer() : next_(0), dropped_(0) {}
  void start(int64_t nowUs) { next_ = nowUs * 1000; }
  // Quanti fotogrammi emulare adesso (0 = in anticipo).
  int due(int64_t nowUs);
  void ran(int n) { next_ += int64_t(n) * kFrameNs; }
  int64_t waitUs(int64_t nowUs) const;  // al prossimo fotogramma (0 = già dovuto)
  uint32_t dropped() const { return dropped_; }

 private:
  int64_t next_;  // ns
  uint32_t dropped_;
};

}  // namespace gb
}  // namespace cal
