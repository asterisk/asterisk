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
	
	var = malloc(sizeof(struct ast_var_t));

	if (var == NULL)
	{
		ast_log(LOG_WARNING, "Out of memory\n");
		return NULL;
	}
	
	i = strlen(value);
	var->value = malloc(i + 1);
	if (var->value == NULL)
	{
		ast_log(LOG_WARNING, "Out of memory\n");
		free(var);
		return NULL;
	}

	strncpy(var->value, value, i);
	var->value[i] = '\0';
	
	i = strlen(name);
	var->name = malloc(i + 1);
	if (var->name == NULL)
	{
		ast_log(LOG_WARNING, "Out of memory\n");
		free(var->value);
		free(var);
		return NULL;
	}

	strncpy(var->name, name, i); 
	var->name[i] = '\0';

	return var;
}	
	
void ast_var_delete(struct ast_var_t *var)
{
	if (var == NULL) return;

	if (var->name != NULL) free(var->name);
	if (var->value != NULL) free(var->value);

	free(var);
}

char *ast_var_name(struct ast_var_t *var)
{
	return (var != NULL ? var->name : NULL);
}

char *ast_var_value(struct ast_var_t *var)
{
	return (var != NULL ? var->value : NULL);
}

	
