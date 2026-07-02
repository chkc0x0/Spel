#include "audio/audio_source.h"
#include "audio/audio_internal.h"
#include "core/log.h"
#include "core/memory.h"
#include <string.h>

spel_api spel_audio_source spel_audio_source_load(const char* path)
{
	if (!path || !*path)
	{
		spel_error(SPEL_ERR_INVALID_ARGUMENT, "audio source path is NULL or empty");
		return NULL;
	}

	ma_decoder test;
	ma_result result = ma_decoder_init_file(path, NULL, &test);
	if (result != MA_SUCCESS)
	{
		spel_error(SPEL_ERR_FILE_NOT_FOUND, "failed to load audio source \"%s\": %s",
				   path, ma_result_description(result));
		return NULL;
	}
	ma_decoder_uninit(&test);

	spel_audio_source_t* src = (spel_audio_source_t*)spel_memory_malloc(
		sizeof(spel_audio_source_t), SPEL_MEM_TAG_AUDIO);
	if (!src)
	{
		return NULL;
	}

	src->path = spel_memory_strdup(path, SPEL_MEM_TAG_AUDIO);
	return (spel_audio_source)src;
}

spel_api void spel_audio_source_destroy(spel_audio_source source)
{
	if (!source)
	{
		return;
	}

	spel_audio_source_t* src = (spel_audio_source_t*)source;
	spel_memory_free(src->path);
	spel_memory_free(src);
}
