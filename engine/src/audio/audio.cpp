#include "hge_audio.h"

#include <SDL.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace audio {
namespace {

const int kVoices = 24;

struct Voice {
    const i16* pcm;
    int    frames;
    int    channels;
    int    rate;
    double pos;
    double step;
    float  vol;
    bool   active;
    bool   loop;
};

Voice g_voices[kVoices];
Voice g_music;
std::atomic<const Clip*> g_musicClip{nullptr};

SDL_AudioDeviceID g_dev = 0;
SDL_AudioSpec     g_spec;
SDL_AudioSpec     g_want;
bool              g_wantValid = false;
float             g_musicVol = 0.45f;
char              g_info[160] = "audio: not initialised";
std::vector<int>  g_mix;   // pre-reserved so the callback never allocates

// Diagnostic state written by the audio callback and printed by Tick().
std::atomic<int> g_cbCount{0};
std::atomic<int> g_activeVoices{0};
std::atomic<int> g_peak{0};
std::atomic<int> g_musicActive{0};

// Lock-free single-producer/single-consumer queue for one-shot SFX. Starting a
// voice no longer calls SDL_LockAudioDevice from the game thread, so a busy
// llvmpipe frame cannot starve the audio callback.
struct PlayCmd {
    const Clip* clip;
    float       vol;
};
const int kCmdRing = 64;
PlayCmd g_cmds[kCmdRing];
std::atomic<unsigned> g_cmdHead{0};
std::atomic<unsigned> g_cmdTail{0};
std::atomic<bool> g_stopFx{false};
std::atomic<bool> g_muted{false};

// Diagnostic A/B switches for the "audio goes silent" hunt. They let one
// binary replay the old behaviour (and its trigger conditions) without a
// rebuild, while keeping the per-second diagnostics:
//   HGE_AUDIO_LOCKED=1     Play/StopEffects lock the device again
//   HGE_AUDIO_NO_REVIVE=1  skip the callback-side BGM auto-revive
bool envFlag(const char* name)
{
    const char* v = getenv(name);
    return v && v[0] && v[0] != '0';
}

bool legacyLocked() { static const bool on = envFlag("HGE_AUDIO_LOCKED"); return on; }
bool noRevive()     { static const bool on = envFlag("HGE_AUDIO_NO_REVIVE"); return on; }

void startVoice(Voice& v, const Clip& c, float vol, bool loop)
{
    v.pcm      = c.pcm;
    v.frames   = c.frames;
    v.channels = c.channels > 0 ? c.channels : 1;
    v.rate     = c.rate > 0 ? c.rate : 44100;
    v.pos      = 0.0;
    v.step     = (double)v.rate / (double)(g_spec.freq > 0 ? g_spec.freq : 44100);
    v.vol      = vol;
    v.loop     = loop;
    v.active   = (c.pcm != nullptr && c.frames > 1);
}

void startSfx(const Clip& c, float vol)
{
    int slot = -1;
    for (int i = 0; i < kVoices; ++i)
        if (!g_voices[i].active) { slot = i; break; }
    if (slot < 0) slot = 0;   // steal the oldest
    startVoice(g_voices[slot], c, vol, false);
}

bool reopenDevice()
{
    if (!g_wantValid) return false;
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    memset(&g_spec, 0, sizeof(g_spec));
    g_dev = SDL_OpenAudioDevice(nullptr, 0, &g_want, &g_spec, 0);
    if (!g_dev) return false;

    for (int i = 0; i < kVoices; ++i) g_voices[i].active = false;
    g_cmdHead.store(0, std::memory_order_relaxed);
    g_cmdTail.store(0, std::memory_order_relaxed);
    g_stopFx.store(false, std::memory_order_relaxed);
    const Clip* mc = g_musicClip.load(std::memory_order_relaxed);
    if (mc && mc->pcm)
        startVoice(g_music, *mc, g_musicVol, true);
    SDL_PauseAudioDevice(g_dev, 0);
    return true;
}

void mixVoice(Voice& v, int* out, int outFrames, int outCh)
{
    if (!v.active || !v.pcm || v.frames < 2) return;

    for (int i = 0; i < outFrames; ++i) {
        if (v.pos >= (double)(v.frames - 1)) {
            if (v.loop) {
                v.pos -= (double)(v.frames - 1);
                if (v.pos < 0.0) v.pos = 0.0;
            } else {
                v.active = false;
                return;
            }
        }
        const int i0 = (int)v.pos;
        int i1 = i0 + 1;
        if (i1 >= v.frames) i1 = v.frames - 1;
        const float f = (float)(v.pos - (double)i0);

        for (int c = 0; c < outCh; ++c) {
            const int sc = (v.channels == 1) ? 0 : (c < v.channels ? c : 0);
            const float s0 = (float)v.pcm[i0 * v.channels + sc];
            const float s1 = (float)v.pcm[i1 * v.channels + sc];
            int o = out[i * outCh + c] + (int)((s0 + (s1 - s0) * f) * v.vol);
            if (o > 2000000) o = 2000000;
            else if (o < -2000000) o = -2000000;
            out[i * outCh + c] = o;
        }
        v.pos += v.step;
    }
}

void renderMix(int* out, int frames, int ch)
{
    memset(out, 0, (size_t)frames * ch * sizeof(int));
    for (int i = 0; i < kVoices; ++i)
        mixVoice(g_voices[i], out, frames, ch);
    mixVoice(g_music, out, frames, ch);
}

// Soft per-sample limiter. The old version scaled the *whole* buffer down
// whenever BGM + a sound effect crossed the ceiling, which made the music duck
// and sound like it was stuttering. Compress only the samples above the knee
// so the music bed stays constant and loud overlaps just round off.
void toInt16(const int* mix, i16* out, int n)
{
    const float kMaster = 0.68f;
    const float kKnee   = 24000.0f;

    for (int i = 0; i < n; ++i) {
        float x = (float)mix[i] * kMaster;
        if (x > kKnee)       x = kKnee + (x - kKnee) * 0.35f;
        else if (x < -kKnee) x = -kKnee + (x + kKnee) * 0.35f;
        if (x > 32767.0f)       x = 32767.0f;
        else if (x < -32768.0f) x = -32768.0f;
        out[i] = (i16)x;
    }
}

void SDLCALL callback(void* /*userdata*/, Uint8* stream, int len)
{
    const int outCh = g_spec.channels > 0 ? g_spec.channels : 2;
    const int frames = len / ((int)sizeof(i16) * outCh);
    Fill((i16*)stream, frames, outCh);
}

} // namespace

void Fill(i16* out, int frames, int channels)
{
    if (!out || frames <= 0) return;
    if (channels <= 0) channels = 2;

    if (g_muted.load(std::memory_order_relaxed)) {
        // Silent dev mode: keep the callback cadence (and its diagnostics)
        // alive so audio stalls are still observable, but emit nothing.
        const int n = frames * channels;
        memset(out, 0, (size_t)n * sizeof(i16));
        g_peak.store(0, std::memory_order_relaxed);
        g_activeVoices.store(0, std::memory_order_relaxed);
        g_musicActive.store(0, std::memory_order_relaxed);
        g_cbCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (g_stopFx.exchange(false, std::memory_order_acq_rel)) {
        for (int i = 0; i < kVoices; ++i) g_voices[i].active = false;
        g_cmdTail.store(g_cmdHead.load(std::memory_order_acquire),
                        std::memory_order_release);
    }

    // Drain SFX requests queued by the game thread. No SDL lock involved.
    unsigned tail = g_cmdTail.load(std::memory_order_relaxed);
    const unsigned head = g_cmdHead.load(std::memory_order_acquire);
    while (tail != head) {
        const PlayCmd& cmd = g_cmds[tail % (unsigned)kCmdRing];
        startSfx(*cmd.clip, cmd.vol);
        ++tail;
    }
    g_cmdTail.store(tail, std::memory_order_release);

    // Safety net: if the music voice was cleared by anything, revive it.
    if (!noRevive()) {
        const Clip* mc = g_musicClip.load(std::memory_order_relaxed);
        if (!g_music.active && mc && mc->pcm)
            startVoice(g_music, *mc, g_musicVol, true);
    }

    g_mix.resize((size_t)frames * channels);
    renderMix(g_mix.data(), frames, channels);

    int peak = 0;
    int active = 0;
    const int n = frames * channels;
    for (int i = 0; i < n; ++i) {
        int a = g_mix[i];
        if (a < 0) a = -a;
        if (a > peak) peak = a;
    }
    for (int i = 0; i < kVoices; ++i)
        if (g_voices[i].active) ++active;
    g_peak.store(peak, std::memory_order_relaxed);
    g_activeVoices.store(active, std::memory_order_relaxed);
    g_musicActive.store(g_music.active ? 1 : 0, std::memory_order_relaxed);
    g_cbCount.fetch_add(1, std::memory_order_relaxed);

    toInt16(g_mix.data(), out, n);
}

bool Init()
{
    if (const char* m = getenv("HGE_AUDIO_MUTE")) {
        if (m[0] && m[0] != '0') g_muted.store(true, std::memory_order_relaxed);
    }
#ifdef HGE_LIBRETRO
    // The frontend owns the audio device; we only mix into its buffer.
    memset(&g_spec, 0, sizeof(g_spec));
    g_spec.freq = 44100;
    g_spec.channels = 2;
    g_spec.samples = 2048;
    g_mix.reserve(32768);
    snprintf(g_info, sizeof(g_info), "audio: libretro pull (%d Hz)", g_spec.freq);
    return true;
#endif
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        snprintf(g_info, sizeof(g_info), "audio: SDL init failed (%s)", SDL_GetError());
        return false;
    }

    SDL_AudioSpec want;
    memset(&want, 0, sizeof(want));
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    // Diagnostics showed the SDL/Pulse callback itself slowing down under
    // load. A deeper device buffer gives the sink more slack; 8192 is ~186 ms,
    // acceptable for music and still usable for SFX.
    want.samples  = 8192;
    want.callback = callback;
    want.userdata = nullptr;
    g_want = want;
    g_wantValid = true;

    memset(&g_spec, 0, sizeof(g_spec));
    // The Onion audio server may not have released MI_AO yet right after it is
    // killed; retry for ~2s before giving up.
    for (int attempt = 0; attempt < 12; ++attempt) {
        g_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &g_spec, 0);
        if (g_dev) break;
        if (attempt == 0)
            fprintf(stderr, "[bubble] audio open failed (%s); retrying...\n", SDL_GetError());
        SDL_Delay(180);
    }
    if (!g_dev) {
        snprintf(g_info, sizeof(g_info), "audio: no device (%s)", SDL_GetError());
        return false;
    }
    g_mix.reserve(32768);
    SDL_PauseAudioDevice(g_dev, 0);
    snprintf(g_info, sizeof(g_info), "audio: %s %d Hz, %d ch, %d voices, %d buf",
             SDL_GetCurrentAudioDriver(), g_spec.freq, g_spec.channels,
             kVoices, g_spec.samples);
    return true;
}

void Shutdown()
{
    for (int i = 0; i < kVoices; ++i) g_voices[i].active = false;
    g_music.active = false;
    g_musicClip.store(nullptr, std::memory_order_relaxed);
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
}

const char* Info() { return g_info; }

void Tick()
{
#ifndef HGE_LIBRETRO
    if (!g_dev) return;
    const Uint32 now = SDL_GetTicks();
    static Uint32 lastLog = 0;
    static int    lastCb = -1;
    static int    stallCount = 0;
    if (now - lastLog < 1000) return;

    const int cb = g_cbCount.load(std::memory_order_relaxed);
    const int music = g_musicActive.load(std::memory_order_relaxed);
    const int active = g_activeVoices.load(std::memory_order_relaxed);
    const int peak = g_peak.load(std::memory_order_relaxed);
    SDL_Window* focusWin = SDL_GetKeyboardFocus();
    const int focus = focusWin != nullptr ? 1 : 0;
    const int minimized = focusWin &&
                          (SDL_GetWindowFlags(focusWin) & SDL_WINDOW_MINIMIZED) ? 1 : 0;
    if (lastCb == cb) {
        ++stallCount;
        fprintf(stderr, "[audio] t=%.1f STALL cb=%d music=%d active=%d peak=%d focus=%d min=%d\n",
                now / 1000.0, cb, music, active, peak, focus, minimized);
    } else {
        stallCount = 0;
        fprintf(stderr, "[audio] t=%.1f cb=%d music=%d active=%d peak=%d focus=%d min=%d\n",
                now / 1000.0, cb, music, active, peak, focus, minimized);
    }
    lastCb = cb;
    lastLog = now;
#endif
}

void Play(const Clip& clip, float volume)
{
    if (!clip.pcm) return;
    if (g_muted.load(std::memory_order_relaxed)) return;

#ifndef HGE_LIBRETRO
    if (legacyLocked()) {                 // A/B: pre-fix path
        if (!g_dev) return;
        SDL_LockAudioDevice(g_dev);
        startSfx(clip, volume);
        SDL_UnlockAudioDevice(g_dev);
        return;
    }
#endif

    const unsigned head = g_cmdHead.load(std::memory_order_relaxed);
    const unsigned tail = g_cmdTail.load(std::memory_order_acquire);
    if (head - tail >= (unsigned)kCmdRing) return;  // drop rather than block
    g_cmds[head % (unsigned)kCmdRing] = { &clip, volume };
    g_cmdHead.store(head + 1, std::memory_order_release);
}

void PlayMusic(const Clip& clip, float volume)
{
    if (g_muted.load(std::memory_order_relaxed)) return;
#ifdef HGE_LIBRETRO
    if (!clip.pcm) return;
#else
    if (!g_dev || !clip.pcm) return;
    SDL_LockAudioDevice(g_dev);
#endif
    g_musicVol = volume;
    g_musicClip.store(&clip, std::memory_order_relaxed);
    startVoice(g_music, clip, volume, true);
#ifndef HGE_LIBRETRO
    SDL_UnlockAudioDevice(g_dev);
#endif
}

void StopMusic()
{
#ifndef HGE_LIBRETRO
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
#endif
    g_music.active = false;
    g_musicClip.store(nullptr, std::memory_order_relaxed);
#ifndef HGE_LIBRETRO
    SDL_UnlockAudioDevice(g_dev);
#endif
}

void StopEffects()
{
#ifndef HGE_LIBRETRO
    if (legacyLocked()) {                 // A/B: pre-fix path
        if (!g_dev) return;
        SDL_LockAudioDevice(g_dev);
        for (int i = 0; i < kVoices; ++i) g_voices[i].active = false;
        g_cmdHead.store(0, std::memory_order_relaxed);
        g_cmdTail.store(0, std::memory_order_relaxed);
        SDL_UnlockAudioDevice(g_dev);
        return;
    }
#endif
    // Consumed at the next callback; do not lock the audio device from the
    // game thread during stage transitions.
    g_stopFx.store(true, std::memory_order_release);
}

void SetMusicVolume(float v)
{
    g_musicVol = v;
#ifndef HGE_LIBRETRO
    if (!g_dev) return;
    SDL_LockAudioDevice(g_dev);
#endif
    g_music.vol = v;
#ifndef HGE_LIBRETRO
    SDL_UnlockAudioDevice(g_dev);
#endif
}

void SetMuted(bool on) { g_muted.store(on, std::memory_order_relaxed); }
bool Muted()           { return g_muted.load(std::memory_order_relaxed); }

} // namespace audio
