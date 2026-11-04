# One-shot audio/mixer snapshot for an attached bubble_level1.
# Sourced by tools/attach_dump.sh. Symbols come from a RelWithDebInfo build.
# Names are single-quoted because the mixer statics live in an anonymous namespace.

echo \n===== audio mixer state =====\n
print 'audio::(anonymous namespace)::g_cbCount'
print 'audio::(anonymous namespace)::g_activeVoices'
print 'audio::(anonymous namespace)::g_musicActive'
print 'audio::(anonymous namespace)::g_peak'
print 'audio::(anonymous namespace)::g_dev'
print 'audio::(anonymous namespace)::g_stopFx'
print 'audio::(anonymous namespace)::g_cmdHead'
print 'audio::(anonymous namespace)::g_cmdTail'
print 'audio::(anonymous namespace)::g_music'

echo \n===== SDL audio device status (0=stopped 1=playing 2=paused) =====\n
print (int)SDL_GetAudioDeviceStatus((unsigned int)'audio::(anonymous namespace)::g_dev')
