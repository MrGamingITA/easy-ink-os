// Driver veloce del pannello, parte pura: geometria della RAM, profili, LUT del tick, motore a budget per pixel.
// Porting di spikes/fastink (risultati dal vivo nel suo README). Provato con `pio test -e native -f test_fastink`.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace fastink {

// Schermo logico (landscape di heltec-eink-modules, antenna in alto) = formato di submit():
// 1 bit per pixel, righe di 32 byte, bit 7 = x più a sinistra, 1 = nero (come Canvas<250,122>::data()).
const int kScrW = 250, kScrH = 122;
const int kFrameStride = 32;
const int kFrameBytes = kFrameStride * kScrH;  // 3904
// RAM del controller (E0213A367, 128 × 250): bit 1 = bianco, 16 byte per riga, colonne 122..127 invisibili.
const int kRamW = 128, kRamH = 250;
const int kRowBytes = kRamW / 8;               // 16
const int kRamBytes = kRowBytes * kRamH;       // 4000 per piano
const int kPixels = kRamW * kRamH;             // 32000
const int kVisW = 122;
const size_t kLutLen = 153;                    // registro 0x32, SSD1680 Rev 0.14 §6.6–6.7

enum class Profile : uint8_t { Clean, Smooth };  // contratto: Clean = F8 P1 N10 FR7, Smooth = F4 P1 N6 FR7

// budget = N impulsi per una transizione completa (1..15); tp = fotogrammi della fase di spinta (F); phases = P;
// fr = FR[n] 0..7; edge = spinte extra ai pixel cambiati e ai loro 4 vicini; polish = spinte extra a tutti i pixel
// quando l'immagine si ferma. edge e polish: valori iniziali, regolati dal vivo nel Task 5.
struct Params { uint8_t budget, tp, phases, fr, edge, polish; };
Params paramsFor(Profile p);
const char *profileName(Profile p);  // 'pulito', 'fluido' (registro)

// Fotogramma di submit() → piano RAM (rotazione 3: RAM x = y logico, RAM y = 249 − x logico).
void frameToRam(const uint8_t *frame, uint8_t *ram);

// LUT del tick: un solo gruppo; LUT0 = VSH1 (nero), LUT1 = VSL (bianco), LUT2/3 ferme, LUT4 = DCVCOM, XON 0.
// Le tensioni non sono nella LUT da 153 byte: restano quelle caricate dall'OTP. Ritorna kLutLen.
size_t buildTickLut(const Params &p, uint8_t *out);

struct TickStats {
  uint32_t toBlack, toWhite;  // impulsi verso il nero / il bianco in questo tick (spinte extra comprese)
  uint32_t extra;             // di cui spinte extra (bordi e pulizia)
  uint32_t driven() const { return toBlack + toWhite; }
};

// Motore a budget (stile Paperboy): livello stimato per pixel 0..N (0 = bianco pieno, N = nero pieno). A ogni tick un
// pixel lontano dal bersaglio riceve un impulso verso il bersaglio; arrivato, riceve solo le spinte extra in sospeso
// (quando anche i 4 vicini sono arrivati), poi niente. 0.12.0, stato compatto per la partita: 4 bit di livello
// (N ≤ 15) e 2 bit di spinte extra (≤ 3) per pixel, in due piani; l'idoneità delle spinte extra si calcola nel piano
// 0x26 del tick. Memoria: 16 000 + 8 000 + 4 000 (bersaglio) = 28 000 byte, allocati da init() (prima 40 000).
class Engine {
 public:
  static const uint8_t kMaxBudget = 15;
  static const uint8_t kMaxExtra = 3;
  static const size_t kStateBytes = size_t(kPixels) / 2 + size_t(kPixels) / 4 + size_t(kRamBytes);

  Engine() : lv_(nullptr), ex_(nullptr), target_(nullptr), budget_(6), polished_(true) {}
  ~Engine() { release(); }
  bool init(uint8_t budget);  // false senza memoria (niente resta allocato)
  void release();
  bool ready() const { return lv_ != nullptr; }
  uint8_t budget() const { return budget_; }
  void setBudget(uint8_t n);  // 1..15, riscala i livelli (arrotondati)

  // Dopo un completo di serie: pannello = ram, livelli saturi, nessuna spinta in sospeso.
  void syncSaturated(const uint8_t *ram);
  // Nuovo bersaglio (formato RAM). edge: spinte extra ai pixel cambiati e ai loro 4 vicini visibili (massimo, tetto kMaxExtra).
  void setTarget(const uint8_t *ram, uint8_t edge);
  // Un tick: riempie i piani 0x24 (bw) e 0x26 (red) e aggiorna lo stato. LUT = (red << 1) | bw. Il contenuto di
  // prima dei due piani non conta (red fa da appoggio per l'idoneità delle spinte extra).
  TickStats tick(uint8_t *bw, uint8_t *red);
  // Pulizia a immagine ferma: ogni pixel visibile riceve «pushes» spinte verso il suo colore (tetto kMaxExtra).
  void polish(uint8_t pushes);
  bool settled() const;  // tutti al bersaglio e nessuna spinta extra in sospeso
  // Il passo del driver: un tick se c'è qualcosa da fare. Quando l'immagine si ferma fa una sola pulizia
  // (polishPushes spinte, 0 = nessuna) finché non arriva un fotogramma diverso. false = niente da mandare.
  bool next(uint8_t *bw, uint8_t *red, uint8_t polishPushes, TickStats &out);
  bool idle() const { return polished_ && settled(); }  // fermo e già pulito

  uint32_t pending() const;  // pixel non ancora al bersaglio (spinte extra escluse)
  uint8_t level(int idx) const { return uint8_t((lv_[idx >> 1] >> ((idx & 1) * 4)) & 0x0F); }
  uint8_t extra(int idx) const { return uint8_t((ex_[idx >> 2] >> ((idx & 3) * 2)) & 0x03); }
  const uint8_t *target() const { return target_; }

 private:
  Engine(const Engine &);
  Engine &operator=(const Engine &);
  void bump(int idx, uint8_t n);
  bool atTarget(int idx) const;
  void setLevel(int idx, uint8_t v) {
    uint8_t &b = lv_[idx >> 1];
    const int sh = (idx & 1) * 4;
    b = uint8_t((b & ~(0x0F << sh)) | (v << sh));
  }
  void setExtra(int idx, uint8_t v) {
    uint8_t &b = ex_[idx >> 2];
    const int sh = (idx & 3) * 2;
    b = uint8_t((b & ~(0x03 << sh)) | (v << sh));
  }

  uint8_t *lv_;      // 4 bit per pixel: livello 0..budget
  uint8_t *ex_;      // 2 bit per pixel: spinte extra in sospeso
  uint8_t *target_;
  uint8_t budget_;
  bool polished_;    // pulizia a immagine ferma già fatta (Task 3)
};

}  // namespace fastink
}  // namespace cal
