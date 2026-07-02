#ifndef SPEL_AUDIO_INTERNAL
#define SPEL_AUDIO_INTERNAL
#include "audio/audio_types.h"
#include "utils/internal/miniaudio.h"

#define SPEL_AUDIO_MAX_VOICES 32

struct spel_audio_voice_t
{
	ma_decoder* decoder;
	float       volume;
	float       pan;
	float       pan_l;
	float       pan_r;
	bool        playing;
	bool        looping;
	bool        active;
	bool        fire_forget;
	bool        done;
};

typedef struct spel_audio_voice_t spel_audio_voice_t;

typedef struct
{
	spel_audio_voice_t voices[SPEL_AUDIO_MAX_VOICES];
} spel_audio_mixer_t;

struct spel_audio_t
{
	ma_device         device;
	spel_audio_config config;
	spel_audio_mixer_t mixer;
	float*            scratch;
	uint32_t          channels;
	uint32_t          sample_rate;
};

typedef struct spel_audio_t spel_audio_state_t;

struct spel_audio_source_t
{
	char* path;
};

typedef struct spel_audio_source_t spel_audio_source_t;

void spel_audio_mixer_process(
	spel_audio_mixer_t* mixer,
	float*              output,
	ma_uint32           frameCount,
	uint32_t            channels,
	float*              scratch);

#endif
