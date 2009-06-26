/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Tilghman Lesher <tlesher AT digium DOT com>
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
 * \brief Substitution Test
 *
 * \author\verbatim Tilghman Lesher <tlesher AT digium DOT com> \endverbatim
 * 
 * \ingroup tests
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/strings.h"
#include "asterisk/stringfields.h"
#include "asterisk/threadstorage.h"
#include "asterisk/cli.h"

AST_THREADSTORAGE(buf_buf);
AST_THREADSTORAGE(var_buf);

static void test_chan_integer(int fd, struct ast_channel *c, int *ifield, const char *expression)
{
	int i, okay = 1, value1 = -1, value2 = -1;
	char workspace[4096];
	struct ast_str *str = ast_str_thread_get(&buf_buf, 16);

	for (i = 0; i < 256; i++) {
		*ifield = i;
		ast_str_substitute_variables(&str, 0, c, expression);
		pbx_substitute_variables_helper(c, expression, workspace, sizeof(workspace));
		if (sscanf(workspace, "%d", &value1) != 1 || value1 != i || sscanf(ast_str_buffer(str), "%d", &value2) != 1 || value2 != i) {
			ast_cli(fd, "%s != %s and/or %d != %d != %d\n", ast_str_buffer(str), workspace, value1, value2, i);
			okay = 0;
			break;
		}
	}
	ast_cli(fd, "Testing '%s' . . . . . %s\n", expression, okay ? "passed" : "FAILED");
}

static void test_chan_string(int fd, struct ast_channel *c, char *cfield, size_t cfieldsize, const char *expression)
{
	const char *values[] = { "one", "three", "reallylongdinosaursoundingthingwithwordsinit" };
	int i, okay = 1;
	char workspace[4096];
	struct ast_str *str = ast_str_thread_get(&buf_buf, 16);

	for (i = 0; i < ARRAY_LEN(values); i++) {
		ast_copy_string(cfield, values[i], cfieldsize);
		ast_str_substitute_variables(&str, 0, c, expression);
		pbx_substitute_variables_helper(c, expression, workspace, sizeof(workspace));
		if (strcmp(cfield, ast_str_buffer(str)) != 0 || strcmp(cfield, workspace) != 0) {
			ast_cli(fd, "%s != %s != %s\n", cfield, ast_str_buffer(str), workspace);
			okay = 0;
			break;
		}
	}
	ast_cli(fd, "Testing '%s' . . . . . %s\n", expression, okay ? "passed" : "FAILED");
}

static void test_chan_variable(int fd, struct ast_channel *c, const char *varname)
{
	const char *values[] = { "one", "three", "reallylongdinosaursoundingthingwithwordsinit" };
	int i, okay = 1;
	char workspace[4096];
	struct ast_str *str = ast_str_thread_get(&buf_buf, 16);
	struct ast_str *var = ast_str_thread_get(&var_buf, 16);

	ast_str_set(&var, 0, "${%s}", varname);
	for (i = 0; i < ARRAY_LEN(values); i++) {
		pbx_builtin_setvar_helper(c, varname, values[i]);
		ast_str_substitute_variables(&str, 0, c, ast_str_buffer(var));
		pbx_substitute_variables_helper(c, ast_str_buffer(var), workspace, sizeof(workspace));
		if (strcmp(values[i], ast_str_buffer(str)) != 0 || strcmp(values[i], workspace) != 0) {
			ast_cli(fd, "%s != %s != %s\n", values[i], ast_str_buffer(str), workspace);
			okay = 0;
			break;
		}
	}
	ast_cli(fd, "Testing '%s' . . . . . %s\n", ast_str_buffer(var), okay ? "passed" : "FAILED");
}

static void test_chan_function(int fd, struct ast_channel *c, const char *expression)
{
	int okay = 1;
	char workspace[4096];
	struct ast_str *str = ast_str_thread_get(&buf_buf, 16);

	ast_str_substitute_variables(&str, 0, c, expression);
	pbx_substitute_variables_helper(c, expression, workspace, sizeof(workspace));
	if (strcmp(workspace, ast_str_buffer(str)) != 0) {
		ast_cli(fd, "%s != %s\n", ast_str_buffer(str), workspace);
		okay = 0;
	}
	ast_cli(fd, "Testing '%s' . . . . . %s\n", expression, okay ? "passed" : "FAILED");
}

static void test_2way_function(int fd, struct ast_channel *c, const char *encode1, const char *encode2, const char *decode1, const char *decode2)
{
	struct ast_str *str = ast_str_thread_get(&buf_buf, 16), *expression = ast_str_alloca(120);

	ast_str_set(&expression, 0, "%s%s%s", encode1, "foobarbaz", encode2);
	ast_str_substitute_variables(&str, 0, c, ast_str_buffer(expression));
	ast_str_set(&expression, 0, "%s%s%s", decode1, ast_str_buffer(str), decode2);
	ast_str_substitute_variables(&str, 0, c, ast_str_buffer(expression));
	ast_cli(fd, "Testing '%s%s' and '%s%s' . . . . . %s\n", encode1, encode2, decode1, decode2, !strcmp(ast_str_buffer(str), "foobarbaz") ? "passed" : "FAILED");
	if (strcmp(ast_str_buffer(str), "foobarbaz")) {
		ast_cli(fd, "  '%s' != 'foobarbaz'\n", ast_str_buffer(str));
	}
}

static void test_expected_result(int fd, struct ast_channel *c, const char *expression, const char *result)
{
	struct ast_str *str = ast_str_thread_get(&buf_buf, 16);
	ast_str_substitute_variables(&str, 0, c, expression);
	ast_cli(fd, "Testing '%s' ('%s') == '%s' . . . . . %s\n", ast_str_buffer(str), expression, result, !strcmp(ast_str_buffer(str), result) ? "passed" : "FAILED");
}

static char *handle_cli_test_substitution(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *c;
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "test substitution";
		e->usage = ""
			"Usage: test substitution\n"
			"   Test variable and function substitution.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "Testing variable substitution ...\n");
	c = ast_dummy_channel_alloc();

	test_chan_integer(a->fd, c, &c->cid.cid_pres, "${CALLINGPRES}");
	test_chan_integer(a->fd, c, &c->cid.cid_ani2, "${CALLINGANI2}");
	test_chan_integer(a->fd, c, &c->cid.cid_ton, "${CALLINGTON}");
	test_chan_integer(a->fd, c, &c->cid.cid_tns, "${CALLINGTNS}");
	test_chan_integer(a->fd, c, &c->hangupcause, "${HANGUPCAUSE}");
	test_chan_integer(a->fd, c, &c->priority, "${PRIORITY}");
	test_chan_string(a->fd, c, c->context, sizeof(c->context), "${CONTEXT}");
	test_chan_string(a->fd, c, c->exten, sizeof(c->exten), "${EXTEN}");
	test_chan_variable(a->fd, c, "CHANNEL(language)");
	test_chan_variable(a->fd, c, "CHANNEL(musicclass)");
	test_chan_variable(a->fd, c, "CHANNEL(parkinglot)");
	test_chan_variable(a->fd, c, "CALLERID(name)");
	test_chan_variable(a->fd, c, "CURLOPT(proxyuserpwd)");
	test_chan_variable(a->fd, c, "CDR(foo)");
	test_chan_variable(a->fd, c, "ENV(foo)");
	test_chan_variable(a->fd, c, "GLOBAL(foo)");
	test_chan_variable(a->fd, c, "GROUP()");
	test_2way_function(a->fd, c, "${AES_ENCRYPT(abcdefghijklmnop,", ")}", "${AES_DECRYPT(abcdefghijklmnop,", ")}");
	test_2way_function(a->fd, c, "${BASE64_ENCODE(", ")}", "${BASE64_DECODE(", ")}");
	pbx_builtin_setvar_helper(c, "foo", "123");
	pbx_builtin_setvar_helper(c, "bar", "foo");
	pbx_builtin_setvar_helper(c, "baz", "fo");
	test_expected_result(a->fd, c, "${foo}${foo}", "123123");
	test_expected_result(a->fd, c, "A${foo}A${foo}A", "A123A123A");
	test_expected_result(a->fd, c, "A${${bar}}A", "A123A");
	test_expected_result(a->fd, c, "A${${baz}o}A", "A123A");
	test_expected_result(a->fd, c, "A${${baz}o:1}A", "A23A");
	test_expected_result(a->fd, c, "A${${baz}o:1:1}A", "A2A");
	test_expected_result(a->fd, c, "A${${baz}o:1:-1}A", "A2A");
	test_expected_result(a->fd, c, "A${${baz}o:-1:1}A", "A3A");
	test_expected_result(a->fd, c, "A${${baz}o:-2:1}A", "A2A");
	test_expected_result(a->fd, c, "A${${baz}o:-2:-1}A", "A2A");

	/* For testing dialplan functions */
	for (i = 0; ; i++) {
		char *cmd = ast_cli_generator("core show function", "", i);
		if (cmd == NULL) {
			break;
		}
		if (strcmp(cmd, "CHANNEL") && strcmp(cmd, "CALLERID") && strcmp(cmd, "CURLOPT") && strncmp(cmd, "AES", 3) && strncmp(cmd, "BASE64", 6) && strcmp(cmd, "CDR") && strcmp(cmd, "ENV") && strcmp(cmd, "GLOBAL") && strcmp(cmd, "GROUP") && strcmp(cmd, "CUT") && strcmp(cmd, "LISTFILTER") && strcmp(cmd, "PP_EACH_EXTENSION") && strcmp(cmd, "SET")) {
			struct ast_custom_function *acf = ast_custom_function_find(cmd);
			if (acf->read && acf->read2) {
				char expression[80];
				snprintf(expression, sizeof(expression), "${%s(foo)}", cmd);
				test_chan_function(a->fd, c, expression);
			}
		}
		ast_free(cmd);
	}

	ast_channel_release(c);

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_substitution[] = {
	AST_CLI_DEFINE(handle_cli_test_substitution, "Test variable substitution"),
};

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_substitution, ARRAY_LEN(cli_substitution));
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_substitution, ARRAY_LEN(cli_substitution));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Substitution tests");
