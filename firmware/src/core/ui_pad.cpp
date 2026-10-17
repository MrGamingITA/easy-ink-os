// Il controller nell'interfaccia: eventi di pad::MenuMapper tradotti nelle azioni del tasto PRG (vedi ui.h).
#include <string.h>

#include "strbuf.h"
#include "ui.h"

namespace cal {

RefreshKind Ui::padEvent(pad::MenuEvent e, Ms now, const WallClock &wc, pad::Kind src) {
  src_ = src == pad::Kind::Phone ? InputSrc::Phone : InputSrc::Xbox;
  // TODO(Task 11): il rilascio per silenzio del telefono (blepad::polledRelease) non deve mai arrivare qui come Select
  // (MenuMapper::update con A giù da meno di 700 ms lo darebbe): main.cpp passa ogni poll() da pad::mapPolled (reset,
  // solo HoldUp; provato in test_pad_phone). Qui arriva solo l'uscita di MenuMapper, mai un evento per report:
  // blepad::poll già scarta i report di mantenimento uguali all'ultimo (altrimenti disarmerebbero, regole 1 e 3).
  RefreshKind r;
  if (src_ == InputSrc::Phone) {
    // R1, F11, regola 1: un evento del telefono disarma ogni conferma armata dal PRG o dall'Xbox.
    const bool disarmed = phoneDisarm();
    const ScreenEntry &a = top(m_);
    const ScreenId id = a.id;
    const int depth = m_.ui.depth, choice = a.choice, sel = a.sel, session = m_.ui.sel;  // session: Home, Dettaglio
    r = padDispatch(e, now, wc);
    // regola 3: schermata, scelta o voce cambiate dal telefono annullano il gesto del PRG o dell'Xbox in corso
    const ScreenEntry &b = top(m_);
    if (b.id != id || m_.ui.depth != depth || b.choice != choice || b.sel != sel || m_.ui.sel != session)
      phoneMovedAt_ = now;
    if (disarmed) r = mergeRefresh(r, RefreshKind::Partial);
  } else {
    r = padDispatch(e, now, wc);
  }
  // Regola 4: fuori da qui gli emit (PRG, timer) non sono del telefono; il tenuto del telefono lo dice holdPhone.
  src_ = InputSrc::Prg;
  return r;
}

bool Ui::phoneDisarm() {
  UiState &u = m_.ui;
  bool any = u.unpairUntil || u.forgetUntil || u.forgetNetUntil;
  u.unpairUntil = u.forgetUntil = u.forgetNetUntil = 0;
  for (int k = 0; k < u.depth; k++)
    if (u.stack[k].id == ScreenId::Confirm && u.stack[k].until) {
      u.stack[k].until = 0;
      any = true;
    }
  return any;
}

void Ui::padInput(const char *what) {
  Sb(lastInput_, sizeof lastInput_).s(fromPhone() ? "Telefono: " : "Controller: ").s(what);
}

RefreshKind Ui::padDispatch(pad::MenuEvent e, Ms now, const WallClock &wc) {
  const ScreenEntry &tp = top(m_);
  // §7: durante il tenuto di A nel Permesso la scelta non cambia; su/giù annullano il tenuto (nessuna risposta).
  if ((e == pad::MenuEvent::Up || e == pad::MenuEvent::Down) && tp.id == ScreenId::Perm && m_.ui.holdPad &&
      m_.ui.holdPerm) {
    m_.ui.holdPerm = false;  // holdLabel nullo: via l'indicatore, lo scatto non risponde
    padInput(e == pad::MenuEvent::Up ? "su" : "giù");
    return padHint("Scelta cambiata: rilascia A e riprova",
                   "Scelta cambiata durante il tenuto: nessuna risposta, rilascia A e riprova", now);
  }
  switch (e) {
    case pad::MenuEvent::Up:
      padInput("su");
      return act(tp.id == ScreenId::Game ? Action::Next : Action::Prev, now, wc);  // nel gioco: alta (1×)
    case pad::MenuEvent::Down:
      padInput("giù");
      return act(tp.id == ScreenId::Game ? Action::Ok : Action::Next, now, wc);    // nel gioco: bassa (2×)
    case pad::MenuEvent::Select:
      padInput("A");
      // R1, F11, regola 3: A dell'Xbox premuto prima che il telefono cambiasse la schermata non vale
      if (src_ != InputSrc::Phone && phoneRaced())
        return padHint(kPhoneRaceToast, "A annullato: il telefono ha cambiato la schermata durante la pressione", now);
      // §7: una pressione non risponde mai
      if (tp.id == ScreenId::Perm)
        return padHint("Tieni premuto A per 3 s", "Permesso: tieni premuto A per 3 secondi per confermare la scelta",
                       now);
      if (tp.id == ScreenId::Game)  // F16: nel gioco A non sceglie, niente risposta per sbaglio
        return padHint(nullptr, "Gioco: su = più alta, giù = più bassa", now);
      return act(Action::Ok, now, wc);
    case pad::MenuEvent::Back:
      padInput("B");
      return act(Action::Back, now, wc);
    case pad::MenuEvent::Menu:
      padInput("Menu");
      return act(Action::Menu, now, wc);
    case pad::MenuEvent::HoldDown: return padHoldDown(now);
    case pad::MenuEvent::HoldUp: return padHoldUp(now, wc);
    case pad::MenuEvent::HoldFire: return padHoldFire(now, wc);
    default: return RefreshKind::None;
  }
}

// Nessun effetto, solo un avviso (toast e ridisegno se toastMsg non è nullo): Permesso senza risposta, tenuto
// annullato dalla croce, A nel gioco, gesto annullato dal telefono (regola 3).
RefreshKind Ui::padHint(const char *toastMsg, const char *action, Ms now) {
  req_ = RefreshKind::None;
  lastInputAt_ = now;
  if (toastMsg) {
    toast(m_, toastMsg, now);
    redraw(RefreshKind::Partial);
  }
  setAction(action);
  return finish(now);
}

RefreshKind Ui::padHoldDown(Ms now) {
  const InputSrc from = src_;          // keyDown() la rimette a Prg
  const Ms kf = keyFrom_, gf = gestureFrom_;
  const RefreshKind r = keyDown(now);  // indicatore come il tasto PRG
  src_ = from;
  if (src_ == InputSrc::Phone) {  // regole 2 e 3: contano solo PRG e Xbox
    keyFrom_ = kf;
    gestureFrom_ = gf;
  } else {
    gestureFrom_ = now;  // l'Xbox non ha pressioni multiple: ogni A è un gesto nuovo
  }
  UiState &u = m_.ui;
  const ScreenEntry &tp = top(m_);
  u.holdPad = true;
  u.holdPhone = src_ == InputSrc::Phone;
  u.holdPerm = tp.id == ScreenId::Perm;
  u.holdI = tp.i;
  u.holdChoice = tp.choice;
  scopy(u.holdRid, u.holdPerm ? tp.rid : "");
  padInput("A tenuto…");
  return r;
}

RefreshKind Ui::padHoldUp(Ms now, const WallClock &wc) {
  UiState &u = m_.ui;
  if (!u.holdPad) return RefreshKind::None;  // il tenuto in corso è del tasto PRG, o non c'è
  const bool done = u.holdDone, onPerm = u.holdPerm;
  const Ms el = now - u.holdSince;
  const InputSrc from = src_;
  const Ms ku = keyUpAt_;
  RefreshKind r = keyUp(now);  // keyUp guarda holdLabel: holdPad si spegne dopo
  src_ = from;
  if (u.holdPhone) keyUpAt_ = ku;  // regola 3: il rilascio del telefono non chiude gesti del PRG
  padInput("A rilasciato");
  u.holdPad = u.holdPerm = u.holdPhone = false;
  u.holdRid[0] = 0;
  if (done || el < kHoldShowMs) return r;
  ScreenEntry &tp = top(m_);
  // Conferma armata (Proteggi le chiavi, Ripristino): come la pressione lunga del PRG, resta armata.
  if (tp.id == ScreenId::Confirm && tp.until > now) return mergeRefresh(r, act(Action::Back, now, wc, true));
  if (onPerm && tp.id == ScreenId::Perm)
    return mergeRefresh(r, padHint("Troppo presto: tieni fino in fondo",
                                   "Rilasciato prima dei 3 s: nessuna risposta, la richiesta resta aperta", now));
  return r;
}

RefreshKind Ui::padHoldFire(Ms now, const WallClock &wc) {
  UiState &u = m_.ui;
  if (!u.holdOn || !u.holdPad || u.holdDone) return RefreshKind::None;  // PRG premuto nel frattempo, o già scattato
  ScreenEntry &tp = top(m_);
  if (u.holdPerm || tp.id == ScreenId::Perm) {
    // §7: dal controller si risponde solo così, e solo alla richiesta su cui il tenuto è iniziato.
    const bool same = tp.id == ScreenId::Perm && holdLabel(m_, now) != nullptr;
    req_ = RefreshKind::None;
    lastInputAt_ = now;
    padInput("A tenuto 3 s");
    u.holdDone = true;
    u.holdOk = same;
    // R1, F11, regola 3: tenuto dell'Xbox durante il quale il telefono ha cambiato la schermata: non vale
    if (same && !u.holdPhone && phoneRaced()) {
      u.holdOk = false;
      return padHint(kPhoneRaceToast, "Tenuto annullato: il telefono ha cambiato la schermata durante il tenuto", now);
    }
    if (!same) {
      setAction("Tenuto 3 s: la richiesta è cambiata, nessuna risposta");
      redraw(RefreshKind::Partial);  // via l'indicatore
      return finish(now);
    }
    u.holdOk = confirmPerm(tp, now);  // false: CONSENTI dal telefono, niente «FATTO · RILASCIA»
    redraw(RefreshKind::Partial);
    return finish(now);
  }
  const bool phone = u.holdPhone;
  const RefreshKind r = gesture(Gesture::VeryLong, now, wc);  // come il PRG: Riposo, o conferma armata
  Sb(lastInput_, sizeof lastInput_).s(phone ? "Telefono: " : "Controller: ").s("A tenuto 3 s");
  return r;
}

RefreshKind Ui::padLinkChanged(pad::Link l, pad::Kind kind, bool xboxBonded, bool phoneBonded, int batt, Ms now) {
  req_ = RefreshKind::None;
  Sys &w = m_.sys;
  const pad::Link was = w.padLink;
  const pad::Kind wasKind = w.padKind;
  const bool hasBatt = batt >= 0 && batt <= 100;
  const uint8_t b = hasBatt ? static_cast<uint8_t>(batt) : 0;
  if (l == was && kind == wasKind && xboxBonded == w.padBonded && phoneBonded == w.padPhoneBonded &&
      hasBatt == w.padHasBatt && b == w.padBatt)
    return finish(now);
  w.padLink = l;
  w.padKind = kind;
  w.padBonded = xboxBonded;
  w.padPhoneBonded = phoneBonded;
  w.padHasBatt = hasBatt;
  w.padBatt = b;
  const bool phone = kind == pad::Kind::Phone;
  if (l != pad::Link::Pairing) dropScreens(m_, ScreenId::PadCode);  // codice non più utile, anche sotto BattLow (M2)
  // Col Bluetooth spento di proposito, o dopo un «Dimentica», lo scollegamento che segue non si annuncia.
  const bool on = m_.set.padOn && !quietLost_;
  if (l == pad::Link::Ready && was == pad::Link::Ready && kind != wasKind) {  // m2: cambio di tipo in un solo giro
    toast(m_, phone ? "Telefono collegato" : "Controller collegato", now);
    quietLost_ = false;
  } else if (l != was) {
    if (l == pad::Link::Ready) toast(m_, phone ? "Telefono collegato" : "Controller collegato", now);
    else if (on && was == pad::Link::Ready)
      toast(m_, wasKind == pad::Kind::Phone ? "Telefono scollegato" : "Controller scollegato", now);
    else if (on && was == pad::Link::Pairing) toast(m_, "Associazione scaduta", now);
    quietLost_ = false;
  }
  redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::padPasskey(uint32_t key, Ms now) {
  req_ = RefreshKind::None;
  char code[8];
  Sb c(code, sizeof code);
  for (uint32_t d = 100000; d; d /= 10) c.ch(static_cast<char>('0' + (key / d) % 10));
  if (top(m_).id != ScreenId::PadCode) {  // un codice vecchio sepolto (es. sotto BattLow) si toglie: uno solo, in cima
    dropScreens(m_, ScreenId::PadCode);
    pushScreen(m_, makeScreen(ScreenId::PadCode));
  }
  scopy(top(m_).code, code);
  redraw(RefreshKind::Partial);
  setAction("Codice per il telefono a schermo");
  return finish(now);
}

RefreshKind Ui::padPairFailed(Ms now) {
  req_ = RefreshKind::None;
  dropScreens(m_, ScreenId::PadCode);
  toast(m_, "Associazione non riuscita", now);
  redraw(RefreshKind::Partial);
  setAction("Telefono: associazione non riuscita (non autenticato)");
  return finish(now);
}

RefreshKind Ui::padBondLost(pad::Kind k, Ms now) {
  req_ = RefreshKind::None;
  const bool phone = k == pad::Kind::Phone;
  toast(m_, phone ? "Abbinamento perso: scegli Associa telefono" : "Abbinamento perso: scegli Associa controller", now);
  redraw(RefreshKind::Partial);
  setAction(phone ? "Telefono: abbinamento perso" : "Controller: abbinamento perso");
  return finish(now);
}

void Ui::gbAct(ActId id, Ms now, char *msg, size_t cap) {
  Settings &st = m_.set;
  Sys &w = m_.sys;
  Sb b(msg, cap);
  if (id == ActId::GbPair || id == ActId::GbPairPhone) {
    const bool phone = id == ActId::GbPairPhone;
    st.padOn = true;  // associare accende il Bluetooth (resta acceso finché c'è un associato)
    padReq_ = phone ? PadReq::PairPhone : PadReq::Pair;
    toast(m_, phone ? "Metti l'app Calamaio sulla scheda Controller" : "Metti il controller in associazione", now);
    b.s(phone ? "Associa telefono: apri l'app Calamaio, scheda Controller, e scrivi il codice che compare qui"
              : "Associa controller: tieni premuto il tasto di associazione finché il logo lampeggia veloce");
    return;
  }
  const bool phone = id == ActId::GbForgetPhone;
  Ms &until = phone ? m_.ui.gbForgetPhoneUntil : m_.ui.gbForgetUntil;
  if (!(phone ? w.padPhoneBonded : w.padBonded)) {
    toast(m_, phone ? "Nessun telefono associato" : "Nessun controller associato", now);
    b.s(phone ? "Dimentica telefono: nessun telefono associato" : "Dimentica controller: nessun controller associato");
    return;
  }
  if (!(until > now)) {  // come «Dimentica rete», seconda scelta entro kForgetMs
    until = now + kForgetMs;
    toast(m_, "Scegli ancora per confermare", now);
    b.s(phone ? "Dimentica telefono: scegli ancora per confermare" : "Dimentica controller: scegli ancora per confermare");
    return;
  }
  until = 0;
  padReq_ = phone ? PadReq::ForgetPhone : PadReq::Forget;
  if (phone) w.padPhoneBonded = false;
  else w.padBonded = false;
  st.padOn = w.padBonded || w.padPhoneBonded;  // il Bluetooth si spegne solo senza associati
  quietLost_ = true;
  toast(m_, phone ? "Telefono dimenticato" : "Controller dimenticato", now);
  b.s(phone ? "Telefono dimenticato: per usarlo di nuovo, Associa telefono"
            : "Controller dimenticato: per usarlo di nuovo, Associa controller");
}

}  // namespace cal
