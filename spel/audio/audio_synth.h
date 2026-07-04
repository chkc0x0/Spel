#ifndef SPEL_AUDIO_SYNTH
#define SPEL_AUDIO_SYNTH
#include "audio/audio_types.h"
#include "core/macros.h"

#define SPEL_AUDIO_SYNTH_PARAM_COUNT 4

typedef enum
{
	SPEL_AUDIO_SYNTH_WAVE_SINE,
	SPEL_AUDIO_SYNTH_WAVE_SQUARE,
	SPEL_AUDIO_SYNTH_WAVE_SAW,
	SPEL_AUDIO_SYNTH_WAVE_TRIANGLE,
	SPEL_AUDIO_SYNTH_WAVE_NOISE,
	SPEL_AUDIO_SYNTH_WAVE_PULSE,
	SPEL_AUDIO_SYNTH_WAVE_CUSTOM,
} spel_audio_synth_waveform;

typedef struct
{
	float attack;
	float decay;
	float sustain;
	float release;
} spel_audio_synth_envelope;

typedef struct spel_audio_synth_t* spel_audio_synth;

spel_api spel_audio_synth
spel_audio_synth_create(spel_audio_synth_waveform wave);

spel_api void
spel_audio_synth_destroy(spel_audio_synth sv);

spel_api spel_audio_voice
spel_audio_synth_voice_get(spel_audio_synth sv);

spel_api void
spel_audio_synth_note_on(spel_audio_synth sv,
							   float frequency, float velocity);

spel_api void
spel_audio_synth_note_off(spel_audio_synth sv);

spel_api bool
spel_audio_synth_note_active(spel_audio_synth sv);

spel_api void
spel_audio_synth_note(spel_audio_synth sv,
							float frequency, float velocity,
							float duration);

spel_api void
spel_audio_synth_envelope_set(spel_audio_synth sv,
									const spel_audio_synth_envelope* env);

spel_api const spel_audio_synth_envelope*
spel_audio_synth_envelope_get(spel_audio_synth sv);

spel_api void
spel_audio_synth_envelope_default(spel_audio_synth sv);

spel_api void
spel_audio_synth_param_set(spel_audio_synth sv,
								 uint32_t index, float value);

spel_api float
spel_audio_synth_param_get(spel_audio_synth sv, uint32_t index);

typedef float (*spel_audio_synth_generator_fn)(float phase,
	const float* params, uint32_t numParams, void* userData);

spel_api void
spel_audio_synth_generator_set(spel_audio_synth sv,
	spel_audio_synth_generator_fn fn, void* userData);

spel_api void
spel_audio_synth_detune_set(spel_audio_synth sv, float cents);

spel_api float
spel_audio_synth_detune_get(spel_audio_synth sv);

spel_api float
spel_audio_synth_frequency_get(spel_audio_synth sv);

#endif
