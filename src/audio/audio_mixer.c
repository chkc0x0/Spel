#include "audio/audio_mixer.h"
#include "audio/audio_internal.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/types.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

typedef struct desc_bridge
{
	spel_audio_read_fn on_read;
	spel_audio_seek_fn on_seek;
	void* user_context;
} desc_bridge_t;

static ma_result on_read_bridge(ma_decoder* pDecoder, void* pBufferOut,
								size_t bytesToRead, size_t* pBytesRead)
{
	desc_bridge_t* b = (desc_bridge_t*)pDecoder->pUserData;
	size_t n = b->on_read(b->user_context, pBufferOut, bytesToRead);
	*pBytesRead = n;
	return (n > 0 || bytesToRead == 0) ? MA_SUCCESS : MA_AT_END;
}

static ma_result on_seek_bridge(ma_decoder* pDecoder, ma_int64 byteOffset,
								ma_seek_origin origin)
{
	desc_bridge_t* b = (desc_bridge_t*)pDecoder->pUserData;
	int ret = b->on_seek(b->user_context, (int64_t)byteOffset, (int)origin);
	return (ret == 0) ? MA_SUCCESS : MA_INVALID_OPERATION;
}

static void* ensure_allocated(void** slot, size_t size)
{
	if (!*slot)
	{
		*slot = spel_memory_malloc(size, SPEL_MEM_TAG_AUDIO);
		if (*slot)
		{
			memset(*slot, 0, size);
		}
	}
	return *slot;
}

static float* ensure_buffer(float** buf, uint32_t* cap, uint32_t neededFrames,
							uint32_t channels)
{
	if (*buf && *cap >= neededFrames)
	{
		return *buf;
	}

	size_t bytes = (size_t)neededFrames * channels * sizeof(float);
	float* nb = (float*)spel_memory_malloc(bytes, SPEL_MEM_TAG_AUDIO);
	if (!nb)
	{
		return NULL;
	}

	memset(nb, 0, bytes);
	if (*buf)
	{
		spel_memory_free(*buf);
	}

	*buf = nb;
	*cap = neededFrames;
	return nb;
}

static void free_buffered_effect(void* effect)
{
	if (!effect)
	{
		return;
	}
	float** buf = (float**)effect;
	if (*buf)
	{
		spel_memory_free(*buf);
	}
	spel_memory_free(effect);
}

static int find_free_slot(spel_audio_mixer_t* mixer)
{
	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		if (!atomic_load_explicit(&mixer->voices[i].active, memory_order_relaxed) &&
			mixer->voices[i].decoder == NULL)
		{
			return i;
		}
	}
	return -1;
}

static int steal_oldest_voice(spel_audio_mixer_t* mixer)
{
	int oldest_idx = -1;
	uint32_t oldest_frame = UINT32_MAX;

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

spel_hidden void spel_audio_dsp_free(spel_audio_dsp_chain_t* dsp)
{
	if (dsp->distortion)
	{
		spel_memory_free(dsp->distortion);
		dsp->distortion = NULL;
	}
	if (dsp->lpf)
	{
		spel_memory_free(dsp->lpf);
		dsp->lpf = NULL;
	}
	if (dsp->hpf)
	{
		spel_memory_free(dsp->hpf);
		dsp->hpf = NULL;
	}
	free_buffered_effect(dsp->delay);
	dsp->delay = NULL;
	free_buffered_effect(dsp->flanger);
	dsp->flanger = NULL;
	free_buffered_effect(dsp->chorus);
	dsp->chorus = NULL;
	free_buffered_effect(dsp->reverb);
	dsp->reverb = NULL;

	if (dsp->effect_chain_to_free)
	{
		spel_memory_free(dsp->effect_chain_to_free);
		dsp->effect_chain_to_free = NULL;
	}
	spel_audio_effect_array_t* _chain =
		atomic_load_explicit(&dsp->effect_chain, memory_order_relaxed);
	if (_chain)
	{
		spel_memory_free(_chain);
		atomic_store_explicit(&dsp->effect_chain, NULL, memory_order_release);
	}

	if (dsp->limiter)
	{
		spel_memory_free(dsp->limiter);
		dsp->limiter = NULL;
	}
	if (dsp->compressor)
	{
		spel_memory_free(dsp->compressor);
		dsp->compressor = NULL;
	}
}

static void spel_audio_voice_free_effects(spel_audio_voice_t* v)
{
	spel_audio_dsp_free(&v->dsp);

	if (v->pitch_buf)
	{
		spel_memory_free(v->pitch_buf);
		v->pitch_buf = NULL;
		v->pitch_buf_cap = 0;
		v->pitch = 1.0F;
	}
}

spel_api spel_audio_voice spel_audio_voice_create(spel_audio_source source)
{
	if (!source)
	{
		spel_error(SPEL_ERR_INVALID_ARGUMENT, "audio source is NULL");
		return NULL;
	}

	spel_audio_source_t* src = (spel_audio_source_t*)source;
	spel_audio_source_desc desc = {
		.type = SPEL_AUDIO_SOURCE_FILE,
		.source.file.path = src->path,
	};
	return spel_audio_voice_create_from_desc(&desc);
}

spel_api spel_audio_voice
spel_audio_voice_create_from_desc(const spel_audio_source_desc* desc)
{
	if (!desc)
	{
		spel_error(SPEL_ERR_INVALID_ARGUMENT, "source desc is NULL");
		return NULL;
	}

	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		spel_error(SPEL_ERR_INVALID_STATE, "audio not initialized");
		return NULL;
	}

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
		if (old->desc_bridge)
		{
			spel_memory_free(old->desc_bridge);
			old->desc_bridge = NULL;
		}
		spel_audio_voice_free_effects(old);

		memset(old, 0, sizeof(*old));

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
	ma_result result = MA_ERROR;

	switch (desc->type)
	{
	case SPEL_AUDIO_SOURCE_FILE:
		result = ma_decoder_init_file(desc->source.file.path, &dec_cfg, v->decoder);
		break;

	case SPEL_AUDIO_SOURCE_MEMORY:
		result = ma_decoder_init_memory(desc->source.memory.data,
										desc->source.memory.size, &dec_cfg, v->decoder);
		break;

	case SPEL_AUDIO_SOURCE_CALLBACKS:
		v->desc_bridge =
			(desc_bridge_t*)spel_memory_malloc(sizeof(desc_bridge_t), SPEL_MEM_TAG_AUDIO);
		if (!v->desc_bridge)
		{
			spel_memory_free(v->decoder);
			v->decoder = NULL;
			return NULL;
		}
		v->desc_bridge->on_read = desc->source.callbacks.on_read;
		v->desc_bridge->on_seek = desc->source.callbacks.on_seek;
		v->desc_bridge->user_context = desc->source.callbacks.user_context;

		result = ma_decoder_init(on_read_bridge, on_seek_bridge, v->desc_bridge, &dec_cfg,
								 v->decoder);
		if (result != MA_SUCCESS)
		{
			spel_memory_free(v->desc_bridge);
			v->desc_bridge = NULL;
		}
		break;

	default:
		spel_memory_free(v->decoder);
		v->decoder = NULL;
		spel_error(SPEL_ERR_INVALID_ARGUMENT, "invalid source desc type %d",
				   (int)desc->type);
		return NULL;
	}

	if (result != MA_SUCCESS)
	{
		spel_error(SPEL_ERR_INTERNAL, "failed to init decoder: %s",
				   ma_result_description(result));
		spel_memory_free(v->decoder);
		v->decoder = NULL;
		return NULL;
	}

	v->volume = 1.0F;
	v->pitch = 1.0F;
	v->pan_l = 1.0F;
	v->pan_r = 1.0F;

	atomic_store_explicit(&v->active, false, memory_order_release);
	atomic_store_explicit(&v->playing, false, memory_order_release);
	atomic_store_explicit(&v->done, false, memory_order_release);
	atomic_store_explicit(&v->looping, false, memory_order_release);
	v->fire_forget = false;

	atomic_store_explicit(&v->active, true, memory_order_release);

	return (spel_audio_voice)v;
}

spel_api spel_audio_voice spel_audio_voice_load(const char* path)
{
	if (!path || !*path)
	{
		spel_error(SPEL_ERR_INVALID_ARGUMENT, "audio path is NULL or empty");
		return NULL;
	}

	spel_audio_source_t temp;
	temp.path = (char*)path;

	return spel_audio_voice_create((spel_audio_source)&temp);
}

spel_api void spel_audio_voice_destroy(spel_audio_voice voice)
{
	if (!voice)
	{
		return;
	}

	spel_audio_state_t* state = spel.audio.state;
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
	cmd.type = SPEL_AUDIO_CMD_DESTROY;
	cmd.voice_index = idx;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping destroy for voice %d", idx);
	}
}

spel_api void spel_audio_voice_play(spel_audio_voice voice)
{
	spel_audio_state_t* state = spel.audio.state;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_PLAY;
	cmd.voice_index = idx;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping PLAY for voice %d", idx);
	}
}

spel_api void spel_audio_voice_stop(spel_audio_voice voice)
{
	spel_audio_state_t* state = spel.audio.state;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_STOP;
	cmd.voice_index = idx;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping STOP for voice %d", idx);
	}
}

spel_api void spel_audio_voice_pause(spel_audio_voice voice)
{
	spel_audio_state_t* state = spel.audio.state;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_PAUSE;
	cmd.voice_index = idx;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping PAUSE for voice %d", idx);
	}
}

spel_api bool spel_audio_voice_playing(spel_audio_voice voice)
{
	if (!voice)
	{
		return false;
	}
	return atomic_load_explicit(&((spel_audio_voice_t*)voice)->playing,
								memory_order_acquire);
}

spel_api bool spel_audio_voice_done(spel_audio_voice voice)
{
	if (!voice)
	{
		return false;
	}
	return atomic_load_explicit(&((spel_audio_voice_t*)voice)->done,
								memory_order_acquire);
}

spel_api void spel_audio_voice_volume_set(spel_audio_voice voice, float volume)
{
	spel_audio_state_t* state = spel.audio.state;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_VOLUME;
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
	spel_audio_state_t* state = spel.audio.state;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_PAN;
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
	spel_audio_state_t* state = spel.audio.state;
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_LOOP;
	cmd.voice_index = idx;
	cmd.bool_value = loop;

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
	return atomic_load_explicit(&((spel_audio_voice_t*)voice)->looping,
								memory_order_acquire);
}

spel_api spel_audio_voice spel_audio_play(spel_audio_source source, bool loop)
{
	spel_audio_voice v = spel_audio_voice_create(source);
	if (!v)
	{
		return NULL;
	}

	spel_audio_state_t* state = spel.audio.state;
	int idx = voice_index(state, v);
	if (idx < 0)
	{
		return NULL;
	}

	spel_audio_voice_t* vp = (spel_audio_voice_t*)v;
	vp->fire_forget = true;

	spel_audio_cmd cmd1;
	cmd1.type = SPEL_AUDIO_CMD_PLAY;
	cmd1.voice_index = idx;
	spel_audio_cmd_push(&state->cmd_ring, &cmd1);

	spel_audio_cmd cmd2;
	cmd2.type = SPEL_AUDIO_CMD_LOOP;
	cmd2.voice_index = idx;
	cmd2.bool_value = loop;
	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd2))
	{
		spel_warn("audio cmd ring full, dropping LOOP for voice %d", idx);
	}

	return v;
}

spel_api void spel_audio_voice_distortion_set(spel_audio_voice voice, float drive)
{
	if (!voice)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	if (drive > 0.0F && !v->dsp.distortion)
	{
		v->dsp.distortion = (spel_audio_effect_distortion_t*)spel_memory_malloc(
			sizeof(spel_audio_effect_distortion_t), SPEL_MEM_TAG_AUDIO);
		if (!v->dsp.distortion)
		{
			return;
		}
		memset(v->dsp.distortion, 0, sizeof(*v->dsp.distortion));
	}

	if (v->dsp.distortion)
	{
		spel_audio_cmd cmd;
		cmd.type = SPEL_AUDIO_CMD_DISTORTION_DRIVE;
		cmd.voice_index = idx;
		cmd.float_value = drive;
		spel_audio_cmd_push(&state->cmd_ring, &cmd);
	}
}

spel_api void spel_audio_voice_lpf_set(spel_audio_voice voice, float cutoffHz)
{
	if (!voice)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	if (cutoffHz > 0.0F && !v->dsp.lpf)
	{
		v->dsp.lpf = (spel_audio_effect_onepole_t*)spel_memory_malloc(
			sizeof(spel_audio_effect_onepole_t), SPEL_MEM_TAG_AUDIO);
		if (!v->dsp.lpf)
		{
			return;
		}
		memset(v->dsp.lpf, 0, sizeof(*v->dsp.lpf));
	}

	if (!v->dsp.lpf)
	{
		return;
	}

	float coeff = 0.0F;
	if (cutoffHz > 0.0F)
	{
		float w = 2.0F * (float)M_PI * cutoffHz / (float)state->sample_rate;
		coeff = 1.0F - expf(-w);
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_LPF_COEFF;
	cmd.voice_index = idx;
	cmd.float_value = coeff;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_voice_hpf_set(spel_audio_voice voice, float cutoffHz)
{
	if (!voice)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	if (cutoffHz > 0.0F && !v->dsp.hpf)
	{
		v->dsp.hpf = (spel_audio_effect_onepole_t*)spel_memory_malloc(
			sizeof(spel_audio_effect_onepole_t), SPEL_MEM_TAG_AUDIO);
		if (!v->dsp.hpf)
		{
			return;
		}
		memset(v->dsp.hpf, 0, sizeof(*v->dsp.hpf));
	}

	if (!v->dsp.hpf)
	{
		return;
	}

	float coeff = 0.0F;
	if (cutoffHz > 0.0F)
	{
		float w = 2.0F * (float)M_PI * cutoffHz / (float)state->sample_rate;
		coeff = 1.0F - expf(-w);
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_HPF_COEFF;
	cmd.voice_index = idx;
	cmd.float_value = coeff;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_voice_delay_set(spel_audio_voice voice, float delayMs,
										 float feedback, float mix)
{
	if (!voice)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	uint32_t channels = state->channels;
	uint32_t sample_rate = state->sample_rate;

	if (delayMs <= 0.0F || mix <= 0.0F)
	{
		free_buffered_effect(v->dsp.delay);
		v->dsp.delay = NULL;
		return;
	}

	if (!ensure_allocated((void**)&v->dsp.delay, sizeof(spel_audio_effect_delay_t)))
	{
		return;
	}
	uint32_t frames_needed = (uint32_t)((delayMs * 0.001F * (float)sample_rate) + 1.0F);
	if (!ensure_buffer(&v->dsp.delay->buffer, &v->dsp.delay->cap, frames_needed, channels))
	{
		return;
	}

	v->dsp.delay->delay_frames = (frames_needed > 0) ? frames_needed - 1 : 0;
	v->dsp.delay->wpos = 0;

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_DELAY_PARAMS;
	cmd.voice_index = idx;
	cmd.floats[0] = feedback;
	cmd.floats[1] = mix;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_voice_flanger_set(spel_audio_voice voice, float rateHz,
										   float depthMs, float mix)
{
	if (!voice)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	uint32_t channels = state->channels;
	uint32_t sample_rate = state->sample_rate;

	if (rateHz <= 0.0F || depthMs <= 0.0F || mix <= 0.0F)
	{
		free_buffered_effect(v->dsp.flanger);
		v->dsp.flanger = NULL;
		return;
	}

	if (!ensure_allocated((void**)&v->dsp.flanger, sizeof(spel_audio_effect_flanger_t)))
	{
		return;
	}

	uint32_t frames_needed = (uint32_t)((5.0F * 0.001F * (float)sample_rate) + 1.0F);
	if (!ensure_buffer(&v->dsp.flanger->buffer, &v->dsp.flanger->cap, frames_needed, channels))
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_FLANGER_PARAMS;
	cmd.voice_index = idx;
	cmd.floats[0] = rateHz;
	cmd.floats[1] = (float)frames_needed - 1.0F;
	cmd.floats[2] = depthMs * 0.001F * (float)sample_rate;
	cmd.floats[3] = mix;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_voice_chorus_set(spel_audio_voice voice, float rateHz,
										  float depthMs, float mix, int voices)
{
	if (!voice)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	uint32_t channels = state->channels;
	uint32_t sr = state->sample_rate;

	if (voices < 2)
	{
		voices = 2;
	}
	if (voices > 4)
	{
		voices = 4;
	}

	if (rateHz <= 0.0F || depthMs <= 0.0F || mix <= 0.0F)
	{
		free_buffered_effect(v->dsp.chorus);
		v->dsp.chorus = NULL;
		return;
	}

	if (!ensure_allocated((void**)&v->dsp.chorus, sizeof(spel_audio_effect_chorus_t)))
	{
		return;
	}

	uint32_t frames_needed = (uint32_t)((40.0F * 0.001F * (float)sr) + 1.0F);
	if (!ensure_buffer(&v->dsp.chorus->buffer, &v->dsp.chorus->cap, frames_needed, channels))
	{
		return;
	}

	for (int vi = 0; vi < voices; vi++)
	{
		v->dsp.chorus->base_delay[vi] = (float)(15 + (vi * 5)) * 0.001F * (float)sr;
		if (v->dsp.chorus->base_delay[vi] >= (float)frames_needed - 1.0F)
		{
			v->dsp.chorus->base_delay[vi] = (float)frames_needed - 2.0F;
		}
	}
	v->dsp.chorus->wpos = 0;
	v->dsp.chorus->voices = voices;

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_CHORUS_PARAMS;
	cmd.voice_index = idx;
	cmd.floats[0] = rateHz;
	cmd.floats[1] = depthMs * 0.001F * (float)sr;
	cmd.floats[2] = mix;
	cmd.floats[3] = (float)voices;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_voice_pitch_set(spel_audio_voice voice, float pitch)
{
	if (!voice)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	if (pitch < 0.001F)
	{
		pitch = 0.001F;
	}
	if (pitch > 10.0F)
	{
		pitch = 10.0F;
	}

	if (pitch != 1.0F && !v->pitch_buf)
	{
		uint32_t buf_sz = spel.audio.buffer_size;
		if (buf_sz == 0)
		{
			buf_sz = 512;
		}
		v->pitch_buf_cap = (uint32_t)(8.0F * (float)buf_sz) + 2;
		size_t bytes = (size_t)v->pitch_buf_cap * state->channels * sizeof(float);
		v->pitch_buf = (float*)spel_memory_malloc(bytes, SPEL_MEM_TAG_AUDIO);
		if (!v->pitch_buf)
		{
			v->pitch_buf_cap = 0;
			return;
		}
		memset(v->pitch_buf, 0, bytes);
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_PITCH_SET;
	cmd.voice_index = idx;
	cmd.float_value = pitch;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_voice_reverb_set(spel_audio_voice voice, float decay,
										  float damping, float preDelayMs, float mix)
{
	if (!voice)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	uint32_t sr = state->sample_rate;

	if (mix <= 0.0F || decay <= 0.0F)
	{
		free_buffered_effect(v->dsp.reverb);
		v->dsp.reverb = NULL;
		return;
	}

	if (!ensure_allocated((void**)&v->dsp.reverb, sizeof(spel_audio_effect_reverb_t)))
	{
		return;
	}

	float scale = (float)sr / 44100.0F;

	static const uint32_t COMB_L_BASE[4] = {1422, 1601, 1868, 2050};
	static const uint32_t COMB_R_BASE[4] = {1392, 1559, 1781, 2041};
	static const uint32_t AP_BASE[2] = {240, 80};

	uint32_t pre_frames = (uint32_t)((preDelayMs * 0.001F * (float)sr) + 1.0F);
	if (pre_frames < 4)
	{
		pre_frames = 4;
	}

	uint32_t pre_floats = pre_frames * 2;
	uint32_t combL_total = 0;
	uint32_t combR_total = 0;
	for (int i = 0; i < 4; i++)
	{
		combL_total += (uint32_t)(((float)COMB_L_BASE[i] * scale) + 1.0F);
		combR_total += (uint32_t)(((float)COMB_R_BASE[i] * scale) + 1.0F);
	}
	uint32_t ap_total = 0;
	for (int i = 0; i < 2; i++)
	{
		ap_total += (uint32_t)(((float)AP_BASE[i] * scale) + 1.0F);
	}
	ap_total *= 2;

	uint32_t total_floats = pre_floats + combL_total + combR_total + ap_total;
	if (!ensure_buffer(&v->dsp.reverb->buffer, &v->dsp.reverb->cap, total_floats, 1))
	{
		free_buffered_effect(v->dsp.reverb);
		v->dsp.reverb = NULL;
		return;
	}

	memset(v->dsp.reverb->damp_prev, 0, sizeof(v->dsp.reverb->damp_prev));

	v->dsp.reverb->pre_offset = 0;
	v->dsp.reverb->pre_len = pre_frames;

	uint32_t offset = pre_floats;
	for (int i = 0; i < 4; i++)
	{
		v->dsp.reverb->comb_l_len[i] = (uint32_t)(((float)COMB_L_BASE[i] * scale) + 1.0F);
		if (v->dsp.reverb->comb_l_len[i] < 4)
		{
			v->dsp.reverb->comb_l_len[i] = 4;
		}
		v->dsp.reverb->comb_l_offset[i] = offset;
		offset += v->dsp.reverb->comb_l_len[i];
	}
	for (int i = 0; i < 4; i++)
	{
		v->dsp.reverb->comb_r_len[i] = (uint32_t)(((float)COMB_R_BASE[i] * scale) + 1.0F);
		if (v->dsp.reverb->comb_r_len[i] < 4)
		{
			v->dsp.reverb->comb_r_len[i] = 4;
		}
		v->dsp.reverb->comb_r_offset[i] = offset;
		offset += v->dsp.reverb->comb_r_len[i];
	}

	for (int i = 0; i < 2; i++)
	{
		v->dsp.reverb->ap_len[i] = (uint32_t)(((float)AP_BASE[i] * scale) + 1.0F);
		if (v->dsp.reverb->ap_len[i] < 4)
		{
			v->dsp.reverb->ap_len[i] = 4;
		}
	}
	v->dsp.reverb->ap_l_offset[0] = offset;
	offset += v->dsp.reverb->ap_len[0];
	v->dsp.reverb->ap_l_offset[1] = offset;
	offset += v->dsp.reverb->ap_len[1];
	v->dsp.reverb->ap_r_offset[0] = offset;
	offset += v->dsp.reverb->ap_len[0];
	v->dsp.reverb->ap_r_offset[1] = offset;

	v->dsp.reverb->wpos = 0;

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_REVERB_PARAMS;
	cmd.voice_index = idx;
	cmd.floats[0] = decay;
	cmd.floats[1] = damping;
	cmd.floats[2] = preDelayMs * 0.001F * (float)sr; /* pre-delay in frames */
	cmd.floats[3] = mix;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

static void apply_distortion_effect(float* buf, ma_uint32 frames, uint32_t channels,
									spel_audio_effect_distortion_t* d)
{
	if (!d || d->drive <= 0.0F)
	{
		return;
	}
	ma_uint32 total = frames * channels;
	for (ma_uint32 s = 0; s < total; s++)
	{
		buf[s] = tanhf(d->drive * buf[s]);
	}
}

static void apply_onepole_lpf(float* buf, ma_uint32 frames, uint32_t channels,
							  spel_audio_effect_onepole_t* f)
{
	if (!f || f->coeff <= 0.0F)
	{
		return;
	}
	float a = f->coeff;
	for (uint32_t ch = 0; ch < channels; ch++)
	{
		float prev = f->prev[ch];
		uint32_t stride = channels;
		for (uint32_t s = ch; s < frames * channels; s += stride)
		{
			prev += a * (buf[s] - prev);
			buf[s] = prev;
		}
		f->prev[ch] = prev;
	}
}

static void apply_onepole_hpf(float* buf, ma_uint32 frames, uint32_t channels,
							  spel_audio_effect_onepole_t* f)
{
	if (!f || f->coeff <= 0.0F)
	{
		return;
	}
	float a = f->coeff;
	for (uint32_t ch = 0; ch < channels; ch++)
	{
		float prev = f->prev[ch];
		uint32_t stride = channels;
		for (uint32_t s = ch; s < frames * channels; s += stride)
		{
			prev += a * (buf[s] - prev);
			buf[s] = buf[s] - prev;
		}
		f->prev[ch] = prev;
	}
}

static void apply_delay_effect(float* buf, ma_uint32 frames, uint32_t channels,
							   spel_audio_effect_delay_t* d)
{
	if (!d || !d->buffer || d->delay_frames == 0)
	{
		return;
	}
	for (ma_uint32 f = 0; f < frames; f++)
	{
		uint32_t rpos = (d->wpos + d->cap - d->delay_frames) % d->cap;
		for (uint32_t ch = 0; ch < channels; ch++)
		{
			ma_uint32 s = (f * channels) + ch;
			float in = buf[s];
			float delayed = d->buffer[(rpos * channels) + ch];
			d->buffer[(d->wpos * channels) + ch] = in + (d->feedback * delayed);
			buf[s] = in + (d->mix * delayed);
		}
		d->wpos = (d->wpos + 1) % d->cap;
	}
}

static void apply_flanger_effect(float* buf, ma_uint32 frames, uint32_t channels,
								 spel_audio_effect_flanger_t* f, uint32_t sampleRate)
{
	if (!f || !f->buffer)
	{
		return;
	}
	float base = (float)f->cap * 0.5F;
	float two_pi = 2.0F * (float)M_PI;
	float phase_inc = two_pi * f->rate / (float)sampleRate;

	for (ma_uint32 fi = 0; fi < frames; fi++)
	{
		float lfo = ((sinf(f->lfo_phase) * 0.5F) + 0.5F) * f->depth_frames;
		uint32_t delay_f = (uint32_t)(base + lfo + 0.5F);
		if (delay_f >= f->cap)
		{
			delay_f = f->cap - 1;
		}
		uint32_t rpos = (f->wpos + f->cap - delay_f) % f->cap;

		for (uint32_t ch = 0; ch < channels; ch++)
		{
			ma_uint32 s = (fi * channels) + ch;
			f->buffer[(f->wpos * channels) + ch] = buf[s];
			buf[s] += f->mix * f->buffer[(rpos * channels) + ch];
		}

		f->lfo_phase += phase_inc;
		if (f->lfo_phase >= two_pi)
		{
			f->lfo_phase -= two_pi;
		}
		f->wpos = (f->wpos + 1) % f->cap;
	}
}

static void apply_chorus_effect(float* buf, ma_uint32 frames, uint32_t channels,
								spel_audio_effect_chorus_t* c, uint32_t sampleRate)
{
	if (!c || !c->buffer)
	{
		return;
	}
	int nv = c->voices;
	float two_pi = 2.0F * (float)M_PI;
	float phase_inc = two_pi * c->rate / (float)sampleRate;

	for (ma_uint32 fi = 0; fi < frames; fi++)
	{
		for (uint32_t ch = 0; ch < channels; ch++)
		{
			ma_uint32 s = (fi * channels) + ch;
			float in = buf[s];
			float sum = 0.0F;

			for (int vi = 0; vi < nv; vi++)
			{
				float lfo = ((sinf(c->lfo_phase[vi]) * 0.5F) + 0.5F) * c->depth_frames;
				float delay_f = c->base_delay[vi] + lfo;
				uint32_t delay_i = (uint32_t)(delay_f + 0.5F);
				if (delay_i >= c->cap)
				{
					delay_i = c->cap - 1;
				}
				uint32_t rpos = (c->wpos + c->cap - delay_i) % c->cap;
				sum += c->buffer[(rpos * channels) + ch];
			}

			c->buffer[(c->wpos * channels) + ch] = in;
			buf[s] = in + (c->mix * sum / (float)nv);
		}

		for (int vi = 0; vi < nv; vi++)
		{
			c->lfo_phase[vi] += phase_inc;
			if (c->lfo_phase[vi] >= two_pi)
			{
				c->lfo_phase[vi] -= two_pi;
			}
		}
		c->wpos = (c->wpos + 1) % c->cap;
	}
}

static void apply_reverb_effect(float* buf, ma_uint32 frames, uint32_t channels,
								spel_audio_effect_reverb_t* r, uint32_t sampleRate)
{
	if (!r || !r->buffer || r->mix <= 0.0F || channels < 2)
	{
		return;
	}
	(void)sampleRate;

	float mix_wet = r->mix;
	float mix_dry = 1.0F - mix_wet;
	float decay = r->decay;
	float damp = r->damping;
	float cross = 0.3F;
	float ap_a = 0.7F;
	float tap_gain = 0.25F;

	uint32_t pre_len = r->pre_len;
	uint32_t pre_dly = (uint32_t)(r->pre_delay + 0.5F);
	if (pre_dly > pre_len)
	{
		pre_dly = pre_len;
	}

	for (ma_uint32 fi = 0; fi < frames; fi++)
	{
		uint32_t sL = fi * channels;
		uint32_t sR = (fi * channels) + 1;
		float inL = buf[sL];
		float inR = buf[sR];

		uint32_t pre_wp = r->wpos % pre_len;
		uint32_t pre_rp = (pre_wp + pre_len - pre_dly) % pre_len;
		float preL = r->buffer[r->pre_offset + (pre_rp * 2)];
		float preR = r->buffer[r->pre_offset + (pre_rp * 2) + 1];
		r->buffer[r->pre_offset + (pre_wp * 2)] = inL;
		r->buffer[r->pre_offset + (pre_wp * 2) + 1] = inR;

		float combL = 0.0F;
		float combR = 0.0F;

		for (int ci = 0; ci < 4; ci++)
		{
			uint32_t wpl = r->wpos % r->comb_l_len[ci];
			uint32_t wpr = r->wpos % r->comb_r_len[ci];

			float delayedL = r->buffer[r->comb_l_offset[ci] + wpl];
			float delayedR = r->buffer[r->comb_r_offset[ci] + wpr];

			float dampedL =
				r->damp_prev[0][ci] + (damp * (delayedL - r->damp_prev[0][ci]));
			r->damp_prev[0][ci] = dampedL;
			float dampedR =
				r->damp_prev[1][ci] + (damp * (delayedR - r->damp_prev[1][ci]));
			r->damp_prev[1][ci] = dampedR;

			float fbL = decay * dampedL;
			float fbR = decay * dampedR;

			r->buffer[r->comb_l_offset[ci] + wpl] = preL + fbL;
			r->buffer[r->comb_r_offset[ci] + wpr] = preR + fbR;

			combL += delayedL * tap_gain;
			combR += delayedR * tap_gain;
		}

		float mixedL = combL + (cross * combR);
		float mixedR = combR + (cross * combL);

		uint32_t ap0_len = r->ap_len[0];
		uint32_t ap0_wp = r->wpos % ap0_len;
		float ap0_readL = r->buffer[r->ap_l_offset[0] + ap0_wp];
		r->buffer[r->ap_l_offset[0] + ap0_wp] = mixedL + (ap_a * ap0_readL);
		float ap_outL = (-ap_a * mixedL) + ap0_readL;

		float ap0_readR = r->buffer[r->ap_r_offset[0] + ap0_wp];
		r->buffer[r->ap_r_offset[0] + ap0_wp] = mixedR + (ap_a * ap0_readR);
		float ap_outR = (-ap_a * mixedR) + ap0_readR;

		uint32_t ap1_len = r->ap_len[1];
		uint32_t ap1_wp = r->wpos % ap1_len;
		float ap1_readL = r->buffer[r->ap_l_offset[1] + ap1_wp];
		r->buffer[r->ap_l_offset[1] + ap1_wp] = ap_outL + (ap_a * ap1_readL);
		float tailL = (-ap_a * ap_outL) + ap1_readL;

		float ap1_readR = r->buffer[r->ap_r_offset[1] + ap1_wp];
		r->buffer[r->ap_r_offset[1] + ap1_wp] = ap_outR + (ap_a * ap1_readR);
		float tailR = (-ap_a * ap_outR) + ap1_readR;

		buf[sL] = (inL * mix_dry) + (tailL * mix_wet);
		buf[sR] = (inR * mix_dry) + (tailR * mix_wet);

		r->wpos++;
	}
}

static void apply_effect_chain(float* buf, ma_uint32 frames, uint32_t channels,
							   uint32_t sampleRate, spel_audio_effect_array_t* chain)
{
	if (!chain)
	{
		return;
	}
	for (uint32_t i = 0; i < chain->count; i++)
	{
		spel_audio_effect_slot_t* s = &chain->slots[i];
		if (s->callback)
		{
			spel_audio_custom_effect_ctx ctx;
			ctx.params = s->params;
			ctx.num_params = SPEL_AUDIO_CUSTOM_PARAM_COUNT;
			ctx.user_data = s->user_data;
			s->callback(buf, frames, channels, sampleRate, &ctx);
		}
	}
}

static float db_to_linear(float db)
{
	return powf(10.0F, db * 0.05F);
}

static void apply_bus_limiter(float* buf, ma_uint32 frames, uint32_t channels,
							  uint32_t sampleRate, spel_audio_effect_limiter_t* lim)
{
	if (!lim)
	{
		return;
	}
	float thresh_lin = db_to_linear(lim->threshold);
	float attack_a = expf(-1.0F / (lim->attack * (float)sampleRate));
	float release_a = expf(-1.0F / (lim->release * (float)sampleRate));

	for (ma_uint32 f = 0; f < frames; f++)
	{
		for (uint32_t ch = 0; ch < channels; ch++)
		{
			uint32_t s = (f * channels) + ch;
			float level = fabsf(buf[s]);
			float env = lim->peak[ch];

			float a = (level > env) ? attack_a : release_a;
			env = ((1.0F - a) * level) + (a * env);
			lim->peak[ch] = env;

			if (env > thresh_lin && env > 1e-10F)
			{
				float gain = thresh_lin / env;
				buf[s] *= gain;
			}
		}
	}
}

static void apply_bus_compressor(float* buf, ma_uint32 frames, uint32_t channels,
								 uint32_t sampleRate, spel_audio_effect_compressor_t* comp)
{
	if (!comp)
	{
		return;
	}
	float attack_a = expf(-1.0F / (comp->attack * (float)sampleRate));
	float release_a = expf(-1.0F / (comp->release * (float)sampleRate));
	float inv_ratio = 1.0F / comp->ratio;

	for (ma_uint32 f = 0; f < frames; f++)
	{
		for (uint32_t ch = 0; ch < channels; ch++)
		{
			uint32_t s = (f * channels) + ch;
			float level = fabsf(buf[s]);
			float envelope = comp->env[ch];

			float a = (level > envelope) ? attack_a : release_a;
			envelope = ((1.0F - a) * level) + (a * envelope);
			comp->env[ch] = envelope;

			if (envelope > 1e-10F)
			{
				float db_env = 20.0F * log10f(envelope);
				if (db_env > comp->threshold)
				{
					float db_reduction = (db_env - comp->threshold) * inv_ratio;
					float target_db = db_env - db_reduction;
					float gain_lin = powf(10.0F, target_db * 0.05F) / envelope;
					buf[s] *= gain_lin;
				}
			}
		}
	}
}

static void spel_audio_dsp_apply(spel_audio_dsp_chain_t* dsp, float* buf,
								 ma_uint32 frames, uint32_t channels,
								 uint32_t sampleRate)
{
	if (!dsp)
	{
		return;
	}
	apply_distortion_effect(buf, frames, channels, dsp->distortion);
	apply_onepole_lpf(buf, frames, channels, dsp->lpf);
	apply_onepole_hpf(buf, frames, channels, dsp->hpf);
	apply_delay_effect(buf, frames, channels, dsp->delay);
	apply_flanger_effect(buf, frames, channels, dsp->flanger, sampleRate);
	apply_chorus_effect(buf, frames, channels, dsp->chorus, sampleRate);
	apply_reverb_effect(buf, frames, channels, dsp->reverb, sampleRate);

	{
		spel_audio_effect_array_t* chain =
			atomic_load_explicit(&dsp->effect_chain, memory_order_acquire);
		apply_effect_chain(buf, frames, channels, sampleRate, chain);
	}

	apply_bus_limiter(buf, frames, channels, sampleRate, dsp->limiter);
	apply_bus_compressor(buf, frames, channels, sampleRate, dsp->compressor);
}

static void apply_accumulate(float* output, const float* scratch, ma_uint32 frames,
							 uint32_t channels, float vol, float panL, float panR)
{
	if (channels == 1)
	{
		for (ma_uint32 f = 0; f < frames; f++)
		{
			output[f] += scratch[f] * vol;
		}
	}
	else
	{
		float l = vol * panL;
		float r = vol * panR;
		for (ma_uint32 f = 0; f < frames; f++)
		{
			output[(size_t)(f * 2)] += scratch[(size_t)(f * 2)] * l;
			output[(size_t)(f * 2) + 1] += scratch[(size_t)(f * 2) + 1] * r;
		}
	}
}

spel_hidden void spel_audio_bus_process(spel_audio_mixer_t* mixer, float* output,
										ma_uint32 frameCount, uint32_t channels,
										uint32_t sampleRate)
{
	(void)sampleRate;

	bool any_solo = false;
	for (uint32_t bi = 1; bi < mixer->bus_count; bi++)
	{
		if (mixer->buses[bi].solo)
		{
			any_solo = true;
			break;
		}
	}

	for (uint32_t bi = 1; bi < mixer->bus_count; bi++)
	{
		spel_audio_bus_state_t* b = &mixer->buses[bi];
		if (!b->buffer)
		{
			continue;
		}

		if (b->mute)
		{
			continue;
		}

		if (any_solo && !b->solo)
		{
			continue;
		}

		spel_audio_dsp_apply(&b->dsp, b->buffer, frameCount, channels, sampleRate);

		float vol = b->volume;
		if (channels == 1)
		{
			for (ma_uint32 f = 0; f < frameCount; f++)
			{
				output[f] += b->buffer[f] * vol;
			}
		}
		else
		{
			for (ma_uint32 f = 0; f < frameCount; f++)
			{
				output[(size_t)(f * 2)]     += b->buffer[(size_t)(f * 2)]     * vol;
				output[(size_t)(f * 2) + 1] += b->buffer[(size_t)(f * 2) + 1] * vol;
			}
		}
	}

	spel_audio_dsp_apply(&mixer->buses[0].dsp, output, frameCount, channels, sampleRate);
}

static bool read_voice_frames(spel_audio_voice_t* v, float* scratch,
							   ma_uint32 frameCount, ma_uint32 channels,
							   ma_uint32* outFrames, bool* pitchRawEnd)
{
	if (v->pitch == 1.0F)
	{
		ma_uint64 dec_read = 0;
		ma_result result =
			ma_decoder_read_pcm_frames(v->decoder, scratch, frameCount, &dec_read);
		*outFrames = (ma_uint32)dec_read;
		*pitchRawEnd = false;
		return (result == MA_SUCCESS && dec_read > 0) != 0;
	}

	if (!v->pitch_buf)
	{
		return false;
	}

	ma_uint32 raw_needed = (ma_uint32)(ceilf(v->pitch * (float)frameCount)) + 1;
	if (raw_needed > v->pitch_buf_cap)
	{
		raw_needed = v->pitch_buf_cap;
	}

	ma_uint64 raw_read = 0;
	ma_result result =
		ma_decoder_read_pcm_frames(v->decoder, v->pitch_buf, raw_needed, &raw_read);

	if (result != MA_SUCCESS || raw_read == 0)
	{
		return false;
	}

	ma_uint32 safe = (ma_uint32)raw_read;
	ma_uint32 max_i0 = (safe > 1) ? safe - 2 : 0;

	for (ma_uint32 fi = 0; fi < frameCount; fi++)
	{
		float src_float = (float)fi * v->pitch;
		ma_uint32 i0 = (ma_uint32)src_float;
		float frac = src_float - (float)i0;

		if (i0 > max_i0)
		{
			i0 = max_i0;
			frac = 0.0F;
		}

		ma_uint32 i1 = i0 + 1;
		for (ma_uint32 ch = 0; ch < channels; ch++)
		{
			ma_uint32 si = (fi * channels) + ch;
			float a = v->pitch_buf[(i0 * channels) + ch];
			float b = v->pitch_buf[(i1 * channels) + ch];
			scratch[si] = a + (frac * (b - a));
		}
	}

	*outFrames = frameCount;
	*pitchRawEnd = (raw_read < (ma_uint64)raw_needed);
	return true;
}

static void voice_mark_end(spel_audio_voice_t* v, bool pitchRawEnd,
							ma_uint32 outFrames, ma_uint32 frameCount)
{
	if (pitchRawEnd || outFrames < frameCount)
	{
		ma_decoder_seek_to_pcm_frame(v->decoder, 0);
	}
	else
	{
		atomic_store_explicit(&v->playing, false, memory_order_release);
		atomic_store_explicit(&v->done, true, memory_order_release);
	}
}

void spel_audio_mixer_process(spel_audio_mixer_t* mixer, float* output,
							  ma_uint32 frameCount, uint32_t channels, float* scratch,
							  uint32_t sampleRate)
{
	atomic_fetch_add_explicit(&mixer->frame_counter, 1, memory_order_relaxed);

	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		spel_audio_voice_t* v = &mixer->voices[i];

		if (!atomic_load_explicit(&v->active, memory_order_acquire) ||
			!atomic_load_explicit(&v->playing, memory_order_acquire) || !v->decoder)
		{
			continue;
		}

		memset(scratch, 0, (size_t)(frameCount * channels) * sizeof(float));

		ma_uint32 out_frames = 0;
		bool pitch_raw_end = false;

		if (!read_voice_frames(v, scratch, frameCount, channels, &out_frames,
							   &pitch_raw_end))
		{
			atomic_store_explicit(&v->playing, false, memory_order_release);
			atomic_store_explicit(&v->done, true, memory_order_release);
			continue;
		}

		spel_audio_dsp_apply(&v->dsp, scratch, out_frames, channels, sampleRate);

		if (v->bus_id == 0)
		{
			apply_accumulate(output, scratch, out_frames, channels,
							 v->volume, v->pan_l, v->pan_r);
		}
		else if (v->bus_id < mixer->bus_count && mixer->buses[v->bus_id].buffer)
		{
			apply_accumulate(mixer->buses[v->bus_id].buffer, scratch, out_frames,
							 channels, v->volume, v->pan_l, v->pan_r);
		}

		voice_mark_end(v, pitch_raw_end, out_frames, frameCount);
	}
}

spel_hidden void spel_audio_cleanup(void)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}

	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		spel_audio_voice_t* v = &state->mixer.voices[i];

		if (v->dsp.effect_chain_to_free)
		{
			spel_memory_free(v->dsp.effect_chain_to_free);
			v->dsp.effect_chain_to_free = NULL;
		}

		if (v->fire_forget && atomic_load_explicit(&v->done, memory_order_acquire))
		{
			if (v->decoder)
			{
				ma_decoder_uninit(v->decoder);
				spel_memory_free(v->decoder);
				v->decoder = NULL;
			}
			if (v->desc_bridge)
			{
				spel_memory_free(v->desc_bridge);
				v->desc_bridge = NULL;
			}
			spel_audio_voice_free_effects(v);
			memset(v, 0, sizeof(*v));
			continue;
		}

		if (!atomic_load_explicit(&v->active, memory_order_acquire) && v->decoder != NULL)
		{
			if (v->decoder)
			{
				ma_decoder_uninit(v->decoder);
				spel_memory_free(v->decoder);
				v->decoder = NULL;
			}
			if (v->desc_bridge)
			{
				spel_memory_free(v->desc_bridge);
				v->desc_bridge = NULL;
			}
			spel_audio_voice_free_effects(v);
			memset(v, 0, sizeof(*v));
		}
	}
}

spel_api void spel_audio_master_limiter_set(float thresholdDb, float attackMs,
											float releaseMs)
{
	spel_audio_bus_limiter_set(0, thresholdDb, attackMs, releaseMs);
}

spel_api void spel_audio_master_compressor_set(float thresholdDb, float ratio,
											   float attackMs, float releaseMs)
{
	spel_audio_bus_compressor_set(0, thresholdDb, ratio, attackMs, releaseMs);
}

spel_api bool spel_audio_master_limiter_enabled(void)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state || state->mixer.bus_count == 0)
	{
		return false;
	}
	return state->mixer.buses[0].dsp.limiter != NULL;
}

spel_api bool spel_audio_master_compressor_enabled(void)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state || state->mixer.bus_count == 0)
	{
		return false;
	}
	return state->mixer.buses[0].dsp.compressor != NULL;
}

spel_api void spel_audio_bus_limiter_set(uint32_t busId, float thresholdDb,
										 float attackMs, float releaseMs)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state || busId >= state->mixer.bus_count)
	{
		return;
	}
	spel_audio_bus_state_t* b = &state->mixer.buses[busId];

	if (thresholdDb <= -80.0F || attackMs <= 0.0F || releaseMs <= 0.0F)
	{
		spel_audio_cmd cmd;
		cmd.type = SPEL_AUDIO_CMD_BUS_LIMITER_ENABLE;
		cmd.voice_index = (int)busId;
		cmd.bool_value = false;
		spel_audio_cmd_push(&state->cmd_ring, &cmd);
		return;
	}

	if (!b->dsp.limiter)
	{
		b->dsp.limiter = (spel_audio_effect_limiter_t*)spel_memory_malloc(
			sizeof(spel_audio_effect_limiter_t), SPEL_MEM_TAG_AUDIO);
		if (b->dsp.limiter)
		{
			memset(b->dsp.limiter, 0, sizeof(*b->dsp.limiter));
		}
	}
	if (!b->dsp.limiter)
	{
		return;
	}

	float attack_sec = attackMs * 0.001F;
	float release_sec = releaseMs * 0.001F;

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_BUS_LIMITER_PARAMS;
	cmd.voice_index = (int)busId;
	cmd.floats[0] = thresholdDb;
	cmd.floats[1] = attack_sec;
	cmd.floats[2] = release_sec;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);

	cmd.type = SPEL_AUDIO_CMD_BUS_LIMITER_ENABLE;
	cmd.bool_value = true;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_bus_compressor_set(uint32_t busId, float thresholdDb,
											float ratio, float attackMs,
											float releaseMs)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state || busId >= state->mixer.bus_count)
	{
		return;
	}
	spel_audio_bus_state_t* b = &state->mixer.buses[busId];

	if (thresholdDb <= -80.0F || ratio <= 1.0F || attackMs <= 0.0F || releaseMs <= 0.0F)
	{
		spel_audio_cmd cmd;
		cmd.type = SPEL_AUDIO_CMD_BUS_COMPRESSOR_ENABLE;
		cmd.voice_index = (int)busId;
		cmd.bool_value = false;
		spel_audio_cmd_push(&state->cmd_ring, &cmd);
		return;
	}

	if (!b->dsp.compressor)
	{
		b->dsp.compressor = (spel_audio_effect_compressor_t*)spel_memory_malloc(
			sizeof(spel_audio_effect_compressor_t), SPEL_MEM_TAG_AUDIO);
		if (b->dsp.compressor)
		{
			memset(b->dsp.compressor, 0, sizeof(*b->dsp.compressor));
		}
	}
	if (!b->dsp.compressor)
	{
		return;
	}

	float attack_sec = attackMs * 0.001F;
	float release_sec = releaseMs * 0.001F;

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_BUS_COMPRESSOR_PARAMS;
	cmd.voice_index = (int)busId;
	cmd.floats[0] = thresholdDb;
	cmd.floats[1] = ratio;
	cmd.floats[2] = attack_sec;
	cmd.floats[3] = release_sec;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);

	cmd.type = SPEL_AUDIO_CMD_BUS_COMPRESSOR_ENABLE;
	cmd.bool_value = true;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api int spel_audio_voice_effect_add(spel_audio_voice voice,
										 spel_audio_effect_fn callback, void* userData)
{
	if (!voice || !callback)
	{
		return -1;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return -1;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return -1;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	spel_audio_effect_array_t* old =
		atomic_load_explicit(&v->dsp.effect_chain, memory_order_relaxed);
	uint32_t old_count = old ? old->count : 0;
	uint32_t new_count = old_count + 1;

	spel_audio_effect_array_t* new_arr = (spel_audio_effect_array_t*)spel_memory_malloc(
		sizeof(spel_audio_effect_array_t) +
			((size_t)new_count * sizeof(spel_audio_effect_slot_t)),
		SPEL_MEM_TAG_AUDIO);
	if (!new_arr)
	{
		return -1;
	}

	new_arr->count = new_count;
	if (old && old_count > 0)
	{
		memcpy(new_arr->slots, old->slots,
			   (size_t)old_count * sizeof(spel_audio_effect_slot_t));
	}
	new_arr->slots[old_count].callback = callback;
	new_arr->slots[old_count].user_data = userData;
	memset(new_arr->slots[old_count].params, 0, sizeof(new_arr->slots[old_count].params));

	atomic_store_explicit(&v->dsp.effect_chain, new_arr, memory_order_release);
	int slot = (int)old_count;

	if (v->dsp.effect_chain_to_free)
	{
		spel_memory_free(v->dsp.effect_chain_to_free);
	}
	v->dsp.effect_chain_to_free = old;

	return slot;
}

spel_api void spel_audio_voice_effect_remove(spel_audio_voice voice, int slot)
{
	if (!voice || slot < 0)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	spel_audio_effect_array_t* old =
		atomic_load_explicit(&v->dsp.effect_chain, memory_order_relaxed);
	if (!old || (uint32_t)slot >= old->count)
	{
		return;
	}

	uint32_t new_count = old->count - 1;

	if (new_count == 0)
	{
		atomic_store_explicit(&v->dsp.effect_chain, NULL, memory_order_release);

		if (v->dsp.effect_chain_to_free)
		{
			spel_memory_free(v->dsp.effect_chain_to_free);
		}
		v->dsp.effect_chain_to_free = old;
		return;
	}

	spel_audio_effect_array_t* new_arr = (spel_audio_effect_array_t*)spel_memory_malloc(
		sizeof(spel_audio_effect_array_t) +
			((size_t)new_count * sizeof(spel_audio_effect_slot_t)),
		SPEL_MEM_TAG_AUDIO);
	if (!new_arr)
	{
		return;
	}

	new_arr->count = new_count;

	if (slot > 0)
	{
		memcpy(new_arr->slots, old->slots,
			   (size_t)slot * sizeof(spel_audio_effect_slot_t));
	}

	if ((uint32_t)(slot + 1) < old->count)
	{
		memcpy(&new_arr->slots[slot], &old->slots[slot + 1],
			   (size_t)(old->count - (uint32_t)slot - 1) *
				   sizeof(spel_audio_effect_slot_t));
	}

	atomic_store_explicit(&v->dsp.effect_chain, new_arr, memory_order_release);

	if (v->dsp.effect_chain_to_free)
	{
		spel_memory_free(v->dsp.effect_chain_to_free);
	}
	v->dsp.effect_chain_to_free = old;
}

spel_api void spel_audio_voice_effect_param_set(spel_audio_voice voice, int slot,
												uint32_t paramIndex, float value)
{
	if (!voice || slot < 0)
	{
		return;
	}
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return;
	}
	int idx = voice_index(state, voice);
	if (idx < 0)
	{
		return;
	}

	if (paramIndex >= SPEL_AUDIO_CUSTOM_PARAM_COUNT)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_EFFECT_PARAM_SET;
	cmd.voice_index = idx;
	cmd.floats[0] = (float)slot;
	cmd.floats[1] = (float)paramIndex;
	cmd.floats[2] = value;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping effect param set for voice %d", idx);
	}
}

spel_api void spel_audio_bus_volume_set(uint32_t busId, float volume)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state || busId >= state->mixer.bus_count)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_BUS_VOLUME;
	cmd.voice_index = (int)busId;
	cmd.float_value = volume;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_bus_mute_set(uint32_t busId, bool mute)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state || busId >= state->mixer.bus_count)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_BUS_MUTE;
	cmd.voice_index = (int)busId;
	cmd.bool_value = mute;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api void spel_audio_bus_solo_set(uint32_t busId, bool solo)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state || busId >= state->mixer.bus_count)
	{
		return;
	}

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_BUS_SOLO;
	cmd.voice_index = (int)busId;
	cmd.bool_value = solo;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api uint32_t spel_audio_bus_find(const char* name)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state || !name)
	{
		return UINT32_MAX;
	}

	uint64_t hash = spel_audio_name_hash(name);
	for (uint32_t bi = 0; bi < state->mixer.bus_count; bi++)
	{
		if (state->mixer.buses[bi].name_hash == hash)
		{
			return bi;
		}
	}
	return UINT32_MAX;
}

spel_api uint32_t spel_audio_bus_count(void)
{
	spel_audio_state_t* state = spel.audio.state;
	if (!state)
	{
		return 0;
	}
	return state->mixer.bus_count;
}

spel_api void spel_audio_voice_bus_set(spel_audio_voice voice, uint32_t busId)
{
	spel_audio_state_t* state = spel.audio.state;
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
	cmd.type = SPEL_AUDIO_CMD_VOICE_BUS;
	cmd.voice_index = idx;
	cmd.float_value = (float)busId;
	spel_audio_cmd_push(&state->cmd_ring, &cmd);
}

spel_api uint32_t spel_audio_voice_bus(spel_audio_voice voice)
{
	if (!voice)
	{
		return 0;
	}
	return ((spel_audio_voice_t*)voice)->bus_id;
}
