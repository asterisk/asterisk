/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2002, Pauline Middelink
 *
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

/*! \file res_indications.c 
 *
 * \brief Load the indications
 * 
 * \author Pauline Middelink <middelink@polyware.nl>
 *
 * Load the country specific dialtones into the asterisk PBX.
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>
#include <sys/stat.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/indications.h"
#include "asterisk/utils.h"

/* Globals */
static const char config[] = "indications.conf";

char *playtones_desc=
"  PlayTones(arg): Plays a tone list. Execution will continue with the next step immediately,\n"
"while the tones continue to play.\n"
"Arg is either the tone name defined in the indications.conf configuration file, or a directly\n"
"specified list of frequencies and durations.\n"
"See the sample indications.conf for a description of the specification of a tonelist.\n\n"
"Use the StopPlayTones application to stop the tones playing. \n";

/*
 * Implementation of functions provided by this module
 */

/*!
 * \brief Add a country to indication
 * \param e the ast_cli_entry for this CLI command
 * \param cmd the reason we are being called
 * \param a the arguments being passed to us
 */
static char *handle_cli_indication_add(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ind_tone_zone *tz;
	int created_country = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "indication add";
		e->usage =
			"Usage: indication add <country> <indication> \"<tonelist>\"\n"
			"       Add the given indication to the country.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 5)
		return CLI_SHOWUSAGE;

	tz = ast_get_indication_zone(a->argv[2]);
	if (!tz) {
		/* country does not exist, create it */
		ast_log(LOG_NOTICE, "Country '%s' does not exist, creating it.\n", a->argv[2]);
		
		if (!(tz = ast_calloc(1, sizeof(*tz)))) {
			return CLI_FAILURE;
		}
		ast_copy_string(tz->country, a->argv[2], sizeof(tz->country));
		if (ast_register_indication_country(tz)) {
			ast_log(LOG_WARNING, "Unable to register new country\n");
			ast_free(tz);
			return CLI_FAILURE;
		}
		created_country = 1;
	}
	if (ast_register_indication(tz, a->argv[3], a->argv[4])) {
		ast_log(LOG_WARNING, "Unable to register indication %s/%s\n", a->argv[2], a->argv[3]);
		if (created_country)
			ast_unregister_indication_country(a->argv[2]);
		return CLI_FAILURE;
	}
	return CLI_SUCCESS;
}

/*!
 * \brief Remove a country from indication
 * \param e the ast_cli_entry for this CLI command
 * \param cmd the reason we are being called
 * \param a the arguments being passed to us
 */
static char *handle_cli_indication_remove(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ind_tone_zone *tz;

	switch (cmd) {
	case CLI_INIT:
		e->command = "indication remove";
		e->usage =
			"Usage: indication remove <country> <indication>\n"
			"       Remove the given indication from the country.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 4)
		return CLI_SHOWUSAGE;

	if (a->argc == 3) {
		/* remove entiry country */
		if (ast_unregister_indication_country(a->argv[2])) {
			ast_log(LOG_WARNING, "Unable to unregister indication country %s\n", a->argv[2]);
			return CLI_FAILURE;
		}
		return CLI_SUCCESS;
	}

	tz = ast_get_indication_zone(a->argv[2]);
	if (!tz) {
		ast_log(LOG_WARNING, "Unable to unregister indication %s/%s, country does not exists\n", a->argv[2], a->argv[3]);
		return CLI_FAILURE;
	}
	if (ast_unregister_indication(tz, a->argv[3])) {
		ast_log(LOG_WARNING, "Unable to unregister indication %s/%s\n", a->argv[2], a->argv[3]);
		return CLI_FAILURE;
	}
	return CLI_SUCCESS;
}

/*!
 * \brief Show the current indications
 * \param e the ast_cli_entry for this CLI command
 * \param cmd the reason we are being called
 * \param a the arguments being passed to us
 */
static char *handle_cli_indication_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ind_tone_zone *tz = NULL;
	char buf[256];
	int found_country = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "indication show";
		e->usage =
			"Usage: indication show [<country> ...]\n"
			"       Display either a condensed for of all country/indications, or the\n"
			"       indications for the specified countries.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 2) {
		/* no arguments, show a list of countries */
		ast_cli(a->fd, "Country Alias   Description\n");
		ast_cli(a->fd, "===========================\n");
		while ((tz = ast_walk_indications(tz)))
			ast_cli(a->fd, "%-7.7s %-7.7s %s\n", tz->country, tz->alias, tz->description);
		return CLI_SUCCESS;
	}
	/* there was a request for specific country(ies), lets humor them */
	while ((tz = ast_walk_indications(tz))) {
		int i, j;
		for (i = 2; i < a->argc; i++) {
			if (strcasecmp(tz->country, a->argv[i]) == 0 && !tz->alias[0]) {
				struct ind_tone_zone_sound* ts;
				if (!found_country) {
					found_country = 1;
					ast_cli(a->fd, "Country Indication      PlayList\n");
					ast_cli(a->fd, "=====================================\n");
				}
				j = snprintf(buf, sizeof(buf), "%-7.7s %-15.15s ", tz->country, "<ringcadence>");
				for (i = 0; i < tz->nrringcadence; i++) {
					j += snprintf(buf + j, sizeof(buf) - j, "%d,", tz->ringcadence[i]);
				}
				if (tz->nrringcadence)
					j--;
				ast_copy_string(buf + j, "\n", sizeof(buf) - j);
				ast_cli(a->fd, "%s", buf);
				for (ts = tz->tones; ts; ts = ts->next)
					ast_cli(a->fd, "%-7.7s %-15.15s %s\n", tz->country, ts->name, ts->data);
				break;
			}
		}
	}
	if (!found_country)
		ast_cli(a->fd, "No countries matched your criteria.\n");
	return CLI_SUCCESS;
}

/*!
 * \brief play tone for indication country
 * \param chan ast_channel to play the sounds back to
 * \param data contains tone to play
 */
static int handle_playtones(struct ast_channel *chan, void *data)
{
	struct ind_tone_zone_sound *ts;
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

/*!
 * \brief Stop tones playing
 * \param chan 
 * \param data 
 */
static int handle_stopplaytones(struct ast_channel *chan, void *data)
{
	ast_playtones_stop(chan);
	return 0;
}

/* helper function to delete a tone_zone in its entirety */
static inline void free_zone(struct ind_tone_zone* zone)
{
	while (zone->tones) {
		struct ind_tone_zone_sound *tmp = zone->tones->next;
		ast_free((void *)zone->tones->name);
		ast_free((void *)zone->tones->data);
		ast_free(zone->tones);
		zone->tones = tmp;
	}

	if (zone->ringcadence)
		ast_free(zone->ringcadence);

	ast_free(zone);
}

/*! \brief load indications module */
static int ind_load_module(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	char *cxt;
	char *c;
	struct ind_tone_zone *tones;
	const char *country = NULL;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };

	/* that the following cast is needed, is yuk! */
	/* yup, checked it out. It is NOT written to. */
	cfg = ast_config_load((char *)config, config_flags);
	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		return -1;
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	if (reload)
		ast_unregister_indication_country(NULL);

	/* Use existing config to populate the Indication table */
	cxt = ast_category_browse(cfg, NULL);
	while(cxt) {
		/* All categories but "general" are considered countries */
		if (!strcasecmp(cxt, "general")) {
			cxt = ast_category_browse(cfg, cxt);
			continue;
		}		
		if (!(tones = ast_calloc(1, sizeof(*tones)))) {
			ast_config_destroy(cfg);
			return -1;
		}
		ast_copy_string(tones->country,cxt,sizeof(tones->country));

		v = ast_variable_browse(cfg, cxt);
		while(v) {
			if (!strcasecmp(v->name, "description")) {
				ast_copy_string(tones->description, v->value, sizeof(tones->description));
			} else if ((!strcasecmp(v->name,"ringcadence"))||(!strcasecmp(v->name,"ringcadance"))) {
				char *ring,*rings = ast_strdupa(v->value);
				c = rings;
				ring = strsep(&c,",");
				while (ring) {
					int *tmp, val;
					if (!isdigit(ring[0]) || (val=atoi(ring))==-1) {
						ast_log(LOG_WARNING,"Invalid ringcadence given '%s' at line %d.\n",ring,v->lineno);
						ring = strsep(&c,",");
						continue;
					}					
					if (!(tmp = ast_realloc(tones->ringcadence, (tones->nrringcadence + 1) * sizeof(int)))) {
						ast_config_destroy(cfg);
						free_zone(tones);
						return -1;
					}
					tones->ringcadence = tmp;
					tmp[tones->nrringcadence] = val;
					tones->nrringcadence++;
					/* next item */
					ring = strsep(&c,",");
				}
			} else if (!strcasecmp(v->name,"alias")) {
				char *countries = ast_strdupa(v->value);
				c = countries;
				country = strsep(&c,",");
				while (country) {
					struct ind_tone_zone* azone;
					if (!(azone = ast_calloc(1, sizeof(*azone)))) {
						ast_config_destroy(cfg);
						free_zone(tones);
						return -1;
					}
					ast_copy_string(azone->country, country, sizeof(azone->country));
					ast_copy_string(azone->alias, cxt, sizeof(azone->alias));
					if (ast_register_indication_country(azone)) {
						ast_log(LOG_WARNING, "Unable to register indication alias at line %d.\n",v->lineno);
						free_zone(tones);
					}
					/* next item */
					country = strsep(&c,",");
				}
			} else {
				/* add tone to country */
				struct ind_tone_zone_sound *ps,*ts;
				for (ps=NULL,ts=tones->tones; ts; ps=ts, ts=ts->next) {
					if (strcasecmp(v->name,ts->name)==0) {
						/* already there */
						ast_log(LOG_NOTICE,"Duplicate entry '%s', skipped.\n",v->name);
						goto out;
					}
				}
				/* not there, add it to the back */				
				if (!(ts = ast_malloc(sizeof(*ts)))) {
					ast_config_destroy(cfg);
					return -1;
				}
				ts->next = NULL;
				ts->name = ast_strdup(v->name);
				ts->data = ast_strdup(v->value);
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
				free_zone(tones);
			}
		} else {
			free_zone(tones);
		}

		cxt = ast_category_browse(cfg, cxt);
	}

	/* determine which country is the default */
	country = ast_variable_retrieve(cfg,"general","country");
	if (ast_strlen_zero(country) || ast_set_indication_country(country)) {
		ast_log(LOG_WARNING,"Unable to set the default country (for indication tones)\n");
	}

	ast_config_destroy(cfg);
	return 0;
}

/*! \brief CLI entries for commands provided by this module */
static struct ast_cli_entry cli_indications[] = {
	AST_CLI_DEFINE(handle_cli_indication_add,    "Add the given indication to the country"),
	AST_CLI_DEFINE(handle_cli_indication_remove, "Remove the given indication from the country"),
	AST_CLI_DEFINE(handle_cli_indication_show,   "Display a list of all countries/indications")
};

/*! \brief Unload indicators module */
static int unload_module(void)
{
	/* remove the registed indications... */
	ast_unregister_indication_country(NULL);

	/* and the functions */
	ast_cli_unregister_multiple(cli_indications, sizeof(cli_indications) / sizeof(struct ast_cli_entry));
	ast_unregister_application("PlayTones");
	ast_unregister_application("StopPlayTones");
	return 0;
}


/*! \brief Load indications module */
static int load_module(void)
{
	if (ind_load_module(0))
		return AST_MODULE_LOAD_DECLINE; 
	ast_cli_register_multiple(cli_indications, sizeof(cli_indications) / sizeof(struct ast_cli_entry));
	ast_register_application("PlayTones", handle_playtones, "Play a tone list", playtones_desc);
	ast_register_application("StopPlayTones", handle_stopplaytones, "Stop playing a tone list","  StopPlayTones(): Stop playing a tone list");

	return AST_MODULE_LOAD_SUCCESS;
}

/*! \brief Reload indications module */
static int reload(void)
{
	return ind_load_module(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Region-specific tones",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
