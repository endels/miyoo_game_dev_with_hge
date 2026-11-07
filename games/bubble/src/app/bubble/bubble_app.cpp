#include "app/bubble/bubble_app.h"

#include "hge_resource.h"
#include "hge_system.h"

#include <cmath>
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
    selfPaths[1] = std::string(hge::BasePath()) + "bubble_level1";

    for (int sp = 0; sp < 2; ++sp) {
        const char* self = selfPaths[sp].empty() ? nullptr : selfPaths[sp].c_str();
        if (!self) continue;
        const long long sz = hge::FileSize(self);
        if (sz <= 64) continue;

        std::vector<u8> tail;
        if (!hge::ReadFileRange(self, sz - 16, 16, tail) ||
            memcmp(&tail[0], "HGEEMB01", 8) != 0) {
            continue;
        }
        u32 gs = 0, as = 0;
        memcpy(&gs, &tail[8], 4);
        memcpy(&as, &tail[12], 4);
        const long long base = sz - 16 - (long long)gs - (long long)as;
        if (base > 0 &&
            hge::ReadFileRange(self, base, gs, embGame) &&
            hge::ReadFileRange(self, base + (long long)gs, as, embAudio))
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
    cands[0] = std::string(hge::BasePath()) + "game.pak";
    cands[1] = "game.pak";
    cands[2] = "assets/game.pak";
    for (int i = 0; i < 3; ++i) {
        std::vector<u8> bytes;
        if (!hge::ReadFileAll(cands[i].c_str(), bytes)) continue;
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

void BubbleApp::buildStage0(Game& game)
{
    // Hand-authored first stage. Rows 0-2 are full except for a notch around
    // the bottom corrupt bubble; that leaves it hanging from one anchor so the
    // player can learn that dropping is a valid solution.
    std::vector<StageCell> cells;
    cells.reserve(64);

    auto add = [&cells](int r, int c, int color, bool corrupt) {
        StageCell s;
        s.r = r;
        s.c = c;
        s.color = color;
        s.corrupt = corrupt;
        cells.push_back(s);
    };

    for (int r = 0; r < 4; ++r) {
        const int n = game.RowCount(r);
        for (int c = 0; c < n; ++c) {
            if (r == 2 && c == 5) continue;
            if (r == 3 && (c == 3 || c == 5)) continue;
            add(r, c, 1 + ((r * 2 + c) % 4), false);
        }
    }

    // C1: top-left, a direct match-3 lesson.
    add(1, 1, 2, false);
    add(0, 2, 2, false);
    add(1, 2, 2, true);

    // C2: mid-right, tucked into a cluster.
    add(2, 6, 3, false);
    add(2, 8, 3, false);
    add(2, 7, 3, true);

    // C3: bottom centre, hanging from one anchor. Pop the anchor's colour-1
    // group to drop it.
    add(2, 3, 1, false);
    add(1, 4, 1, false);
    add(2, 4, 1, false);
    add(3, 4, 4, true);

    game.LoadStage(4, 0, cells.data(), (int)cells.size());
}

void BubbleApp::startStage(int stage, bool showBrief)
{
    // The win jingle is long; do not let it bleed into the next stage or
    // stack voices when the player drafts quickly.
    audio::StopEffects();

    m_stage = stage;
    fprintf(stderr, "[game] t=%.2f start stage=%d mods=%d brief=%d\n",
            hge::Time(), stage, m_mods, showBrief ? 1 : 0);
    m_phase = showBrief ? Phase::BRIEF : Phase::PLAY;
    m_phaseTimer = 0.0f;
    m_draftSel = 0;
    m_draftCooldown = 0.0f;
    m_draftMouseMode = false;
    m_perfect = false;
    m_banner = nullptr;
    m_bannerTimer = 0.0f;
    m_bombHintShown = false;
    m_lastSpecialHint = 0;
    m_lastCorrupt = 0;
    m_corruptFlash = 0.0f;
    m_chainTimer = 0.0f;
    m_chainRank = 0;
    m_chainCount = 0;
    m_shake = 0.0f;
    m_shakeAmp = 0.0f;
    m_slowMo = 0.0f;
    m_activeShotSpecial = 0;

    resetBattle(m_sides);

    // Stage 1 is a sparring match; later stages tighten the AI and start
    // sending garbage much sooner, so draft effects have something to bite.
    m_sidesPtr[1].sparring = true;
    m_sidesPtr[1].fireScale = (stage == 0) ? 1.0f : 0.65f;
    m_sidesPtr[1].clearStun = (stage == 0) ? 3.0f : 1.2f;
    m_sidesPtr[1].energyGain = (stage == 0) ? 1.0f : 2.0f;
    m_sidesPtr[1].garbageAfterMoves = (stage == 0) ? 20 : 2;
    m_sidesPtr[1].aiTimer = (stage == 0) ? 1.2f : 0.8f;
    m_sidesPtr[0].keepWin = false;

    // Stage tuning: drops are the high-skill play, so they charge faster and
    // make the base punishment wave 3 bubbles instead of 2.
    m_sidesPtr[0].stageRules = m_sidesPtr[1].stageRules = true;
    m_sidesPtr[0].dropEnergy = m_sidesPtr[1].dropEnergy = 2.0f;
    m_sidesPtr[0].waveSize = m_sidesPtr[1].waveSize = 3;
    m_sidesPtr[0].attackBonus = m_sidesPtr[1].attackBonus = 0;

    m_sidesPtr[0].game.Reset(0x1234u + (u32)stage * 777u);
    buildStage0(m_sidesPtr[0].game);
    m_sidesPtr[0].game.SetBombAtMove(2);   // third shot is a bomb
    if (stage == 0) {
        m_sidesPtr[0].game.SetQueue(2, 1);          // match C1, then drop C3
        m_sidesPtr[0].game.SetPrismAtMove(3);       // fourth shot teaches prism
        m_sidesPtr[0].game.SetChameleonAtMove(5);   // sixth shot teaches chameleon
    }

    if (m_mods & 1)  m_sidesPtr[0].game.GiveBombNow();
    if (m_mods & 2)  m_sidesPtr[0].energyGain = 1.5f;
    if (m_mods & 4)  m_sidesPtr[0].game.SetRiserSpeed(0.85f);
    if (m_mods & 8)  m_sidesPtr[0].game.SetBombRadius(2.1f);
    if (m_mods & 16) m_sidesPtr[0].game.GiveBombNow();   // DOUBLE BOMB
    if (m_mods & 32) m_sidesPtr[0].game.SetBombEvery(6); // BOMB CYCLE
    if (m_mods & 64) m_sidesPtr[0].chainPay = true;      // CHAIN PAYOUT
    if (m_mods & 128) m_sidesPtr[0].game.SetPrismEvery(5);
    if (m_mods & 256) m_sidesPtr[0].game.SetChameleonEvery(7);
    if (m_bonusBombs > 0 && m_sidesPtr[0].game.CurrentSpecial() != kSpecialBomb) {
        m_sidesPtr[0].game.GiveBombNow();
        --m_bonusBombs;
    }

    m_retries = m_opt.retries > 0 ? m_opt.retries : 0;

    if (m_opt.duel) {
        // Duel v2: no stage chain, no draft, no briefing. DuelMatch owns both
        // boards and every rule; the stage scaffolding above is unused here.
        m_duel = true;
        m_sidesPtr = m_match.Sides();
        m_match.Reset((u32)m_duelSeed);
        m_duelResult = 0;
        m_duelAtk[0] = m_duelAtk[1] = 0;
        m_duelAtkCol[0] = m_duelAtkCol[1] = -1;
        m_duelAtkTimer = 0.0f;
        m_duelShotCd = 0.0f;
        m_banner = nullptr;
        m_bannerTimer = 0.0f;
        m_phase = Phase::PLAY;
    } else {
        m_duel = false;
        m_sidesPtr = m_sides;
    }

    m_lastMoves[0] = m_lastMoves[1] = 0;
    m_lastPopsAll = 0;
    m_lastDropsAll = 0;
    m_popsAtFire = 0;
    m_prevState[0] = m_sidesPtr[0].game.GetState();
    m_prevState[1] = m_sidesPtr[1].game.GetState();

    if (!showBrief) {
        m_banner = "GO!";
        m_bannerTimer = 1.0f;
    }
}

int BubbleApp::draftOption(int slot) const
{
    static const int pools[3][3] = {
        { 0, 1, 2 },   // first draft: one per archetype (bomb / chain / control)
        { 3, 4, 5 },   // second: big blast / prism / chameleon
        { 6, 7, 8 },   // later: bomb cycle / double bomb / energy
    };
    if (slot < 0) slot = 0;
    if (slot > 2) slot = 2;
    return pools[m_draftRound % 3][slot];
}

const char* BubbleApp::draftName(int option) const
{
    switch (option) {
    case 0: return "BOMB START";
    case 1: return "CHAIN PAYOUT";
    case 2: return "SLOW RISER";
    case 3: return "BIG BLAST";
    case 4: return "PRISM CYCLE";
    case 5: return "CHAMELEON";
    case 6: return "BOMB CYCLE";
    case 7: return "DOUBLE BOMB";
    case 8: return "ENERGY UP";
    default: return "?";
    }
}

const char* BubbleApp::draftDesc(int option) const
{
    switch (option) {
    case 0: return "1 BOMB AT START";
    case 1: return "5+ = +1 GARBAGE";
    case 2: return "RISER -15%";
    case 3: return "BOMB RADIUS +40%";
    case 4: return "EVERY 5TH PRISM";
    case 5: return "EVERY 7TH PAINT";
    case 6: return "EVERY 6TH BOMB";
    case 7: return "2 BOMBS THIS STAGE";
    case 8: return "ENERGY +50%";
    default: return "";
    }
}

void BubbleApp::applyDraft(int option)
{
    switch (option) {
    case 0: m_mods |= 1; break;      // BOMB START
    case 1: m_mods |= 64; break;     // CHAIN PAYOUT
    case 2: m_mods |= 4; break;      // SLOW RISER
    case 3: m_mods |= 8; break;      // BIG BLAST
    case 4: m_mods |= 128; break;    // PRISM CYCLE
    case 5: m_mods |= 256; break;    // CHAMELEON CYCLE
    case 6: m_mods |= 32; break;     // BOMB CYCLE
    case 7: m_mods |= 16; break;     // DOUBLE BOMB
    case 8: m_mods |= 2; break;      // ENERGY UP
    default: break;
    }
}

int BubbleApp::draftHitTest(int mx, int my) const
{
    // Keep these card bounds in sync with the DRAW block in render.cpp.
    const float cardY = 150.0f, cardH = 250.0f, cardW = 220.0f, gap = 250.0f;
    if ((float)my < cardY || (float)my > cardY + cardH) return -1;
    for (int i = 0; i < 3; ++i) {
        const float x = 110.0f + (float)i * gap;
        if ((float)mx >= x && (float)mx <= x + cardW) return i;
    }
    return -1;
}

void BubbleApp::updateDraft(float /*dt*/, const hge::Input& in)
{
    if (in.aimLeft || in.aimRight)
        m_draftMouseMode = false;

    if (m_draftCooldown <= 0.0f) {
        if (in.aimLeft) {
            m_draftSel = (m_draftSel + 2) % 3;
            m_draftCooldown = 0.18f;
        } else if (in.aimRight) {
            m_draftSel = (m_draftSel + 1) % 3;
            m_draftCooldown = 0.18f;
        }
    }

    // Hover follows the mouse; keyboard navigation keeps control when the
    // mouse is still, so both input paths feel predictable.
    if (in.mouseMoved) {
        const int hit = draftHitTest(in.mouseX, in.mouseY);
        if (hit >= 0) {
            m_draftSel = hit;
            m_draftMouseMode = true;
        }
    }

    if (in.firePressed) {
        if (m_draftMouseMode) {
            const int hit = draftHitTest(in.mouseX, in.mouseY);
            if (hit >= 0) m_draftSel = hit;
        }
        const int opt = draftOption(m_draftSel);
        fprintf(stderr, "[game] t=%.2f pick=%s\n", hge::Time(), draftName(opt));
        applyDraft(opt);
        ++m_draftRound;
        m_lastModName = draftName(opt);
        m_lastModDesc = draftDesc(opt);
        startStage(m_stage + 1, true);
    }
}

bool BubbleApp::Init(IGfx* gfx, int /*width*/, int /*height*/)
{
    m_gfx = gfx;
    m_render.g = gfx;

    std::vector<u8> embGame, embAudio;
    const bool haveEmbedded = loadEmbedded(m_opt.selfPath, embGame, embAudio);

    Pack pack;
    const bool loaded = loadPack(pack, haveEmbedded, embGame);
    fprintf(stderr, "[t] game.pak=%s %.3fs\n", loaded ? "ok" : "FAIL", hge::Time());
    if (!loaded) {
        hge::MessageBox("Bubble Versus",
                         "game.pak not found.\n\n"
                         "Keep bubble_level1.exe, game.pak and audio.pak in the same folder.");
        Shutdown();
        return false;
    }

    std::string err;
    if (!LoadTextures(m_render, gfx, pack, &err)) {
        hge::MessageBox("Bubble Versus", err.c_str());
        Shutdown();
        return false;
    }
    fprintf(stderr, "[t] textures=%.3fs\n", hge::Time());

    // ---------------------------------------------------------- audio
    m_audioPack.reset(new AudioPack());
    if (haveEmbedded && m_audioPack->Load(embAudio))
        m_audioOn = audio::Init();
    if (!m_audioOn) {
        std::string apath[3];
        apath[0] = std::string(hge::BasePath()) + "audio.pak";
        apath[1] = "audio.pak";
        apath[2] = "assets/audio.pak";
        std::vector<u8> abytes;
        for (int i = 0; i < 3 && !m_audioOn; ++i) {
            if (!hge::ReadFileAll(apath[i].c_str(), abytes)) continue;
            if (!m_audioPack->Load(abytes)) continue;
            if (!audio::Init()) break;
            m_audioOn = true;
        }
    }
    fprintf(stderr, "[t] audio_init=%.3fs (on=%d)\n", hge::Time(), (int)m_audioOn);

    if (m_audioOn) {
        m_cShoot = m_audioPack->Find("shoot");
        m_cStick = m_audioPack->Find("stick");
        m_cPop   = m_audioPack->Find("pop");
        m_cDrop  = m_audioPack->Find("drop");
        m_cDesc  = m_audioPack->Find("descend");
        m_cWin   = m_audioPack->Find("win");
        m_cLose  = m_audioPack->Find("lose");
        const audio::Clip* bgm = m_audioPack->Find("bgm");
        if (bgm) audio::PlayMusic(*bgm, 0.55f);
    }
    printf("[bubble] %s\n", audio::Info());

    startStage(0);
    if (m_opt.debugClear) {
        m_sidesPtr[1].game.DebugLose();
        m_phase = Phase::PLAY;
    }
    if (m_opt.debugDraft) {
        m_phase = Phase::DRAFT;
        m_draftSel = 0;
    }
    if (m_opt.debugChain) {
        m_phase = Phase::PLAY;
        m_chainTimer = 2.0f;
        m_chainRank = 1;
        m_chainCount = 5;
        m_shake = 0.0f;
        m_shakeAmp = 0.0f;
    }

    // --warmup N: player fires N shots before the first frame (screenshots)
    if (m_duel) {
        // Duel: warmup is a headless match run, so a screenshot can show a
        // mid-match HUD. The rules still own every shot.
        const float step = 1.0f / 60.0f;
        for (int i = 0; i < m_opt.warmup && m_phase == Phase::PLAY; ++i)
            m_match.Tick(step, true);
    } else
    for (int i = 0; i < m_opt.warmup; ++i) {
        if (m_sidesPtr[0].game.GetState() != Game::AIM) break;
        int r = -1, c = -1;
        for (int rr = 0; rr < m_sidesPtr[0].game.RowsUsed() && r < 0; ++rr) {
            const int n = m_sidesPtr[0].game.RowCount(rr);
            for (int cc = 0; cc < n; ++cc) {
                if (m_sidesPtr[0].game.Cell(rr, cc) == m_sidesPtr[0].game.CurrentColor()) {
                    r = rr; c = cc; break;
                }
            }
        }
        if (r < 0) break;
        m_sidesPtr[0].game.AimAt(m_sidesPtr[0].game.XOf(r, c) + ((i * 37) % 17) - 8.0f,
                              Game::YOf(r));
        m_sidesPtr[0].game.Fire();
        int guard = 0;
        while (m_sidesPtr[0].game.IsFlying() && guard++ < 4000)
            m_sidesPtr[0].game.Update(1.0f / 240.0f);
    }

    // Screenshot/warmup runs expect to see the live board, not the briefing.
    if (m_opt.warmup > 0 && !m_opt.debugDraft)
        m_phase = Phase::PLAY;

    if (m_opt.simSeconds > 0.0f) {
        const float step = 1.0f / 60.0f;
        for (float t = 0.0f; t < m_opt.simSeconds; t += step) {
            updateBattle(m_sides, step);
            aiUpdate(m_sidesPtr[1], step);
        }
        printf("[bubble] sim %.1fs: p1 left=%d p2 left=%d p1 risers=%d p2 risers=%d\n",
               m_opt.simSeconds, m_sidesPtr[0].game.BubblesLeft(), m_sidesPtr[1].game.BubblesLeft(),
               (int)m_sidesPtr[0].game.Risers().size(), (int)m_sidesPtr[1].game.Risers().size());
    }

    m_ready = true;
    if (m_opt.autoplay)
        fprintf(stderr, "[game] autoplay=1 retries=%d\n", m_retries);
    return true;
}

namespace {

// Topmost row that still holds a bubble (-1 when the board is empty).
bool lineClear(const Game& g, float x0, float y0, float x1, float y1)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float dist = sqrtf(dx * dx + dy * dy);
    const int steps = (int)(dist / 10.0f);
    for (int i = 3; i < steps - 2; ++i) {
        const float u = (float)i / (float)steps;
        if (g.SolidAt(x0 + dx * u, y0 + dy * u)) return false;
    }
    return true;
}

int topOccupiedRow(const Game& g)
{
    for (int r = 0; r < kMaxRows; ++r) {
        const int n = g.RowCount(r);
        for (int c = 0; c < n; ++c)
            if (g.Cell(r, c)) return r;
    }
    return -1;
}

} // namespace

// Pick the best available shot by replaying candidates on a throw-away copy of
// the board. Unlike the CPU's greedy "most same-coloured neighbours" heuristic,
// this sees the full result of the shot: matches, chain drops, corruption
// bounties, bombs, prism and chameleon payloads, and losing the board.
bool BubbleApp::planAutoplayShot(float* tx, float* ty)
{
    Game& g = m_sidesPtr[0].game;
    if (g.GetState() != Game::AIM) return false;

    const float sx = g.ShooterX();
    const float sy = kShooterY;
    const float wallL = g.X0() + kR;
    const float wallR = g.X0() + g.FieldW() - kR;
    float bestX = 0.0f, bestY = 0.0f;
    int   bestScore = -1000000000;
    int   tried = 0;

    for (int r = 0; r <= g.RowsUsed() && r < kMaxRows; ++r) {
        const int n = g.RowCount(r);
        for (int c = 0; c < n; ++c) {
            if (g.Cell(r, c)) continue;
            if (!g.Attachable(r, c)) continue;

            const float cx = g.XOf(r, c);
            const float cy = Game::YOf(r);

            // Aim points for this cell: the straight line first, plus a wall
            // mirror (a one-bounce shot) when the straight line is blocked.
            float aims[3][2];
            int   nAim = 0;

            if (lineClear(g, sx, sy, cx, cy)) {
                aims[nAim][0] = cx; aims[nAim][1] = cy; ++nAim;
            } else if ((g.Moves() % 4) == 0) {
                // Only try wall shots now and then, so the bot keeps a
                // human-ish mix instead of always going for the trick.
                for (int w = 0; w < 2; ++w) {
                    const float wall = w ? wallR : wallL;
                    const float mx = 2.0f * wall - cx;
                    const float t = (wall - sx) / (mx - sx);
                    if (!(t > 0.02f && t < 0.98f)) continue;
                    const float by = sy + t * (cy - sy);
                    if (by < kCeilY + kR) continue;
                    if (!lineClear(g, sx, sy, wall, by)) continue;
                    if (!lineClear(g, wall, by, cx, cy)) continue;
                    aims[nAim][0] = mx; aims[nAim][1] = cy; ++nAim;
                }
                if (nAim == 0) continue;
            } else {
                continue;   // buried cell, no bounce allowed this time
            }

            for (int a = 0; a < nAim; ++a) {
                Game sim = g;      // value copy: same grid, same RNG cursor
                sim.AimAt(aims[a][0], aims[a][1]);
                sim.Fire();
                int guard = 0;
                while (sim.GetState() == Game::FLY && guard++ < 600)
                    sim.Update(1.0f / 120.0f);

                int score;
                if (sim.GetState() == Game::LOSE) {
                    score = -1000000;   // never trade a loss for a clear
                } else {
                    const int cleared = (sim.Pops() - g.Pops()) + (sim.Drops() - g.Drops());
                    const int corrupt = sim.CorruptPopped() - g.CorruptPopped();
                    const int top     = topOccupiedRow(sim);
                    score = cleared * 100 + corrupt * 500 - top * 4;
                // duel setups: landing next to an attack bubble of our colour
                score += 60 * g.CorruptNeighbors(r, c, g.CurrentColor());
                    if (cleared >= 3) score += 120;             // a real 3-match
                    if (sim.Score() > g.Score()) score += (sim.Score() - g.Score()) / 20;
                }
                if (score > bestScore) {
                    bestScore = score;
                    bestX = aims[a][0];
                    bestY = aims[a][1];
                }
                ++tried;
            }
        }
    }

    if (tried == 0) {
        // Nothing clean to aim at: lob one up the middle, still deterministic.
        bestX = g.ShooterX() + (float)((int)((g.Moves() * 37u) % 61u) - 30);
        bestY = 120.0f;
    }
    *tx = bestX;
    *ty = bestY;
    return true;
}

int BubbleApp::autoplayDraftSlot() const
{
    // Slot -> card mapping comes from draftOption()'s pools. One pick per
    // archetype: bomb start, big blast, double bomb.
    static const int pref[3] = { 0, 0, 1 };
    return pref[m_draftRound % 3];
}

// Drive the normal player input path from a bot decision, so autoplay exercises
// exactly the same code as a human (banners, specials, retries, draft).
void BubbleApp::driveAutoplay(float dt, hge::Input& in)
{
    in.firePressed = false;
    in.restartPressed = false;
    in.mouseMoved = false;
    in.aimLeft = in.aimRight = false;

    // Duel: the bot drives the player side through DuelMatch (see updateDuel),
    // so it never touches the raw input path here.
    if (m_duel) {
        m_autoArmed = false;
        if (m_phase == Phase::CLEAR || m_phase == Phase::OVER) in.firePressed = true;
        return;
    }

    if (m_autoThink > 0.0f) m_autoThink -= dt;

    switch (m_phase) {
    case Phase::BRIEF:
        in.firePressed = true;
        break;

    case Phase::PLAY: {
        if (m_sidesPtr[0].game.GetState() != Game::AIM) { m_autoArmed = false; break; }
        if (m_autoArmed) {
            // The app fires with last frame's angle, then applies the new aim,
            // so aim on one frame and pull the trigger on the next.
            in.mouseMoved = true;
            in.mouseX = (int)m_autoAimX;
            in.mouseY = (int)m_autoAimY;
            in.firePressed = true;
            m_autoArmed = false;
            m_autoThink = 0.22f;
            break;
        }
        if (m_autoThink > 0.0f) break;
        float tx = 0.0f, ty = 0.0f;
        if (planAutoplayShot(&tx, &ty)) {
            m_autoAimX = tx;
            m_autoAimY = ty;
            m_autoArmed = true;
            in.mouseMoved = true;
            in.mouseX = (int)tx;
            in.mouseY = (int)ty;
        }
        break;
    }

    case Phase::DRAFT:
        if (m_draftCooldown <= 0.0f) {
            m_draftSel = autoplayDraftSlot();
            m_draftMouseMode = false;
            in.firePressed = true;
        }
        break;

    case Phase::OVER:
        if (m_retries > 0) in.firePressed = true;   // retry same stage
        else in.restartPressed = true;              // out of retries: new run
        break;

    default:
        break;
    }
}

// Duel v2 adapter. Every rule lives in DuelMatch; this function only forwards
// input, keeps the one-shot fire cooldown and lifts attack events into the HUD.
void BubbleApp::updateDuel(float dt, const hge::Input& in)
{
    if (m_duelAtkTimer > 0.0f) m_duelAtkTimer -= dt;
    if (m_duelShotCd > 0.0f) m_duelShotCd -= dt;

    // Aiming is allowed at any time (design §4.3): it only moves the barrel and
    // the pre-aim angle, never any rule value.
    if (in.mouseMoved) m_match.AimAt((float)in.mouseX, (float)in.mouseY);
    const float aimRate = 1.6f;     // rad/s
    if (in.aimLeft)  m_match.AimBy(-aimRate * dt);
    if (in.aimRight) m_match.AimBy(aimRate * dt);

    if (in.firePressed && m_duelShotCd <= 0.0f) {
        if (m_match.Fire()) m_duelShotCd = 0.12f;   // T8: no accidental double fire
    }

    m_match.Tick(dt, m_opt.autoplay);

    int side = 0, atk = 0, col = 0;
    while (m_match.TakeAttackEvent(&side, &atk, &col)) {
        m_duelAtk[side] = atk;
        m_duelAtkCol[side] = col;
        m_duelAtkTimer = 1.2f;
        m_corruptFlash = 0.0f;
        if (side == 0 && m_cPop) audio::Play(*m_cPop, 0.40f);
    }

    const int res = m_match.Result();
    if (res == kDuelRunning) return;

    m_duelResult = (res == kDuelPlayerWin) ? 1 : (res == kDuelPlayerLose ? 2 : 3);
    if (m_phase != Phase::CLEAR && m_phase != Phase::OVER) {
        if (res == kDuelPlayerLose) {
            m_phase = Phase::OVER;
            m_phaseTimer = 0.0f;
            if (m_cLose) audio::Play(*m_cLose, 0.55f);
        } else {
            m_phase = Phase::CLEAR;
            m_phaseTimer = 1.8f;
            if (m_cWin) audio::Play(*m_cWin, 0.70f);
        }
    }
}

// Same difficulty, fresh seed: the duel equivalent of a rematch.
void BubbleApp::rematchDuel()
{
    ++m_duelSeed;
    m_phase = Phase::PLAY;
    m_duelResult = 0;
    m_match.Reset((u32)m_duelSeed);
    m_duelAtk[0] = m_duelAtk[1] = 0;
    m_duelAtkCol[0] = m_duelAtkCol[1] = -1;
    m_duelAtkTimer = 0.0f;
    m_duelShotCd = 0.0f;
    m_banner = nullptr;
    m_bannerTimer = 0.0f;
    m_corruptFlash = 0.0f;
    fprintf(stderr, "[duel] rematch seed=%d\n", m_duelSeed);
}

void BubbleApp::Update(float dt, const hge::Input& inRaw)
{
    hge::Input synthesized = inRaw;
    if (m_opt.autoplay) driveAutoplay(dt, synthesized);
    const hge::Input& in = synthesized;

    if (!m_ready) return;

    if (m_bannerTimer > 0.0f) m_bannerTimer -= dt;
    if (m_draftCooldown > 0.0f) m_draftCooldown -= dt;
    if (m_corruptFlash > 0.0f) m_corruptFlash -= dt;
    if (m_chainTimer > 0.0f) m_chainTimer -= dt;
    if (m_shake > 0.0f) m_shake -= dt;

    // A short slow-mo on a big chain, but keep UI/readability responsive.
    float simDt = dt;
    if (m_slowMo > 0.0f) {
        m_slowMo -= dt;
        simDt = dt * 0.28f;
    }

    switch (m_phase) {
    case Phase::BRIEF:
        if (in.firePressed) {
            m_phase = Phase::PLAY;
            m_banner = "GO!";
            m_bannerTimer = 1.0f;
        }
        break;

    case Phase::PLAY: {
        if (in.restartPressed) {
            m_mods = 0;
            m_bonusBombs = 0;
            m_draftRound = 0;
            m_lastModName = nullptr;
            m_lastModDesc = nullptr;
            startStage(0);
            break;
        }

        if (m_duel) {
            updateDuel(dt, in);
            break;
        } else {
            if (in.firePressed) m_sidesPtr[0].game.Fire();
            if (m_sidesPtr[0].game.IsFlying())
                m_activeShotSpecial = m_sidesPtr[0].game.ShotSpecial();
            if (in.mouseMoved)
                m_sidesPtr[0].game.AimAt((float)in.mouseX, (float)in.mouseY);

            const float aimRate = 1.6f; // rad/s (slower for finer aim control)
            if (in.aimLeft)  m_sidesPtr[0].game.AimBy(-aimRate * dt);
            if (in.aimRight) m_sidesPtr[0].game.AimBy(aimRate * dt);
        }

        const int curSp = m_sidesPtr[0].game.CurrentSpecial();
        if (curSp != m_lastSpecialHint) {
            if (curSp != kSpecialNone && m_sidesPtr[0].game.GetState() == Game::AIM) {
                if (curSp == kSpecialBomb) {
                    if (!m_bombHintShown) {
                        m_bombHintShown = true;
                        m_banner = "BOMB READY";
                        m_bannerTimer = 1.6f;
                    }
                } else if (curSp == kSpecialPrism) {
                    m_banner = "PRISM READY";
                    m_bannerTimer = 1.2f;
                } else if (curSp == kSpecialChameleon) {
                    m_banner = "CHAMELEON READY";
                    m_bannerTimer = 1.2f;
                }
            }
            m_lastSpecialHint = curSp;
        }

        if (m_sidesPtr[0].game.GetState() == Game::LOSE) {
            fprintf(stderr, "[game] t=%.2f over\n", hge::Time());
            m_phase = Phase::OVER;
            m_phaseTimer = 0.0f;
            break;
        }

        const int pops0 = m_sidesPtr[0].game.Pops();
        const int drops0 = m_sidesPtr[0].game.Drops();
        updateBattle(m_sides, simDt);
        if (!m_duel) aiUpdate(m_sidesPtr[1], simDt);

        // Chain rank: total bubbles removed by one shot (match + drops).
        if (m_activeShotSpecial != kSpecialNone && !m_sidesPtr[0].game.IsFlying()) {
            if (m_activeShotSpecial == kSpecialPrism) {
                m_banner = "PRISM!";
                m_bannerTimer = 1.2f;
            } else if (m_activeShotSpecial == kSpecialChameleon) {
                m_banner = "PAINT!";
                m_bannerTimer = 1.2f;
            }
            m_activeShotSpecial = kSpecialNone;
        }

        const int cleared = (m_sidesPtr[0].game.Pops() - pops0) +
                            (m_sidesPtr[0].game.Drops() - drops0);
        if (cleared >= 3) {
            int rank = 0;
            if (cleared >= 12) rank = 3;
            else if (cleared >= 8) rank = 2;
            else if (cleared >= 5) rank = 1;
            if (rank >= m_chainRank || m_chainTimer <= 0.0f) {
                m_chainTimer = (rank == 0) ? 0.55f : (rank == 1 ? 0.8f : 1.05f);
                m_chainRank = rank;
                m_chainCount = cleared;
                // Only the rare 8+ chain gets a tiny board-only shake; 3/5
                // keep the screen stable.
                m_shake = (rank >= 2) ? 0.14f : 0.0f;
                m_shakeAmp = (rank >= 2) ? (2.0f + 0.6f * (float)rank) : 0.0f;
                m_slowMo = (rank >= 1) ? 0.18f : 0.0f;
                fprintf(stderr, "[chain] cleared=%d rank=%d count=%d\n",
                        cleared, rank, m_chainCount);
            }
        }

        const int total = m_sidesPtr[0].game.CorruptTotal();
        const int cp = m_sidesPtr[0].game.CorruptPopped();
        if (!m_duel && cp > m_lastCorrupt) {
            const int gained = cp - m_lastCorrupt;
            m_lastCorrupt = cp;
            m_corruptFlash = 0.18f;

            // Corrupted bubbles are a bounty, not the win condition: each one
            // sends a garbage wave to the CPU, and clearing all three adds a
            // larger burst.
            m_sidesPtr[1].game.AddGarbage(3 * gained);
            if (total > 0 && cp >= total) {
                m_sidesPtr[1].game.AddGarbage(4);
                m_banner = "BURST!";
            } else {
                m_banner = "CORRUPTED!";
            }
            m_bannerTimer = 1.2f;
        }

        if (m_sidesPtr[0].game.GetState() == Game::LOSE) {
            fprintf(stderr, "[game] t=%.2f over\n", hge::Time());
            m_phase = Phase::OVER;
            m_phaseTimer = 0.0f;
            break;
        }

        // Default stage win: the CPU's board overflows. The corrupted-bubble
        // bounty only accelerates this; it is not a separate exit.
        if (m_sidesPtr[1].game.GetState() == Game::LOSE) {
            fprintf(stderr, "[game] t=%.2f clear\n", hge::Time());
            m_phase = Phase::CLEAR;
            m_phaseTimer = 1.6f;
            m_perfect = (total > 0 && cp >= total);
            if (m_perfect) m_bonusBombs = 1;
            if (m_cWin) audio::Play(*m_cWin, 0.70f);
        }
        break;
    }

    case Phase::CLEAR:
        m_phaseTimer -= dt;
        if (m_phaseTimer <= 0.0f || in.firePressed) {
            // The win jingle is ~12 s; let the clear card play it, then cut it
            // before the draft so it cannot bleed into the next stage.
            audio::StopEffects();
            if (m_duel) { rematchDuel(); break; }
            fprintf(stderr, "[game] t=%.2f draft round=%d\n", hge::Time(), m_draftRound);
            m_phase = Phase::DRAFT;
            m_draftSel = 0;
            m_draftCooldown = 0.0f;
            m_draftMouseMode = false;
        }
        break;

    case Phase::DRAFT:
        updateDraft(dt, in);
        break;

    case Phase::OVER:
        if (m_duel) {
            if (in.firePressed) rematchDuel();
            break;
        }
        if (in.firePressed && m_retries > 0) {
            const int left = m_retries - 1;
            startStage(m_stage, false);
            m_retries = left;
            m_banner = "RETRY";
            m_bannerTimer = 1.2f;
        } else if (in.restartPressed) {
            m_mods = 0;
            m_bonusBombs = 0;
            m_draftRound = 0;
            m_lastModName = nullptr;
            m_lastModDesc = nullptr;
            startStage(0);
        }
        break;
    }

    if (m_opt.audioStress && m_audioOn) {
        m_audioStressTimer -= dt;
        if (m_audioStressTimer <= 0.0f) {
            m_audioStressTimer = 0.08f;
            for (int i = 0; i < 2; ++i)
                if (m_cPop) audio::Play(*m_cPop, 0.20f);
        }
    }

    updateAudio(dt);
    audio::Tick();
}

void BubbleApp::updateAudio(float /*dt*/)
{
    if (!m_audioOn) return;

    const int mv0 = m_sidesPtr[0].game.Moves();
    const int pp0 = m_sidesPtr[0].game.Pops();
    const int dd0 = m_sidesPtr[0].game.Drops();
    const int de0 = m_sidesPtr[0].game.Descends();
    if (mv0 > m_lastMoves[0]) { if (m_cShoot) audio::Play(*m_cShoot, 0.38f); m_popsAtFire = pp0; }
    if (pp0 > m_lastPopsAll)  { if (m_cPop)   audio::Play(*m_cPop, 0.52f); }
    if (dd0 > m_lastDropsAll) { if (m_cDrop)  audio::Play(*m_cDrop, 0.48f); }
    if (de0 > m_lastMoves[1]) { if (m_cDesc)  audio::Play(*m_cDesc, 0.40f); }
    if (m_prevState[0] == Game::FLY && m_sidesPtr[0].game.GetState() != Game::FLY &&
        pp0 == m_popsAtFire) {
        if (m_cStick) audio::Play(*m_cStick, 0.30f);
    }
    m_lastMoves[0] = mv0;
    m_lastMoves[1] = de0;
    m_lastPopsAll = pp0;
    m_lastDropsAll = dd0;

    const Game::State st0 = m_sidesPtr[0].game.GetState();
    const Game::State st1 = m_sidesPtr[1].game.GetState();
    if (st0 == Game::LOSE && m_prevState[0] != Game::LOSE) {
        fprintf(stderr, "[game] t=%.2f sfx lose\n", hge::Time());
        if (m_cLose) audio::Play(*m_cLose, 0.55f);
    }
    if (st1 == Game::LOSE && m_prevState[1] != Game::LOSE)
        fprintf(stderr, "[game] t=%.2f ai lose\n", hge::Time());
    m_prevState[0] = st0;
    m_prevState[1] = st1;
}

void BubbleApp::Render(IGfx* /*gfx*/, double time)
{
    if (!m_ready) return;

    HudState hud;
    hud.stage = m_stage + 1;
    hud.corruptPopped = m_sidesPtr[0].game.CorruptPopped();
    hud.corruptTotal = m_sidesPtr[0].game.CorruptTotal();
    hud.phase = (int)m_phase;
    hud.draftSel = m_draftSel;
    for (int i = 0; i < 3; ++i) {
        const int opt = draftOption(i);
        hud.draftName[i] = draftName(opt);
        hud.draftDesc[i] = draftDesc(opt);
    }
    hud.perfect = m_perfect;
    hud.canRetry = m_retries > 0;
    hud.retries = m_retries;
    hud.duel = m_duel;
    if (m_duel) {
        hud.depth[0] = m_match.Depth(0);
        hud.depth[1] = m_match.Depth(1);
        hud.depthMax = kDepthMax;
        hud.turn = m_match.Turn();
        hud.shotsLeft = m_match.ShotsLeft();
        hud.shotsMax = m_match.ShotsPerTurn();
        hud.nextDescendIn = m_match.NextDescendIn();
        hud.attackOut[0] = m_duelAtk[0];
        hud.attackOut[1] = m_duelAtk[1];
        hud.attackCol[0] = m_duelAtkCol[0];
        hud.attackCol[1] = m_duelAtkCol[1];
        hud.attackFlash = m_duelAtkTimer;
        hud.duelResult = m_duelResult;
        hud.duelShots = m_match.GlobalShots();
        hud.duelMaxAtk = m_match.MaxAttack(0);
        hud.pass = m_match.SubPhase() != kDuelShooting;
        hud.canRetry = false;
    }
    hud.banner = m_banner;
    hud.modName = m_lastModName;
    hud.modDesc = m_lastModDesc;
    hud.bannerTimer = m_bannerTimer;
    hud.corruptFlash = m_corruptFlash;
    hud.chainTimer = m_chainTimer;
    hud.chainRank = m_chainRank;
    hud.chainCount = m_chainCount;
    hud.shake = m_shake;
    hud.shakeAmp = m_shakeAmp;

    RenderFrame(m_render, m_sidesPtr, time + m_opt.animOffset, 0.0f, hud);
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
