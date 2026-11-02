#include "hge_audio.h"

#include <SDL.h>
#include <cstdio>
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

SDL_AudioDeviceID g_dev = 0;
SDL_AudioSpec     g_spec;
float             g_musicVol = 0.45f;
char              g_info[160] = "audio: not initialised";

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

// Peak limiter: mix in 32-bit, then scale the whole buffer down only if it
// would clip. Leaves headroom for the audioserver/frontend so loud overlaps
// (BGM + several SFX) no longer crackle.
void toInt16(const int* mix, i16* out, int n)
{
    // Fixed headroom keeps us away from the audioserver/amp rails; the peak
    // limiter below is then a safety net that only engages on loud overlaps.
    const float kMaster = 0.70f;
    const int   kCeil   = 28000;

    int peak = 0;
    for (int i = 0; i < n; ++i) {
        int a = mix[i]; if (a < 0) a = -a;
        if (a > peak) peak = a;
    }
    float g = kMaster;
    if ((int)((float)peak * g) > kCeil) g = (float)kCeil / (float)peak;
    for (int i = 0; i < n; ++i) {
        int sv = (int)(mix[i] * g);
        if (sv > 32767) sv = 32767;
        else if (sv < -32768) sv = -32768;
        out[i] = (i16)sv;
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
    static std::vector<int> mix;
    mix.resize((size_t)frames * channels);
    renderMix(mix.data(), frames, channels);
    toInt16(mix.data(), out, frames * channels);
}

bool Init()
{
#ifdef HGE_LIBRETRO
    // The frontend owns the audio device; we only mix into its buffer.
    memset(&g_spec, 0, sizeof(g_spec));
    g_spec.freq = 44100;
    g_spec.channels = 2;
    g_spec.samples = 1024;
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
    want.samples  = 1024;
    want.callback = callback;
    want.userdata = nullptr;

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
    SDL_PauseAudioDevice(g_dev, 0);
    snprintf(g_info, sizeof(g_info), "audio: %d Hz, %d ch, %d voices",
             g_spec.freq, g_spec.channels, kVoices);
    return true;
}

void Shutdown()
{
    for (int i = 0; i < kVoices; ++i) g_voices[i].active = false;
    g_music.active = false;
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
}

const char* Info() { return g_info; }

void Play(const Clip& clip, float volume)
{
#ifdef HGE_LIBRETRO
    if (!clip.pcm) return;
#else
    if (!g_dev || !clip.pcm) return;
    SDL_LockAudioDevice(g_dev);
#endif
    int slot = -1;
    for (int i = 0; i < kVoices; ++i)
        if (!g_voices[i].active) { slot = i; break; }
    if (slot < 0) slot = 0;   // steal the oldest
    startVoice(g_voices[slot], clip, volume, false);
#ifndef HGE_LIBRETRO
    SDL_UnlockAudioDevice(g_dev);
#endif
}

void PlayMusic(const Clip& clip, float volume)
{
#ifdef HGE_LIBRETRO
    if (!clip.pcm) return;
#else
    if (!g_dev || !clip.pcm) return;
    SDL_LockAudioDevice(g_dev);
#endif
    g_musicVol = volume;
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
#ifndef HGE_LIBRETRO
    SDL_UnlockAudioDevice(g_dev);
#endif
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

} // namespace audio
