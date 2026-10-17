// Aggancio di Menu › Gioco › ROM per il piano B (giochi Game Boy): le voci dei giochi del PC e del gioco già sulla
// scheda, sempre (anche col controller scollegato: la scelta senza controller la rifiuta il piano B).
// Scrive al massimo cap voci complete (MenuItem azzerata e riempita) e ritorna quante.
#pragma once
#include "menu.h"

namespace cal {

int gbGameItems(const Model &m, Ms now, MenuItem *out, int cap);

}  // namespace cal
