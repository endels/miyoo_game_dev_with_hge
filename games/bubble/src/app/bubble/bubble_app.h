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
    bool        debugClear = false;   // screenshot/test: jump to STAGE CLEAR
    bool        debugDraft = false;   // screenshot/test: jump to the draft screen
    bool        debugChain = false;   // screenshot/test: show a chain hit
    bool        audioStress = false;  // dev: BGM + continuous overlapping SFX
    bool        autoplay = false;     // dev: let a bot drive the player side
    int         retries = 3;          // consecutive stage losses before GAME OVER
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

    enum class Phase { PLAY, CLEAR, DRAFT, OVER, BRIEF };

    void startStage(int stage, bool showBrief = true);
    void buildStage0(Game& game);
    void applyDraft(int option);
    void updateDraft(float dt, const hge::Input& in);
    int  draftHitTest(int mx, int my) const;
    int  draftOption(int slot) const;
    const char* draftName(int option) const;
    const char* draftDesc(int option) const;

    void updateAudio(float dt);

    // Dev autoplay: pick a shot by simulating candidate landings on a copy of
    // the player board, then drive the same input path a human would.
    void driveAutoplay(float dt, hge::Input& in);
    bool planAutoplayShot(float* tx, float* ty);
    int  autoplayDraftSlot() const;

    BubbleOptions m_opt;
    IGfx*         m_gfx = nullptr;
    RenderCtx     m_render;
    Side          m_sides[2];

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

    Phase m_phase = Phase::PLAY;
    int   m_stage = 0;
    int   m_mods = 0;
    int   m_bonusBombs = 0;
    int   m_retries = 3;
    float m_autoThink = 0.0f;   // autoplay: delay between shots
    bool  m_autoArmed = false;  // autoplay: aim was pushed, fire next frame
    float m_autoAimX = 0.0f;
    float m_autoAimY = 0.0f;
    int   m_draftSel = 0;
    int   m_draftRound = 0;
    bool  m_draftMouseMode = false;
    float m_phaseTimer = 0.0f;
    float m_draftCooldown = 0.0f;
    float m_bannerTimer = 0.0f;
    const char* m_banner = nullptr;
    const char* m_lastModName = nullptr;
    const char* m_lastModDesc = nullptr;
    bool  m_perfect = false;
    bool  m_bombHintShown = false;
    int   m_lastSpecialHint = 0;
    int   m_lastCorrupt = 0;
    float m_corruptFlash = 0.0f;

    // Short, cheap chain spectacle: rank + count + a brief slow-mo.
    float m_chainTimer = 0.0f;
    int   m_chainRank = 0;
    int   m_chainCount = 0;
    float m_shake = 0.0f;
    float m_shakeAmp = 0.0f;
    float m_slowMo = 0.0f;
    int   m_activeShotSpecial = 0;
    float m_audioStressTimer = 0.0f;

    bool m_ready = false;
};

} // namespace bbl

#endif // BUBBLE_APP_H
