#include "utils/internal/stb_vorbis.h"
#define MINIAUDIO_IMPLEMENTATION
#include "audio/audio_device.h"
#include "audio/audio_internal.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/types.h"
#include "utils/internal/miniaudio.h"
#include <stddef.h>
#include <stdio.h>

static void device_callback(ma_device* device, void* output, const void* input,
							ma_uint32 frameCount)
{
	(void)input;

	spel_audio_state_t* state = (spel_audio_state_t*)device->pUserData;
	if (!state)
	{
		return;
	}

	memset(output, 0, sizeof(float) * state->channels * frameCount);

	for (uint32_t bi = 1; bi < state->mixer.bus_count; bi++)
	{
		spel_audio_bus_state_t* bus = &state->mixer.buses[bi];
		if (bus->buffer)
		{
			memset(bus->buffer, 0,
				   sizeof(float) * state->channels * frameCount);
		}
	}

	spel_audio_cmd_process(&state->mixer, &state->cmd_ring);

	spel_audio_mixer_process(&state->mixer, (float*)output, frameCount, state->channels,
							 state->scratch, state->sample_rate);

	spel_audio_bus_process(&state->mixer, (float*)output, frameCount, state->channels,
						   state->sample_rate);
}

spel_api bool spel_audio_init(void)
{
	if (spel.audio.state)
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

	state->channels = spel.audio.channels ? spel.audio.channels : 2;
	state->sample_rate = spel.audio.sample_rate ? spel.audio.sample_rate : 48000;

	uint32_t buffer_size = spel.audio.buffer_size ? spel.audio.buffer_size : 512;

	state->scratch = (float*)spel_memory_malloc(
		sizeof(float) * buffer_size * state->channels, SPEL_MEM_TAG_AUDIO);
	if (!state->scratch)
	{
		spel_memory_free(state);
		return false;
	}

	uint32_t bus_count = spel.audio.bus_count ? spel.audio.bus_count : 1;
	if (bus_count > SPEL_AUDIO_MAX_BUSES)
	{
		bus_count = SPEL_AUDIO_MAX_BUSES;
	}
	state->mixer.bus_count = bus_count;

	for (uint32_t bi = 0; bi < bus_count; bi++)
	{
		spel_audio_bus_state_t* b = &state->mixer.buses[bi];
		b->volume = 1.0F;
		b->mute = false;
		b->solo = false;
		b->buffer = NULL;

		if (spel.audio.bus_names && spel.audio.bus_names[bi])
		{
			snprintf(b->name, sizeof(b->name), "%s", spel.audio.bus_names[bi]);
		}
		else
		{
			snprintf(b->name, sizeof(b->name), "bus_%u", bi);
		}
		b->name_hash = spel_audio_name_hash(b->name);

		if (bi > 0)
		{
			b->buffer = (float*)spel_memory_malloc(
				sizeof(float) * buffer_size * state->channels,
				SPEL_MEM_TAG_AUDIO);
			if (b->buffer)
			{
				memset(b->buffer, 0,
					   sizeof(float) * buffer_size * state->channels);
			}
		}
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

	spel.audio.state = state;
	spel_info("audio initialized (%uHz, %uch, %u frames)", state->sample_rate,
			  state->channels, buffer_size);
	return true;
}

spel_api void spel_audio_shutdown(void)
{
	if (!spel.audio.state)
	{
		return;
	}

	spel_audio_state_t* state = spel.audio.state;

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
		if (v->desc_bridge)
		{
			spel_memory_free(v->desc_bridge);
		}
		spel_audio_dsp_free(&v->dsp);
	}

	for (uint32_t bi = 1; bi < state->mixer.bus_count; bi++)
	{
		if (state->mixer.buses[bi].buffer)
		{
			spel_memory_free(state->mixer.buses[bi].buffer);
			state->mixer.buses[bi].buffer = NULL;
		}
	}

	for (uint32_t bi = 0; bi < state->mixer.bus_count; bi++)
	{
		spel_audio_dsp_free(&state->mixer.buses[bi].dsp);
	}

	spel_memory_free(state->scratch);
	spel_memory_free(state);
	spel.audio.state = NULL;
}
