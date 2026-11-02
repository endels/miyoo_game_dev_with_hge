// Boundary probe: a minimal game that only includes the public engine headers.
// If this stops compiling, the engine/game include boundary has leaked.

#include "hge_app.h"
#include "hge_gfx.h"

#include <cstdlib>
#include <cstring>

namespace {

class HelloGame : public hge::IGame {
public:
    bool Init(IGfx* /*gfx*/, int /*width*/, int /*height*/) override { return true; }

    void Update(float /*dt*/, const hge::Input& /*in*/) override {}

    void Render(IGfx* gfx, double /*time*/) override
    {
        gfx->BeginFrame();
        gfx->Clear(0xFF101020);

        GfxVertex v[4];
        for (int i = 0; i < 4; ++i) {
            v[i].z = 0.0f;
            v[i].color = 0xFF40C0FFu;
        }
        v[0].x = 40;  v[0].y = 40;  v[0].u = 0; v[0].v = 0;
        v[1].x = 280; v[1].y = 40;  v[1].u = 1; v[1].v = 0;
        v[2].x = 40;  v[2].y = 200; v[2].u = 0; v[2].v = 1;
        v[3].x = 280; v[3].y = 200; v[3].u = 1; v[3].v = 1;
        gfx->DrawQuads(nullptr, v, 1);

        gfx->EndFrame();
    }
};

} // namespace

int main(int argc, char** argv)
{
    hge::RunConfig cfg;
    cfg.width = 320;
    cfg.height = 240;
    cfg.title = "Hello HGE";

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            cfg.maxFrames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc)
            cfg.shotPath = argv[++i];
    }

    if (cfg.shotPath) {
        if (cfg.maxFrames <= 0) cfg.maxFrames = 1;
        cfg.shotFrame = cfg.maxFrames;
    }

    HelloGame game;
    return hge::Run(game, cfg);
}
