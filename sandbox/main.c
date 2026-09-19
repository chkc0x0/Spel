#include "gfx/gfx_canvas.h"
#include "utils/math.h"

void spel_draw()
{
	spel_canvas_begin(NULL);
	spel_canvas_draw_text("Hello from spël!", spel_vec2(100, 100));
	spel_canvas_end();
}