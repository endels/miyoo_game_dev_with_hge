#include "app/bubble/assets.h"

#include <cstdio>
#include <cstring>

namespace bbl {

namespace {

struct Cursor {
    const u8* p;
    size_t    left;
    bool ok;

    Cursor(const void* d, size_t n) : p((const u8*)d), left(n), ok(true) {}

    bool take(void* dst, size_t n)
    {
        if (!ok || left < n) { ok = false; return false; }
        memcpy(dst, p, n);
        p += n;
        left -= n;
        return true;
    }
};

} // namespace

bool Pack::LoadFromMemory(const void* data, size_t size, const char* label)
{
    m_images.clear();
    m_err.clear();

    Cursor c(data, size);
    char magic[4] = {0};
    u32 version = 0, count = 0;

    if (!c.take(magic, 4) || memcmp(magic, "HGEP", 4) != 0) {
        m_err = std::string("bad magic in ") + (label ? label : "?");
        return false;
    }
    if (!c.take(&version, 4) || version != 1) { m_err = "bad version"; return false; }
    if (!c.take(&count, 4) || count == 0 || count > 256) { m_err = "bad count"; return false; }

    struct Hdr { char name[24]; u32 w, h, bytes; };
    std::vector<Hdr> hdrs(count);
    for (u32 i = 0; i < count; ++i) {
        if (!c.take(&hdrs[i], sizeof(Hdr))) { m_err = "truncated header"; return false; }
        hdrs[i].name[23] = 0;
        if (hdrs[i].w == 0 || hdrs[i].h == 0 || hdrs[i].w > 8192 || hdrs[i].h > 8192 ||
            hdrs[i].bytes != hdrs[i].w * hdrs[i].h * 4) {
            m_err = "bad entry dimensions";
            return false;
        }
    }

    m_images.reserve(count);
    for (u32 i = 0; i < count; ++i) {
        Image img;
        img.name.assign(hdrs[i].name);
        img.w = (int)hdrs[i].w;
        img.h = (int)hdrs[i].h;
        img.px.resize((size_t)img.w * img.h);
        if (!c.take(&img.px[0], hdrs[i].bytes)) {
            m_err = std::string("truncated blob ") + img.name;
            m_images.clear();
            return false;
        }
        m_images.push_back(img);
    }
    return true;
}

const Image* Pack::Find(const char* name) const
{
    for (size_t i = 0; i < m_images.size(); ++i)
        if (m_images[i].name == name) return &m_images[i];
    return nullptr;
}

} // namespace bbl
