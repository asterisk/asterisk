/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
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

/*! \file
 *
 * \brief ast_sched performance test module
 *
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

#include <inttypes.h>

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/sched.h"

static int sched_cb(const void *data)
{
	return 0;
}

static char *handle_cli_sched_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sched_context *con;
	char *res = CLI_FAILURE;
	int id1, id2, id3, wait;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sched test";
		e->usage = ""
			"Usage: sched test\n"
			"   Test scheduler entry ordering.\n"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "Testing scheduler entry ordering ...\n");

	if (!(con = sched_context_create())) {
		ast_cli(a->fd, "Test failed - could not create scheduler context\n");
		return CLI_FAILURE;
	}

	/* Add 3 scheduler entries, and then remove them, ensuring that the result
	 * of ast_sched_wait() looks appropriate at each step along the way. */

	if ((wait = ast_sched_wait(con)) != -1) {
		ast_cli(a->fd, "ast_sched_wait() should have returned -1, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if ((id1 = ast_sched_add(con, 100000, sched_cb, NULL)) == -1) {
		ast_cli(a->fd, "Failed to add scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) > 100000) {
		ast_cli(a->fd, "ast_sched_wait() should have returned <= 100000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if ((id2 = ast_sched_add(con, 10000, sched_cb, NULL)) == -1) {
		ast_cli(a->fd, "Failed to add scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) > 10000) {
		ast_cli(a->fd, "ast_sched_wait() should have returned <= 10000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if ((id3 = ast_sched_add(con, 1000, sched_cb, NULL)) == -1) {
		ast_cli(a->fd, "Failed to add scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) > 1000) {
		ast_cli(a->fd, "ast_sched_wait() should have returned <= 1000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if (ast_sched_del(con, id3) == -1) {
		ast_cli(a->fd, "Failed to remove scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) <= 1000) {
		ast_cli(a->fd, "ast_sched_wait() should have returned > 1000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if (ast_sched_del(con, id2) == -1) {
		ast_cli(a->fd, "Failed to remove scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) <= 10000) {
		ast_cli(a->fd, "ast_sched_wait() should have returned > 10000, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	if (ast_sched_del(con, id1) == -1) {
		ast_cli(a->fd, "Failed to remove scheduler entry\n");
		goto return_cleanup;
	}

	if ((wait = ast_sched_wait(con)) != -1) {
		ast_cli(a->fd, "ast_sched_wait() should have returned -1, returned '%d'\n",
				wait);
		goto return_cleanup;
	}

	res = CLI_SUCCESS;

	ast_cli(a->fd, "Test passed!\n");

return_cleanup:
	sched_context_destroy(con);

	return res;
}

static char *handle_cli_sched_bench(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sched_context *con;
	struct timeval start;
	unsigned int num, i;
	int *sched_ids = NULL;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sched benchmark";
		e->usage = ""
			"Usage: sched benchmark <num>\n"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	if (sscanf(a->argv[e->args], "%u", &num) != 1) {
		return CLI_SHOWUSAGE;
	}

	if (!(con = sched_context_create())) {
		ast_cli(a->fd, "Test failed - could not create scheduler context\n");
		return CLI_FAILURE;
	}

	if (!(sched_ids = ast_malloc(sizeof(*sched_ids) * num))) {
		ast_cli(a->fd, "Test failed - memory allocation failure\n");
		goto return_cleanup;
	}

	ast_cli(a->fd, "Testing ast_sched_add() performance - timing how long it takes "
			"to add %u entries at random time intervals from 0 to 60 seconds\n", num);

	start = ast_tvnow();

	for (i = 0; i < num; i++) {
		int when = abs(ast_random()) % 60000;
		if ((sched_ids[i] = ast_sched_add(con, when, sched_cb, NULL)) == -1) {
			ast_cli(a->fd, "Test failed - sched_add returned -1\n");
			goto return_cleanup;
		}
	}

	ast_cli(a->fd, "Test complete - %" PRIi64 " us\n", ast_tvdiff_us(ast_tvnow(), start));

	ast_cli(a->fd, "Testing ast_sched_del() performance - timing how long it takes "
			"to delete %u entries with random time intervals from 0 to 60 seconds\n", num);

	start = ast_tvnow();

	for (i = 0; i < num; i++) {
		if (ast_sched_del(con, sched_ids[i]) == -1) {
			ast_cli(a->fd, "Test failed - sched_del returned -1\n");
			goto return_cleanup;
		}
	}

	ast_cli(a->fd, "Test complete - %" PRIi64 " us\n", ast_tvdiff_us(ast_tvnow(), start));

return_cleanup:
	sched_context_destroy(con);
	if (sched_ids) {
		ast_free(sched_ids);
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_sched[] = {
	AST_CLI_DEFINE(handle_cli_sched_bench, "Benchmark ast_sched add/del performance"),
	AST_CLI_DEFINE(handle_cli_sched_test, "Test scheduler entry ordering"),
};

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_sched, ARRAY_LEN(cli_sched));
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_sched, ARRAY_LEN(cli_sched));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ast_sched performance test module");
