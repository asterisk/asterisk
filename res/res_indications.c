/** @file res_indications.c 
 *
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Load the indications
 * 
 * Copyright (C) 2002, Pauline Middelink
 *
 * Pauline Middelink <middelink@polyware.nl>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * Load the country specific dialtones into the asterisk PBX.
 */
 
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <asterisk/lock.h>
#include <asterisk/file.h>
#include <asterisk/cli.h>
#include <asterisk/logger.h>
#include <asterisk/config.h>
#include <asterisk/channel.h>
#include <asterisk/pbx.h>
#include <asterisk/module.h>
#include <asterisk/translate.h>
#include <asterisk/indications.h>


/* Globals */
static const char dtext[] = "Indications Configuration";
static const char config[] = "indications.conf";

/*
 * Help for commands provided by this module ...
 */
static char help_add_indication[] =
"Usage: indication add <country> <indication> \"<tonelist>\"\n"
"       Add the given indication to the country.\n";

static char help_remove_indication[] =
"Usage: indication remove <country> <indication>\n"
"       Remove the given indication from the country.\n";

static char help_show_indications[] =
"Usage: show indications [<country> ...]\n"
"       Show either a condensed for of all country/indications, or the\n"
"       indications for the specified countries.\n";

char *playtones_desc=
"PlayTone(arg): Plays a tone list. Execution will continue with the next step immediately,\n"
"while the tones continue to play.\n"
"Arg is either the tone name defined in the indications.conf configuration file, or a directly\n"
"specified list of frequencies and durations.\n"
"See indications.conf for a description of the specification of a tonelist.\n\n"
"Use the StopPlaytones application to stop the tones playing. \n";

/*
 * Implementation of functions provided by this module
 */

/*
 * ADD INDICATION command stuff
 */
static int handle_add_indication(int fd, int argc, char *argv[])
{
	struct tone_zone *tz;
	int created_country = 0;
	if (argc != 5) return RESULT_SHOWUSAGE;

	tz = ast_get_indication_zone(argv[2]);
	if (!tz) {
		/* country does not exist, create it */
		ast_log(LOG_NOTICE, "Country '%s' does not exist, creating it.\n",argv[2]);

		tz = malloc(sizeof(struct tone_zone));
		if (!tz) {
			ast_log(LOG_WARNING, "Out of memory\n");
			return -1;
		}
		memset(tz,0,sizeof(struct tone_zone));
		strncpy(tz->country,argv[2],sizeof(tz->country)-1);
		if (ast_register_indication_country(tz)) {
			ast_log(LOG_WARNING, "Unable to register new country\n");
			free(tz);
			return -1;
		}
		created_country = 1;
	}
	if (ast_register_indication(tz,argv[3],argv[4])) {
		ast_log(LOG_WARNING, "Unable to register indication %s/%s\n",argv[2],argv[3]);
		if (created_country)
			ast_unregister_indication_country(argv[2]);
		return -1;
	}
	return 0;
}

/*
 * REMOVE INDICATION command stuff
 */
static int handle_remove_indication(int fd, int argc, char *argv[])
{
	struct tone_zone *tz;
	if (argc != 3 && argc != 4) return RESULT_SHOWUSAGE;

	if (argc == 3) {
		/* remove entiry country */
		if (ast_unregister_indication_country(argv[2])) {
			ast_log(LOG_WARNING, "Unable to unregister indication country %s\n",argv[2]);
			return -1;
		}
		return 0;
	}

	tz = ast_get_indication_zone(argv[2]);
	if (!tz) {
		ast_log(LOG_WARNING, "Unable to unregister indication %s/%s, country does not exists\n",argv[2],argv[3]);
		return -1;
	}
	if (ast_unregister_indication(tz,argv[3])) {
		ast_log(LOG_WARNING, "Unable to unregister indication %s/%s\n",argv[2],argv[3]);
		return -1;
	}
	return 0;
}

/*
 * SHOW INDICATIONS command stuff
 */
static int handle_show_indications(int fd, int argc, char *argv[])
{
	struct tone_zone *tz;
	char buf[256];
	int found_country = 0;

	if (ast_mutex_lock(&tzlock)) {
		ast_log(LOG_WARNING, "Unable to lock tone_zones list\n");
		return 0;
	}
	if (argc == 2) {
		/* no arguments, show a list of countries */
		ast_cli(fd,"Country Alias   Description\n"
			   "===========================\n");
		for (tz=tone_zones; tz; tz=tz->next) {
			ast_cli(fd,"%-7.7s %-7.7s %s\n", tz->country, tz->alias, tz->description);
		}
		ast_mutex_unlock(&tzlock);
		return 0;
	}
	/* there was a request for specific country(ies), lets humor them */
	for (tz=tone_zones; tz; tz=tz->next) {
		int i,j;
		for (i=2; i<argc; i++) {
			if (strcasecmp(tz->country,argv[i])==0 &&
			    !tz->alias[0]) {
				struct tone_zone_sound* ts;
				if (!found_country) {
					found_country = 1;
					ast_cli(fd,"Country Indication      PlayList\n"
						   "=====================================\n");
				}
				j = snprintf(buf,sizeof(buf),"%-7.7s %-15.15s ",tz->country,"<ringcadance>");
				for (i=0; i<tz->nrringcadance; i++) {
					j += snprintf(buf+j,sizeof(buf)-j,"%d,",tz->ringcadance[i]);
				}
				if (tz->nrringcadance) j--;
				strncpy(buf+j,"\n",sizeof(buf)-j-1);
				ast_cli(fd,buf);
				for (ts=tz->tones; ts; ts=ts->next)
					ast_cli(fd,"%-7.7s %-15.15s %s\n",tz->country,ts->name,ts->data);
				break;
			}
		}
	}
	if (!found_country)
		ast_cli(fd,"No countries matched your criteria.\n");
	ast_mutex_unlock(&tzlock);
	return -1;
}

/*
 * Playtones command stuff
 */
static int handle_playtones(struct ast_channel *chan, void *data)
{
	struct tone_zone_sound *ts;
	int res;

	if (!data || !((char*)data)[0]) {
		ast_log(LOG_NOTICE,"Nothing to play\n");
		return -1;
	}
	ts = ast_get_indication_tone(chan->zone, (const char*)data);
	if (ts && ts->data[0])
		res = ast_playtones_start(chan, 0, ts->data, 0);
	else
		res = ast_playtones_start(chan, 0, (const char*)data, 0);
	if (res)
		ast_log(LOG_NOTICE,"Unable to start playtones\n");
	return res;
}

/*
 * StopPlaylist command stuff
 */
static int handle_stopplaytones(struct ast_channel *chan, void *data)
{
	ast_playtones_stop(chan);
	return 0;
}

/*
 * Load module stuff
 */
static int ind_load_module(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	char *cxt;
	char *c;
	struct tone_zone *tones;
	const char *country = NULL;

	/* that the following cast is needed, is yuk! */
	/* yup, checked it out. It is NOT written to. */
	cfg = ast_load((char *)config);
	if (!cfg)
		return 0;

	/* Use existing config to populate the Indication table */
	cxt = ast_category_browse(cfg, NULL);
	while(cxt) {
		/* All categories but "general" are considered countries */
		if (!strcasecmp(cxt, "general")) {
			cxt = ast_category_browse(cfg, cxt);
			continue;
		}
		tones = malloc(sizeof(struct tone_zone));
		if (!tones) {
			ast_log(LOG_WARNING,"Out of memory\n");
			ast_destroy(cfg);
			return -1;
		}
		memset(tones,0,sizeof(struct tone_zone));
		strncpy(tones->country,cxt,sizeof(tones->country) - 1);

		v = ast_variable_browse(cfg, cxt);
		while(v) {
			if (!strcasecmp(v->name, "description")) {
				strncpy(tones->description, v->value, sizeof(tones->description)-1);
			} else if (!strcasecmp(v->name,"ringcadance")) {
				char *ring,*rings = ast_strdupa(v->value);
				c = rings;
				ring = strsep(&c,",");
				while (ring) {
					int *tmp, val;
					if (!isdigit(ring[0]) || (val=atoi(ring))==-1) {
						ast_log(LOG_WARNING,"Invalid ringcadance given '%s' at line %d.\n",ring,v->lineno);
						ring = strsep(&c,",");
						continue;
					}
					tmp = realloc(tones->ringcadance,(tones->nrringcadance+1)*sizeof(int));
					if (!tmp) {
						ast_log(LOG_WARNING, "Out of memory\n");
						ast_destroy(cfg);
						return -1;
					}
					tones->ringcadance = tmp;
					tmp[tones->nrringcadance] = val;
					tones->nrringcadance++;
					/* next item */
					ring = strsep(&c,",");
				}
			} else if (!strcasecmp(v->name,"alias")) {
				char *countries = ast_strdupa(v->value);
				c = countries;
				country = strsep(&c,",");
				while (country) {
					struct tone_zone* azone = malloc(sizeof(struct tone_zone));
					if (!azone) {
						ast_log(LOG_WARNING,"Out of memory\n");
						ast_destroy(cfg);
						return -1;
					}
					memset(azone,0,sizeof(struct tone_zone));
					strncpy(azone->country, country, sizeof(azone->country) - 1);
					strncpy(azone->alias, cxt, sizeof(azone->alias)-1);
					if (ast_register_indication_country(azone)) {
						ast_log(LOG_WARNING, "Unable to register indication alias at line %d.\n",v->lineno);
						free(tones);
					}
					/* next item */
					country = strsep(&c,",");
				}
			} else {
				/* add tone to country */
				struct tone_zone_sound *ps,*ts;
				for (ps=NULL,ts=tones->tones; ts; ps=ts, ts=ts->next) {
					if (strcasecmp(v->name,ts->name)==0) {
						/* already there */
						ast_log(LOG_NOTICE,"Duplicate entry '%s', skipped.\n",v->name);
						goto out;
					}
				}
				/* not there, add it to the back */
				ts = malloc(sizeof(struct tone_zone_sound));
				if (!ts) {
					ast_log(LOG_WARNING, "Out of memory\n");
					ast_destroy(cfg);
					return -1;
				}
				ts->next = NULL;
				ts->name = strdup(v->name);
				ts->data = strdup(v->value);
				if (ps)
					ps->next = ts;
				else
					tones->tones = ts;
			}
out:			v = v->next;
		}
		if (tones->description[0] || tones->alias[0] || tones->tones) {
			if (ast_register_indication_country(tones)) {
				ast_log(LOG_WARNING, "Unable to register indication at line %d.\n",v->lineno);
				free(tones);
			}
		} else free(tones);

		cxt = ast_category_browse(cfg, cxt);
	}

	/* determine which country is the default */
	country = ast_variable_retrieve(cfg,"general","country");
	if (!country || !*country || ast_set_indication_country(country))
		ast_log(LOG_WARNING,"Unable to set the default country (for indication tones)\n");

	ast_destroy(cfg);
	return 0;
}

/*
 * CLI entries for commands provided by this module
 */
static struct ast_cli_entry add_indication_cli =
	{ { "indication", "add", NULL }, handle_add_indication,
		"Add the given indication to the country", help_add_indication,
		NULL };

static struct ast_cli_entry remove_indication_cli =
	{ { "indication", "remove", NULL }, handle_remove_indication,
		"Remove the given indication from the country", help_remove_indication,
		NULL };

static struct ast_cli_entry show_indications_cli =
	{ { "show", "indications", NULL }, handle_show_indications,
		"Show a list of all country/indications", help_show_indications,
		NULL };

/*
 * Standard module functions ...
 */
int unload_module(void)
{
	/* remove the registed indications... */
	ast_unregister_indication_country(NULL);

	/* and the functions */
	ast_cli_unregister(&add_indication_cli);
	ast_cli_unregister(&remove_indication_cli);
	ast_cli_unregister(&show_indications_cli);
	ast_unregister_application("Playtones");
	ast_unregister_application("StopPlaytones");
	return 0;
}


int load_module(void)
{
	if (ind_load_module()) return -1;
 
	ast_cli_register(&add_indication_cli);
	ast_cli_register(&remove_indication_cli);
	ast_cli_register(&show_indications_cli);
	ast_register_application("Playtones", handle_playtones, "Play a tone list", playtones_desc);
	ast_register_application("StopPlaytones", handle_stopplaytones, "Stop playing a tone list","Stop playing a tone list");

	return 0;
}

int reload(void)
{
	/* remove the registed indications... */
	ast_unregister_indication_country(NULL);

	return ind_load_module();
}

char *description(void)
{
	/* that the following cast is needed, is yuk! */
	return (char*)dtext;
}

int usecount(void)
{
	return 0;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
