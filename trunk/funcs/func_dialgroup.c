/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Tilghman Lesher
 *
 * Tilghman Lesher <func_dialgroup__200709@the-tilghman.com>
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
 * \brief Dial group dialplan function
 *
 * \author Tilghman Lesher <func_dialgroup__200709@the-tilghman.com>
 *
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"

static struct ao2_container *group_container = NULL;

struct group_entry {
	char name[AST_CHANNEL_NAME];
};

struct group {
	char name[AST_MAX_EXTENSION];
	struct ao2_container *entries;
};

static void group_destroy(void *vgroup)
{
	struct group *group = vgroup;
	ao2_ref(group->entries, -1);
}

static int group_hash_fn(const void *obj, const int flags)
{
	const struct group *g = obj;
	return ast_str_hash(g->name);
}

static int group_cmp_fn(void *obj1, void *name2, int flags)
{
	struct group *g1 = obj1, *g2 = name2;
	char *name = name2;
	if (flags & OBJ_POINTER)
		return strcmp(g1->name, g2->name) ? 0 : CMP_MATCH;
	else
		return strcmp(g1->name, name) ? 0 : CMP_MATCH;
}

static int entry_hash_fn(const void *obj, const int flags)
{
	const struct group_entry *e = obj;
	return ast_str_hash(e->name);
}

static int entry_cmp_fn(void *obj1, void *name2, int flags)
{
	struct group_entry *e1 = obj1, *e2 = name2;
	char *name = name2;
	if (flags & OBJ_POINTER)
		return strcmp(e1->name, e2->name) ? 0 : CMP_MATCH;
	else
		return strcmp(e1->name, name) ? 0 : CMP_MATCH;
}

static int dialgroup_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ao2_iterator i;
	struct group *grhead = ao2_find(group_container, data, 0);
	struct group_entry *entry;
	size_t bufused = 0;
	int trunc_warning = 0;

	if (!grhead) {
		ast_log(LOG_WARNING, "No such dialgroup '%s'\n", data);
		return -1;
	}

	i = ao2_iterator_init(grhead->entries, 0);
	while ((entry = ao2_iterator_next(&i))) {
		int tmp = strlen(entry->name);
		/* Ensure that we copy only complete names, not partials */
		if (len - bufused > tmp + 2) {
			if (bufused != 0)
				buf[bufused++] = '&';
			ast_copy_string(buf + bufused, entry->name, len - bufused);
			bufused += tmp;
		} else if (trunc_warning++ == 0)
			ast_log(LOG_WARNING, "Dialgroup '%s' is too large.  Truncating list.\n", data);
	}

	return 0;
}

static int dialgroup_write(struct ast_channel *chan, const char *cmd, char *data, const char *cvalue)
{
	struct group *grhead;
	struct group_entry *entry;
	int j;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(group);
		AST_APP_ARG(op);
	);
	AST_DECLARE_APP_ARGS(inter,
		AST_APP_ARG(faces)[100];
	);
	char *value = ast_strdupa(cvalue);

	AST_STANDARD_APP_ARGS(args, data);
	AST_NONSTANDARD_APP_ARGS(inter, value, '&');

	if (!(grhead = ao2_find(group_container, data, 0))) {
		/* Create group */
		grhead = ao2_alloc(sizeof(*grhead), group_destroy);
		if (!grhead)
			return -1;
		grhead->entries = ao2_container_alloc(37, entry_hash_fn, entry_cmp_fn);
		if (!grhead->entries) {
			ao2_ref(grhead, -1);
			return -1;
		}
		ast_copy_string(grhead->name, args.group, sizeof(grhead->name));
		ao2_link(group_container, grhead);
	}

	if (ast_strlen_zero(args.op)) {
		/* Wholesale replacement of the group */
		args.op = "add";

		/* Remove all existing */
		ao2_ref(grhead->entries, -1);
		if (!(grhead->entries = ao2_container_alloc(37, entry_hash_fn, entry_cmp_fn)))
			return -1;
	}

	if (strcasecmp(args.op, "add") == 0) {
		for (j = 0; j < inter.argc; j++) {
			if ((entry = ao2_alloc(sizeof(*entry), NULL))) {
				ast_copy_string(entry->name, inter.faces[j], sizeof(entry->name));
				ao2_link(grhead->entries, entry);
			} else
				ast_log(LOG_WARNING, "Unable to add '%s' to dialgroup '%s'\n", inter.faces[j], grhead->name);
		}
	} else if (strncasecmp(args.op, "del", 3) == 0) {
		for (j = 0; j < inter.argc; j++) {
			if ((entry = ao2_find(grhead->entries, inter.faces[j], OBJ_UNLINK)))
				ao2_ref(entry, -1);
			else
				ast_log(LOG_WARNING, "Interface '%s' not found in dialgroup '%s'\n", inter.faces[j], grhead->name);
		}
	} else
		ast_log(LOG_ERROR, "Unrecognized operation: %s\n", args.op);

	return 0;
}

static struct ast_custom_function dialgroup_function = {
	.name = "DIALGROUP",
	.synopsis = "Manages a group of users for dialing",
	.syntax = "DIALGROUP(<group>[,op])",
	.desc =
"  DIALGROUP presents an interface meant to be used in concert with the Dial\n"
"application, by presenting a list of channels which should be dialled when\n"
"referenced.\n"
"  When DIALGROUP is read from, the argument is interpreted as the particular\n"
"group for which a dial should be attempted.  When DIALGROUP is written to\n"
"with no arguments, the entire list is replaced with the argument specified.\n"
"Other operations are as follows:\n"
"  add - add a channel name or interface (write-only)\n"
"  del - remove a channel name or interface (write-only)\n\n"
"Functionality is similar to a queue, except that when no interfaces are\n"
"available, execution may continue in the dialplan.  This is useful when\n"
"you want certain people to be the first to answer any calls, with immediate\n"
"fallback to a queue when the front line people are busy or unavailable, but\n"
"you still want front line people to log in and out of that group, just like\n"
"a queue.\n",
	.read = dialgroup_read,
	.write = dialgroup_write,
};

static int unload_module(void)
{
	int res = ast_custom_function_unregister(&dialgroup_function);
	ao2_ref(group_container, -1);
	return res;
}

static int load_module(void)
{
	if ((group_container = ao2_container_alloc(37, group_hash_fn, group_cmp_fn)))
		return ast_custom_function_register(&dialgroup_function);
	else
		return AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialgroup dialplan function");
