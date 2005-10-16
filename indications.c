/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2002, Pauline Middelink
 *
 * Pauline Middelink <middelink@polyware.nl>
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

/*
 *
 * Tone Management
 * 
 * This set of function allow us to play a list of tones on a channel.
 * Each element has two frequencies, which are mixed together and a
 * duration. For silence both frequencies can be set to 0.
 * The playtones can be given as a comma separated string.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>			/* For PI */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/indications.h"
#include "asterisk/frame.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"

static int midi_tohz[128] = {
			8,8,9,9,10,10,11,12,12,13,14,
			15,16,17,18,19,20,21,23,24,25,
			27,29,30,32,34,36,38,41,43,46,
			48,51,55,58,61,65,69,73,77,82,
			87,92,97,103,110,116,123,130,138,146,
			155,164,174,184,195,207,220,233,246,261,
			277,293,311,329,349,369,391,415,440,466,
			493,523,554,587,622,659,698,739,783,830,
			880,932,987,1046,1108,1174,1244,1318,1396,1479,
			1567,1661,1760,1864,1975,2093,2217,2349,2489,2637,
			2793,2959,3135,3322,3520,3729,3951,4186,4434,4698,
			4978,5274,5587,5919,6271,6644,7040,7458,7902,8372,
			8869,9397,9956,10548,11175,11839,12543
			};

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
	int origwfmt;
	struct ast_frame f;
	unsigned char offset[AST_FRIENDLY_OFFSET];
	short data[4000];
};

static void playtones_release(struct ast_channel *chan, void *params)
{
	struct playtones_state *ps = params;
	if (chan) {
		ast_set_write_format(chan, ps->origwfmt);
	}
	if (ps->items) free(ps->items);
	free(ps);
}

static void * playtones_alloc(struct ast_channel *chan, void *params)
{
	struct playtones_def *pd = params;
	struct playtones_state *ps = malloc(sizeof(struct playtones_state));
	if (!ps)
		return NULL;
	memset(ps, 0, sizeof(struct playtones_state));
	ps->origwfmt = chan->writeformat;
	if (ast_set_write_format(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_WARNING, "Unable to set '%s' to signed linear format (write)\n", chan->name);
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
	if (pd->interruptible)
		ast_set_flag(chan, AST_FLAG_WRITE_INT);
	else
		ast_clear_flag(chan, AST_FLAG_WRITE_INT);
	return ps;
}

static int playtones_generator(struct ast_channel *chan, void *data, int len, int samples)
{
	struct playtones_state *ps = data;
	struct playtones_item *pi;
	int x;
	/* we need to prepare a frame with 16 * timelen samples as we're 
	 * generating SLIN audio
	 */
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
	for (x=0;x<len/2;x++) {
		ps->v1_1 = ps->v2_1;
		ps->v2_1 = ps->v3_1;
		ps->v3_1 = (pi->fac1 * ps->v2_1 >> 15) - ps->v1_1;
		
		ps->v1_2 = ps->v2_2;
		ps->v2_2 = ps->v3_2;
		ps->v3_2 = (pi->fac2 * ps->v2_2 >> 15) - ps->v1_2;
		if (pi->modulate) {
			int p;
			p = ps->v3_2 - 32768;
			if (p < 0) p = -p;
			p = ((p * 9) / 10) + 1;
			ps->data[x] = (ps->v3_1 * p) >> 15;
		} else
			ps->data[x] = ps->v3_1 + ps->v3_2; 
	}
	
	ps->f.frametype = AST_FRAME_VOICE;
	ps->f.subclass = AST_FORMAT_SLINEAR;
	ps->f.datalen = len;
	ps->f.samples = samples;
	ps->f.offset = AST_FRIENDLY_OFFSET;
	ps->f.data = ps->data;
	ps->f.delivery.tv_sec = 0;
	ps->f.delivery.tv_usec = 0;
	ast_write(chan, &ps->f);

	ps->pos += x;
	if (pi->duration && ps->pos >= pi->duration * 8) {	/* item finished? */
		ps->pos = 0;					/* start new item */
		ps->npos++;
		if (ps->npos >= ps->nitems) {			/* last item? */
			if (ps->reppos == -1)			/* repeat set? */
				return -1;
			ps->npos = ps->reppos;			/* redo from top */
		}
	}
	return 0;
}

static struct ast_generator playtones = {
	alloc: playtones_alloc,
	release: playtones_release,
	generate: playtones_generator,
};

int ast_playtones_start(struct ast_channel *chan, int vol, const char *playlst, int interruptible)
{
	char *s, *data = ast_strdupa(playlst); /* cute */
	struct playtones_def d = { vol, -1, 0, 1, NULL};
	char *stringp=NULL;
	char *separator;
	if (!data)
		return -1;
	if (vol < 1)
		d.vol = 7219; /* Default to -8db */

	d.interruptible = interruptible;
	
	stringp=data;
	/* the stringp/data is not null here */
	/* check if the data is separated with '|' or with ',' by default */
	if (strchr(stringp,'|'))
		separator = "|";
	else
		separator = ",";
	s = strsep(&stringp,separator);
	while (s && *s) {
		int freq1, freq2, time, modulate=0, midinote=0;

		if (s[0]=='!')
			s++;
		else if (d.reppos == -1)
			d.reppos = d.nitems;
		if (sscanf(s, "%d+%d/%d", &freq1, &freq2, &time) == 3) {
			/* f1+f2/time format */
		} else if (sscanf(s, "%d+%d", &freq1, &freq2) == 2) {
			/* f1+f2 format */
			time = 0;
		} else if (sscanf(s, "%d*%d/%d", &freq1, &freq2, &time) == 3) {
			/* f1*f2/time format */
			modulate = 1;
		} else if (sscanf(s, "%d*%d", &freq1, &freq2) == 2) {
			/* f1*f2 format */
			time = 0;
			modulate = 1;
		} else if (sscanf(s, "%d/%d", &freq1, &time) == 2) {
			/* f1/time format */
			freq2 = 0;
		} else if (sscanf(s, "%d", &freq1) == 1) {
			/* f1 format */
			freq2 = 0;
			time = 0;
		} else if (sscanf(s, "M%d+M%d/%d", &freq1, &freq2, &time) == 3) {
			/* Mf1+Mf2/time format */
			midinote = 1;
		} else if (sscanf(s, "M%d+M%d", &freq1, &freq2) == 2) {
			/* Mf1+Mf2 format */
			time = 0;
			midinote = 1;
		} else if (sscanf(s, "M%d*M%d/%d", &freq1, &freq2, &time) == 3) {
			/* Mf1*Mf2/time format */
			modulate = 1;
			midinote = 1;
		} else if (sscanf(s, "M%d*M%d", &freq1, &freq2) == 2) {
			/* Mf1*Mf2 format */
			time = 0;
			modulate = 1;
			midinote = 1;
		} else if (sscanf(s, "M%d/%d", &freq1, &time) == 2) {
			/* Mf1/time format */
			freq2 = -1;
			midinote = 1;
		} else if (sscanf(s, "M%d", &freq1) == 1) {
			/* Mf1 format */
			freq2 = -1;
			time = 0;
			midinote = 1;
		} else {
			ast_log(LOG_WARNING,"%s: tone component '%s' of '%s' is no good\n",chan->name,s,playlst);
			return -1;
		}

		if (midinote) {
			/* midi notes must be between 0 and 127 */
			if ((freq1 >= 0) && (freq1 <= 127))
				freq1 = midi_tohz[freq1];
			else
				freq1 = 0;

			if ((freq2 >= 0) && (freq2 <= 127))
				freq2 = midi_tohz[freq2];
			else
				freq2 = 0;
		}

		d.items = realloc(d.items,(d.nitems+1)*sizeof(struct playtones_item));
		if (d.items == NULL) {
			ast_log(LOG_WARNING, "Realloc failed!\n");
			return -1;
		}
		d.items[d.nitems].fac1 = 2.0 * cos(2.0 * M_PI * (freq1 / 8000.0)) * 32768.0;
		d.items[d.nitems].init_v2_1 = sin(-4.0 * M_PI * (freq1 / 8000.0)) * d.vol;
		d.items[d.nitems].init_v3_1 = sin(-2.0 * M_PI * (freq1 / 8000.0)) * d.vol;

		d.items[d.nitems].fac2 = 2.0 * cos(2.0 * M_PI * (freq2 / 8000.0)) * 32768.0;
		d.items[d.nitems].init_v2_2 = sin(-4.0 * M_PI * (freq2 / 8000.0)) * d.vol;
		d.items[d.nitems].init_v3_2 = sin(-2.0 * M_PI * (freq2 / 8000.0)) * d.vol;
		d.items[d.nitems].duration = time;
		d.items[d.nitems].modulate = modulate;
		d.nitems++;

		s = strsep(&stringp,separator);
	}

	if (ast_activate_generator(chan, &playtones, &d)) {
		free(d.items);
		return -1;
	}
	return 0;
}

void ast_playtones_stop(struct ast_channel *chan)
{
	ast_deactivate_generator(chan);
}

/*--------------------------------------------*/

struct tone_zone *tone_zones;
static struct tone_zone *current_tonezone;

/* Protect the tone_zones list (highly unlikely that two things would change
 * it at the same time, but still! */
AST_MUTEX_DEFINE_EXPORTED(tzlock);

/* Set global indication country */
int ast_set_indication_country(const char *country)
{
	if (country) {
		struct tone_zone *z = ast_get_indication_zone(country);
		if (z) {
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Setting default indication country to '%s'\n",country);
			current_tonezone = z;
			return 0;
		}
	}
	return 1; /* not found */
}

/* locate tone_zone, given the country. if country == NULL, use the default country */
struct tone_zone *ast_get_indication_zone(const char *country)
{
	struct tone_zone *tz;
	int alias_loop = 0;

	/* we need some tonezone, pick the first */
	if (country == NULL && current_tonezone)
		return current_tonezone;	/* default country? */
	if (country == NULL && tone_zones)
		return tone_zones;		/* any country? */
	if (country == NULL)
		return 0;	/* not a single country insight */

	if (ast_mutex_lock(&tzlock)) {
		ast_log(LOG_WARNING, "Unable to lock tone_zones list\n");
		return 0;
	}
	do {
		for (tz=tone_zones; tz; tz=tz->next) {
			if (strcasecmp(country,tz->country)==0) {
				/* tone_zone found */
				if (tz->alias && tz->alias[0]) {
					country = tz->alias;
					break;
				}
				ast_mutex_unlock(&tzlock);
				return tz;
			}
		}
	} while (++alias_loop<20 && tz);
	ast_mutex_unlock(&tzlock);
	if (alias_loop==20)
		ast_log(LOG_NOTICE,"Alias loop for '%s' forcefull broken\n",country);
	/* nothing found, sorry */
	return 0;
}

/* locate a tone_zone_sound, given the tone_zone. if tone_zone == NULL, use the default tone_zone */
struct tone_zone_sound *ast_get_indication_tone(const struct tone_zone *zone, const char *indication)
{
	struct tone_zone_sound *ts;

	/* we need some tonezone, pick the first */
	if (zone == NULL && current_tonezone)
		zone = current_tonezone;	/* default country? */
	if (zone == NULL && tone_zones)
		zone = tone_zones;		/* any country? */
	if (zone == NULL)
		return 0;	/* not a single country insight */

	if (ast_mutex_lock(&tzlock)) {
		ast_log(LOG_WARNING, "Unable to lock tone_zones list\n");
		return 0;
	}
	for (ts=zone->tones; ts; ts=ts->next) {
		if (strcasecmp(indication,ts->name)==0) {
			/* found indication! */
			ast_mutex_unlock(&tzlock);
			return ts;
		}
	}
	/* nothing found, sorry */
	ast_mutex_unlock(&tzlock);
	return 0;
}

/* helper function to delete a tone_zone in its entirety */
static inline void free_zone(struct tone_zone* zone)
{
	while (zone->tones) {
		struct tone_zone_sound *tmp = zone->tones->next;
		free((void*)zone->tones->name);
		free((void*)zone->tones->data);
		free(zone->tones);
		zone->tones = tmp;
	}
	if (zone->ringcadance)
		free((void*)zone->ringcadance);
	free(zone);
}

/*--------------------------------------------*/

/* add a new country, if country exists, it will be replaced. */
int ast_register_indication_country(struct tone_zone *zone)
{
	struct tone_zone *tz,*pz;

	if (ast_mutex_lock(&tzlock)) {
		ast_log(LOG_WARNING, "Unable to lock tone_zones list\n");
		return -1;
	}
	for (pz=NULL,tz=tone_zones; tz; pz=tz,tz=tz->next) {
		if (strcasecmp(zone->country,tz->country)==0) {
			/* tone_zone already there, replace */
			zone->next = tz->next;
			if (pz)
				pz->next = zone;
			else
				tone_zones = zone;
			/* if we are replacing the default zone, re-point it */
			if (tz == current_tonezone)
				current_tonezone = zone;
			/* now free the previous zone */
			free_zone(tz);
			ast_mutex_unlock(&tzlock);
			return 0;
		}
	}
	/* country not there, add */
	zone->next = NULL;
	if (pz)
		pz->next = zone;
	else
		tone_zones = zone;
	ast_mutex_unlock(&tzlock);

	if (option_verbose > 2)
		ast_verbose(VERBOSE_PREFIX_3 "Registered indication country '%s'\n",zone->country);
	return 0;
}

/* remove an existing country and all its indications, country must exist.
 * Also, all countries which are an alias for the specified country are removed. */
int ast_unregister_indication_country(const char *country)
{
	struct tone_zone *tz, *pz = NULL, *tmp;
	int res = -1;

	if (ast_mutex_lock(&tzlock)) {
		ast_log(LOG_WARNING, "Unable to lock tone_zones list\n");
		return -1;
	}
	tz = tone_zones;
	while (tz) {
		if (country==NULL ||
		    (strcasecmp(country, tz->country)==0 ||
		     strcasecmp(country, tz->alias)==0)) {
			/* tone_zone found, remove */
			tmp = tz->next;
			if (pz)
				pz->next = tmp;
			else
				tone_zones = tmp;
			/* if we are unregistering the default country, w'll notice */
			if (tz == current_tonezone) {
				ast_log(LOG_NOTICE,"Removed default indication country '%s'\n",tz->country);
				current_tonezone = NULL;
			}
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Unregistered indication country '%s'\n",tz->country);
			free_zone(tz);
			if (tone_zones == tz)
				tone_zones = tmp;
			tz = tmp;
			res = 0;
		}
		else {
			/* next zone please */
			pz = tz;
			tz = tz->next;
		}
	}
	ast_mutex_unlock(&tzlock);
	return res;
}

/* add a new indication to a tone_zone. tone_zone must exist. if the indication already
 * exists, it will be replaced. */
int ast_register_indication(struct tone_zone *zone, const char *indication, const char *tonelist)
{
	struct tone_zone_sound *ts,*ps;

	/* is it an alias? stop */
	if (zone->alias[0])
		return -1;

	if (ast_mutex_lock(&tzlock)) {
		ast_log(LOG_WARNING, "Unable to lock tone_zones list\n");
		return -2;
	}
	for (ps=NULL,ts=zone->tones; ts; ps=ts,ts=ts->next) {
		if (strcasecmp(indication,ts->name)==0) {
			/* indication already there, replace */
			free((void*)ts->name);
			free((void*)ts->data);
			break;
		}
	}
	if (!ts) {
		/* not there, we have to add */
		ts = malloc(sizeof(struct tone_zone_sound));
		if (!ts) {
			ast_log(LOG_WARNING, "Out of memory\n");
			ast_mutex_unlock(&tzlock);
			return -2;
		}
		ts->next = NULL;
	}
	ts->name = strdup(indication);
	ts->data = strdup(tonelist);
	if (ts->name==NULL || ts->data==NULL) {
		ast_log(LOG_WARNING, "Out of memory\n");
		ast_mutex_unlock(&tzlock);
		return -2;
	}
	if (ps)
		ps->next = ts;
	else
		zone->tones = ts;
	ast_mutex_unlock(&tzlock);
	return 0;
}

/* remove an existing country's indication. Both country and indication must exist */
int ast_unregister_indication(struct tone_zone *zone, const char *indication)
{
	struct tone_zone_sound *ts,*ps = NULL, *tmp;
	int res = -1;

	/* is it an alias? stop */
	if (zone->alias[0])
		return -1;

	if (ast_mutex_lock(&tzlock)) {
		ast_log(LOG_WARNING, "Unable to lock tone_zones list\n");
		return -1;
	}
	ts = zone->tones;
	while (ts) {
		if (strcasecmp(indication,ts->name)==0) {
			/* indication found */
			tmp = ts->next;
			if (ps)
				ps->next = tmp;
			else
				zone->tones = tmp;
			free((void*)ts->name);
			free((void*)ts->data);
			free(ts);
			ts = tmp;
			res = 0;
		}
		else {
			/* next zone please */
			ps = ts;
			ts = ts->next;
		}
	}
	/* indication not found, goodbye */
	ast_mutex_unlock(&tzlock);
	return res;
}
