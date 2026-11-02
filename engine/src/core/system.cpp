#include "hge_system.h"
#include "platform/plat.h"

namespace hge {

const char* BasePath() { return plat::BasePath(); }
double      Time()     { return plat::Time(); }
void        SleepMs(int ms) { plat::SleepMs(ms); }

long long FileSize(const char* path) { return plat::FileSize(path); }

bool ReadFileAll(const char* path, std::vector<u8>& out)
{
    return plat::ReadFileAll(path, out);
}

bool ReadFileRange(const char* path, long long offset, size_t size,
                   std::vector<u8>& out)
{
    return plat::ReadFileRange(path, offset, size, out);
}

void MessageBox(const char* title, const char* text)
{
    plat::MessageBox(title, text);
}

} // namespace hge
