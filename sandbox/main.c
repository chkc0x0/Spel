// lets make pong
#include "audio/audio.h"
#include "core/log.h"
#include "core/types.h"
#include "gfx/gfx.h"
#include "input/input.h"

static float ball_reset_delay;

static int score_enemy;
static int score_player;

static spel_circle ball;
static spel_rect paddle_enemy;
static spel_rect paddle_player;

static spel_vec2 ball_vel = {250, 150};

static spel_action axis_vertical;

// audio
static spel_audio_source sfx_wall;
static spel_audio_source sfx_paddle;
static spel_audio_source sfx_score;
static spel_audio_source sfx_reset;

// rising-edge guards to avoid replaying sounds each frame
static bool wall_was_at_top;
static bool wall_was_at_bottom;
static bool hit_player_paddle;
static bool hit_enemy_paddle;

void spel_load()
{
	// why an axis instead of spel_input_key?
	// axes provide a way to gather input in more than a simple "is key pressed"
	// way, by aggregating input from other sources (e.g: keyboard + gamepad).
	// this way the paddle can move with the w/s keys, the arrow keys, or a gamepad
	// if we desire
	axis_vertical = spel_input_action_create("vertical", SPEL_ACTION_ANALOG);
	spel_input_action_bind_axis(axis_vertical, SPEL_KEY_S, SPEL_KEY_W);

	// middle of the screen
	ball.center = spel_vec2((float)spel.window.width / 2, (float)spel.window.height / 2);
	ball.radius = 10;

	paddle_enemy = spel_rect(50, (spel.window.height / 2) - 50, 25, 100);
	paddle_player =
		spel_rect(spel.window.width - 75, (spel.window.height / 2) - 50, 25, 100);

	// load sound effects
	sfx_wall   = spel_audio_source_load("sfx_wall.wav");
	sfx_paddle = spel_audio_source_load("sfx_paddle.wav");
	sfx_score  = spel_audio_source_load("sfx_score.wav");
	sfx_reset  = spel_audio_source_load("sfx_reset.wav");
}

void spel_update(double dt)
{
	if (ball_reset_delay != -1.0F)
	{
		ball_reset_delay -= dt;
		if (ball_reset_delay <= 0.0F)
		{
			ball.center =
				spel_vec2((float)spel.window.width / 2, (float)spel.window.height / 2);
			ball_reset_delay = -1.0F;

			// ball respawned — play reset chirp
			if (sfx_reset)
				spel_audio_play(sfx_reset, false);
		}
	}

	// 500 px/s basically
	// we subtract instead of adding because, in screen space, y increases
	// as we move down. confusing, i know
	paddle_player.y -= (float)(500 * spel_input_action_value(axis_vertical) * dt);
	paddle_player.y =
		spel_math_clamp(paddle_player.y, 0, spel.window.height - paddle_player.height);

	ball.center.x += ball_vel.x * dt;
	ball.center.y += ball_vel.y * dt;

	// wall bounce top
	if (ball.center.y <= ball.radius)
	{
		ball_vel.y = fabsf(ball_vel.y);
		if (!wall_was_at_top && sfx_wall)
			spel_audio_play(sfx_wall, false);
		wall_was_at_top = true;
	}
	else
	{
		wall_was_at_top = false;
	}

	// wall bounce bottom
	if (ball.center.y >= spel.window.height - ball.radius)
	{
		ball_vel.y = -fabsf(ball_vel.y);
		if (!wall_was_at_bottom && sfx_wall)
			spel_audio_play(sfx_wall, false);
		wall_was_at_bottom = true;
	}
	else
	{
		wall_was_at_bottom = false;
	}

	// paddle hit (player)
	if (spel_circle_intersects_rect(ball, paddle_player))
	{
		ball_vel.x = -fabsf(ball_vel.x);
		float hit = (ball.center.y - paddle_player.y) / paddle_player.height;
		ball_vel.y = (hit - 0.5F) * 2.F * 400.F;

		if (!hit_player_paddle && sfx_paddle)
			spel_audio_play(sfx_paddle, false);
		hit_player_paddle = true;
	}
	else
	{
		hit_player_paddle = false;
	}

	// paddle hit (enemy)
	if (spel_circle_intersects_rect(ball, paddle_enemy))
	{
		ball_vel.x = fabsf(ball_vel.x);
		float hit = (ball.center.y - paddle_enemy.y) / paddle_enemy.height;
		ball_vel.y = (hit - 0.5F) * 2.F * 400.F;

		if (!hit_enemy_paddle && sfx_paddle)
			spel_audio_play(sfx_paddle, false);
		hit_enemy_paddle = true;
	}
	else
	{
		hit_enemy_paddle = false;
	}

	// enemy AI
	float enemy_center = paddle_enemy.y + 50.f; // paddle center
	float diff = ball.center.y - enemy_center;
	float move = spel_math_clamp(diff, -200.f * dt, 200.f * dt);
	paddle_enemy.y += move;
	paddle_enemy.y =
		spel_math_clamp(paddle_enemy.y, 0, spel.window.height - paddle_enemy.height);

	// scoring
	if (ball.center.x < 0 && ball_reset_delay == -1.0F)
	{
		score_player++;
		ball_reset_delay = 0.75F;

		if (sfx_score)
			spel_audio_play(sfx_score, false);
	}
	if (ball.center.x > spel.window.width && ball_reset_delay == -1.0F)
	{
		score_enemy++;
		ball_reset_delay = 0.75F;

		if (sfx_score)
			spel_audio_play(sfx_score, false);
	}
}

void spel_draw()
{
	spel_canvas_begin(NULL);
	spel_canvas_font_size_set(96);

	// court line
	// white but kinda transparent
	spel_canvas_color_set(spel_color_hexa(0xFFFFFF55));
	spel_canvas_draw_rect(
		spel_rect((spel.window.width / 2) - 2.5F, 0, 5, spel.window.height));

	spel_canvas_color_set(spel_color_white);
	spel_canvas_draw_rect(paddle_enemy);

	// why "spel.window.width - 75"?
	// so we can assure it always renders in the same spot (right side).
	// we subtract 75 because we want it to be 50px off the right side,
	// and we have to account for the paddle being 25px wide
	spel_canvas_draw_rect(paddle_player);

	spel_canvas_draw_circle(ball.center, ball.radius);

	spel_canvas_text_align_set(SPEL_CANVAS_ALIGN_RIGHT);
	spel_canvas_print(spel_vec2(spel.window.width / 2 - 25, 0), "%d", score_enemy);

	spel_canvas_text_align_set(SPEL_CANVAS_ALIGN_LEFT);
	spel_canvas_print(spel_vec2(spel.window.width / 2 + 25, 0), "%d", score_player);

	spel_canvas_font_size_set(16);
	spel_canvas_print(spel_vec2(100, 100), "%.2f", spel.time.time_scale);

	spel_canvas_font_size_set(256);
	spel_canvas_draw_text("H8G!$", spel_vec2(100, 300));

	spel_canvas_end();
}
