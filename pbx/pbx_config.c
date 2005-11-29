/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Populate and remember extensions from static config file
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <asterisk/pbx.h>
#include <asterisk/config.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* For where to put dynamic tables */
#include "../asterisk.h"

static char *dtext = "Text Extension Configuration";
static char *config = "extensions.conf";
static char *registrar = "pbx_config";

static int static_config = 0;

int unload_module(void)
{
	ast_context_destroy(NULL, registrar);
	return 0;
}

static int pbx_load_module(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	char *cxt, *ext, *pri, *appl, *data, *tc;
	struct ast_context *con;

	cfg = ast_load(config);
	if (cfg) {
		/* Use existing config to populate the PBX table */
		static_config = ast_true(ast_variable_retrieve(cfg, "general", "static"));
		cxt = ast_category_browse(cfg, NULL);
		while(cxt) {
			/* All categories but "general" are considered contexts */
			if (!strcasecmp(cxt, "general")) {
				cxt = ast_category_browse(cfg, cxt);
				continue;
			}
			if ((con=ast_context_create(cxt, registrar))) {
				v = ast_variable_browse(cfg, cxt);
				while(v) {
					if (!strcasecmp(v->name, "exten")) {
						tc = strdup(v->value);
						ext = strtok(tc, ",");
						if (!ext)
							ext="";
						pri = strtok(NULL, ",");
						if (!pri)
							pri="";
						appl = strtok(NULL, ",");
						if (!appl)
							appl="";
						data = strtok(NULL, ",");
						if (!data)
							data="";
						if (ast_add_extension2(con, 0, ext, atoi(pri), appl, strdup(data), free, registrar)) {
							ast_log(LOG_WARNING, "Unable to register extension at line %d\n", v->lineno);
						}
						free(tc);
					} else if(!strcasecmp(v->name, "include")) {
						if (ast_context_add_include2(con, v->value, registrar))
							ast_log(LOG_WARNING, "Unable to include context '%s' in context '%s'\n", v->value, cxt);
					}
					v = v->next;
				}
			}
			cxt = ast_category_browse(cfg, cxt);
		}
		ast_destroy(cfg);
	}
	return 0;
}

int load_module(void)
{
	return pbx_load_module();
}

int reload(void)
{
	ast_context_destroy(NULL, registrar);
	load_module();
	return 0;
}

int usecount(void)
{
	return 0;
}

char *description(void)
{
	return dtext;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}
