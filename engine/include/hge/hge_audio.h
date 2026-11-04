#ifndef BBL_AUDIO_H
#define BBL_AUDIO_H

// Minimal mixer on top of SDL2 audio: a fixed voice pool for SFX plus one
// looping music stream. PCM is already resampled at bake time; the mixer only
// does linear interpolation when a clip's rate differs from the device rate.

#include "hge_types.h"

namespace audio {

struct Clip {
    const i16* pcm;
    int        frames;
    int        channels;
    int        rate;
};

bool Init();
void Shutdown();
const char* Info();

void Play(const Clip& clip, float volume);         // one-shot SFX
void PlayMusic(const Clip& clip, float volume);    // loops
void StopMusic();
void StopEffects();                                // silence SFX, keep music
void SetMusicVolume(float v);

// Dev switch: keep the device (and the callback diagnostics) alive but emit
// silence. Set with --mute or HGE_AUDIO_MUTE=1; survives Init().
void SetMuted(bool on);
bool Muted();

// Main-thread diagnostic heartbeat (no-op in libretro builds).
void Tick();

// Pull model used by the libretro core: mix `frames` stereo (or `channels`)
// samples into `out`. Safe to call on the same thread as Play().
void Fill(i16* out, int frames, int channels);

} // namespace audio

#endif // BBL_AUDIO_H
