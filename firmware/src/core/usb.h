// Cavo USB collegato? Logica pura (provata in native); la lettura del pin è in hw/usb.cpp.
// Principio (schema Wireless_Paper_V0.4): il CP2102 è alimentato solo da VDD_5V, cioè dalla USB
// (REGIN pin 7 e VBUS pin 8), e la sua TXD (pin 26) va diretta a U0RXD = GPIO44, senza resistenze.
// Con il cavo la TXD del CP2102 sta alta a riposo; senza cavo il CP2102 è spento e, con il pull-down
// che hw/usb mette per qualche microsecondo, GPIO44 legge basso. Un campione alto è quindi prova di
// cavo collegato; tutti bassi può essere anche un byte di soli zeri in arrivo dal PC, perciò il «no»
// vale solo dopo due letture di fila.
#pragma once
#include <stdint.h>

namespace cal {

enum class UsbPresent : uint8_t { Unknown, No, Yes };

inline const char *usbName(UsbPresent p) {
  return p == UsbPresent::Yes ? "sì" : p == UsbPresent::No ? "no" : "sconosciuto";
}

// Una lettura: high campioni alti su n, inputOk = il pad ha l'ingresso abilitato (altrimenti il
// registro d'ingresso non dice niente).
inline UsbPresent usbClassify(int high, int n, bool inputOk) {
  if (!inputOk || n <= 0) return UsbPresent::Unknown;
  return high > 0 ? UsbPresent::Yes : UsbPresent::No;
}

// Stato stabile: «sì» subito, «no» dopo kNoRuns letture «no» di fila, «sconosciuto» non cambia niente.
class UsbFilter {
 public:
  static constexpr int kNoRuns = 2;
  // true se lo stato stabile è cambiato.
  bool update(UsbPresent p) {
    const UsbPresent before = state_;
    if (p == UsbPresent::Yes) { state_ = UsbPresent::Yes; noRun_ = 0; }
    else if (p == UsbPresent::No) { if (++noRun_ >= kNoRuns) state_ = UsbPresent::No; }
    return state_ != before;
  }
  UsbPresent state() const { return state_; }

 private:
  UsbPresent state_ = UsbPresent::Unknown;
  int noRun_ = 0;
};

// ---------- M6: alimentazione fantasma del CP2102 (bug dal vivo, 2026-10-11) ----------
// Senza VBUS la U0TXD dell'ESP (GPIO43, a riposo alta, e i log escono di continuo) arriva alla RXD del CP2102
// attraverso R2 (499 Ω) e i diodi di protezione: il CP2102 si alimenta in parte (condensatori di VDD carichi,
// ~1 µF sullo schema V0.4) e la sua TXD tiene alto GPIO44 → «cavo collegato» senza cavo, a intermittenza.
// Rimedio: prima di campionare, GPIO43 staccato dalla UART e tenuto BASSO, GPIO44 con il pull-down, per
// kUsbDrainMs: i condensatori si scaricano attraverso la TXD del CP2102 e il pull-down (τ ≈ 45 kΩ × 1,1 µF
// ≈ 50 ms [stimato]; 150 ms ≈ 3 τ, la linea scende a ~5 % di 2,7 V). Con il cavo il CP2102 è alimentato
// dal VBUS e la linea resta alta comunque. Quando lo stato stabile è «no» la TX resta staccata (niente log
// su UART0, che sarebbe di nuovo la causa) e le letture successive non hanno bisogno di attesa.
constexpr uint32_t kUsbDrainMs = 150;

// I due pin dietro un'interfaccia: sulla scheda hw/usb.cpp, nei test un CP2102 finto.
class UsbPins {
 public:
  virtual ~UsbPins() {}
  // Inizio lettura: pull-down su GPIO44; releaseTx = GPIO43 dalla UART a uscita bassa.
  virtual void begin(bool releaseTx) = 0;
  virtual void waitMs(uint32_t ms) = 0;
  virtual bool sample(int &high, int &n) = 0;  // false = ingresso del pad spento (lettura inutile)
  // Fine lettura: pad di GPIO44 com'era; restoreTx = GPIO43 di nuovo alla UART.
  virtual void end(bool restoreTx) = 0;
  // Solo la TX, fuori da una lettura (stato stabile cambiato).
  virtual void setTx(bool uart) = 0;
};

struct UsbProbe {
  UsbPresent present;
  int high, n;
  bool drained;  // c'è stata l'attesa con la TX bassa
};

// Una lettura. txOn = la TX è collegata alla UART adesso: si stacca, si aspetta kUsbDrainMs, si campiona e si
// rimette. Con la TX già staccata (stato «no») si campiona subito.
inline UsbProbe usbProbe(UsbPins &p, bool txOn) {
  UsbProbe r = {UsbPresent::Unknown, 0, 0, txOn};
  p.begin(txOn);
  if (txOn) p.waitMs(kUsbDrainMs);
  const bool ok = p.sample(r.high, r.n);
  p.end(txOn);
  r.present = usbClassify(r.high, r.n, ok);
  return r;
}

// Filtro + regola della TX: con lo stato stabile «no» la TX resta staccata, con «sì» o «sconosciuto» è sulla UART.
class UsbMonitor {
 public:
  // true se lo stato stabile è cambiato; last() dice la lettura (per Info › Cavo USB e il log).
  // Come usbProbe(), ma la TX si rimette alla UART solo se lo stato resta «sì» o «sconosciuto»: rimetterla e
  // poi staccarla ricaricherebbe subito il CP2102 (i byte del log lo alimentano di nuovo).
  bool check(UsbPins &p) {
    last_ = {UsbPresent::Unknown, 0, 0, txOn_};
    p.begin(txOn_);
    if (txOn_) p.waitMs(kUsbDrainMs);
    const bool ok = p.sample(last_.high, last_.n);
    last_.present = usbClassify(last_.high, last_.n, ok);
    const bool changed = filter_.update(last_.present);
    const bool wantTx = filter_.state() != UsbPresent::No;
    p.end(txOn_ && wantTx);
    if (wantTx && !txOn_) p.setTx(true);
    txOn_ = wantTx;
    return changed;
  }
  UsbPresent state() const { return filter_.state(); }
  bool txOn() const { return txOn_; }
  const UsbProbe &last() const { return last_; }

 private:
  UsbFilter filter_;
  bool txOn_ = true;
  UsbProbe last_ = {UsbPresent::Unknown, 0, 0, false};
};

}  // namespace cal
