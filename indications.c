/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Tone Management
 * 
 * Copyright (C) 2002, Pauline Middelink
 *
 * Pauline Middelink <middelink@polyware.nl>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * This set of function allow us to play a list of tones on a channel.
 * Each element has two frequencies, which are mixed together and a
 * duration. For silence both frequencies can be set to 0.
 * The playtones can be given as a comma separated string.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>			/* For PI */
#include <asterisk/indications.h>
#include <asterisk/frame.h>
#include <asterisk/options.h>
#include <asterisk/channel.h>
#include <asterisk/logger.h>
#include <asterisk/lock.h>
#include <asterisk/utils.h>

struct playtones_item {
	int freq1;
	int freq2;
	int duration;
	int modulate;
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
	int reppos;
	int nitems;
	struct playtones_item *items;
	int npos;
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
	for (x=0;x<len/2;x++) {
		if (pi->modulate)
		/* Modulate 1st tone with 2nd, to 90% modulation depth */
		ps->data[x] = ps->vol * 2 * (
			sin((pi->freq1 * 2.0 * M_PI / 8000.0) * (ps->pos + x)) *
			(0.9 * fabs(sin((pi->freq2 * 2.0 * M_PI / 8000.0) * (ps->pos + x))) + 0.1)
			);
		else
			/* Add 2 tones together */
			ps->data[x] = ps->vol * (
				sin((pi->freq1 * 2.0 * M_PI / 8000.0) * (ps->pos + x)) +
				sin((pi->freq2 * 2.0 * M_PI / 8000.0) * (ps->pos + x))
			);
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
		d.vol = 8192;

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
		int freq1, freq2, time, modulate=0;

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
		} else {
			ast_log(LOG_WARNING,"%s: tone component '%s' of '%s' is no good\n",chan->name,s,playlst);
			return -1;
		}

		d.items = realloc(d.items,(d.nitems+1)*sizeof(struct playtones_item));
		if (d.items == NULL)
			return -1;
		d.items[d.nitems].freq1    = freq1;
		d.items[d.nitems].freq2    = freq2;
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
