#ifndef SPEL_AUDIO_TYPES
#define SPEL_AUDIO_TYPES
#include "core/macros.h"
#include <stdint.h>

typedef struct spel_audio_t* spel_audio;
typedef struct spel_audio_source_t* spel_audio_source;
typedef struct spel_audio_voice_t* spel_audio_voice;

typedef struct
{
	uint32_t sample_rate; // 0 = device default (48000)
	uint16_t channels;	  // 0 = device default (2)
	uint32_t buffer_size; // 0 = default (512 frames)
} spel_audio_config;

typedef size_t (*spel_audio_read_fn)(void* userCtx, void* output, size_t count);
typedef int (*spel_audio_seek_fn)(void* userCtx, int64_t offset, int whence);

typedef enum
{
	SPEL_AUDIO_SOURCE_FILE,
	SPEL_AUDIO_SOURCE_MEMORY,
	SPEL_AUDIO_SOURCE_CALLBACKS,
} spel_audio_source_kind;

typedef struct
{
	spel_audio_source_kind type;
	union
	{
		struct
		{
			const char* path;
		} file;
		struct
		{
			const void* data;
			size_t size;
		} memory;
		struct
		{
			spel_audio_read_fn on_read;
			spel_audio_seek_fn on_seek;
			void* user_context;
		} callbacks;
	} source;
} spel_audio_source_desc;

#endif
