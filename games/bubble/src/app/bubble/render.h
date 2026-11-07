#ifndef BUBBLE_RENDER_H
#define BUBBLE_RENDER_H

#include "hge_gfx.h"
#include "hge_resource.h"
#include "app/bubble/battle.h"

#include <string>

namespace bbl {

const int kWinW = 960;
const int kWinH = 540;

struct HudState {
    int stage = 1;
    int corruptPopped = 0;
    int corruptTotal = 0;
    int phase = 0;              // 0 play, 1 clear, 2 draft, 3 over
    int draftSel = 0;
    const char* draftName[3] = {0, 0, 0};
    const char* draftDesc[3] = {0, 0, 0};
    bool perfect = false;
    bool canRetry = false;
    int  retries = 0;
    const char* banner = nullptr;
    const char* modName = nullptr;
    const char* modDesc = nullptr;
    float bannerTimer = 0.0f;
    float corruptFlash = 0.0f;
    float chainTimer = 0.0f;
    int   chainRank = 0;
    int   chainCount = 0;
    float shake = 0.0f;
    float shakeAmp = 0.0f;

    // turn-based duel (v2)
    bool  duel = false;
    int   depth[2] = { 0, 0 };          // DepthRows(), 0..13
    int   depthMax = 13;
    int   turn = 0;
    int   shotsLeft = 0;
    int   shotsMax = 5;
    int   nextDescendIn = 0;            // shots until the next descend
    int   attackOut[2] = { 0, 0 };      // last attack sent by each side
    int   attackCol[2] = { -1, -1 };    // target column of that attack
    float attackFlash = 0.0f;
    int   duelResult = 0;               // 0 running, 1 win, 2 lose, 3 draw
    int   duelShots = 0;                // global shots played
    int   duelMaxAtk = 0;
    bool  pass = false;                 // hand-over / turn-start: no firing
};

struct RenderCtx {
    IGfx* g = nullptr;
    IGfxTexture* bg = nullptr;
    IGfxTexture* bubbles[kColors] = {0};
    IGfxTexture* bubblesBig[kColors] = {0};
    IGfxTexture* bubblesAnim[kColors] = {0};
    IGfxTexture* cannonBase = nullptr;
    IGfxTexture* cannonBarrelFrames = nullptr;
    float barrelSrcW = 0.0f, barrelSrcH = 0.0f;
    IGfxTexture* dot = nullptr;
    IGfxTexture* ring = nullptr;
    IGfxTexture* wave = nullptr;
    IGfxTexture* font = nullptr;
    IGfxTexture* dinoIdle[2][3] = {{0}};
    IGfxTexture* dinoShoot[2] = {0};
    IGfxTexture* dinoCheer[2] = {0};
    IGfxTexture* dinoSad[2] = {0};
    IGfxTexture* dinoLook[2] = {0};
};

bool LoadTextures(RenderCtx& c, IGfx* gfx, const Pack& pack, std::string* err);
void FreeTextures(RenderCtx& c);
void RenderFrame(RenderCtx& c, Side sides[2], double time, float resultTimer,
                 const HudState& hud);

} // namespace bbl

#endif // BUBBLE_RENDER_H
