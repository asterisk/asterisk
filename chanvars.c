/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Channel Variables
 * 
 * Copyright (C) 2002, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <stdlib.h>
#include <string.h>

#include <asterisk/chanvars.h>
#include <asterisk/logger.h>

struct ast_var_t *ast_var_assign(const char *name, const char *value)
{
	int i;
	struct ast_var_t *var;
	int len;
	
	len = sizeof(struct ast_var_t);
	
	len += strlen(name) + 1;
	len += strlen(value) + 1;
	
	var = malloc(len);

	if (var == NULL)
	{
		ast_log(LOG_WARNING, "Out of memory\n");
		return NULL;
	}
	
	i = strlen(name);
	strncpy(var->name, name, i); 
	var->name[i] = '\0';

	var->value = var->name + i + 1;

	i = strlen(value);
	strncpy(var->value, value, i);
	var->value[i] = '\0';
	
	return var;
}	
	
void ast_var_delete(struct ast_var_t *var)
{
	if (var == NULL) return;
	free(var);
}

char *ast_var_name(struct ast_var_t *var)
{
	char *name;

	if (var == NULL)
		return NULL;
	if (var->name == NULL)
		return NULL;
	/* Return the name without the initial underscores */
	if ((strlen(var->name) > 0) && (var->name[0] == '_')) {
		if ((strlen(var->name) > 1) && (var->name[1] == '_'))
			name = (char*)&(var->name[2]);
		else
			name = (char*)&(var->name[1]);
	} else
		name = var->name;
	return name;
}

char *ast_var_full_name(struct ast_var_t *var)
{
	return (var != NULL ? var->name : NULL);
}

char *ast_var_value(struct ast_var_t *var)
{
	return (var != NULL ? var->value : NULL);
}

	
