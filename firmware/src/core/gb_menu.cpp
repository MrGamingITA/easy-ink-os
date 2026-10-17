// Giochi Game Boy (piano B, primo gioco): le voci di Menu › Gioco › ROM (MenuId::GbRoms del piano A). La ROM si scrive
// col cavo nella partizione «gbrom»: qui c'è al più un gioco, riconosciuto dall'intestazione. La voce si vede sempre,
// anche col controller scollegato (la scelta senza controller la rifiuta Ui::gbActivate).
#include "gb_menu.h"

#include <string.h>

#include "strbuf.h"

namespace cal {

namespace {

MenuItem *item(MenuItem *out, int cap, int &n, const char *label, const char *val, const char *help) {
  if (n >= cap) return nullptr;
  MenuItem &it = out[n++];
  memset(&it, 0, sizeof it);
  it.kind = ItemKind::Info;
  it.bars = -1;
  scopy(it.label, label);
  scopy(it.val, val);
  scopy(it.help, help);
  return &it;
}

}  // namespace

int gbGameItems(const Model &m, Ms now, MenuItem *out, int cap) {
  (void)now;
  const GbView &g = m.gb;
  int n = 0;
  if (!g.partOk) {
    item(out, cap, n, "Giochi non disponibili", "",
         "Manca lo spazio per i giochi: serve un aggiornamento via cavo della memoria della scheda.");
    return n;
  }
  if (!g.romOk) {
    item(out, cap, n, "Nessuna ROM", "", "La ROM si scrive sulla scheda col cavo USB, dal PC.");
    return n;
  }
  MenuItem *it = item(out, cap, n, g.romTitle[0] ? g.romTitle : "Gioco senza titolo", "sulla scheda",
                      "Premi per giocare. Durante la partita Wi-Fi e avvisi sono in pausa: le richieste di permesso "
                      "aspettano nel terminale.");
  if (it) {
    it->kind = ItemKind::Act;
    it->act = ActId::GbPick;
    it->net = -1;
  }
  return n;
}

}  // namespace cal
