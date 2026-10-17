// Controller Xbox Series X|S (modello 1914) via Bluetooth LE: parte pura, provata in env:native (test_pad).
// Report d'ingresso (firmware 5.x, HID-over-GATT, report id 1, 16 byte senza l'id), dallo spike blepad (verificato dal vivo):
//   0-1 LX, 2-3 LY, 4-5 RX, 6-7 RY uint16 LE (centro 32768, Y: 0 = su) · 8-9 LT, 10-11 RT 0..1023 · 12 croce (hat 0..8)
//   13 bit0 A, bit1 B, bit3 X, bit4 Y, bit6 LB, bit7 RB · 14 bit2 View, bit3 Menu, bit4 Xbox, bit5 LS, bit6 RS · 15 bit0 Share
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cal {
namespace pad {

// 0.14.0: due tipi di controller (spec app iPhone §2): l'Xbox via HID e il telefono via il servizio «Calamaio pad».
enum class Kind : uint8_t { Xbox, Phone };
const char *kindName(Kind k);  // 'controller', 'telefono' (registro)

enum Button : uint16_t {
  A = 1u << 0, B = 1u << 1, X = 1u << 2, Y = 1u << 3, LB = 1u << 4, RB = 1u << 5, View = 1u << 6, Menu = 1u << 7,
  Guide = 1u << 8, LS = 1u << 9, RS = 1u << 10, Share = 1u << 11, Up = 1u << 12, Down = 1u << 13, Left = 1u << 14,
  Right = 1u << 15
};

struct State {
  uint16_t buttons;        // Button, OR (la croce accende Up/Down/Left/Right; le diagonali due bit)
  int8_t lx, ly, rx, ry;   // −100..100
  uint16_t lt, rt;         // 0..1023
  Kind src;                // 0.14.0: da dove viene (il telefono non dà mai CONSENTI, §7); sameState non lo guarda
};

const size_t kReportLen = 16;

uint16_t hatToButtons(uint8_t hat);   // 0 = niente, 1 = su, 2 = su-destra … 8 = su-sinistra; fuori scala = niente
int8_t stickPercent(uint16_t raw);    // 0..65535 → −100..100
// false (out intatto) se il report è più corto di 16 byte o manca.
bool parseXboxReport(const uint8_t *r, size_t n, State &out);
bool sameState(const State &a, const State &b);

enum class Link : uint8_t { Off, Pairing, Connecting, Ready };
const char *linkKey(Link l);  // 'off', 'pairing', 'connecting', 'ready' (come gb-pad del contratto)
// Round 1 del Task 11 (I1): fronti del collegamento fra due giri del loop. conn = contatore di blepad (cresce a ogni
// scollegamento): Ready → Ready con conn diverso è un collegamento nuovo (Xbox → telefono, Xbox → Xbox) anche se il
// loop non ha visto il non-Ready in mezzo. reset: il MenuMapper si azzera (solo HoldUp) prima di ogni stato nuovo.
struct LinkEdge {
  bool reset;   // collegamento di prima finito (o contatore cambiato)
  bool lost;    // c'era un collegamento pronto e non c'è più (o è un altro)
  bool gained;  // collegamento pronto nuovo
};
LinkEdge linkEdge(Link was, uint8_t wasConn, Link now, uint8_t conn);

// Eventi per i menu di Calamaio. Croce su/giù = sposta, A = scegli, B = indietro, Menu = apre il Menu;
// HoldDown/HoldUp/HoldFire = il tenuto 3 s di A, lo stesso del tasto PRG (indicatore, conferme, permessi).
enum class MenuEvent : uint8_t { None, Up, Down, Select, Back, Menu, HoldDown, HoldUp, HoldFire };
const char *menuEventName(MenuEvent e);  // per il registro: 'su', 'giù', 'A', 'B', 'menu', 'A giù', 'A su', 'A tenuto 3 s'

class MenuMapper {
 public:
  static const uint32_t kHoldMs = 3000;  // = cal::kHoldMs
  static const uint32_t kTapMs = 700;    // = cal::kHoldShowMs: A rilasciato prima = «scegli»
  MenuMapper() : buttons_(0), holding_(false), fired_(false), holdFrom_(0) {}
  // Nuovo stato dal controller con l'istante d'arrivo del report (ms). Al massimo cap eventi in out; ritorna quanti.
  int update(const State &s, uint32_t nowMs, MenuEvent *out, int cap);
  // Da chiamare spesso: il controller manda report solo ai cambiamenti. HoldFire a 3 s con A giù.
  int tick(uint32_t nowMs, MenuEvent *out, int cap);
  // Controller scollegato: tutto rilasciato; una tenuta in corso finisce con HoldUp (mai Select né HoldFire).
  int reset(MenuEvent *out, int cap);
  bool holding() const { return holding_; }

 private:
  uint16_t buttons_;
  bool holding_, fired_;
  uint32_t holdFrom_;
};

// Stato del collegamento (gira nel task del controller, hw/blepad). Un associato per tipo (core/pad_bonds); bonded = almeno uno.
class LinkMachine {
 public:
  static const uint32_t kPairMs = 120000;  // associazione: 2 minuti per il tasto del controller
  LinkMachine() : on_(false), bonded_(false), link_(Link::Off), kind_(Kind::Xbox), pairUntil_(0) {}
  void enable(bool on, bool bonded);         // radio accesa o spenta; bonded = abbinamento nella NVS
  void pair(uint32_t now, Kind k = Kind::Xbox);  // «Associa controller» / «Associa telefono»
  void forget();                             // «Dimentica controller»
  void connected(bool bonded, Kind k = Kind::Xbox);  // collegato, cifrato e iscritto ai report
  void setBonded(bool b) { bonded_ = b; }            // 0.14.0: c'è ancora un associato (di un tipo o dell'altro)
  void cancel();                                     // 0.14.0: «LUNGA ANNULLA» sul codice del telefono
  Kind kind() const { return kind_; }                // dopo scadenza o cancel() resta il tipo della finestra chiusa: chi collega prenda il tipo da ScanPick.kind
  void lost(uint32_t now, bool bondInvalid); // scollegato o fallito; bondInvalid = cifratura rifiutata con l'abbinamento
  void tick(uint32_t now);                   // scadenza dell'associazione
  Link link() const { return link_; }
  bool bonded() const { return bonded_; }
  bool acceptNew() const { return link_ == Link::Pairing; }  // la ricerca accetta un controller nuovo

 private:
  bool on_, bonded_;
  Link link_;
  Kind kind_;           // 0.14.0: prima di pairUntil_, 8 byte invece di 12 (RAM statica)
  uint32_t pairUntil_;
};

// Quando tenere accesa la radio e quando cercare il controller (energia).
const uint32_t kSearchMs = 120000;        // dopo accensione, scollegamento o tasto: ricerca continua
const uint32_t kIdleScanEveryMs = 30000;  // poi, in Attesa a batteria, 3 s ogni 30 s
const uint32_t kIdleScanMs = 3000;
struct RadioIn {
  // Settings::padOn && (abbinato || associazione chiesta o in corso): padOn senza abbinamento e senza
  // associazione non tiene la radio accesa (la calcola il chiamante, padLoop).
  bool setting;
  bool asleep;    // sonno profondo in arrivo, o risveglio breve (timer)
  bool wifiPsOk;  // il Wi-Fi non è in «nessun risparmio» (vietato dall'IDF col Bluetooth)
  bool usb, idle; // cavo; stato Attesa (core/power)
  Link link;
  uint32_t now, searchFrom;  // ms
};
struct RadioOut { bool radio, scan; };
RadioOut radioPlan(const RadioIn &in);

}  // namespace pad
}  // namespace cal
