/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Channel unit tests
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/channel.h"

AST_TEST_DEFINE(set_fd_grow)
{
	struct ast_channel *mock_channel;
	enum ast_test_result_state res = AST_TEST_PASS;
	int pos;

	switch (cmd) {
	case TEST_INIT:
		info->name = "set_fd_grow";
		info->category = "/main/channel/";
		info->summary = "channel setting file descriptor with growth test";
		info->description =
			"Test that setting a file descriptor on a high position of a channel results in -1 set on any new positions";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	mock_channel = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "TestChannel");
	ast_test_validate_cleanup(test, mock_channel, res, done);

	ast_channel_set_fd(mock_channel, AST_EXTENDED_FDS + 10, 1);
	ast_test_validate_cleanup(test, ast_channel_fd_count(mock_channel) == AST_EXTENDED_FDS + 11, res, done);

	for (pos = AST_EXTENDED_FDS; (pos < AST_EXTENDED_FDS + 10); pos++) {
		ast_test_validate_cleanup(test, ast_channel_fd(mock_channel, pos) == -1, res, done);
	}

done:
	ast_hangup(mock_channel);

	return res;
}

AST_TEST_DEFINE(add_fd)
{
	struct ast_channel *mock_channel;
	enum ast_test_result_state res = AST_TEST_PASS;
	int pos;

	switch (cmd) {
	case TEST_INIT:
		info->name = "add_fd";
		info->category = "/main/channel/";
		info->summary = "channel adding file descriptor test";
		info->description =
			"Test that adding a file descriptor to a channel places it in the expected position";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	mock_channel = ast_channel_alloc(0, AST_STATE_DOWN, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, "TestChannel");
	ast_test_validate_cleanup(test, mock_channel, res, done);

	pos = ast_channel_fd_add(mock_channel, 1);
	ast_test_validate_cleanup(test, pos == AST_EXTENDED_FDS, res, done);

	ast_channel_set_fd(mock_channel, pos, -1);
	ast_test_validate_cleanup(test, ast_channel_fd(mock_channel, pos) == -1, res, done);

done:
	ast_hangup(mock_channel);

	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(set_fd_grow);
	AST_TEST_UNREGISTER(add_fd);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(set_fd_grow);
	AST_TEST_REGISTER(add_fd);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel Unit Tests");
