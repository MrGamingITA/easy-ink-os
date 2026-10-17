#include "pad_bonds.h"

#include <string.h>

namespace cal {
namespace pad {

const char *const kBondVersionKey = "padv";
const uint32_t Backoff::kStartMs;
const uint32_t Backoff::kMaxMs;

const char *bondKey(Kind k) { return k == Kind::Phone ? "padp" : "padx"; }

bool sameAddr(const Addr &a, const Addr &b) {
  return (a.type & 1) == (b.type & 1) && memcmp(a.v, b.v, sizeof a.v) == 0;
}

void BondBook::set(Kind k, const Addr &a) {
  const int i = idx(k), o = 1 - i;
  if (has_[o] && sameAddr(a_[o], a)) has_[o] = false;
  a_[i] = a;
  a_[i].type &= 1;
  has_[i] = true;
}

bool BondBook::kindOf(const Addr &a, Kind &k) const {
  for (int i = 0; i < 2; i++)
    if (has_[i] && sameAddr(a_[i], a)) {
      k = i ? Kind::Phone : Kind::Xbox;
      return true;
    }
  return false;
}

void BondBook::encode(const Addr &a, uint8_t out[kBondBlob]) {
  memcpy(out, a.v, 6);
  out[6] = a.type & 1;
}

bool BondBook::decode(const uint8_t *b, size_t n, Addr &out) {
  if (!b || n != kBondBlob || b[6] > 1) return false;
  bool zero = true, ones = true;
  for (int i = 0; i < 6; i++) {
    zero = zero && b[i] == 0x00;
    ones = ones && b[i] == 0xFF;
  }
  if (zero || ones) return false;  // NVS corrotta o cancellata
  memcpy(out.v, b, 6);
  out.type = b[6];
  return true;
}

int staleBonds(const BondBook &book, const Addr *bonds, int n, int *out, int cap) {
  int k = 0;
  for (int i = 0; i < n && k < cap; i++) {
    Kind kind;
    if (!book.kindOf(bonds[i], kind)) out[k++] = i;
  }
  return k;
}

bool legacyIsXbox(bool migrated, const BondBook &book, int nBonds) { return !migrated && !book.any() && nBonds == 1; }

bool migrateLegacy(BondBook &book, bool migrated, const Addr *bonds, int n) {
  if (!legacyIsXbox(migrated, book, n)) return false;
  book.set(Kind::Xbox, bonds[0]);
  return true;
}

ScanPick pickAd(const ScanAd &ad, const BondBook &book, Link link, Kind pairKind, uint8_t quarantine) {
  ScanPick p = {false, Kind::Xbox, false};
  Kind k;
  const bool pairing = link == Link::Pairing;
  if (book.kindOf(ad.addr, k)) {
    if (pairing && k != pairKind) return p;
    if ((quarantine & Quarantine::kindMask(k)) && !pairing) return p;  // N1 e Backoff
    // Dal vivo: iOS annuncia per conto suo (Continuity, Dov'è) con un RPA risolto nell'identità del telefono; il
    // telefono si prende solo con il servizio «Calamaio pad» (app in primo piano). L'Xbox: basta l'indirizzo.
    if (k == Kind::Phone && !ad.phoneSvc) return p;
    p.take = true;
    p.kind = k;
    p.bonded = true;
    return p;
  }
  if (!pairing || !ad.connectable) return p;
  if (pairKind == Kind::Xbox && ad.gamepad && ad.hid) p.take = true;
  if (pairKind == Kind::Phone && ad.phoneSvc) p.take = true;
  p.kind = pairKind;
  return p;
}

SecCfg secFor(Kind k) {
  if (k == Kind::Phone) return SecCfg{kIoDisplayOnly, true, true, true, true};
  return SecCfg{kIoNoInputOutput, true, false, false, false};
}

bool linkSecure(Kind k, bool encrypted, bool authenticated, uint8_t keySize, bool sc) {
  if (k == Kind::Phone) return encrypted && authenticated && keySize == 16 && sc;
  return encrypted;
}

SecStart secStart(bool pairing, bool wasBonded, bool hadLtk) {
  if (pairing) return SecStart::Go;
  if (!wasBonded) return SecStart::RefuseNew;
  return hadLtk ? SecStart::Go : SecStart::NoLtk;
}

SecOut secOutcome(const SecIn &in) {
  SecOut o = {false, false, false, false, false, false};
  const bool fresh = !in.wasBonded && in.bondedNow;  // abbinamento nato su questo collegamento
  // R1-I2: chiavi cambiate fuori dalla finestra, con qualunque esito (anche un abbinamento finito mentre l'attesa
  // scadeva o veniva annullata): mai tenute. Revisione finale I1 (b): si rimette la copia presa prima della
  // cifratura, l'associato resta nel libro. Senza copia (blepad non parte senza LTK letta): via chiavi e libro.
  if (!in.pairing && in.wasBonded && in.bondedNow && !in.sameKeys) {
    if (in.snapshot) o.restore = true;
    else o.deleteBond = o.dropBook = o.bondLost = true;
    return o;
  }
  switch (in.res) {
    case SecResult::Cancelled:
      o.deleteBond = fresh;
      return o;
    case SecResult::Ok:
      if (!in.pairing && !in.sameKeys) {  // I1: chiavi cambiate o sparite dopo un Ok fuori dalla finestra
        o.deleteBond = in.bondedNow;
        o.dropBook = o.bondLost = true;
        return o;
      }
      if (!in.linkOk) {  // I2: cifrato ma sotto il requisito del tipo
        o.deleteBond = in.bondedNow;
        o.dropBook = o.bondLost = in.wasBonded;
        o.pairFail = !in.wasBonded && in.kind == Kind::Phone;
        return o;
      }
      o.ready = true;
      return o;
    default:  // Failed
      o.deleteBond = fresh;
      if (in.wasBonded && !in.bondedNow) o.dropBook = o.bondLost = true;  // chiavi già tolte da NimBLE
      o.pairFail = in.pairing && !in.wasBonded && in.kind == Kind::Phone;
      return o;
  }
}

}  // namespace pad
}  // namespace cal
