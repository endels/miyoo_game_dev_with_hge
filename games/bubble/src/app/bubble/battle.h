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

    // duel v2: the AI is the same greedy shooter, but it cares about the
    // column its attack will land in (AI4) and fires on the duel clock.
    bool  duelMode;
    float aiInterval;       // seconds between bot shots (AI2)
    int   aiFocusCol;       // column to favour (-1 = no preference)
    float aiFocusW;         // weight of the column preference (AI4)

    // Stage tuning. The defaults reproduce the original endless-versus AI;
    // resetStage-style setup can slow the AI down and delay garbage.
    bool  sparring;
    bool  keepWin;          // stage boards stay WIN so the app can see it
    float stun;
    float clearStun;        // pause after the AI clears its own board
    float fireScale;        // <1 = faster sparring AI
    int   garbageAfterMoves;
    float energyGain;
    bool  stageRules;       // stage tuning: drops charge faster, bigger waves
    float dropEnergy;       // energy per dropped bubble
    int   waveSize;         // base garbage bubbles per attack wave
    int   attackBonus;      // extra bubbles earned by a big drop / chain
    bool  chainPay;         // 5+ pop chains bank an extra garbage bubble
};

void resetBattle(Side s[2]);
void aiUpdate(Side& s, float dt);
void updateBattle(Side s[2], float dt);

// Deterministic checksum of both boards + counters, for regression tests.
u64 battleChecksum(const Side s[2]);

} // namespace bbl

#endif // BUBBLE_BATTLE_H
