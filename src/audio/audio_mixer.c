#include "audio/audio_mixer.h"
#include "audio/audio_internal.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/types.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

static int find_free_slot(spel_audio_mixer_t* mixer)
{
	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		if (!mixer->voices[i].active)
		{
			return i;
		}
	}
	return -1;
}

static void compute_pan_factors(float pan, float* l, float* r)
{
	if (pan <= 0.0F)
	{
		*l = 1.0F;
	}
	else
	{
		*l = 1.0F - pan;
	}

	if (pan >= 0.0F)
	{
		*r = 1.0F;
	}
	else
	{
		*r = 1.0F + pan;
	}
}

spel_api spel_audio_voice spel_audio_voice_create(spel_audio_source source)
{
	if (!source)
	{
		spel_error(SPEL_ERR_INVALID_ARGUMENT, "audio source is NULL");
		return NULL;
	}

	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	if (!state)
	{
		spel_error(SPEL_ERR_INVALID_STATE, "audio not initialized");
		return NULL;
	}

	spel_audio_source_t* src = (spel_audio_source_t*)source;

	int slot = find_free_slot(&state->mixer);
	if (slot < 0)
	{
		spel_warn("no free audio voice slots (max %d)", SPEL_AUDIO_MAX_VOICES);
		return NULL;
	}

	spel_audio_voice_t* v = &state->mixer.voices[slot];
	memset(v, 0, sizeof(*v));

	v->decoder = (ma_decoder*)spel_memory_malloc(sizeof(ma_decoder), SPEL_MEM_TAG_AUDIO);
	if (!v->decoder)
	{
		return NULL;
	}

	ma_decoder_config dec_cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
	ma_result result = ma_decoder_init_file(src->path, &dec_cfg, v->decoder);
	if (result != MA_SUCCESS)
	{
		spel_error(SPEL_ERR_INTERNAL, "failed to open decoder: %s",
				   ma_result_description(result));
		spel_memory_free(v->decoder);
		v->decoder = NULL;
		return NULL;
	}

	v->volume = 1.0F;
	v->pan_l = 1.0F;
	v->pan_r = 1.0F;
	v->active = true;
	v->playing = false;
	v->looping = false;
	v->fire_forget = false;
	v->done = false;

	return (spel_audio_voice)v;
}

spel_api void spel_audio_voice_destroy(spel_audio_voice voice)
{
	if (!voice)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	v->playing = false;

	if (v->decoder)
	{
		ma_decoder_uninit(v->decoder);
		spel_memory_free(v->decoder);
		v->decoder = NULL;
	}

	memset(v, 0, sizeof(*v));
}

spel_api void spel_audio_voice_play(spel_audio_voice voice)
{
	if (!voice)
	{
		return;
	}
	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	v->done = false;
	v->playing = true;
}

spel_api void spel_audio_voice_stop(spel_audio_voice voice)
{
	if (!voice)
	{
		return;
	}
	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	v->playing = false;

	if (v->decoder)
	{
		ma_decoder_seek_to_pcm_frame(v->decoder, 0);
	}
}

spel_api bool spel_audio_voice_playing(spel_audio_voice voice)
{
	if (!voice)
	{
		return false;
	}
	return ((spel_audio_voice_t*)voice)->playing;
}

spel_api bool spel_audio_voice_done(spel_audio_voice voice)
{
	if (!voice)
	{
		return false;
	}
	return ((spel_audio_voice_t*)voice)->done;
}

spel_api void spel_audio_voice_volume_set(spel_audio_voice voice, float volume)
{
	if (!voice)
	{
		return;
	}
	((spel_audio_voice_t*)voice)->volume = volume;
}

spel_api float spel_audio_voice_volume_get(spel_audio_voice voice)
{
	if (!voice)
	{
		return 0.0F;
	}
	return ((spel_audio_voice_t*)voice)->volume;
}

spel_api void spel_audio_voice_pan_set(spel_audio_voice voice, float pan)
{
	if (!voice)
	{
		return;
	}
	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	v->pan = pan;
	compute_pan_factors(pan, &v->pan_l, &v->pan_r);
}

spel_api float spel_audio_voice_pan(spel_audio_voice voice)
{
	if (!voice)
	{
		return 0.0F;
	}
	return ((spel_audio_voice_t*)voice)->pan;
}

spel_api void spel_audio_voice_looping_set(spel_audio_voice voice, bool loop)
{
	if (!voice)
	{
		return;
	}
	((spel_audio_voice_t*)voice)->looping = loop;
}

spel_api bool spel_audio_voice_looping(spel_audio_voice voice)
{
	if (!voice)
	{
		return false;
	}
	return ((spel_audio_voice_t*)voice)->looping;
}

spel_api spel_audio_voice spel_audio_play(spel_audio_source source, bool loop)
{
	spel_audio_voice v = spel_audio_voice_create(source);
	if (!v)
	{
		return NULL;
	}

	spel_audio_voice_t* vp = (spel_audio_voice_t*)v;
	vp->looping = loop;
	vp->fire_forget = true;
	vp->playing = true;

	return v;
}

void spel_audio_mixer_process(spel_audio_mixer_t* mixer, float* output,
							  ma_uint32 frameCount, uint32_t channels, float* scratch)
{
	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		spel_audio_voice_t* v = &mixer->voices[i];

		if (!v->active || !v->playing || !v->decoder)
		{
			continue;
		}

		memset(scratch, 0, (__ssize_t)(frameCount * channels) * sizeof(float));

		ma_uint64 frames_read = 0;
		ma_result result =
			ma_decoder_read_pcm_frames(v->decoder, scratch, frameCount, &frames_read);

		if (result != MA_SUCCESS || frames_read == 0)
		{
			v->playing = false;
			v->done = true;
			continue;
		}

		float vol = v->volume;
		float l = vol * v->pan_l;
		float r = vol * v->pan_r;

		if (channels == 1)
		{
			for (ma_uint32 f = 0; f < frames_read; f++)
			{
				output[f] += scratch[f] * vol;
			}
		}
		else
		{
			for (ma_uint32 f = 0; f < frames_read; f++)
			{
				output[(size_t)(f * 2)] += scratch[(size_t)(f * 2)] * l;
				output[(f * 2) + 1] += scratch[(f * 2) + 1] * r;
			}
		}

		if (frames_read < frameCount)
		{
			if (v->looping)
			{
				ma_decoder_seek_to_pcm_frame(v->decoder, 0);
			}
			else
			{
				v->playing = false;
				v->done = true;
			}
		}
	}
}

spel_hidden void spel_audio_cleanup(void)
{
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	if (!state)
	{
		return;
	}

	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		spel_audio_voice_t* v = &state->mixer.voices[i];
		if (v->fire_forget && v->done)
		{
			spel_audio_voice_destroy((spel_audio_voice)v);
		}
	}
}
