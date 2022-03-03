/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, CFWare, LLC
 *
 * Corey Farrell <git@cfware.com>
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

/*! \file
 *
 * \brief Dialplan context include routines.
 *
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/pbx.h"
#include "pbx_private.h"

/*! ast_include: include= support in extensions.conf */
struct ast_include {
	const char *name;
	/*! Context to include */
	const char *rname;
	/*! Registrar */
	const char *registrar;
	/*! If time construct exists */
	int hastime;
	/*! time construct */
	struct ast_timing timing;
	char stuff[0];
};

const char *ast_get_include_name(const struct ast_include *inc)
{
	return inc ? inc->name : NULL;
}

const char *include_rname(const struct ast_include *inc)
{
	return inc ? inc->rname : NULL;
}

const char *ast_get_include_registrar(const struct ast_include *inc)
{
	return inc ? inc->registrar : NULL;
}

int include_valid(const struct ast_include *inc)
{
	if (!inc->hastime) {
		return 1;
	}

	return ast_check_timing(&(inc->timing));
}

struct ast_include *include_alloc(const char *value, const char *registrar)
{
	struct ast_include *new_include;
	char *c;
	int valuebufsz = strlen(value) + 1;
	char *p;

	/* allocate new include structure ... */
	new_include = ast_calloc(1, sizeof(*new_include) + (valuebufsz * 2));
	if (!new_include) {
		return NULL;
	}

	/* Fill in this structure. Use 'p' for assignments, as the fields
	 * in the structure are 'const char *'
	 */
	p = new_include->stuff;
	new_include->name = p;
	strcpy(p, value);
	p += valuebufsz;
	new_include->rname = p;
	strcpy(p, value);
	/* Strip off timing info, and process if it is there */
	if ( (c = strchr(p, '|')) || (c = strchr(p, ',')) ) {
		*c++ = '\0';
		new_include->hastime = ast_build_timing(&(new_include->timing), c);
	}
	new_include->registrar = registrar;

	return new_include;
}

void include_free(struct ast_include *inc)
{
	ast_destroy_timing(&(inc->timing));
	ast_free(inc);
}
