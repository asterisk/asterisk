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
 * \brief Dialplan context ignorepat routines.
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

/*! \brief ast_ignorepat: Ignore patterns in dial plan */
struct ast_ignorepat {
	const char *registrar;
	const char pattern[0];
};

const char *ast_get_ignorepat_name(const struct ast_ignorepat *ip)
{
	return ip ? ip->pattern : NULL;
}

const char *ast_get_ignorepat_registrar(const struct ast_ignorepat *ip)
{
	return ip ? ip->registrar : NULL;
}

struct ast_ignorepat *ignorepat_alloc(const char *value, const char *registrar)
{
	struct ast_ignorepat *ignorepat;
	int length = strlen(value) + 1;
	char *pattern;

	/* allocate new include structure ... */
	ignorepat = ast_calloc(1, sizeof(*ignorepat) + length);
	if (!ignorepat) {
		return NULL;
	}

	/* The cast to char * is because we need to write the initial value.
	 * The field is not supposed to be modified otherwise.  Also, gcc 4.2
	 * sees the cast as dereferencing a type-punned pointer and warns about
	 * it.  This is the workaround (we're telling gcc, yes, that's really
	 * what we wanted to do).
	 */
	pattern = (char *) ignorepat->pattern;
	strcpy(pattern, value);
	ignorepat->registrar = registrar;

	return ignorepat;
}

void ignorepat_free(struct ast_ignorepat *ip)
{
	ast_free(ip);
}
