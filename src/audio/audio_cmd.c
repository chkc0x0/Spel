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

static void process_bus_cmd(spel_audio_mixer_t* mixer, const spel_audio_cmd* cmd)
{
	uint32_t bi = (uint32_t)cmd->voice_index;
	if (bi >= mixer->bus_count)
	{
		return;
	}

	switch (cmd->type)
	{
	case SPEL_AUDIO_CMD_BUS_VOLUME:
		mixer->buses[bi].volume = cmd->float_value;
		return;

	case SPEL_AUDIO_CMD_BUS_MUTE:
		mixer->buses[bi].mute = cmd->bool_value;
		return;

	case SPEL_AUDIO_CMD_BUS_SOLO:
		mixer->buses[bi].solo = cmd->bool_value;
		return;

	case SPEL_AUDIO_CMD_BUS_LIMITER_PARAMS:
		if (mixer->buses[bi].dsp.limiter)
		{
			mixer->buses[bi].dsp.limiter->threshold = cmd->floats[0];
			mixer->buses[bi].dsp.limiter->attack    = cmd->floats[1];
			mixer->buses[bi].dsp.limiter->release   = cmd->floats[2];
		}
		return;

	case SPEL_AUDIO_CMD_BUS_LIMITER_ENABLE:
		if (!cmd->bool_value)
		{
			spel_audio_dsp_free(&mixer->buses[bi].dsp);
		}
		return;

	case SPEL_AUDIO_CMD_BUS_COMPRESSOR_PARAMS:
		if (mixer->buses[bi].dsp.compressor)
		{
			mixer->buses[bi].dsp.compressor->threshold = cmd->floats[0];
			mixer->buses[bi].dsp.compressor->ratio     = cmd->floats[1];
			mixer->buses[bi].dsp.compressor->attack    = cmd->floats[2];
			mixer->buses[bi].dsp.compressor->release   = cmd->floats[3];
		}
		return;

	case SPEL_AUDIO_CMD_BUS_COMPRESSOR_ENABLE:
		if (!cmd->bool_value)
		{
			spel_audio_dsp_free(&mixer->buses[bi].dsp);
		}
		return;

	default:
		return;
	}
}

static void process_voice_cmd(spel_audio_mixer_t* mixer, spel_audio_voice_t* v,
							   const spel_audio_cmd* cmd)
{
	(void)mixer;

	switch (cmd->type)
	{
	case SPEL_AUDIO_CMD_PLAY:
		atomic_store_explicit(&v->playing, true, memory_order_release);
		atomic_store_explicit(&v->done, false, memory_order_release);
		return;

	case SPEL_AUDIO_CMD_STOP:
		atomic_store_explicit(&v->playing, false, memory_order_release);
		if (v->decoder)
		{
			ma_decoder_seek_to_pcm_frame(v->decoder, 0);
		}
		return;

	case SPEL_AUDIO_CMD_VOLUME:
		v->volume = cmd->float_value;
		return;

	case SPEL_AUDIO_CMD_PAN:
		v->pan = cmd->float_value;
		pan_factors(cmd->float_value, &v->pan_l, &v->pan_r);
		return;

	case SPEL_AUDIO_CMD_DESTROY:
		atomic_store_explicit(&v->playing, false, memory_order_release);
		atomic_store_explicit(&v->active, false, memory_order_release);
		return;

	case SPEL_AUDIO_CMD_LOOP:
		atomic_store_explicit(&v->looping, cmd->bool_value, memory_order_release);
		return;

	case SPEL_AUDIO_CMD_PAUSE:
		atomic_store_explicit(&v->playing, false, memory_order_release);
		return;

	case SPEL_AUDIO_CMD_DISTORTION_DRIVE:
		if (v->dsp.distortion)
		{
			v->dsp.distortion->drive = cmd->float_value;
		}
		return;

	case SPEL_AUDIO_CMD_LPF_COEFF:
		if (v->dsp.lpf)
		{
			v->dsp.lpf->coeff = cmd->float_value;
		}
		return;

	case SPEL_AUDIO_CMD_HPF_COEFF:
		if (v->dsp.hpf)
		{
			v->dsp.hpf->coeff = cmd->float_value;
		}
		return;

	case SPEL_AUDIO_CMD_DELAY_PARAMS:
		if (v->dsp.delay)
		{
			v->dsp.delay->feedback = cmd->floats[0];
			v->dsp.delay->mix = cmd->floats[1];
		}
		return;

	case SPEL_AUDIO_CMD_FLANGER_PARAMS:
		if (v->dsp.flanger)
		{
			v->dsp.flanger->rate = cmd->floats[0];
			v->dsp.flanger->depth_frames = cmd->floats[2];
			v->dsp.flanger->mix = cmd->floats[3];
		}
		return;

	case SPEL_AUDIO_CMD_CHORUS_PARAMS:
		if (v->dsp.chorus)
		{
			v->dsp.chorus->rate = cmd->floats[0];
			v->dsp.chorus->depth_frames = cmd->floats[1];
			v->dsp.chorus->mix = cmd->floats[2];
			v->dsp.chorus->voices = (int)cmd->floats[3];
		}
		return;

	case SPEL_AUDIO_CMD_EFFECT_PARAM_SET:
	{
		spel_audio_effect_array_t* chain =
			atomic_load_explicit(&v->dsp.effect_chain, memory_order_acquire);
		if (chain)
		{
			unsigned int si = (unsigned int)cmd->floats[0];
			unsigned int pi = (unsigned int)cmd->floats[1];
			if (si < chain->count && pi < SPEL_AUDIO_CUSTOM_PARAM_COUNT)
			{
				chain->slots[si].params[pi] = cmd->floats[2];
			}
		}
		return;
	}

	case SPEL_AUDIO_CMD_REVERB_PARAMS:
		if (v->dsp.reverb)
		{
			v->dsp.reverb->decay     = cmd->floats[0];
			v->dsp.reverb->damping   = cmd->floats[1];
			v->dsp.reverb->pre_delay = cmd->floats[2];
			v->dsp.reverb->mix       = cmd->floats[3];
		}
		return;

	case SPEL_AUDIO_CMD_PITCH_SET:
		v->pitch = cmd->float_value;
		return;

	case SPEL_AUDIO_CMD_VOICE_BUS:
	{
		uint32_t new_bus = (uint32_t)cmd->float_value;
		if (new_bus < mixer->bus_count)
		{
			v->bus_id = new_bus;
		}
		return;
	}

	default:
		return;
	}
}

static inline bool is_bus_cmd(spel_audio_cmd_type type)
{
	return (type >= SPEL_AUDIO_CMD_BUS_VOLUME &&
		   type <= SPEL_AUDIO_CMD_BUS_COMPRESSOR_ENABLE) != 0;
}

spel_hidden void spel_audio_cmd_process(spel_audio_mixer_t* mixer,
										spel_audio_cmd_ring* ring)
{
	spel_audio_cmd cmd;

	while (spel_audio_cmd_pop(ring, &cmd))
	{
		if (is_bus_cmd(cmd.type))
		{
			process_bus_cmd(mixer, &cmd);
			continue;
		}

		int idx = voice_index_check(mixer, cmd.voice_index);
		if (idx < 0)
		{
			continue;
		}

		process_voice_cmd(mixer, &mixer->voices[idx], &cmd);
	}
}
