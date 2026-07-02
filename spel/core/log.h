#ifndef SPEL_LOG
#define SPEL_LOG
#include "core/macros.h"
#include "core/types.h"
#include "utils/time.h"
#include <stddef.h>
#include <stdint.h>
#include <time.h>

enum
{
	SPEL_ERR_NONE = 0,

	SPEL_ERR_OOM,
	SPEL_ERR_INVALID_ARGUMENT,
	SPEL_ERR_INVALID_STATE,
	SPEL_ERR_INVALID_RESOURCE,

	SPEL_ERR_CONTEXT_FAILED,
	SPEL_ERR_WINDOWING_FAILED,
	SPEL_ERR_SHADER_FAILED,
	SPEL_ERR_SHADER_REFLECTION_FAILED,
	SPEL_ERR_SHADER_REFLECTION_MISMATCH,

	SPEL_ERR_FILE_NOT_FOUND,
	SPEL_ERR_ASSERTION_FAILED,
	SPEL_ERR_INTERNAL,
};

enum
{
	SPEL_DATA_NONE = 0,
	SPEL_DATA_STRING,
	SPEL_DATA_SHADER_LOG,
	SPEL_DATA_GFX_MSG
};

typedef uint8_t spel_error_code;
typedef uint8_t spel_log_data_type;

typedef struct spel_log_event_t
{
	spel_severity severity;
	spel_error_code code;
	spel_log_data_type data_type;

	uint32_t length;
	char* message;
	bool message_owned;

	uint64_t timestamp;
	time_t wall_sec;

	const char* file;
	int line;

	const void* data;
	size_t data_size;
} spel_log_event_t;

spel_api void spel_log_filter(spel_severity severity);
spel_api void spel_log_callback_set(spel_log_fn fn, void* user);
spel_api void spel_log_emit(spel_log_event evt);
spel_api spel_log_event spel_log_fmt(spel_log_event evt, const char* fmt, ...);

spel_api void spel_log_assert(bool condition, spel_log_event evt);

spel_api void spel_log_stderr_install();
spel_hidden void spel_log_stderr(spel_log_event evt, void* user);

const spel_api char* spel_log_sev_to_string(spel_severity severity);

spel_api void spel_log_panic(spel_log_event evt);

#define spel_panic(err, msg, ...)                                                        \
	spel_log_panic(spel_log_fmt(&(spel_log_event_t){.severity = (SPEL_SEV_FATAL),        \
													.code = (err),                       \
													.message = NULL,                     \
													.length = 0,                         \
													.file = __FILE__,                    \
													.line = __LINE__,                    \
													.data = NULL,                        \
													.data_type = SPEL_DATA_NONE,         \
													.data_size = 0},                     \
								msg, ##__VA_ARGS__))

#define spel_log(sev, error, dataVal, dataType, dataSize, msg, ...)                      \
	spel_log_emit(spel_log_fmt(&(spel_log_event_t){.severity = (sev),                    \
												   .code = (error),                      \
												   .message = NULL,                      \
												   .length = 0,                          \
												   .file = __FILE__,                     \
												   .line = __LINE__,                     \
												   .data = (dataVal),                    \
												   .data_type = (dataType),              \
												   .data_size = (dataSize),              \
												   .timestamp = spel_time_now_ns(),      \
												   .wall_sec = spel_time_now_sec()},     \
							   msg, ##__VA_ARGS__))

#define spel_info(msg, ...)                                                              \
	spel_log(SPEL_SEV_INFO, SPEL_ERR_NONE, NULL, SPEL_DATA_NONE, 0, msg, ##__VA_ARGS__)
#define spel_warn(msg, ...)                                                              \
	spel_log(SPEL_SEV_WARN, SPEL_ERR_NONE, NULL, SPEL_DATA_NONE, 0, msg, ##__VA_ARGS__)
#define spel_error(code, msg, ...)                                                       \
	spel_log(SPEL_SEV_ERROR, code, NULL, SPEL_DATA_NONE, 0, msg, ##__VA_ARGS__)
#define spel_trace(msg, ...)                                                             \
	spel_log(SPEL_SEV_TRACE, SPEL_ERR_NONE, NULL, SPEL_DATA_NONE, 0, msg, ##__VA_ARGS__)

#ifdef DEBUG
#	define spel_debug(msg, ...)                                                         \
		spel_log(SPEL_SEV_DEBUG, SPEL_ERR_NONE, NULL, SPEL_DATA_NONE, 0, msg,            \
				 ##__VA_ARGS__)
#	define spel_assert(condition, msg, ...)                                             \
		do                                                                               \
		{                                                                                \
			if (!(condition))                                                            \
			{                                                                            \
				spel_log_panic(spel_log_fmt(                                             \
					&(spel_log_event_t){.severity = (SPEL_SEV_FATAL),                    \
										.code = (SPEL_ERR_ASSERTION_FAILED),             \
										.message = "assertion failed: " #condition,      \
										.length = 0,                                     \
										.file = __FILE__,                                \
										.line = __LINE__,                                \
										.data = NULL,                                    \
										.data_type = SPEL_DATA_NONE,                     \
										.data_size = 0},                                 \
					msg, ##__VA_ARGS__));                                                \
			}                                                                            \
		} while (0)
#else
#	define spel_debug(msg, ...)
#	define spel_assert(condition, msg, ...)
#endif
#endif
