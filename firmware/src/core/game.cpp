#include "game.h"

namespace cal {

const char *const kRanks[14] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

uint32_t xorshift32(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

int gameDraw(Game &g, RandomFn rnd) {
  const uint32_t v = rnd ? rnd() : xorshift32(g.seed);
  return 1 + static_cast<int>(v % 13);
}

void gameNew(Game &g, RandomFn rnd) {
  g.card = gameDraw(g, rnd);
  g.prev = 0;
  g.streak = 0;
  g.last = GameResult::None;
  g.over = false;
  g.startBest = g.best;
}

GameResult gameGuess(Game &g, bool up, RandomFn rnd) {
  const int next = gameDraw(g, rnd);
  g.prev = g.card;
  g.card = next;
  if (next == g.prev) {
    g.last = GameResult::Tie;
  } else if ((next > g.prev) == up) {
    g.last = GameResult::Win;
    g.streak++;
    if (g.streak > g.best) g.best = g.streak;
  } else {
    g.last = GameResult::Lose;
    g.over = true;
  }
  return g.last;
}

MascotPose gamePose(const Game &g) {
  switch (g.last) {
    case GameResult::Win: return MascotPose::Done;
    case GameResult::Lose: return MascotPose::Error;
    case GameResult::Tie: return MascotPose::Idle;
    default: return MascotPose::Input;
  }
}

}  // namespace cal
