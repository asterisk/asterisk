/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Configuration File Parser (Deprecated APIs)
 * 
 * Copyright (C) 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION("$Revision$")

#include "asterisk/config.h"
#include "asterisk/logger.h"

struct ast_config *ast_load(char *configfile)
{
	static int warning = 0;

	if (!warning) {
		ast_log(LOG_WARNING, "ast_load is deprecated, use ast_config_load instead!\n");
		warning = 1;
	}

	return ast_config_load(configfile);
}

void ast_destroy(struct ast_config *config)
{
	static int warning = 0;

	if (!warning) {
		ast_log(LOG_WARNING, "ast_destroy is deprecated, use ast_config_destroy instead!\n");
		warning = 1;
	}
	ast_config_destroy(config);
}

void ast_destroy_realtime(struct ast_variable *var)
{
	static int warning = 0;

	if (!warning) {
		ast_log(LOG_WARNING, "ast_destroy_realtime is deprecated, use ast_variables_destroy instead!\n");
		warning = 1;
	}
	ast_variables_destroy(var);
}

struct ast_config *ast_internal_load(const char *configfile, struct ast_config *cfg)
{
	static int warning = 0;

	if (!warning) {
		ast_log(LOG_WARNING, "ast_internal_load is deprecated, use ast_config_internal_load instead!\n");
		warning = 1;
	}

	return ast_internal_load(configfile, cfg);
}

