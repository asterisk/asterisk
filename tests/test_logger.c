/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 * \brief Test module for the logging subsystem
 *
 * \author\verbatim Kevin P. Fleming <kpfleming@digium.com> \endverbatim
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"

struct test {
	const char *name;
	unsigned int x_success;
	unsigned int x_failure;
	unsigned int u_success;
	unsigned int u_failure;
};

static void output_tests(struct test *tests, size_t num_tests, int fd)
{
	unsigned int x;

	for (x = 0; x < num_tests; x++) {
		ast_cli(fd, "Test %u: %s\n", x + 1, tests[x].name);
		ast_cli(fd, "\tExpected Successes: %u\n", tests[x].x_success);
		ast_cli(fd, "\tExpected Failures: %u\n", tests[x].x_failure);
		ast_cli(fd, "\tUnexpected Successes: %u\n", tests[x].u_success);
		ast_cli(fd, "\tUnexpected Failures: %u\n", tests[x].u_failure);
		ast_cli(fd, "Test %u Result: %s\n", x + 1, (tests[x].u_success + tests[x].u_failure) ? "FAIL" : "PASS");
	}
}

static char *handle_cli_dynamic_level_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	unsigned int level;
	unsigned int x;
	unsigned int test;
	struct test tests[] = {
		{ .name = "Simple register/message/unregister",
		},
		{ .name = "Register multiple levels",
		},
	};

	switch (cmd) {
	case CLI_INIT:
		e->command = "logger test dynamic";
		e->usage = ""
			"Usage: logger test dynamic\n"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	for (test = 0; test < ARRAY_LEN(tests); test++) {
		ast_cli(a->fd, "Test %u: %s.\n", test + 1, tests[test].name);
		switch (test) {
		case 0:
			if ((level = ast_logger_register_level("test")) != -1) {
				ast_cli(a->fd, "Test: got level %u\n", level);
				ast_log_dynamic_level(level, "Logger Dynamic Test: Test 1\n");
				ast_logger_unregister_level("test");
				tests[test].x_success++;
			} else {
				ast_cli(a->fd, "Test: Failed, could not register level 'test'.\n");
				tests[test].u_failure++;
			}
			break;
		case 1:
		{
			char level_name[18][8];

			for (x = 0; x < ARRAY_LEN(level_name); x++) {
				sprintf(level_name[x], "level%02u", x);
				if ((level = ast_logger_register_level(level_name[x])) == -1) {
					if (x < 16) {
						tests[test].u_failure++;
					} else {
						tests[test].x_failure++;
					}
					level_name[x][0] = '\0';
				} else {
					ast_cli(a->fd, "Test: registered '%s', got level %u\n", level_name[x], level);
					if (x < 16) {
						tests[test].x_success++;
					} else {
						tests[test].u_success++;
					}
				}
			}

			for (x = 0; x < ARRAY_LEN(level_name); x++) {
				if (!ast_strlen_zero(level_name[x])) {
					ast_logger_unregister_level(level_name[x]);
				}
			}
		}
		}
	}

	output_tests(tests, ARRAY_LEN(tests), a->fd);

	return CLI_SUCCESS;
}

static char *handle_cli_performance_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	unsigned int level;
	unsigned int test;
	struct test tests[] = {
		{ .name = "Log 10,000 messages",
		},
	};

	switch (cmd) {
	case CLI_INIT:
		e->command = "logger test performance";
		e->usage = ""
			"Usage: logger test performance\n"
			"";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	for (test = 0; test < ARRAY_LEN(tests); test++) {
		ast_cli(a->fd, "Test %u: %s.\n", test + 1, tests[test].name);
		switch (test) {
		case 0:
			if ((level = ast_logger_register_level("perftest")) != -1) {
				unsigned int x;
				struct timeval start, end;
				int elapsed;

				ast_cli(a->fd, "Test: got level %u\n", level);
				start = ast_tvnow();
				for (x = 0; x < 10000; x++) {
					ast_log_dynamic_level(level, "Performance test log message\n");
				}
				end = ast_tvnow();
				elapsed = ast_tvdiff_ms(end, start);
				ast_cli(a->fd, "Test: 10,000 messages in %f seconds.\n", (float) elapsed / 1000);
				ast_logger_unregister_level("perftest");
				tests[test].x_success++;
			} else {
				ast_cli(a->fd, "Test: Failed, could not register level 'perftest'.\n");
				tests[test].u_failure++;
			}
			break;
		}
	}

	output_tests(tests, ARRAY_LEN(tests), a->fd);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_logger[] = {
	AST_CLI_DEFINE(handle_cli_dynamic_level_test, "Test the dynamic logger level implementation"),
	AST_CLI_DEFINE(handle_cli_performance_test, "Test the logger performance"),
};

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_logger, ARRAY_LEN(cli_logger));
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_logger, ARRAY_LEN(cli_logger));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Logger Test Module");
