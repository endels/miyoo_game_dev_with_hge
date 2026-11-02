#include "app/bubble/battle.h"

#include <cmath>
#include <cstring>

namespace bbl {

const float kBoardX[2] = { 6.0f, 554.0f };

namespace {

float rnd01()
{
    static u32 s = 0x9E3779B9u;
    s = s * 1664525u + 1013904223u;
    return (float)((s >> 8) & 0xFFFF) / 65535.0f;
}

} // namespace

void resetBattle(Side s[2])
{
    for (int i = 0; i < 2; ++i) {
        s[i].game.Configure(kBoardCols, kBoardX[i]);
        s[i].game.SetColors(kMaxColors);
        s[i].game.Reset(0x1234u + (u32)i * 777u);
        s[i].energy = 0.0f;
        s[i].aiTimer = 1.2f;
        s[i].lastPops = s[i].game.Pops();
        s[i].lastDrops = s[i].game.Drops();
    }
    s[0].ai = false;
    s[1].ai = true;
}

void aiUpdate(Side& s, float dt)
{
    if (s.game.GetState() != Game::AIM) return;
    s.aiTimer -= dt;
    if (s.aiTimer > 0.0f) return;

    // Score every attachable empty cell: matching neighbours matter most,
    // then prefer higher cells so the stack does not creep downwards.
    // Only consider cells with a clear line of fire: aiming at a buried cell
    // just makes the shot bounce off the outer shell.
    auto reachable = [&](float tx, float ty) {
        const float sx = s.game.ShooterX(), sy = kShooterY;
        const float dx = tx - sx, dy = ty - sy;
        const float dist = sqrtf(dx * dx + dy * dy);
        const int steps = (int)(dist / 8.0f);
        for (int i = 3; i < steps - 2; ++i) {
            const float u = (float)i / (float)steps;
            if (s.game.SolidAt(sx + dx * u, sy + dy * u)) return false;
        }
        return true;
    };

    const int want = s.game.CurrentColor();
    int bestR = -1, bestC = -1, bestScore = -100000;
    for (int r = 0; r <= s.game.RowsUsed() && r < kMaxRows; ++r) {
        const int n = s.game.RowCount(r);
        for (int c = 0; c < n; ++c) {
            if (s.game.Cell(r, c)) continue;
            if (!s.game.Attachable(r, c)) continue;
            if (!reachable(s.game.XOf(r, c), Game::YOf(r))) continue;
            const int same = s.game.SameNeighbors(r, c, want);
            const int score = same * 100 - r * 3 + (int)(rnd01() * 6.0f);
            if (score > bestScore) { bestScore = score; bestR = r; bestC = c; }
        }
    }

    if (bestR < 0) {
        s.game.AimAt(s.game.ShooterX() + (rnd01() - 0.5f) * 40.0f, 120.0f);
        s.game.Fire();
        s.aiTimer = 0.9f + rnd01() * 0.8f;
        return;
    }

    const float tx = s.game.XOf(bestR, bestC) + (rnd01() - 0.5f) * 14.0f;
    s.game.AimAt(tx, Game::YOf(bestR));
    s.game.Fire();
    s.aiTimer = 1.00f + rnd01() * 0.75f;
}

void updateBattle(Side s[2], float dt)
{
    s[0].game.Update(dt);
    s[1].game.Update(dt);

    for (int i = 0; i < 2; ++i) {
        const int dp = s[i].game.Pops() - s[i].lastPops;
        const int dd = s[i].game.Drops() - s[i].lastDrops;
        s[i].lastPops = s[i].game.Pops();
        s[i].lastDrops = s[i].game.Drops();
        if (dp > 0 || dd > 0) {
            // direct pops count half, chain drops count full: dropping is the
            // skilled play, so it should be what charges the attack
            s[i].energy += (float)dp * 0.5f + (float)dd;
            while (s[i].energy >= (float)kEnergyPerWave) {
                s[i].energy -= (float)kEnergyPerWave;
                s[1 - i].game.AddGarbage(kWaveSize);
            }
        }
        if (s[i].game.GetState() == Game::WIN) s[i].game.ResumeIfWon();
    }
}

u64 battleChecksum(const Side s[2])
{
    u64 h = 1469598103934665603ull;
    auto mix = [&h](u64 v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFFull;
            h *= 1099511628211ull;
        }
    };

    for (int i = 0; i < 2; ++i) {
        const Game& g = s[i].game;
        mix((u64)g.RowsUsed());
        mix((u64)g.Cols());
        mix((u64)g.Score());
        mix((u64)g.Moves());
        mix((u64)g.Pops());
        mix((u64)g.Drops());
        mix((u64)g.Descends());
        mix((u64)g.BubblesLeft());
        mix((u64)g.GetState());
        mix((u64)g.CurrentColor());
        mix((u64)g.NextColor());
        for (int r = 0; r < kMaxRows; ++r) {
            for (int c = 0; c < kColsMax; ++c)
                mix((u64)g.Cell(r, c));
        }
        u32 energyBits = 0;
        memcpy(&energyBits, &s[i].energy, sizeof(energyBits));
        mix((u64)energyBits);
        mix((u64)s[i].game.Risers().size());
        mix((u64)s[i].game.FallingBubbles().size());
        mix((u64)s[i].game.Particles().size());
    }
    return h;
}

} // namespace bbl
