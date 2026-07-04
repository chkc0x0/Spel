#include "audio/audio_internal.h"
#include "core/log.h"
#include <stddef.h>

static int voice_index_check(spel_audio_mixer_t* mixer, int idx)
{
	(void)mixer;
	return (idx >= 0 && idx < SPEL_AUDIO_MAX_VOICES) ? idx : -1;
}

spel_hidden bool spel_audio_cmd_push(spel_audio_cmd_ring* ring, const spel_audio_cmd* cmd)
{
	unsigned int tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
	unsigned int head = atomic_load_explicit(&ring->head, memory_order_relaxed);

	if ((head - tail) >= SPEL_AUDIO_CMD_RING_SIZE)
	{
		return false;
	}

	ring->buffer[head & SPEL_AUDIO_CMD_RING_MASK] = *cmd;

	atomic_store_explicit(&ring->head, head + 1, memory_order_release);
	return true;
}

spel_hidden bool spel_audio_cmd_pop(spel_audio_cmd_ring* ring, spel_audio_cmd* out)
{
	unsigned int tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
	unsigned int head = atomic_load_explicit(&ring->head, memory_order_acquire);

	if (tail == head)
	{
		return false;
	}

	*out = ring->buffer[tail & SPEL_AUDIO_CMD_RING_MASK];

	atomic_store_explicit(&ring->tail, tail + 1, memory_order_release);
	return true;
}

static void pan_factors(float pan, float* l, float* r)
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

spel_hidden void spel_audio_cmd_process(spel_audio_mixer_t* mixer,
										spel_audio_cmd_ring* ring)
{
	spel_audio_cmd cmd;

	while (spel_audio_cmd_pop(ring, &cmd))
	{
		switch (cmd.type)
		{
		case SPEL_AUDIO_CMD_MASTER_LIMITER_PARAMS:
			mixer->limiter_threshold = cmd.floats[0];
			mixer->limiter_attack    = cmd.floats[1];
			mixer->limiter_release   = cmd.floats[2];
			continue;

		case SPEL_AUDIO_CMD_MASTER_LIMITER_ENABLE:
			mixer->limiter_enabled = cmd.bool_value;
			if (!cmd.bool_value)
			{
				mixer->limiter_peak[0] = 0.0F;
				mixer->limiter_peak[1] = 0.0F;
			}
			continue;

		case SPEL_AUDIO_CMD_MASTER_COMPRESSOR_PARAMS:
			mixer->compressor_threshold = cmd.floats[0];
			mixer->compressor_ratio     = cmd.floats[1];
			mixer->compressor_attack    = cmd.floats[2];
			mixer->compressor_release   = cmd.floats[3];
			continue;

		case SPEL_AUDIO_CMD_MASTER_COMPRESSOR_ENABLE:
			mixer->compressor_enabled = cmd.bool_value;
			if (!cmd.bool_value)
			{
				mixer->compressor_env[0] = 0.0F;
				mixer->compressor_env[1] = 0.0F;
			}
			continue;

		case SPEL_AUDIO_CMD_BUS_VOLUME:
		{
			uint32_t bi = (uint32_t)cmd.voice_index;
			if (bi < mixer->bus_count)
			{
				mixer->buses[bi].volume = cmd.float_value;
			}
			continue;
		}

		case SPEL_AUDIO_CMD_BUS_MUTE:
		{
			uint32_t bi = (uint32_t)cmd.voice_index;
			if (bi < mixer->bus_count)
			{
				mixer->buses[bi].mute = cmd.bool_value;
			}
			continue;
		}

		case SPEL_AUDIO_CMD_BUS_SOLO:
		{
			uint32_t bi = (uint32_t)cmd.voice_index;
			if (bi < mixer->bus_count)
			{
				mixer->buses[bi].solo = cmd.bool_value;
			}
			continue;
		}

		default:
			break;
		}

		int idx = voice_index_check(mixer, cmd.voice_index);
		if (idx < 0)
		{
			continue;
		}

		spel_audio_voice_t* v = &mixer->voices[idx];

		switch (cmd.type)
		{
		case SPEL_AUDIO_CMD_PLAY:
			atomic_store_explicit(&v->playing, true, memory_order_release);
			atomic_store_explicit(&v->done, false, memory_order_release);
			atomic_store_explicit(
				&v->start_frame,
				atomic_load_explicit(&mixer->frame_counter, memory_order_relaxed),
				memory_order_release);
			break;

		case SPEL_AUDIO_CMD_STOP:
			atomic_store_explicit(&v->playing, false, memory_order_release);
			if (v->decoder)
			{
				ma_decoder_seek_to_pcm_frame(v->decoder, 0);
			}
			break;

		case SPEL_AUDIO_CMD_VOLUME:
			v->volume = cmd.float_value;
			break;

		case SPEL_AUDIO_CMD_PAN:
			v->pan = cmd.float_value;
			pan_factors(cmd.float_value, &v->pan_l, &v->pan_r);
			break;

		case SPEL_AUDIO_CMD_DESTROY:
			atomic_store_explicit(&v->playing, false, memory_order_release);
			atomic_store_explicit(&v->active, false, memory_order_release);
			break;

		case SPEL_AUDIO_CMD_LOOP:
			atomic_store_explicit(&v->looping, cmd.bool_value, memory_order_release);
			break;

		case SPEL_AUDIO_CMD_PAUSE:
			atomic_store_explicit(&v->playing, false, memory_order_release);
			break;

		case SPEL_AUDIO_CMD_DISTORTION_DRIVE:
			if (v->distortion)
			{
				v->distortion->drive = cmd.float_value;
			}
			break;

		case SPEL_AUDIO_CMD_LPF_COEFF:
			if (v->lpf)
			{
				v->lpf->coeff = cmd.float_value;
			}
			break;

		case SPEL_AUDIO_CMD_HPF_COEFF:
			if (v->hpf)
			{
				v->hpf->coeff = cmd.float_value;
			}
			break;

		case SPEL_AUDIO_CMD_DELAY_PARAMS:
			if (v->delay)
			{
				v->delay->feedback = cmd.floats[0];
				v->delay->mix = cmd.floats[1];
			}
			break;

		case SPEL_AUDIO_CMD_FLANGER_PARAMS:
			if (v->flanger)
			{
				v->flanger->rate = cmd.floats[0];
				v->flanger->depth_frames = cmd.floats[2];
				v->flanger->mix = cmd.floats[3];
			}
			break;

		case SPEL_AUDIO_CMD_CHORUS_PARAMS:
			if (v->chorus)
			{
				v->chorus->rate = cmd.floats[0];
				v->chorus->depth_frames = cmd.floats[1];
				v->chorus->mix = cmd.floats[2];
				v->chorus->voices = (int)cmd.floats[3];
			}
			break;

		case SPEL_AUDIO_CMD_EFFECT_PARAM_SET:
		{
			spel_audio_effect_array_t* _chain =
				atomic_load_explicit(&v->effect_chain, memory_order_acquire);
			if (_chain)
			{
				unsigned int si = (unsigned int)cmd.floats[0];
				unsigned int pi = (unsigned int)cmd.floats[1];
				if (si < _chain->count && pi < SPEL_AUDIO_CUSTOM_PARAM_COUNT)
				{
					_chain->slots[si].params[pi] = cmd.floats[2];
				}
			}
			break;
		}

		case SPEL_AUDIO_CMD_PITCH_SET:
			v->pitch = cmd.float_value;
			break;

		case SPEL_AUDIO_CMD_REVERB_PARAMS:
			if (v->reverb)
			{
				v->reverb->decay     = cmd.floats[0];
				v->reverb->damping   = cmd.floats[1];
				v->reverb->pre_delay = cmd.floats[2];
				v->reverb->mix       = cmd.floats[3];
			}
			break;

		case SPEL_AUDIO_CMD_VOICE_BUS:
		{
			uint32_t new_bus = (uint32_t)cmd.float_value;
			if (new_bus < mixer->bus_count)
			{
				v->bus_id = new_bus;
			}
			break;
		}

		default:
			break;
		}
	}
}
