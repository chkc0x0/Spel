/*
 * spel-mid2sht — a MIDI-to-Spel-sheet converter
 *
 * Reads a Standard MIDI File (SMF) and emits C source declaring
 * `spel_audio_synth_event` arrays and `spel_audio_synth_sheet` structs,
 * one per instrument-bearing track.  MIDI program numbers are mapped to
 * the nearest chiptune waveform (square / saw / triangle / pulse / noise)
 * for a deliberate NES / chiptune aesthetic.
 *
 * Usage:
 *   spel-mid2sht input.mid        →  C source on stdout
 *   spel-mid2sht -s input.mid     →  data declarations only, no runtime funcs
 *   spel-mid2sht -o out.c song.mid  →  write to file
 *
 * The generated setup() creates synths + players and starts playback.
 * The generated update() calls player_update on every player each frame.
 *
 * No external dependencies — plain C, standard library only.
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Limits ────────────────────────────────────────────────────────── */

#define MAX_TRACKS       64
#define MAX_EVENTS     4096
#define MAX_NAME         64
#define MAX_PENDING     256
#define MAX_FILE_SIZE   (16u * 1024u * 1024u)
#define MAX_NOTES      4096          /* total notes across all tracks */

/* ── MIDI program → chiptune waveform mapping ─────────────────────── */

typedef enum
{
	WAVE_SINE     = 0,
	WAVE_SQUARE   = 1,
	WAVE_SAW      = 2,
	WAVE_TRIANGLE = 3,
	WAVE_NOISE    = 4,
	WAVE_PULSE    = 5,
} wave_t;

static const char* wave_name[] =
{
	"SPEL_AUDIO_SYNTH_WAVE_SINE",
	"SPEL_AUDIO_SYNTH_WAVE_SQUARE",
	"SPEL_AUDIO_SYNTH_WAVE_SAW",
	"SPEL_AUDIO_SYNTH_WAVE_TRIANGLE",
	"SPEL_AUDIO_SYNTH_WAVE_NOISE",
	"SPEL_AUDIO_SYNTH_WAVE_PULSE",
};

/*
 * General MIDI instrument groups mapped to chiptune approximations.
 *
 *   Piano-family        → Square   (beepy, percussive)
 *   Chromatic percussion → Triangle (bell-like, metallic)
 *   Organ               → Pulse    (narrow-duty buzz)
 *   Guitar              → Saw      (buzzy, string-like)
 *   Bass                → Saw      (growly low end)
 *   Strings             → Triangle (warm, smooth)
 *   Ensemble            → Saw      (rich buzzy pad)
 *   Brass               → Square   (brassy beep)
 *   Reed                → Pulse    (nasal)
 *   Pipe                → Square   (thin, breathy)
 *   Synth Lead          → Saw      (classic lead)
 *   Synth Pad           → Square   (warm pad)
 *   Synth FX            → Noise
 *   Ethnic              → Triangle (flute-like)
 *   Percussive          → Noise + short square
 *   Sound FX            → Noise
 */
static wave_t program_to_wave(int prog)
{
	if (prog < 0) return WAVE_SQUARE;
	if (prog <= 7)   return WAVE_SQUARE;    /* Piano */
	if (prog <= 15)  return WAVE_TRIANGLE;  /* Chromatic perc */
	if (prog <= 23)  return WAVE_PULSE;     /* Organ */
	if (prog <= 31)  return WAVE_SAW;       /* Guitar */
	if (prog <= 39)  return WAVE_SAW;       /* Bass */
	if (prog <= 47)  return WAVE_TRIANGLE;  /* Strings */
	if (prog <= 55)  return WAVE_SAW;       /* Ensemble */
	if (prog <= 63)  return WAVE_SQUARE;    /* Brass */
	if (prog <= 71)  return WAVE_PULSE;     /* Reed */
	if (prog <= 79)  return WAVE_SQUARE;    /* Pipe */
	if (prog <= 87)  return WAVE_SAW;       /* Synth lead */
	if (prog <= 95)  return WAVE_SQUARE;    /* Synth pad */
	if (prog <= 103) return WAVE_NOISE;     /* Synth FX */
	if (prog <= 111) return WAVE_TRIANGLE;  /* Ethnic */
	if (prog <= 119) return WAVE_NOISE;     /* Percussive */
	return WAVE_NOISE;                       /* Sound FX */
}

/* ── Collected sheet event (intermediate representation) ──────────── */

typedef struct
{
	float   beat;
	float   duration;
	uint8_t midi_note;
	float   velocity;
} sheet_event_t;

/* ── Data per parsed track / instrument ───────────────────────────── */

typedef struct
{
	char     name[MAX_NAME];       /* sanitised C identifier */
	char     orig_name[MAX_NAME];  /* original track name (for comment) */
	wave_t   wave;
	int      num_events;
	int      max_voices;
	sheet_event_t events[MAX_EVENTS];
} parsed_track_t;

/* ── Pending-note tracker (note-on/note-off pairing) ──────────────── */

typedef struct
{
	uint8_t  note;
	float    start_beat;
	float    velocity;
	bool     active;
} pending_note_t;

/* ── Big-endian helpers ───────────────────────────────────────────── */

static uint32_t read_u32be(const uint8_t** p)
{
	uint32_t v = ((uint32_t)(*p)[0] << 24) | ((uint32_t)(*p)[1] << 16)
	           | ((uint32_t)(*p)[2] <<  8) |  (uint32_t)(*p)[3];
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
	uint8_t  b;
	do
	{
		if (**p == 0 && v == 0) { (*p)++; return 0; } /* common fast path */
		b = *(*p)++;
		v = (v << 7) | (b & 0x7F);
	} while (b & 0x80);
	return v;
}

/* ── Track name sanitisation ──────────────────────────────────────── */

/* Produce a valid C identifier from the raw UTF-8 name. */
static void sanitise_name(char* dst, size_t dstsz, const char* src)
{
	size_t di = 0;
	for (const char* s = src; *s && di < dstsz - 1; s++)
	{
		unsigned char c = (unsigned char)*s;
		if (di == 0 && isdigit(c))
		{
			dst[di++] = '_'; /* prefix digit with underscore */
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
		/* else skip non-identifier chars */
	}
	if (di == 0)
		dst[di++] = 't';
	dst[di] = '\0';
}

/* ── MIDI file loading ────────────────────────────────────────────── */

static uint8_t* load_file(const char* path, size_t* out_len)
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

	if (len < 0 || (size_t)len > MAX_FILE_SIZE)
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

	*out_len = (size_t)len;
	return buf;
}

/* ── Main conversion ──────────────────────────────────────────────── */

typedef struct
{
	parsed_track_t tracks[MAX_TRACKS];
	int            num_tracks;
	float          bpm;
	int            tpqn;
	int            midi_format;
} convert_result_t;

/*
 * Add a sheet event to a parsed track.  Returns false on overflow.
 */
static bool add_event(parsed_track_t* t, float beat, float duration,
					  uint8_t note, float velocity)
{
	if (t->num_events >= MAX_EVENTS)
		return false;

	sheet_event_t* e = &t->events[t->num_events++];
	e->beat      = beat;
	e->duration  = duration;
	e->midi_note = note;
	e->velocity  = velocity;
	return true;
}

static int convert_midi(const uint8_t* data, size_t len,
						convert_result_t* out)
{
	memset(out, 0, sizeof(*out));
	out->bpm = 120.0f; /* default */
	int rc = 0;

	const uint8_t* p = data;
	const uint8_t* end = data + len;

	/* ── Header chunk ──────────────────────────────────────────── */
	if (end - p < 14) { fprintf(stderr, "error: file too short\n"); return -1; }
	if (memcmp(p, "MThd", 4) != 0) { fprintf(stderr, "error: not a MIDI file\n"); return -1; }
	p += 4;
	uint32_t hdr_len = read_u32be(&p);          /* should be 6 */
	(void)hdr_len;
	out->midi_format = read_u16be(&p);
	int     ntracks   = (int)read_u16be(&p);
	uint16_t division = read_u16be(&p);

	if (out->midi_format < 0 || out->midi_format > 2)
	{
		fprintf(stderr, "error: unsupported MIDI format %d\n", out->midi_format);
		return -1;
	}
	if (ntracks > MAX_TRACKS)
		ntracks = MAX_TRACKS;

	/* Division: bit 15 = 0 → ticks per quarter note */
	int tpqn;
	if (division & 0x8000)
	{
		/* SMPTE: -frames/sec + ticks/frame — convert to ~TPQN equiv */
		int frames_per_sec  = -(int8_t)(division >> 8);
		int ticks_per_frame = division & 0xFF;
		tpqn = frames_per_sec * ticks_per_frame * 4; /* approx quarter = 4 frames */
	}
	else
	{
		tpqn = division;
	}
	out->tpqn = tpqn;
	if (tpqn <= 0) tpqn = 480; /* safety */

	/* ── Track chunks ──────────────────────────────────────────── */
	int    track_idx = 0;
	int    track_count = 0;

	for (int ti = 0; ti < ntracks && track_idx < MAX_TRACKS; ti++)
	{
		if (end - p < 8) break;
		if (memcmp(p, "MTrk", 4) != 0)
		{
			fprintf(stderr, "warning: unexpected chunk at track %d\n", ti);
			break;
		}
		p += 4;
		uint32_t trk_len = read_u32be(&p);
		const uint8_t* trk_end = p + trk_len;
		if (trk_end > end) trk_end = end;

		/* Per-track state */
		parsed_track_t* trk = &out->tracks[track_idx];
		trk->wave = WAVE_SQUARE;
		trk->num_events = 0;
		trk->max_voices = 8;
		snprintf(trk->orig_name, MAX_NAME, "Track_%d", ti);
		snprintf(trk->name, MAX_NAME, "track_%d", ti);

		/* For note-off / note-on pairing */
		pending_note_t pending[MAX_PENDING];
		memset(pending, 0, sizeof(pending));
		int npending = 0;

		int  program    = -1;
		float beat      = 0.0f;
		uint8_t running_status = 0;

		while (p < trk_end)
		{
			uint32_t delta = read_vlq(&p);
			beat += (float)delta / (float)tpqn;

			if (p >= trk_end) break;

			uint8_t status;
			if (*p & 0x80)
			{
				status = *p++;
			}
			else if (running_status != 0 &&
					 (running_status & 0xF0) != 0xF0)
			{
				status = running_status;
				/* p already points at first data byte */
			}
			else
			{
				break; /* malformed */
			}

			/* ── System exclusive ───────────────────────────── */
			if (status == 0xF0 || status == 0xF7)
			{
				uint32_t slen = read_vlq(&p);
				p += slen;
				continue;
			}

			/* ── Meta event ─────────────────────────────────── */
			if (status == 0xFF)
			{
				if (p >= trk_end) break;
				uint8_t  meta_type = *p++;
				uint32_t meta_len  = read_vlq(&p);
				const uint8_t* meta_data = p;
				p += meta_len;
				if (p > trk_end) p = trk_end;

				switch (meta_type)
				{
				case 0x03: /* Track name */
				{
					size_t cpylen = meta_len < MAX_NAME - 1 ? meta_len : MAX_NAME - 1;
					memcpy(trk->orig_name, meta_data, cpylen);
					trk->orig_name[cpylen] = '\0';
					sanitise_name(trk->name, MAX_NAME, trk->orig_name);
					break;
				}
				case 0x51: /* Set tempo (μs/quarter) */
					if (meta_len >= 3)
					{
						uint32_t us_per_q = ((uint32_t)meta_data[0] << 16)
										  | ((uint32_t)meta_data[1] << 8)
										  |  (uint32_t)meta_data[2];
						if (us_per_q > 0)
						{
							float bpm = 60000000.0f / (float)us_per_q;
							if (track_count == 0)  /* use first tempo for sheet BPM */
								out->bpm = bpm;
						}
					}
					break;
				case 0x2F: /* End of track */
					goto end_track;
				default:
					break;
				}
				continue;
			}

			/* ── System real-time (should not appear in files) ─ */
			if (status >= 0xF8)
				continue;

			/* ── Channel voice message ───────────────────────── */
			(void)(status & 0x0F); /* channel nibble — not currently used */
			running_status = status; /* only channel messages set running status */

			switch (status & 0xF0)
			{
			case 0x80: /* Note Off */
			{
				if (p + 2 > trk_end) goto end_track;
				uint8_t note = *p++;
				uint8_t vel  = *p++;
				(void)vel;

				/* Find matching pending note (FIFO per pitch) */
				for (int pi = 0; pi < npending; pi++)
				{
					if (pending[pi].active && pending[pi].note == note)
					{
						float dur = beat - pending[pi].start_beat;
						if (dur < 0.001f) dur = 0.001f;
						if (!add_event(trk, pending[pi].start_beat,
									   dur, note, pending[pi].velocity))
						{
							fprintf(stderr, "warning: too many events in track %d\n", ti);
						}
						pending[pi].active = false;
						break;
					}
				}
				break;
			}

			case 0x90: /* Note On */
			{
				if (p + 2 > trk_end) goto end_track;
				uint8_t note = *p++;
				uint8_t vel  = *p++;

				if (vel > 0)
				{
					/* Note on with velocity > 0 */
					if (npending < MAX_PENDING)
					{
						pending[npending].active     = true;
						pending[npending].note       = note;
						pending[npending].start_beat = beat;
						pending[npending].velocity   = (float)vel / 127.0f;
						if (pending[npending].velocity < 0.05f)
							pending[npending].velocity = 0.05f;
						npending++;
					}
				}
				else
				{
					/* Velocity 0 = note off */
					for (int pi = 0; pi < npending; pi++)
					{
						if (pending[pi].active && pending[pi].note == note)
						{
							float dur = beat - pending[pi].start_beat;
							if (dur < 0.001f) dur = 0.001f;
							if (!add_event(trk, pending[pi].start_beat,
										   dur, note, pending[pi].velocity))
							{
								fprintf(stderr, "warning: too many events in track %d\n", ti);
							}
							pending[pi].active = false;
							break;
						}
					}
				}
				break;
			}

			case 0xA0: /* Polyphonic Key Pressure */
			case 0xB0: /* Control Change */
			case 0xE0: /* Pitch Bend */
				p += 2;
				break;

			case 0xC0: /* Program Change */
				if (p + 1 > trk_end) goto end_track;
				program = *p++;
				trk->wave = program_to_wave(program);
				break;

			case 0xD0: /* Channel Pressure */
				p += 1;
				break;

			default:
				break;
			}
		}

end_track:
		/* Flush any pending notes that never got a note-off */
		for (int pi = 0; pi < npending; pi++)
		{
			if (pending[pi].active)
			{
				float dur = 4.0f; /* release after 4 beats */
				if (!add_event(trk, pending[pi].start_beat,
							   dur, pending[pi].note, pending[pi].velocity))
				{
					fprintf(stderr, "warning: too many events in track %d\n", ti);
				}
			}
		}

		/* Only keep tracks that actually have events */
		if (trk->num_events > 0)
		{
			/* Estimate voice count: scan for max simultaneous notes */
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
							c++;
					}
					if (c > max_simul) max_simul = c;
				}
			}
			trk->max_voices = max_simul + 4; /* headroom */
			if (trk->max_voices > 64) trk->max_voices = 64;
			track_idx++;
		}

		p = trk_end;
		track_count++;
	}

	out->num_tracks = track_idx;
	return rc;
}

/* ── C code emission ──────────────────────────────────────────────── */

static FILE* g_out;

static void emit_header(const char* input_file, const char* prefix,
						const convert_result_t* res)
{
	fprintf(g_out,
		"/*\n"
		" * Generated by spel-mid2sht from \"%s\"\n"
		" * MIDI format %d, %d note tracks, TPQN %d, BPM %.1f\n"
		" *\n"
		" * Include audio/audio_synth.h before this file.\n"
		" */\n\n",
		input_file, res->midi_format, res->num_tracks,
		res->tpqn, res->bpm);
}

/* Build a safe C identifier prefix from the filename. */
static void make_prefix(const char* filename, char* out, size_t outsz)
{
	const char* base = filename;
	const char* slash = strrchr(filename, '/');
	if (slash) base = slash + 1;
#ifdef _WIN32
	const char* bslash = strrchr(base, '\\');
	if (bslash) base = bslash + 1;
#endif

	/* Strip extension */
	char buf[MAX_NAME];
	snprintf(buf, sizeof(buf), "%s", base);
	char* dot = strrchr(buf, '.');
	if (dot) *dot = '\0';

	/* Entire buf may be empty or all-invalid */
	sanitise_name(out, outsz, buf);
	if (strlen(out) == 0 || isdigit((unsigned char)out[0]))
	{
		char tmp[MAX_NAME];
		snprintf(tmp, sizeof(tmp), "midi_%s", out);
		snprintf(out, outsz, "%s", tmp);
	}
}

static void emit_track_data(int idx, const parsed_track_t* trk,
							const char* pfx, float bpm)
{
	/* Event count constant */
	fprintf(g_out,
		"#define %s_EVENTS_%d %d\n",
		pfx, idx, trk->num_events);

	/* Event array */
	fprintf(g_out,
		"static const spel_audio_synth_event %s_events_%d[%s_EVENTS_%d] =\n"
		"{\n",
		pfx, idx, pfx, idx);

	for (int ei = 0; ei < trk->num_events; ei++)
	{
		const sheet_event_t* e = &trk->events[ei];
		/* Compact: group indent, all on one line */
		fprintf(g_out, "	{ %.3ff, %.3ff, %3d, %.2ff }",
				e->beat, e->duration, e->midi_note, e->velocity);
		if (ei < trk->num_events - 1)
			fputc(',', g_out);
		fputc('\n', g_out);
	}
	fprintf(g_out, "};\n\n");

	/* Sheet struct */
	fprintf(g_out,
		"static const spel_audio_synth_sheet %s_sheet_%d =\n"
		"{\n"
		"	.bpm        = %.1ff,\n"
		"	.num_events = %s_EVENTS_%d,\n"
		"	.events     = %s_events_%d,\n"
		"};\n\n",
		pfx, idx,
		bpm,
		pfx, idx,
		pfx, idx);
}

static void emit_static_only(const char* filename, const convert_result_t* res)
{
	char pfx[MAX_NAME];
	make_prefix(filename, pfx, sizeof(pfx));
	emit_header(filename, pfx, res);

	fprintf(g_out, "#include \"audio/audio_synth.h\"\n\n");

	for (int i = 0; i < res->num_tracks; i++)
	{
		const parsed_track_t* trk = &res->tracks[i];
		fprintf(g_out,
			"/* Track %d: \"%s\"  —  %s  (%d events, ~%d voices) */\n",
			i, trk->orig_name, wave_name[trk->wave],
			trk->num_events, trk->max_voices);
		emit_track_data(i, trk, "mid", res->bpm);
	}
}

static void emit_with_runtime(const char* filename, const convert_result_t* res)
{
	char pfx[MAX_NAME];
	make_prefix(filename, pfx, sizeof(pfx));

	emit_header(filename, pfx, res);

	fprintf(g_out,
		"/*\n"
		" * Usage:\n"
		" *   Call %s_setup() once during load.\n"
		" *   Call %s_update() every frame.\n"
		" */\n\n",
		pfx, pfx);

	fprintf(g_out, "#include \"audio/audio_synth.h\"\n\n");

	/* ── Data declarations ────────────────────────────────── */
	for (int i = 0; i < res->num_tracks; i++)
	{
		const parsed_track_t* trk = &res->tracks[i];
		fprintf(g_out,
			"/* Track %d: \"%s\"  —  %s  (%d events, ~%d voices) */\n",
			i, trk->orig_name, wave_name[trk->wave],
			trk->num_events, trk->max_voices);
		emit_track_data(i, trk, pfx, res->bpm);
	}

	/* ── Player pointers ──────────────────────────────────── */
	fprintf(g_out,
		"static spel_audio_synth_player %s_players[%d];\n"
		"static uint32_t %s_num_players = 0;\n\n",
		pfx, res->num_tracks, pfx);

	/* ── Setup function ────────────────────────────────────── */
	fprintf(g_out,
		"void %s_setup(void)\n"
		"{\n",
		pfx);

	for (int i = 0; i < res->num_tracks; i++)
	{
		const parsed_track_t* trk = &res->tracks[i];
		fprintf(g_out,
			"\t/* Track %d: \"%s\"  →  %s  (%d voices) */\n"
			"\t{\n"
			"		spel_audio_synth s = spel_audio_synth_create(%s, %d);\n"
			"\t\tspel_audio_synth_envelope_default(s);\n"
			"\t\t%s_players[%d] = spel_audio_synth_player_create(s, &%s_sheet_%d);\n"
			"\t\tspel_audio_synth_player_play(%s_players[%d]);\n"
			"\t\t%s_num_players++;\n"
			"\t}\n\n",
			i, trk->orig_name, wave_name[trk->wave], trk->max_voices,
			wave_name[trk->wave], trk->max_voices,
			pfx, i, pfx, i,
			pfx, i,
			pfx);
	}

	fprintf(g_out, "}\n\n");

	/* ── Update function ───────────────────────────────────── */
	fprintf(g_out,
		"void %s_update(void)\n"
		"{\n"
		"\tfor (uint32_t i = 0; i < %s_num_players; i++)\n"
		"\t\tspel_audio_synth_player_update(%s_players[i]);\n"
		"}\n\n",
		pfx, pfx, pfx);
}

/* ── Entry point ───────────────────────────────────────────────────── */

static void usage(void)
{
	fprintf(stderr,
		"Usage: spel-mid2sht [options] <input.mid>\n"
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
			static_only = true;
		else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
			output_file = argv[++i];
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
			input_file = argv[i];
	}

	if (!input_file)
	{
		usage();
		return 1;
	}

	/* Open output */
	if (output_file)
	{
		g_out = fopen(output_file, "w");
		if (!g_out)
		{
			fprintf(stderr, "error: cannot write '%s': %s\n",
					output_file, strerror(errno));
			return 1;
		}
	}
	else
		g_out = stdout;

	/* Load & parse */
	size_t file_len;
	uint8_t* data = load_file(input_file, &file_len);
	if (!data)
	{
		if (g_out != stdout) fclose(g_out);
		return 1;
	}

	convert_result_t result;
	if (convert_midi(data, file_len, &result) < 0)
	{
		free(data);
		if (g_out != stdout) fclose(g_out);
		return 1;
	}
	free(data);

	if (result.num_tracks == 0)
	{
		fprintf(stderr, "warning: no note events found in '%s'\n", input_file);
	}

	/* Emit */
	if (static_only)
		emit_static_only(input_file, &result);
	else
		emit_with_runtime(input_file, &result);

	if (g_out != stdout)
	{
		fclose(g_out);
		fprintf(stderr, "wrote %s\n", output_file);
	}

	return 0;
}
