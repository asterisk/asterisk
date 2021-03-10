/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005-2006, Russell Bryant
 *
 * Russell Bryant <russell@digium.com>
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

/*!
 * \file
 *
 * \brief public data structures and defaults for menuselect
 *
 */

#ifndef MENUSELECT_H
#define MENUSELECT_H

#include "linkedlists.h"

#define OUTPUT_MAKEOPTS_DEFAULT "menuselect.makeopts"
#define OUTPUT_MAKEDEPS_DEFAULT "menuselect.makedeps"
#define MENUSELECT_DEPS         "build_tools/menuselect-deps"

struct member;

struct reference {
	/*! the name of the dependency */
	const char *name;
	/*! the display name of the dependency */
	const char *displayname;
	/*! if this dependency is a member, not an external object */
	struct member *member;
	/*! if this package was found */
	unsigned char met:1;
	/*! if this package should be autoselected */
	unsigned char autoselect:1;
	/*! for linking */
	AST_LIST_ENTRY(reference) list;
};

enum failure_types {
	NO_FAILURE = 0,
	SOFT_FAILURE = 1,
	HARD_FAILURE = 2,
};

AST_LIST_HEAD_NOLOCK(reference_list, reference);

struct member {
	/*! What will be sent to the makeopts file */
	const char *name;
	/*! Display name if known */
	const char *displayname;
	/*! Default setting */
	const char *defaultenabled;
	/*! Delete these file(s) if this member changes */
	const char *remove_on_change;
	/*! Touch these file(s) if this member changes */
	const char *touch_on_change;
	const char *support_level;
	const char *replacement;
	const char *deprecated_in;
	const char *removed_in;
	/*! member_data is just an opaque, member-specific string */
	const char *member_data;
	/*! This module is currently selected */
	unsigned int enabled:1;
	/*! This module was enabled when the config was loaded */
	unsigned int was_enabled:1;
	/*! This module has failed dependencies */
	unsigned int depsfailed:2;
	/*! Previous failed dependencies when calculating */
	unsigned int depsfailedold:2;
	/*! This module has failed conflicts */
	unsigned int conflictsfailed:2;
	/*! This module's 'enabled' flag was changed by a default only */
	unsigned int was_defaulted:1;
	/*! This module is a dependency, and if it is selected then
	  we have included it in the MENUSELECT_BUILD_DEPS line
	  in the output file */
	unsigned int build_deps_output:1;
	/*! This module should never be enabled automatically, but only
	 * when explicitly set. */
	unsigned int explicitly_enabled_only:1;
	/*! This isn't actually a module!  It's a separator, and it should
	 * be passed over for many of the usual purposes associated with members. */
	unsigned int is_separator:1;
	/*! dependencies of this module */
	struct reference_list deps;
	/*! conflicts of this module */
	struct reference_list conflicts;
	/*! optional packages used by this module */
	struct reference_list uses;
	/*! for making a list of modules */
	AST_LIST_ENTRY(member) list;
};

enum support_level_values {
	SUPPORT_CORE = 0,
	SUPPORT_EXTENDED = 1,
	SUPPORT_DEPRECATED = 2,
	SUPPORT_UNSPECIFIED = 3,
	SUPPORT_EXTERNAL = 4,
	SUPPORT_OPTION = 5,
	SUPPORT_COUNT = 6, /* Keep this item at the end of the list. Tracks total number of support levels. */
};

AST_LIST_HEAD_NOLOCK(support_level_bucket, member);

struct category {
	/*! Workspace for building support levels */
	struct support_level_bucket buckets[SUPPORT_COUNT];
	/*! the Makefile variable */
	const char *name;
	/*! the name displayed in the menu */
	const char *displayname;
	/*! Delete these file(s) if anything in this category changes */
	const char *remove_on_change;
	/*! Touch these file(s) if anything in this category changes */
	const char *touch_on_change;
	/*! Output what is selected, as opposed to not selected */
	unsigned int positive_output:1;
	/*! All choices in this category are mutually exclusive */
	unsigned int exclusive:1;
	/*! the list of possible values to be set in this variable */
	AST_LIST_HEAD_NOLOCK(, member) members;
	/*! for linking */
	AST_LIST_ENTRY(category) list;
};

extern AST_LIST_HEAD_NOLOCK(categories, category) categories;

extern const char *menu_name;

/*! This is implemented by the frontend */
int run_menu(void);

int count_categories(void);

int count_members(struct category *cat);

/*! \brief Toggle a member of a category at the specified index to enabled/disabled */
void toggle_enabled_index(struct category *cat, int index);

void toggle_enabled(struct member *mem);

/*! \brief Set a member of a category at the specified index to enabled */
void set_enabled(struct category *cat, int index);
/*! \brief Set a member of a category at the specified index to not enabled */
void clear_enabled(struct category *cat, int index);

/*! \brief Enable/Disable all members of a category as long as dependencies have been met and no conflicts are found */
void set_all(struct category *cat, int val);

/*! \brief returns non-zero if the string is not defined, or has zero length */
static inline int strlen_zero(const char *s)
{
	return (!s || (*s == '\0'));
}

#if !defined(ast_strdupa) && defined(__GNUC__)
#define ast_strdupa(s)                                                    \
	(__extension__                                                    \
	({                                                                \
		const char *__old = (s);                                  \
		size_t __len = strlen(__old) + 1;                         \
		char *__new = __builtin_alloca(__len);                    \
		memcpy (__new, __old, __len);                             \
		__new;                                                    \
	}))
#endif

#endif /* MENUSELECT_H */
