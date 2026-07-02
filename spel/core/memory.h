#ifndef SPEL_MEMORY
#define SPEL_MEMORY
#include "core/macros.h"
#include <stddef.h>
#include <stdint.h>

typedef enum
{
	SPEL_MEM_TAG_CORE,
	SPEL_MEM_TAG_GFX,
	SPEL_MEM_TAG_AUDIO,
	SPEL_MEM_TAG_MISC,
	SPEL_MEM_TAG_TEMP, // temporal / ephemeral
	SPEL_MEM_TAG_COUNT
} spel_memory_tag;

typedef struct spel_memory_tag_stats
{
	size_t bytes_current;
	size_t bytes_peak;
	uint64_t alloc_count;
	uint64_t free_count;
	uint64_t largest_block;
} spel_memory_tag_stats;

typedef struct spel_memory
{
	size_t current;
	size_t peak;
	size_t total_allocated;
	size_t total_freed;

	size_t header_allocated;
	size_t header_freed;

	uint64_t alloc_count;
	uint64_t free_count;

	spel_memory_tag_stats tags[SPEL_MEM_TAG_COUNT];
} spel_memory;

spel_api void* spel_memory_malloc(size_t size, spel_memory_tag tag);
spel_api void spel_memory_free(void* ptr);
spel_api void* spel_memory_realloc(void* ptr, size_t newSize, spel_memory_tag tag);
const spel_api char* spel_memory_fmt_size(size_t bytes, char buf[32], bool colors);

// not really memory-related, but i don't feel like writing string.h again
spel_api char* spel_memory_strdup(const char* src, spel_memory_tag tag);

spel_api void spel_memory_dump_terminal();

spel_hidden void spel_memory_sdl_setup();

#endif