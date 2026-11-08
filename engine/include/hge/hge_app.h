#ifndef HGE_APP_H
#define HGE_APP_H

// Engine application loop. The game receives semantic input and draws through
// IGfx; the engine owns window setup, timing, frame pacing, screenshot and
// shutdown.

#include "hge/hge_config.h"
#include "hge_types.h"

class IGfx;

namespace hge {

struct Input {
    bool quit = false;
    bool resize = false;
    int  width = 0;
    int  height = 0;

    bool mouseMoved = false;
    int  mouseX = 0;
    int  mouseY = 0;

    // A left click is FIRE and a positioned click at the same time. Games that
    // put clickable widgets in the playfield need the position and need to know
    // where the click landed, so it can be swallowed there instead of doubling
    // as a shot.
    bool clickPressed = false;
    int  clickX = 0;
    int  clickY = 0;

    bool firePressed = false;
    bool restartPressed = false;
    bool aimLeft = false;
    bool aimRight = false;

    bool focusLost = false;
    bool focusGained = false;
};

struct RunConfig {
    int         width = 960;
    int         height = 540;
    const char* title = "HGE";
    bool        vsync = true;
    bool        resizable = true;

    int         maxFrames = 0;      // 0 = unlimited
    double      watchdog = 0.0;     // seconds, 0 = off
    const char* shotPath = nullptr; // non-null: write one PPM and exit
    int         shotFrame = 40;

    double      targetFps = 0.0;    // 0 = platform hint (see plat::ManualFrameCap)
    double      fixedDt = 0.0;      // >0: deterministic timestep (no wall clock)
    const char* replayPath = nullptr; // optional scripted input
};

class IGame {
public:
    virtual ~IGame() {}
    virtual bool Init(IGfx* gfx, int width, int height) = 0;
    virtual void Update(float dt, const Input& in) = 0;
    virtual void Render(IGfx* gfx, double time) = 0;
    virtual void OnResize(int width, int height) { (void)width; (void)height; }
    virtual void Shutdown() {}
};

int Run(IGame& game, const RunConfig& cfg);

} // namespace hge

#endif // HGE_APP_H
