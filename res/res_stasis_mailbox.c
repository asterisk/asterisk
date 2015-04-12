/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<depend type="module">res_mwi_external</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/astdb.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/stasis_app_impl.h"
#include "asterisk/stasis_app_mailbox.h"
#include "asterisk/res_mwi_external.h"

/*! Number of hash buckets for mailboxes */
#define MAILBOX_BUCKETS 37

static struct ast_json *mailbox_to_json(
	const struct ast_mwi_mailbox_object *mailbox)
{
	return ast_json_pack("{s: s, s: i, s: i}",
		"name", ast_mwi_mailbox_get_id(mailbox),
		"old_messages", ast_mwi_mailbox_get_msgs_old(mailbox),
		"new_messages", ast_mwi_mailbox_get_msgs_new(mailbox));
}

enum stasis_mailbox_result stasis_app_mailbox_to_json(
	const char *name, struct ast_json **json)
{
	struct ast_json *mailbox_json;
	const struct ast_mwi_mailbox_object *mailbox;

	mailbox = ast_mwi_mailbox_get(name);
	if (!mailbox) {
		return STASIS_MAILBOX_MISSING;
	}

	mailbox_json = mailbox_to_json(mailbox);
	if (!mailbox_json) {
		ast_mwi_mailbox_unref(mailbox);
		return STASIS_MAILBOX_ERROR;
	}

	*json = mailbox_json;

	return STASIS_MAILBOX_OK;
}

struct ast_json *stasis_app_mailboxes_to_json()
{
	struct ast_json *array = ast_json_array_create();
	struct ao2_container *mailboxes;
	struct ao2_iterator iter;
	const struct ast_mwi_mailbox_object *mailbox;

	if (!array) {
		return NULL;
	}

	mailboxes = ast_mwi_mailbox_get_all();
	if (!mailboxes) {
		ast_json_unref(array);
		return NULL;
	}

	iter = ao2_iterator_init(mailboxes, 0);
	for (; (mailbox = ao2_iterator_next(&iter)); ast_mwi_mailbox_unref(mailbox)) {
		struct ast_json *appending = mailbox_to_json(mailbox);
		if (!appending || ast_json_array_append(array, appending)) {
			/* Failed to append individual mailbox to the array. Abort. */
			ast_json_unref(array);
			array = NULL;
			break;
		}
	}
	ao2_iterator_destroy(&iter);

	return array;
}

int stasis_app_mailbox_update(
	const char *name, int old_messages, int new_messages)
{
	struct ast_mwi_mailbox_object *mailbox;
	int res = 0;

	mailbox = ast_mwi_mailbox_alloc(name);
	if (!mailbox) {
		return -1;
	}
	ast_mwi_mailbox_set_msgs_new(mailbox, new_messages);
	ast_mwi_mailbox_set_msgs_old(mailbox, old_messages);
	if (ast_mwi_mailbox_update(mailbox)) {
		res = -1;
	}

	ast_mwi_mailbox_unref(mailbox);

	return res;
}

enum stasis_mailbox_result stasis_app_mailbox_delete(
	const char *name)
{
	const struct ast_mwi_mailbox_object *mailbox;

	/* Make sure the mailbox actually exists before we delete it */
	mailbox = ast_mwi_mailbox_get(name);
	if (!mailbox) {
		return STASIS_MAILBOX_MISSING;
	}

	ast_mwi_mailbox_unref(mailbox);
	mailbox = NULL;

	/* Now delete the mailbox */
	if (ast_mwi_mailbox_delete(name)) {
		return STASIS_MAILBOX_ERROR;
	}

	return STASIS_MAILBOX_OK;
}

static int load_module(void)
{
	/* Must be done first */
	ast_mwi_external_ref();

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	/* Must be done last */
	ast_mwi_external_unref();

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Stasis application mailbox support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis,res_mwi_external"
	);
