// Engine application loop: turn platform events into semantic input, call the
// game's Update/Render, present, pace, and tear everything down.

#include "hge_app.h"
#include "hge_gfx.h"
#include "platform/plat.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hge {
namespace {

bool writePPM(const char* path, const u32* px, int w, int h)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<u8> row((size_t)w * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u32 c = px[(size_t)y * w + x];
            row[x * 3 + 0] = (u8)((c >> 16) & 0xFF);
            row[x * 3 + 1] = (u8)((c >> 8) & 0xFF);
            row[x * 3 + 2] = (u8)(c & 0xFF);
        }
        fwrite(&row[0], 1, (size_t)w * 3, f);
    }
    fclose(f);
    return true;
}

Input pollInput()
{
    Input in;
    in.aimLeft  = plat::KeyDown(HGEK_LEFT);
    in.aimRight = plat::KeyDown(HGEK_RIGHT);

    plat::Event ev;
    while (plat::Poll(ev)) {
        switch (ev.type) {
        case plat::EV_QUIT:
            in.quit = true;
            break;
        case plat::EV_KEYDOWN:
            if (ev.key == HGEK_ESCAPE)      in.quit = true;
            else if (ev.key == HGEK_R)      in.restartPressed = true;
            else if (ev.key == HGEK_SPACE)  in.firePressed = true;
            break;
        case plat::EV_MOUSEMOVE:
            in.mouseMoved = true;
            in.mouseX = ev.x;
            in.mouseY = ev.y;
            break;
        case plat::EV_MOUSEDOWN:
            if (ev.button == 0)      in.firePressed = true;
            else if (ev.button == 2) in.restartPressed = true;
            break;
        case plat::EV_FOCUS_LOST:
            in.focusLost = true;
            break;
        case plat::EV_FOCUS_GAINED:
            in.focusGained = true;
            break;
        case plat::EV_RESIZE:
            in.resize = true;
            in.width = plat::Width();
            in.height = plat::Height();
            break;
        default:
            break;
        }
    }
    return in;
}

struct ReplayAction {
    int frame = 0;
    enum Type { AIM, FIRE, RESTART, QUIT, LEFT_ON, LEFT_OFF, RIGHT_ON, RIGHT_OFF } type = FIRE;
    int x = 0;
    int y = 0;
};

bool loadReplay(const char* path, std::vector<ReplayAction>& out, std::string& err)
{
    std::ifstream f(path);
    if (!f) {
        err = std::string("cannot open replay: ") + path;
        return false;
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);

        std::istringstream ss(line);
        int frame = 0;
        std::string what;
        if (!(ss >> frame)) continue;
        if (!(ss >> what)) continue;
        if (frame < 0) {
            err = "replay line " + std::to_string(lineNo) + ": negative frame";
            return false;
        }

        ReplayAction a;
        a.frame = frame;
        if (what == "aim") {
            a.type = ReplayAction::AIM;
            if (!(ss >> a.x >> a.y)) {
                err = "replay line " + std::to_string(lineNo) + ": aim needs x y";
                return false;
            }
        } else if (what == "fire") {
            a.type = ReplayAction::FIRE;
        } else if (what == "restart") {
            a.type = ReplayAction::RESTART;
        } else if (what == "quit") {
            a.type = ReplayAction::QUIT;
        } else if (what == "left") {
            a.type = ReplayAction::LEFT_ON;
        } else if (what == "noleft") {
            a.type = ReplayAction::LEFT_OFF;
        } else if (what == "right") {
            a.type = ReplayAction::RIGHT_ON;
        } else if (what == "noright") {
            a.type = ReplayAction::RIGHT_OFF;
        } else {
            err = "replay line " + std::to_string(lineNo) + ": unknown action " + what;
            return false;
        }
        out.push_back(a);
    }
    return true;
}

} // namespace

int Run(IGame& game, const RunConfig& cfg)
{
    if (!plat::Init(cfg.width, cfg.height, cfg.title, cfg.vsync, cfg.resizable)) {
        plat::MessageBox(cfg.title, "Could not create the window (SDL init failed).");
        return 2;
    }

    IGfx* gfx = CreateGfx();
    if (!gfx || !gfx->Init(plat::Window(), plat::GLGetProc)) {
        plat::MessageBox(cfg.title,
                         "Could not create the renderer.\n"
                         "A GPU driver with OpenGL 2.1 / OpenGL ES 2.0 is required.");
        plat::Shutdown();
        delete gfx;
        return 2;
    }
    fprintf(stderr, "[t] gfx_init=%.3fs\n", plat::Time());
    gfx->OnResize(plat::Width(), plat::Height());
    printf("[hge] renderer: %s\n", gfx->Info());

    if (!game.Init(gfx, plat::Width(), plat::Height())) {
        gfx->Shutdown();
        delete gfx;
        plat::Shutdown();
        return 2;
    }

    std::vector<ReplayAction> replay;
    if (cfg.replayPath) {
        std::string err;
        if (!loadReplay(cfg.replayPath, replay, err)) {
            fprintf(stderr, "[hge] %s\n", err.c_str());
            gfx->Shutdown();
            delete gfx;
            plat::Shutdown();
            return 2;
        }
    }
    size_t replayIdx = 0;
    bool   heldLeft = false;
    bool   heldRight = false;

    int iw = plat::Width(), ih = plat::Height();
    fprintf(stderr, "[t] enter_loop=%.3fs size=%dx%d\n", plat::Time(), iw, ih);

    const double target = cfg.targetFps > 0.0
                        ? cfg.targetFps
                        : (plat::ManualFrameCap() ? 60.0 : 0.0);
    const double targetDt = target > 0.0 ? 1.0 / target : 0.0;

    double last = plat::Time();
    double simTime = 0.0;
    int    frame = 0;
    bool   quit = false;

    while (!quit) {
        Input in = pollInput();

        while (replayIdx < replay.size() && replay[replayIdx].frame <= frame) {
            const ReplayAction& a = replay[replayIdx++];
            switch (a.type) {
            case ReplayAction::AIM:       in.mouseMoved = true; in.mouseX = a.x; in.mouseY = a.y; break;
            case ReplayAction::FIRE:      in.firePressed = true; break;
            case ReplayAction::RESTART:   in.restartPressed = true; break;
            case ReplayAction::QUIT:      in.quit = true; break;
            case ReplayAction::LEFT_ON:   heldLeft = true; break;
            case ReplayAction::LEFT_OFF:  heldLeft = false; break;
            case ReplayAction::RIGHT_ON:  heldRight = true; break;
            case ReplayAction::RIGHT_OFF: heldRight = false; break;
            }
        }
        if (heldLeft)  in.aimLeft = true;
        if (heldRight) in.aimRight = true;

        if (iw != plat::Width() || ih != plat::Height()) {
            iw = plat::Width();
            ih = plat::Height();
            in.resize = true;
            in.width = iw;
            in.height = ih;
        }
        if (in.resize)
            game.OnResize(iw, ih);

        const double nowWall = plat::Time();
        float dt = cfg.fixedDt > 0.0
                 ? (float)cfg.fixedDt
                 : (float)(nowWall - last);
        last = nowWall;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.05f) dt = 0.05f;

        const double renderTime = cfg.fixedDt > 0.0 ? simTime : nowWall;

        game.Update(dt, in);
        game.Render(gfx, renderTime);

        if (cfg.shotPath && frame + 1 >= cfg.shotFrame) {
            std::vector<u32> px((size_t)iw * ih);
            if (gfx->ReadPixels(0, 0, iw, ih, &px[0]))
                if (writePPM(cfg.shotPath, &px[0], iw, ih))
                    printf("[hge] wrote %s (%dx%d)\n", cfg.shotPath, iw, ih);
            quit = true;
        }

        plat::SwapBuffers();

        if (targetDt > 0.0 && cfg.fixedDt <= 0.0) {
            const double spent = plat::Time() - nowWall;
            if (spent < targetDt)
                plat::SleepMs((int)((targetDt - spent) * 1000.0));
        }

        if (cfg.fixedDt > 0.0) simTime += cfg.fixedDt;

        ++frame;
        if (in.quit) quit = true;
        if (cfg.maxFrames > 0 && frame >= cfg.maxFrames) quit = true;
        if (cfg.watchdog > 0.0 && nowWall > cfg.watchdog) {
            fprintf(stderr, "[watchdog] auto-exit\n");
            quit = true;
        }
    }

    game.Shutdown();
    gfx->Shutdown();
    delete gfx;
    plat::Shutdown();
    return 0;
}

} // namespace hge
