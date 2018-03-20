/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * David Brooks <dbrooks@digium.com>
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
 * \brief Test AMI hook
 *
 * \author David Brooks <dbrooks@digium.com> based off of code written by Russell Bryant <russell@digium.com>
 *
 * This started, and continues to serves, as an example illustrating the ability
 * for a custom module to hook into AMI. Registration for AMI events and sending
 * of AMI actions is shown. A test has also been created that utilizes the original
 * example in order to make sure the ami event hook gets raised.
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/manager.h"
#include "asterisk/test.h"

#define CATEGORY "/main/amihooks/"

AST_MUTEX_DEFINE_STATIC(hook_lock);
ast_cond_t hook_cond;
int done;

static int wait_for_hook(struct ast_test *test)
{
	struct timeval start = ast_tvnow();
	struct timespec timeout = {
		.tv_sec = start.tv_sec + 2,
		.tv_nsec = start.tv_usec * 1000
	};
	int res = 0;

	ast_mutex_lock(&hook_lock);
	while (!done) {
		if (ast_cond_timedwait(&hook_cond, &hook_lock, &timeout) == ETIMEDOUT) {
			ast_test_status_update(test, "Test timed out while waiting for hook event\n");
			res = -1;
			break;
		}
	}
	ast_mutex_unlock(&hook_lock);

	return res;
}

AST_TEST_DEFINE(amihook_cli_send)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->category = CATEGORY;
		info->summary = "Execute an action using an AMI hook";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	done = 0;
	if (ast_cli_command(-1, "amihook send")) {
		return AST_TEST_FAIL;
	}

	return wait_for_hook(test) ? AST_TEST_FAIL : AST_TEST_PASS;
}

/* The helper function is required by struct manager_custom_hook.
 * See __ast_manager_event_multichan for details */
static int amihook_helper(int category, const char *event, char *content)
{
	ast_log(LOG_NOTICE, "AMI Event: \nCategory: %d Event: %s\n%s\n", category, event, content);

	ast_mutex_lock(&hook_lock);
	done = 1;
	ast_cond_signal(&hook_cond);
	ast_mutex_unlock(&hook_lock);
	return 0;
}

static struct manager_custom_hook test_hook = {
	.file = __FILE__,
	.helper = &amihook_helper,
};

static int hook_send(void) {
	int res;

	/* Send a test action (core show version) to the AMI */
	res = ast_hook_send_action(&test_hook, "Action: Command\nCommand: core show version\nActionID: 987654321\n");

	return res;
}

static void register_hook(void) {

	/* Unregister the hook, we don't want a double-registration (Bad Things(tm) happen) */
	ast_manager_unregister_hook(&test_hook);

	/* Register the hook for AMI events */
	ast_manager_register_hook(&test_hook);

}

static void unregister_hook(void) {

	/* Unregister the hook */
	ast_manager_unregister_hook(&test_hook);

}

static char *handle_cli_amihook_send(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "amihook send";
		e->usage = ""
			"Usage: amihook send"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	case CLI_HANDLER:
		hook_send();
		return CLI_SUCCESS;
	}

	return CLI_FAILURE;
}

static char *handle_cli_amihook_register_hook(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "amihook register";
		e->usage = ""
			"Usage: amihook register"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	case CLI_HANDLER:
		register_hook();
		return CLI_SUCCESS;
	}

	return CLI_FAILURE;
}

static char *handle_cli_amihook_unregister_hook(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "amihook unregister";
		e->usage = ""
			"Usage: amihook unregister"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	case CLI_HANDLER:
		unregister_hook();
		return CLI_SUCCESS;
	}

	return CLI_FAILURE;
}

static struct ast_cli_entry cli_amihook_evt[] = {
	AST_CLI_DEFINE(handle_cli_amihook_send, "Send an AMI event"),
	AST_CLI_DEFINE(handle_cli_amihook_register_hook, "Register module for AMI hook"),
	AST_CLI_DEFINE(handle_cli_amihook_unregister_hook, "Unregister module for AMI hook"),
};

static int unload_module(void)
{
	AST_TEST_UNREGISTER(amihook_cli_send);
	ast_manager_unregister_hook(&test_hook);
	return ast_cli_unregister_multiple(cli_amihook_evt, ARRAY_LEN(cli_amihook_evt));
}

static int load_module(void)
{
	int res;

	res = ast_cli_register_multiple(cli_amihook_evt, ARRAY_LEN(cli_amihook_evt));

	AST_TEST_REGISTER(amihook_cli_send);

	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "AMI Hook Test Module");
