/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Philip Prindeville
 *
 * Philip Prindeville <philipp@redfish-solutions.com>
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
 * \brief Make basic use of capture capability in test framework.
 *
 * \author\verbatim Philip Prindeville <philipp@redfish-solutions.com> \endverbatim
 *
 * Exercise the capture capabilities built into the test framework so
 * that external commands might be used to generate validating results
 * used on corroborating tests.
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/test.h"

AST_TEST_DEFINE(test_capture_true)
{
	int status = AST_TEST_FAIL;
	struct ast_test_capture cap;
	const char *command = "true";
	char *const args[] = { "true", NULL };

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_capture_true";
		info->category = "/main/test_capture/";
		info->summary = "capture true exit unit test";
		info->description =
			"Capture exit code from true command.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing true exit test...\n");

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		return status;
	}

	if (ast_test_capture_command(&cap, command, args, NULL, 0) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		return status;
	}

	if (cap.outlen != 0) {
		ast_test_status_update(test, "unexpected value for stdout\n");
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "unexpected value for stderr\n");
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	status = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);

	return status;
}

AST_TEST_DEFINE(test_capture_false)
{
	int status = AST_TEST_FAIL;
	struct ast_test_capture cap;
	const char *command = "false";
	char *const args[] = { "false", NULL };

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_capture_false";
		info->category = "/main/test_capture/";
		info->summary = "capture false exit unit test";
		info->description =
			"Capture exit code from false command.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing false exit test...\n");

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		return status;
	}

	if (ast_test_capture_command(&cap, command, args, NULL, 0) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		return status;
	}

	if (cap.outlen != 0) {
		ast_test_status_update(test, "unexpected value for stdout\n");
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "unexpected value for stderr\n");
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 1) {
		ast_test_status_update(test, "child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	status = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);

	return status;
}

AST_TEST_DEFINE(test_capture_with_stdin)
{
	int status = AST_TEST_FAIL;
	struct ast_test_capture cap;
	const char *command = "base64";
	char *const args[] = { "base64", NULL };
	const char data[] = "Mary had a little lamb.";
	const unsigned datalen = sizeof(data) - 1;
	const char output[] = "TWFyeSBoYWQgYSBsaXR0bGUgbGFtYi4=\n";
	const unsigned outputlen = sizeof(output) - 1;

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_capture_with_stdin";
		info->category = "/main/test_capture/";
		info->summary = "capture with stdin unit test";
		info->description =
			"Capture output from stdin transformation command.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing stdin test...\n");

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		return status;
	}

	if (ast_test_capture_command(&cap, command, args, data, datalen) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		return status;
	}

	if (cap.outlen != outputlen || memcmp(cap.outbuf, output, cap.outlen)) {
		ast_test_status_update(test, "unexpected value for stdout\n");
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "unexpected value for stderr\n");
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	status = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);

	return status;
}

AST_TEST_DEFINE(test_capture_with_dynamic)
{
	int status = AST_TEST_FAIL;
	struct ast_test_capture cap;
	const char *command = "date";
	char *args[] = { "date", "DATE", "FORMAT", NULL };
	char date[40];
	const char format[] = "+%a, %d %b %y %T %z";
	const char format2[] = "%a, %d %b %y %T %z\n";
	char myresult[64];
	unsigned myresultlen;
	time_t now;
	struct tm *tm;

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_capture_with_dynamic";
		info->category = "/main/test_capture/";
		info->summary = "capture with dynamic argument unit test";
		info->description =
			"Capture output from dynamic transformation command.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing dynamic argument test...\n");

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		return status;
	}

	time(&now);
	snprintf(date, sizeof(date), "--date=@%lu", now);

	tm = localtime(&now);
	strftime(myresult, sizeof(myresult), format2, tm);
	myresultlen = strlen(myresult);

	args[1] = date;
	args[2] = (char *)format;

	if (ast_test_capture_command(&cap, command, args, NULL, 0) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		return status;
	}

	if (cap.outlen != myresultlen || memcmp(cap.outbuf, myresult, cap.outlen)) {
		ast_test_status_update(test, "unexpected value for stdout\n");
		goto cleanup;
	}

	if (cap.errlen != 0) {
		ast_test_status_update(test, "unexpected value for stderr\n");
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	status = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);

	return status;
}

AST_TEST_DEFINE(test_capture_stdout_stderr)
{
	int status = AST_TEST_FAIL;
	struct ast_test_capture cap;
	const char *command = "sh";
	char *const args[] = { "sh", "-c", "echo -n 'foo' >&2 ; echo -n 'zzz' >&1 ; echo -n 'bar' >&2", NULL };

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_capture_stdout_stderr";
		info->category = "/main/test_capture/";
		info->summary = "capture stdout & stderr unit test";
		info->description =
			"Capture both stdout and stderr from shell.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Executing stdout/stderr test...\n");

	if (!ast_check_command_in_path(command)) {
		ast_test_status_update(test, "couldn't find %s\n", command);
		return status;
	}

	if (ast_test_capture_command(&cap, command, args, NULL, 0) != 1) {
		ast_test_status_update(test, "ast_test_capture_command() failed\n");
		return status;
	}

	if (cap.outlen != 3 || memcmp(cap.outbuf, "zzz", 3)) {
		ast_test_status_update(test, "unexpected value for stdout\n");
		goto cleanup;
	}

	if (cap.errlen != 6 || memcmp(cap.errbuf, "foobar", 6)) {
		ast_test_status_update(test, "unexpected value for stderr\n");
		goto cleanup;
	}

	if (cap.pid == -1) {
		ast_test_status_update(test, "invalid process id\n");
		goto cleanup;
	}

	if (cap.exitcode != 0) {
		ast_test_status_update(test, "child exited %d\n", cap.exitcode);
		goto cleanup;
	}

	status = AST_TEST_PASS;

cleanup:
	ast_test_capture_free(&cap);

	return status;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_capture_with_stdin);
	AST_TEST_UNREGISTER(test_capture_with_dynamic);
	AST_TEST_UNREGISTER(test_capture_stdout_stderr);
	AST_TEST_UNREGISTER(test_capture_true);
	AST_TEST_UNREGISTER(test_capture_false);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_capture_with_stdin);
	AST_TEST_REGISTER(test_capture_with_dynamic);
	AST_TEST_REGISTER(test_capture_stdout_stderr);
	AST_TEST_REGISTER(test_capture_true);
	AST_TEST_REGISTER(test_capture_false);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Capture support test");

