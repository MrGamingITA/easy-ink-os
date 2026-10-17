// Giochi Game Boy: la parte di cal::Ui che riguarda avvio, partita e pausa (dichiarata in ui.h). Le regole della
// partita stanno in core/gb_session; qui solo pila delle schermate, testi e richieste per il loop (hw/gb_main).
#include <string.h>

#include "strbuf.h"
#include "ui.h"

namespace cal {

namespace {

bool isPause(const ScreenEntry &e) { return e.id == ScreenId::List && e.menu == MenuId::GbPause; }
bool isGbScreen(const ScreenEntry &e) { return e.id == ScreenId::GbLoad || e.id == ScreenId::GbPlay || isPause(e); }

}  // namespace

bool Ui::gbOnStack() const {
  for (int k = 0; k < m_.ui.depth; k++)
    if (isGbScreen(m_.ui.stack[k])) return true;
  return false;
}

gb::GbAct Ui::takeGbAct() {
  const gb::GbAct a = gbAct_;
  gbAct_ = gb::GbAct::None;
  return a;
}

void Ui::gbInstalled(bool partOk, bool romOk, const char *title, uint32_t romKb) {
  GbView &g = m_.gb;
  g.partOk = partOk;
  g.romOk = partOk && romOk;
  scopy(g.romTitle, g.romOk && title ? title : "");
  g.romKb = g.romOk ? romKb : 0;
}

RefreshKind Ui::gbLoad(Ms now) {
  req_ = RefreshKind::None;
  GbView &g = m_.gb;
  scopy(g.title, g.romTitle);
  g.pause = gb::PauseWhy::None;
  g.lowBand = false;
  while (m_.ui.depth > 1 && isGbScreen(top(m_))) popScreen(m_);
  if (!pushScreen(m_, makeScreen(ScreenId::GbLoad))) {
    resetStack(m_, makeScreen(ScreenId::Home));
    pushScreen(m_, makeScreen(ScreenId::GbLoad));
  }
  setAction("Gioco: avvio");
  redraw(RefreshKind::Quick);
  return finish(now);
}

RefreshKind Ui::gbPlay(Ms now) {
  req_ = RefreshKind::None;
  for (int k = 0; k < m_.ui.depth; k++)
    if (m_.ui.stack[k].id == ScreenId::GbLoad) m_.ui.stack[k] = makeScreen(ScreenId::GbPlay);
  if (!inStack(ScreenId::GbPlay)) pushScreen(m_, makeScreen(ScreenId::GbPlay));
  m_.gb.pause = gb::PauseWhy::None;
  setAction("Gioco: partita");
  (void)now;
  return RefreshKind::None;
}

RefreshKind Ui::gbPaused(gb::PauseWhy w, Ms now) {
  req_ = RefreshKind::None;
  m_.gb.pause = w;
  ScreenEntry e = makeScreen(ScreenId::List);
  e.menu = MenuId::GbPause;
  e.sel = (w == gb::PauseWhy::Controller || w == gb::PauseWhy::Idle) ? 1 : 0;  // su «Riprendi» (dopo la riga d'avviso)
  int at = -1;
  for (int k = 0; k < m_.ui.depth; k++)
    if (m_.ui.stack[k].id == ScreenId::GbPlay) at = k;
  if (at < 0) return RefreshKind::None;
  if (at + 1 < m_.ui.depth && isPause(m_.ui.stack[at + 1])) {
    m_.ui.stack[at + 1].sel = e.sel;  // già in pausa: aggiorna solo la riga d'avviso
  } else {
    if (m_.ui.depth >= kStackCap) return RefreshKind::None;
    memmove(&m_.ui.stack[at + 2], &m_.ui.stack[at + 1], sizeof(ScreenEntry) * size_t(m_.ui.depth - at - 1));
    m_.ui.stack[at + 1] = e;
    m_.ui.depth++;
  }
  setAction("Gioco: pausa");
  redraw(RefreshKind::Quick);
  return finish(now);
}

RefreshKind Ui::gbResumed(Ms now) {
  req_ = RefreshKind::None;
  if (isPause(top(m_))) popScreen(m_);
  m_.gb.pause = gb::PauseWhy::None;
  setAction("Gioco: si riprende");
  (void)now;
  return RefreshKind::None;
}

RefreshKind Ui::gbEnd(const char *msg, bool rest, Ms now) {
  req_ = RefreshKind::None;
  int d = 0;
  for (int k = 0; k < m_.ui.depth; k++)
    if (!isGbScreen(m_.ui.stack[k])) m_.ui.stack[d++] = m_.ui.stack[k];
  if (d == 0) {
    m_.ui.stack[0] = makeScreen(ScreenId::Home);
    d = 1;
  }
  m_.ui.depth = d;
  GbView &g = m_.gb;
  g.title[0] = 0;
  g.pause = gb::PauseWhy::None;
  g.lowBand = false;
  if (rest && top(m_).id != ScreenId::Sleep) pushScreen(m_, makeScreen(ScreenId::Sleep));
  if (msg && msg[0]) toast(m_, msg, now);
  setAction(msg && msg[0] ? msg : "Gioco: fine");
  redraw(RefreshKind::Quick);
  return finish(now);
}

bool Ui::gbInput(Action a, Ms now) {
  (void)now;
  const ScreenEntry &tp = top(m_);
  if (tp.id == ScreenId::GbLoad) {
    setAction("Avvio del gioco: un momento");
    return true;
  }
  if (tp.id == ScreenId::GbPlay) {  // il tasto PRG durante la partita: pausa
    gbAct_ = gb::GbAct::Pause;
    setAction("Pausa");
    return true;
  }
  if (!isPause(tp)) return false;
  switch (a) {
    case Action::Back: gbAct_ = gb::GbAct::Resume; setAction("Riprendi"); return true;
    case Action::Sleep: gbAct_ = gb::GbAct::ExitRest; setAction("Salva ed esci, poi Riposo"); return true;
    case Action::Menu:
    case Action::Home: setAction("Prima Riprendi o Salva ed esci"); return true;
    default: return false;  // su, giù, scegli: come ogni lista
  }
}

void Ui::gbActivate(ActId a, Ms now) {
  switch (a) {
    case ActId::GbPick:
      if (m_.sys.padLink != pad::Link::Ready) {  // l'elenco si vede sempre, ma si gioca solo col controller
        toast(m_, "Collega il controller per giocare", now);
        setAction("Collega il controller per giocare");
        redraw(RefreshKind::Partial);
        break;
      }
      gbAct_ = gb::GbAct::Pick;
      setAction("Gioco scelto");
      break;
    case ActId::GbResume: gbAct_ = gb::GbAct::Resume; setAction("Riprendi"); break;
    case ActId::GbExit: gbAct_ = gb::GbAct::Exit; setAction("Salva ed esci"); break;
    default: break;
  }
}

}  // namespace cal
