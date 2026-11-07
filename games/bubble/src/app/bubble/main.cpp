// Bubble stage prototype — player vs AI on two boards, with a corrupted-bubble
// objective on the player's board.
//
//   bubble_level1              play (mouse aim, click/space fire, R restart)
//   bubble_level1 --selftest   headless grid/physics invariants, exit 0/1
//   bubble_level1 --shot a.ppm render one frame to a PPM and exit
//   bubble_level1 --autoplay   dev: a bot plays the player board
//   bubble_level1 --retries N  consecutive stage losses before GAME OVER (default 3)
//   bubble_level1 --mute       silent run (audio callback keeps ticking)
//   bubble_level1 --duel       turn-based puzzle duel (5 shots each, depth HP)
//   bubble_level1 --ai-level N start the CPU at level N (1..5; in game, click
//                              the CPU dino to cycle it)
//   --debug-clear / --debug-draft  jump to a screenshot state (development)

#include "hge_app.h"
#include "hge_audio.h"
#include "app/bubble/battle.h"
#include "app/bubble/bubble_app.h"
#include "app/bubble/duel.h"
#include "app/bubble/game.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Options {
    bool        selftest = false;
    int         frames = 0;
    int         warmup = 0;
    float       animOffset = 0.0f;
    float       simSeconds = 0.0f;
    double      simOnly = -1.0;
    bool        renderTest = false;
    bool        debugClear = false;
    bool        debugDraft = false;
    bool        debugChain = false;
    bool        audioStress = false;
    bool        autoplay = false;
    bool        duel = false;
    bool        mute = false;
    int         retries = 3;
    int         aiLevel = bbl::kAiLevelDefault;
    bool        aiLevelsGiven = false;
    int         aiLevelP = bbl::kAiLevelDefault;
    int         aiLevelC = bbl::kAiLevelDefault;
    double      fixedDt = 0.0;
    bool        duelSim = false;
    unsigned    seed = 1u;
    int         shotFrame = 40;
    bool        shotFrameGiven = false;
    const char* shotPath = nullptr;
    const char* replayPath = nullptr;
    unsigned long long expectChecksum = 0;
    bool        expectChecksumGiven = false;
};

Options parseArgs(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--selftest")) o.selftest = true;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) o.shotPath = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) o.frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warmup") && i + 1 < argc) o.warmup = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--anim") && i + 1 < argc) o.animOffset = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--sim") && i + 1 < argc) o.simSeconds = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--sim-only") && i + 1 < argc) o.simOnly = atof(argv[++i]);
        else if (!strcmp(argv[i], "--render-test")) o.renderTest = true;
        else if (!strcmp(argv[i], "--debug-clear")) o.debugClear = true;
        else if (!strcmp(argv[i], "--debug-draft")) o.debugDraft = true;
        else if (!strcmp(argv[i], "--debug-chain")) o.debugChain = true;
        else if (!strcmp(argv[i], "--audio-stress")) o.audioStress = true;
        else if (!strcmp(argv[i], "--autoplay")) o.autoplay = true;
        else if (!strcmp(argv[i], "--duel")) o.duel = true;
        else if (!strcmp(argv[i], "--duel-sim")) o.duelSim = true;
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) o.seed = (unsigned)strtoul(argv[++i], nullptr, 0);
        else if (!strcmp(argv[i], "--mute")) o.mute = true;
        else if (!strcmp(argv[i], "--retries") && i + 1 < argc) o.retries = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ai-level") && i + 1 < argc) o.aiLevel = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--duel-ai-levels") && i + 1 < argc) {
            int a = 0, b = 0;
            if (sscanf(argv[++i], "%d,%d", &a, &b) == 2) {
                o.aiLevelsGiven = true;
                o.aiLevelP = a;
                o.aiLevelC = b;
            }
        }
        else if (!strcmp(argv[i], "--fixed-dt") && i + 1 < argc) o.fixedDt = atof(argv[++i]);
        else if (!strcmp(argv[i], "--shot-frame") && i + 1 < argc) {
            o.shotFrame = atoi(argv[++i]);
            o.shotFrameGiven = true;
        }
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) o.replayPath = argv[++i];
        else if (!strcmp(argv[i], "--expect-checksum") && i + 1 < argc) {
            o.expectChecksum = strtoull(argv[++i], nullptr, 0);
            o.expectChecksumGiven = true;
        }
    }
    return o;
}

int runSimOnly(const Options& o)
{
    const float step = 1.0f / 60.0f;
    const int steps = (int)(o.simOnly / step + 0.5);

    bbl::Side sides[2];
    bbl::resetBattle(sides);
    for (int i = 0; i < steps; ++i) {
        bbl::updateBattle(sides, step);
        bbl::aiUpdate(sides[1], step);
    }

    const u64 sum = bbl::battleChecksum(sides);
    printf("[sim] p1_left=%d p2_left=%d p1_score=%d p2_score=%d "
           "p1_rows=%d p2_rows=%d checksum=0x%016llx\n",
           sides[0].game.BubblesLeft(), sides[1].game.BubblesLeft(),
           sides[0].game.Score(), sides[1].game.Score(),
           sides[0].game.RowsUsed(), sides[1].game.RowsUsed(),
           (unsigned long long)sum);

    if (o.expectChecksumGiven && sum != o.expectChecksum) {
        fprintf(stderr, "[sim] checksum mismatch: got 0x%016llx expected 0x%016llx\n",
                (unsigned long long)sum, o.expectChecksum);
        return 1;
    }
    return 0;
}

// Headless duel: both sides driven by the same greedy bot, 1/60 fixed step.
// Prints one summary line per match so a shell loop can collect 200 seeds.
int runDuelSim(const Options& o)
{
    const float step = 1.0f / 60.0f;
    const int maxFrames = (o.simOnly > 0.0) ? (int)(o.simOnly / step + 0.5) : 400000;

    bbl::DuelMatch m;
    m.Reset(o.seed);
    if (o.aiLevelsGiven) m.SetAiLevels(o.aiLevelP, o.aiLevelC);
    else m.SetAiLevel(o.aiLevel);

    int frames = 0;
    while (frames < maxFrames && m.Result() == bbl::kDuelRunning) {
        m.Tick(step, true);
        ++frames;
    }

    bbl::Side tmp[2] = { m.SideRef(0), m.SideRef(1) };
    u64 sum = bbl::battleChecksum(tmp);
    auto mix = [&sum](u64 v) {
        sum ^= v + 0x9E3779B97F4A7C15ull + (sum << 6) + (sum >> 2);
    };
    mix((u64)m.Result());
    mix((u64)m.GlobalShots());
    mix((u64)m.Depth(0));
    mix((u64)m.Depth(1));
    mix((u64)m.Overflow());

    printf("[duel] seed=%u winner=%d turns=%d shots=%d dur=%.1f depth0=%d depth1=%d "
           "atk0=%d atk1=%d overflow=%d shots_log=%d frames=%d blunder=%d/%d "
           "checksum=0x%016llx\n",
           o.seed, m.Result(), m.TurnCount(), m.GlobalShots(),
           (double)frames * (double)step,
           m.Depth(0), m.Depth(1), m.MaxAttack(0), m.MaxAttack(1), m.Overflow(),
           m.ShotCount(), frames, m.AiBlunders(), m.AiShots(),
           (unsigned long long)sum);

    if (o.expectChecksumGiven && sum != o.expectChecksum) {
        fprintf(stderr, "[duel] checksum mismatch: got 0x%016llx expected 0x%016llx\n",
                (unsigned long long)sum, (unsigned long long)o.expectChecksum);
        return 1;
    }
    return m.Result() == bbl::kDuelRunning ? 2 : 0;
}

int runGame(int argc, char** argv)
{
    const Options opts = parseArgs(argc, argv);

    if (opts.selftest) {
        bbl::Game game;
        game.Configure(bbl::kBoardCols, 0.0f);
        char msg[256];
        const bool ok = game.SelfTest(msg, sizeof(msg));
        printf("[bubble] SELFTEST %s: %s\n", ok ? "PASS" : "FAIL", msg);
        return ok ? 0 : 1;
    }

    if (opts.duelSim)
        return runDuelSim(opts);

    if (opts.simOnly >= 0.0) {
        if (opts.duel || opts.aiLevelsGiven) return runDuelSim(opts);
        return runSimOnly(opts);
    }

    bbl::BubbleOptions gameOpt;
    gameOpt.warmup = opts.warmup;
    gameOpt.animOffset = opts.animOffset;
    gameOpt.simSeconds = opts.simSeconds;
    gameOpt.debugClear = opts.debugClear;
    gameOpt.debugDraft = opts.debugDraft;
    gameOpt.debugChain = opts.debugChain;
    gameOpt.audioStress = opts.audioStress;
    gameOpt.autoplay = opts.autoplay;
    gameOpt.duel = opts.duel;
    if (opts.mute) audio::SetMuted(true);
    gameOpt.retries = opts.retries;
    gameOpt.aiLevel = opts.aiLevel;
    if (argc > 0 && argv[0]) gameOpt.selfPath = argv[0];

    hge::RunConfig cfg;
    cfg.width = bbl::kWinW;
    cfg.height = bbl::kWinH;
    cfg.title = "Bubble Versus - HGE-SDL";
    cfg.vsync = (opts.shotPath == nullptr) && !opts.renderTest;
    // WSLg/llvmpipe often ignores the swap interval; an uncapped loop can
    // starve the SDL audio callback. Cap the game at 60 FPS on desktop.
    cfg.targetFps = 60.0;
    cfg.resizable = true;
    cfg.maxFrames = opts.frames;
    cfg.shotPath = opts.shotPath;
    cfg.shotFrame = opts.shotFrame;
    cfg.fixedDt = opts.fixedDt;
    cfg.replayPath = opts.replayPath;

    if (opts.renderTest) {
        if (cfg.fixedDt <= 0.0) cfg.fixedDt = 1.0 / 60.0;
        if (cfg.maxFrames <= 0) cfg.maxFrames = 1;
        if (cfg.shotPath && !opts.shotFrameGiven)
            cfg.shotFrame = cfg.maxFrames;
    }

    const char* wd = getenv("BUBBLE_WATCHDOG");
    if (wd) cfg.watchdog = atof(wd);
    if (cfg.watchdog > 0.0)
        fprintf(stderr, "[t] watchdog=%.0fs\n", cfg.watchdog);

    bbl::BubbleApp app(gameOpt);
    return hge::Run(app, cfg);
}

} // namespace

int main(int argc, char** argv)
{
    return runGame(argc, argv);
}
