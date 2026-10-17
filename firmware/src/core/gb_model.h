// Giochi Game Boy: la parte del modello che l'interfaccia mostra (Menu › Gioco › ROM, avvio, partita, pausa, bande).
// Senza JSON né hardware: la includono model.h e i test. Primo gioco (2026-10-15): la ROM si scrive col cavo nella
// partizione «gbrom» e la scheda la riconosce dall'intestazione; l'elenco dei giochi del PC arriva col piano C.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "clock.h"

namespace cal {
namespace gb {

const size_t kIdLen = 17;     // 16 cifre esadecimali minuscole (primi 8 byte dello SHA-256 della ROM, contratto)
const size_t kTitleLen = 17;  // titolo dell'intestazione della cartuccia (0x134, ≤ 16 ASCII)

enum class PauseWhy : uint8_t { None, User, Controller, Idle, Perm };

// Richieste del menu al loop (Ui::takeGbAct): il loop le passa al runtime del gioco (hw/gb_main).
enum class GbAct : uint8_t { None, Pick, Resume, Exit, ExitRest, Pause };

}  // namespace gb

struct GbView {
  bool partOk;                     // partizione «gbrom» presente (tabella nuova, flash via cavo fatto)
  bool romOk;                      // nella partizione c'è un'intestazione valida
  char romTitle[gb::kTitleLen];    // titolo della ROM nella partizione ('' se senza titolo)
  uint32_t romKb;                  // dimensione dichiarata dall'intestazione, KB
  char title[gb::kTitleLen];       // del gioco in avvio, in gioco o in pausa
  gb::PauseWhy pause;
  bool lowBand;                    // batteria < 15 % senza cavo: avviso nella banda
};

}  // namespace cal
