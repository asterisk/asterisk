/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2002, Pauline Middelink
 * Copyright (C) 2009, Digium, Inc.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 * \brief Indication Tone Handling
 *
 * \author Pauline Middelink <middelink@polyware.nl>
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <math.h>

#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/indications.h"
#include "asterisk/frame.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/data.h"

#include "asterisk/_private.h" /* _init(), _reload() */

#define DATA_EXPORT_TONE_ZONE(MEMBER)					\
	MEMBER(ast_tone_zone, country, AST_DATA_STRING)			\
	MEMBER(ast_tone_zone, description, AST_DATA_STRING)		\
	MEMBER(ast_tone_zone, nrringcadence, AST_DATA_UNSIGNED_INTEGER)

AST_DATA_STRUCTURE(ast_tone_zone, DATA_EXPORT_TONE_ZONE);

#define DATA_EXPORT_TONE_ZONE_SOUND(MEMBER)			\
	MEMBER(ast_tone_zone_sound, name, AST_DATA_STRING)	\
	MEMBER(ast_tone_zone_sound, data, AST_DATA_STRING)

AST_DATA_STRUCTURE(ast_tone_zone_sound, DATA_EXPORT_TONE_ZONE_SOUND);

/* Globals */
static const char config[] = "indications.conf";

static const int midi_tohz[128] = {
	8,     8,     9,     9,     10,    10,    11,    12,    12,    13,
	14,    15,    16,    17,    18,    19,    20,    21,    23,    24,
	25,    27,    29,    30,    32,    34,    36,    38,    41,    43,
	46,    48,    51,    55,    58,    61,    65,    69,    73,    77,
	82,    87,    92,    97,    103,   110,   116,   123,   130,   138,
	146,   155,   164,   174,   184,   195,   207,   220,   233,   246,
	261,   277,   293,   311,   329,   349,   369,   391,   415,   440,
	466,   493,   523,   554,   587,   622,   659,   698,   739,   783,
	830,   880,   932,   987,   1046,  1108,  1174,  1244,  1318,  1396,
	1479,  1567,  1661,  1760,  1864,  1975,  2093,  2217,  2349,  2489,
	2637,  2793,  2959,  3135,  3322,  3520,  3729,  3951,  4186,  4434,
	4698,  4978,  5274,  5587,  5919,  6271,  6644,  7040,  7458,  7902,
	8372,  8869,  9397,  9956,  10548, 11175, 11839, 12543
};

static struct ao2_container *ast_tone_zones;

#define NUM_TONE_ZONE_BUCKETS 53

/*!
 * \note Access to this is protected by locking the ast_tone_zones container
 */
static struct ast_tone_zone *default_tone_zone;

struct playtones_item {
	int fac1;
	int init_v2_1;
	int init_v3_1;
	int fac2;
	int init_v2_2;
	int init_v3_2;
	int modulate;
	int duration;
};

struct playtones_def {
	int vol;
	int reppos;
	int nitems;
	int interruptible;
	struct playtones_item *items;
};

struct playtones_state {
	int vol;
	int v1_1;
	int v2_1;
	int v3_1;
	int v1_2;
	int v2_2;
	int v3_2;
	int reppos;
	int nitems;
	struct playtones_item *items;
	int npos;
	int oldnpos;
	int pos;
	struct ast_format origwfmt;
	struct ast_frame f;
	unsigned char offset[AST_FRIENDLY_OFFSET];
	short data[4000];
};

static void playtones_release(struct ast_channel *chan, void *params)
{
	struct playtones_state *ps = params;

	if (chan) {
		ast_set_write_format(chan, &ps->origwfmt);
	}

	if (ps->items) {
		ast_free(ps->items);
		ps->items = NULL;
	}

	ast_free(ps);
}

static void *playtones_alloc(struct ast_channel *chan, void *params)
{
	struct playtones_def *pd = params;
	struct playtones_state *ps = NULL;

	if (!(ps = ast_calloc(1, sizeof(*ps)))) {
		return NULL;
	}

	ast_format_copy(&ps->origwfmt, ast_channel_writeformat(chan));

	if (ast_set_write_format_by_id(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set '%s' to signed linear format (write)\n", ast_channel_name(chan));
		playtones_release(NULL, ps);
		ps = NULL;
	} else {
		ps->vol = pd->vol;
		ps->reppos = pd->reppos;
		ps->nitems = pd->nitems;
		ps->items = pd->items;
		ps->oldnpos = -1;
	}

	/* Let interrupts interrupt :) */
	if (pd->interruptible) {
		ast_set_flag(ast_channel_flags(chan), AST_FLAG_WRITE_INT);
	} else {
		ast_clear_flag(ast_channel_flags(chan), AST_FLAG_WRITE_INT);
	}

	return ps;
}

static int playtones_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct playtones_state *ps = data;
	struct playtones_item *pi;
	int x;

	/* we need to prepare a frame with 16 * timelen samples as we're
	 * generating SLIN audio */

	len = samples * 2;
	if (len > sizeof(ps->data) / 2 - 1) {
		ast_log(LOG_WARNING, "Can't generate that much data!\n");
		return -1;
	}

	memset(&ps->f, 0, sizeof(ps->f));

	pi = &ps->items[ps->npos];

	if (ps->oldnpos != ps->npos) {
		/* Load new parameters */
		ps->v1_1 = 0;
		ps->v2_1 = pi->init_v2_1;
		ps->v3_1 = pi->init_v3_1;
		ps->v1_2 = 0;
		ps->v2_2 = pi->init_v2_2;
		ps->v3_2 = pi->init_v3_2;
		ps->oldnpos = ps->npos;
	}

	for (x = 0; x < samples; x++) {
		ps->v1_1 = ps->v2_1;
		ps->v2_1 = ps->v3_1;
		ps->v3_1 = (pi->fac1 * ps->v2_1 >> 15) - ps->v1_1;

		ps->v1_2 = ps->v2_2;
		ps->v2_2 = ps->v3_2;
		ps->v3_2 = (pi->fac2 * ps->v2_2 >> 15) - ps->v1_2;
		if (pi->modulate) {
			int p;
			p = ps->v3_2 - 32768;
			if (p < 0) {
				p = -p;
			}
			p = ((p * 9) / 10) + 1;
			ps->data[x] = (ps->v3_1 * p) >> 15;
		} else {
			ps->data[x] = ps->v3_1 + ps->v3_2;
		}
	}

	ps->f.frametype = AST_FRAME_VOICE;
	ast_format_set(&ps->f.subclass.format, AST_FORMAT_SLINEAR, 0);
	ps->f.datalen = len;
	ps->f.samples = samples;
	ps->f.offset = AST_FRIENDLY_OFFSET;
	ps->f.data.ptr = ps->data;

	if (ast_write(chan, &ps->f)) {
		return -1;
	}

	ps->pos += x;

	if (pi->duration && ps->pos >= pi->duration * 8) {	/* item finished? */
		ps->pos = 0;					/* start new item */
		ps->npos++;
		if (ps->npos >= ps->nitems) {			/* last item? */
			if (ps->reppos == -1) {			/* repeat set? */
				return -1;
			}
			ps->npos = ps->reppos;			/* redo from top */
		}
	}

	return 0;
}

static struct ast_generator playtones = {
	.alloc     = playtones_alloc,
	.release   = playtones_release,
	.generate  = playtones_generator,
};

int ast_tone_zone_part_parse(const char *s, struct ast_tone_zone_part *tone_data)
{
	if (sscanf(s, "%30u+%30u/%30u", &tone_data->freq1, &tone_data->freq2,
			&tone_data->time) == 3) {
		/* f1+f2/time format */
	} else if (sscanf(s, "%30u+%30u", &tone_data->freq1, &tone_data->freq2) == 2) {
		/* f1+f2 format */
		tone_data->time = 0;
	} else if (sscanf(s, "%30u*%30u/%30u", &tone_data->freq1, &tone_data->freq2,
			&tone_data->time) == 3) {
		/* f1*f2/time format */
		tone_data->modulate = 1;
	} else if (sscanf(s, "%30u*%30u", &tone_data->freq1, &tone_data->freq2) == 2) {
		/* f1*f2 format */
		tone_data->time = 0;
		tone_data->modulate = 1;
	} else if (sscanf(s, "%30u/%30u", &tone_data->freq1, &tone_data->time) == 2) {
		/* f1/time format */
		tone_data->freq2 = 0;
	} else if (sscanf(s, "%30u", &tone_data->freq1) == 1) {
		/* f1 format */
		tone_data->freq2 = 0;
		tone_data->time = 0;
	} else if (sscanf(s, "M%30u+M%30u/%30u", &tone_data->freq1, &tone_data->freq2,
			&tone_data->time) == 3) {
		/* Mf1+Mf2/time format */
		tone_data->midinote = 1;
	} else if (sscanf(s, "M%30u+M%30u", &tone_data->freq1, &tone_data->freq2) == 2) {
		/* Mf1+Mf2 format */
		tone_data->time = 0;
		tone_data->midinote = 1;
	} else if (sscanf(s, "M%30u*M%30u/%30u", &tone_data->freq1, &tone_data->freq2,
			&tone_data->time) == 3) {
		/* Mf1*Mf2/time format */
		tone_data->modulate = 1;
		tone_data->midinote = 1;
	} else if (sscanf(s, "M%30u*M%30u", &tone_data->freq1, &tone_data->freq2) == 2) {
		/* Mf1*Mf2 format */
		tone_data->time = 0;
		tone_data->modulate = 1;
		tone_data->midinote = 1;
	} else if (sscanf(s, "M%30u/%30u", &tone_data->freq1, &tone_data->time) == 2) {
		/* Mf1/time format */
		tone_data->freq2 = -1;
		tone_data->midinote = 1;
	} else if (sscanf(s, "M%30u", &tone_data->freq1) == 1) {
		/* Mf1 format */
		tone_data->freq2 = -1;
		tone_data->time = 0;
		tone_data->midinote = 1;
	} else {
		return -1;
	}

	return 0;
}

int ast_playtones_start(struct ast_channel *chan, int vol, const char *playlst, int interruptible)
{
	char *s, *data = ast_strdupa(playlst);
	struct playtones_def d = { vol, -1, 0, 1, NULL };
	char *stringp;
	char *separator;
	static const float sample_rate = 8000.0;
	static const float max_sample_val = 32768.0;

	if (vol < 1) {
		d.vol = 7219; /* Default to -8db */
	}

	d.interruptible = interruptible;

	stringp = data;

	/* check if the data is separated with '|' or with ',' by default */
	if (strchr(stringp,'|')) {
		separator = "|";
	} else {
		separator = ",";
	}

	while ((s = strsep(&stringp, separator)) && !ast_strlen_zero(s)) {
		struct playtones_item *new_items;
		struct ast_tone_zone_part tone_data = {
			.time = 0,
		};

		s = ast_strip(s);
		if (s[0]=='!') {
			s++;
		} else if (d.reppos == -1) {
			d.reppos = d.nitems;
		}

		if (ast_tone_zone_part_parse(s, &tone_data)) {
			ast_log(LOG_ERROR, "Failed to parse tone part '%s'\n", s);
			continue;
		}

		if (tone_data.midinote) {
			/* midi notes must be between 0 and 127 */
			if (tone_data.freq1 <= 127) {
				tone_data.freq1 = midi_tohz[tone_data.freq1];
			} else {
				tone_data.freq1 = 0;
			}

			if (tone_data.freq2 <= 127) {
				tone_data.freq2 = midi_tohz[tone_data.freq2];
			} else {
				tone_data.freq2 = 0;
			}
		}

		new_items = ast_realloc(d.items, (d.nitems + 1) * sizeof(*d.items));
		if (!new_items) {
			ast_free(d.items);
			return -1;
		}
		d.items = new_items;

		d.items[d.nitems].fac1 = 2.0 * cos(2.0 * M_PI * (tone_data.freq1 / sample_rate)) * max_sample_val;
		d.items[d.nitems].init_v2_1 = sin(-4.0 * M_PI * (tone_data.freq1 / sample_rate)) * d.vol;
		d.items[d.nitems].init_v3_1 = sin(-2.0 * M_PI * (tone_data.freq1 / sample_rate)) * d.vol;

		d.items[d.nitems].fac2 = 2.0 * cos(2.0 * M_PI * (tone_data.freq2 / sample_rate)) * max_sample_val;
		d.items[d.nitems].init_v2_2 = sin(-4.0 * M_PI * (tone_data.freq2 / sample_rate)) * d.vol;
		d.items[d.nitems].init_v3_2 = sin(-2.0 * M_PI * (tone_data.freq2 / sample_rate)) * d.vol;

		d.items[d.nitems].duration = tone_data.time;
		d.items[d.nitems].modulate = tone_data.modulate;

		d.nitems++;
	}

	if (!d.nitems) {
		ast_log(LOG_ERROR, "No valid tone parts\n");
		return -1;
	}

	if (ast_activate_generator(chan, &playtones, &d)) {
		ast_free(d.items);
		return -1;
	}

	return 0;
}

void ast_playtones_stop(struct ast_channel *chan)
{
	ast_deactivate_generator(chan);
}

int ast_tone_zone_count(void)
{
	return ao2_container_count(ast_tone_zones);
}

struct ao2_iterator ast_tone_zone_iterator_init(void)
{
	return ao2_iterator_init(ast_tone_zones, 0);
}

/*! \brief Set global indication country
   If no country is specified or we are unable to find the zone, then return not found */
static int ast_set_indication_country(const char *country)
{
	struct ast_tone_zone *zone = NULL;

	if (ast_strlen_zero(country) || !(zone = ast_get_indication_zone(country))) {
		return -1;
	}

	ast_verb(3, "Setting default indication country to '%s'\n", country);

	ao2_lock(ast_tone_zones);
	if (default_tone_zone) {
		default_tone_zone = ast_tone_zone_unref(default_tone_zone);
	}
	default_tone_zone = ast_tone_zone_ref(zone);
	ao2_unlock(ast_tone_zones);

	zone = ast_tone_zone_unref(zone);

	return 0;
}

/*! \brief locate ast_tone_zone, given the country. if country == NULL, use the default country */
struct ast_tone_zone *ast_get_indication_zone(const char *country)
{
	struct ast_tone_zone *tz = NULL;
	struct ast_tone_zone zone_arg = {
		.nrringcadence = 0,
	};

	if (ast_strlen_zero(country)) {
		ao2_lock(ast_tone_zones);
		if (default_tone_zone) {
			tz = ast_tone_zone_ref(default_tone_zone);
		}
		ao2_unlock(ast_tone_zones);

		return tz;
	}

	ast_copy_string(zone_arg.country, country, sizeof(zone_arg.country));

	return ao2_find(ast_tone_zones, &zone_arg, OBJ_POINTER);
}

struct ast_tone_zone_sound *ast_get_indication_tone(const struct ast_tone_zone *_zone, const char *indication)
{
	struct ast_tone_zone_sound *ts = NULL;
	/* _zone is const to the users of the API */
	struct ast_tone_zone *zone = (struct ast_tone_zone *) _zone;

	/* If no zone is specified, use the default */
	if (!zone) {
		ao2_lock(ast_tone_zones);
		if (default_tone_zone) {
			zone = ast_tone_zone_ref(default_tone_zone);
		}
		ao2_unlock(ast_tone_zones);

		if (!zone) {
			return NULL;
		}
	}

	ast_tone_zone_lock(zone);

	/* Look through list of tones in the zone searching for the right one */
	AST_LIST_TRAVERSE(&zone->tones, ts, entry) {
		if (!strcasecmp(ts->name, indication)) {
			/* Increase ref count for the reference we will return */
			ts = ast_tone_zone_sound_ref(ts);
			break;
		}
	}

	ast_tone_zone_unlock(zone);

	if (!_zone)
		zone = ast_tone_zone_unref(zone);

	return ts;
}

static void ast_tone_zone_sound_destructor(void *obj)
{
	struct ast_tone_zone_sound *ts = obj;

	/* Deconstify the 'const char *'s so the compiler doesn't complain. (but it's safe) */
	if (ts->name) {
		ast_free((char *) ts->name);
		ts->name = NULL;
	}

	if (ts->data) {
		ast_free((char *) ts->data);
		ts->data = NULL;
	}
}

/*! \brief deallocate the passed tone zone */
static void ast_tone_zone_destructor(void *obj)
{
	struct ast_tone_zone *zone = obj;
	struct ast_tone_zone_sound *current;

	while ((current = AST_LIST_REMOVE_HEAD(&zone->tones, entry))) {
		current = ast_tone_zone_sound_unref(current);
	}

	if (zone->ringcadence) {
		ast_free(zone->ringcadence);
		zone->ringcadence = NULL;
	}
}

/*! \brief add a new country, if country exists, it will be replaced. */
static int ast_register_indication_country(struct ast_tone_zone *zone)
{
	ao2_lock(ast_tone_zones);
	if (!default_tone_zone) {
		default_tone_zone = ast_tone_zone_ref(zone);
	}
	ao2_unlock(ast_tone_zones);

	ao2_link(ast_tone_zones, zone);

	ast_verb(3, "Registered indication country '%s'\n", zone->country);

	return 0;
}

/*! \brief remove an existing country and all its indications, country must exist. */
static int ast_unregister_indication_country(const char *country)
{
	struct ast_tone_zone *tz = NULL;
	struct ast_tone_zone zone_arg = {
		.nrringcadence = 0,
	};

	ast_copy_string(zone_arg.country, country, sizeof(zone_arg.country));

	ao2_lock(ast_tone_zones);
	tz = ao2_find(ast_tone_zones, &zone_arg, OBJ_POINTER | OBJ_UNLINK);
	if (!tz) {
		ao2_unlock(ast_tone_zones);
		return -1;
	}

	if (default_tone_zone == tz) {
		ast_tone_zone_unref(default_tone_zone);
		/* Get a new default, punt to the first one we find */
		default_tone_zone = ao2_callback(ast_tone_zones, 0, NULL, NULL);
	}
	ao2_unlock(ast_tone_zones);

	tz = ast_tone_zone_unref(tz);

	return 0;
}

/*!
 * \note called with the tone zone locked
 */
static int ast_register_indication(struct ast_tone_zone *zone, const char *indication,
		const char *tonelist)
{
	struct ast_tone_zone_sound *ts;

	if (ast_strlen_zero(indication) || ast_strlen_zero(tonelist)) {
		return -1;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(&zone->tones, ts, entry) {
		if (!strcasecmp(indication, ts->name)) {
			AST_LIST_REMOVE_CURRENT(entry);
			ts = ast_tone_zone_sound_unref(ts);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (!(ts = ao2_alloc(sizeof(*ts), ast_tone_zone_sound_destructor))) {
		return -1;
	}

	if (!(ts->name = ast_strdup(indication)) || !(ts->data = ast_strdup(tonelist))) {
		ts = ast_tone_zone_sound_unref(ts);
		return -1;
	}

	AST_LIST_INSERT_TAIL(&zone->tones, ts, entry); /* Inherit reference */

	return 0;
}

/*! \brief remove an existing country's indication. Both country and indication must exist */
static int ast_unregister_indication(struct ast_tone_zone *zone, const char *indication)
{
	struct ast_tone_zone_sound *ts;
	int res = -1;

	ast_tone_zone_lock(zone);

	AST_LIST_TRAVERSE_SAFE_BEGIN(&zone->tones, ts, entry) {
		if (!strcasecmp(indication, ts->name)) {
			AST_LIST_REMOVE_CURRENT(entry);
			ts = ast_tone_zone_sound_unref(ts);
			res = 0;
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	ast_tone_zone_unlock(zone);

	return res;
}

static struct ast_tone_zone *ast_tone_zone_alloc(void)
{
	return ao2_alloc(sizeof(struct ast_tone_zone), ast_tone_zone_destructor);
}

static char *complete_country(struct ast_cli_args *a)
{
	char *res = NULL;
	struct ao2_iterator i;
	int which = 0;
	size_t wordlen;
	struct ast_tone_zone *tz;

	wordlen = strlen(a->word);

	i = ao2_iterator_init(ast_tone_zones, 0);
	while ((tz = ao2_iterator_next(&i))) {
		if (!strncasecmp(a->word, tz->country, wordlen) && ++which > a->n) {
			res = ast_strdup(tz->country);
		}
		tz = ast_tone_zone_unref(tz);
		if (res) {
			break;
		}
	}
	ao2_iterator_destroy(&i);

	return res;
}

static char *handle_cli_indication_add(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_tone_zone *tz;
	int created_country = 0;
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "indication add";
		e->usage =
			"Usage: indication add <country> <indication> \"<tonelist>\"\n"
			"       Add the given indication to the country.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_country(a);
		} else {
			return NULL;
		}
	}

	if (a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (!(tz = ast_get_indication_zone(a->argv[2]))) {
		/* country does not exist, create it */
		ast_log(LOG_NOTICE, "Country '%s' does not exist, creating it.\n", a->argv[2]);

		if (!(tz = ast_tone_zone_alloc())) {
			return CLI_FAILURE;
		}

		ast_copy_string(tz->country, a->argv[2], sizeof(tz->country));

		if (ast_register_indication_country(tz)) {
			ast_log(LOG_WARNING, "Unable to register new country\n");
			tz = ast_tone_zone_unref(tz);
			return CLI_FAILURE;
		}

		created_country = 1;
	}

	ast_tone_zone_lock(tz);

	if (ast_register_indication(tz, a->argv[3], a->argv[4])) {
		ast_log(LOG_WARNING, "Unable to register indication %s/%s\n", a->argv[2], a->argv[3]);
		if (created_country) {
			ast_unregister_indication_country(a->argv[2]);
		}
		res = CLI_FAILURE;
	}

	ast_tone_zone_unlock(tz);

	tz = ast_tone_zone_unref(tz);

	return res;
}

static char *complete_indications(struct ast_cli_args *a)
{
	char *res = NULL;
	int which = 0;
	size_t wordlen;
	struct ast_tone_zone_sound *ts;
	struct ast_tone_zone *tz, tmp_tz = {
		.nrringcadence = 0,
	};

	ast_copy_string(tmp_tz.country, a->argv[a->pos - 1], sizeof(tmp_tz.country));

	if (!(tz = ao2_find(ast_tone_zones, &tmp_tz, OBJ_POINTER))) {
		return NULL;
	}

	wordlen = strlen(a->word);

	ast_tone_zone_lock(tz);
	AST_LIST_TRAVERSE(&tz->tones, ts, entry) {
		if (!strncasecmp(a->word, ts->name, wordlen) && ++which > a->n) {
			res = ast_strdup(ts->name);
			break;
		}
	}
	ast_tone_zone_unlock(tz);

	tz = ast_tone_zone_unref(tz);

	return res;
}

static char *handle_cli_indication_remove(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_tone_zone *tz;
	char *res = CLI_SUCCESS;

	switch (cmd) {
	case CLI_INIT:
		e->command = "indication remove";
		e->usage =
			"Usage: indication remove <country> [indication]\n"
			"       Remove the given indication from the country.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_country(a);
		} else if (a->pos == 3) {
			return complete_indications(a);
		}
	}

	if (a->argc != 3 && a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 3) {
		/* remove entire country */
		if (ast_unregister_indication_country(a->argv[2])) {
			ast_log(LOG_WARNING, "Unable to unregister indication country %s\n", a->argv[2]);
			return CLI_FAILURE;
		}

		return CLI_SUCCESS;
	}

	if (!(tz = ast_get_indication_zone(a->argv[2]))) {
		ast_log(LOG_WARNING, "Unable to unregister indication %s/%s, country does not exists\n", a->argv[2], a->argv[3]);
		return CLI_FAILURE;
	}

	if (ast_unregister_indication(tz, a->argv[3])) {
		ast_log(LOG_WARNING, "Unable to unregister indication %s/%s\n", a->argv[2], a->argv[3]);
		res = CLI_FAILURE;
	}

	tz = ast_tone_zone_unref(tz);

	return res;
}

static char *handle_cli_indication_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_tone_zone *tz = NULL;
	struct ast_str *buf;
	int found_country = 0;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "indication show";
		e->usage =
			"Usage: indication show [<country> ...]\n"
			"       Display either a condensed summary of all countries and indications, or a\n"
			"       more verbose list of indications for the specified countries.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_country(a);
	}

	if (a->argc == 2) {
		struct ao2_iterator iter;
		/* no arguments, show a list of countries */
		ast_cli(a->fd, "Country   Description\n");
		ast_cli(a->fd, "===========================\n");
		iter = ast_tone_zone_iterator_init();
		while ((tz = ao2_iterator_next(&iter))) {
			ast_tone_zone_lock(tz);
			ast_cli(a->fd, "%-7.7s  %s\n", tz->country, tz->description);
			ast_tone_zone_unlock(tz);
			tz = ast_tone_zone_unref(tz);
		}
		ao2_iterator_destroy(&iter);
		return CLI_SUCCESS;
	}

	buf = ast_str_alloca(256);

	for (i = 2; i < a->argc; i++) {
		struct ast_tone_zone zone_arg = {
			.nrringcadence = 0,
		};
		struct ast_tone_zone_sound *ts;
		int j;

		ast_copy_string(zone_arg.country, a->argv[i], sizeof(zone_arg.country));

		if (!(tz = ao2_find(ast_tone_zones, &zone_arg, OBJ_POINTER))) {
			continue;
		}

		if (!found_country) {
			found_country = 1;
			ast_cli(a->fd, "Country Indication      PlayList\n");
			ast_cli(a->fd, "=====================================\n");
		}

		ast_tone_zone_lock(tz);

		ast_str_set(&buf, 0, "%-7.7s %-15.15s ", tz->country, "<ringcadence>");
		for (j = 0; j < tz->nrringcadence; j++) {
			ast_str_append(&buf, 0, "%d%s", tz->ringcadence[j],
					(j == tz->nrringcadence - 1) ? "" : ",");
		}
		ast_str_append(&buf, 0, "\n");
		ast_cli(a->fd, "%s", ast_str_buffer(buf));

		AST_LIST_TRAVERSE(&tz->tones, ts, entry) {
			ast_cli(a->fd, "%-7.7s %-15.15s %s\n", tz->country, ts->name, ts->data);
		}

		ast_tone_zone_unlock(tz);
		tz = ast_tone_zone_unref(tz);
	}

	if (!found_country) {
		ast_cli(a->fd, "No countries matched your criteria.\n");
	}

	return CLI_SUCCESS;
}

static int is_valid_tone_zone(struct ast_tone_zone *zone)
{
	int res;

	ast_tone_zone_lock(zone);
	res = (!ast_strlen_zero(zone->description) && !AST_LIST_EMPTY(&zone->tones));
	ast_tone_zone_unlock(zone);

	return res;
}

/*!\brief
 *
 * \note This is called with the tone zone locked.
 */
static void store_tone_zone_ring_cadence(struct ast_tone_zone *zone, const char *val)
{
	char buf[1024];
	char *ring, *c = buf;

	ast_copy_string(buf, val, sizeof(buf));

	while ((ring = strsep(&c, ","))) {
		int *tmp, val;

		ring = ast_strip(ring);

		if (!isdigit(ring[0]) || (val = atoi(ring)) == -1) {
			ast_log(LOG_WARNING, "Invalid ringcadence given '%s'.\n", ring);
			continue;
		}

		if (!(tmp = ast_realloc(zone->ringcadence, (zone->nrringcadence + 1) * sizeof(int)))) {
			return;
		}

		zone->ringcadence = tmp;
		tmp[zone->nrringcadence] = val;
		zone->nrringcadence++;
	}
}

static void store_config_tone_zone(struct ast_tone_zone *zone, const char *var,
		const char *value)
{
	CV_START(var, value);

	CV_STR("description", zone->description);
	CV_F("ringcadence", store_tone_zone_ring_cadence(zone, value));

	ast_register_indication(zone, var, value);

	CV_END;
}

static void reset_tone_zone(struct ast_tone_zone *zone)
{
	ast_tone_zone_lock(zone);

	zone->killme = 0;

	if (zone->nrringcadence) {
		zone->nrringcadence = 0;
		ast_free(zone->ringcadence);
		zone->ringcadence = NULL;
	}

	ast_tone_zone_unlock(zone);
}

static int parse_tone_zone(struct ast_config *cfg, const char *country)
{
	struct ast_variable *v;
	struct ast_tone_zone *zone;
	struct ast_tone_zone tmp_zone = {
		.nrringcadence = 0,
	};
	int allocd = 0;

	ast_copy_string(tmp_zone.country, country, sizeof(tmp_zone.country));

	if ((zone = ao2_find(ast_tone_zones, &tmp_zone, OBJ_POINTER))) {
		reset_tone_zone(zone);
	} else if ((zone = ast_tone_zone_alloc())) {
		allocd = 1;
		ast_copy_string(zone->country, country, sizeof(zone->country));
	} else {
		return -1;
	}

	ast_tone_zone_lock(zone);
	for (v = ast_variable_browse(cfg, country); v; v = v->next) {
		store_config_tone_zone(zone, v->name, v->value);
	}
	ast_tone_zone_unlock(zone);

	if (allocd) {
		if (!is_valid_tone_zone(zone)) {
			ast_log(LOG_WARNING, "Indication country '%s' is invalid\n", country);
		} else if (ast_register_indication_country(zone)) {
			ast_log(LOG_WARNING, "Unable to register indication country '%s'.\n",
					country);
		}
	}

	zone = ast_tone_zone_unref(zone);

	return 0;
}

/*! \brief
 * Mark the zone and its tones before parsing configuration.  We will use this
 * to know what to remove after configuration is parsed.
 */
static int tone_zone_mark(void *obj, void *arg, int flags)
{
	struct ast_tone_zone *zone = obj;
	struct ast_tone_zone_sound *s;

	ast_tone_zone_lock(zone);

	zone->killme = 1;

	AST_LIST_TRAVERSE(&zone->tones, s, entry) {
		s->killme = 1;
	}

	ast_tone_zone_unlock(zone);

	return 0;
}

/*! \brief
 * Prune tones no longer in the configuration, and have the tone zone unlinked
 * if it is no longer in the configuration at all.
 */
static int prune_tone_zone(void *obj, void *arg, int flags)
{
	struct ast_tone_zone *zone = obj;
	struct ast_tone_zone_sound *s;

	ast_tone_zone_lock(zone);

	AST_LIST_TRAVERSE_SAFE_BEGIN(&zone->tones, s, entry) {
		if (s->killme) {
			AST_LIST_REMOVE_CURRENT(entry);
			s = ast_tone_zone_sound_unref(s);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	ast_tone_zone_unlock(zone);

	return zone->killme ? CMP_MATCH : 0;
}

/*! \brief load indications module */
static int load_indications(int reload)
{
	struct ast_config *cfg;
	const char *cxt = NULL;
	const char *country = NULL;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int res = -1;

	cfg = ast_config_load2(config, "indications", config_flags);

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_WARNING, "Can't find indications config file %s.\n", config);
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	/* Lock the container to prevent multiple simultaneous reloads */
	ao2_lock(ast_tone_zones);

	ao2_callback(ast_tone_zones, OBJ_NODATA, tone_zone_mark, NULL);

	/* Use existing config to populate the Indication table */
	while ((cxt = ast_category_browse(cfg, cxt))) {
		/* All categories but "general" are considered countries */
		if (!strcasecmp(cxt, "general")) {
			continue;
		}

		if (parse_tone_zone(cfg, cxt)) {
			goto return_cleanup;
		}
	}

	ao2_callback(ast_tone_zones, OBJ_NODATA | OBJ_MULTIPLE | OBJ_UNLINK,
			prune_tone_zone, NULL);

	/* determine which country is the default */
	country = ast_variable_retrieve(cfg, "general", "country");
	if (ast_strlen_zero(country) || ast_set_indication_country(country)) {
		ast_log(LOG_WARNING, "Unable to set the default country (for indication tones)\n");
	}

	res = 0;

return_cleanup:
	ao2_unlock(ast_tone_zones);
	ast_config_destroy(cfg);

	return res;
}

/*! \brief CLI entries for commands provided by this module */
static struct ast_cli_entry cli_indications[] = {
	AST_CLI_DEFINE(handle_cli_indication_add,    "Add the given indication to the country"),
	AST_CLI_DEFINE(handle_cli_indication_remove, "Remove the given indication from the country"),
	AST_CLI_DEFINE(handle_cli_indication_show,   "Display a list of all countries/indications")
};

static int ast_tone_zone_hash(const void *obj, const int flags)
{
	const struct ast_tone_zone *zone = obj;

	return ast_str_case_hash(zone->country);
}

static int ast_tone_zone_cmp(void *obj, void *arg, int flags)
{
	struct ast_tone_zone *zone = obj;
	struct ast_tone_zone *zone_arg = arg;

	return (!strcasecmp(zone->country, zone_arg->country)) ?
			CMP_MATCH | CMP_STOP : 0;
}

int ast_tone_zone_data_add_structure(struct ast_data *tree, struct ast_tone_zone *zone)
{
	struct ast_data *data_zone_sound;
	struct ast_tone_zone_sound *s;

	ast_data_add_structure(ast_tone_zone, tree, zone);

	if (AST_LIST_EMPTY(&zone->tones)) {
		return 0;
	}

	data_zone_sound = ast_data_add_node(tree, "tones");
	if (!data_zone_sound) {
		return -1;
	}

	ast_tone_zone_lock(zone);

	AST_LIST_TRAVERSE(&zone->tones, s, entry) {
		ast_data_add_structure(ast_tone_zone_sound, data_zone_sound, s);
	}

	ast_tone_zone_unlock(zone);

	return 0;
}

/*! \internal \brief Clean up resources on Asterisk shutdown */
static void indications_shutdown(void)
{
	ast_cli_unregister_multiple(cli_indications, ARRAY_LEN(cli_indications));
	if (default_tone_zone) {
		ast_tone_zone_unref(default_tone_zone);
		default_tone_zone = NULL;
	}
	if (ast_tone_zones) {
		ao2_ref(ast_tone_zones, -1);
		ast_tone_zones = NULL;
	}
}

/*! \brief Load indications module */
int ast_indications_init(void)
{
	if (!(ast_tone_zones = ao2_container_alloc(NUM_TONE_ZONE_BUCKETS,
			ast_tone_zone_hash, ast_tone_zone_cmp))) {
		return -1;
	}

	if (load_indications(0)) {
		indications_shutdown();
		return -1;
	}

	ast_cli_register_multiple(cli_indications, ARRAY_LEN(cli_indications));

	ast_register_cleanup(indications_shutdown);
	return 0;
}

/*! \brief Reload indications module */
int ast_indications_reload(void)
{
	return load_indications(1);
}

