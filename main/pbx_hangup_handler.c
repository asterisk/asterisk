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
 * \brief PBX Hangup Handler management routines.
 *
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/utils.h"

/*!
 * \internal
 * \brief Publish a hangup handler related message to \ref stasis
 */
static void publish_hangup_handler_message(const char *action, struct ast_channel *chan, const char *handler)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	blob = ast_json_pack("{s: s, s: s}",
			"type", action,
			"handler", S_OR(handler, ""));
	if (!blob) {
		return;
	}

	ast_channel_publish_blob(chan, ast_channel_hangup_handler_type(), blob);
}

int ast_pbx_hangup_handler_run(struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;

	ast_channel_lock(chan);
	handlers = ast_channel_hangup_handlers(chan);
	if (AST_LIST_EMPTY(handlers)) {
		ast_channel_unlock(chan);
		return 0;
	}

	/*
	 * Make sure that the channel is marked as hungup since we are
	 * going to run the hangup handlers on it.
	 */
	ast_softhangup_nolock(chan, AST_SOFTHANGUP_HANGUP_EXEC);

	for (;;) {
		handlers = ast_channel_hangup_handlers(chan);
		h_handler = AST_LIST_REMOVE_HEAD(handlers, node);
		if (!h_handler) {
			break;
		}

		publish_hangup_handler_message("run", chan, h_handler->args);
		ast_channel_unlock(chan);

		ast_app_exec_sub(NULL, chan, h_handler->args, 1);
		ast_free(h_handler);

		ast_channel_lock(chan);
	}
	ast_channel_unlock(chan);
	return 1;
}

void ast_pbx_hangup_handler_init(struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;

	handlers = ast_channel_hangup_handlers(chan);
	AST_LIST_HEAD_INIT_NOLOCK(handlers);
}

void ast_pbx_hangup_handler_destroy(struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;

	ast_channel_lock(chan);

	/* Get rid of each of the hangup handlers on the channel */
	handlers = ast_channel_hangup_handlers(chan);
	while ((h_handler = AST_LIST_REMOVE_HEAD(handlers, node))) {
		ast_free(h_handler);
	}

	ast_channel_unlock(chan);
}

int ast_pbx_hangup_handler_pop(struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;

	ast_channel_lock(chan);
	handlers = ast_channel_hangup_handlers(chan);
	h_handler = AST_LIST_REMOVE_HEAD(handlers, node);
	if (h_handler) {
		publish_hangup_handler_message("pop", chan, h_handler->args);
	}
	ast_channel_unlock(chan);
	if (h_handler) {
		ast_free(h_handler);
		return 1;
	}
	return 0;
}

void ast_pbx_hangup_handler_push(struct ast_channel *chan, const char *handler)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;
	const char *expanded_handler;

	if (ast_strlen_zero(handler)) {
		return;
	}

	expanded_handler = ast_app_expand_sub_args(chan, handler);
	if (!expanded_handler) {
		return;
	}
	h_handler = ast_malloc(sizeof(*h_handler) + 1 + strlen(expanded_handler));
	if (!h_handler) {
		ast_free((char *) expanded_handler);
		return;
	}
	strcpy(h_handler->args, expanded_handler);/* Safe */
	ast_free((char *) expanded_handler);

	ast_channel_lock(chan);

	handlers = ast_channel_hangup_handlers(chan);
	AST_LIST_INSERT_HEAD(handlers, h_handler, node);
	publish_hangup_handler_message("push", chan, h_handler->args);
	ast_channel_unlock(chan);
}

#define HANDLER_FORMAT	"%-30s %s\n"

/*!
 * \internal
 * \brief CLI output the hangup handler headers.
 * \since 11.0
 *
 * \param fd CLI file descriptor to use.
 */
static void ast_pbx_hangup_handler_headers(int fd)
{
	ast_cli(fd, HANDLER_FORMAT, "Channel", "Handler");
}

/*!
 * \internal
 * \brief CLI output the channel hangup handlers.
 * \since 11.0
 *
 * \param fd CLI file descriptor to use.
 * \param chan Channel to show hangup handlers.
 */
static void ast_pbx_hangup_handler_show(int fd, struct ast_channel *chan)
{
	struct ast_hangup_handler_list *handlers;
	struct ast_hangup_handler *h_handler;
	int first = 1;

	ast_channel_lock(chan);
	handlers = ast_channel_hangup_handlers(chan);
	AST_LIST_TRAVERSE(handlers, h_handler, node) {
		ast_cli(fd, HANDLER_FORMAT, first ? ast_channel_name(chan) : "", h_handler->args);
		first = 0;
	}
	ast_channel_unlock(chan);
}

/*!
 * \brief 'show hanguphandlers \<channel\>' CLI command implementation function...
 */
static char *handle_show_hangup_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hanguphandlers";
		e->usage =
			"Usage: core show hanguphandlers <channel>\n"
			"       Show hangup handlers of a specified channel.\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, e->args);
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	chan = ast_channel_get_by_name(a->argv[3]);
	if (!chan) {
		ast_cli(a->fd, "Channel does not exist.\n");
		return CLI_FAILURE;
	}

	ast_pbx_hangup_handler_headers(a->fd);
	ast_pbx_hangup_handler_show(a->fd, chan);

	ast_channel_unref(chan);

	return CLI_SUCCESS;
}

/*!
 * \brief 'show hanguphandlers all' CLI command implementation function...
 */
static char *handle_show_hangup_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel_iterator *iter;
	struct ast_channel *chan;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show hanguphandlers all";
		e->usage =
			"Usage: core show hanguphandlers all\n"
			"       Show hangup handlers for all channels.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	iter = ast_channel_iterator_all_new();
	if (!iter) {
		return CLI_FAILURE;
	}

	ast_pbx_hangup_handler_headers(a->fd);
	for (; (chan = ast_channel_iterator_next(iter)); ast_channel_unref(chan)) {
		ast_pbx_hangup_handler_show(a->fd, chan);
	}
	ast_channel_iterator_destroy(iter);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli[] = {
	AST_CLI_DEFINE(handle_show_hangup_all, "Show hangup handlers of all channels"),
	AST_CLI_DEFINE(handle_show_hangup_channel, "Show hangup handlers of a specified channel"),
};

static void unload_pbx_hangup_handler(void)
{
	ast_cli_unregister_multiple(cli, ARRAY_LEN(cli));
}

int load_pbx_hangup_handler(void)
{
	ast_cli_register_multiple(cli, ARRAY_LEN(cli));
	ast_register_cleanup(unload_pbx_hangup_handler);

	return 0;
}
