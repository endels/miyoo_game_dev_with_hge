#include "app/bubble/bubble_app.h"

#include "hge_resource.h"
#include "platform/plat.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace bbl {

struct BubbleApp::AudioPack {
    std::vector< std::vector<i16> > data;
    std::vector<std::string>        names;
    std::vector<audio::Clip>        clips;

    bool Load(const std::vector<u8>& bytes)
    {
        if (bytes.size() < 12 || memcmp(&bytes[0], "HGEA", 4) != 0) return false;
        u32 ver = 0, count = 0;
        memcpy(&ver, &bytes[4], 4);
        memcpy(&count, &bytes[8], 4);
        if (ver != 1 || count == 0 || count > 64) return false;

        struct Hdr { char name[24]; u32 frames, ch, rate; };
        if (12 + sizeof(Hdr) * count > bytes.size()) return false;

        std::vector<Hdr> hs(count);
        size_t off = 12;
        for (u32 i = 0; i < count; ++i) {
            memcpy(&hs[i], &bytes[off], sizeof(Hdr));
            hs[i].name[23] = 0;
            off += sizeof(Hdr);
        }

        data.resize(count);
        names.resize(count);
        clips.resize(count);
        for (u32 i = 0; i < count; ++i) {
            if (hs[i].frames == 0 || hs[i].ch == 0 || hs[i].ch > 2) return false;
            const size_t n = (size_t)hs[i].frames * hs[i].ch;
            if (off + n * 2 > bytes.size()) return false;
            data[i].resize(n);
            memcpy(&data[i][0], &bytes[off], n * 2);
            off += n * 2;
            names[i].assign(hs[i].name);
            clips[i].pcm      = &data[i][0];
            clips[i].frames   = (int)hs[i].frames;
            clips[i].channels = (int)hs[i].ch;
            clips[i].rate     = (int)hs[i].rate;
        }
        return true;
    }

    const audio::Clip* Find(const char* n) const
    {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == n) return &clips[i];
        return nullptr;
    }
};

namespace {

bool loadEmbedded(const std::string& selfPath,
                  std::vector<u8>& embGame, std::vector<u8>& embAudio)
{
    std::string selfPaths[2];
    selfPaths[0] = selfPath;
    selfPaths[1] = std::string(plat::BasePath()) + "bubble_level1";

    for (int sp = 0; sp < 2; ++sp) {
        const char* self = selfPaths[sp].empty() ? nullptr : selfPaths[sp].c_str();
        if (!self) continue;
        const long long sz = plat::FileSize(self);
        if (sz <= 64) continue;

        std::vector<u8> tail;
        if (!plat::ReadFileRange(self, sz - 16, 16, tail) ||
            memcmp(&tail[0], "HGEEMB01", 8) != 0) {
            continue;
        }
        u32 gs = 0, as = 0;
        memcpy(&gs, &tail[8], 4);
        memcpy(&as, &tail[12], 4);
        const long long base = sz - 16 - (long long)gs - (long long)as;
        if (base > 0 &&
            plat::ReadFileRange(self, base, gs, embGame) &&
            plat::ReadFileRange(self, base + (long long)gs, as, embAudio))
            return true;
    }
    return false;
}

bool loadPack(Pack& pack, bool haveEmbedded,
              const std::vector<u8>& embGame)
{
    if (haveEmbedded)
        return pack.LoadFromMemory(&embGame[0], embGame.size(), "<exe>");

    std::string cands[3];
    cands[0] = std::string(plat::BasePath()) + "game.pak";
    cands[1] = "game.pak";
    cands[2] = "assets/game.pak";
    for (int i = 0; i < 3; ++i) {
        std::vector<u8> bytes;
        if (!plat::ReadFileAll(cands[i].c_str(), bytes)) continue;
        if (pack.LoadFromMemory(&bytes[0], bytes.size(), cands[i].c_str()))
            return true;
        fprintf(stderr, "[bubble] %s: %s\n", cands[i].c_str(), pack.LastError());
    }
    return false;
}

} // namespace

BubbleApp::BubbleApp(const BubbleOptions& opt)
    : m_opt(opt)
{
}

BubbleApp::~BubbleApp() = default;

bool BubbleApp::Init(IGfx* gfx, int /*width*/, int /*height*/)
{
    m_gfx = gfx;
    m_render.g = gfx;

    std::vector<u8> embGame, embAudio;
    const bool haveEmbedded = loadEmbedded(m_opt.selfPath, embGame, embAudio);

    Pack pack;
    const bool loaded = loadPack(pack, haveEmbedded, embGame);
    fprintf(stderr, "[t] game.pak=%s %.3fs\n", loaded ? "ok" : "FAIL", plat::Time());
    if (!loaded) {
        plat::MessageBox("Bubble Versus",
                         "game.pak not found.\n\n"
                         "Keep bubble_level1.exe, game.pak and audio.pak in the same folder.");
        Shutdown();
        return false;
    }

    std::string err;
    if (!LoadTextures(m_render, gfx, pack, &err)) {
        plat::MessageBox("Bubble Versus", err.c_str());
        Shutdown();
        return false;
    }
    fprintf(stderr, "[t] textures=%.3fs\n", plat::Time());

    // ---------------------------------------------------------- audio
    m_audioPack.reset(new AudioPack());
    if (haveEmbedded && m_audioPack->Load(embAudio))
        m_audioOn = audio::Init();
    if (!m_audioOn) {
        std::string apath[3];
        apath[0] = std::string(plat::BasePath()) + "audio.pak";
        apath[1] = "audio.pak";
        apath[2] = "assets/audio.pak";
        std::vector<u8> abytes;
        for (int i = 0; i < 3 && !m_audioOn; ++i) {
            if (!plat::ReadFileAll(apath[i].c_str(), abytes)) continue;
            if (!m_audioPack->Load(abytes)) continue;
            if (!audio::Init()) break;
            m_audioOn = true;
        }
    }
    fprintf(stderr, "[t] audio_init=%.3fs (on=%d)\n", plat::Time(), (int)m_audioOn);

    if (m_audioOn) {
        m_cShoot = m_audioPack->Find("shoot");
        m_cStick = m_audioPack->Find("stick");
        m_cPop   = m_audioPack->Find("pop");
        m_cDrop  = m_audioPack->Find("drop");
        m_cDesc  = m_audioPack->Find("descend");
        m_cWin   = m_audioPack->Find("win");
        m_cLose  = m_audioPack->Find("lose");
        const audio::Clip* bgm = m_audioPack->Find("bgm");
        if (bgm) audio::PlayMusic(*bgm, 0.40f);
    }
    printf("[bubble] %s\n", audio::Info());

    resetBattle(m_sides);
    m_prevState[0] = m_sides[0].game.GetState();
    m_prevState[1] = m_sides[1].game.GetState();

    // --warmup N: player fires N shots before the first frame (screenshots)
    for (int i = 0; i < m_opt.warmup; ++i) {
        if (m_sides[0].game.GetState() != Game::AIM) break;
        int r = -1, c = -1;
        for (int rr = 0; rr < m_sides[0].game.RowsUsed() && r < 0; ++rr) {
            const int n = m_sides[0].game.RowCount(rr);
            for (int cc = 0; cc < n; ++cc) {
                if (m_sides[0].game.Cell(rr, cc) == m_sides[0].game.CurrentColor()) {
                    r = rr; c = cc; break;
                }
            }
        }
        if (r < 0) break;
        m_sides[0].game.AimAt(m_sides[0].game.XOf(r, c) + ((i * 37) % 17) - 8.0f,
                              Game::YOf(r));
        m_sides[0].game.Fire();
        int guard = 0;
        while (m_sides[0].game.IsFlying() && guard++ < 4000)
            m_sides[0].game.Update(1.0f / 240.0f);
    }

    if (m_opt.simSeconds > 0.0f) {
        const float step = 1.0f / 60.0f;
        for (float t = 0.0f; t < m_opt.simSeconds; t += step) {
            updateBattle(m_sides, step);
            aiUpdate(m_sides[1], step);
        }
        printf("[bubble] sim %.1fs: p1 left=%d p2 left=%d p1 risers=%d p2 risers=%d\n",
               m_opt.simSeconds, m_sides[0].game.BubblesLeft(), m_sides[1].game.BubblesLeft(),
               (int)m_sides[0].game.Risers().size(), (int)m_sides[1].game.Risers().size());
    }

    m_ready = true;
    return true;
}

void BubbleApp::Update(float dt, const hge::Input& in)
{
    if (!m_ready) return;

    if (in.restartPressed) {
        resetBattle(m_sides);
        m_resultTimer = -1.0f;
    }

    const bool frozen = m_resultTimer > 0.0f;
    if (!frozen) {
        if (in.firePressed) m_sides[0].game.Fire();
        if (in.mouseMoved)
            m_sides[0].game.AimAt((float)in.mouseX, (float)in.mouseY);
    }
    if (m_resultTimer <= 0.0f) {
        const float aimRate = 1.6f; // rad/s (slower for finer aim control)
        if (in.aimLeft)  m_sides[0].game.AimBy(-aimRate * dt);
        if (in.aimRight) m_sides[0].game.AimBy(aimRate * dt);
    }

    const bool over = m_sides[0].game.GetState() == Game::LOSE ||
                      m_sides[1].game.GetState() == Game::LOSE;

    if (over) {
        if (m_resultTimer < 0.0f) m_resultTimer = 3.0f;
        else m_resultTimer -= dt;
        if (m_resultTimer <= 0.0f) {
            resetBattle(m_sides);
            m_resultTimer = -1.0f;
            m_lastMoves[0] = m_lastMoves[1] = 0;
            m_lastPopsAll = m_lastDropsAll = 0;
            m_prevState[0] = m_sides[0].game.GetState();
            m_prevState[1] = m_sides[1].game.GetState();
        }
    } else {
        m_resultTimer = -1.0f;
        updateBattle(m_sides, dt);
        aiUpdate(m_sides[1], dt);
    }

    updateAudio(dt);
}

void BubbleApp::updateAudio(float /*dt*/)
{
    if (!m_audioOn) return;

    const int mv0 = m_sides[0].game.Moves();
    const int pp0 = m_sides[0].game.Pops();
    const int dd0 = m_sides[0].game.Drops();
    const int de0 = m_sides[0].game.Descends();
    if (mv0 > m_lastMoves[0]) { if (m_cShoot) audio::Play(*m_cShoot, 0.38f); m_popsAtFire = pp0; }
    if (pp0 > m_lastPopsAll)  { if (m_cPop)   audio::Play(*m_cPop, 0.52f); }
    if (dd0 > m_lastDropsAll) { if (m_cDrop)  audio::Play(*m_cDrop, 0.48f); }
    if (de0 > m_lastMoves[1]) { if (m_cDesc)  audio::Play(*m_cDesc, 0.40f); }
    if (m_prevState[0] == Game::FLY && m_sides[0].game.GetState() != Game::FLY &&
        pp0 == m_popsAtFire) {
        if (m_cStick) audio::Play(*m_cStick, 0.30f);
    }
    m_lastMoves[0] = mv0;
    m_lastMoves[1] = de0;
    m_lastPopsAll = pp0;
    m_lastDropsAll = dd0;

    const Game::State st0 = m_sides[0].game.GetState();
    const Game::State st1 = m_sides[1].game.GetState();
    if (st0 == Game::LOSE && m_prevState[0] != Game::LOSE) { if (m_cLose) audio::Play(*m_cLose, 0.55f); }
    if (st1 == Game::LOSE && m_prevState[1] != Game::LOSE) { if (m_cWin)  audio::Play(*m_cWin, 0.70f); }
    m_prevState[0] = st0;
    m_prevState[1] = st1;
}

void BubbleApp::Render(IGfx* /*gfx*/, double time)
{
    if (!m_ready) return;
    RenderFrame(m_render, m_sides, time + m_opt.animOffset, m_resultTimer);
}

void BubbleApp::OnResize(int width, int height)
{
    if (m_gfx) m_gfx->OnResize(width, height);
}

void BubbleApp::Shutdown()
{
    if (m_audioOn) {
        audio::Shutdown();
        m_audioOn = false;
    }
    if (m_render.g)
        FreeTextures(m_render);
    m_audioPack.reset();
    m_gfx = nullptr;
    m_ready = false;
}

} // namespace bbl
