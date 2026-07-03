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

spel_hidden void spel_audio_voice_free_effects(spel_audio_voice_t* v)
{
	if (v->distortion)
	{
		spel_memory_free(v->distortion);
		v->distortion = NULL;
	}
	if (v->lpf)
	{
		spel_memory_free(v->lpf);
		v->lpf = NULL;
	}
	if (v->hpf)
	{
		spel_memory_free(v->hpf);
		v->hpf = NULL;
	}
	free_buffered_effect(v->delay);
	v->delay = NULL;
	free_buffered_effect(v->flanger);
	v->flanger = NULL;
	free_buffered_effect(v->chorus);
	v->chorus = NULL;
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

	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
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
	cmd.type = SPEL_AUDIO_CMD_DESTROY;
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
	cmd.type = SPEL_AUDIO_CMD_PLAY;
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
	cmd.type = SPEL_AUDIO_CMD_STOP;
	cmd.voice_index = idx;

	if (!spel_audio_cmd_push(&state->cmd_ring, &cmd))
	{
		spel_warn("audio cmd ring full, dropping STOP for voice %d", idx);
	}
}

spel_api void spel_audio_voice_pause(spel_audio_voice voice)
{
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
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
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
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
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
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
	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
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

	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;
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

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	if (drive > 0.0F && !v->distortion)
	{
		v->distortion = (spel_audio_effect_distortion_t*)spel_memory_malloc(
			sizeof(spel_audio_effect_distortion_t), SPEL_MEM_TAG_AUDIO);
		if (!v->distortion)
		{
			return;
		}
		memset(v->distortion, 0, sizeof(*v->distortion));
	}

	if (v->distortion)
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

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	if (cutoffHz > 0.0F && !v->lpf)
	{
		v->lpf = (spel_audio_effect_onepole_t*)spel_memory_malloc(
			sizeof(spel_audio_effect_onepole_t), SPEL_MEM_TAG_AUDIO);
		if (!v->lpf)
		{
			return;
		}
		memset(v->lpf, 0, sizeof(*v->lpf));
	}

	if (!v->lpf)
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

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;

	if (cutoffHz > 0.0F && !v->hpf)
	{
		v->hpf = (spel_audio_effect_onepole_t*)spel_memory_malloc(
			sizeof(spel_audio_effect_onepole_t), SPEL_MEM_TAG_AUDIO);
		if (!v->hpf)
		{
			return;
		}
		memset(v->hpf, 0, sizeof(*v->hpf));
	}

	if (!v->hpf)
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

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	uint32_t channels = state->channels;
	uint32_t sample_rate = state->sample_rate;

	if (delayMs <= 0.0F || mix <= 0.0F)
	{
		free_buffered_effect(v->delay);
		v->delay = NULL;
		return;
	}

	if (!ensure_allocated((void**)&v->delay, sizeof(spel_audio_effect_delay_t)))
	{
		return;
	}
	uint32_t frames_needed = (uint32_t)((delayMs * 0.001F * (float)sample_rate) + 1.0F);
	if (!ensure_buffer(&v->delay->buffer, &v->delay->cap, frames_needed, channels))
	{
		return;
	}

	v->delay->delay_frames = (frames_needed > 0) ? frames_needed - 1 : 0;
	v->delay->wpos = 0;

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

	spel_audio_voice_t* v = (spel_audio_voice_t*)voice;
	uint32_t channels = state->channels;
	uint32_t sample_rate = state->sample_rate;

	if (rateHz <= 0.0F || depthMs <= 0.0F || mix <= 0.0F)
	{
		free_buffered_effect(v->flanger);
		v->flanger = NULL;
		return;
	}

	if (!ensure_allocated((void**)&v->flanger, sizeof(spel_audio_effect_flanger_t)))
	{
		return;
	}

	uint32_t frames_needed = (uint32_t)((5.0F * 0.001F * (float)sample_rate) + 1.0F);
	if (!ensure_buffer(&v->flanger->buffer, &v->flanger->cap, frames_needed, channels))
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
		free_buffered_effect(v->chorus);
		v->chorus = NULL;
		return;
	}

	if (!ensure_allocated((void**)&v->chorus, sizeof(spel_audio_effect_chorus_t)))
	{
		return;
	}

	uint32_t frames_needed = (uint32_t)((40.0F * 0.001F * (float)sr) + 1.0F);
	if (!ensure_buffer(&v->chorus->buffer, &v->chorus->cap, frames_needed, channels))
	{
		return;
	}

	for (int vi = 0; vi < voices; vi++)
	{
		v->chorus->base_delay[vi] = (float)(15 + (vi * 5)) * 0.001F * (float)sr;
		if (v->chorus->base_delay[vi] >= (float)frames_needed - 1.0F)
		{
			v->chorus->base_delay[vi] = (float)frames_needed - 2.0F;
		}
	}
	v->chorus->wpos = 0;
	v->chorus->voices = voices;

	spel_audio_cmd cmd;
	cmd.type = SPEL_AUDIO_CMD_CHORUS_PARAMS;
	cmd.voice_index = idx;
	cmd.floats[0] = rateHz;
	cmd.floats[1] = depthMs * 0.001F * (float)sr;
	cmd.floats[2] = mix;
	cmd.floats[3] = (float)voices;
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

		apply_distortion_effect(scratch, (ma_uint32)frames_read, channels, v->distortion);
		apply_onepole_lpf(scratch, (ma_uint32)frames_read, channels, v->lpf);
		apply_onepole_hpf(scratch, (ma_uint32)frames_read, channels, v->hpf);
		apply_delay_effect(scratch, (ma_uint32)frames_read, channels, v->delay);
		apply_flanger_effect(scratch, (ma_uint32)frames_read, channels, v->flanger,
							 sampleRate);
		apply_chorus_effect(scratch, (ma_uint32)frames_read, channels, v->chorus,
							sampleRate);

		apply_accumulate(output, scratch, (ma_uint32)frames_read, channels, v->volume,
						 v->pan_l, v->pan_r);

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
