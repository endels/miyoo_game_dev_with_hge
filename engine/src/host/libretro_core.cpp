// Generic libretro core host. The game supplies hge::libretro::CreateGame()
// and GetGameInfo(); this file owns the retro_* entry points, the coroutine
// stepping, video/audio handoff and input mapping.

#include "hge_libretro.h"
#include "hge_config.h"
#include "hge_audio.h"

#include "platform/plat_libretro.h"
#include "gfx/gfx_blit.h"

#include <libretro.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

static retro_video_refresh_t        video_cb;
static retro_audio_sample_t         audio_cb;
static retro_audio_sample_batch_t   audio_batch_cb;
static retro_environment_t          environ_cb;
static retro_input_poll_t           input_poll_cb;
static retro_input_state_t          input_state_cb;

static bool     g_running = false;
static unsigned g_prevButtons = 0;

namespace {

double nowSec()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

const hge::libretro::GameInfo& info()
{
    return hge::libretro::GetGameInfo();
}

int runEntry(int, char**)
{
    const hge::libretro::GameInfo& gi = info();

    hge::RunConfig cfg;
    cfg.width = (int)gi.logical_width;
    cfg.height = (int)gi.logical_height;
    cfg.title = gi.name;
    cfg.vsync = false;
    cfg.resizable = false;

    std::unique_ptr<hge::IGame> game(hge::libretro::CreateGame());
    if (!game) return 2;
    return hge::Run(*game, cfg);
}

void writePPM(const char* path, const void* fb, int w, int h, int pitch)
{
    if (!fb || w <= 0 || h <= 0) return;
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "[core] PPM open failed: %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    const unsigned char* p = (const unsigned char*)fb;
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = p + (size_t)y * pitch;
        for (int x = 0; x < w; ++x) {
            unsigned char rgb[3] = { row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0] };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    fprintf(stderr, "[core] dumped %s (%dx%d)\n", path, w, h);
}

} // namespace

extern "C" {

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_set_controller_port_device(unsigned, unsigned) {}

void retro_init(void)
{
    plat_libretro_set_entry(runEntry);
}

void retro_deinit(void) { plat_libretro_cleanup(); }

void retro_get_system_info(struct retro_system_info* si)
{
    memset(si, 0, sizeof(*si));
    si->library_name     = info().name;
    si->library_version  = info().version;
    si->valid_extensions = "";
    si->need_fullpath    = false;
    si->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info* av)
{
    memset(av, 0, sizeof(*av));
    av->geometry.base_width   = info().video_width;
    av->geometry.base_height  = info().video_height;
    av->geometry.max_width    = info().video_width;
    av->geometry.max_height   = info().video_height;
    av->geometry.aspect_ratio = (float)info().video_width / (float)info().video_height;
    av->timing.fps            = info().fps;
    av->timing.sample_rate    = info().sample_rate;
}

void retro_set_environment(retro_environment_t cb)
{
    environ_cb = cb;
    bool no_game = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

bool retro_load_game(const struct retro_game_info*)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);

    plat_libretro_set_entry(runEntry);
    g_running = plat_libretro_begin() != 0;
    return g_running;
}

bool retro_load_game_special(unsigned, const struct retro_game_info*, size_t)
{
    return retro_load_game(nullptr);
}

void retro_unload_game(void) { g_running = false; }

void retro_reset(void) {}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

void retro_run(void)
{
    if (!g_running) {
        if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
        return;
    }

    const double tFrame0 = nowSec();
    if (input_poll_cb) input_poll_cb();

    unsigned held = 0;
    if (input_state_cb) {
        const unsigned port = 0;
        const unsigned dev  = RETRO_DEVICE_JOYPAD;
        const unsigned idx  = 0;
        auto B = [&](unsigned id) {
            return input_state_cb(port, dev, idx, id) != 0;
        };
        if (B(RETRO_DEVICE_ID_JOYPAD_LEFT))   held |= PB_LEFT;
        if (B(RETRO_DEVICE_ID_JOYPAD_RIGHT))  held |= PB_RIGHT;
        if (B(RETRO_DEVICE_ID_JOYPAD_UP))     held |= PB_UP;
        if (B(RETRO_DEVICE_ID_JOYPAD_DOWN))   held |= PB_DOWN;
        if (B(RETRO_DEVICE_ID_JOYPAD_A))      held |= PB_A;
        if (B(RETRO_DEVICE_ID_JOYPAD_B))      held |= PB_B;
        if (B(RETRO_DEVICE_ID_JOYPAD_X))      held |= PB_X;
        if (B(RETRO_DEVICE_ID_JOYPAD_Y))      held |= PB_Y;
        if (B(RETRO_DEVICE_ID_JOYPAD_START))  held |= PB_START;
        if (B(RETRO_DEVICE_ID_JOYPAD_SELECT)) held |= PB_SELECT;
        if (B(RETRO_DEVICE_ID_JOYPAD_L))      held |= PB_L;
        if (B(RETRO_DEVICE_ID_JOYPAD_R))      held |= PB_R;
    }

    const unsigned pressed = held & ~g_prevButtons;
    g_prevButtons = held;
    plat_libretro_set_buttons(held, pressed);

    const double tStep0 = nowSec();
    const bool alive = plat_libretro_step() != 0;
    const double tStep1 = nowSec();
    if (!alive) {
        g_running = false;
        if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
        return;
    }

    int w = 0, h = 0, pitch = 0;
    const void* fb = BlitFramebuffer(&w, &h, &pitch);
    if (fb && video_cb)
        video_cb(fb, (unsigned)w, (unsigned)h, (size_t)pitch);

#if HGE_FEATURE_AUDIO
    // Generate audio for the real elapsed time. The frontend owns the device
    // and paces frames; a fixed samples-per-frame value would stretch audio.
    static int16_t abuf[8192 * 2];
    static double   lastT = 0.0;
    static double   acc   = 0.0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const double nowT = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    if (lastT == 0.0) lastT = nowT;
    double dt = nowT - lastT;
    lastT = nowT;
    if (dt < 0.0) dt = 0.0;
    if (dt > 0.200) dt = 0.200;
    acc += dt * (info().sample_rate > 0.0 ? info().sample_rate : 44100.0);
    int frames = (int)acc;
    acc -= (double)frames;
    if (frames > 8192) frames = 8192;
    if (frames > 0) {
        audio::Fill(abuf, frames, 2);
        static int audioLog = 0;
        if (++audioLog % 120 == 0) {
            int peak = 0;
            for (int i = 0; i < frames * 2; ++i) {
                int v = abuf[i]; if (v < 0) v = -v;
                if (v > peak) peak = v;
            }
            fprintf(stderr, "[core] audio frames=%d peak=%d\n", frames, peak);
        }
        if (audio_batch_cb) audio_batch_cb(abuf, (size_t)frames);
    }
#endif

    {
        static int fc = 0;
        const char* dp = getenv("HGE_DUMP");
        if (dp) {
            int at = 60;
            const char* dfs = getenv("HGE_DUMP_FRAME");
            if (dfs) at = atoi(dfs);
            if (fc == at) writePPM(dp, fb, w, h, pitch);
        }
        ++fc;
    }
    {
        static double accStep = 0, accAud = 0, accCall = 0;
        static int perfLog = 0;
        const double tAud = nowSec();
        accStep += tStep1 - tStep0;
        accAud  += tAud - tStep1;
        accCall += tAud - tStep0;
        if (++perfLog % 120 == 0) {
            fprintf(stderr, "[core] perf step=%.1fms audio=%.1fms call=%.1fms\n",
                    accStep / 120 * 1000.0, accAud / 120 * 1000.0,
                    accCall / 120 * 1000.0);
            accStep = accAud = accCall = 0;
        }
    }

    // Pace to 60 fps so audio delivery and timing stay stable.
    // HGE_UNCAPPED=1 disables.
    {
        static const bool uncapped = getenv("HGE_UNCAPPED") != nullptr;
        if (!uncapped) {
            const double spent = nowSec() - tFrame0;
            const double target = 1.0 / 60.0;
            if (spent < target) {
                double sl = target - spent;
                struct timespec ts;
                ts.tv_sec = (time_t)sl;
                ts.tv_nsec = (long)((sl - (double)ts.tv_sec) * 1e9);
                nanosleep(&ts, nullptr);
            }
        }
    }
}

size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void*, size_t) { return false; }
bool retro_unserialize(const void*, size_t) { return false; }
void* retro_get_memory_data(unsigned) { return nullptr; }
size_t retro_get_memory_size(unsigned) { return 0; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned, bool, const char*) {}

} // extern "C"
