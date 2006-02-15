/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Channel Variables
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include <stdlib.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/chanvars.h"
#include "asterisk/logger.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"

struct ast_var_t *ast_var_assign(const char *name, const char *value)
{	
	struct ast_var_t *var;
	int name_len = strlen(name) + 1;
	int value_len = strlen(value) + 1;

	if (!(var = ast_calloc(sizeof(*var) + name_len + value_len, sizeof(char)))) {
		return NULL;
	}

	ast_copy_string(var->name, name, name_len);
	var->value = var->name + name_len;
	ast_copy_string(var->value, value, value_len);
	
	return var;
}	
	
void ast_var_delete(struct ast_var_t *var)
{
	if (var)
		free(var);
}

const char *ast_var_name(const struct ast_var_t *var)
{
	const char *name;

	if (var == NULL || (name = var->name) == NULL)
		return NULL;
	/* Return the name without the initial underscores */
	if (name[0] == '_') {
		name++;
		if (name[0] == '_')
			name++;
	}
	return name;
}

const char *ast_var_full_name(const struct ast_var_t *var)
{
	return (var ? var->name : NULL);
}

const char *ast_var_value(const struct ast_var_t *var)
{
	return (var ? var->value : NULL);
}


