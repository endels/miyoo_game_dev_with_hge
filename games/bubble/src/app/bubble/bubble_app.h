#ifndef BUBBLE_APP_H
#define BUBBLE_APP_H

#include "hge_app.h"
#include "hge_audio.h"
#include "app/bubble/battle.h"
#include "app/bubble/render.h"

#include <memory>
#include <string>

namespace bbl {

struct BubbleOptions {
    int         warmup = 0;
    float       animOffset = 0.0f;
    float       simSeconds = 0.0f;
    std::string selfPath;
};

class BubbleApp : public hge::IGame {
public:
    explicit BubbleApp(const BubbleOptions& opt);
    ~BubbleApp() override;

    bool Init(IGfx* gfx, int width, int height) override;
    void Update(float dt, const hge::Input& in) override;
    void Render(IGfx* gfx, double time) override;
    void OnResize(int width, int height) override;
    void Shutdown() override;

private:
    struct AudioPack;

    void updateAudio(float dt);

    BubbleOptions m_opt;
    IGfx*         m_gfx = nullptr;
    RenderCtx     m_render;
    Side          m_sides[2];

    float m_resultTimer = -1.0f;
    int   m_lastMoves[2] = { 0, 0 };
    int   m_lastPopsAll = 0;
    int   m_lastDropsAll = 0;
    int   m_popsAtFire = 0;
    Game::State m_prevState[2] = { Game::AIM, Game::AIM };

    std::unique_ptr<AudioPack> m_audioPack;
    bool m_audioOn = false;
    const audio::Clip* m_cShoot = nullptr;
    const audio::Clip* m_cStick = nullptr;
    const audio::Clip* m_cPop   = nullptr;
    const audio::Clip* m_cDrop  = nullptr;
    const audio::Clip* m_cDesc  = nullptr;
    const audio::Clip* m_cWin   = nullptr;
    const audio::Clip* m_cLose  = nullptr;

    bool m_ready = false;
};

} // namespace bbl

#endif // BUBBLE_APP_H
