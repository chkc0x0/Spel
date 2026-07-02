#include "core/entry.h"
#include "SDL3/SDL_hints.h"
#include "SDL3/SDL_init.h"
#include "audio/audio.h"
#include "core/event.h"
#include "core/log.h"
#include "core/types.h"
#include "core/window.h"
#include "gfx/gfx.h"
#include "gfx/gfx_types.h"
#include "input/input.h"
#include "input/input_internal.h"
#include "utils/terminal.h"
#include "utils/time.h"
#include <stdlib.h>

spel_api spel_context spel = {
	.window = {.title = "Spël",
			   .width = 800,
			   .height = 600,
			   .borderless = false,
			   .swapchain = {.vsync = 1, .depth = 16, .stencil = 0, .msaa = 1},
			   .resizable = false},
	.log = {.severity = SPEL_SEV_DEBUG}};

#ifdef SP_WEAK_LINK
int main(int argc, const char** argv)
{
	spel_app_desc app = (spel_app_desc){
		.argc = argc,
		.argv = argv,
		.gfx_backend = SPEL_GFX_BACKEND_OPENGL,
		.debug = spel_debug_build,
		.conf = spel_conf,
		.load = spel_load,
		.update = spel_update,
		.draw = spel_draw,
		.quit = spel_quit,
		.run = spel_run,
		.low_memory = spel_low_memory,
	};
	return spel_app_run(&app);
}
#endif

#ifdef SP_WEAK_LINK
#	ifndef _WIN32
spel_weak void spel_run()
#	else
spel_hidden void spel_run_fallback()
#	endif
#else
spel_api void spel_run()
#endif
{
	while (spel_window_running())
	{
		spel_run_frame();
	}
}

spel_hidden void spel_run_frame()
{
	spel_time_frame_begin(&spel.time);
	spel_input_update();
	spel_event_poll();
	spel_audio_cleanup();

	if (spel.app.update)
	{
		spel.app.update(spel.time.delta_unscaled);
	}

	spel_gfx_frame_begin(spel.gfx);
	spel_callback(spel.app.draw);
	spel_gfx_frame_present(spel.gfx);
}

spel_api bool spel_args_has(const char* arg)
{
	for (int i = 1; i < spel.process.argc; i++)
	{
		if (strcmp(spel.process.argv[i], arg) == 0)
		{
			return true;
		}
	}

	return false;
}

spel_api int spel_app_run(spel_app_desc* app)
{
#ifdef DEBUG
	uint64_t start_ns = spel_time_now_ns();
#endif
	spel_app_transform(app);

	spel.window.swapchain.vsync = 1; // vsync on by default
	spel.log.function = NULL;

	spel.window.x = 0;
	spel.window.y = 0;

	if (spel.process.argc > 0)
	{
		spel.process.name = spel.process.argv[0];
	}

	spel_memory_sdl_setup();

	spel_runtime_info_setup();
	spel_build_info_init();
	spel_log_stderr_install();
	spel_terminal_detect_ansi();
	spel_input_init();

#ifdef DEBUG
	spel.env.debug = true;
#endif

	if (spel_args_has("--debug"))
	{
		spel.env.debug = true;
	}

	spel_callback(spel.app.conf);

	SDL_SetHint(SDL_HINT_JOYSTICK_LINUX_DEADZONES, "1");

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		spel_panic(SPEL_ERR_WINDOWING_FAILED,
				   "failed to initialize windowing backend: %s", SDL_GetError());
		return -1;
	}

	spel_time_init(&spel.time);

	spel_gfx_context_conf(spel.app.gfx_backend);
	spel_window_create();

	spel_gfx_context_desc gfx_desc;
	gfx_desc.backend = spel.app.gfx_backend;
	gfx_desc.vsync = spel.window.swapchain.vsync;
	gfx_desc.debug = spel.env.debug;

	spel.gfx = spel_gfx_context_create(&gfx_desc);
	if (!spel_audio_init(NULL))
	{
		spel_error(SPEL_ERR_INTERNAL, "failed to initialize audio engine");
	}

	spel_callback(spel.app.load);

	spel_debug("started application in %dms", (spel_time_now_ns() - start_ns) / 1000000);
	spel_callback(spel.app.run);
	spel_callback(spel.app.quit);

	spel_audio_shutdown();
	spel_input_shutdown();
	spel_gfx_context_destroy(spel.gfx);
	spel_window_cleanup();
	spel_event_terminate();

	return 0;
}

spel_hidden void spel_app_transform(spel_app_desc* app)
{
	spel.process.argv = app->argv;
	spel.process.argc = app->argc;

	spel.env.debug = app->debug;
	spel.app.gfx_backend = app->gfx_backend;

	spel.app.conf = app->conf;
	spel.app.load = app->load;
	spel.app.run = app->run;
	spel.app.update = app->update;
	spel.app.draw = app->draw;
	spel.app.quit = app->quit;

	spel.app.low_memory = app->low_memory;

	if (spel.app.run == NULL)
	{
		spel.app.run = spel_run;
	}
}

spel_api spel_app_desc spel_app_desc_default()
{
	return (spel_app_desc){
		.argc = 0,
		.argv = NULL,
		.gfx_backend = SPEL_GFX_BACKEND_OPENGL,
		.debug = spel_debug_build,
		.conf = NULL,
		.load = NULL,
		.update = NULL,
		.draw = NULL,
		.quit = NULL,
		.run = NULL,
		.low_memory = NULL,
	};
}