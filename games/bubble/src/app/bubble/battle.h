#ifndef BUBBLE_BATTLE_H
#define BUBBLE_BATTLE_H

#include "app/bubble/game.h"

namespace bbl {

const int kBoardCols = 10;
extern const float kBoardX[2];

struct Side {
    Game  game;
    float energy;
    float aiTimer;
    int   lastPops;
    int   lastDrops;
    bool  ai;
};

void resetBattle(Side s[2]);
void aiUpdate(Side& s, float dt);
void updateBattle(Side s[2], float dt);

// Deterministic checksum of both boards + counters, for regression tests.
u64 battleChecksum(const Side s[2]);

} // namespace bbl

#endif // BUBBLE_BATTLE_H
