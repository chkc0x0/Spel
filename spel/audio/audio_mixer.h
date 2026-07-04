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

spel_api void spel_audio_voice_reverb_set(spel_audio_voice voice, float decay,
										  float damping, float preDelayMs, float mix);

#define SPEL_AUDIO_CUSTOM_PARAM_COUNT 4

typedef struct
{
	const float* params;
	uint32_t num_params;
	void* user_data;
} spel_audio_custom_effect_ctx;

typedef void (*spel_audio_effect_fn)(float* samples, uint32_t frameCount,
									 uint32_t channels, uint32_t sampleRate,
									 spel_audio_custom_effect_ctx* ctx);
spel_api int spel_audio_voice_effect_add(spel_audio_voice voice,
										 spel_audio_effect_fn callback, void* userData);

spel_api void spel_audio_voice_effect_remove(spel_audio_voice voice, int slot);

spel_api void spel_audio_voice_effect_param_set(spel_audio_voice voice, int slot,
												uint32_t paramIndex, float value);

spel_api void spel_audio_voice_pitch_set(spel_audio_voice voice, float pitch);

spel_api void spel_audio_master_limiter_set(float thresholdDb, float attackMs,
											float releaseMs);
spel_api void spel_audio_master_compressor_set(float thresholdDb, float ratio,
											   float attackMs, float releaseMs);
spel_api bool spel_audio_master_limiter_enabled(void);
spel_api bool spel_audio_master_compressor_enabled(void);

spel_hidden void spel_audio_cleanup(void);

spel_api void spel_audio_bus_volume_set(uint32_t busId, float volume);
spel_api void spel_audio_bus_mute_set(uint32_t busId, bool mute);
spel_api void spel_audio_bus_solo_set(uint32_t busId, bool solo);
spel_api uint32_t spel_audio_bus_find(const char* name);
spel_api uint32_t spel_audio_bus_count(void);

spel_api void spel_audio_voice_bus_set(spel_audio_voice voice, uint32_t busId);
spel_api uint32_t spel_audio_voice_bus(spel_audio_voice voice);

#endif
