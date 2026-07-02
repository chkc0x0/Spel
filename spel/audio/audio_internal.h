#ifndef SPEL_AUDIO_INTERNAL
#define SPEL_AUDIO_INTERNAL
#include "audio/audio_types.h"
#include "utils/internal/miniaudio.h"
#include <stdatomic.h>
#include <stdint.h>

#define SPEL_AUDIO_MAX_VOICES     48
#define SPEL_AUDIO_CMD_RING_SIZE  64  /* power of 2 */
#define SPEL_AUDIO_CMD_RING_MASK  (SPEL_AUDIO_CMD_RING_SIZE - 1)

/* ── Command types for main-thread → audio-thread dispatch ── */

typedef enum
{
	SPEL_AUDIO_CMD_NONE = 0,
	SPEL_AUDIO_CMD_PLAY,
	SPEL_AUDIO_CMD_STOP,
	SPEL_AUDIO_CMD_VOLUME,
	SPEL_AUDIO_CMD_PAN,
	SPEL_AUDIO_CMD_DESTROY,
	SPEL_AUDIO_CMD_LOOP,
} spel_audio_cmd_type;

typedef struct
{
	spel_audio_cmd_type type;
	int                 voice_index;
	union
	{
		float float_value;   /* volume, pan */
		bool  bool_value;    /* loop */
	};
} spel_audio_cmd;

/* ── Lock-free SPSC ring buffer ──
 *
 * Single-producer (main thread), single-consumer (audio callback).
 * Head is written by producer, read by consumer.
 * Tail is written by consumer, read by producer.
 * All ordering via C11 atomics — no CAS, no mutexes.
 */

typedef struct
{
	spel_audio_cmd buffer[SPEL_AUDIO_CMD_RING_SIZE];
	atomic_uint    head;  /* producer: next slot to write   */
	atomic_uint    tail;  /* consumer: next slot to read    */
} spel_audio_cmd_ring;

/* ── Voice struct ────────────────────────────────────────
 *
 * Fields accessed by both threads are _Atomic.
 * Fields only written by the audio callback (via command
 * processing) and only read by the callback during mixing
 * are plain types.
 * fire_forget is set once on the main thread during create()
 * and only read on the main thread during cleanup() — never
 * touched by the callback.
 */

struct spel_audio_voice_t
{
	ma_decoder* decoder;
	float       volume;
	float       pan;
	float       pan_l;
	float       pan_r;
	atomic_bool playing;
	atomic_bool looping;
	atomic_bool active;
	bool        fire_forget;
	atomic_bool done;
	atomic_uint start_frame;
};

typedef struct spel_audio_voice_t spel_audio_voice_t;

typedef struct
{
	spel_audio_voice_t voices[SPEL_AUDIO_MAX_VOICES];
	atomic_uint frame_counter;
} spel_audio_mixer_t;

struct spel_audio_t
{
	ma_device         device;
	spel_audio_config config;
	spel_audio_mixer_t  mixer;
	spel_audio_cmd_ring cmd_ring;
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

/* ── Ring buffer API ── */

spel_hidden bool spel_audio_cmd_push(spel_audio_cmd_ring* ring, const spel_audio_cmd* cmd);
spel_hidden bool spel_audio_cmd_pop(spel_audio_cmd_ring* ring, spel_audio_cmd* out);
spel_hidden void spel_audio_cmd_process(spel_audio_mixer_t* mixer, spel_audio_cmd_ring* ring);

/* ── Mixer ── */

void spel_audio_mixer_process(
	spel_audio_mixer_t* mixer,
	float*              output,
	ma_uint32           frameCount,
	uint32_t            channels,
	float*              scratch);

#endif
