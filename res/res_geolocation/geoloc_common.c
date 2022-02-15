/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#include "asterisk.h"
#include "geoloc_private.h"

static const char *result_names[] = {
	"Success",
	"Missing type",
	"Invalid shape type",
	"Invalid variable name",
	"Not enough variables",
	"Too many variables",
	"Invalid variable value"
};

const char *ast_geoloc_validate_result_to_str(enum ast_geoloc_validate_result result)
{
	return result_names[result];
}

