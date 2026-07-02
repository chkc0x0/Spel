#ifndef SPEL_AUDIO_SOURCE
#define SPEL_AUDIO_SOURCE
#include "core/macros.h"
#include "audio/audio_types.h"

spel_api spel_audio_source spel_audio_source_load(const char* path);
spel_api void spel_audio_source_destroy(spel_audio_source source);

#endif
