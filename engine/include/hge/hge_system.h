#ifndef HGE_SYSTEM_H
#define HGE_SYSTEM_H

// Public system services: time, base path, file reads and message boxes.
// Backed by the selected platform layer; games never include platform/plat.h.

#include "hge_types.h"
#include <vector>

namespace hge {

const char* BasePath();       // directory of the executable, trailing separator
double      Time();           // seconds since platform init
void        SleepMs(int ms);

long long FileSize(const char* path);
bool ReadFileAll(const char* path, std::vector<u8>& out);
bool ReadFileRange(const char* path, long long offset, size_t size,
                   std::vector<u8>& out);

void MessageBox(const char* title, const char* text);

} // namespace hge

#endif // HGE_SYSTEM_H
