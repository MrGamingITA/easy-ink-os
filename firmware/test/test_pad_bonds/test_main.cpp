// Abbinamenti per tipo (spec app iPhone §2.2): un Xbox e un telefono, nella NVS calamaio-gb padx/padp.
#include <string.h>
#include <unity.h>

#include <initializer_list>

#include "core/pad_bonds.h"

using namespace cal;

void setUp() {}
void tearDown() {}

static pad::Addr A(uint8_t last, uint8_t type = 0) {
  pad::Addr a = {{0x10, 0x20, 0x30, 0x40, 0x50, last}, type};
  return a;
}

static void test_empty_book() {
  pad::BondBook b;
  TEST_ASSERT_FALSE(b.has(pad::Kind::Xbox));
  TEST_ASSERT_FALSE(b.has(pad::Kind::Phone));
  TEST_ASSERT_FALSE(b.any());
  pad::Kind k;
  TEST_ASSERT_FALSE(b.kindOf(A(1), k));
}

// Review Focus: associare un telefono nuovo sostituisce solo il telefono.
static void test_new_phone_keeps_xbox() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  b.set(pad::Kind::Phone, A(3, 1));  // telefono nuovo
  TEST_ASSERT_TRUE(b.has(pad::Kind::Xbox));
  TEST_ASSERT_TRUE(pad::sameAddr(A(1), b.get(pad::Kind::Xbox)));
  TEST_ASSERT_TRUE(pad::sameAddr(A(3, 1), b.get(pad::Kind::Phone)));
  pad::Kind k;
  TEST_ASSERT_FALSE(b.kindOf(A(2, 1), k));  // il vecchio telefono non c'è più
}

static void test_new_xbox_keeps_phone() {
  pad::BondBook b;
  b.set(pad::Kind::Phone, A(2, 1));
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Xbox, A(4));
  TEST_ASSERT_TRUE(pad::sameAddr(A(2, 1), b.get(pad::Kind::Phone)));
  TEST_ASSERT_TRUE(pad::sameAddr(A(4), b.get(pad::Kind::Xbox)));
}

// Lo stesso indirizzo non è mai di due tipi: passa al tipo nuovo.
static void test_same_address_moves_kind() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(1));
  TEST_ASSERT_FALSE(b.has(pad::Kind::Xbox));
  pad::Kind k;
  TEST_ASSERT_TRUE(b.kindOf(A(1), k));
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(k));
}

static void test_clear_one_kind() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  b.clear(pad::Kind::Phone);
  TEST_ASSERT_TRUE(b.has(pad::Kind::Xbox));
  TEST_ASSERT_FALSE(b.has(pad::Kind::Phone));
  TEST_ASSERT_TRUE(b.any());
}

// Un indirizzo risolto dal controller arriva come tipo «identità» (2 pubblico, 3 casuale): vale come 0/1.
static void test_identity_type_matches() {
  pad::BondBook b;
  b.set(pad::Kind::Phone, A(2, 1));
  pad::Kind k;
  TEST_ASSERT_TRUE(b.kindOf(A(2, 3), k));
  TEST_ASSERT_FALSE(b.kindOf(A(2, 0), k));  // pubblico ≠ casuale
  TEST_ASSERT_FALSE(b.kindOf(A(9, 1), k));
}

static void test_blob_round_trip() {
  uint8_t blob[pad::kBondBlob];
  pad::BondBook::encode(A(0xAB, 1), blob);
  const uint8_t want[pad::kBondBlob] = {0x10, 0x20, 0x30, 0x40, 0x50, 0xAB, 1};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, blob, pad::kBondBlob);
  pad::Addr out;
  TEST_ASSERT_TRUE(pad::BondBook::decode(blob, sizeof blob, out));
  TEST_ASSERT_TRUE(pad::sameAddr(A(0xAB, 1), out));
}

static void test_blob_rejects_bad() {
  uint8_t blob[8] = {1, 2, 3, 4, 5, 6, 0, 0};
  pad::Addr out;
  TEST_ASSERT_FALSE(pad::BondBook::decode(blob, 6, out));
  TEST_ASSERT_FALSE(pad::BondBook::decode(blob, 8, out));
  TEST_ASSERT_FALSE(pad::BondBook::decode(nullptr, 7, out));
  blob[6] = 2;  // il libro salva solo 0 o 1
  TEST_ASSERT_FALSE(pad::BondBook::decode(blob, 7, out));
}

static void test_blob_rejects_zero_and_ff() {
  uint8_t z[7] = {0, 0, 0, 0, 0, 0, 0};
  uint8_t f[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1};
  pad::Addr a;
  TEST_ASSERT_FALSE(pad::BondBook::decode(z, 7, a));
  TEST_ASSERT_FALSE(pad::BondBook::decode(f, 7, a));
}

static void test_book_get_zeroed() {
  pad::BondBook b;
  pad::Addr a = b.get(pad::Kind::Phone);
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL(0, a.v[i]);
}

// Abbinamenti di NimBLE da cancellare: quelli che il libro non conosce (il vecchio telefono dopo il nuovo).
static void test_stale_bonds() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(3, 1));
  const pad::Addr bonds[3] = {A(1), A(2, 1), A(3, 3)};
  int out[3] = {-1, -1, -1};
  TEST_ASSERT_EQUAL(1, pad::staleBonds(b, bonds, 3, out, 3));
  TEST_ASSERT_EQUAL(1, out[0]);
  TEST_ASSERT_EQUAL(0, pad::staleBonds(b, bonds, 1, out, 3));
  pad::BondBook empty;
  TEST_ASSERT_EQUAL(2, pad::staleBonds(empty, bonds, 3, out, 2));  // al più cap
}

// Prima accensione della 0.14.0: l'unico abbinamento della 0.13.0-ed è l'Xbox.
static void test_migrate_legacy_once() {
  pad::BondBook b;
  const pad::Addr one[1] = {A(1)};
  TEST_ASSERT_TRUE(pad::migrateLegacy(b, false, one, 1));
  TEST_ASSERT_TRUE(pad::sameAddr(A(1), b.get(pad::Kind::Xbox)));
  TEST_ASSERT_FALSE(b.has(pad::Kind::Phone));
  pad::BondBook c;
  TEST_ASSERT_FALSE(pad::migrateLegacy(c, true, one, 1));  // già migrato: «Dimentica» poi riavvio non lo riporta
  TEST_ASSERT_FALSE(c.any());
  const pad::Addr two[2] = {A(1), A(2)};
  TEST_ASSERT_FALSE(pad::migrateLegacy(c, false, two, 2));  // due: non si indovina, li toglie la riconciliazione
  TEST_ASSERT_FALSE(pad::migrateLegacy(c, false, one, 0));
  pad::BondBook d;
  d.set(pad::Kind::Phone, A(5, 1));
  TEST_ASSERT_FALSE(pad::migrateLegacy(d, false, one, 1));  // libro non vuoto
}

// D2: «associato» a radio spenta e migrazione con la stessa regola (esattamente un abbinamento).
static void test_legacy_is_xbox() {
  pad::BondBook b;
  TEST_ASSERT_TRUE(pad::legacyIsXbox(false, b, 1));
  TEST_ASSERT_FALSE(pad::legacyIsXbox(false, b, 0));
  TEST_ASSERT_FALSE(pad::legacyIsXbox(false, b, 2));
  TEST_ASSERT_FALSE(pad::legacyIsXbox(true, b, 1));
  b.set(pad::Kind::Phone, A(5, 1));
  TEST_ASSERT_FALSE(pad::legacyIsXbox(false, b, 1));
}

static void test_keys() {
  TEST_ASSERT_EQUAL_STRING("padx", pad::bondKey(pad::Kind::Xbox));
  TEST_ASSERT_EQUAL_STRING("padp", pad::bondKey(pad::Kind::Phone));
  TEST_ASSERT_EQUAL_STRING("padv", pad::kBondVersionKey);
}

static pad::ScanAd ad(const pad::Addr &a, bool gamepad, bool hid, bool phone, bool conn = true) {
  pad::ScanAd s;
  s.addr = a;
  s.gamepad = gamepad;
  s.hid = hid;
  s.phoneSvc = phone;
  s.connectable = conn;
  return s;
}

static void test_pick_bonded_any_time() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  pad::ScanPick p = pad::pickAd(ad(A(1), true, true, false), b, pad::Link::Connecting, pad::Kind::Xbox);
  TEST_ASSERT_TRUE(p.take && p.bonded);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Xbox), static_cast<int>(p.kind));
  p = pad::pickAd(ad(A(2, 3), false, false, true), b, pad::Link::Connecting, pad::Kind::Xbox);  // risolto
  TEST_ASSERT_TRUE(p.take && p.bonded);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(p.kind));
}

// N1 (riesame della correzione): dopo una rimessa delle chiavi il tipo è in quarantena (solo RAM): lo stesso
// dispositivo non si riprende finché non arriva «Associa…» o «Dimentica» di quel tipo, o la prossima accensione.
static void test_quarantine_after_restore() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  const pad::ScanAd x = ad(A(1), true, true, false), ph = ad(A(2, 1), false, false, true);
  // tre modi di uscirne
  for (int exitBy = 0; exitBy < 3; exitBy++) {
    pad::Quarantine q;
    TEST_ASSERT_TRUE(pad::pickAd(x, b, pad::Link::Connecting, pad::Kind::Xbox, q.mask).take);
    TEST_ASSERT_TRUE(q.restored(pad::Kind::Xbox));    // prima volta: avviso
    TEST_ASSERT_FALSE(q.restored(pad::Kind::Xbox));   // già in quarantena: niente secondo avviso
    for (int i = 0; i < 3; i++)                       // più giri di ricerca: mai ripreso
      TEST_ASSERT_FALSE(pad::pickAd(x, b, pad::Link::Connecting, pad::Kind::Xbox, q.mask).take);
    TEST_ASSERT_FALSE(pad::pickAd(x, b, pad::Link::Pairing, pad::Kind::Phone, q.mask).take);  // finestra dell'altro
    TEST_ASSERT_TRUE(pad::pickAd(ph, b, pad::Link::Connecting, pad::Kind::Xbox, q.mask).take);  // l'altro tipo sì
    if (exitBy == 0) q.pairing(pad::Kind::Phone);  // «Associa telefono» non libera l'Xbox
    if (exitBy == 0) TEST_ASSERT_FALSE(pad::pickAd(x, b, pad::Link::Connecting, pad::Kind::Xbox, q.mask).take);
    if (exitBy == 0) q.pairing(pad::Kind::Xbox);
    if (exitBy == 1) q.forgot(pad::Kind::Phone);
    if (exitBy == 1) TEST_ASSERT_FALSE(pad::pickAd(x, b, pad::Link::Connecting, pad::Kind::Xbox, q.mask).take);
    if (exitBy == 1) q.forgot(pad::Kind::Xbox);
    if (exitBy == 2) q.radioStart();
    TEST_ASSERT_TRUE(pad::pickAd(x, b, pad::Link::Connecting, pad::Kind::Xbox, q.mask).take);
    TEST_ASSERT_TRUE(q.restored(pad::Kind::Xbox));    // una nuova quarantena avvisa di nuovo
  }
  pad::Quarantine q;
  q.restored(pad::Kind::Phone);
  q.forgotAll();  // Ripristino: tutti
  TEST_ASSERT_EQUAL(0, q.mask);
}

static void test_pick_unknown_outside_pairing() {
  pad::BondBook b;
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), true, true, false), b, pad::Link::Connecting, pad::Kind::Xbox).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(8, 1), false, false, true), b, pad::Link::Connecting, pad::Kind::Phone).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(8, 1), false, false, true), b, pad::Link::Ready, pad::Kind::Phone).take);
}

static void test_pick_pairing_xbox() {
  pad::BondBook b;
  TEST_ASSERT_TRUE(pad::pickAd(ad(A(7), true, true, false), b, pad::Link::Pairing, pad::Kind::Xbox).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), true, false, false), b, pad::Link::Pairing, pad::Kind::Xbox).take);  // senza HID
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), true, true, false, false), b, pad::Link::Pairing, pad::Kind::Xbox).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(8, 1), false, false, true), b, pad::Link::Pairing, pad::Kind::Xbox).take);  // telefono
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), false, true, false), b, pad::Link::Pairing, pad::Kind::Xbox).take);  // HID senza aspetto gamepad
}

// Annuncio con tutti i segnali: si prende col tipo della finestra.
static void test_pick_all_signals_follows_window() {
  pad::BondBook b;
  pad::ScanPick p = pad::pickAd(ad(A(7), true, true, true), b, pad::Link::Pairing, pad::Kind::Xbox);
  TEST_ASSERT_TRUE(p.take);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Xbox), static_cast<int>(p.kind));
  p = pad::pickAd(ad(A(7), true, true, true), b, pad::Link::Pairing, pad::Kind::Phone);
  TEST_ASSERT_TRUE(p.take);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(p.kind));
}

static void test_pick_pairing_phone() {
  pad::BondBook b;
  const pad::ScanPick p = pad::pickAd(ad(A(8, 1), false, false, true), b, pad::Link::Pairing, pad::Kind::Phone);
  TEST_ASSERT_TRUE(p.take);
  TEST_ASSERT_FALSE(p.bonded);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(p.kind));
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(8, 1), false, false, true, false), b, pad::Link::Pairing, pad::Kind::Phone).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(7), true, true, false), b, pad::Link::Pairing, pad::Kind::Phone).take);  // Xbox
}

// Durante l'associazione di un tipo l'associato dell'altro tipo non si prende: non chiuderebbe la finestra.
static void test_pick_pairing_ignores_other_kind() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(1), true, true, false), b, pad::Link::Pairing, pad::Kind::Phone).take);
  TEST_ASSERT_TRUE(pad::pickAd(ad(A(2, 1), false, false, true), b, pad::Link::Pairing, pad::Kind::Phone).take);
  TEST_ASSERT_FALSE(pad::pickAd(ad(A(2, 1), false, false, true), b, pad::Link::Pairing, pad::Kind::Xbox).take);
}

// Review Focus «abbassamento della sicurezza»: l'Xbox come la 0.13.0-ed, il telefono solo SC con MITM, DisplayOnly.
static void test_sec_for_kinds() {
  const pad::SecCfg x = pad::secFor(pad::Kind::Xbox);
  TEST_ASSERT_EQUAL(pad::kIoNoInputOutput, x.ioCap);
  TEST_ASSERT_TRUE(x.bond);
  TEST_ASSERT_FALSE(x.mitm);
  TEST_ASSERT_FALSE(x.sc);
  TEST_ASSERT_FALSE(x.scOnly);
  const pad::SecCfg p = pad::secFor(pad::Kind::Phone);
  TEST_ASSERT_EQUAL(pad::kIoDisplayOnly, p.ioCap);
  TEST_ASSERT_TRUE(p.bond);
  TEST_ASSERT_TRUE(p.mitm);
  TEST_ASSERT_TRUE(p.sc);
  TEST_ASSERT_TRUE(p.scOnly);
}

static void test_link_secure_phone_needs_all_three() {
  TEST_ASSERT_TRUE(pad::linkSecure(pad::Kind::Phone, true, true, 16, true));
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, true, false, 16, true));  // Just Works
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, false, true, 16, true));
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, true, true, 7, true));    // chiave corta
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, true, true, 15, true));   // serve 16 esatti
  TEST_ASSERT_TRUE(pad::linkSecure(pad::Kind::Xbox, true, false, 16, false));   // Xbox: come oggi
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Xbox, false, false, 0, false));
}

// F5: da iniziatore NimBLE accetta il legacy con passkey (autenticato, chiave 16): per il telefono serve anche SC.
static void test_link_secure_phone_needs_sc() {
  TEST_ASSERT_FALSE(pad::linkSecure(pad::Kind::Phone, true, true, 16, false));
  TEST_ASSERT_TRUE(pad::linkSecure(pad::Kind::Xbox, true, false, 16, true));  // all'Xbox sc non serve
}

// ---- secOutcome (revisione del Task 10: I1, I2) ----
static pad::SecIn secIn(pad::Kind k, bool pairing, bool wasBonded, pad::SecResult r) {
  pad::SecIn in;
  in.kind = k;
  in.pairing = pairing;
  in.wasBonded = wasBonded;
  in.res = r;
  in.sameKeys = true;
  in.linkOk = true;
  in.bondedNow = wasBonded;
  in.snapshot = wasBonded;  // come blepad: copia del record presa con la LTK (fuori dalla finestra serve per partire)
  return in;
}

static bool nothing(const pad::SecOut &o) {
  return !o.ready && !o.deleteBond && !o.dropBook && !o.pairFail && !o.bondLost && !o.restore;
}

// Rimettere la copia del record: niente cancellazioni, niente libro toccato, nessun avviso; solo scollegare.
static void assertRestoreOnly(const pad::SecOut &o) {
  TEST_ASSERT_TRUE(o.restore);
  TEST_ASSERT_FALSE(o.ready);
  TEST_ASSERT_FALSE(o.deleteBond);
  TEST_ASSERT_FALSE(o.dropBook);
  TEST_ASSERT_FALSE(o.bondLost);
  TEST_ASSERT_FALSE(o.pairFail);
}

// I1: fuori dalla finestra, di tutti e due i tipi, un abbinamento nuovo (chiavi cambiate) non si tiene mai.
// Revisione finale I1 (b): si rimette il record salvato prima della cifratura, l'associato resta nel libro.
static void test_sec_new_bond_outside_window_rejected() {
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone}) {
    pad::SecIn in = secIn(k, false, true, pad::SecResult::Ok);
    in.sameKeys = false;  // NimBLE ha rifatto l'abbinamento (richiesta di sicurezza con MITM)
    in.bondedNow = true;
    assertRestoreOnly(pad::secOutcome(in));
  }
}

// Senza la copia (non dovrebbe succedere: fuori dalla finestra si parte solo con la LTK letta) le chiavi nuove non
// si tengono: comportamento di prima (via chiavi e libro, avviso).
static void test_sec_changed_keys_without_snapshot_deleted() {
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone})
    for (pad::SecResult r : {pad::SecResult::Ok, pad::SecResult::Failed, pad::SecResult::Cancelled}) {
      pad::SecIn in = secIn(k, false, true, r);
      in.sameKeys = false;
      in.bondedNow = true;
      in.snapshot = false;
      const pad::SecOut o = pad::secOutcome(in);
      TEST_ASSERT_FALSE(o.ready);
      TEST_ASSERT_FALSE(o.restore);
      TEST_ASSERT_TRUE(o.deleteBond);
      TEST_ASSERT_TRUE(o.dropBook);
      TEST_ASSERT_TRUE(o.bondLost);
    }
}

// La copia si rimette solo fuori dalla finestra: nella finestra le chiavi nuove sono l'associazione voluta.
static void test_sec_no_restore_in_window() {
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone}) {
    pad::SecIn in = secIn(k, true, true, pad::SecResult::Ok);
    in.sameKeys = false;
    in.bondedNow = true;
    const pad::SecOut o = pad::secOutcome(in);
    TEST_ASSERT_TRUE(o.ready);
    TEST_ASSERT_FALSE(o.restore);
  }
}

// Le stesse chiavi o le chiavi già tolte da NimBLE: niente da rimettere.
static void test_sec_no_restore_same_or_gone() {
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone})
    for (pad::SecResult r : {pad::SecResult::Ok, pad::SecResult::Failed, pad::SecResult::Cancelled}) {
      pad::SecIn in = secIn(k, false, true, r);
      TEST_ASSERT_FALSE(pad::secOutcome(in).restore);  // stesse chiavi
      in.bondedNow = false;
      in.sameKeys = false;
      TEST_ASSERT_FALSE(pad::secOutcome(in).restore);  // chiavi sparite
    }
}

// I1: nella finestra del suo tipo un abbinamento nuovo va bene (l'associato che si riassocia compreso).
static void test_sec_new_bond_in_window_ok() {
  pad::SecIn in = secIn(pad::Kind::Xbox, true, false, pad::SecResult::Ok);
  in.sameKeys = false;
  in.bondedNow = true;
  TEST_ASSERT_TRUE(pad::secOutcome(in).ready);
  in.wasBonded = true;
  TEST_ASSERT_TRUE(pad::secOutcome(in).ready);
}

static void test_sec_reconnect_same_keys_ok() {
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone}) {
    const pad::SecOut o = pad::secOutcome(secIn(k, false, true, pad::SecResult::Ok));
    TEST_ASSERT_TRUE(o.ready);
    TEST_ASSERT_FALSE(o.deleteBond || o.dropBook || o.bondLost || o.pairFail);
  }
}

// I2: un associato che cade o scade durante la cifratura, con le chiavi ancora in NimBLE: solo scollegato.
static void test_sec_bonded_failure_keeps_bond() {
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone})
    for (bool pairing : {false, true}) {
      pad::SecIn in = secIn(k, pairing, true, pad::SecResult::Failed);
      in.sameKeys = true;  // esplicito: stessa LTK di prima
      in.bondedNow = true;
      TEST_ASSERT_TRUE(nothing(pad::secOutcome(in)));
    }
}

// R1-I2: un abbinamento finito proprio mentre l'attesa si chiude (tempo scaduto o annullata) cambia la LTK anche
// senza Ok: fuori dalla finestra non si tiene mai (revisione finale I1: si rimette la copia del record).
static void test_sec_changed_keys_any_result_outside_window() {
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone})
    for (pad::SecResult r : {pad::SecResult::Ok, pad::SecResult::Failed, pad::SecResult::Cancelled}) {
      pad::SecIn in = secIn(k, false, true, r);
      in.sameKeys = false;
      in.bondedNow = true;
      in.linkOk = false;  // qualunque sia il collegamento
      assertRestoreOnly(pad::secOutcome(in));
    }
}

// R1-I1: fuori dalla finestra la scheda avvia la cifratura solo con una LTK salvata (senza, startSecurity farebbe
// un abbinamento nuovo); senza chiavi o senza LTK scollega e non tocca niente.
static void test_sec_start_rules() {
  TEST_ASSERT_EQUAL(static_cast<int>(pad::SecStart::Go), static_cast<int>(pad::secStart(false, true, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(pad::SecStart::NoLtk), static_cast<int>(pad::secStart(false, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(pad::SecStart::RefuseNew), static_cast<int>(pad::secStart(false, false, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(pad::SecStart::RefuseNew), static_cast<int>(pad::secStart(false, false, true)));
  for (bool was : {false, true})
    for (bool ltk : {false, true})  // nella finestra si va sempre (anche un associato senza LTK si riabbina)
      TEST_ASSERT_EQUAL(static_cast<int>(pad::SecStart::Go), static_cast<int>(pad::secStart(true, was, ltk)));
}

// I2: chiavi tolte da NimBLE (chiave mancante): il libro si mette in ordine, l'utente riassocia. Mai una finestra.
static void test_sec_keys_gone_tidies_book() {
  for (pad::Kind k : {pad::Kind::Xbox, pad::Kind::Phone}) {
    pad::SecIn in = secIn(k, false, true, pad::SecResult::Failed);
    in.bondedNow = false;
    const pad::SecOut o = pad::secOutcome(in);
    TEST_ASSERT_FALSE(o.ready);
    TEST_ASSERT_FALSE(o.deleteBond);
    TEST_ASSERT_TRUE(o.dropBook);
    TEST_ASSERT_TRUE(o.bondLost);
    TEST_ASSERT_FALSE(o.pairFail);
  }
}

// I2: collegamento cifrato ma sotto il requisito (telefono senza MITM o senza SC): via le chiavi e il libro.
static void test_sec_weak_link_deleted() {
  pad::SecIn in = secIn(pad::Kind::Phone, false, true, pad::SecResult::Ok);
  in.linkOk = false;
  pad::SecOut o = pad::secOutcome(in);
  TEST_ASSERT_FALSE(o.ready);
  TEST_ASSERT_TRUE(o.deleteBond);
  TEST_ASSERT_TRUE(o.dropBook);
  TEST_ASSERT_TRUE(o.bondLost);
  TEST_ASSERT_FALSE(o.pairFail);
  in = secIn(pad::Kind::Phone, true, false, pad::SecResult::Ok);  // telefono nuovo, abbinamento debole
  in.linkOk = false;
  in.bondedNow = true;
  o = pad::secOutcome(in);
  TEST_ASSERT_TRUE(o.deleteBond);
  TEST_ASSERT_TRUE(o.pairFail);  // F6: finestra chiusa
  TEST_ASSERT_FALSE(o.bondLost);
}

// F6: telefono nuovo, codice sbagliato o tempo scaduto: finestra chiusa; un abbinamento a metà si cancella.
static void test_sec_new_phone_failure() {
  pad::SecIn in = secIn(pad::Kind::Phone, true, false, pad::SecResult::Failed);
  pad::SecOut o = pad::secOutcome(in);
  TEST_ASSERT_TRUE(o.pairFail);
  TEST_ASSERT_FALSE(o.deleteBond || o.dropBook || o.bondLost || o.ready);
  in.bondedNow = true;
  TEST_ASSERT_TRUE(pad::secOutcome(in).deleteBond);
  in = secIn(pad::Kind::Xbox, true, false, pad::SecResult::Failed);  // Xbox nuovo: la finestra resta
  TEST_ASSERT_TRUE(nothing(pad::secOutcome(in)));
}

// Annullata (LUNGA ANNULLA, end(), finestra chiusa): solo un abbinamento appena nato si cancella.
static void test_sec_cancelled() {
  pad::SecIn in = secIn(pad::Kind::Phone, true, false, pad::SecResult::Cancelled);
  in.bondedNow = true;
  pad::SecOut o = pad::secOutcome(in);
  TEST_ASSERT_TRUE(o.deleteBond);
  TEST_ASSERT_FALSE(o.ready || o.dropBook || o.pairFail || o.bondLost);
  in = secIn(pad::Kind::Phone, true, true, pad::SecResult::Cancelled);  // associato: chiavi tenute
  TEST_ASSERT_TRUE(nothing(pad::secOutcome(in)));
}

// Dal vivo (0.14.0): l'iPhone associato continua ad annunciare per iOS (Continuity, Dov'è) con un RPA che si risolve
// nella sua identità; il telefono associato si prende solo con il servizio «Calamaio pad» nell'annuncio (l'app lo
// annuncia solo con la scheda Controller in primo piano). L'Xbox associato resta com'è: basta l'indirizzo.
static void test_pick_bonded_phone_needs_service() {
  pad::BondBook b;
  b.set(pad::Kind::Xbox, A(1));
  b.set(pad::Kind::Phone, A(2, 1));
  for (pad::Link l : {pad::Link::Connecting, pad::Link::Pairing}) {
    TEST_ASSERT_FALSE(pad::pickAd(ad(A(2, 3), false, false, false), b, l, pad::Kind::Phone).take);  // annuncio di iOS
    const pad::ScanPick p = pad::pickAd(ad(A(2, 3), false, false, true), b, l, pad::Kind::Phone);
    TEST_ASSERT_TRUE(p.take && p.bonded);
    TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Phone), static_cast<int>(p.kind));
  }
  // Xbox associato senza aspetto né HID nell'annuncio: preso come oggi
  const pad::ScanPick x = pad::pickAd(ad(A(1), false, false, false), b, pad::Link::Connecting, pad::Kind::Xbox);
  TEST_ASSERT_TRUE(x.take && x.bonded);
  TEST_ASSERT_EQUAL(static_cast<int>(pad::Kind::Xbox), static_cast<int>(x.kind));
}

// Difesa in profondità: dopo «servizio mancante» su un associato, quel tipo non si prende per 30 s, poi 60, 120, 240,
// 300 (tetto); un PRONTO azzera.
static void test_backoff() {
  pad::Backoff bo;
  TEST_ASSERT_FALSE(bo.blocked(0));
  bo.failed(1000);
  TEST_ASSERT_TRUE(bo.blocked(1000));
  TEST_ASSERT_TRUE(bo.blocked(30999));
  TEST_ASSERT_FALSE(bo.blocked(31000));
  const uint32_t steps[] = {60000, 120000, 240000, 300000, 300000};
  uint32_t t = 31000;
  for (uint32_t d : steps) {
    bo.failed(t);
    TEST_ASSERT_TRUE(bo.blocked(t + d - 1));
    TEST_ASSERT_FALSE(bo.blocked(t + d));
    t += d;
  }
  bo.ok();
  TEST_ASSERT_FALSE(bo.blocked(t));
  bo.failed(t);  // di nuovo da 30 s
  TEST_ASSERT_FALSE(bo.blocked(t + 30000));
  // millis() che gira: nessun blocco eterno
  pad::Backoff w;
  w.failed(0xFFFFF000u);
  TEST_ASSERT_TRUE(w.blocked(0xFFFFF000u + 20000));
  TEST_ASSERT_FALSE(w.blocked(0xFFFFF000u + 30000));
}

// Nella ricerca il blocco passa da pickAd come la quarantena: vale fuori dalla finestra, non nella finestra del tipo.
static void test_backoff_mask_in_pick() {
  pad::BondBook b;
  b.set(pad::Kind::Phone, A(2, 1));
  const pad::ScanAd ph = ad(A(2, 3), false, false, true);
  const uint8_t m = pad::Quarantine::kindMask(pad::Kind::Phone);
  TEST_ASSERT_FALSE(pad::pickAd(ph, b, pad::Link::Connecting, pad::Kind::Xbox, m).take);
  TEST_ASSERT_TRUE(pad::pickAd(ph, b, pad::Link::Pairing, pad::Kind::Phone, m).take);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_book);
  RUN_TEST(test_new_phone_keeps_xbox);
  RUN_TEST(test_new_xbox_keeps_phone);
  RUN_TEST(test_same_address_moves_kind);
  RUN_TEST(test_clear_one_kind);
  RUN_TEST(test_identity_type_matches);
  RUN_TEST(test_blob_round_trip);
  RUN_TEST(test_blob_rejects_bad);
  RUN_TEST(test_blob_rejects_zero_and_ff);
  RUN_TEST(test_book_get_zeroed);
  RUN_TEST(test_stale_bonds);
  RUN_TEST(test_migrate_legacy_once);
  RUN_TEST(test_legacy_is_xbox);
  RUN_TEST(test_keys);
  RUN_TEST(test_pick_bonded_any_time);
  RUN_TEST(test_pick_unknown_outside_pairing);
  RUN_TEST(test_quarantine_after_restore);
  RUN_TEST(test_pick_pairing_xbox);
  RUN_TEST(test_pick_pairing_phone);
  RUN_TEST(test_pick_pairing_ignores_other_kind);
  RUN_TEST(test_pick_all_signals_follows_window);
  RUN_TEST(test_sec_for_kinds);
  RUN_TEST(test_link_secure_phone_needs_all_three);
  RUN_TEST(test_link_secure_phone_needs_sc);
  RUN_TEST(test_sec_new_bond_outside_window_rejected);
  RUN_TEST(test_sec_new_bond_in_window_ok);
  RUN_TEST(test_sec_reconnect_same_keys_ok);
  RUN_TEST(test_sec_bonded_failure_keeps_bond);
  RUN_TEST(test_sec_keys_gone_tidies_book);
  RUN_TEST(test_sec_weak_link_deleted);
  RUN_TEST(test_sec_new_phone_failure);
  RUN_TEST(test_sec_cancelled);
  RUN_TEST(test_sec_changed_keys_any_result_outside_window);
  RUN_TEST(test_sec_start_rules);
  RUN_TEST(test_sec_changed_keys_without_snapshot_deleted);
  RUN_TEST(test_sec_no_restore_in_window);
  RUN_TEST(test_sec_no_restore_same_or_gone);
  RUN_TEST(test_pick_bonded_phone_needs_service);
  RUN_TEST(test_backoff);
  RUN_TEST(test_backoff_mask_in_pick);
  return UNITY_END();
}
