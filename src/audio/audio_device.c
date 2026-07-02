#define MINIAUDIO_IMPLEMENTATION
#include "audio/audio_device.h"
#include "audio/audio_internal.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/types.h"
#include "utils/internal/miniaudio.h"
#include <stddef.h>

static void device_callback(ma_device* device, void* output, const void* input,
							ma_uint32 frameCount)
{
	(void)input;

	spel_audio_state_t* state = (spel_audio_state_t*)device->pUserData;
	if (!state)
	{
		return;
	}

	memset(output, 0, (__ssize_t)frameCount * state->channels * sizeof(float));

	spel_audio_cmd_process(&state->mixer, &state->cmd_ring);

	spel_audio_mixer_process(&state->mixer, (float*)output, frameCount, state->channels,
							 state->scratch);
}

spel_api bool spel_audio_init(const spel_audio_config* config)
{
	if (spel.audio)
	{
		spel_warn("audio already initialized");
		return false;
	}

	spel_audio_state_t* state = (spel_audio_state_t*)spel_memory_malloc(
		sizeof(spel_audio_state_t), SPEL_MEM_TAG_AUDIO);
	if (!state)
	{
		return false;
	}

	memset(state, 0, sizeof(*state));

	if (config)
	{
		state->config = *config;
	}

	state->channels = state->config.channels ? state->config.channels : 2;
	state->sample_rate = state->config.sample_rate ? state->config.sample_rate : 48000;

	uint32_t buffer_size = state->config.buffer_size ? state->config.buffer_size : 512;

	state->scratch = (float*)spel_memory_malloc(
		(__ssize_t)(buffer_size * state->channels) * sizeof(float), SPEL_MEM_TAG_AUDIO);
	if (!state->scratch)
	{
		spel_memory_free(state);
		return false;
	}

	ma_device_config dev_cfg = ma_device_config_init(ma_device_type_playback);
	dev_cfg.playback.format = ma_format_f32;
	dev_cfg.playback.channels = state->channels;
	dev_cfg.sampleRate = state->sample_rate;
	dev_cfg.dataCallback = device_callback;
	dev_cfg.pUserData = state;
	dev_cfg.periodSizeInFrames = buffer_size;

	ma_result result = ma_device_init(NULL, &dev_cfg, &state->device);
	if (result != MA_SUCCESS)
	{
		spel_error(SPEL_ERR_INTERNAL, "failed to init audio device: %s",
				   ma_result_description(result));
		spel_memory_free(state->scratch);
		spel_memory_free(state);
		return false;
	}

	result = ma_device_start(&state->device);
	if (result != MA_SUCCESS)
	{
		spel_error(SPEL_ERR_INTERNAL, "failed to start audio device: %s",
				   ma_result_description(result));
		ma_device_uninit(&state->device);
		spel_memory_free(state->scratch);
		spel_memory_free(state);
		return false;
	}

	state->channels = state->device.playback.channels;
	state->sample_rate = state->device.sampleRate;

	spel.audio = (spel_audio)state;
	spel_info("audio initialized (%uHz, %uch, %u frames)", state->sample_rate,
			  state->channels, buffer_size);
	return true;
}

spel_api void spel_audio_shutdown(void)
{
	if (!spel.audio)
	{
		return;
	}

	spel_audio_state_t* state = (spel_audio_state_t*)spel.audio;

	ma_device_stop(&state->device);
	ma_device_uninit(&state->device);

	for (int i = 0; i < SPEL_AUDIO_MAX_VOICES; i++)
	{
		spel_audio_voice_t* v = &state->mixer.voices[i];
		if (v->decoder)
		{
			ma_decoder_uninit(v->decoder);
			spel_memory_free(v->decoder);
		}
	}

	spel_memory_free(state->scratch);
	spel_memory_free(state);
	spel.audio = NULL;
}
