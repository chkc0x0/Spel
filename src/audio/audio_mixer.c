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
		if (!atomic_load_explicit(&mixer->voices[i].active, memory_order_relaxed)
			&& mixer->voices[i].decoder == NULL)
		{
			return i;
		}
	}
	return -1;
}

static int steal_oldest_voice(spel_audio_mixer_t* mixer)
{
	int         oldest_idx = -1;
	uint32_t    oldest_frame = UINT32_MAX;

	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		spel_audio_voice_t* v = &mixer->voices[i];

		if (!atomic_load_explicit(&v->active, memory_order_acquire))
		{
			continue;
		}
		if (!atomic_load_explicit(&v->playing, memory_order_acquire))
		{
			continue;
		}

		uint32_t start = atomic_load_explicit(&v->start_frame, memory_order_acquire);
		if (start < oldest_frame)
		{
			oldest_frame = start;
			oldest_idx = i;
		}
	}

	return oldest_idx;
}

static int voice_index(spel_audio_state_t* state, spel_audio_voice voice)
{
	if (!state || !voice)
	{
		return -1;
	}
	int idx = (int)((spel_audio_voice_t*)voice - state->mixer.voices);
	if (idx < 0 || idx >= SPEL_AUDIO_MAX_VOICES)
	{
		return -1;
	}
	return idx;
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
		slot = steal_oldest_voice(&state->mixer);
		if (slot < 0)
		{
			spel_warn("no free audio voice slots and no voice to steal (max %d)",
					  SPEL_AUDIO_MAX_VOICES);
			return NULL;
		}

		spel_audio_voice_t* old = &state->mixer.voices[slot];

		atomic_store_explicit(&old->playing, false, memory_order_release);
		atomic_store_explicit(&old->active, false, memory_order_release);

		if (old->decoder)
		{
			ma_decoder_uninit(old->decoder);
			spel_memory_free(old->decoder);
			old->decoder = NULL;
		}

		memset(old, 0, sizeof(*old));

		/* Now treat the slot as fresh — fall through to init */
		slot = (int)(old - state->mixer.voices);
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

	v->volume   = 1.0F;
	v->pan_l    = 1.0F;
	v->pan_r    = 1.0F;

	atomic_store_explicit(&v->active,  false, memory_order_release);
	atomic_store_explicit(&v->playing, false, memory_order_release);
	atomic_store_explicit(&v->done,    false, memory_order_release);
	atomic_store_explicit(&v->looping, false, memory_order_release);
	v->fire_forget = false;

	atomic_store_explicit(&v->active, true, memory_order_release);

	return (spel_audio_voice)v;
}

spel_api void spel_audio_voice_destroy(spel_audio_voice voice)
{
	if (!voice)
	{
		return;
	}

	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	if (!state)
	{
		return;
	}

	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type        = SPEL_AUDIO_CMD_DESTROY;
	cmd.voice_index = idx;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping destroy for voice %d", idx);
	}
}

spel_api void spel_audio_voice_play(spel_audio_voice voice)
{
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type        = SPEL_AUDIO_CMD_PLAY;
	cmd.voice_index = idx;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping PLAY for voice %d", idx);
	}
}

spel_api void spel_audio_voice_stop(spel_audio_voice voice)
{
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type        = SPEL_AUDIO_CMD_STOP;
	cmd.voice_index = idx;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping STOP for voice %d", idx);
	}
}

spel_api bool spel_audio_voice_playing(spel_audio_voice voice)
{
	if (!voice)
	{
		return false;
	}
	return atomic_load_explicit(&((spel_audio_voice_t*)voice)->playing, memory_order_acquire);
}

spel_api bool spel_audio_voice_done(spel_audio_voice voice)
{
	if (!voice)
	{
		return false;
	}
	return atomic_load_explicit(&((spel_audio_voice_t*)voice)->done, memory_order_acquire);
}

spel_api void spel_audio_voice_volume_set(spel_audio_voice voice, float volume)
{
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type        = SPEL_AUDIO_CMD_VOLUME;
	cmd.voice_index = idx;
	cmd.float_value = volume;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping VOLUME for voice %d", idx);
	}
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
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type        = SPEL_AUDIO_CMD_PAN;
	cmd.voice_index = idx;
	cmd.float_value = pan;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping PAN for voice %d", idx);
	}
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
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type        = SPEL_AUDIO_CMD_LOOP;
	cmd.voice_index = idx;
	cmd.bool_value  = loop;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping LOOP for voice %d", idx);
	}
}

spel_api bool spel_audio_voice_looping(spel_audio_voice voice)
{
	if (!voice)
	{
		return false;
	}
	return atomic_load_explicit(&((spel_audio_voice_t*)voice)->looping, memory_order_acquire);
}

spel_api spel_audio_voice spel_audio_play(spel_audio_source source, bool loop)
{
	spel_audio_voice v = spel_audio_voice_create(source);
	if (!v)
	{
		return NULL;
	}

	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
	int idx = voice_index(state, v);
	if (idx < 0)
	{
		return NULL;
	}

	spel_audio_voice_t* vp = (spel_audio_voice_t*)v;
	vp->fire_forget = true;

	spel_audio_cmd cmd1;
	cmd1.type        = SPEL_AUDIO_CMD_PLAY;
	cmd1.voice_index = idx;
	spel_audio_cmd_push(&state->cmd_ring, &cmd1);

	spel_audio_cmd cmd2;
	cmd2.type        = SPEL_AUDIO_CMD_LOOP;
	cmd2.voice_index = idx;
	cmd2.bool_value  = loop;
	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd2))
	{
		spel_warn("audio cmd ring full, dropping LOOP for voice %d", idx);
	}

	return v;
}

void spel_audio_mixer_process(spel_audio_mixer_t* mixer, float* output,
							  ma_uint32 frameCount, uint32_t channels, float* scratch)
{
	atomic_fetch_add_explicit(&mixer->frame_counter, 1, memory_order_relaxed);

	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		spel_audio_voice_t* v = &mixer->voices[i];

		if (!atomic_load_explicit(&v->active, memory_order_acquire)
			|| !atomic_load_explicit(&v->playing, memory_order_acquire)
			|| !v->decoder)
		{
			continue;
		}

		memset(scratch, 0, (__ssize_t)(frameCount * channels) * sizeof(float));

		ma_uint64 frames_read = 0;
		ma_result result =
			ma_decoder_read_pcm_frames(v->decoder, scratch, frameCount, &frames_read);

		if (result != MA_SUCCESS || frames_read == 0)
		{
			atomic_store_explicit(&v->playing, false, memory_order_release);
			atomic_store_explicit(&v->done, true, memory_order_release);
			continue;
		}

		float vol = v->volume;
		float l   = vol * v->pan_l;
		float r   = vol * v->pan_r;

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
				output[(size_t)(f * 2)]     += scratch[(size_t)(f * 2)]     * l;
				output[(size_t)(f * 2) + 1] += scratch[(size_t)(f * 2) + 1] * r;
			}
		}

		if (frames_read < frameCount)
		{
			if (atomic_load_explicit(&v->looping, memory_order_relaxed))
			{
				ma_decoder_seek_to_pcm_frame(v->decoder, 0);
			}
			else
			{
				atomic_store_explicit(&v->playing, false, memory_order_release);
				atomic_store_explicit(&v->done, true, memory_order_release);
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

		if (v->fire_forget
			&& atomic_load_explicit(&v->done, memory_order_acquire))
		{
			if (v->decoder)
			{
				ma_decoder_uninit(v->decoder);
				spel_memory_free(v->decoder);
				v->decoder = NULL;
			}
			memset(v, 0, sizeof(*v));
			continue;
		}

		if (!atomic_load_explicit(&v->active, memory_order_acquire)
			&& v->decoder != NULL)
		{
			if (v->decoder)
			{
				ma_decoder_uninit(v->decoder);
				spel_memory_free(v->decoder);
				v->decoder = NULL;
			}
			memset(v, 0, sizeof(*v));
		}
	}
}
