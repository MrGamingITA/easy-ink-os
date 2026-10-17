// Abbinamenti per tipo (spec app iPhone §2.2): al massimo un Xbox e un telefono. NimBLE tiene le chiavi
// («nimble_bond», fino a 3 abbinamenti); questo libro dice di che tipo è ognuno ed è la sola verità: un
// abbinamento di NimBLE che il libro non conosce si cancella. NVS «calamaio-gb»: padx, padp (7 byte), padv = 1.
// Parte pura, provata in env:native (test_pad_bonds).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "pad.h"

namespace cal {
namespace pad {

struct Addr {
  uint8_t v[6];   // come ble_addr_t.val (byte meno significativo per primo)
  uint8_t type;   // 0 pubblico, 1 casuale (d'identità)
};
// 6 byte uguali e stesso tipo a meno del bit «identità» (2/3 = 0/1 risolti dal controller).
bool sameAddr(const Addr &a, const Addr &b);

const size_t kBondBlob = 7;
const char *bondKey(Kind k);           // 'padx', 'padp'
extern const char *const kBondVersionKey;  // 'padv': 1 = libro già scritto dalla 0.14.0

class BondBook {
 public:
  BondBook() : has_{false, false}, a_{} {}
  bool has(Kind k) const { return has_[idx(k)]; }
  const Addr &get(Kind k) const { return a_[idx(k)]; }
  bool any() const { return has_[0] || has_[1]; }
  // Il nuovo del suo tipo sostituisce il vecchio dello stesso tipo, mai l'altro; un indirizzo che era dell'altro
  // tipo passa a questo (mai lo stesso dispositivo due volte).
  void set(Kind k, const Addr &a);
  void clear(Kind k) { has_[idx(k)] = false; }
  bool kindOf(const Addr &a, Kind &k) const;
  static void encode(const Addr &a, uint8_t out[kBondBlob]);
  static bool decode(const uint8_t *b, size_t n, Addr &out);  // false: lunghezza ≠ 7, tipo > 1, 00…00 o FF…FF

 private:
  static int idx(Kind k) { return k == Kind::Phone ? 1 : 0; }
  bool has_[2];
  Addr a_[2];
};

// Indici (in bonds) degli abbinamenti che il libro non conosce, al massimo cap; ritorna quanti.
int staleBonds(const BondBook &book, const Addr *bonds, int n, int *out, int cap);
// Migrazione dalla 0.13.0-ed (un solo controller, nessun libro): con libro vuoto, mai migrato e un solo
// abbinamento, quello è l'Xbox. true se il libro è cambiato (va salvato, con padv = 1).
bool migrateLegacy(BondBook &book, bool migrated, const Addr *bonds, int n);
// D2: la stessa regola per «associato» a radio spenta (hw/blepad bonded): l'unico abbinamento di NimBLE di una
// 0.13 non ancora migrata è l'Xbox.
bool legacyIsXbox(bool migrated, const BondBook &book, int nBonds);

// Ricerca (hw/blepad, callback dei risultati). Fuori dall'associazione solo gli associati; durante quella di un
// tipo l'associato di quel tipo o un nuovo di quel tipo, mai l'associato dell'altro (chiuderebbe la finestra).
// Xbox nuovo: aspetto «gamepad» 0x03C4 + servizio HID 0x1812 + collegabile (come la 0.13). Telefono nuovo:
// servizio «Calamaio pad» + collegabile.
struct ScanAd {
  Addr addr;
  bool gamepad, hid, phoneSvc, connectable;
};
struct ScanPick {
  bool take;
  Kind kind;
  bool bonded;  // nel libro
};
// quarantine: maschera di Quarantine (sotto); un associato in quarantena non si prende, salvo nella finestra del suo
// tipo.
ScanPick pickAd(const ScanAd &ad, const BondBook &book, Link link, Kind pairKind, uint8_t quarantine = 0);

// N1 (riesame della correzione finale): dopo una rimessa delle chiavi (SecOut.restore) il tipo va in quarantena,
// solo in RAM: la ricerca non riprende quell'associato (altrimenti ogni 5–10 s abbinamento di NimBLE, rimessa e due
// scritture NVS). Finisce con «Associa…» o «Dimentica» di quel tipo, o alla prossima accensione della radio.
struct Quarantine {
  uint8_t mask = 0;
  static uint8_t kindMask(Kind k) { return k == Kind::Phone ? 2 : 1; }
  bool restored(Kind k) {  // true la prima volta: un solo avviso «Abbinamento perso»
    const bool fresh = !(mask & kindMask(k));
    mask |= kindMask(k);
    return fresh;
  }
  void pairing(Kind k) { mask &= static_cast<uint8_t>(~kindMask(k)); }
  void forgot(Kind k) { mask &= static_cast<uint8_t>(~kindMask(k)); }
  void forgotAll() { mask = 0; }
  void radioStart() { mask = 0; }
};

// Dal vivo 0.14.0: dopo «servizio mancante o sconosciuto» su un associato quel tipo non si prende per un po'
// (30 s, raddoppia fino a 5 min); un collegamento PRONTO azzera. hw/blepad lo passa a pickAd come la quarantena.
class Backoff {
 public:
  static const uint32_t kStartMs = 30000, kMaxMs = 300000;
  void failed(uint32_t now) {
    ms_ = ms_ ? (ms_ >= kMaxMs / 2 ? kMaxMs : ms_ * 2) : kStartMs;
    from_ = now;
  }
  void ok() { ms_ = 0; }
  bool blocked(uint32_t now) const { return ms_ && now - from_ < ms_; }

 private:
  uint32_t from_ = 0, ms_ = 0;
};

// Sicurezza per tipo: si scrive in ble_hs_cfg prima di ogni collegamento (NimBLE la legge all'inizio
// dell'abbinamento). Xbox: come la 0.13.0-ed (Just Works). Telefono: solo Secure Connections, MITM, scheda
// DisplayOnly → passkey a schermo, iOS lo fa digitare.
const uint8_t kIoDisplayOnly = 0, kIoNoInputOutput = 3;  // BLE_HS_IO_DISPLAY_ONLY, BLE_HS_IO_NO_INPUT_OUTPUT
struct SecCfg {
  uint8_t ioCap;
  bool bond, mitm, sc, scOnly;
};
SecCfg secFor(Kind k);
// Collegamento accettabile dopo la cifratura: telefono cifrato + autenticato + chiave da 16 byte + Secure Connections
// (sc: dall'archivio delle chiavi, F5: da iniziatore NimBLE non applica sm_sc_only); Xbox cifrato.
bool linkSecure(Kind k, bool encrypted, bool authenticated, uint8_t keySize, bool sc);

// Esito della cifratura di un collegamento (hw/blepad; revisione del Task 10, I1 e I2). Regola: fuori dalla finestra
// del suo tipo nessun abbinamento nuovo, mai; un errore passeggero durante la cifratura scollega soltanto.
enum class SecResult : uint8_t { Ok, Failed, Cancelled };  // Cancelled: LUNGA ANNULLA, end() o finestra chiusa
struct SecIn {
  Kind kind;        // da ScanPick.kind
  bool pairing;     // finestra di questo tipo aperta
  bool wasBonded;   // NimBLE aveva le chiavi prima della cifratura
  SecResult res;
  bool sameKeys;    // fuori dalla finestra con le chiavi: stessa LTK di prima, qualunque res (R1-I2)
  bool linkOk;      // Ok: linkSecure
  bool bondedNow;   // NimBLE ha le chiavi adesso
  bool snapshot;    // copia del record delle chiavi presa prima della cifratura (revisione finale I1)
};
struct SecOut {
  bool ready;       // si va avanti (GATT, libro)
  bool deleteBond;  // via le chiavi di NimBLE di questo indirizzo
  bool dropBook;    // via dal libro, se c'è con questo indirizzo
  bool pairFail;    // telefono nuovo non autenticato: «Associazione non riuscita», finestra chiusa (F6)
  bool bondLost;    // l'associato va riassociato dall'utente (mai una finestra automatica)
  bool restore;     // rimettere la copia del record al posto delle chiavi nuove (libro intatto, nessun avviso)
};
SecOut secOutcome(const SecIn &in);
// Prima della cifratura (R1-I1): fuori dalla finestra solo un associato con la LTK salvata (senza, startSecurity
// avvierebbe un abbinamento nuovo e il confronto della LTK poi cancellerebbe l'abbinamento). RefuseNew e NoLtk:
// scollegare senza toccare chiavi né libro.
enum class SecStart : uint8_t { Go, RefuseNew, NoLtk };
SecStart secStart(bool pairing, bool wasBonded, bool hadLtk);

}  // namespace pad
}  // namespace cal
