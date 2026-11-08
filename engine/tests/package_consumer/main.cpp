// External consumer of the installed engine package.
//
// If this compiles and links, the package is self-describing: public headers,
// HGE_* compile definitions, the generated hge_config.h and the transitive
// link dependencies (SDL2 + Threads) all came through find_package(hge).

#include "hge_app.h"
#include "hge_system.h"

#include <cstdio>

namespace {

class ConsumerGame : public hge::IGame {
public:
    bool Init(IGfx*, int, int) override { return true; }
    void Update(float, const hge::Input&) override {}
    void Render(IGfx*, double) override {}
    void Shutdown() override {}
};

}  // namespace

int main()
{
    ConsumerGame game;
    hge::IGame* base = &game;
    std::printf("[consumer] find_package(hge) OK, BasePath=%s\n", hge::BasePath());
    return base ? 0 : 1;
}
