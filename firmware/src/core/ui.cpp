#include "ui.h"

#include <math.h>
#include <string.h>

#include "accessories.h"
#include "apply_state.h"
#include "pet.h"
#include "strbuf.h"
#include "workday.h"

namespace cal {

const char *netCmdKey(NetCmd c) {
  static const char *const k[] = {"scan", "provision", "cancel-provision", "join", "forget", "reboot", "settings", "ack", "dismiss",
                                  "pair-confirm", "pair-cancel", "perm-reply", "unpair", "protect-keys", "factory-reset",
                                  "avatar-need", "ota-go", "status", "forget-net", "accessory-wear", "accessory-need", "anchors-need",
                                  "themecur", "avatar-ack"};
  return k[static_cast<int>(c)];
}

bool phoneForbidden(NetCmd c) {
  switch (c) {
    case NetCmd::ProtectKeys: case NetCmd::FactoryReset: case NetCmd::PairConfirm: case NetCmd::Unpair:
    case NetCmd::Forget: case NetCmd::ForgetNet: case NetCmd::Provision: case NetCmd::Join:
      return true;
    default:
      return false;
  }
}

bool dropFromPhone(const PermEvent &e) { return e.src == InputSrc::Phone && e.outcome == PermOutcome::Allow; }

bool dropFromPhone(const NetRequest &r) {
  if (r.src != InputSrc::Phone) return false;
  return phoneForbidden(r.cmd) || (r.cmd == NetCmd::PermReply && r.ok);
}

const char *refreshKey(RefreshKind k) {
  static const char *const n[] = {"none", "partial", "quick", "full"};
  return n[static_cast<int>(k)];
}

RefreshKind RefreshCounter::apply(RefreshKind req) {
  switch (req) {
    case RefreshKind::Partial:
      // Screen.partial(): disegna, conta e, raggiunta la soglia, fa subito un completo.
      // Sul pannello vero basta il completo, che copre anche il parziale.
      if (++partials_ >= fullEvery_) { partials_ = 0; return RefreshKind::Full; }
      return RefreshKind::Partial;
    case RefreshKind::Quick:
      partials_++;  // Screen.quick() conta come un parziale ma non controlla la soglia
      return RefreshKind::Quick;
    case RefreshKind::Full:
      partials_ = 0;
      return RefreshKind::Full;
    default:
      return RefreshKind::None;
  }
}

double Rng::next() {
  s_ ^= s_ << 13;
  s_ ^= s_ >> 17;
  s_ ^= s_ << 5;
  return s_ / 4294967296.0;
}

// ---------- metriche usate dal disegno per limitare lo scorrimento ----------

namespace {
struct Met { int sb, f3, pad, hint; };
Met met(const DeviceProfile &dev) {
  Met m;
  if (dev.cls == 'S') { m.sb = 0; m.f3 = 10; m.pad = 6; m.hint = 0; return m; }
  if (dev.cls == 'M') { m.sb = 24; m.f3 = 11; m.pad = 8; m.hint = 20; return m; }
  if (dev.cls == 'W') { m.sb = 24; m.f3 = 11; m.pad = 10; m.hint = 20; return m; }
  const double k = dev.H / 480.0;
  m.sb = jsRound(32 * k); m.f3 = jsRound(14 * k); m.pad = jsRound(14 * k); m.hint = jsRound(26 * k);
  return m;
}
}  // namespace

int scrollVisible(const DeviceProfile &dev, ScreenId id) {
  const bool S = dev.cls == 'S';
  const Met mt = met(dev);
  const int t0 = S ? 16 : mt.sb + mt.pad;  // viewHeader()
  const int bottom = dev.H - (S ? 0 : mt.hint) - 2;
  int fs, lh;
  if (id == ScreenId::Storico) { fs = S ? 10 : mt.f3; lh = fs + (S ? 4 : 7); }
  else { fs = S ? 10 : mt.f3 + 1; lh = fs + (S ? 5 : 9); }
  int vis = static_cast<int>(floorDiv(bottom - t0, lh));
  return vis < 1 ? 1 : vis;
}

// ---------- Ui ----------

Ui::Ui(const DeviceProfile &dev) : dev_(dev), list_(sharedMenuList()), req_(RefreshKind::None), frame_(0), lastFrame_(0), lastInputAt_(0),
                                   lastMin_(-1), lastPage_(0), beepUntil_(0), permFn_(0), permCtx_(0),
                                   netFn_(0), netCtx_(0), provActive_(false), lastState_(0), dismissSeq_(0) {
  memset(&m_, 0, sizeof m_);
  memset(&list_, 0, sizeof list_);
  lastInput_[0] = 0;
  lastAction_[0] = 0;
  permText_[0] = 0;
}

RefreshKind Ui::begin(Ms now, const WallClock &wc) {
  newModel(m_, now, wc);
  frame_ = 0;
  lastFrame_ = 0;
  lastInputAt_ = 0;
  lastMin_ = -1;
  lastPage_ = 0;
  beepUntil_ = 0;
  lastInput_[0] = 0;
  lastAction_[0] = 0;
  return RefreshKind::Full;
}

void Ui::setAction(const char *msg) { scopy(lastAction_, msg); }

// Il simulatore limita sel e off mentre disegna: qui si fa lo stesso quando c'è un refresh.
void Ui::normalize(Ms now) {
  ScreenEntry &tp = top(m_);
  if (tp.id == ScreenId::List) {
    MenuList &L = list_;
    WallClock dummy = {0};
    menuList(tp.menu, dev_, m_, now, dummy, L, tp.ssid);  // conta solo le voci: l'ora non serve
    int hi = L.n - 1;
    tp.sel = tp.sel > hi ? hi : tp.sel;
    if (tp.sel < 0) tp.sel = 0;
  } else if (tp.id == ScreenId::WifiScan && !(tp.scanUntil > now)) {
    if (tp.sel > m_.nNets - 1) tp.sel = m_.nNets - 1;
    if (tp.sel < 0) tp.sel = 0;
  } else if (tp.id == ScreenId::Storico) {
    int maxOff = m_.log.n - scrollVisible(dev_, ScreenId::Storico);
    if (maxOff < 0) maxOff = 0;
    if (tp.off > maxOff) tp.off = maxOff;
  } else if (tp.id == ScreenId::Servizi) {
    int rows = 2 + m_.nServices + m_.nPrs;
    int maxOff = rows - scrollVisible(dev_, ScreenId::Servizi);
    if (maxOff < 0) maxOff = 0;
    if (tp.off > maxOff) tp.off = maxOff;
    if (tp.off < 0) tp.off = 0;
  }
}

RefreshKind Ui::finish(Ms now) {
  // Rete vera: uscire dal QR (indietro, menu, riposo…) spegne AP e portale.
  if (m_.sys.live && provActive_ && !inStack(ScreenId::Qr) && !inStack(ScreenId::Connecting)) {
    provActive_ = false;
    m_.sys.apPass[0] = 0;
    emitNet(NetCmd::CancelProvision);
  }
  if (req_ != RefreshKind::None) normalize(now);
  return req_;
}

bool Ui::inStack(ScreenId id) const {
  for (int k = 0; k < m_.ui.depth; k++)
    if (m_.ui.stack[k].id == id) return true;
  return false;
}

void Ui::emitNet(NetCmd c, const char *ssid) {
  if (!netFn_) return;
  // R1 e F11, seconda barriera: dal telefono niente eFuse, ripristino, abbinamento col PC né Wi-Fi.
  const bool phone = fromPhone();
  if (phone && phoneForbidden(c)) return;
  NetRequest r;
  memset(&r, 0, sizeof r);
  r.cmd = c;
  r.src = phone ? InputSrc::Phone : src_;
  scopy(r.ssid, ssid ? ssid : "");
  netFn_(r, netCtx_);
}

void Ui::fillItems(const ScreenEntry &tp, Ms now, const WallClock &wc) {
  if (tp.id == ScreenId::WifiScan) netItems(m_, list_);
  else if (tp.id == ScreenId::List) menuList(tp.menu, dev_, m_, now, wc, list_, tp.ssid);
  else { list_.n = 0; list_.title = ""; list_.truncated = false; }
}

const MenuList &Ui::items(Ms now, const WallClock &wc) {
  fillItems(top(m_), now, wc);
  return list_;
}

bool Ui::ledOn(Ms now) const {
  return dev_.led && m_.set.led && attention(m_) && !quiet(m_, now) && floorMod(floorDiv(now, 500), 2) == 0;
}

void Ui::emitPerm(PermOutcome o, int session, const char *rid, Ms now) {
  // §7, scelta A: seconda barriera dopo confirmPerm. Un «consenti» dal telefono non esce mai.
  if (o == PermOutcome::Allow && fromPhone()) return;
  if (!permFn_) return;
  PermEvent e;
  e.outcome = o;
  e.session = session;
  e.requestId = rid ? rid : "";
  e.sessionName = session >= 0 && session < m_.nSessions ? m_.sessions[session].name : "";
  e.at = now;
  e.src = fromPhone() ? InputSrc::Phone : src_;
  permFn_(e, permCtx_);
}

bool Ui::canAnswer(const Session &s) const {
  // Con la rete serve anche la richiesta verificata del servizio (perm-request), non solo lo stato.
  return permFromDisplay(m_) && (!m_.sys.live || permReqFor(m_, s.id) >= 0);
}

void Ui::changed(Ms now) {
  if (!quiet(m_, now)) m_.burstUntil = now + 4000;
  redraw(RefreshKind::Partial);
}

void Ui::toggleMute(Ms now) {
  if (muted(m_, now)) {
    m_.set.mute = 0;
    m_.set.muteKind = MuteKind::None;
    toast(m_, "Avvisi attivi", now);
    redraw(RefreshKind::Partial);
    setAction("Avvisi riattivati");
    return;
  }
  m_.set.mute = now + kMuteMs;
  m_.set.muteKind = MuteKind::Hour;
  toast(m_, "Silenziato per 1 ora", now);
  redraw(RefreshKind::Partial);
  setAction("Silenzia per 1 ora");
}

RefreshKind Ui::keyDown(Ms now) {
  src_ = InputSrc::Prg;
  req_ = RefreshKind::None;
  UiState &u = m_.ui;
  u.holdOn = true;
  u.holdSince = now;
  u.holdNext = now + kHoldStepMs;
  u.holdDone = u.holdOk = false;
  u.holdPad = u.holdPerm = u.holdPhone = false;  // 0.11.0: il tasto PRG vince su un tenuto di A in corso
  keyFrom_ = now;
  // regola 3: una pressione entro la finestra delle multiple fa parte dello stesso gesto
  if (keyUpAt_ < 0 || now - keyUpAt_ > static_cast<Ms>(GestureRecognizer::kMultiMs)) gestureFrom_ = now;
  scopy(lastInput_, "Tasto premuto…");
  return finish(now);
}

RefreshKind Ui::keyUp(Ms now) {
  src_ = InputSrc::Prg;
  req_ = RefreshKind::None;
  UiState &u = m_.ui;
  const bool on = u.holdOn, done = u.holdDone, ok = u.holdOk;
  u.holdOn = u.holdDone = u.holdOk = false;
  keyUpAt_ = now;
  if (on && done) {  // l'azione è già partita a 3 s: il rilascio non è un gesto
    scopy(lastInput_, "Rilasciato");
    if (ok) redraw(RefreshKind::Partial);  // via «FATTO · RILASCIA»
  } else if (on && now - u.holdSince >= 700 && holdLabel(m_, now)) {
    redraw(RefreshKind::Partial);  // l'indicatore era a schermo: va tolto
  }
  return finish(now);
}

bool Ui::phoneRefused(const char *action, Ms now) {
  if (!fromPhone()) return false;
  toast(m_, kPhoneConfirmToast, now);
  redraw(RefreshKind::Partial);
  setAction(action);
  return true;
}

RefreshKind Ui::gesture(Gesture g, Ms now, const WallClock &wc) {
  const bool viaPhone = fromPhone();  // tenuto del telefono che scatta (padHoldFire)
  src_ = InputSrc::Prg;
  scopy(lastInput_, gestureName(g));
  // R1, F11, regola 3: 2× o tenuto 3 s del PRG (o dell'Xbox) durante il quale il telefono ha cambiato schermata,
  // scelta o voce: il gesto non vale, niente parte.
  if (!viaPhone && (g == Gesture::Press2 || g == Gesture::VeryLong) && phoneRaced()) {
    if (g == Gesture::VeryLong && m_.ui.holdOn && !m_.ui.holdDone) {
      m_.ui.holdDone = true;  // il rilascio non è un gesto
      m_.ui.holdOk = false;
    }
    return padHint(kPhoneRaceToast, "Gesto annullato: il telefono ha cambiato la schermata durante il gesto", now);
  }
  if (g == Gesture::VeryLong && m_.ui.holdOn && !m_.ui.holdDone) {  // holdFire() del simulatore
    m_.ui.holdDone = true;
    m_.ui.holdOk = holdLabel(m_, now) != nullptr;
    const bool ok = m_.ui.holdOk;
    RefreshKind r = act(Action::Sleep, now, wc);
    if (!lastAction_[0]) setAction("—");
    if (ok) r = mergeRefresh(r, RefreshKind::Partial);  // «FATTO · RILASCIA»
    return r;
  }
  RefreshKind r = RefreshKind::None;
  switch (g) {
    case Gesture::Press1: r = act(Action::Next, now, wc); break;
    case Gesture::Press2: r = act(Action::Ok, now, wc); break;
    case Gesture::Press3: r = act(Action::Menu, now, wc); break;
    case Gesture::Long: r = act(Action::Back, now, wc, true); break;
    case Gesture::VeryLong: r = act(Action::Sleep, now, wc); break;
    default: setAction(""); break;
  }
  if (!lastAction_[0]) setAction("—");
  return r;
}

RefreshKind Ui::act(Action a, Ms t, const WallClock &wc, bool homeMute) {
  req_ = RefreshKind::None;
  lastAction_[0] = 0;
  Model &m = m_;
  ScreenEntry &tp = top(m);
  const bool S = dev_.cls == 'S';
  const int n = m.nSessions;
  char msg[128];
  lastInputAt_ = t;

  if (tp.id == ScreenId::Boot) { setAction("Avvio in corso: tasto ignorato"); return finish(t); }
  if (tp.id == ScreenId::Sleep) { popScreen(m); redraw(RefreshKind::Quick); setAction("Riattivato"); return finish(t); }
  // M6: batteria scarica (sulla scheda il tasto la sveglia dal sonno profondo e la batteria si ricontrolla).
  if (tp.id == ScreenId::BattLow) {
    if (!m.sys.usb) { setAction("Batteria scarica: collega la USB-C"); return finish(t); }
    popScreen(m);
    redraw(RefreshKind::Quick);
    setAction("Riattivato");
    return finish(t);
  }
  // Aggiornamento: durante la ricezione nessun gesto conta; errore e rifiuto si chiudono con qualunque gesto.
  if (tp.id == ScreenId::Ota) {
    if (m.ota.phase == OtaPhase::Error || m.ota.phase == OtaPhase::Rejected) {
      closeOta();
      redraw(RefreshKind::Quick);
      setAction("Chiuso");
      return finish(t);
    }
    setAction("Aggiornamento in corso: non spegnere");
    return finish(t);
  }
  if (gbInput(a, t)) return finish(t);  // 0.12.0: giochi: avvio, partita, pausa
  // M6: calibrazione in corso: solo 3× la interrompe.
  if (tp.id == ScreenId::Calib && calRunning(m)) {
    if (a == Action::Menu) { calibAbort("Calibrazione annullata", t); setAction("Calibrazione annullata"); return finish(t); }
    setAction("Calibrazione in corso: 3X per annullare");
    return finish(t);
  }
  if (!muted(m, t)) m.burstUntil = t + 3000;

  if (a == Action::Sleep && tp.id == ScreenId::PadCode) {  // F9: il tenuto non manda a riposo sul codice
    setAction("Scrivi il codice sul telefono o annulla");
    return finish(t);
  }
  if (a == Action::Sleep && tp.id == ScreenId::Confirm) {
    // Proteggi le chiavi, Ripristino di fabbrica: il tenuto 3 s qui conferma invece di mandare a riposo.
    // R1, regola 2: conta solo un tenuto iniziato dopo l'armatura (non quello cominciato altrove, prima).
    if (tp.until > t && keyFrom_ >= tp.armedAt) { if (!confirmAction(tp, t)) m.ui.holdOk = false; return finish(t); }
    m.ui.holdOk = false;
    toast(m, "Prima 2X, poi tieni 3 s", t);
    redraw(RefreshKind::Partial);
    setAction("Prima 2 pressioni, poi tieni premuto 3 secondi");
    return finish(t);
  }
  if (a == Action::Sleep) {
    pushScreen(m, makeScreen(ScreenId::Sleep));
    redraw(RefreshKind::Quick);
    setAction("Riposo: orologio e Goccia che dorme");
    return finish(t);
  }
  if (a == Action::Mute) { toggleMute(t); return finish(t); }
  if (a == Action::Home) {
    resetStack(m, makeScreen(ScreenId::Home));
    redraw(RefreshKind::Quick);
    setAction("Torna alla Home");
    return finish(t);
  }
  if (a == Action::Menu) {
    if (tp.id == ScreenId::Perm) { setAction("Prima rispondi o annulla"); return finish(t); }
    if (tp.id == ScreenId::Pair) { setAction("Prima conferma o annulla"); return finish(t); }
    if (tp.id == ScreenId::PadCode) { setAction("Prima scrivi il codice sul telefono o annulla"); return finish(t); }
    if (tp.id == ScreenId::Home || tp.id == ScreenId::Detail) {
      ScreenEntry e = makeScreen(ScreenId::List);
      e.menu = MenuId::Main;
      pushScreen(m, e);
      redraw(RefreshKind::Quick);
      setAction("Apri il Menu");
      return finish(t);
    }
    resetStack(m, makeScreen(ScreenId::Home));
    redraw(RefreshKind::Quick);
    setAction("Torna alla Home");
    return finish(t);
  }
  if (a == Action::Back) {
    if (tp.id == ScreenId::PadCode) {  // 0.14.0: LUNGA ANNULLA, l'associazione del telefono si ferma
      popScreen(m);
      padReq_ = PadReq::CancelPair;
      redraw(RefreshKind::Quick);
      setAction("Associazione del telefono annullata");
      return finish(t);
    }
    // Conferma armata (dopo il 2×): rilasciato fra 0,7 e 3 s non torna indietro, resta armata altri 10 s.
    if (homeMute && tp.id == ScreenId::Confirm && tp.until > t) {
      tp.until = t + 10000;
      toast(m, "Troppo presto: tieni fino in fondo", t);
      redraw(RefreshKind::Partial);
      setAction("Rilasciato prima dei 3 s: la conferma resta armata");
      return finish(t);
    }
    if (tp.id == ScreenId::Perm) {
      // Nessuna scadenza: si chiude senza rispondere e la richiesta resta aperta, qui (Dettaglio, 2×) e nel terminale.
      char rid[kReqIdLen];
      scopy(rid, tp.rid);
      const int i = tp.i;
      popScreen(m);
      emitPerm(PermOutcome::Cancelled, i, rid, t);
      toast(m, "Annullato · resta in attesa", t);
      redraw(RefreshKind::Quick);
      setAction("Annulla: la richiesta resta aperta, qui e nel terminale");
      return finish(t);
    }
    if (tp.id == ScreenId::Pair) {
      popScreen(m);
      if (m.sys.live) emitNet(NetCmd::PairCancel);
      toast(m, "Abbinamento annullato", t);
      logEv(m, 0, "abbinamento annullato sul display", t);
      redraw(RefreshKind::Quick);
      setAction("Abbinamento annullato: nessuna chiave salvata");
      return finish(t);
    }
    if (m.ui.depth > 1) { popScreen(m); redraw(RefreshKind::Quick); setAction("Indietro"); return finish(t); }
    if (homeMute) { toggleMute(t); return finish(t); }
    setAction("Sei già nella Home");
    return finish(t);
  }

  if (tp.id == ScreenId::PadCode && (a == Action::Next || a == Action::Prev || a == Action::Ok)) {
    setAction("Scrivi il codice sul telefono");
    return finish(t);
  }
  if (tp.id == ScreenId::Game && (a == Action::Next || a == Action::Ok)) { gameAct(a == Action::Next, t); return finish(t); }
  if (a == Action::Next || a == Action::Prev) {
    const int d = a == Action::Next ? 1 : -1;
    if (!m.sys.pc && (tp.id == ScreenId::Home || tp.id == ScreenId::Detail)) {
      setAction("PC non raggiungibile: apri il Menu");
      return finish(t);
    }
    if (n == 0 && (tp.id == ScreenId::Home || tp.id == ScreenId::Detail)) { setAction("Nessuna sessione"); return finish(t); }
    if (tp.id == ScreenId::Home) {
      int base = S ? (m.set.homeStyle == HomeStyle::Una ? unaIdx(m, t) : sIdx(m, t)) : m.ui.sel;
      m.ui.sel = (base + d + n) % n;
      m.ui.page = m.ui.sel;
      if (S) m.ui.selUntil = t + kSelMs;
      redraw(RefreshKind::Partial);
      setAction(Sb(msg, sizeof msg).s("Seleziona ").s(m.sessions[m.ui.sel].name).str());
      return finish(t);
    }
    if (tp.id == ScreenId::Detail) {
      m.ui.sel = (m.ui.sel + d + n) % n;
      redraw(RefreshKind::Partial);
      setAction(Sb(msg, sizeof msg).s("Sessione ").s(m.sessions[m.ui.sel].name).str());
      return finish(t);
    }
    if (tp.id == ScreenId::Perm) {
      tp.choice = 1 - tp.choice;
      redraw(RefreshKind::Partial);
      setAction(tp.choice == 0 ? "Scelta: CONSENTI" : "Scelta: NEGA");
      return finish(t);
    }
    if (tp.id == ScreenId::List || tp.id == ScreenId::WifiScan) {
      if (tp.id == ScreenId::WifiScan && tp.scanUntil > t) { setAction("Ricerca in corso"); return finish(t); }
      fillItems(tp, t, wc);
      if (!list_.n) { setAction("Elenco vuoto"); return finish(t); }
      tp.sel = (tp.sel + d + list_.n) % list_.n;
      redraw(RefreshKind::Partial);
      setAction(Sb(msg, sizeof msg).s("Voce: ").s(list_.items[tp.sel].label).str());
      return finish(t);
    }
    if (tp.id == ScreenId::Storico || tp.id == ScreenId::Servizi) {
      const bool sto = tp.id == ScreenId::Storico;
      const int len = sto ? m.log.n : 2 + m.nServices + m.nPrs, stepN = sto ? 3 : 2;
      const int max = len - 4 > 0 ? len - 4 : 0;
      tp.off += d * stepN;
      if (tp.off > max) tp.off = 0;
      if (tp.off < 0) tp.off = max;
      redraw(RefreshKind::Partial);
      setAction("Scorri lo storico");
      return finish(t);
    }
    setAction("Niente da scorrere qui");
    return finish(t);
  }

  if (a == Action::Ok) {
    if (!m.sys.pc && (tp.id == ScreenId::Home || tp.id == ScreenId::Detail)) {
      setAction("PC non raggiungibile: apri il Menu");
      return finish(t);
    }
    if (n == 0 && (tp.id == ScreenId::Home || tp.id == ScreenId::Detail)) { setAction("Nessuna sessione"); return finish(t); }
    if (tp.id == ScreenId::Home) {
      if (S) m.ui.sel = m.set.homeStyle == HomeStyle::Una ? unaIdx(m, t) : sIdx(m, t);
      m.ui.selUntil = 0;
      pushScreen(m, makeScreen(ScreenId::Detail));
      redraw(RefreshKind::Quick);
      setAction(Sb(msg, sizeof msg).s("Apri ").s(m.sessions[m.ui.sel].name).str());
      return finish(t);
    }
    if (tp.id == ScreenId::Detail) {
      Session &s = m.sessions[m.ui.sel];
      const int q = m.sys.live ? permReqFor(m, s.id) : -1;
      if (s.state == SessionState::Permission || q >= 0) {
        if (!m.set.permOn) {
          toast(m, "Risposte dal display spente", t);
          redraw(RefreshKind::Partial);
          setAction("Risposte dal display disattivate (Menu › Avvisi)");
          return finish(t);
        }
        if (!canAnswer(s)) {
          // Senza abbinamento (o senza la richiesta firmata del servizio) il servizio non accetterebbe la risposta.
          toast(m, "Rispondi nel terminale", t);
          redraw(RefreshKind::Partial);
          setAction(permFromDisplay(m) ? "Nessuna richiesta verificata dal PC: rispondi nel terminale"
                                       : "Permessi dal display dopo l'abbinamento: rispondi nel terminale");
          return finish(t);
        }
        // §7: NEGA preselezionato; per consentire servono due gesti (1× poi 2×). Nessuna scadenza.
        ScreenEntry e = makeScreen(ScreenId::Perm);
        e.i = m.ui.sel;
        e.choice = 1;
        if (q >= 0) scopy(e.rid, m.perms[q].id);
        pushScreen(m, e);
        permWaitText(t, permSince(m, e), permText_, sizeof permText_);
        redraw(RefreshKind::Quick);
        setAction(Sb(msg, sizeof msg).s("Rispondi al permesso di ").s(s.name).s(" (NEGA preselezionato)").str());
        return finish(t);
      }
      if (s.state == SessionState::Input) {
        toast(m, "Rispondi nel terminale", t);
        redraw(RefreshKind::Partial);
        setAction("Le domande si rispondono nel terminale");
        return finish(t);
      }
      if (canArchive(s)) { archive(m.ui.sel, t); return finish(t); }
      setAction("Niente da confermare per questa sessione");
      return finish(t);
    }
    if (tp.id == ScreenId::Perm) { confirmPerm(tp, t); return finish(t); }
    if (tp.id == ScreenId::Pair) {
      if (tp.sel == 1) { pairConfirm(t); return finish(t); }
      toast(m, "Prima scrivi il codice sul PC", t);
      redraw(RefreshKind::Partial);
      setAction("Scrivi il codice sul PC: poi qui si conferma con 2 pressioni");
      return finish(t);
    }
    if (tp.id == ScreenId::Confirm) {
      // R1, regola 1: il telefono non arma Proteggi né Ripristino.
      if (phoneRefused("Telefono: Proteggi e Ripristino si confermano col tasto PRG o col controller", t)) return finish(t);
      tp.until = t + kArmMs;
      tp.armedAt = t;
      redraw(RefreshKind::Partial);
      setAction("Ora tieni premuto 3 secondi per confermare");
      return finish(t);
    }
    if (tp.id == ScreenId::List) { activate(tp, t, wc); return finish(t); }
    if (tp.id == ScreenId::WifiScan) {
      if (tp.scanUntil > t) { setAction("Ricerca in corso"); return finish(t); }
      if (tp.sel < 0 || tp.sel >= m.nNets) { setAction(""); return finish(t); }
      const NetEntry net = m.nets[tp.sel];  // copia: la pila cambia qui sotto
      // F11: dal telefono si cerca e si guarda, ma il Wi-Fi non si cambia (configurazione o rete aperta).
      if (phoneRefused("Telefono: il Wi-Fi si cambia col tasto PRG o col controller", t)) return finish(t);
      if (net.lock) {
        ScreenEntry e = makeScreen(ScreenId::Qr);
        scopy(e.ssid, net.ssid);
        pushScreen(m, e);
        if (m.sys.live) { provActive_ = true; emitNet(NetCmd::Provision, net.ssid); }
        redraw(RefreshKind::Quick);
        setAction(Sb(msg, sizeof msg).s("Configura ").s(net.ssid).s(" dal telefono").str());
        return finish(t);
      }
      ScreenEntry e = makeScreen(ScreenId::Connecting);
      scopy(e.ssid, net.ssid);
      e.until = t + (m.sys.live ? kJoinMaxMs : 2600);
      pushScreen(m, e);
      if (m.sys.live) emitNet(NetCmd::Join, net.ssid);
      redraw(RefreshKind::Quick);
      setAction(Sb(msg, sizeof msg).s("Connessione a ").s(net.ssid).str());
      return finish(t);
    }
    if (tp.id == ScreenId::Qr && m.sys.live) { setAction("Configura dal telefono"); return finish(t); }
    if (tp.id == ScreenId::Qr) {
      ScreenEntry e = makeScreen(ScreenId::Connecting);
      scopy(e.ssid, tp.ssid[0] ? tp.ssid : (m.sys.ssid[0] ? m.sys.ssid : "CasaWiFi"));
      e.until = t + 2600;
      pushScreen(m, e);
      redraw(RefreshKind::Quick);
      setAction("Simulato: password inserita dal telefono");
      return finish(t);
    }
    if (tp.id == ScreenId::Focus) {
      if (focusOn(m, t)) {
        m.focus.until = 0;
        toast(m, "Concentrazione interrotta", t);
        logEv(m, 0, "concentrazione interrotta", t);
        redraw(RefreshKind::Partial);
        setAction("Concentrazione interrotta");
        return finish(t);
      }
      m.focus.until = t + m.focus.len * 60000LL;
      char hh[8], tm[64];
      hmAt(wc, t, m.focus.until, hh, sizeof hh);
      toast(m, Sb(tm, sizeof tm).s("Concentrazione fino alle ").s(hh).str(), t);
      logEv(m, 0, "concentrazione avviata", t);
      redraw(RefreshKind::Partial);
      setAction(Sb(msg, sizeof msg).s("Concentrazione per ").i(m.focus.len).s(" minuti: avvisi in silenzio").str());
      return finish(t);
    }
    if (tp.id == ScreenId::Pet) {  // 0.10.0: La mascotte › 2× apre Accessorio, sull'indossato
      int k = -1;
      for (int q = 0; q < m.pet.nUnlocked && k < 0; q++)
        if (streq(m.pet.unlocked[q].id, m.pet.wear.id)) k = q;
      ScreenEntry e = makeScreen(ScreenId::List);
      e.menu = MenuId::PetAcc;
      e.sel = k + 1;
      pushScreen(m, e);
      redraw(RefreshKind::Quick);
      setAction("Apri Accessorio");
      return finish(t);
    }
    if (tp.id == ScreenId::Batteria) {
      m.set.save = !m.set.save;
      redraw(RefreshKind::Partial);
      setAction(m.set.save ? "Risparmio energetico: sì" : "Risparmio energetico: no");
      return finish(t);
    }
    setAction("Niente da confermare qui");
    return finish(t);
  }
  return finish(t);
}

bool Ui::confirmPerm(ScreenEntry &tp, Ms now) {
  const bool ok = tp.choice == 0;
  // §7, scelta A (spec app iPhone §2.3): qui nasce perm-reply. Dal telefono si conferma solo NEGA; CONSENTI
  // resta a schermo e la richiesta aperta, per consentire servono il PRG o l'Xbox.
  if (ok && fromPhone()) {
    toast(m_, kPhoneAllowToast, now);
    redraw(RefreshKind::Partial);
    setAction("Telefono: CONSENTI non si conferma dal telefono, la richiesta resta aperta");
    return false;
  }
  const int i = tp.i;
  Session &s = m_.sessions[i];
  // Uscita verso il servizio: con la rete perm-reply firmato (M5 §3), senza l'aggiornamento locale del finto.
  emitPerm(ok ? PermOutcome::Allow : PermOutcome::Deny, i, tp.rid, now);
  const int q = permReqById(m_, tp.rid);
  if (q >= 0) dropPermReq(m_, q);
  // Altre richieste della stessa sessione ancora in coda: resta «PERMESSO» finché il servizio non dice altro.
  const bool more = m_.sys.live && permReqFor(m_, s.id) >= 0;
  if (!more) s.state = SessionState::Working;
  s.ticks = 0;
  if (more) {
  } else if (ok) {
    if (startsWith(s.detail, "Bash: ")) {
      char d[kDetailLen];
      Sb(d, sizeof d).s("Bash in corso: ").s(s.detail + strlen("Bash: "));
      scopy(s.detail, d);
    }
  } else {
    scopy(s.detail, "Permesso negato, cerca un’alternativa");
  }
  m_.stats.permDisplay++;
  if (!more) addWait(m_, s, now);
  logEv(m_, &s, ok ? "permesso concesso dal display" : "permesso negato dal display", now);
  popScreen(m_);
  toast(m_, ok ? "Consentito · inviato a Claude Code" : "Negato · inviato a Claude Code", now);
  redraw(RefreshKind::Quick);
  setAction(ok ? "Consentito: Claude Code prosegue" : "Negato: Claude cerca un’alternativa");
  return true;
}

void Ui::activate(ScreenEntry &tp, Ms now, const WallClock &wc) {
  fillItems(tp, now, wc);
  if (tp.sel < 0 || tp.sel >= list_.n) { setAction(""); return; }
  const MenuItem &it = list_.items[tp.sel];
  char msg[128];
  if (!(it.kind == ItemKind::Act && it.act == ActId::Forget)) m_.ui.forgetUntil = 0;
  if (!(it.kind == ItemKind::Act && it.act == ActId::ForgetNet)) m_.ui.forgetNetUntil = 0;
  if (!(it.kind == ItemKind::Act && it.act == ActId::Unpair)) m_.ui.unpairUntil = 0;
  if (!(it.kind == ItemKind::Act && it.act == ActId::NewBatt)) m_.ui.newBattUntil = 0;
  if (!(it.kind == ItemKind::Act && it.act == ActId::GbForget)) m_.ui.gbForgetUntil = 0;
  if (!(it.kind == ItemKind::Act && it.act == ActId::GbForgetPhone)) m_.ui.gbForgetPhoneUntil = 0;
  if (it.kind == ItemKind::Go) {
    ScreenEntry e = makeScreen(ScreenId::List);
    e.menu = it.go;
    if (it.go == MenuId::SavedNet && it.net >= 0 && it.net < m_.nSaved) scopy(e.ssid, m_.saved[it.net].ssid);
    pushScreen(m_, e);
    redraw(RefreshKind::Quick);
    setAction(Sb(msg, sizeof msg).s("Apri ").s(it.label).str());
    return;
  }
  if (it.kind == ItemKind::Open) {
    // F11: «Configura da telefono» apre l'AP e il portale: dal telefono no.
    if (it.open == ScreenId::Qr && phoneRefused("Telefono: il Wi-Fi si cambia col tasto PRG o col controller", now)) return;
    ScreenEntry e = makeScreen(it.open);
    if (it.open == ScreenId::WifiScan) e.scanUntil = now + (m_.sys.live ? kScanMaxMs : 1500);
    if (it.open == ScreenId::Game) gameNew(m_.game, rnd_);
    pushScreen(m_, e);
    if (m_.sys.live && it.open == ScreenId::WifiScan) emitNet(NetCmd::Scan);
    if (m_.sys.live && it.open == ScreenId::Qr) { provActive_ = true; emitNet(NetCmd::Provision); }
    redraw(RefreshKind::Quick);
    setAction(Sb(msg, sizeof msg).s("Apri ").s(it.label).str());
    return;
  }
  if (it.kind == ItemKind::Act && it.act == ActId::Wear) { petWear(it.net, now); return; }
  if (it.kind == ItemKind::Act && it.act == ActId::Theme) { themePick(it.net, now); return; }
  if (it.kind == ItemKind::Act && it.act >= ActId::GbPick && it.act <= ActId::GbExit) { gbActivate(it.act, now); return; }
  if (it.kind == ItemKind::Act) { doAct(it.act, now, wc); return; }
  setAction(Sb(msg, sizeof msg).s(it.label).s(": solo lettura").str());
}

// Silenzia «fino alle 8» dell'ora locale: oggi se è prima delle 8, altrimenti domani.
static Ms muteUntilEight(Ms now, const WallClock &wc) {
  const int64_t day = 86400000;
  int64_t target = wc.localMs - floorMod(wc.localMs, day) + 8 * 3600000LL;
  if (civilFrom(wc.localMs).hour >= 8) target += day;
  return now + (target - wc.localMs);
}

void Ui::doAct(ActId id, Ms now, const WallClock &wc) {
  Settings &st = m_.set;
  char msg[128];
  Sb b(msg, sizeof msg);
  switch (id) {
    case ActId::Style:
      st.homeStyle = static_cast<HomeStyle>((static_cast<int>(st.homeStyle) + 1) % 4);
      b.s("Stile home: ").s(kHomeLabel[static_cast<int>(st.homeStyle)]);
      break;
    case ActId::Anim:
      st.anim = static_cast<AnimMode>((static_cast<int>(st.anim) + 1) % 4);
      b.s("Animazioni: ").s(kAnimLabel[static_cast<int>(st.anim)]);
      break;
    case ActId::Speed:
      st.speed = st.speed == SpeedMode::Massima ? SpeedMode::Profilo : SpeedMode::Massima;
      b.s("Velocità animazioni: ").s(kSpeedLabel[static_cast<int>(st.speed)]);
      break;
    case ActId::Save: st.save = !st.save; b.s("Risparmio energetico: ").s(yes(st.save)); break;
    case ActId::PairInfo:
      toast(m_, "Avvia dal PC: calamaio pair", now);
      b.s("L'abbinamento parte dal PC: calamaio pair o Abbina nella webapp");
      break;
    case ActId::Unpair:
      // F11, regola 1: dal telefono non si arma nemmeno (si vede la voce, non si sceglie).
      if (phoneRefused("Telefono: l'abbinamento col PC si dimentica col tasto PRG o col controller", now)) return;
      if (m_.sys.pair == PairState::None) {
        toast(m_, "Nessun abbinamento", now);
        b.s("Dimentica abbinamento: il display non è abbinato");
        break;
      }
      // Come «Dimentica rete»: seconda doppia pressione entro kForgetMs.
      if (!(m_.ui.unpairUntil > now)) {
        m_.ui.unpairUntil = now + kForgetMs;
        toast(m_, "2 pressioni ancora per confermare", now);
        b.s("Dimentica abbinamento: conferma con altre 2 pressioni");
        break;
      }
      m_.ui.unpairUntil = 0;
      if (m_.sys.live) emitNet(NetCmd::Unpair);
      m_.sys.pair = PairState::None;
      m_.sys.authed = false;
      m_.sys.pairHost[0] = 0;
      m_.nPerms = 0;
      logEv(m_, 0, "abbinamento dimenticato", now);
      toast(m_, "Abbinamento dimenticato", now);
      b.s("Abbinamento dimenticato: permessi solo nel terminale");
      break;
    case ActId::Protect:
    case ActId::Factory: {
      // R1, regola 1: dal telefono non si apre nemmeno l'avviso.
      if (phoneRefused("Telefono: Proteggi e Ripristino si confermano col tasto PRG o col controller", now)) return;
      if (id == ActId::Protect && m_.sys.keysProtected) {
        toast(m_, "Chiavi già protette", now);
        b.s("Le chiavi sono già protette dalla chiave nel chip");
        break;
      }
      ScreenEntry e = makeScreen(ScreenId::Confirm);
      e.kind = id == ActId::Protect ? ConfirmKind::Protect : ConfirmKind::Factory;
      pushScreen(m_, e);
      redraw(RefreshKind::Quick);
      setAction(id == ActId::Protect ? "Proteggi le chiavi: leggi l'avviso, poi 2X e tieni 3 s"
                                     : "Ripristino di fabbrica: leggi l'avviso, poi 2X e tieni 3 s");
      return;
    }
    case ActId::Invert:
      st.invert = !st.invert;
      redraw(RefreshKind::Quick);
      setAction(st.invert ? "Inverti colori: sì" : "Inverti colori: no");
      return;
    case ActId::Rot:
      st.rot = !st.rot;
      redraw(RefreshKind::Quick);
      setAction(st.rot ? "Ruota 180°: sì" : "Ruota 180°: no");
      return;
    case ActId::Full:
      redraw(RefreshKind::Full);
      setAction("Refresh completo");
      return;
    case ActId::Mute:
      if (!muted(m_, now)) {
        st.mute = now + kMuteMs;
        st.muteKind = MuteKind::Hour;
        b.s("Silenzia: 1 ora");
      } else if (st.muteKind == MuteKind::Hour) {
        st.mute = muteUntilEight(now, wc);
        st.muteKind = MuteKind::Night;
        b.s("Silenzia: fino alle 8");
      } else {
        st.mute = 0;
        st.muteKind = MuteKind::None;
        b.s("Avvisi attivi");
      }
      break;
    case ActId::Night: st.night = !st.night; b.s("Notturno: ").s(yes(st.night)); break;
    case ActId::Perm: st.permOn = !st.permOn; b.s("Permessi dal display: ").s(yes(st.permOn)); break;
    case ActId::Sound: st.sound = !st.sound; b.s("Cicalino: ").s(yes(st.sound)); break;
    case ActId::Led: st.led = !st.led; b.s("LED: ").s(yes(st.led)); break;
    case ActId::Hook: st.hookDirect = !st.hookDirect; b.s("Hook diretti: ").s(yes(st.hookDirect)); break;
    case ActId::Forget:
      if (phoneRefused("Telefono: le reti si dimenticano col tasto PRG o col controller", now)) return;  // F11, regola 1
      // Operazione distruttiva: serve una seconda doppia pressione entro kForgetMs (anche nel simulatore).
      if (!(m_.ui.forgetUntil > now)) {
        m_.ui.forgetUntil = now + kForgetMs;
        toast(m_, "2 pressioni ancora per confermare", now);
        b.s("Dimentica tutte le reti: conferma con altre 2 pressioni");
        break;
      }
      m_.ui.forgetUntil = 0;
      if (m_.sys.live) {
        emitNet(NetCmd::Forget);
        m_.sys.ip[0] = 0;
        m_.sys.pcHost[0] = 0;
      }
      m_.nSaved = 0;
      m_.sys.wifi = false;
      m_.sys.pc = false;
      m_.sys.lastPc = now;
      m_.sys.ssid[0] = 0;
      toast(m_, "Reti dimenticate", now);
      b.s("Tutte le reti Wi-Fi dimenticate");
      break;
    case ActId::ForgetNet: {
      // Una rete di Reti salvate: seconda doppia pressione entro kForgetMs. Con la rete: vault, pulizia profonda
      // della NVS e riavvio dopo 3 s (si riparte con la rete salvata più forte, o in configurazione).
      if (phoneRefused("Telefono: le reti si dimenticano col tasto PRG o col controller", now)) return;  // F11, regola 1
      char ssid[kSsidLen];
      scopy(ssid, top(m_).ssid);
      if (!(m_.ui.forgetNetUntil > now)) {
        m_.ui.forgetNetUntil = now + kForgetMs;
        toast(m_, "2 pressioni ancora per confermare", now);
        b.s("Dimentica ").s(ssid).s(": conferma con altre 2 pressioni");
        break;
      }
      m_.ui.forgetNetUntil = 0;
      if (m_.sys.live) emitNet(NetCmd::ForgetNet, ssid);
      dropSaved(ssid, now);
      popScreen(m_);
      toast(m_, "Rete dimenticata", now);
      redraw(RefreshKind::Quick);
      setAction(Sb(msg, sizeof msg).s("Rete ").s(ssid).s(" dimenticata").str());
      return;
    }
    case ActId::Reboot: {
      ScreenEntry e = makeScreen(ScreenId::Boot);
      e.until = now + kBootMs;
      resetStack(m_, e);
      m_.sys.bootAt = now;
      if (m_.sys.live) emitNet(NetCmd::Reboot);
      redraw(RefreshKind::Quick);
      setAction("Riavvio");
      return;
    }
    case ActId::Sleep:
      pushScreen(m_, makeScreen(ScreenId::Sleep));
      redraw(RefreshKind::Quick);
      setAction("Riposo");
      return;
    case ActId::Calib: {
      // M6: serve la batteria almeno al 90% (col cavo si controlla quando lo stacchi).
      Model &m = m_;
      if (!m.sys.usb && m.sys.batt < kCalMinPct) {
        toast(m, "Carica almeno al 90%", now);
        b.s("Calibrazione: serve la batteria almeno al 90%");
        break;
      }
      memset(&m.calib, 0, sizeof m.calib);
      m.calib.phase = m.sys.usb ? CalibPhase::Wait : CalibPhase::Max;
      m.calib.last = now;
      pushScreen(m, makeScreen(ScreenId::Calib));
      logEv(m, 0, m.sys.usb ? "calibrazione: stacca il cavo" : "calibrazione: fase 1", now);
      if (!m.sys.usb) {
        if (m.sys.live) m.calib.due = true;  // la prima lettura la fa il loop, subito
        else calibSampleIn(mvFromPercent(m.sys.batt), now, wc);
      }
      redraw(RefreshKind::Quick);
      setAction(m.sys.usb ? "Calibrazione: stacca il cavo USB-C per iniziare" : "Calibrazione: fase 1, massimo consumo");
      return;
    }
    case ActId::NewBatt:
      // Come «Dimentica rete»: seconda doppia pressione entro kForgetMs, poi propone di calibrare.
      if (!(m_.ui.newBattUntil > now)) {
        m_.ui.newBattUntil = now + kForgetMs;
        toast(m_, "2 pressioni ancora per confermare", now);
        b.s("Nuova batteria: conferma con altre 2 pressioni");
        break;
      }
      m_.ui.newBattUntil = 0;
      memset(&m_.bat.cal, 0, sizeof m_.bat.cal);
      m_.bat.hasSleep = false;
      m_.bat.sleep = 0;
      batChanged_ = true;
      top(m_).sel = 1;
      logEv(m_, 0, "nuova batteria: calibrazione cancellata", now);
      toast(m_, "Ora calibra la batteria", now);
      b.s("Nuova batteria: calibrazione cancellata, ora calibra");
      break;
    case ActId::WorkStart: b.s(workStartUi(now, wc)); break;
    case ActId::WorkStop: b.s(workStopUi(now)); break;
    case ActId::Game:  // parte settings con game; il servizio risponde con un game (on): intanto lo si dà per fatto
      setGameSwitch(m_, !st.game);
      m_.pet.on = st.game;
      petChanged_ = true;
      b.s("Gioco della mascotte: ").s(yes(st.game)); break;
    case ActId::GbPair:
    case ActId::GbForget:
    case ActId::GbPairPhone:
    case ActId::GbForgetPhone:
      gbAct(id, now, msg, sizeof msg);  // 0.11.0: core/ui_pad.cpp
      break;
    default:
      setAction("");
      return;
  }
  redraw(RefreshKind::Partial);
  setAction(msg);
}

void Ui::dropSaved(const char *ssid, Ms now) {
  int k = 0;
  while (k < m_.nSaved && !streq(m_.saved[k].ssid, ssid)) k++;
  if (k < m_.nSaved) {
    for (; k + 1 < m_.nSaved; k++) m_.saved[k] = m_.saved[k + 1];
    m_.nSaved--;
  }
  if (m_.sys.wifi && streq(m_.sys.ssid, ssid)) {
    m_.sys.wifi = false;
    m_.sys.pc = false;
    m_.sys.lastPc = now;
    m_.sys.ssid[0] = 0;
  }
}

void Ui::finishConnect(const ScreenEntry &tp, Ms now, const WallClock &wc) {
  char ssid[kSsidLen], tmp[96];
  scopy(ssid, tp.ssid);  // tp si invalida con i pop qui sotto
  int rssi = -60;
  for (int k = 0; k < m_.nNets; k++)
    if (streq(m_.nets[k].ssid, ssid)) { rssi = m_.nets[k].rssi; break; }
  m_.sys.wifi = true;
  m_.sys.pc = true;
  scopy(m_.sys.ssid, ssid);
  m_.sys.rssi = rssi;
  // La rete scelta si aggiunge alle salvate (se c'è posto: con 5 reti il portale chiede quale togliere).
  int sv = -1;
  for (int k = 0; k < m_.nSaved; k++)
    if (streq(m_.saved[k].ssid, ssid)) sv = k;
  if (sv < 0 && m_.nSaved < kSavedMax) {
    sv = m_.nSaved++;
    memset(&m_.saved[sv], 0, sizeof m_.saved[sv]);
    scopy(m_.saved[sv].ssid, ssid);
    for (int k = 0; k < m_.nNets; k++)
      if (streq(m_.nets[k].ssid, ssid)) { m_.saved[sv].visible = true; m_.saved[sv].rssi = m_.nets[k].rssi; }
  }
  if (sv >= 0) m_.saved[sv].usedLocal = wc.localMs;
  while (m_.ui.depth > 1 && top(m_).id != ScreenId::List) popScreen(m_);
  toast(m_, Sb(tmp, sizeof tmp).s("Connesso a ").s(ssid).str(), now);
  logEv(m_, 0, Sb(tmp, sizeof tmp).s("Wi-Fi connesso a ").s(ssid).str(), now);
  setAction(Sb(tmp, sizeof tmp).s("Connesso a ").s(ssid).str());
  redraw(RefreshKind::Quick);
}

RefreshKind Ui::tick(Ms t, const WallClock &wc) {
  req_ = RefreshKind::None;
  Model &m = m_;
  ScreenEntry &tp = top(m);
  UiState &u = m.ui;
  if (u.holdOn && !u.holdDone && t >= u.holdNext && u.holdNext - u.holdSince <= kHoldLastMs) {
    if (holdLabel(m, t)) redraw(RefreshKind::Partial);  // indicatore del tenuto: 0,7 · 1,4 · 2,1 s
    u.holdNext += kHoldStepMs;
  }
  if (tp.id == ScreenId::Boot && t > tp.until) {
    resetStack(m, makeScreen(ScreenId::Home));
    redraw(RefreshKind::Quick);
    return finish(t);
  }
  if (tp.id == ScreenId::Pair && !m.sys.live && tp.until && t > tp.until) {
    // Senza rete (simulatore): la finestra di 3 minuti la chiude il display. Con la rete la chiude il servizio.
    popScreen(m);
    toast(m, "Abbinamento annullato", t);
    setAction("Abbinamento scaduto: 3 minuti senza conferma");
    redraw(RefreshKind::Quick);
    return finish(t);
  }
  if (tp.id == ScreenId::Ota && m.ota.until && t > m.ota.until) {  // errore o rifiuto: si chiude da sola
    closeOta();
    redraw(RefreshKind::Quick);
    return finish(t);
  }
  if (tp.id == ScreenId::Connecting && t > tp.until) {
    if (m.sys.live) {  // la rete non ha risposto in tempo: come un errore
      char ssid[kSsidLen];
      scopy(ssid, tp.ssid);
      RefreshKind r = netConnectFailed(ssid, t);
      return r;
    }
    finishConnect(tp, t, wc);
    return finish(t);
  }
  if (tp.id == ScreenId::WifiScan && tp.scanUntil && t > tp.scanUntil) { tp.scanUntil = 0; redraw(RefreshKind::Partial); }
  if (m.toast.until && t > m.toast.until) { m.toast.until = 0; m.toast.msg[0] = 0; redraw(RefreshKind::Partial); }
  if (m.ui.selUntil && t > m.ui.selUntil) { m.ui.selUntil = 0; if (dev_.cls == 'S') redraw(RefreshKind::Partial); }
  if (m.set.mute && t > m.set.mute) { m.set.mute = 0; m.set.muteKind = MuteKind::None; redraw(RefreshKind::Partial); }
  if (m.focus.until && t > m.focus.until) {
    m.focus.until = 0;
    m.burstUntil = t + 4000;
    toast(m, "Fine concentrazione · fai una pausa", t, 5000);
    logEv(m, 0, "concentrazione finita", t);
    setAction("Fine dei 25 minuti di concentrazione");
    redraw(RefreshKind::Partial);
  }
  workTick(t, wc);
  if (m.pet.party && t >= m.pet.party) { m.pet.party = 0; redraw(RefreshKind::Partial); }  // fine della festa (gioco)
  if (m.pet.specialToast && !m.toast.msg[0]) {  // dopo il toast del traguardo: «Nuovo accessorio speciale…»
    m.pet.specialToast = false;
    if (petOn(m)) { toast(m, kPetSpecialToast, t, 5000); redraw(RefreshKind::Partial); }
  }
  if (!m.sys.live) {  // azzeramento finto dei limiti: con la rete i valori arrivano dal PC
    if (m.limits.hasH5 && t > m.limits.h5Reset) { m.limits.h5 = 3; m.limits.h5Reset = t + 5 * 3600000LL; redraw(RefreshKind::Partial); }
    if (m.limits.hasD7 && t > m.limits.d7Reset) { m.limits.d7 = 1; m.limits.d7Reset = t + 7 * 24 * 3600000LL; }
  }
  if (m.ui.forgetUntil && t > m.ui.forgetUntil) { m.ui.forgetUntil = 0; redraw(RefreshKind::Partial); }
  if (m.ui.forgetNetUntil && t > m.ui.forgetNetUntil) { m.ui.forgetNetUntil = 0; redraw(RefreshKind::Partial); }
  if (m.ui.unpairUntil && t > m.ui.unpairUntil) { m.ui.unpairUntil = 0; redraw(RefreshKind::Partial); }
  if (m.ui.newBattUntil && t > m.ui.newBattUntil) { m.ui.newBattUntil = 0; redraw(RefreshKind::Partial); }
  if (m.ui.gbForgetUntil && t > m.ui.gbForgetUntil) { m.ui.gbForgetUntil = 0; redraw(RefreshKind::Partial); }
  if (m.ui.gbForgetPhoneUntil && t > m.ui.gbForgetPhoneUntil) { m.ui.gbForgetPhoneUntil = 0; redraw(RefreshKind::Partial); }  // F8
  calibTick(t, wc);
  if (tp.id == ScreenId::Confirm && tp.until && t > tp.until) { tp.until = 0; redraw(RefreshKind::Partial); }
  for (int k = m.nArchived - 1; k >= 0; k--)  // Archivia con la rete: nessun ack entro 10 s
    if (m.archived[k].req[0] && !m.archived[k].confirmed && t > m.archived[k].deadline) unarchive(k, t, true);
  if (t > m.nextBurst) { m.burstUntil = t + 2500; m.nextBurst = t + 30000; }
  if (tp.id == ScreenId::Perm) {
    // «IN ATTESA DA …»: si ridisegna solo quando il testo cambia (al più una volta al minuto).
    char w[sizeof permText_];
    permWaitText(t, permSince(m, tp), w, sizeof w);
    if (!streq(w, permText_)) { scopy(permText_, w); redraw(RefreshKind::Partial); }
  }
  const int mm = civilFrom(wc.localMs).minute;
  // L'orologio: ogni minuto, con il risparmio energetico ogni 5 minuti.
  if (mm != lastMin_) { if (lastMin_ != -1 && (!powerSave(m) || mm % 5 == 0)) redraw(RefreshKind::Partial); lastMin_ = mm; }
  if (dev_.cls == 'S' && m.set.homeStyle == HomeStyle::Una && tp.id == ScreenId::Home && !attention(m) &&
      !(m.ui.selUntil > t) && t - lastPage_ > 8000 && m.nSessions > 0 && !powerSave(m) && !m.sys.idle) {
    lastPage_ = t;
    m.ui.page = (m.ui.page + 1) % m.nSessions;
    redraw(RefreshKind::Partial);
  }
  // Fotogramma dell'animazione: nel simulatore si salta se un ridisegno è già in coda (this.pending).
  const float rate = animRate(dev_, m, t);
  if (rate > 0 && req_ == RefreshKind::None && static_cast<double>(t - lastFrame_) >= 1000.0 / rate) {
    lastFrame_ = t;
    frame_++;
    redraw(RefreshKind::Partial);
  }
  return finish(t);
}

RefreshKind Ui::slowTick(Ms now, Rng &rng) {
  req_ = RefreshKind::None;
  Sys &s = m_.sys;
  char t0[16], t1[16];
  fmt1(s.temp, t0, sizeof t0);
  const int b0 = jsRound(s.batt), r0 = rssiBars(s.rssi);
  if (!battReal_) {  // finta solo finché non arriva una lettura vera (battery())
    if (s.usb) s.batt = s.batt + 1 > 100 ? 100 : s.batt + 1;
    else s.batt = s.batt - 0.15 < 1 ? 1 : s.batt - 0.15;
  }
  if (s.live) return finish(now);  // con la rete il segnale è vero
  int r = s.rssi + jsRound((rng.next() - 0.5) * 6);
  s.rssi = r > -50 ? -50 : (r < -80 ? -80 : r);
  s.temp = floor((s.temp + (rng.next() - 0.5) * 0.2) * 10 + 0.5) / 10;
  fmt1(s.temp, t1, sizeof t1);
  const bool diff = b0 != jsRound(s.batt) || r0 != rssiBars(s.rssi) || !streq(t0, t1);
  if (diff && dev_.cls != 'S') redraw(RefreshKind::Partial);
  return finish(now);
}

namespace {
const char *const kTools[] = {"Edit src/app.ts", "Read package.json", "Bash: npm run build", "Grep \"TODO\" src/",
                              "Write tests/api.test.ts", "Read src/main.cpp", "Bash: npm run test"};
const char *const kSummaries[] = {"Endpoint ordini completato, 12 test verdi.", "Layout mobile del carrello sistemato.",
                                  "Driver del display aggiornato e provato.", "Documentazione rigenerata, nessun link rotto.",
                                  "Refactoring finito: tre file più corti."};
const char *const kCmds[] = {"Bash: npm run test", "Bash: git push origin main", "Bash: docker compose up -d",
                             "Bash: pio run -t upload", "Bash: rm -rf dist"};
const char *const kTaskNames[] = {"Analizza il codice", "Scrivi il test", "Implementa", "Rifattorizza", "Aggiorna la doc", "Verifica finale"};

int pickIdx(Rng &rng, int n) { return static_cast<int>(floor(rng.next() * n)); }
}  // namespace

RefreshKind Ui::raise(int i, SessionState kind, Ms now, Rng &rng) {
  req_ = RefreshKind::None;
  Session &s = m_.sessions[i];
  char tmp[96];
  s.state = kind;
  s.ticks = 0;
  s.hasAttnAt = true;
  s.attnAt = now;
  if (kind == SessionState::Permission && top(m_).id == ScreenId::Game) interruptGame(now);
  if (kind == SessionState::Permission) {
    scopy(s.detail, kCmds[pickIdx(rng, 5)]);
    // s.detail.replace('Bash: ', ''): prima occorrenza
    Sb b(tmp, sizeof tmp);
    b.s("chiede permesso: ");
    const char *p = strstr(s.detail, "Bash: ");
    if (p) {
      char head[kDetailLen];
      size_t n = static_cast<size_t>(p - s.detail);
      memcpy(head, s.detail, n);
      head[n] = 0;
      b.s(head).s(p + strlen("Bash: "));
    } else {
      b.s(s.detail);
    }
    logEv(m_, &s, tmp, now);
  } else if (kind == SessionState::Input) {
    scopy(s.detail, "Domanda: quale database uso?");
    logEv(m_, &s, "ti ha fatto una domanda", now);
  } else {
    scopy(s.detail, "StopFailure: rate_limit");
    logEv(m_, &s, "errore rate_limit", now);
  }
  if (dev_.cls != 'S' && now - lastInputAt_ > 8000 && top(m_).id == ScreenId::Home) m_.ui.sel = i;
  if ((dev_.buzzer || m_.acc) && m_.set.sound && !quiet(m_, now) && kind == SessionState::Permission) beepUntil_ = now + 900;
  changed(now);
  return finish(now);
}

RefreshKind Ui::step(Ms t, const WallClock &wc, Rng &rng) {
  (void)wc;
  req_ = RefreshKind::None;
  Model &m = m_;
  if (top(m).id == ScreenId::Boot) return finish(t);
  if (m.sys.live) return finish(t);  // con la rete le sessioni arrivano solo dal PC
  if (!m.sys.pc) return finish(t);
  Session *S = m.sessions;
  const int n = m.nSessions;
  char tmp[96];
  for (int k = 0; k < n; k++) S[k].ticks++;
  for (int k = 0; k < m.nArchived; k++) m.archived[k].s.ticks++;
  m.limits.h5 = m.limits.h5 + 0.3 + rng.next() * 0.6;
  if (m.limits.h5 > 100) m.limits.h5 = 100;
  m.limits.d7 = m.limits.d7 + 0.05 + rng.next() * 0.1;
  if (m.limits.d7 > 100) m.limits.d7 = 100;
  if (m.acc) {
    const double before = m.air.co2;
    double c = m.air.co2 + jsRound((rng.next() - 0.35) * 60);
    m.air.co2 = c > 1500 ? 1500 : (c < 450 ? 450 : c);
    if (before <= 1000 && m.air.co2 > 1000) toast(m, "CO2 alta: apri la finestra", t);
  }
  for (int k = 0; k < n; k++) {
    Session &c = S[k];
    if (c.state == SessionState::Compact && c.ticks >= 2) {
      c.state = SessionState::Working;
      c.ctx = 22;
      scopy(c.detail, kTools[pickIdx(rng, 7)]);
      c.ticks = 0;
      logEv(m, &c, "memoria riordinata, contesto al 22%", t);
      changed(t);
      return finish(t);
    }
  }
  const int busy = top(m).id == ScreenId::Perm ? top(m).i : -1;
  int idx[kMaxSessions], cnt = 0;
  for (int k = 0; k < n; k++)
    if (isAttn(S[k]) && S[k].ticks >= 4 && k != busy) idx[cnt++] = k;
  if (cnt && rng.next() < 0.4) {
    Session &s = S[idx[pickIdx(rng, cnt)]];
    if (s.state == SessionState::Permission) m.stats.permTerminal++;
    addWait(m, s, t);
    s.state = SessionState::Working;
    scopy(s.detail, kTools[pickIdx(rng, 7)]);
    s.ticks = 0;
    logEv(m, &s, "risposto dal terminale", t);
    changed(t);
    return finish(t);
  }
  // Nuovo prompt: sessioni finite e archiviate (queste tornano in fondo all'elenco, come revive() del simulatore).
  int fin[2 * kMaxSessions];
  cnt = 0;
  for (int k = 0; k < n; k++)
    if (S[k].state == SessionState::Done && S[k].ticks >= 5) fin[cnt++] = k;
  for (int k = 0; k < m.nArchived; k++)
    if (m.archived[k].s.ticks >= 5) fin[cnt++] = kMaxSessions + k;
  if (cnt && rng.next() < 0.4) {
    const int pick = fin[pickIdx(rng, cnt)];
    const int nt = 3 + static_cast<int>(floor(rng.next() * 4));
    int si = pick;
    if (pick >= kMaxSessions) {
      if (m.nSessions >= kMaxSessions) return finish(t);  // non succede nel finto (4 sessioni)
      si = m.nSessions++;
      S[si] = m.archived[pick - kMaxSessions].s;
      dropArchived(pick - kMaxSessions);
    }
    Session &s = S[si];
    s.nTasks = nt;
    for (int k = 0; k < nt; k++) {
      scopy(s.tasks[k].name, kTaskNames[k % 6]);
      s.tasks[k].status = k == 0 ? TaskStatus::Doing : TaskStatus::Todo;
    }
    countTasks(s);
    s.state = SessionState::Working;
    scopy(s.detail, kTools[pickIdx(rng, 7)]);
    s.since = t;
    s.ticks = 0;
    s.summary[0] = 0;
    s.test = TestResult::None;
    logEv(m, &s, Sb(tmp, sizeof tmp).s("nuovo prompt, ").i(nt).s(" task").str(), t);
    changed(t);
    return finish(t);
  }
  cnt = 0;
  for (int k = 0; k < n; k++)
    if (S[k].state == SessionState::Working) idx[cnt++] = k;
  if (!cnt) return finish(t);
  const int si = idx[pickIdx(rng, cnt)];
  Session &s = S[si];
  const double r = rng.next();
  if (r < 0.1) return raise(si, SessionState::Permission, t, rng);
  if (r < 0.14) return raise(si, SessionState::Input, t, rng);
  if (r < 0.16) return raise(si, SessionState::Error, t, rng);
  s.ctx = s.ctx + 2 + static_cast<int>(floor(rng.next() * 5));
  if (s.ctx > 99) s.ctx = 99;
  if (s.ctx >= 92) {
    s.state = SessionState::Compact;
    scopy(s.detail, "Riordina la memoria");
    s.ticks = 0;
    logEv(m, &s, Sb(tmp, sizeof tmp).s("contesto al ").i(s.ctx).s("%, riordina la memoria").str(), t);
    changed(t);
    return finish(t);
  }
  if (rng.next() < 0.2) {
    if (s.subs && rng.next() < 0.5) { s.subs--; logEv(m, &s, "subagente finito", t); }
    else if (s.subs < 3) { s.subs++; logEv(m, &s, "subagente avviato", t); }
  }
  m.stats.linesAdd += static_cast<long>(floor(rng.next() * 40));
  m.stats.linesDel += static_cast<long>(floor(rng.next() * 15));
  m.stats.cost += rng.next() * 0.25;
  int i = -1;
  for (int k = 0; k < s.nTasks; k++)
    if (s.tasks[k].status == TaskStatus::Doing) { i = k; break; }
  if (i >= 0) {
    s.tasks[i].status = TaskStatus::Done;
    if (i + 1 < s.nTasks) s.tasks[i + 1].status = TaskStatus::Doing;
    m.stats.tasks++;
  } else {
    for (int k = 0; k < s.nTasks; k++)
      if (s.tasks[k].status == TaskStatus::Todo) { s.tasks[k].status = TaskStatus::Doing; break; }
  }
  countTasks(s);
  scopy(s.detail, kTools[pickIdx(rng, 7)]);
  if (startsWith(s.detail, "Bash: npm test") || startsWith(s.detail, "Bash: npm run test")) {
    s.test = rng.next() < 0.7 ? TestResult::Ok : TestResult::Ko;
    logEv(m, &s, s.test == TestResult::Ok ? "test superati" : "test falliti", t);
  }
  if (s.total && s.done >= s.total) {
    s.state = SessionState::Done;
    Sb(s.detail, sizeof s.detail).i(s.total).s(" task completati");
    scopy(s.summary, kSummaries[pickIdx(rng, 5)]);
    s.subs = 0;
    s.ticks = 0;
    logEv(m, &s, "turno completato", t);
  } else {
    logEv(m, &s, Sb(tmp, sizeof tmp).s("task ").i(s.done).s("/").i(s.total).s(" completato").str(), t);
  }
  changed(t);
  return finish(t);
}

// ---------- M3: rete vera ----------

void Ui::goLive() {
  clearForLive(m_);
  provActive_ = false;
  lastState_ = 0;
}

bool Ui::showsData() const {
  switch (top(m_).id) {
    case ScreenId::Home: case ScreenId::Detail: case ScreenId::Perm: case ScreenId::List: case ScreenId::Oggi:
    case ScreenId::Storico: case ScreenId::Stato: case ScreenId::Limiti: case ScreenId::Servizi:
    case ScreenId::Agenda: case ScreenId::Sleep:
      return true;
    default:
      return false;
  }
}

RefreshKind Ui::netApReady(const char *apPass, Ms now) {
  req_ = RefreshKind::None;
  scopy(m_.sys.apPass, apPass);
  provActive_ = true;
  if (top(m_).id == ScreenId::Qr) {
    redraw(RefreshKind::Partial);  // compare il QR vero
  } else if (top(m_).id != ScreenId::Connecting) {
    // primo avvio senza rete salvata: il QR si apre da solo sopra la Home
    pushScreen(m_, makeScreen(ScreenId::Qr));
    redraw(RefreshKind::Quick);
    setAction("Configura Wi-Fi dal telefono");
  }
  return finish(now);
}

RefreshKind Ui::netConnecting(const char *ssid, Ms now) {
  req_ = RefreshKind::None;
  ScreenEntry &tp = top(m_);
  if (tp.id == ScreenId::Connecting) {
    scopy(tp.ssid, ssid);
    tp.until = now + kJoinMaxMs;
    redraw(RefreshKind::Partial);
  } else {
    ScreenEntry e = makeScreen(ScreenId::Connecting);
    scopy(e.ssid, ssid);
    e.until = now + kJoinMaxMs;
    pushScreen(m_, e);
    redraw(RefreshKind::Quick);
  }
  char msg[96];
  setAction(Sb(msg, sizeof msg).s("Connessione a ").s(ssid).str());
  return finish(now);
}

RefreshKind Ui::netConnected(const char *ssid, int rssi, const char *ip, Ms now) {
  req_ = RefreshKind::None;
  Sys &y = m_.sys;
  y.wifi = true;
  scopy(y.ssid, ssid);
  y.rssi = rssi;
  scopy(y.ip, ip);
  y.apPass[0] = 0;
  provActive_ = false;
  char tmp[96];
  logEv(m_, 0, Sb(tmp, sizeof tmp).s("Wi-Fi connesso a ").s(ssid).str(), now);
  if (inStack(ScreenId::Qr) || inStack(ScreenId::Connecting)) {
    // come finishConnect(): si torna sotto il QR e la connessione (alla lista o alla Home)
    while (m_.ui.depth > 1 &&
           (top(m_).id == ScreenId::Connecting || top(m_).id == ScreenId::Qr || top(m_).id == ScreenId::WifiScan))
      popScreen(m_);
    toast(m_, Sb(tmp, sizeof tmp).s("Connesso a ").s(ssid).str(), now);
    redraw(RefreshKind::Quick);
  } else if (showsData()) {
    redraw(RefreshKind::Partial);
  }
  setAction(Sb(tmp, sizeof tmp).s("Connesso a ").s(ssid).str());
  return finish(now);
}

RefreshKind Ui::netConnectFailed(const char *ssid, Ms now) {
  req_ = RefreshKind::None;
  m_.sys.wifi = false;
  char tmp[96];
  logEv(m_, 0, Sb(tmp, sizeof tmp).s("Wi-Fi: ").s(ssid).s(" non risponde").str(), now);
  if (top(m_).id == ScreenId::Connecting) {
    popScreen(m_);  // torna al QR (si riprova dal telefono) o all'elenco delle reti
    toast(m_, "Connessione non riuscita", now);
    redraw(RefreshKind::Quick);
  } else if (showsData()) {
    redraw(RefreshKind::Partial);
  }
  setAction(Sb(tmp, sizeof tmp).s("Connessione a ").s(ssid).s(" non riuscita").str());
  return finish(now);
}

RefreshKind Ui::netWifiLost(Ms now) {
  req_ = RefreshKind::None;
  if (m_.sys.wifi) logEv(m_, 0, "Wi-Fi perso", now);
  m_.sys.wifi = false;
  m_.sys.ip[0] = 0;
  if (showsData()) redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::netRssi(int rssi, Ms now) {
  req_ = RefreshKind::None;
  m_.sys.rssi = rssi;  // come slowTick() del simulatore: sulla classe S il segnale non chiede un ridisegno
  return finish(now);
}

RefreshKind Ui::usb(bool on, Ms now) {
  req_ = RefreshKind::None;
  if (m_.sys.usb == on) return finish(now);
  m_.sys.usb = on;
  toast(m_, on ? "In carica" : "A batteria", now);
  redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::battery(int pct, Ms now) {
  req_ = RefreshKind::None;
  battReal_ = true;
  Sys &s = m_.sys;
  const int b0 = jsRound(s.batt);
  s.batt = pct < 0 ? 0 : pct > 100 ? 100 : pct;
  // Come slowTick(): sulla classe S il livello non chiede un ridisegno da solo; si vede al prossimo
  // (l'orologio ne fa uno al minuto, uno ogni 5 minuti con il risparmio energetico).
  if (b0 != jsRound(s.batt) && dev_.cls != 'S') redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::netScanDone(const NetEntry *nets, int n, Ms now) {
  req_ = RefreshKind::None;
  m_.nNets = n < 0 ? 0 : n > kNetCap ? kNetCap : n;
  for (int k = 0; k < m_.nNets; k++) m_.nets[k] = nets[k];
  ScreenEntry &tp = top(m_);
  if (tp.id == ScreenId::WifiScan && tp.scanUntil) {
    tp.scanUntil = 0;
    tp.sel = 0;
    redraw(RefreshKind::Partial);
  }
  return finish(now);
}

RefreshKind Ui::netSaved(const SavedNet *list, int n, Ms now) {
  req_ = RefreshKind::None;
  if (n < 0) n = 0;
  if (n > kSavedMax) n = kSavedMax;
  const bool same = n == m_.nSaved && (!n || memcmp(m_.saved, list, sizeof(SavedNet) * n) == 0);
  if (!same) {
    memset(m_.saved, 0, sizeof m_.saved);
    for (int k = 0; k < n; k++) m_.saved[k] = list[k];
    m_.nSaved = n;
    const ScreenEntry &tp = top(m_);
    if (tp.id == ScreenId::List && (tp.menu == MenuId::Wifi || tp.menu == MenuId::Saved || tp.menu == MenuId::SavedNet))
      redraw(RefreshKind::Partial);
  }
  return finish(now);
}

RefreshKind Ui::netForgotten(const char *ssid, Ms now) {
  req_ = RefreshKind::None;
  dropSaved(ssid, now);
  if (top(m_).id == ScreenId::List && top(m_).menu == MenuId::SavedNet && streq(top(m_).ssid, ssid)) popScreen(m_);
  char tmp[96];
  logEv(m_, 0, Sb(tmp, sizeof tmp).s("Wi-Fi: rete ").s(ssid).s(" dimenticata dal PC").str(), now);
  toast(m_, "Rete dimenticata", now);
  redraw(RefreshKind::Quick);
  setAction(Sb(tmp, sizeof tmp).s("Rete ").s(ssid).s(" dimenticata dal PC").str());
  return finish(now);
}

RefreshKind Ui::netPcHost(const char *host, Ms now) {
  req_ = RefreshKind::None;
  if (!streq(m_.sys.pcHost, host)) {
    scopy(m_.sys.pcHost, host);
    if (top(m_).id == ScreenId::List || top(m_).id == ScreenId::Stato) redraw(RefreshKind::Partial);
  }
  return finish(now);
}

RefreshKind Ui::netPcLost(Ms now) {
  req_ = RefreshKind::None;
  if (!m_.sys.pc) return finish(now);
  m_.sys.pc = false;  // lastPc resta l'ora dell'ultimo dato: la mostra «PC non raggiungibile»
  m_.sys.authed = false;
  m_.nPerms = 0;      // al ritorno il servizio rimanda le richieste ancora aperte (M5, test 6)
  lastState_ = 0;     // alla riconnessione il primo state si ridisegna comunque
  logEv(m_, 0, "PC non raggiungibile", now);
  if (top(m_).id == ScreenId::Perm) popScreen(m_);
  if (top(m_).id == ScreenId::Pair) { popScreen(m_); toast(m_, "Abbinamento annullato", now); }
  const ScreenId id = top(m_).id;
  if (id == ScreenId::Home || id == ScreenId::Detail) redraw(RefreshKind::Quick);
  else if (showsData()) redraw(RefreshKind::Partial);
  return finish(now);
}

namespace {
// FNV-1a a 64 bit: impronta di uno state per non ridisegnare se non è cambiato niente.
uint64_t fnv(const void *p, size_t n, uint64_t h = 1469598103934665603ULL) {
  const uint8_t *b = static_cast<const uint8_t *>(p);
  for (size_t k = 0; k < n; k++) { h ^= b[k]; h *= 1099511628211ULL; }
  return h;
}
}  // namespace

RefreshKind Ui::netState(const StateMsg &msg, Ms now) {
  req_ = RefreshKind::None;
  Model &m = m_;
  const bool wasPc = m.sys.pc;
  // L'impronta salta `time` (cambia a ogni messaggio). decodeState azzera la struttura, quindi anche
  // i byte di riempimento sono sempre uguali.
  const uint64_t h = fnv(reinterpret_cast<const uint8_t *>(&msg) + sizeof msg.time, sizeof msg - sizeof msg.time);
  if (wasPc && h == lastState_) {
    m.sys.lastPc = now;
    return finish(now);
  }
  lastState_ = h;

  // Prima: sessione selezionata e stati per id, per tenere la selezione e accorgersi dei cambi.
  char selId[kIdLen] = "";
  if (m.ui.sel >= 0 && m.ui.sel < m.nSessions) scopy(selId, m.sessions[m.ui.sel].id);
  char permId[kIdLen] = "";
  if (top(m).id == ScreenId::Perm && top(m).i < m.nSessions) scopy(permId, m.sessions[top(m).i].id);
  const bool permByReq = top(m).id == ScreenId::Perm && permReqById(m, top(m).rid) >= 0;
  struct Old { char id[kIdLen]; SessionState st; } old[kMaxSessions];
  const int nOld = m.nSessions;
  for (int k = 0; k < nOld; k++) { scopy(old[k].id, m.sessions[k].id); old[k].st = m.sessions[k].state; }

  applyState(m, msg, now);
  filterArchived();

  const int n = m.nSessions;
  int newSel = -1;
  for (int k = 0; k < n && selId[0]; k++)
    if (streq(m.sessions[k].id, selId)) newSel = k;
  if (newSel >= 0) m.ui.sel = newSel;
  else if (m.ui.sel >= n) m.ui.sel = n > 0 ? n - 1 : 0;
  if (n > 0) m.ui.page %= n;
  else m.ui.page = 0;
  if (newSel < 0 && selId[0]) m.ui.selUntil = 0;  // la sessione scelta non c'è più

  // Permesso aperto: si chiude se la sessione non c'è più o non chiede più il permesso.
  if (top(m).id == ScreenId::Perm) {
    int pi = -1;
    for (int k = 0; k < n; k++)
      if (streq(m.sessions[k].id, permId)) pi = k;
    // Con la richiesta del servizio la chiude perm-close (o la sessione che sparisce), non lo stato: uno
    // state calcolato prima della richiesta non deve chiudere la schermata.
    if (pi < 0 || (!permByReq && m.sessions[pi].state != SessionState::Permission)) {
      popScreen(m);
      toast(m, "Risposto altrove", now);
      redraw(RefreshKind::Quick);
    } else {
      top(m).i = pi;
    }
  }
  if (top(m).id == ScreenId::Detail && n == 0) { popScreen(m); redraw(RefreshKind::Quick); }

  // Cambio di stato di una sessione (o sessione nuova): raffica come changed() del simulatore.
  bool stateChanged = n != nOld;
  for (int k = 0; k < n && !stateChanged; k++) {
    bool found = false;
    for (int j = 0; j < nOld; j++)
      if (streq(old[j].id, m.sessions[k].id)) { found = true; if (old[j].st != m.sessions[k].state) stateChanged = true; }
    if (!found) stateChanged = true;
  }
  if (!wasPc && (top(m).id == ScreenId::Home || top(m).id == ScreenId::Detail)) redraw(RefreshKind::Quick);
  if (stateChanged && wasPc) changed(now);
  // Risparmio energetico: solo i cambiamenti veri (stato delle sessioni) ridisegnano; il resto si vedrà al prossimo.
  else if (showsData() && !powerSave(m) && !m.sys.idle) redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::applyRemote(const SettingsPatch &p, Ms now, const WallClock &wc) {
  req_ = RefreshKind::None;
  Settings &st = m_.set;
  const RemoteValues &v = p.v;
  char msg[128];
  Sb b(msg, sizeof msg);
  b.s("Dal PC:");
  for (int k = 0; k < static_cast<int>(RemoteKey::Count); k++) {
    const RemoteKey rk = static_cast<RemoteKey>(k);
    if (!p.on(rk)) continue;
    RefreshKind r = RefreshKind::Partial;  // come doAct(): valori nel menu e nella Home
    switch (rk) {
      case RemoteKey::HomeStyle: st.homeStyle = v.homeStyle; break;
      case RemoteKey::Anim: st.anim = v.anim; break;
      case RemoteKey::Speed: st.speed = v.speed; break;
      case RemoteKey::Invert: st.invert = v.invert; r = RefreshKind::Quick; break;
      case RemoteKey::Rot: st.rot = v.rot; r = RefreshKind::Quick; break;
      case RemoteKey::Night: st.night = v.night; break;
      case RemoteKey::PermOn:
        // Da remoto solo spento (decodeSet rifiuta true). Se un permesso è aperto si chiude: la risposta
        // resta nel terminale, come con l'annullamento.
        st.permOn = false;
        if (top(m_).id == ScreenId::Perm) {
          emitPerm(PermOutcome::Cancelled, top(m_).i, top(m_).rid, now);
          popScreen(m_);
          r = RefreshKind::Quick;
        }
        break;
      case RemoteKey::Sound: st.sound = v.sound; break;
      case RemoteKey::Led: st.led = v.led; break;
      case RemoteKey::Save: st.save = v.save; break;
      // 0.9.0: orari (già controllati: inizio prima della fine). La giornata in corso tiene la sua fine (regola A:
      // fissa all'avvio); la barra usa subito il nuovo inizio.
      case RemoteKey::WorkStart: st.workStart = v.workStart; break;
      case RemoteKey::WorkEnd: st.workEnd = v.workEnd; break;
      case RemoteKey::Game: {  // 0.10.0: come Display › Gioco della mascotte; spento, via le schermate del gioco
        const int depth = m_.ui.depth;
        setGameSwitch(m_, v.game);
        m_.pet.on = v.game;  // come dal menu: il game del servizio lo confermerà
        petChanged_ = true;
        if (m_.ui.depth != depth) r = RefreshKind::Quick;
        break;
      }
      case RemoteKey::Mute:
        if (v.mute == MuteKind::Hour) { st.mute = now + kMuteMs; st.muteKind = MuteKind::Hour; }
        else if (v.mute == MuteKind::Night) { st.mute = muteUntilEight(now, wc); st.muteKind = MuteKind::Night; }
        else { st.mute = 0; st.muteKind = MuteKind::None; }
        break;
      default: break;
    }
    redraw(r);
    b.s(" ").s(remoteKeyName(rk));
  }
  setAction(msg);
  return finish(now);
}

// ---------- 0.9.0: Giornata di lavoro ----------
// workStart()/workStop()/workTick() del simulatore. La logica pura è in core/workday.

const char *Ui::workStartUi(Ms now, const WallClock &wc) {
  Model &m = m_;
  char hh[8], t[64];
  bool already = false;
  workErr_ = workStart(m, wc, already);
  Sb b(workMsg_, sizeof workMsg_);
  if (already) {
    toast(m, "Giornata già attiva", now);
    hmLocal(m.work.endLocal, hh, sizeof hh);
    b.s("Giornata di lavoro già attiva fino alle ").s(hh);
    return workMsg_;
  }
  minHm(m.set.workEnd, hh, sizeof hh);
  if (workErr_ == kWorkPastError) {
    toast(m, "Orario di fine già passato", now);
    b.s("Giornata di lavoro: le ").s(hh).s(" sono già passate");
    return workMsg_;
  }
  if (workErr_) {  // solo sulla scheda: l'ora del muro non è ancora arrivata
    toast(m, "Ora non ancora nota", now);
    b.s("Giornata di lavoro: aspetto l'ora dal PC");
    return workMsg_;
  }
  toast(m, Sb(t, sizeof t).s("Giornata fino alle ").s(hh).str(), now);
  logEv(m, 0, "giornata di lavoro avviata", now);
  char left[16];
  workLeftText(m, wc, false, left, sizeof left);
  b.s("Giornata di lavoro fino alle ").s(hh).s(": mancano ").s(left);
  return workMsg_;
}

const char *Ui::workStopUi(Ms now) {
  workErr_ = nullptr;
  if (!workStop(m_)) {
    toast(m_, "Giornata non attiva", now);
    return "Giornata di lavoro già spenta";
  }
  toast(m_, "Giornata fermata", now);
  logEv(m_, 0, "giornata di lavoro fermata", now);
  return "Giornata di lavoro fermata";
}

RefreshKind Ui::workdayRemote(bool start, Ms now, const WallClock &wc, char *err, size_t cap) {
  req_ = RefreshKind::None;
  char msg[160];
  Sb(msg, sizeof msg).s("Dal PC: ").s(start ? workStartUi(now, wc) : workStopUi(now));
  scopy(err, cap, workErr_ ? workErr_ : "");
  redraw(RefreshKind::Partial);
  setAction(msg);
  return finish(now);
}

// A mezzanotte si azzera; alla fine «GIORNATA FINITA» (toast, Riposo e Home per un'ora), festa breve (rispetta
// silenzio, notturno e risparmio come le altre animazioni), poi si spegne da sola. Senza l'ora vera si aspetta.
void Ui::workTick(Ms t, const WallClock &wc) {
  Model &m = m_;
  Workday &w = m.work;
  if (!m.sys.wallOk) return;
  const WorkStep st = workStep(m, wc);
  if (st == WorkStep::Midnight) {
    w.on = false;
    w.doneUntilLocal = 0;
    w.party = 0;
    logEv(m, 0, "giornata di lavoro azzerata a mezzanotte", t);
    redraw(RefreshKind::Partial);
  } else if (st == WorkStep::Finish) {
    if (workFinish(m, wc)) {
      toast(m, "GIORNATA FINITA", t, 5000);
      w.party = t + kWorkPartyMs;
      if (!quiet(m, t) && m.burstUntil < w.party) m.burstUntil = w.party;
    }
    logEv(m, 0, "giornata di lavoro finita", t);
    setAction("Fine della giornata di lavoro");
    redraw(RefreshKind::Partial);
  }
  if (w.doneUntilLocal && wc.localMs >= w.doneUntilLocal) { w.doneUntilLocal = 0; redraw(RefreshKind::Partial); }
  if (w.party && t >= w.party) w.party = 0;
}

// ---------- 0.9.0: gioco «Alto o basso» ----------

void Ui::gameAct(bool up, Ms now) {
  Model &m = m_;
  Game &g = m.game;
  char msg[128];
  Sb b(msg, sizeof msg);
  if (g.over) {
    gameNew(g, rnd_);
    redraw(RefreshKind::Partial);
    setAction(b.s("Nuova partita: ").s(kRanks[g.card]).str());
    return;
  }
  const char *was = kRanks[g.card];
  const GameResult r = gameGuess(g, up, rnd_);
  redraw(RefreshKind::Partial);
  b.s(up ? "Più alta: " : "Più bassa: ").s(was).s(" → ").s(kRanks[g.card]);
  if (r == GameResult::Win) b.s(", giusto · serie ").i(g.streak);
  else if (r == GameResult::Tie) b.s(", pari · serie ").i(g.streak);
  else {
    if (g.streak > g.startBest) {
      char t[48];
      logEv(m, 0, Sb(t, sizeof t).s("gioco: nuovo record ").i(g.best).str(), now);
    }
    b.s(", sbagliato · serie ").i(g.streak).s(", record ").i(g.best);
  }
  setAction(msg);
}

// Un permesso in arrivo interrompe la partita e torna alla Home, che lo mostra; la risposta segue le regole di
// sempre (Dettaglio, 2×, NEGA preselezionato). Il record è già salvato a ogni serie migliore.
void Ui::interruptGame(Ms now) {
  resetStack(m_, makeScreen(ScreenId::Home));
  toast(m_, "Partita interrotta: un permesso", now);
  redraw(RefreshKind::Quick);
}

// ---------- Archivia ----------

void Ui::dropArchived(int k) {
  for (int j = k; j + 1 < m_.nArchived; j++) m_.archived[j] = m_.archived[j + 1];
  m_.nArchived--;
}

void Ui::archive(int i, Ms now) {
  Model &m = m_;
  if (m.nArchived >= kMaxSessions) dropArchived(0);  // non succede: le sessioni sono al massimo 6
  Archived &a = m.archived[m.nArchived++];
  memset(&a, 0, sizeof a);
  a.s = m.sessions[i];
  a.s.ticks = 0;
  for (int k = i; k + 1 < m.nSessions; k++) m.sessions[k] = m.sessions[k + 1];
  m.nSessions--;
  logEv(m, &a.s, "archiviata", now);
  resetStack(m, makeScreen(ScreenId::Home));
  const int n = m.nSessions;
  m.ui.sel = i < n - 1 ? i : (n > 0 ? n - 1 : 0);
  m.ui.selUntil = 0;
  m.ui.page = n ? m.ui.page % n : 0;
  toast(m, "Archiviata", now);
  redraw(RefreshKind::Quick);
  char msg[96];
  setAction(Sb(msg, sizeof msg).s("Archiviata: ").s(a.s.name).str());
  // Con la rete: dismiss al servizio; la sessione resta nascosta in attesa dell'ack (kDismissMs).
  if (m.sys.live && a.s.id[0] && netFn_) {
    Sb(a.req, sizeof a.req).s("d").i(++dismissSeq_);
    a.deadline = now + kDismissMs;
    NetRequest r;
    memset(&r, 0, sizeof r);
    r.cmd = NetCmd::Dismiss;
    scopy(r.id, a.req);
    scopy(r.session, a.s.id);
    netFn_(r, netCtx_);
  }
}

void Ui::unarchive(int k, Ms now, bool failed) {
  Model &m = m_;
  bool present = false;
  for (int j = 0; j < m.nSessions; j++)
    if (m.archived[k].s.id[0] && streq(m.sessions[j].id, m.archived[k].s.id)) present = true;
  if (!present && m.nSessions < kMaxSessions) m.sessions[m.nSessions++] = m.archived[k].s;
  char msg[96];
  Sb(msg, sizeof msg).s("Non archiviata: ").s(m.archived[k].s.name);
  dropArchived(k);
  if (failed) {
    toast(m, "Non archiviata", now);
    setAction(msg);
  }
  const ScreenId id = top(m).id;
  redraw(id == ScreenId::Home ? RefreshKind::Quick : RefreshKind::Partial);
}

void Ui::filterArchived() {
  Model &m = m_;
  for (int k = m.nArchived - 1; k >= 0; k--) {
    Archived &a = m.archived[k];
    if (!a.req[0]) continue;  // solo locale (senza rete): lo state non c'entra
    int j = -1;
    for (int q = 0; q < m.nSessions; q++)
      if (streq(m.sessions[q].id, a.s.id)) j = q;
    if (j < 0) {
      if (a.confirmed) dropArchived(k);  // il servizio l'ha tolta: fatto
      continue;
    }
    if (!canArchive(m.sessions[j])) { dropArchived(k); continue; }  // è tornata attiva: si vede di nuovo
    a.s = m.sessions[j];  // ancora ferma: copia fresca (serve se poi va ripristinata) e via dall'elenco
    for (int q = j; q + 1 < m.nSessions; q++) m.sessions[q] = m.sessions[q + 1];
    m.nSessions--;
  }
}

RefreshKind Ui::netAck(const char *id, bool ok, Ms now) {
  req_ = RefreshKind::None;
  for (int k = 0; k < m_.nArchived; k++) {
    Archived &a = m_.archived[k];
    if (!a.req[0] || a.confirmed || !streq(a.req, id)) continue;
    if (ok) {
      a.confirmed = true;  // resta nascosta finché il servizio non manda lo state senza di lei
    } else {
      unarchive(k, now, true);
    }
    break;
  }
  return finish(now);
}

// ---------- M5: abbinamento, chiavi, permessi ----------

void Ui::pairConfirm(Ms now) {
  if (phoneRefused("Telefono: l'abbinamento col PC si conferma col tasto PRG o col controller", now)) return;
  popScreen(m_);
  if (m_.sys.live) emitNet(NetCmd::PairConfirm);
  m_.sys.pair = PairState::Paired;
  m_.sys.authed = false;  // con la rete: la sfida del servizio arriva subito dopo pair-done
  scopy(m_.sys.pairHost, pcLabel(m_));
  char msg[96];
  logEv(m_, 0, Sb(msg, sizeof msg).s("abbinato a ").s(m_.sys.pairHost).str(), now);
  toast(m_, "Abbinato", now);
  redraw(RefreshKind::Quick);
  setAction("Abbinato: il display risponde ai permessi di questo PC");
}

bool Ui::confirmAction(ScreenEntry &tp, Ms now) {
  if (phoneRefused("Telefono: Proteggi e Ripristino si confermano col tasto PRG o col controller", now)) return false;
  const ConfirmKind k = tp.kind;
  popScreen(m_);  // torna a Menu › Sicurezza
  if (k == ConfirmKind::Protect) {
    if (m_.sys.live) {
      // La scrittura nella eFuse la fa il loop (hw/secrets): l'esito arriva con netKeysProtected.
      emitNet(NetCmd::ProtectKeys);
      redraw(RefreshKind::Quick);
      setAction("Proteggi le chiavi: scrittura nel chip in corso");
      return true;
    }
    m_.sys.keysProtected = true;
    logEv(m_, 0, "chiavi protette nel chip", now);
    toast(m_, "Chiavi protette", now);
    redraw(RefreshKind::Quick);
    setAction("Chiavi protette: segreti cifrati con la chiave nel chip");
    return true;
  }
  // Ripristino di fabbrica: con la rete il loop cancella e riavvia; qui si riparte come dopo Riavvia.
  if (m_.sys.live) emitNet(NetCmd::FactoryReset);
  defaultSettings(m_.set);
  Sys &y = m_.sys;
  y.wifi = false;
  y.ssid[0] = 0;
  y.pc = false;
  y.lastPc = now;
  y.pair = PairState::None;
  y.authed = false;
  y.pairHost[0] = 0;
  m_.nPerms = 0;
  m_.nSaved = 0;
  m_.ui.forgetUntil = m_.ui.forgetNetUntil = m_.ui.unpairUntil = m_.ui.gbForgetUntil = m_.ui.gbForgetPhoneUntil = 0;
  ScreenEntry e = makeScreen(ScreenId::Boot);
  e.until = now + kBootMs;
  resetStack(m_, e);
  y.bootAt = now;
  redraw(RefreshKind::Quick);
  setAction("Ripristino di fabbrica: Wi-Fi, abbinamento e impostazioni cancellati");
  return true;
}

RefreshKind Ui::netPairCode(const char *code, Ms now) {
  req_ = RefreshKind::None;
  if (top(m_).id == ScreenId::Sleep) popScreen(m_);  // l'abbinamento si fa guardando il display
  ScreenEntry &tp = top(m_);
  if (tp.id == ScreenId::Pair) {
    scopy(tp.code, code);
    tp.sel = 0;
    tp.until = now + kPairMs;
    redraw(RefreshKind::Partial);
  } else {
    ScreenEntry e = makeScreen(ScreenId::Pair);
    scopy(e.code, code);
    e.until = now + kPairMs;
    pushScreen(m_, e);
    redraw(RefreshKind::Quick);
  }
  setAction("Abbinamento: scrivi il codice sul PC");
  return finish(now);
}

RefreshKind Ui::netPairReady(Ms now) {
  req_ = RefreshKind::None;
  for (int k = m_.ui.depth - 1; k >= 0; k--) {
    ScreenEntry &e = m_.ui.stack[k];
    if (e.id != ScreenId::Pair) continue;
    e.sel = 1;
    if (k == m_.ui.depth - 1) redraw(RefreshKind::Partial);
    setAction("Il PC ha il codice giusto: conferma con 2 pressioni");
    break;
  }
  return finish(now);
}

RefreshKind Ui::netPairAborted(const char *reason, Ms now) {
  req_ = RefreshKind::None;
  if (top(m_).id == ScreenId::Pair) {
    popScreen(m_);
    toast(m_, "Abbinamento annullato", now);
    redraw(RefreshKind::Quick);
    char msg[96];
    setAction(Sb(msg, sizeof msg).s("Abbinamento annullato dal PC (").s(reason && reason[0] ? reason : "?").s(")").str());
  }
  return finish(now);
}

RefreshKind Ui::netPairDone(Ms now) {
  req_ = RefreshKind::None;
  m_.sys.pair = PairState::Paired;
  if (!m_.sys.pairHost[0]) scopy(m_.sys.pairHost, pcLabel(m_));
  if (top(m_).id == ScreenId::List) redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::netAuthed(Ms now) {
  req_ = RefreshKind::None;
  m_.sys.authed = true;
  m_.sys.pair = PairState::Paired;
  if (!m_.sys.pairHost[0]) scopy(m_.sys.pairHost, pcLabel(m_));
  if (showsData()) redraw(RefreshKind::Partial);
  setAction("PC abbinato e autenticato: permessi dal display attivi");
  return finish(now);
}

RefreshKind Ui::netRedo(Ms now) {
  req_ = RefreshKind::None;
  m_.sys.pair = PairState::Redo;
  m_.sys.authed = false;
  m_.nPerms = 0;
  if (top(m_).id == ScreenId::Perm && top(m_).rid[0]) {
    popScreen(m_);
    toast(m_, "Rispondi nel terminale", now);
    redraw(RefreshKind::Quick);
  } else if (showsData()) {
    redraw(RefreshKind::Partial);
  }
  logEv(m_, 0, "abbinamento da rifare", now);
  setAction("Abbinamento da rifare: le chiavi del display e del PC non coincidono");
  return finish(now);
}

RefreshKind Ui::netPermRequest(const PermReq &r, Ms now, int64_t epochNow) {
  req_ = RefreshKind::None;
  if (permReqById(m_, r.id) >= 0) return finish(now);
  if (m_.nPerms >= kPermCap) {
    logEv(m_, 0, "troppe richieste: questa resta nel terminale", now);
    return finish(now);
  }
  PermReq &q = m_.perms[m_.nPerms++];
  q = r;
  // In attesa da quando il servizio l'ha ricevuta (at), se l'ora del muro è valida; altrimenti dall'arrivo.
  q.since = epochNow > 0 && r.at > 0 && r.at <= epochNow ? now - (epochNow - r.at) : now;
  if (top(m_).id == ScreenId::Game) interruptGame(now);
  if (showsData()) redraw(RefreshKind::Partial);
  char msg[128];
  setAction(Sb(msg, sizeof msg).s("Richiesta di permesso verificata: ").s(r.name).s(" · ").s(r.tool).str());
  return finish(now);
}

RefreshKind Ui::netPermClose(const char *id, Ms now) {
  req_ = RefreshKind::None;
  const int q = permReqById(m_, id);
  if (q < 0) return finish(now);
  dropPermReq(m_, q);
  if (top(m_).id == ScreenId::Perm && streq(top(m_).rid, id)) {
    popScreen(m_);
    toast(m_, "Risposto altrove", now);
    redraw(RefreshKind::Quick);
    setAction("Richiesta chiusa altrove: risposta nel terminale o da un altro display");
  } else if (showsData()) {
    redraw(RefreshKind::Partial);
  }
  return finish(now);
}

RefreshKind Ui::netKeysProtected(bool ok, Ms now) {
  req_ = RefreshKind::None;
  if (ok) m_.sys.keysProtected = true;
  toast(m_, ok ? "Chiavi protette" : "Protezione non riuscita", now);
  logEv(m_, 0, ok ? "chiavi protette nel chip" : "protezione delle chiavi non riuscita", now);
  redraw(RefreshKind::Partial);
  setAction(ok ? "Chiavi protette: segreti cifrati con la chiave nel chip" : "Protezione non riuscita: vedi il log seriale");
  return finish(now);
}

// ---------- M6: energia e batteria ----------

RefreshKind Ui::netBye(Ms now) {
  req_ = RefreshKind::None;
  Sys &y = m_.sys;
  // Come Simula «PC spento»: riepilogo fermo sulla Home. Le richieste di permesso non hanno più senso.
  y.pcOff = true;
  y.pc = false;
  y.authed = false;
  y.offAt = now;
  y.lastPc = now;
  m_.nPerms = 0;
  lastState_ = 0;
  if (!calRunning(m_)) resetStack(m_, makeScreen(ScreenId::Home));
  logEv(m_, 0, "PC spento", now);
  redraw(RefreshKind::Full);  // brief §8.3: completo dopo il riepilogo «PC spento»
  setAction("PC spento: il display mostra il riepilogo");
  return finish(now);
}

RefreshKind Ui::battLow(Ms now) {
  req_ = RefreshKind::None;
  memset(&m_.calib, 0, sizeof m_.calib);
  resetStack(m_, makeScreen(ScreenId::Home));
  pushScreen(m_, makeScreen(ScreenId::BattLow));
  logEv(m_, 0, "batteria scarica: sonno profondo", now);
  redraw(RefreshKind::Full);
  setAction("Batteria scarica: il display si spegne finché non colleghi la USB-C");
  return finish(now);
}

void Ui::calibAbort(const char *msg, Ms now) {
  Model &m = m_;
  memset(&m.calib, 0, sizeof m.calib);
  while (m.ui.depth > 1 && top(m).id == ScreenId::Calib) popScreen(m);
  toast(m, msg, now);
  logEv(m, 0, "calibrazione annullata", now);
  setAction(msg);
  redraw(RefreshKind::Quick);
}

// Avanzamento (calibTick del simulatore): il tempo con il cavo collegato non conta.
void Ui::calibTick(Ms t, const WallClock &wc) {
  Model &m = m_;
  Calib &k = m.calib;
  if (!calRunning(m)) return;
  const Ms dt = t - k.last;
  k.last = t;
  if (k.phase == CalibPhase::Wait) {
    if (m.sys.usb) return;
    if (m.sys.batt < kCalMinPct) { calibAbort("Carica almeno al 90%", t); return; }
    k.phase = CalibPhase::Max;
    k.ms = 0;
    k.next = 0;
    k.n = 0;
    k.paused = false;
    logEv(m, 0, "calibrazione: fase 1", t);
    redraw(RefreshKind::Partial);
  }
  if (m.sys.usb) {
    if (!k.paused) { k.paused = true; redraw(RefreshKind::Partial); }
    return;
  }
  if (k.paused) { k.paused = false; redraw(RefreshKind::Partial); }
  else k.ms += dt;
  if (!m.sys.live) {
    // Simulatore: la batteria scende come su una scheda vera (8 %/h in fase 1, 1,5 %/h in fase 2).
    const double b = m.sys.batt - dt * (k.phase == CalibPhase::Max ? 8.0 : 1.5) / 3600000.0;
    m.sys.batt = b > 0 ? b : 0;
  }
  if (k.ms >= k.next && !k.due) {
    if (m.sys.live) k.due = true;  // la tensione la legge il loop (ADC, mai durante un refresh)
    else calibSampleIn(mvFromPercent(m.sys.batt), t, wc);
  }
}

RefreshKind Ui::calibSample(int mv, Ms now, const WallClock &wc) {
  req_ = RefreshKind::None;
  if (calRunning(m_) && m_.calib.due) calibSampleIn(mv, now, wc);
  return finish(now);
}

void Ui::calibSampleIn(int mv, Ms t, const WallClock &wc) {
  Model &m = m_;
  Calib &k = m.calib;
  k.due = false;
  k.mv = mv;
  if (k.n < kCalCap) {
    k.h[k.n] = k.ms / 3600000.0;
    k.p[k.n] = ocvPercent(mv);
    k.n++;
  }
  k.next += kCalEveryMs;
  if (k.ms >= kCalPhaseMs) {
    double r = 0;
    const bool ok = calRate(k.h, k.p, k.n, r);
    if (k.phase == CalibPhase::Max) {
      k.hasMax = ok;
      k.rMax = r;
      k.phase = CalibPhase::Idle;
      k.ms = 0;
      k.next = 0;
      k.n = 0;
      logEv(m, 0, "calibrazione: fase 2", t);
    } else {
      if (!k.hasMax || !ok || k.rMax <= 0 || r <= 0) { calibAbort("Calibrazione non riuscita", t); return; }
      k.phase = CalibPhase::Done;
      m.bat.cal.valid = true;
      m.bat.cal.active = k.rMax;
      m.bat.cal.idle = r;
      m.bat.cal.atLocal = wc.localMs;
      batChanged_ = true;
      logEv(m, 0, "batteria calibrata", t);
      toast(m, "Batteria calibrata", t);
    }
  }
  redraw(RefreshKind::Partial);
}

}  // namespace cal

// ---------- aggiornamento remoto (docs/OTA.md §1.6) ----------
namespace cal {

void Ui::closeOta() {
  while (m_.ui.depth > 1 && top(m_).id == ScreenId::Ota) popScreen(m_);
  if (top(m_).id == ScreenId::Ota) resetStack(m_, makeScreen(ScreenId::Home));
  memset(&m_.ota, 0, sizeof m_.ota);
}

void Ui::showOta() {
  if (top(m_).id != ScreenId::Ota && !pushScreen(m_, makeScreen(ScreenId::Ota))) {
    resetStack(m_, makeScreen(ScreenId::Home));
    pushScreen(m_, makeScreen(ScreenId::Ota));
  }
}

RefreshKind Ui::otaStart(const char *from, const char *to, uint32_t total, Ms now) {
  req_ = RefreshKind::None;
  OtaView &o = m_.ota;
  memset(&o, 0, sizeof o);
  o.phase = OtaPhase::Receive;
  scopy(o.from, from);
  scopy(o.to, to);
  o.total = total;
  o.drawnAt = now;
  showOta();
  setAction("Aggiornamento dal PC");
  redraw(RefreshKind::Quick);
  return finish(now);
}

RefreshKind Ui::otaProgress(uint32_t done, Ms now) {
  req_ = RefreshKind::None;
  OtaView &o = m_.ota;
  if (o.phase != OtaPhase::Receive) return RefreshKind::None;
  o.done = done > o.total ? o.total : done;
  // Ogni ~5% o ogni 2 s, il più lento dei due (docs/OTA.md §1.6): servono entrambi.
  const int pct = otaPct(o);
  if (pct - o.drawnPct < kOtaRedrawPct || now - o.drawnAt < kOtaRedrawMs) return RefreshKind::None;
  o.drawnPct = pct;
  o.drawnAt = now;
  o.step++;  // la corsa avanza di un fotogramma a ogni ridisegno
  if (top(m_).id != ScreenId::Ota) return RefreshKind::None;
  redraw(RefreshKind::Partial);
  return finish(now);
}

RefreshKind Ui::otaPhase(OtaPhase p, const char *why, Ms now) {
  req_ = RefreshKind::None;
  OtaView &o = m_.ota;
  if (o.phase == OtaPhase::None) return RefreshKind::None;  // nessun aggiornamento a schermo
  const bool result = p == OtaPhase::Error || p == OtaPhase::Rejected;
  o.phase = p;
  if (p == OtaPhase::Verify || p == OtaPhase::Write || p == OtaPhase::Reboot) o.done = o.total;
  scopy(o.why, why ? why : "");
  o.until = result ? now + kOtaResultMs : 0;
  o.drawnPct = otaPct(o);
  o.drawnAt = now;
  o.step++;
  showOta();
  redraw(result ? RefreshKind::Quick : RefreshKind::Partial);  // errore: cambia tutto il layout
  return finish(now);
}

RefreshKind Ui::otaRejected(const char *from, const char *to, const char *why, Ms now) {
  req_ = RefreshKind::None;
  OtaView &o = m_.ota;
  memset(&o, 0, sizeof o);
  o.phase = OtaPhase::Rejected;
  scopy(o.from, from);
  scopy(o.to, to);
  scopy(o.why, why ? why : "");
  o.until = now + kOtaResultMs;
  showOta();
  setAction("Aggiornamento rifiutato");
  redraw(RefreshKind::Quick);
  return finish(now);
}

RefreshKind Ui::otaUpdated(const char *version, Ms now) {
  req_ = RefreshKind::None;
  char msg[kToastLen];
  Sb(msg, sizeof msg).s("Aggiornato alla ").s(version);
  toast(m_, msg, now, 5000);
  setAction(msg);
  redraw(RefreshKind::Partial);
  return finish(now);
}

// ---------- 0.10.0: gioco della mascotte ----------
// petWear() del simulatore; la logica pura (interruttore, game, festa) è in core/pet.

void Ui::petWear(int k, Ms now) {
  Pet &p = m_.pet;
  if (k >= p.nUnlocked) { setAction(""); return; }
  const PetUnlocked *u = k >= 0 ? &p.unlocked[k] : nullptr;
  memset(&p.wear, 0, sizeof p.wear);
  if (u) {
    // Il CRC di quello che c'è già (catalogo o ricevuto); se non c'è, qualunque versione: la dirà il PC con il game.
    scopy(p.wear.id, u->id);
    AvatarRef any;
    setAvatarRef(any, u->id);
    const Accessory *a = accessoryStore() ? accessoryStore()->find(any) : builtinAccessoryById(u->id);
    if (a) { p.wear.crc = a->crc; p.wear.hasCrc = true; }
  }
  // accessory-wear {id | null} al PC, che risponde con un game aggiornato.
  if (netFn_) {
    NetRequest r;
    memset(&r, 0, sizeof r);
    r.cmd = NetCmd::AccWear;
    scopy(r.avatarIds[0], u ? u->id : "");
    r.nAvatarIds = 1;
    netFn_(r, netCtx_);
  }
  petChanged_ = true;
  if (top(m_).id == ScreenId::List && top(m_).menu == MenuId::PetAcc) popScreen(m_);
  char msg[64];
  toast(m_, u ? Sb(msg, sizeof msg).s("Indossa: ").s(u->name).str() : "Nessun accessorio", now);
  redraw(RefreshKind::Quick);
  setAction(u ? Sb(msg, sizeof msg).s("Accessorio: ").s(u->name).str() : "Accessorio: nessuno");
}

// 0.13.0: Display › Tema › 2×. Il loop vede il cambio di m.themes.cur, attiva il tema e lo salva.
void Ui::themePick(int k, Ms now) {
  (void)now;
  Themes &t = m_.themes;
  t.cur = k >= 0 && k < t.n ? k : -1;
  t.picked = true;
  char msg[64];
  Sb(msg, sizeof msg).s("Tema: ").s(t.cur >= 0 ? t.items[t.cur].name : "Calamaio");
  redraw(RefreshKind::Partial);
  setAction(msg);
}

RefreshKind Ui::netGame(const GameMsg &g, Ms now) {
  req_ = RefreshKind::None;
  const bool wasOn = petOn(m_), wasTired = m_.pet.tired, wasSpecial = m_.pet.special;
  const AvatarRef wasWear = m_.pet.wear;
  const Celebrate c = applyGame(m_, g, now);
  petChanged_ = true;
  // Si ridisegna se cambia la mascotte (acceso, stanca, accessorio), se si vedono i dati del gioco o per la festa.
  const bool look = wasOn != petOn(m_) || (petOn(m_) && (wasTired != m_.pet.tired || wasWear != m_.pet.wear || wasSpecial != m_.pet.special)) ||
                    m_.toast.until > now;
  const ScreenEntry &tp = top(m_);
  const bool shown = tp.id == ScreenId::Pet || (tp.id == ScreenId::List && (tp.menu == MenuId::PetAcc || tp.menu == MenuId::Main));
  if (look || shown || c != Celebrate::None) redraw(RefreshKind::Partial);
  if (g.hasCelebrate) {  // come il pulsante Simula «Mascotte: traguardo»
    char msg[128];
    const char *how = c == Celebrate::Party ? " (festa di 3 s)" : c == Celebrate::Toast ? " (solo il toast)"
                      : petOn(m_) ? " (già festeggiato)" : " (gioco spento: niente)";
    setAction(Sb(msg, sizeof msg).s("Traguardo: ").s(g.celebrateTitle).s(how).str());
  }
  return finish(now);
}

RefreshKind Ui::netAccessoryArrived(Ms now) {
  req_ = RefreshKind::None;
  if (petOn(m_)) redraw(RefreshKind::Partial);
  return finish(now);
}

}  // namespace cal
