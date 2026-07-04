// lets make pong
#include "audio/audio.h"
#include "core/log.h"
#include "core/types.h"
#include "gfx/gfx.h"
#include "input/input.h"
#include <math.h>

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

static spel_audio_voice bgm;

static bool fx_distortion_on;
static bool fx_lpf_on;
static bool fx_hpf_on;
static bool fx_delay_on;
static bool fx_flanger_on;
static bool fx_chorus_on;

static bool fx_reverb_on;

static bool fx_limiter_on;
static bool fx_compressor_on;

static int fx_bitcrush_slot = -1;
static float fx_decim = 4.0f;
static float fx_bits = 16.0f;

static uint32_t bitcrush_hold_count;
static float bitcrush_last[2];

static int fx_bitcrush_decim_idx = 2;
static int fx_bitcrush_bit_idx = 0;
static const float fx_bitcrush_decim_presets[] = {1, 2, 4, 8, 16, 32, 64};
static const float fx_bitcrush_bit_presets[] = {16, 8, 4, 2, 1};

static const float pitch_presets[] = {1.0f, 0.5f, 0.75f, 1.25f, 1.5f, 2.0f};
static const int pitch_preset_count = sizeof(pitch_presets) / sizeof(pitch_presets[0]);
static int fx_pitch_idx; /* index into pitch_presets */

static bool wall_was_at_top;
static bool wall_was_at_bottom;
static bool hit_player_paddle;
static bool hit_enemy_paddle;

static void bitcrush_cb(float* samples, uint32_t frameCount, uint32_t channels,
						uint32_t sampleRate, spel_audio_custom_effect_ctx* ctx)
{
	float bit_depth = ctx->params[0]; /* 1..16 */
	float decim_f = ctx->params[1];	  /* 1..64 */

	if (bit_depth < 1.0f)
		bit_depth = 16.0f;
	if (decim_f < 1.0f)
		decim_f = 1.0f;

	float steps = powf(2.0f, bit_depth);
	float scale = 1.0f / steps;
	uint32_t decim = (uint32_t)(decim_f + 0.5f);
	if (decim < 1)
		decim = 1;

	for (uint32_t f = 0; f < frameCount; f++)
	{
		uint32_t decim_idx = (f + bitcrush_hold_count) % decim;

		for (uint32_t c = 0; c < channels && c < 2; c++)
		{
			uint32_t idx = f * channels + c;

			if (decim_idx == 0)
				bitcrush_last[c] = samples[idx];
			samples[idx] = roundf(bitcrush_last[c] * steps) * scale;
			if (decim_idx == 0)
				bitcrush_last[c] = samples[idx];
		}

		for (uint32_t c = 2; c < channels; c++)
		{
			samples[f * channels + c] = bitcrush_last[c & 1];
		}
	}
	bitcrush_hold_count = (bitcrush_hold_count + frameCount) % decim;
}

void spel_conf()
{
	
}

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
	sfx_wall = spel_audio_source_load("sfx_wall.wav");
	sfx_paddle = spel_audio_source_load("sfx_paddle.wav");
	sfx_score = spel_audio_source_load("sfx_score.wav");
	sfx_reset = spel_audio_source_load("sfx_reset.wav");

	bgm = spel_audio_voice_load("test.ogg");
	spel_audio_voice_play(bgm);
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

	if (spel_input_key_pressed(SPEL_KEY_1))
	{
		fx_distortion_on = !fx_distortion_on;
		spel_audio_voice_distortion_set(bgm, fx_distortion_on ? 4.0f : 0.0f);
	}
	if (spel_input_key_pressed(SPEL_KEY_2))
	{
		fx_lpf_on = !fx_lpf_on;
		spel_audio_voice_lpf_set(bgm, fx_lpf_on ? 400.0f : 0.0f);
	}
	if (spel_input_key_pressed(SPEL_KEY_3))
	{
		fx_hpf_on = !fx_hpf_on;
		spel_audio_voice_hpf_set(bgm, fx_hpf_on ? 5000.0f : 0.0f);
	}

	if (spel_input_key_pressed(SPEL_KEY_4))
	{
		fx_delay_on = !fx_delay_on;
		spel_audio_voice_delay_set(bgm, fx_delay_on ? 1500.0f : 0.0f, 0.4f, 0.35f);
	}
	if (spel_input_key_pressed(SPEL_KEY_5))
	{
		fx_flanger_on = !fx_flanger_on;
		spel_audio_voice_flanger_set(bgm, fx_flanger_on ? 2.0f : 0.0f,
									 fx_flanger_on ? 8.0f : 0.0f, 0.4f);
	}
	if (spel_input_key_pressed(SPEL_KEY_6))
	{
		fx_chorus_on = !fx_chorus_on;
		spel_audio_voice_chorus_set(bgm, fx_chorus_on ? 0.3f : 0.0f,
									fx_chorus_on ? 1.0f : 0.0f, 0.5f, 9);
	}

	if (spel_input_key_pressed(SPEL_KEY_7))
	{
		fx_reverb_on = !fx_reverb_on;
		spel_audio_voice_reverb_set(
			bgm, fx_reverb_on ? 0.84f : 0.0f, fx_reverb_on ? 0.4f : 0.0f,
			fx_reverb_on ? 30.0f : 0.0f, fx_reverb_on ? 0.5f : 0.0f);
	}

	if (spel_input_key_pressed(SPEL_KEY_0))
	{
		fx_pitch_idx = (fx_pitch_idx + 1) % pitch_preset_count;
		spel_audio_voice_pitch_set(bgm, pitch_presets[fx_pitch_idx]);
	}

	if (spel_input_key_pressed(SPEL_KEY_8))
	{
		fx_limiter_on = !fx_limiter_on;
		spel_audio_master_limiter_set(fx_limiter_on ? -6.0f : -999.0f,
									  fx_limiter_on ? 1.0f : 0.0f,
									  fx_limiter_on ? 10.0f : 0.0f);
	}
	if (spel_input_key_pressed(SPEL_KEY_9))
	{
		fx_compressor_on = !fx_compressor_on;
		spel_audio_master_compressor_set(
			fx_compressor_on ? -12.0f : -999.0f, fx_compressor_on ? 4.0f : 1.0f,
			fx_compressor_on ? 5.0f : 0.0f, fx_compressor_on ? 50.0f : 0.0f);
	}

	/* Bit crusher — key - (minus) toggle, = cycle sample rate (primary), BACKSPACE cycle
	 * bit depth */
	if (spel_input_key_pressed(SPEL_KEY_MINUS))
	{
		if (fx_bitcrush_slot >= 0)
		{
			spel_audio_voice_effect_remove(bgm, fx_bitcrush_slot);
			fx_bitcrush_slot = -1;
		}
		else
		{
			fx_decim = 4.0f;
			fx_bits = 16.0f;
			fx_bitcrush_decim_idx = 2;
			fx_bitcrush_bit_idx = 0;
			bitcrush_hold_count = 0;
			bitcrush_last[0] = bitcrush_last[1] = 0.0f;
			fx_bitcrush_slot = spel_audio_voice_effect_add(bgm, bitcrush_cb, NULL);
			spel_audio_voice_effect_param_set(bgm, fx_bitcrush_slot, 0, fx_bits);
			spel_audio_voice_effect_param_set(bgm, fx_bitcrush_slot, 1, fx_decim);
		}
	}
	if (fx_bitcrush_slot >= 0)
	{
		if (spel_input_key_pressed(SPEL_KEY_EQUALS))
		{
			fx_bitcrush_decim_idx = (fx_bitcrush_decim_idx + 1) % 7;
			fx_decim = fx_bitcrush_decim_presets[fx_bitcrush_decim_idx];
			spel_audio_voice_effect_param_set(bgm, fx_bitcrush_slot, 1, fx_decim);
		}
		if (spel_input_key_pressed(SPEL_KEY_BACKSPACE))
		{
			fx_bitcrush_bit_idx = (fx_bitcrush_bit_idx + 1) % 5;
			fx_bits = fx_bitcrush_bit_presets[fx_bitcrush_bit_idx];
			spel_audio_voice_effect_param_set(bgm, fx_bitcrush_slot, 0, fx_bits);
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
		{
			// spel_audio_play(sfx_score, false);
		}
	}
	if (ball.center.x > spel.window.width && ball_reset_delay == -1.0F)
	{
		score_enemy++;
		ball_reset_delay = 0.75F;

		if (sfx_score)
		{
			// spel_audio_play(sfx_score, false);
		}
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

	spel_canvas_font_size_set(18);
	spel_canvas_fill_color_set(spel_color_hexa(0x00000088));
	spel_canvas_draw_rect(spel_rect(4, spel.window.height - 96, 820, 92));
	spel_canvas_fill_color_set(spel_color_hexa(0xFFFFFFFF));
	spel_canvas_print(spel_vec2(8, spel.window.height - 93),
					  "[1]dist:%-3s [2]lpf:%-3s [3]hpf:%-3s "
					  "[4]dly:%-3s [5]flg:%-3s [6]cho:%-3s [7]rev:%-3s",
					  fx_distortion_on ? "ON" : "off", fx_lpf_on ? "ON" : "off",
					  fx_hpf_on ? "ON" : "off", fx_delay_on ? "ON" : "off",
					  fx_flanger_on ? "ON" : "off", fx_chorus_on ? "ON" : "off",
					  fx_reverb_on ? "ON" : "off");
	spel_canvas_print(spel_vec2(8, spel.window.height - 69),
					  "[0]pitch:%.2fx  [8]lim:-%s  [9]cmp:-%s",
					  pitch_presets[fx_pitch_idx], fx_limiter_on ? "ON " : "off",
					  fx_compressor_on ? "ON " : "off");
	spel_canvas_print(spel_vec2(8, spel.window.height - 45),
					  "[-]bitcrush:%-3s  [=]sr:%.0fx  [BSP]bits:%.0f",
					  fx_bitcrush_slot >= 0 ? "ON" : "off", fx_decim, fx_bits);

	spel_canvas_end();
}
