#ifndef SPEL_AUDIO_MIXER
#define SPEL_AUDIO_MIXER
#include "audio/audio_types.h"
#include "core/macros.h"

spel_api spel_audio_voice spel_audio_voice_create(spel_audio_source source);
spel_api spel_audio_voice
spel_audio_voice_create_from_desc(const spel_audio_source_desc* desc);
spel_api spel_audio_voice spel_audio_voice_load(const char* path);
spel_api void spel_audio_voice_destroy(spel_audio_voice voice);

spel_api void spel_audio_voice_play(spel_audio_voice voice);
spel_api void spel_audio_voice_stop(spel_audio_voice voice);
spel_api void spel_audio_voice_pause(spel_audio_voice voice);
spel_api bool spel_audio_voice_playing(spel_audio_voice voice);
spel_api bool spel_audio_voice_done(spel_audio_voice voice);

spel_api void spel_audio_voice_volume_set(spel_audio_voice voice, float volume);
spel_api float spel_audio_voice_volume(spel_audio_voice voice);
spel_api void spel_audio_voice_pan_set(spel_audio_voice voice, float pan);
spel_api float spel_audio_voice_pan(spel_audio_voice voice);
spel_api void spel_audio_voice_looping_set(spel_audio_voice voice, bool loop);
spel_api bool spel_audio_voice_looping(spel_audio_voice voice);

// create and play immediately, auto-freed when done
spel_api spel_audio_voice spel_audio_play(spel_audio_source source, bool loop);

spel_api void spel_audio_voice_distortion_set(spel_audio_voice voice, float drive);
spel_api void spel_audio_voice_lpf_set(spel_audio_voice voice, float cutoffHz);
spel_api void spel_audio_voice_hpf_set(spel_audio_voice voice, float cutoffHz);

spel_api void spel_audio_voice_delay_set(spel_audio_voice voice, float delayMs,
										 float feedback, float mix);
spel_api void spel_audio_voice_flanger_set(spel_audio_voice voice, float rateHz,
										   float depthMs, float mix);
spel_api void spel_audio_voice_chorus_set(spel_audio_voice voice, float rateHz,
										  float depthMs, float mix, int voices);

spel_hidden void spel_audio_cleanup(void);

#endif
