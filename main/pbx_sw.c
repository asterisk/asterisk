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
 * \brief Dialplan switch routines.
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

/*! \brief ast_sw: Switch statement in extensions.conf */
struct ast_sw {
	const char *name;
	/*! Registrar */
	const char *registrar;
	/*! Data load */
	const char *data;
	int eval;
	AST_LIST_ENTRY(ast_sw) list;
	char stuff[0];
};

const char *ast_get_switch_name(const struct ast_sw *sw)
{
	return sw ? sw->name : NULL;
}

const char *ast_get_switch_data(const struct ast_sw *sw)
{
	return sw ? sw->data : NULL;
}

int ast_get_switch_eval(const struct ast_sw *sw)
{
	return sw->eval;
}

const char *ast_get_switch_registrar(const struct ast_sw *sw)
{
	return sw ? sw->registrar : NULL;
}

struct ast_sw *sw_alloc(const char *value, const char *data, int eval, const char *registrar)
{
	struct ast_sw *new_sw;
	int length;
	char *p;

	if (!data) {
		data = "";
	}
	length = sizeof(struct ast_sw);
	length += strlen(value) + 1;
	length += strlen(data) + 1;

	/* allocate new sw structure ... */
	if (!(new_sw = ast_calloc(1, length))) {
		return NULL;
	}

	/* ... fill in this structure ... */
	p = new_sw->stuff;
	new_sw->name = p;
	strcpy(p, value);

	p += strlen(value) + 1;
	new_sw->data = p;
	strcpy(p, data);

	new_sw->eval	  = eval;
	new_sw->registrar = registrar;

	return new_sw;
}

void sw_free(struct ast_sw *sw)
{
	ast_free(sw);
}
