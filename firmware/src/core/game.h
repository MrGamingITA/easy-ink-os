// 0.9.0: gioco «Alto o basso», logica pura (provata in native, test_game). Porting di gameDraw, gameNew e gameGuess
// del simulatore. Carte 1–13 (A, 2…10, J, Q, K): si indovina se la prossima sarà più alta (1×) o più bassa (2×);
// una carta uguale è pari (la serie resta com'è); sbagliando la partita finisce. Il record sale appena la serie lo
// supera, così uscire a metà partita (o un permesso che la interrompe) non lo perde.
#pragma once
#include <stdint.h>

#include "model.h"

namespace cal {

typedef uint32_t (*RandomFn)();  // sulla scheda esp_random; nullptr = xorshift32 su Game::seed (simulatore, test)

extern const char *const kRanks[14];  // RANKS del simulatore: '', 'A', '2', …, '10', 'J', 'Q', 'K'
uint32_t xorshift32(uint32_t &s);
int gameDraw(Game &g, RandomFn rnd);
void gameNew(Game &g, RandomFn rnd);
GameResult gameGuess(Game &g, bool up, RandomFn rnd);
MascotPose gamePose(const Game &g);  // input: domanda · done: giusto · idle: pari · error: sbagliato

}  // namespace cal
