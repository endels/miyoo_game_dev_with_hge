// Minimal platform stub for host-side gfx backend tests. gfx_blit only needs
// the logical draw size; it does not touch the window or input.

#include "platform/plat.h"

#include <ctime>

namespace plat {

int Width()  { return 960; }
int Height() { return 540; }

// gfx_blit times its own flush for the HGE_BLIT_LOG diagnostics, so the stub
// needs a clock too.
double Time()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

} // namespace plat
