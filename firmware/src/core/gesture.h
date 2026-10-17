// Riconoscitore dei gesti con un tasto solo: porting di keyUp() del simulatore.
// Logica pura (nessuna dipendenza da Arduino), testata con `pio test -e native`.
#pragma once
#include <stdint.h>

enum class Gesture : uint8_t { None, Press1, Press2, Press3, Long, VeryLong };

inline const char *gestureName(Gesture g) {
  switch (g) {
    case Gesture::Press1: return "1 pressione";
    case Gesture::Press2: return "2 pressioni";
    case Gesture::Press3: return "3 pressioni";
    case Gesture::Long: return "Pressione lunga";
    case Gesture::VeryLong: return "Tenuto 3 secondi";
    default: return "";
  }
}

class GestureRecognizer {
 public:
  static constexpr uint32_t kMultiMs = 380;   // finestra tra le pressioni
  static constexpr uint32_t kLongMs = 700;
  static constexpr uint32_t kVeryLongMs = 3000;
  static constexpr uint32_t kDebounceMs = 30;

  // Fronti del tasto con il loro istante (ms). I fronti più vicini di kDebounceMs vengono scartati.
  void down(uint32_t t) {
    if (pressed_ || (t - lastEdge_ < kDebounceMs && haveEdge_)) return;
    pressed_ = true;
    downAt_ = t;
    lastEdge_ = t;
    haveEdge_ = true;
  }

  // Restituisce subito Long o VeryLong; le pressioni brevi si risolvono in poll(). Se il tenuto 3 s è
  // già scattato in poll() con il tasto giù, il rilascio non è un gesto (None).
  Gesture up(uint32_t t) {
    if (!pressed_ || t - lastEdge_ < kDebounceMs) return Gesture::None;
    pressed_ = false;
    lastEdge_ = t;
    if (fired_) { fired_ = false; return Gesture::None; }
    uint32_t dur = t - downAt_;
    if (dur >= kVeryLongMs) { count_ = 0; return Gesture::VeryLong; }
    if (dur >= kLongMs) { count_ = 0; return Gesture::Long; }
    count_++;
    deadline_ = t + kMultiMs;
    return Gesture::None;
  }

  // Da chiamare spesso: chiude la sequenza di pressioni brevi quando scade la finestra e, con il tasto
  // ancora giù, dà VeryLong appena passano 3 s (una volta sola per pressione: l'azione parte subito).
  Gesture poll(uint32_t t) {
    if (pressed_ && !fired_ && t - downAt_ >= kVeryLongMs) {
      fired_ = true;
      count_ = 0;
      return Gesture::VeryLong;
    }
    if (count_ == 0 || pressed_ || (int32_t)(t - deadline_) < 0) return Gesture::None;
    uint8_t k = count_ > 3 ? 3 : count_;
    count_ = 0;
    return k == 1 ? Gesture::Press1 : k == 2 ? Gesture::Press2 : Gesture::Press3;
  }

  bool pressed() const { return pressed_; }
  bool fired() const { return fired_; }  // tenuto 3 s già scattato in questa pressione
  uint8_t pending() const { return count_; }

 private:
  bool pressed_ = false;
  bool fired_ = false;
  bool haveEdge_ = false;
  uint32_t downAt_ = 0;
  uint32_t lastEdge_ = 0;
  uint32_t deadline_ = 0;
  uint8_t count_ = 0;
};
