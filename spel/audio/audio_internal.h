#ifndef SPEL_AUDIO_INTERNAL
#define SPEL_AUDIO_INTERNAL
#include "audio/audio_mixer.h"
#include "audio/audio_types.h"
#include "utils/internal/miniaudio.h"
#include <stdatomic.h>
#include <stdint.h>

#define SPEL_AUDIO_MAX_VOICES 48
#define SPEL_AUDIO_CMD_RING_SIZE 64 /* pot */
#define SPEL_AUDIO_CMD_RING_MASK (SPEL_AUDIO_CMD_RING_SIZE - 1)
#define SPEL_AUDIO_CUSTOM_PARAM_COUNT 4

typedef enum
{
	SPEL_AUDIO_CMD_NONE = 0,
	SPEL_AUDIO_CMD_PLAY,
	SPEL_AUDIO_CMD_STOP,
	SPEL_AUDIO_CMD_VOLUME,
	SPEL_AUDIO_CMD_PAN,
	SPEL_AUDIO_CMD_DESTROY,
	SPEL_AUDIO_CMD_LOOP,
	SPEL_AUDIO_CMD_PAUSE,
	SPEL_AUDIO_CMD_DISTORTION_DRIVE,
	SPEL_AUDIO_CMD_LPF_COEFF,
	SPEL_AUDIO_CMD_HPF_COEFF,
	SPEL_AUDIO_CMD_DELAY_PARAMS,
	SPEL_AUDIO_CMD_FLANGER_PARAMS,
	SPEL_AUDIO_CMD_CHORUS_PARAMS,
	SPEL_AUDIO_CMD_EFFECT_PARAM_SET,
	SPEL_AUDIO_CMD_PITCH_SET,
	SPEL_AUDIO_CMD_MASTER_LIMITER_PARAMS,
	SPEL_AUDIO_CMD_MASTER_LIMITER_ENABLE,
	SPEL_AUDIO_CMD_MASTER_COMPRESSOR_PARAMS,
	SPEL_AUDIO_CMD_MASTER_COMPRESSOR_ENABLE,
	SPEL_AUDIO_CMD_REVERB_PARAMS,
} spel_audio_cmd_type;

typedef struct
{
	spel_audio_cmd_type type;
	int voice_index;
	union
	{
		float float_value; /* single-float params */
		bool bool_value;   /* toggle params */
		float floats[4];
	};
} spel_audio_cmd;

typedef struct
{
	spel_audio_cmd buffer[SPEL_AUDIO_CMD_RING_SIZE];
	atomic_uint head;
	atomic_uint tail;
} spel_audio_cmd_ring;

typedef struct
{
	float drive;
} spel_audio_effect_distortion_t;

typedef struct
{
	float coeff;
	float prev[2];
} spel_audio_effect_onepole_t;

typedef struct
{
	float* buffer;
	uint32_t cap;
	uint32_t delay_frames;
	uint32_t wpos;
	float feedback;
	float mix;
} spel_audio_effect_delay_t;

typedef struct
{
	float* buffer;
	uint32_t cap;
	uint32_t wpos;
	float lfo_phase;
	float rate;
	float depth_frames;
	float mix;
} spel_audio_effect_flanger_t;

typedef struct
{
	float* buffer;
	uint32_t cap;
	uint32_t wpos;
	float lfo_phase[4];
	float base_delay[4];
	float rate;
	float depth_frames;
	float mix;
	int voices;
} spel_audio_effect_chorus_t;

typedef struct
{
	float* buffer;
	uint32_t cap;
	uint32_t wpos;

	float decay;
	float damping;
	float pre_delay;
	float mix;

	uint32_t pre_offset;
	uint32_t pre_len;
	uint32_t comb_l_offset[4];
	uint32_t comb_l_len[4];
	uint32_t comb_r_offset[4];
	uint32_t comb_r_len[4];
	uint32_t ap_l_offset[2];
	uint32_t ap_r_offset[2];
	uint32_t ap_len[2];

	float damp_prev[2][4];
} spel_audio_effect_reverb_t;

typedef struct
{
	spel_audio_effect_fn callback;
	void* user_data;
	float params[SPEL_AUDIO_CUSTOM_PARAM_COUNT];
} spel_audio_effect_slot_t;

typedef struct
{
	uint32_t count;
	spel_audio_effect_slot_t slots[];
} spel_audio_effect_array_t;

struct spel_audio_voice_t
{
	ma_decoder* decoder;
	float volume;
	float pan;
	float pan_l;
	float pan_r;
	atomic_bool playing;
	atomic_bool looping;
	atomic_bool active;
	bool fire_forget;
	atomic_bool done;
	atomic_uint start_frame;
	struct desc_bridge* desc_bridge; // non-null only for custom decoders
	spel_audio_effect_distortion_t* distortion;
	spel_audio_effect_onepole_t* lpf;
	spel_audio_effect_onepole_t* hpf;
	spel_audio_effect_delay_t* delay;
	spel_audio_effect_flanger_t* flanger;
	spel_audio_effect_chorus_t* chorus;
	spel_audio_effect_reverb_t* reverb;
	_Atomic(spel_audio_effect_array_t*) effect_chain;
	spel_audio_effect_array_t* effect_chain_to_free;
	float pitch;
	float* pitch_buf;
	uint32_t pitch_buf_cap;
};

typedef struct spel_audio_voice_t spel_audio_voice_t;

typedef struct
{
	spel_audio_voice_t voices[SPEL_AUDIO_MAX_VOICES];
	atomic_uint frame_counter;
	bool limiter_enabled;
	float limiter_threshold;
	float limiter_attack;
	float limiter_release;
	float limiter_peak[2];

	bool compressor_enabled;
	float compressor_threshold;
	float compressor_ratio;
	float compressor_attack;
	float compressor_release;
	float compressor_env[2];
} spel_audio_mixer_t;

struct spel_audio_t
{
	ma_device device;
	spel_audio_config config;
	spel_audio_mixer_t mixer;
	spel_audio_cmd_ring cmd_ring;
	float* scratch;
	uint32_t channels;
	uint32_t sample_rate;
};

typedef struct spel_audio_t spel_audio_state_t;

struct spel_audio_source_t
{
	char* path;
};

typedef struct spel_audio_source_t spel_audio_source_t;

spel_hidden bool spel_audio_cmd_push(spel_audio_cmd_ring* ring,
									 const spel_audio_cmd* cmd);
spel_hidden bool spel_audio_cmd_pop(spel_audio_cmd_ring* ring, spel_audio_cmd* out);
spel_hidden void spel_audio_cmd_process(spel_audio_mixer_t* mixer,
										spel_audio_cmd_ring* ring);

void spel_audio_mixer_process(spel_audio_mixer_t* mixer, float* output,
							  ma_uint32 frameCount, uint32_t channels, float* scratch,
							  uint32_t sampleRate);

void spel_audio_master_process(spel_audio_mixer_t* mixer, float* output,
							   ma_uint32 frameCount, uint32_t channels,
							   uint32_t sampleRate);

spel_hidden void spel_audio_voice_free_effects(spel_audio_voice_t* v);

#endif
