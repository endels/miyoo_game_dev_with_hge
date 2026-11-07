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
    // Duel per-shot thinking: aiPlan() selects the shot and points the barrel;
    // aiUpdate() only counts down and fires. aiAimCol is the planned landing
    // column for the telegraph (-1 when there is no plan).
    bool  aiPlanned;
    int   aiAimCol;
    bool  aiFallbackShot;   // last aiPlan() was the no-target lob

    // duel v2: the AI is the same greedy shooter, but it cares about the
    // column its attack will land in (AI4) and fires on the duel clock.
    bool  duelMode;
    float aiInterval;       // seconds between bot shots (AI2)
    int   aiFocusCol;       // column to favour (-1 = no preference)
    float aiFocusW;         // weight of the column preference (AI4)

    // Difficulty dial state, all written by aiApplyLevel(). Level 3 is the
    // tuning this build shipped with, so a default match is unchanged.
    int   aiLevel;          // 1..kAiLevels
    float aiJitter;         // aim noise in px
    float aiBounceP;        // chance to even consider a wall shot
    float aiPace;           // stage mode: shot delay multiplier

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

// ---- CPU difficulty dial ------------------------------------------------
// Clicking the CPU dino cycles through these. One table, one place to tune the
// bot: level 3 is the tuning this build shipped with, 1 is a slow and wild
// shooter, 5 barely misses and hits harder than the default.
const int kAiLevels       = 5;
const int kAiLevelDefault = 3;
// Duel: every CPU shot gets this much visible thinking before it fires. Stage
// mode keeps its old level-based delays.
const float kDuelCpuThinkSec = 0.30f;

struct AiLevelInfo {
    const char* name;
    float interval;     // duel: seconds between CPU shots (AI2)
    float jitter;       // aim noise, px
    float focusW;       // weight on the opponent's deepest column (AI4)
    float bounce;       // wall-shot consideration chance
    int   atkBonus;     // extra garbage on every shot that connects (AI6)
    float pace;         // stage mode: shot delay multiplier
};

int  aiClampLevel(int level);
const AiLevelInfo& aiLevelInfo(int level);
// Write every AI knob of one side from the table. aiFocusCol is per-turn state
// and deliberately left alone.
void aiApplyLevel(Side& s, int level);

void resetBattle(Side s[2]);
// Decide the next shot and point the barrel. Returns true for a wall-bounce
// shot (stage mode uses that to add its extra delay). No firing.
bool aiPlan(Side& s);
void aiUpdate(Side& s, float dt);
void updateBattle(Side s[2], float dt);

// Deterministic checksum of both boards + counters, for regression tests.
u64 battleChecksum(const Side s[2]);

} // namespace bbl

#endif // BUBBLE_BATTLE_H
