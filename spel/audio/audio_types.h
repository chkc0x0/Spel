#ifndef SPEL_AUDIO_TYPES
#define SPEL_AUDIO_TYPES
#include "core/macros.h"
#include <stdint.h>

typedef struct spel_audio_t* spel_audio;
typedef struct spel_audio_source_t* spel_audio_source;
typedef struct spel_audio_voice_t* spel_audio_voice;

typedef struct
{
	uint32_t sample_rate;  // 0 = device default (48000)
	uint16_t channels;     // 0 = device default (2)
	uint32_t buffer_size;  // 0 = default (512 frames)
} spel_audio_config;

#endif
