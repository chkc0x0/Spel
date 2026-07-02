#ifndef SPEL_AUDIO_DEVICE
#define SPEL_AUDIO_DEVICE
#include "core/macros.h"
#include "audio/audio_types.h"

spel_api bool spel_audio_init(const spel_audio_config* config);
spel_api void spel_audio_shutdown(void);

#endif
