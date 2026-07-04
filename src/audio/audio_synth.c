#include "audio/audio_synth.h"
#include "audio/audio_internal.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/types.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef enum
{
	SPEL_SYNTH_ENV_IDLE,
	SPEL_SYNTH_ENV_ATTACK,
	SPEL_SYNTH_ENV_DECAY,
	SPEL_SYNTH_ENV_SUSTAIN,
	SPEL_SYNTH_ENV_RELEASE,
	SPEL_SYNTH_ENV_DONE,
} spel_synth_env_state_t;

typedef struct
{
	spel_audio_synth_waveform wave_type;
	bool note_active;
	float frequency;
	float velocity;
	float params[SPEL_AUDIO_SYNTH_PARAM_COUNT];
	float detune;

	spel_audio_synth_generator_fn custom_generator;
	void* custom_user_data;

	float note_duration;
	spel_audio_synth_envelope env_cfg;

	float phase;
	float elapsed;
	unsigned int rng_state;
	spel_synth_env_state_t env_state;
	float env_level;
	float attack_rate;
	float decay_rate;
	float release_rate;
	float sustain_level;

	uint32_t sample_rate;
	uint32_t channels;

	spel_audio_voice audio_voice;
} spel_synth_t;

static void reset_envelope_rates(spel_synth_t* sv);

static float gen_sine(float phase)
{
	return sinf(2.0F * (float)M_PI * phase);
}

static float gen_square(float phase, float pulseWidth)
{
	return phase < pulseWidth ? 1.0F : -1.0F;
}

static float gen_saw(float phase)
{
	return (2.0F * phase) - 1.0F;
}

static float gen_triangle(float phase)
{
	return (4.0F * fabsf(phase - 0.5F)) - 1.0F;
}

static float gen_noise(unsigned int* rng)
{
	unsigned int s = *rng;
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	*rng = s;
	return (float)((int)s) * (1.0F / 2147483648.0F);
}

static size_t synth_read_cb(void* userCtx, void* output, size_t count)
{
	spel_synth_t* sv = (spel_synth_t*)userCtx;
	uint32_t channels = sv->channels;
	uint32_t sr = sv->sample_rate;
	float* out = (float*)output;

	uint32_t frames = (uint32_t)(count / (sizeof(float) * channels));
	if (frames == 0)
	{
		return 0;
	}

	float inv_sr = 1.0F / (float)sr;
	float phase_inc = sv->frequency * inv_sr;

	float detune_inc = 0.0F;
	if (sv->detune != 0.0F)
	{
		detune_inc = sv->frequency * (powf(2.0F, sv->detune / 1200.0F) - 1.0F) * inv_sr;
	}

	bool note_active = sv->note_active;
	float velocity = sv->velocity;
	float pulse_width = sv->params[0];
	if (pulse_width < 0.01F)
	{
		pulse_width = 0.01F;
	}
	if (pulse_width > 0.99F)
	{
		pulse_width = 0.99F;
	}

	uint32_t f;

	for (f = 0; f < frames; f++)
	{
		float sample;

		if (!note_active)
		{
			goto synth_silence;
		}

		switch (sv->env_state)
		{
		case SPEL_SYNTH_ENV_ATTACK:
			sv->env_level += sv->attack_rate;
			if (sv->env_level >= 1.0F)
			{
				sv->env_level = 1.0F;
				sv->env_state = SPEL_SYNTH_ENV_DECAY;
			}
			break;

		case SPEL_SYNTH_ENV_DECAY:
			sv->env_level -= sv->decay_rate;
			if (sv->env_level <= sv->sustain_level)
			{
				sv->env_level = sv->sustain_level;
				sv->env_state = SPEL_SYNTH_ENV_SUSTAIN;
			}
			break;

		case SPEL_SYNTH_ENV_SUSTAIN:
			if (sv->note_duration > 0.0F)
			{
				sv->elapsed += inv_sr;
				if (sv->elapsed >= sv->note_duration)
				{
					sv->env_state = SPEL_SYNTH_ENV_RELEASE;
				}
			}
			break;

		case SPEL_SYNTH_ENV_RELEASE:
			sv->env_level -= sv->release_rate;
			if (sv->env_level <= 0.0F)
			{
				sv->env_level = 0.0F;
				sv->env_state = SPEL_SYNTH_ENV_DONE;
				goto synth_silence;
			}
			break;

		case SPEL_SYNTH_ENV_IDLE:
		case SPEL_SYNTH_ENV_DONE:
		default:
			goto synth_silence;
		}

		{
			float gen_phase = sv->phase;

			switch (sv->wave_type)
			{
			case SPEL_AUDIO_SYNTH_WAVE_SINE:
				sample = gen_sine(gen_phase);
				break;

			case SPEL_AUDIO_SYNTH_WAVE_SQUARE:
				sample = gen_square(gen_phase, 0.5F);
				break;

			case SPEL_AUDIO_SYNTH_WAVE_PULSE:
				sample = gen_square(gen_phase, pulse_width);
				break;

			case SPEL_AUDIO_SYNTH_WAVE_SAW:
				sample = gen_saw(gen_phase);
				break;

			case SPEL_AUDIO_SYNTH_WAVE_TRIANGLE:
				sample = gen_triangle(gen_phase);
				break;

			case SPEL_AUDIO_SYNTH_WAVE_NOISE:
				sample = gen_noise(&sv->rng_state);
				break;

			case SPEL_AUDIO_SYNTH_WAVE_CUSTOM:
				if (sv->custom_generator)
				{
					sample = sv->custom_generator(gen_phase, sv->params,
												  SPEL_AUDIO_SYNTH_PARAM_COUNT,
												  sv->custom_user_data);
				}
				else
				{
					sample = 0.0F;
				}
				break;
			}

			sample *= sv->env_level * velocity;
		}

		for (uint32_t ch = 0; ch < channels; ch++)
		{
			out[(f * channels) + ch] = sample;
		}

		sv->phase += phase_inc + detune_inc;
		if (sv->phase >= 1.0F)
		{
			sv->phase -= 1.0F;
		}

		continue;

	synth_silence:
		for (uint32_t ch = 0; ch < channels; ch++)
		{
			out[(f * channels) + ch] = 0.0F;
		}

		if (f > 0)
		{
			return sizeof(float) * (size_t)f * channels;
		}
		return 0;
	}

	return sizeof(float) * (size_t)frames * channels;
}

static int synth_seek_cb(void* userCtx, int64_t offset, int whence)
{
	spel_synth_t* sv = (spel_synth_t*)userCtx;

	if (offset == 0 && whence == SEEK_SET)
	{
		sv->phase = 0.0F;
		sv->elapsed = 0.0F;
		sv->env_state = SPEL_SYNTH_ENV_IDLE;
		sv->env_level = 0.0F;
		return 0;
	}
	return -1;
}

static void reset_envelope_rates(spel_synth_t* sv)
{
	float sr = (float)sv->sample_rate;
	float att = sv->env_cfg.attack;
	float dec = sv->env_cfg.decay;
	float rel = sv->env_cfg.release;

	sv->sustain_level = sv->env_cfg.sustain;
	sv->attack_rate = (att > 0.0F) ? (1.0F / (att * sr)) : 1.0F;
	sv->decay_rate = (dec > 0.0F) ? ((1.0F - sv->sustain_level) / (dec * sr))
								  : (1.0F - sv->sustain_level);
	sv->release_rate =
		(rel > 0.0F) ? (sv->sustain_level / (rel * sr)) : sv->sustain_level;
}

spel_api spel_audio_synth spel_audio_synth_create(spel_audio_synth_waveform wave)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		spel_error(SPEL_ERR_INVALID_STATE, "audio not initialised");
		return NULL;
	}

	spel_synth_t* sv =
		(spel_synth_t*)spel_memory_malloc(sizeof(spel_synth_t), SPEL_MEM_TAG_AUDIO);
	if (!sv)
	{
		return NULL;
	}
	memset(sv, 0, sizeof(*sv));

	sv->wave_type = wave;
	sv->sample_rate = state->sample_rate;
	sv->channels = state->channels;
	sv->note_duration = -1.0F;
	sv->params[0] = 0.5F;
	sv->rng_state = 12345U;

	spel_audio_synth_envelope_default((spel_audio_synth)sv);

	reset_envelope_rates(sv);

	spel_audio_source_desc desc;
	desc.type = SPEL_AUDIO_SOURCE_CALLBACKS;
	desc.source.callbacks.on_read = synth_read_cb;
	desc.source.callbacks.on_seek = synth_seek_cb;
	desc.source.callbacks.user_context = sv;

	spel_audio_voice av = spel_audio_voice_create_from_desc(&desc);
	if (!av)
	{
		spel_error(SPEL_ERR_INTERNAL, "failed to create mixer voice for synth");
		spel_memory_free(sv);
		return NULL;
	}

	sv->audio_voice = av;
	return (spel_audio_synth)sv;
}

spel_api void spel_audio_synth_destroy(spel_audio_synth sv)
{
	if (!sv)
	{
		return;
	}
	spel_synth_t* s = (spel_synth_t*)sv;

	spel_audio_voice_destroy(s->audio_voice);
	spel_memory_free(s);
}

spel_api spel_audio_voice spel_audio_synth_voice_get(spel_audio_synth sv)
{
	if (!sv)
	{
		return NULL;
	}
	return ((spel_synth_t*)sv)->audio_voice;
}

spel_api void spel_audio_synth_note_on(spel_audio_synth sv, float frequency,
									   float velocity)
{
	if (!sv)
	{
		return;
	}
	spel_synth_t* s = (spel_synth_t*)sv;

	s->frequency = frequency;
	s->velocity = velocity;
	s->note_active = true;

	s->phase = 0.0F;
	s->elapsed = 0.0F;
	reset_envelope_rates(s);
	s->env_state = SPEL_SYNTH_ENV_ATTACK;
	s->env_level = 0.0F;

	spel_audio_voice_t* av = (spel_audio_voice_t*)s->audio_voice;
	if (av->decoder)
	{
		ma_decoder_seek_to_pcm_frame(av->decoder, 0);
	}
	spel_audio_voice_play(s->audio_voice);
}

spel_api void spel_audio_synth_note_off(spel_audio_synth sv)
{
	if (!sv)
	{
		return;
	}
	spel_synth_t* s = (spel_synth_t*)sv;

	if (s->env_state == SPEL_SYNTH_ENV_ATTACK || s->env_state == SPEL_SYNTH_ENV_DECAY ||
		s->env_state == SPEL_SYNTH_ENV_SUSTAIN)
	{
		s->env_state = SPEL_SYNTH_ENV_RELEASE;
	}
}

spel_api bool spel_audio_synth_note_active(spel_audio_synth sv)
{
	if (!sv)
	{
		return false;
	}
	return ((spel_synth_t*)sv)->note_active;
}

spel_api void spel_audio_synth_note(spel_audio_synth sv, float frequency, float velocity,
									float duration)
{
	if (!sv)
	{
		return;
	}
	spel_synth_t* s = (spel_synth_t*)sv;

	s->note_duration = (duration > 0.0F) ? duration : -1.0F;

	spel_audio_synth_note_on(sv, frequency, velocity);
	s->note_duration = (duration > 0.0F) ? duration : -1.0F;
}

spel_api void spel_audio_synth_envelope_set(spel_audio_synth sv,
											const spel_audio_synth_envelope* env)
{
	if (!sv || !env)
	{
		return;
	}
	spel_synth_t* s = (spel_synth_t*)sv;
	s->env_cfg = *env;
	reset_envelope_rates(s);
}

const spel_api spel_audio_synth_envelope* spel_audio_synth_envelope_get(
	spel_audio_synth sv)
{
	if (!sv)
	{
		return NULL;
	}
	return &((spel_synth_t*)sv)->env_cfg;
}

spel_api void spel_audio_synth_envelope_default(spel_audio_synth sv)
{
	if (!sv)
	{
		return;
	}
	spel_synth_t* s = (spel_synth_t*)sv;
	s->env_cfg.attack = 0.05F;
	s->env_cfg.decay = 0.05F;
	s->env_cfg.sustain = 0.7F;
	s->env_cfg.release = 0.2F;
	reset_envelope_rates(s);
}

spel_api void spel_audio_synth_param_set(spel_audio_synth sv, uint32_t index, float value)
{
	if (!sv || index >= SPEL_AUDIO_SYNTH_PARAM_COUNT)
	{
		return;
	}
	((spel_synth_t*)sv)->params[index] = value;
}

spel_api float spel_audio_synth_param_get(spel_audio_synth sv, uint32_t index)
{
	if (!sv || index >= SPEL_AUDIO_SYNTH_PARAM_COUNT)
	{
		return 0.0F;
	}
	return ((spel_synth_t*)sv)->params[index];
}

spel_api void spel_audio_synth_generator_set(spel_audio_synth sv,
											 spel_audio_synth_generator_fn fn,
											 void* userData)
{
	if (!sv || !fn)
	{
		return;
	}
	spel_synth_t* s = (spel_synth_t*)sv;
	s->custom_generator = fn;
	s->custom_user_data = userData;
}

spel_api void spel_audio_synth_detune_set(spel_audio_synth sv, float cents)
{
	if (!sv)
	{
		return;
	}
	if (cents < -1200.0F)
	{
		cents = -1200.0F;
	}
	if (cents > 1200.0F)
	{
		cents = 1200.0F;
	}
	((spel_synth_t*)sv)->detune = cents;
}

spel_api float spel_audio_synth_detune_get(spel_audio_synth sv)
{
	if (!sv)
	{
		return 0.0F;
	}
	return ((spel_synth_t*)sv)->detune;
}

spel_api float spel_audio_synth_frequency_get(spel_audio_synth sv)
{
	if (!sv)
	{
		return 0.0F;
	}
	return ((spel_synth_t*)sv)->frequency;
}

static const char* SEMITONE_LETTERS = "CDEFGAB";
static const int8_t SEMITONE_VALUES[7] = {0, 2, 4, 5, 7, 9, 11};

static int8_t letter_to_semitone(char c)
{
	if (c >= 'a' && c <= 'g')
	{
		c -= 32;
	}
	if (c >= 'A' && c <= 'G')
	{
		for (int i = 0; i < 7; i++)
		{
			if (c == SEMITONE_LETTERS[i])
			{
				return SEMITONE_VALUES[i];
			}
		}
	}
	return -1;
}

spel_api float spel_audio_synth_note_freq(const char* name)
{
	if (!name || !*name)
	{
		return 0.0F;
	}

	int8_t semitone = letter_to_semitone(name[0]);
	if (semitone < 0)
	{
		return 0.0F;
	}
	size_t pos = 1;

	if (name[pos] == '#')
	{
		semitone++;
		pos++;
	}
	else if (name[pos] == 'b')
	{
		semitone--;
		pos++;
	}

	if (semitone < 0)
	{
		semitone += 12;
	}
	else if (semitone > 11)
	{
		semitone -= 12;
	}

	int octave = 4;
	if (name[pos] >= '0' && name[pos] <= '9')
	{
		octave = name[pos] - '0';
		pos++;
	}

	int midi = ((octave + 1) * 12) + (int)semitone;
	if (midi < 0 || midi > 127)
	{
		return 0.0F;
	}

	return 440.0F * powf(2.0F, (float)(midi - 69) * (1.0F / 12.0F));
}

typedef struct spel_audio_synth_player_t
{
	spel_audio_synth synth;
	const spel_audio_synth_sheet* sheet;

	bool playing;
	bool done;
	double elapsed_beats;

	uint32_t next_event;
	int32_t active_event;

	float bpm;
} spel_synth_player_t;

spel_api spel_audio_synth_player spel_audio_synth_player_create(
	spel_audio_synth synth, const spel_audio_synth_sheet* sheet)
{
	if (!synth || !sheet)
	{
		return NULL;
	}

	spel_synth_player_t* p = (spel_synth_player_t*)spel_memory_malloc(
		sizeof(spel_synth_player_t), SPEL_MEM_TAG_AUDIO);
	if (!p)
	{
		return NULL;
	}

	memset(p, 0, sizeof(*p));
	p->synth = synth;
	p->sheet = sheet;
	p->bpm = sheet->bpm;
	p->active_event = -1;

	return (spel_audio_synth_player)p;
}

spel_api void spel_audio_synth_player_destroy(spel_audio_synth_player player)
{
	if (!player)
	{
		return;
	}
	spel_synth_player_t* p = (spel_synth_player_t*)player;
	spel_audio_synth_note_off(p->synth);
	spel_memory_free(p);
}

spel_api void spel_audio_synth_player_play(spel_audio_synth_player player)
{
	if (!player)
	{
		return;
	}
	spel_synth_player_t* p = (spel_synth_player_t*)player;
	p->elapsed_beats = 0.0;
	p->next_event = 0;
	p->active_event = -1;
	p->playing = true;
	p->done = false;
	spel_audio_synth_note_off(p->synth);
}

spel_api void spel_audio_synth_player_stop(spel_audio_synth_player player)
{
	if (!player)
	{
		return;
	}
	spel_synth_player_t* p = (spel_synth_player_t*)player;
	p->playing = false;
	p->done = true;
	spel_audio_synth_note_off(p->synth);
}

spel_api void spel_audio_synth_player_update(spel_audio_synth_player player)
{
	if (!player)
	{
		return;
	}
	spel_synth_player_t* p = (spel_synth_player_t*)player;
	if (!p->playing || p->done)
	{
		return;
	}

	p->elapsed_beats += spel.time.delta_unscaled * (double)(p->bpm / 60.0F);

	const spel_audio_synth_sheet* sh = p->sheet;
	uint32_t count = sh->num_events;
	float eb = (float)p->elapsed_beats;

	while (p->next_event < count)
	{
		const spel_audio_synth_event* ev = &sh->events[p->next_event];
		if (ev->beat > eb)
		{
			break;
		}

		float freq = 440.0F * powf(2.0F, (float)(ev->midi_note - 69) * (1.0F / 12.0F));

		float dur = ev->duration;
		if (dur <= 0.0F)
		{
			dur = 0.05F;
		}

		spel_audio_synth_note(p->synth, freq, ev->velocity, dur);

		p->active_event = (int32_t)p->next_event;
		p->next_event++;
	}

	if (p->next_event >= count && p->active_event < 0)
	{
		p->done = true;
		p->playing = false;
	}
}

spel_api bool spel_audio_synth_player_done(spel_audio_synth_player player)
{
	if (!player)
	{
		return true;
	}
	return ((spel_synth_player_t*)player)->done;
}

spel_api void spel_audio_synth_player_seek(spel_audio_synth_player player, float beat)
{
	if (!player)
	{
		return;
	}
	spel_synth_player_t* p = (spel_synth_player_t*)player;

	p->elapsed_beats = (double)beat;
	p->playing = false;
	p->done = false;
	spel_audio_synth_note_off(p->synth);

	p->next_event = 0;
	p->active_event = -1;
	const spel_audio_synth_sheet* sh = p->sheet;
	for (uint32_t i = 0; i < sh->num_events; i++)
	{
		if (sh->events[i].beat >= beat)
		{
			p->next_event = i;
			break;
		}
	}
}

spel_api float spel_audio_synth_player_position_get(spel_audio_synth_player player)
{
	if (!player)
	{
		return 0.0F;
	}
	return (float)((spel_synth_player_t*)player)->elapsed_beats;
}

spel_api uint32_t spel_audio_synth_player_event_count(spel_audio_synth_player player)
{
	if (!player)
	{
		return 0;
	}
	spel_synth_player_t* p = (spel_synth_player_t*)player;
	return p->sheet ? p->sheet->num_events : 0;
}

spel_api void spel_audio_synth_player_bpm_set(spel_audio_synth_player player, float bpm)
{
	if (!player || bpm < 1.0F)
	{
		return;
	}
	((spel_synth_player_t*)player)->bpm = bpm;
}
