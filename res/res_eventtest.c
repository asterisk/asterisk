/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
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
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief Test code for the internal event system
 * 
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$");

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/module.h"
#include "asterisk/event.h"
#include "asterisk/cli.h"

static void process_event_generic(const struct ast_event *event)
{
	ast_log(LOG_DEBUG, "Event type: %u\n", ast_event_get_type(event));
}

static void process_event_mwi(const struct ast_event *event)
{
	const char *mailbox;
	unsigned int new;
	unsigned int old;

	mailbox = ast_event_get_ie_str(event, AST_EVENT_IE_MAILBOX);
	new = ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS);
	old = ast_event_get_ie_uint(event, AST_EVENT_IE_OLDMSGS);

	ast_log(LOG_DEBUG, "MWI Event.  Mailbox: %s  New: %u  Old: %u\n",
		mailbox, new, old);
}

static void ast_event_process(const struct ast_event *event, void *userdata)
{
	switch (ast_event_get_type(event)) {
	case AST_EVENT_MWI:
		process_event_mwi(event);
		break;
	default:
		process_event_generic(event);
	}
}

static char *event_gen(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_event *event;
	const char *mailbox = "1234@fakecontext";
	unsigned int new = 5;
	unsigned int old = 12;
	struct ast_event_sub *event_sub;

	switch (cmd) {
	case CLI_INIT:
		e->command = "event generate";
		e->usage =
			"Usage: event generate\n"
			"       Generate a test event.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}
	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	if (!(event_sub = ast_event_subscribe(AST_EVENT_ALL, ast_event_process, 
		NULL, AST_EVENT_IE_END))) {
		return CLI_FAILURE;
	}

	if (!(event = ast_event_new(AST_EVENT_MWI, 
			AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
			AST_EVENT_IE_NEWMSGS, AST_EVENT_IE_PLTYPE_UINT, new,
			AST_EVENT_IE_OLDMSGS, AST_EVENT_IE_PLTYPE_UINT, old,
			AST_EVENT_IE_END))) {
		return CLI_FAILURE;
	}

	ast_event_queue_and_cache(event,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR,
		AST_EVENT_IE_END);

	/* XXX This is a hack.  I should use a timed thread condition instead. */
	usleep(1000000);

	ast_event_unsubscribe(event_sub);

	return CLI_SUCCESS;
}

static char *event_get_cached(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_event *event;
	const char *mailbox = "1234@fakecontext";

	switch (cmd) {
	case CLI_INIT:
		e->command = "event get cached";
		e->usage =
			"Usage: event get cached\n"
			"       Test getting an event from the cache.\n";
		return NULL;

	case CLI_GENERATE:
		return NULL;	/* no completion */
	}
	if (a->argc != e->args)
		return CLI_SHOWUSAGE;

	event = ast_event_get_cached(AST_EVENT_MWI,
		AST_EVENT_IE_MAILBOX, AST_EVENT_IE_PLTYPE_STR, mailbox,
		AST_EVENT_IE_END);

	if (!event) {
		ast_cli(a->fd, "No event retrieved!\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Got the event.  New: %u  Old: %u\n",
		ast_event_get_ie_uint(event, AST_EVENT_IE_NEWMSGS),
		ast_event_get_ie_uint(event, AST_EVENT_IE_OLDMSGS));

	ast_event_destroy(event);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	NEW_CLI(event_gen, "Generate a test event"),
	NEW_CLI(event_get_cached, "Get an event from the cache"),
};

static int load_module(void)
{
	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));

	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Test code for the internal event system");
