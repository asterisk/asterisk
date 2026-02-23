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
 * \brief Channel Variables
 */

#ifndef _ASTERISK_CHANVARS_H
#define _ASTERISK_CHANVARS_H

#include "asterisk/linkedlists.h"

struct ast_var_t {
	AST_LIST_ENTRY(ast_var_t) entries;
	char *value;
	char name[0];
};

AST_LIST_HEAD_NOLOCK(varshead, ast_var_t);


/*!
 * \brief Create a new variables list and initalize to empty
 *
 * \retval NULL on error
 * \retval returns the pointer to the newly created list on success
 */
struct varshead *ast_var_list_create(void);

/*!
 * \brief Remove all variables from the list, free them and also free the list
 *
 * \param head pointer to the list
 *
 */
void ast_var_list_destroy(struct varshead *head);

struct ast_var_t *_ast_var_assign(const char *name, const char *value, const char *file, int lineno, const char *function);

/*!
 * \brief Insert a new variable into the variables list and store the value
 *
 * \param name      name of the variable to set
 * \param value     value of the variable to set
 *
 * \retval NULL on error
 * \retval returns a populated struct ast_var_t
 */
#define ast_var_assign(name, value) _ast_var_assign(name, value, __FILE__, __LINE__, __PRETTY_FUNCTION__)

/*!
 * \brief Free a variable.  This does not remove the variable from the list that it might be a part of
 * \see linkedlists.h
 *
 * \see ast_var_find
 * \see ast_var_assign
 *
 * \param var           must be a struct ast_var_t* that's been set up with ast_var_assign
 *
 */
void ast_var_delete(struct ast_var_t *var);

/*!
 * \brief Return the name component of an existing struct ast_var_t, stripping any _ or __ inheritance modifiers
 *
 * \param var must be a struct ast_var_t * (Allowed to be empty/uninitalized)
 *
 * \retval NULL on error
 * \retval variable name on success
 */
const char *ast_var_name(const struct ast_var_t *var);

/*!
 * \brief Return the full name component of an existing struct ast_var_t, including any _ or __ inheritance modifiers
 *
 * \param var must be a struct ast_var_t * (Allowed to be empty/uninitalized)
 *
 * \retval NULL on error
 * \retval variable name on success
 */
const char *ast_var_full_name(const struct ast_var_t *var);

/*!
 * \brief Return the value component of an existing struct ast_var_t
 *
 * \param var must be a struct ast_var_t * (Allowed to be empty/uninitalized)
 *
 * \retval NULL on error
 * \retval variable value on success
 */
const char *ast_var_value(const struct ast_var_t *var);

/*!
 * \brief Find a variable by full name
 *
 * \note If the original variable was set with a _ or __ prefix, the name param for this search will have to match exactly
 *
 * \see ast_var_full_name
 *
 * \param varshead      pointer to an existing and fully set up variables list
 * \param name          name of the variable to find
 *
 * \retval variable value on success
 * \retval NULL on not found
 */
char *ast_var_find(const struct varshead *head, const char *name);

/*!
 * \brief Create a brand new variables list with the same variables as the source list
 *
 * \param head an existing varshead
 *
 * \retval NULL on error
 * \retval pointer to new list on success
 */
struct varshead *ast_var_list_clone(struct varshead *head);

/*!
 * \brief Traverse the variable list in the same style as AST_LIST_TRAVERSE
 *
 * * \param head This is a pointer to the list head structure
 * \param var This is the name of the variable that will hold a pointer to the
 * current list entry on each iteration. It must be declared before calling
 * this macro.
 * \param field This is the name of the field (declared using AST_LIST_ENTRY())
 * used to link entries of this list together.
 *
 *
 * \see AST_LIST_TRAVERSE
 */
#define AST_VAR_LIST_TRAVERSE(head, var) AST_LIST_TRAVERSE(head, var, entries)

/*!
 * \brief Insert into the end of the variables list in the same style as AST_LIST_INSERT_TAIL
 *
 * \see AST_LIST_INSERT_TAIL
 *
 * \param head          list to insert into
 * \param var           must be a struct ast_var_t * (Allowed to be empty/uninitalized)
 *
 */
static inline void AST_VAR_LIST_INSERT_TAIL(struct varshead *head, struct ast_var_t *var) {
	if (var) {
		AST_LIST_INSERT_TAIL(head, var, entries);
	}
}


/*!
 * \brief Insert into the beginning of the variables list in the same style as AST_LIST_INSERT_HEAD
 *
 * \see AST_LIST_INSERT_HEAD
 *
 * \param head          list to insert into
 * \param var           must be a struct ast_var_t * (Allowed to be empty/uninitalized)
 *
 */
static inline void AST_VAR_LIST_INSERT_HEAD(struct varshead *head, struct ast_var_t *var) {
	if (var) {
		AST_LIST_INSERT_HEAD(head, var, entries);
	}
}

#endif /* _ASTERISK_CHANVARS_H */
