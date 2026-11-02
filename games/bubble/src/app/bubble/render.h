#ifndef BUBBLE_RENDER_H
#define BUBBLE_RENDER_H

#include "hge_gfx.h"
#include "hge_resource.h"
#include "app/bubble/battle.h"

#include <string>

namespace bbl {

const int kWinW = 960;
const int kWinH = 540;

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
void RenderFrame(RenderCtx& c, Side sides[2], double time, float resultTimer);

} // namespace bbl

#endif // BUBBLE_RENDER_H
