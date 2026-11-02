// Minimal platform stub for host-side gfx backend tests. gfx_blit only needs
// the logical draw size; it does not touch the window or input.

#include "platform/plat.h"

namespace plat {

int Width()  { return 960; }
int Height() { return 540; }

} // namespace plat
