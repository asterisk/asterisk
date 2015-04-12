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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <sys/stat.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/astdb.h"

/*** DOCUMENTATION
	<function name="DIALGROUP" language="en_US">
		<synopsis>
			Manages a group of users for dialing.
		</synopsis>
		<syntax>
			<parameter name="group" required="true" />
			<parameter name="op">
				<para>The operation name, possible values are:</para>
				<para><literal>add</literal> - add a channel name or interface (write-only)</para>
				<para><literal>del</literal> - remove a channel name or interface (write-only)</para>
			</parameter>
		</syntax>
		<description>
			<para>Presents an interface meant to be used in concert with the Dial
			application, by presenting a list of channels which should be dialled when
			referenced.</para>
			<para>When DIALGROUP is read from, the argument is interpreted as the particular
			<replaceable>group</replaceable> for which a dial should be attempted.  When DIALGROUP is written to
			with no arguments, the entire list is replaced with the argument specified.</para>
			<para>Functionality is similar to a queue, except that when no interfaces are
			available, execution may continue in the dialplan.  This is useful when
			you want certain people to be the first to answer any calls, with immediate
			fallback to a queue when the front line people are busy or unavailable, but
			you still want front line people to log in and out of that group, just like
			a queue.</para>
			<para>Example:</para>
			<para>exten => 1,1,Set(DIALGROUP(mygroup,add)=SIP/10)</para>
			<para>exten => 1,n,Set(DIALGROUP(mygroup,add)=SIP/20)</para>
			<para>exten => 1,n,Dial(${DIALGROUP(mygroup)})</para>
		</description>
	</function>
 ***/

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
		return strcmp(g1->name, g2->name) ? 0 : CMP_MATCH | CMP_STOP;
	else
		return strcmp(g1->name, name) ? 0 : CMP_MATCH | CMP_STOP;
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
		return strcmp(e1->name, e2->name) ? 0 : CMP_MATCH | CMP_STOP;
	else
		return strcmp(e1->name, name) ? 0 : CMP_MATCH | CMP_STOP;
}

static int dialgroup_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ao2_iterator i;
	struct group *grhead = ao2_find(group_container, data, 0);
	struct group_entry *entry;
	size_t bufused = 0;
	int trunc_warning = 0;
	int res = 0;

	if (!grhead) {
		if (!ast_strlen_zero(cmd)) {
			ast_log(LOG_WARNING, "No such dialgroup '%s'\n", data);
		}
		return -1;
	}

	buf[0] = '\0';

	i = ao2_iterator_init(grhead->entries, 0);
	while ((entry = ao2_iterator_next(&i))) {
		int tmp = strlen(entry->name);
		/* Ensure that we copy only complete names, not partials */
		if (len - bufused > tmp + 2) {
			if (bufused != 0)
				buf[bufused++] = '&';
			ast_copy_string(buf + bufused, entry->name, len - bufused);
			bufused += tmp;
		} else if (trunc_warning++ == 0) {
			if (!ast_strlen_zero(cmd)) {
				ast_log(LOG_WARNING, "Dialgroup '%s' is too large.  Truncating list.\n", data);
			} else {
				res = 1;
				ao2_ref(entry, -1);
				break;
			}
		}
		ao2_ref(entry, -1);
	}
	ao2_iterator_destroy(&i);
	ao2_ref(grhead, -1);

	return res;
}

static int dialgroup_refreshdb(struct ast_channel *chan, const char *cdialgroup)
{
	int len = 500, res = 0;
	char *buf = NULL;
	char *new_buf;
	char *dialgroup = ast_strdupa(cdialgroup);

	do {
		len *= 2;
		new_buf = ast_realloc(buf, len);
		if (!new_buf) {
			ast_free(buf);
			return -1;
		}
		buf = new_buf;

		if ((res = dialgroup_read(chan, "", dialgroup, buf, len)) < 0) {
			ast_free(buf);
			return -1;
		}
	} while (res == 1);

	if (ast_strlen_zero(buf)) {
		ast_db_del("dialgroup", cdialgroup);
	} else {
		ast_db_put("dialgroup", cdialgroup, buf);
	}
	ast_free(buf);
	return 0;
}

static int dialgroup_write(struct ast_channel *chan, const char *cmd, char *data, const char *cvalue)
{
	struct group *grhead;
	struct group_entry *entry;
	int j, needrefresh = 1;
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

	if (!(grhead = ao2_find(group_container, args.group, 0))) {
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
		if (!(grhead->entries = ao2_container_alloc(37, entry_hash_fn, entry_cmp_fn))) {
			ao2_unlink(group_container, grhead);
			ao2_ref(grhead, -1);
			return -1;
		}
	}

	if (strcasecmp(args.op, "add") == 0) {
		for (j = 0; j < inter.argc; j++) {
			/* Eliminate duplicates */
			if ((entry = ao2_find(grhead->entries, inter.faces[j], 0))) {
				ao2_ref(entry, -1);
				continue;
			}
			if ((entry = ao2_alloc(sizeof(*entry), NULL))) {
				ast_copy_string(entry->name, inter.faces[j], sizeof(entry->name));
				ao2_link(grhead->entries, entry);
				ao2_ref(entry, -1);
			} else {
				ast_log(LOG_WARNING, "Unable to add '%s' to dialgroup '%s'\n", inter.faces[j], grhead->name);
			}
		}
	} else if (strncasecmp(args.op, "del", 3) == 0) {
		for (j = 0; j < inter.argc; j++) {
			if ((entry = ao2_find(grhead->entries, inter.faces[j], OBJ_UNLINK))) {
				ao2_ref(entry, -1);
			} else {
				ast_log(LOG_WARNING, "Interface '%s' not found in dialgroup '%s'\n", inter.faces[j], grhead->name);
			}
		}
	} else {
		ast_log(LOG_ERROR, "Unrecognized operation: %s\n", args.op);
		needrefresh = 0;
	}
	ao2_ref(grhead, -1);

	if (needrefresh) {
		dialgroup_refreshdb(chan, args.group);
	}

	return 0;
}

static struct ast_custom_function dialgroup_function = {
	.name = "DIALGROUP",
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
	struct ast_db_entry *dbtree, *tmp;
	char groupname[AST_MAX_EXTENSION], *ptr;

	if ((group_container = ao2_container_alloc(37, group_hash_fn, group_cmp_fn))) {
		/* Refresh groups from astdb */
		if ((dbtree = ast_db_gettree("dialgroup", NULL))) {
			for (tmp = dbtree; tmp; tmp = tmp->next) {
				ast_copy_string(groupname, tmp->key, sizeof(groupname));
				if ((ptr = strrchr(groupname, '/'))) {
					ptr++;
					dialgroup_write(NULL, "", ptr, tmp->data);
				}
			}
			ast_db_freetree(dbtree);
		}
		return ast_custom_function_register(&dialgroup_function);
	} else {
		return AST_MODULE_LOAD_DECLINE;
	}
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Dialgroup dialplan function");
