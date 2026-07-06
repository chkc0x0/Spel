#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	SPEL_MAX_TRACKS = 64,
	SPEL_MAX_EVENTS = 4096,
	SPEL_MAX_NAME = 64,
	SPEL_MAX_PENDING = 256,
	SPEL_MAX_FILE_SIZE = (16U * 1024U * 1024U),
	SPEL_MAX_NOTES = 4096
};

typedef enum
{
	WAVE_SINE = 0,
	WAVE_SQUARE = 1,
	WAVE_SAW = 2,
	WAVE_TRIANGLE = 3,
	WAVE_NOISE = 4,
	WAVE_PULSE = 5,
} wave_t;

static const char* wave_name[] = {
	"SPEL_AUDIO_SYNTH_WAVE_SINE",  "SPEL_AUDIO_SYNTH_WAVE_SQUARE",
	"SPEL_AUDIO_SYNTH_WAVE_SAW",   "SPEL_AUDIO_SYNTH_WAVE_TRIANGLE",
	"SPEL_AUDIO_SYNTH_WAVE_NOISE", "SPEL_AUDIO_SYNTH_WAVE_PULSE",
};

static wave_t program_to_wave(int prog)
{
	if (prog < 0)
	{
		return WAVE_SQUARE;
	}
	if (prog <= 7)
	{
		return WAVE_SQUARE;
	}
	if (prog <= 15)
	{
		return WAVE_TRIANGLE;
	}
	if (prog <= 23)
	{
		return WAVE_PULSE;
	}
	if (prog <= 31)
	{
		return WAVE_SAW;
	}
	if (prog <= 39)
	{
		return WAVE_SAW;
	}
	if (prog <= 47)
	{
		return WAVE_TRIANGLE;
	}
	if (prog <= 55)
	{
		return WAVE_SAW;
	}
	if (prog <= 63)
	{
		return WAVE_SQUARE;
	}
	if (prog <= 71)
	{
		return WAVE_PULSE;
	}
	if (prog <= 79)
	{
		return WAVE_SQUARE;
	}
	if (prog <= 87)
	{
		return WAVE_SAW;
	}
	if (prog <= 95)
	{
		return WAVE_SQUARE;
	}
	if (prog <= 103)
	{
		return WAVE_NOISE;
	}
	if (prog <= 111)
	{
		return WAVE_TRIANGLE;
	}
	if (prog <= 119)
	{
		return WAVE_NOISE;
	}
	return WAVE_NOISE;
}

typedef struct
{
	float attack;
	float decay;
	float sustain;
	float release;
} adsr_t;

static adsr_t program_to_envelope(int prog, int channel)
{
	adsr_t env;

	if (channel == 9)
	{

		env.attack = 0.002F;
		env.decay = 0.08F;
		env.sustain = 0.0F;
		env.release = 0.10F;
		return env;
	}

	if (prog < 0)
	{
		prog = 0;
	}

	if (prog <= 7)
	{

		env.attack = 0.005F;
		env.decay = 0.15F;
		env.sustain = 0.45F;
		env.release = 0.60F;
	}
	else if (prog <= 15)
	{

		env.attack = 0.002F;
		env.decay = 0.05F;
		env.sustain = 0.10F;
		env.release = 0.80F;
	}
	else if (prog <= 23)
	{

		env.attack = 0.010F;
		env.decay = 0.05F;
		env.sustain = 1.00F;
		env.release = 0.05F;
	}
	else if (prog <= 31)
	{

		env.attack = 0.005F;
		env.decay = 0.08F;
		env.sustain = 0.55F;
		env.release = 0.35F;
	}
	else if (prog <= 39)
	{

		env.attack = 0.010F;
		env.decay = 0.08F;
		env.sustain = 0.80F;
		env.release = 0.25F;
	}
	else if (prog <= 47)
	{

		env.attack = 0.060F;
		env.decay = 0.20F;
		env.sustain = 0.80F;
		env.release = 0.60F;
	}
	else if (prog <= 55)
	{

		env.attack = 0.040F;
		env.decay = 0.15F;
		env.sustain = 0.90F;
		env.release = 0.50F;
	}
	else if (prog <= 63)
	{

		env.attack = 0.020F;
		env.decay = 0.10F;
		env.sustain = 0.90F;
		env.release = 0.30F;
	}
	else if (prog <= 71)
	{

		env.attack = 0.020F;
		env.decay = 0.10F;
		env.sustain = 0.70F;
		env.release = 0.30F;
	}
	else if (prog <= 79)
	{

		env.attack = 0.030F;
		env.decay = 0.10F;
		env.sustain = 0.80F;
		env.release = 0.30F;
	}
	else if (prog <= 87)
	{

		env.attack = 0.005F;
		env.decay = 0.05F;
		env.sustain = 1.00F;
		env.release = 0.30F;
	}
	else if (prog <= 95)
	{

		env.attack = 0.100F;
		env.decay = 0.30F;
		env.sustain = 1.00F;
		env.release = 0.60F;
	}
	else if (prog <= 103)
	{

		env.attack = 0.005F;
		env.decay = 0.10F;
		env.sustain = 0.40F;
		env.release = 0.50F;
	}
	else if (prog <= 111)
	{

		env.attack = 0.015F;
		env.decay = 0.10F;
		env.sustain = 0.70F;
		env.release = 0.30F;
	}
	else
	{

		env.attack = 0.002F;
		env.decay = 0.06F;
		env.sustain = 0.05F;
		env.release = 0.20F;
	}

	return env;
}

static float program_to_gain(int prog, int channel)
{
	if (channel == 9)
	{
		return 0.55F;
	}

	if (prog < 0)
	{
		prog = 0;
	}

	if (prog <= 7)
	{
		return 0.80F;
	}
	if (prog <= 15)
	{
		return 0.70F;
	}
	if (prog <= 23)
	{
		return 0.70F;
	}
	if (prog <= 31)
	{
		return 0.78F;
	}
	if (prog <= 39)
	{
		return 0.85F;
	}
	if (prog <= 47)
	{
		return 0.65F;
	}
	if (prog <= 55)
	{
		return 0.65F;
	}
	if (prog <= 63)
	{
		return 0.75F;
	}
	if (prog <= 71)
	{
		return 0.70F;
	}
	if (prog <= 79)
	{
		return 0.70F;
	}
	if (prog <= 87)
	{
		return 0.90F;
	}
	if (prog <= 95)
	{
		return 0.60F;
	}
	if (prog <= 103)
	{
		return 0.55F;
	}
	if (prog <= 111)
	{
		return 0.70F;
	}
	if (prog <= 119)
	{
		return 0.55F;
	}
	return 0.55F;
}

typedef struct
{
	float beat;
	float duration;
	uint8_t midi_note;
	float velocity;
} sheet_event_t;

static int event_cmp(const void* a, const void* b)
{
	const sheet_event_t* ea = (const sheet_event_t*)a;
	const sheet_event_t* eb = (const sheet_event_t*)b;
	if (ea->beat < eb->beat)
	{
		return -1;
	}
	if (ea->beat > eb->beat)
	{
		return 1;
	}
	return 0;
}

typedef struct
{
	char name[SPEL_MAX_NAME];
	char orig_name[SPEL_MAX_NAME];
	wave_t wave;
	int channel;
	int program;
	int num_events;
	int max_voices;
	sheet_event_t events[SPEL_MAX_EVENTS];
} parsed_track_t;

typedef struct
{
	uint8_t note;
	float start_beat;
	float velocity;
	bool active;
} pending_note_t;

static uint32_t read_u32be(const uint8_t** p)
{
	uint32_t v = ((uint32_t)(*p)[0] << 24) | ((uint32_t)(*p)[1] << 16) |
				 ((uint32_t)(*p)[2] << 8) | (uint32_t)(*p)[3];
	*p += 4;
	return v;
}

static uint16_t read_u16be(const uint8_t** p)
{
	uint16_t v = (uint16_t)(((uint32_t)(*p)[0] << 8) | (uint32_t)(*p)[1]);
	*p += 2;
	return v;
}

static uint32_t read_vlq(const uint8_t** p)
{
	uint32_t v = 0;
	uint8_t b;
	do
	{
		if (**p == 0 && v == 0)
		{
			(*p)++;
			return 0;
		}
		b = *(*p)++;
		v = (v << 7) | (b & 0x7F);
	} while (b & 0x80);
	return v;
}

static void sanitise_name(char* dst, size_t dstsz, const char* src)
{
	size_t di = 0;
	for (const char* s = src; *s && di < dstsz - 1; s++)
	{
		unsigned char c = (unsigned char)*s;
		if (di == 0 && isdigit(c))
		{
			dst[di++] = '_';
			dst[di++] = c;
		}
		else if (isalnum(c) || c == '_')
		{
			dst[di++] = (char)c;
		}
		else if (c == ' ' || c == '-')
		{
			dst[di++] = '_';
		}
	}
	if (di == 0)
	{
		dst[di++] = 't';
	}
	dst[di] = '\0';
}

static uint8_t* load_file(const char* path, size_t* outLen)
{
	FILE* f = fopen(path, "rb");
	if (!f)
	{
		fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (len < 0 || (size_t)len > SPEL_MAX_FILE_SIZE)
	{
		fprintf(stderr, "error: '%s' too large or unreadable\n", path);
		fclose(f);
		return NULL;
	}

	uint8_t* buf = (uint8_t*)malloc((size_t)len);
	if (!buf)
	{
		fprintf(stderr, "error: out of memory reading '%s'\n", path);
		fclose(f);
		return NULL;
	}

	size_t nread = fread(buf, 1, (size_t)len, f);
	fclose(f);

	if ((long)nread != len)
	{
		fprintf(stderr, "error: short read on '%s'\n", path);
		free(buf);
		return NULL;
	}

	*outLen = (size_t)len;
	return buf;
}

typedef struct
{
	parsed_track_t tracks[SPEL_MAX_TRACKS];
	int num_tracks;
	float bpm;
	int tpqn;
	int midi_format;
} convert_result_t;

static bool add_event(parsed_track_t* t, float beat, float duration, uint8_t note,
					  float velocity)
{
	if (t->num_events >= SPEL_MAX_EVENTS)
	{
		return false;
	}

	sheet_event_t* e = &t->events[t->num_events++];
	e->beat = beat;
	e->duration = duration;
	e->midi_note = note;
	e->velocity = velocity;
	return true;
}

static int pending_alloc_slot(pending_note_t* pending, int* npending)
{
	for (int i = 0; i < *npending; i++)
	{
		if (!pending[i].active)
		{
			return i;
		}
	}
	if (*npending < SPEL_MAX_PENDING)
	{
		return (*npending)++;
	}
	return -1;
}

static int convert_midi(const uint8_t* data, size_t len, convert_result_t* out)
{
	memset(out, 0, sizeof(*out));
	out->bpm = 120.0F;
	int rc = 0;

	const uint8_t* p = data;
	const uint8_t* end = data + len;

	if (end - p < 14)
	{
		fprintf(stderr, "error: file too short\n");
		return -1;
	}
	if (memcmp(p, "MThd", 4) != 0)
	{
		fprintf(stderr, "error: not a MIDI file\n");
		return -1;
	}
	p += 4;
	uint32_t hdr_len = read_u32be(&p);
	(void)hdr_len;
	out->midi_format = read_u16be(&p);
	int ntracks = (int)read_u16be(&p);
	uint16_t division = read_u16be(&p);

	if (out->midi_format < 0 || out->midi_format > 2)
	{
		fprintf(stderr, "error: unsupported MIDI format %d\n", out->midi_format);
		return -1;
	}
	if (ntracks > SPEL_MAX_TRACKS)
	{
		ntracks = SPEL_MAX_TRACKS;
	}

	int tpqn;
	if (division & 0x8000)
	{

		int frames_per_sec = -(int8_t)(division >> 8);
		int ticks_per_frame = division & 0xFF;
		tpqn = frames_per_sec * ticks_per_frame * 4;
	}
	else
	{
		tpqn = division;
	}
	out->tpqn = tpqn;
	if (tpqn <= 0)
	{
		tpqn = 480;
	}

	int track_idx = 0;
	int track_count = 0;

	for (int ti = 0; ti < ntracks && track_idx < SPEL_MAX_TRACKS; ti++)
	{
		if (end - p < 8)
		{
			break;
		}
		if (memcmp(p, "MTrk", 4) != 0)
		{
			fprintf(stderr, "warning: unexpected chunk at track %d\n", ti);
			break;
		}
		p += 4;
		uint32_t trk_len = read_u32be(&p);
		const uint8_t* trk_end = p + trk_len;
		if (trk_end > end)
		{
			trk_end = end;
		}

		parsed_track_t* trk = &out->tracks[track_idx];
		trk->wave = WAVE_SQUARE;
		trk->channel = -1;
		trk->program = -1;
		trk->num_events = 0;
		trk->max_voices = 8;
		snprintf(trk->orig_name, SPEL_MAX_NAME, "Track_%d", ti);
		snprintf(trk->name, SPEL_MAX_NAME, "track_%d", ti);

		pending_note_t pending[SPEL_MAX_PENDING];
		memset(pending, 0, sizeof(pending));
		int npending = 0;

		int program = -1;
		float beat = 0.0F;
		uint8_t running_status = 0;

		while (p < trk_end)
		{
			uint32_t delta = read_vlq(&p);
			beat += (float)delta / (float)tpqn;

			if (p >= trk_end)
			{
				break;
			}

			uint8_t status;
			if (*p & 0x80)
			{
				status = *p++;
			}
			else if (running_status != 0 && (running_status & 0xF0) != 0xF0)
			{
				status = running_status;
			}
			else
			{
				break;
			}

			if (status == 0xF0 || status == 0xF7)
			{
				uint32_t slen = read_vlq(&p);
				p += slen;
				continue;
			}

			if (status == 0xFF)
			{
				if (p >= trk_end)
				{
					break;
				}
				uint8_t meta_type = *p++;
				uint32_t meta_len = read_vlq(&p);
				const uint8_t* meta_data = p;
				p += meta_len;
				if (p > trk_end)
				{
					p = trk_end;
				}

				switch (meta_type)
				{
				case 0x03:
				{
					size_t cpylen =
						meta_len < SPEL_MAX_NAME - 1 ? meta_len : SPEL_MAX_NAME - 1;
					memcpy(trk->orig_name, meta_data, cpylen);
					trk->orig_name[cpylen] = '\0';
					sanitise_name(trk->name, SPEL_MAX_NAME, trk->orig_name);
					break;
				}
				case 0x51:
					if (meta_len >= 3)
					{
						uint32_t us_per_q = ((uint32_t)meta_data[0] << 16) |
											((uint32_t)meta_data[1] << 8) |
											(uint32_t)meta_data[2];
						if (us_per_q > 0)
						{
							float bpm = 60000000.0F / (float)us_per_q;
							if (track_count == 0)
							{
								out->bpm = bpm;
							}
						}
					}
					break;
				case 0x2F:
					goto end_track;
				default:
					break;
				}
				continue;
			}

			if (status >= 0xF8)
			{
				continue;
			}

			trk->channel = (status & 0x0F);
			if (trk->channel == 9)
			{
				trk->wave = WAVE_NOISE;
			}
			running_status = status;

			switch (status & 0xF0)
			{
			case 0x80:
			{
				if (p + 2 > trk_end)
				{
					goto end_track;
				}
				uint8_t note = *p++;
				uint8_t vel = *p++;
				(void)vel;

				for (int pi = 0; pi < npending; pi++)
				{
					if (pending[pi].active && pending[pi].note == note)
					{
						float dur = beat - pending[pi].start_beat;
						if (dur < 0.001F)
						{
							dur = 0.001F;
						}
						if (!add_event(trk, pending[pi].start_beat, dur, note,
									   pending[pi].velocity))
						{
							fprintf(stderr, "warning: too many events in track %d\n", ti);
						}
						pending[pi].active = false;
						break;
					}
				}
				break;
			}

			case 0x90:
			{
				if (p + 2 > trk_end)
				{
					goto end_track;
				}
				uint8_t note = *p++;
				uint8_t vel = *p++;

				if (vel > 0)
				{

					int slot = pending_alloc_slot(pending, &npending);
					if (slot >= 0)
					{
						pending[slot].active = true;
						pending[slot].note = note;
						pending[slot].start_beat = beat;
						pending[slot].velocity = (float)vel / 127.0F;
						if (pending[slot].velocity < 0.05F)
						{
							pending[slot].velocity = 0.05F;
						}
					}
					else
					{
						fprintf(stderr,
								"warning: more than %d notes held "
								"simultaneously in track %d\n",
								SPEL_MAX_PENDING, ti);
					}
				}
				else
				{

					for (int pi = 0; pi < npending; pi++)
					{
						if (pending[pi].active && pending[pi].note == note)
						{
							float dur = beat - pending[pi].start_beat;
							if (dur < 0.001F)
							{
								dur = 0.001F;
							}
							if (!add_event(trk, pending[pi].start_beat, dur, note,
										   pending[pi].velocity))
							{
								fprintf(stderr, "warning: too many events in track %d\n",
										ti);
							}
							pending[pi].active = false;
							break;
						}
					}
				}
				break;
			}

			case 0xA0:
			case 0xB0:
			case 0xE0:
				p += 2;
				break;

			case 0xC0:
				if (p + 1 > trk_end)
				{
					goto end_track;
				}
				program = *p++;
				trk->program = program;
				if (trk->channel != 9)
				{
					trk->wave = program_to_wave(program);
				}
				break;

			case 0xD0:
				p += 1;
				break;

			default:
				break;
			}
		}

	end_track:

		for (int pi = 0; pi < npending; pi++)
		{
			if (pending[pi].active)
			{
				float dur = 4.0F;
				if (!add_event(trk, pending[pi].start_beat, dur, pending[pi].note,
							   pending[pi].velocity))
				{
					fprintf(stderr, "warning: too many events in track %d\n", ti);
				}
			}
		}

		qsort(trk->events, trk->num_events, sizeof(sheet_event_t), event_cmp);

		if (trk->num_events > 0)
		{

			int max_simul = 0;
			{
				for (int ei = 0; ei < trk->num_events; ei++)
				{
					float b = trk->events[ei].beat;
					float e = b + trk->events[ei].duration;
					int c = 0;
					for (int ej = 0; ej < trk->num_events; ej++)
					{
						float bj = trk->events[ej].beat;
						float ej_end = bj + trk->events[ej].duration;
						if (bj < e && ej_end > b)
						{
							c++;
						}
					}
					if (c > max_simul)
					{
						max_simul = c;
					}
				}
			}
			trk->max_voices = max_simul + 4;
			if (trk->max_voices > 64)
			{
				trk->max_voices = 64;
			}
			track_idx++;
		}

		p = trk_end;
		track_count++;
	}

	out->num_tracks = track_idx;
	return rc;
}

static FILE* g_out;

static void emit_header(const char* inputFile, const char* prefix,
						const convert_result_t* res)
{
	fprintf(g_out,
			"/*\n"
			" * input: \"%s\"\n"
			" * midi format %d, %d note tracks, tpqn %d, bpm %.1f\n"
			" *\n"
			" * include audio/audio_synth.h\n"
			" */\n\n",
			inputFile, res->midi_format, res->num_tracks, res->tpqn, res->bpm);
}

static void make_prefix(const char* filename, char* out, size_t outsz)
{
	const char* base = filename;
	const char* slash = strrchr(filename, '/');
	if (slash)
	{
		base = slash + 1;
	}
#ifdef _WIN32
	const char* bslash = strrchr(base, '\\');
	if (bslash)
		base = bslash + 1;
#endif

	char buf[SPEL_MAX_NAME];
	snprintf(buf, sizeof(buf), "%s", base);
	char* dot = strrchr(buf, '.');
	if (dot)
	{
		*dot = '\0';
	}

	sanitise_name(out, outsz, buf);
	if (strlen(out) == 0 || isdigit((unsigned char)out[0]))
	{
		char tmp[SPEL_MAX_NAME];
		snprintf(tmp, sizeof(tmp), "midi_%s", out);
		snprintf(out, outsz, "%s", tmp);
	}
}

static void emit_track_data(int idx, const parsed_track_t* trk, const char* pfx,
							float bpm)
{

	fprintf(g_out, "#define %s_EVENTS_%d %d\n", pfx, idx, trk->num_events);

	fprintf(g_out,
			"static const spel_audio_synth_event %s_events_%d[%s_EVENTS_%d] =\n"
			"{\n",
			pfx, idx, pfx, idx);

	for (int ei = 0; ei < trk->num_events; ei++)
	{
		const sheet_event_t* e = &trk->events[ei];

		fprintf(g_out, "	{ %.3ff, %.3ff, %3d, %.2ff }", e->beat, e->duration,
				e->midi_note, e->velocity);
		if (ei < trk->num_events - 1)
		{
			fputc(',', g_out);
		}
		fputc('\n', g_out);
	}
	fprintf(g_out, "};\n\n");

	fprintf(g_out,
			"static const spel_audio_synth_sheet %s_sheet_%d =\n"
			"{\n"
			"	.bpm        = %.1ff,\n"
			"	.num_events = %s_EVENTS_%d,\n"
			"	.events     = %s_events_%d,\n"
			"};\n\n",
			pfx, idx, bpm, pfx, idx, pfx, idx);
}

static void emit_static_only(const char* filename, const convert_result_t* res)
{
	char pfx[SPEL_MAX_NAME];
	make_prefix(filename, pfx, sizeof(pfx));
	emit_header(filename, pfx, res);

	fprintf(g_out, "#include \"audio/audio_synth.h\"\n\n");

	for (int i = 0; i < res->num_tracks; i++)
	{
		const parsed_track_t* trk = &res->tracks[i];
		fprintf(g_out, "/* track %d: \"%s\"  —  %s  (%d events, ~%d voices) */\n", i,
				trk->orig_name, wave_name[trk->wave], trk->num_events, trk->max_voices);
		emit_track_data(i, trk, "mid", res->bpm);
	}
}

static void emit_with_runtime(const char* filename, const convert_result_t* res)
{
	char pfx[SPEL_MAX_NAME];
	make_prefix(filename, pfx, sizeof(pfx));

	emit_header(filename, pfx, res);

	fprintf(g_out,
			"/*\n"
			" * Usage:\n"
			" *   Call %s_setup() once during load.\n"
			" *   Call %s_update() every frame.\n"
			" */\n\n",
			pfx, pfx);

	fprintf(g_out, "#include \"audio/audio_synth.h\"\n");
	fprintf(g_out, "#include \"audio/audio_mixer.h\"\n\n");

	for (int i = 0; i < res->num_tracks; i++)
	{
		const parsed_track_t* trk = &res->tracks[i];
		fprintf(g_out, "/* Track %d: \"%s\"  —  %s  (%d events, ~%d voices) */\n", i,
				trk->orig_name, wave_name[trk->wave], trk->num_events, trk->max_voices);
		emit_track_data(i, trk, pfx, res->bpm);
	}

	fprintf(g_out,
			"static spel_audio_synth_player %s_players[%d];\n"
			"static uint32_t %s_num_players = 0;\n\n",
			pfx, res->num_tracks, pfx);

	fprintf(g_out,
			"void %s_setup(void)\n"
			"{\n",
			pfx);

	for (int i = 0; i < res->num_tracks; i++)
	{
		const parsed_track_t* trk = &res->tracks[i];
		adsr_t env = program_to_envelope(trk->program, trk->channel);

		float base_gain = program_to_gain(trk->program, trk->channel);
		float track_vol = base_gain;
		fprintf(
			g_out,
			"	/* Track %d: \"%s\"  →  %s  (%d voices) */\n"
			"	{\n"
			"		spel_audio_synth s = spel_audio_synth_create(%s, %d);\n"
			"		spel_audio_synth_envelope_set(s, &(spel_audio_synth_envelope){\n"
			"			.attack = %.4ff, .decay = %.4ff,\n"
			"			.sustain = %.4ff, .release = %.4ff\n"
			"		});\n"
			"		spel_audio_voice_volume_set(spel_audio_synth_voice_get(s), %.4ff);\n"
			"		%s_players[%d] = spel_audio_synth_player_create(s, &%s_sheet_%d);\n"
			"		spel_audio_synth_player_play(%s_players[%d]);\n"
			"		%s_num_players++;\n"
			"	}\n\n",
			i, trk->orig_name, wave_name[trk->wave], trk->max_voices,
			wave_name[trk->wave], trk->max_voices, env.attack, env.decay, env.sustain,
			env.release, track_vol, pfx, i, pfx, i, pfx, i, pfx);
	}

	fprintf(g_out, "	spel_audio_master_limiter_set(-0.5f, 1.0f, 5.0f);\n");

	fprintf(g_out, "}\n\n");

	fprintf(g_out,
			"void %s_update(void)\n"
			"{\n"
			"\tfor (uint32_t i = 0; i < %s_num_players; i++)\n"
			"\t\tspel_audio_synth_player_update(%s_players[i]);\n"
			"}\n\n",
			pfx, pfx, pfx);
}

static void usage(void)
{
	fprintf(stderr, "Usage: spel-mid2sht [options] <input.mid>\n"
					"Options:\n"
					"  -s         Static declarations only (no setup/update functions)\n"
					"  -o <file>  Write to file instead of stdout\n"
					"Reads a Standard MIDI File and emits C source on stdout (or -o)\n"
					"declaring spel_audio_synth_sheet arrays for each instrument track,\n"
					"with MIDI programs mapped to chiptune waveforms.\n");
}

int main(int argc, char** argv)
{
	const char* input_file = NULL;
	const char* output_file = NULL;
	bool static_only = false;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-s") == 0)
		{
			{
				static_only = true;
			}
		}
		else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
		{
			{
				output_file = argv[++i];
			}
		}
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
		{
			usage();
			return 0;
		}
		else if (argv[i][0] == '-')
		{
			fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
			usage();
			return 1;
		}
		else
		{
			{
				input_file = argv[i];
			}
		}
	}

	if (!input_file)
	{
		usage();
		return 1;
	}

	if (output_file)
	{
		g_out = fopen(output_file, "w");
		if (!g_out)
		{
			fprintf(stderr, "error: cannot write '%s': %s\n", output_file,
					strerror(errno));
			return 1;
		}
	}
	else
	{
		{
			g_out = stdout;
		}
	}

	size_t file_len;
	uint8_t* data = load_file(input_file, &file_len);
	if (!data)
	{
		if (g_out != stdout)
		{
			fclose(g_out);
		}
		return 1;
	}

	convert_result_t result;
	if (convert_midi(data, file_len, &result) < 0)
	{
		free(data);
		if (g_out != stdout)
		{
			fclose(g_out);
		}
		return 1;
	}
	free(data);

	if (result.num_tracks == 0)
	{
		fprintf(stderr, "warning: no note events found in '%s'\n", input_file);
	}

	if (static_only)
	{
		emit_static_only(input_file, &result);
	}
	else
	{
		emit_with_runtime(input_file, &result);
	}

	if (g_out != stdout)
	{
		fclose(g_out);
		fprintf(stderr, "wrote %s\n", output_file);
	}

	return 0;
}