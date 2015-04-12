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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/chanvars.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"

#ifdef MALLOC_DEBUG
struct ast_var_t *_ast_var_assign(const char *name, const char *value, const char *file, int lineno, const char *function)
#else
struct ast_var_t *ast_var_assign(const char *name, const char *value)
#endif
{
	struct ast_var_t *var;
	int name_len = strlen(name) + 1;
	int value_len = strlen(value) + 1;

#ifdef MALLOC_DEBUG
	if (!(var = __ast_calloc(sizeof(*var) + name_len + value_len, sizeof(char), file, lineno, function))) {
#else
	if (!(var = ast_calloc(sizeof(*var) + name_len + value_len, sizeof(char)))) {
#endif
		return NULL;
	}

	ast_copy_string(var->name, name, name_len);
	var->value = var->name + name_len;
	ast_copy_string(var->value, value, value_len);

	return var;
}

void ast_var_delete(struct ast_var_t *var)
{
	ast_free(var);
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

char *ast_var_find(const struct varshead *head, const char *name)
{
	struct ast_var_t *var;

	AST_LIST_TRAVERSE(head, var, entries) {
		if (!strcmp(name, var->name)) {
			return var->value;
		}
	}
	return NULL;
}

struct varshead *ast_var_list_create(void)
{
	struct varshead *head;

	head = ast_calloc(1, sizeof(*head));
	if (!head) {
		return NULL;
	}
	AST_LIST_HEAD_INIT_NOLOCK(head);
	return head;
}

void ast_var_list_destroy(struct varshead *head)
{
	struct ast_var_t *var;

	if (!head) {
		return;
	}

	while ((var = AST_LIST_REMOVE_HEAD(head, entries))) {
		ast_var_delete(var);
	}

	ast_free(head);
}

struct varshead *ast_var_list_clone(struct varshead *head)
{
	struct varshead *clone;
	struct ast_var_t *var, *newvar;

	if (!head) {
		return NULL;
	}

	clone = ast_var_list_create();
	if (!clone) {
		return NULL;
	}

	AST_VAR_LIST_TRAVERSE(head, var) {
		newvar = ast_var_assign(var->name, var->value);
		if (!newvar) {
			ast_var_list_destroy(clone);
			return NULL;
		}
		AST_VAR_LIST_INSERT_TAIL(clone, newvar);
	}

	return clone;
}
