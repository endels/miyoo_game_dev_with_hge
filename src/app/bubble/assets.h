#ifndef BBL_ASSETS_H
#define BBL_ASSETS_H

#include "hge_types.h"
#include <string>
#include <vector>

namespace bbl {

// One baked image. Pixels are 0xAARRGGBB, i.e. B,G,R,A in memory, matching
// what IGfx::CreateTexture expects.
struct Image {
    std::string name;
    int         w, h;
    std::vector<u32> px;
};

class Pack {
public:
    bool LoadFromMemory(const void* data, size_t size, const char* label);
    const Image* Find(const char* name) const;
    int Count() const { return (int)m_images.size(); }
    const char* LastError() const { return m_err.c_str(); }

private:
    std::vector<Image> m_images;
    std::string        m_err;
};

} // namespace bbl

#endif // BBL_ASSETS_H
