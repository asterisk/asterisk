/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Naveen Albert <asterisk@phreaknet.org>
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
 * \brief Returns files played by Say applications
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/pbx.h"
#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/say.h"
#include "asterisk/lock.h"
#include "asterisk/localtime.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/conversions.h"

/*** DOCUMENTATION
	<function name="SAYFILES" language="en_US">
		<since>
			<version>16.21.0</version>
			<version>18.7.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Returns the ampersand-delimited file names that would be played by the Say applications (e.g. SayAlpha, SayDigits).
		</synopsis>
		<syntax>
			<parameter name="value" required="true">
				<para>The value to be translated to filenames.</para>
			</parameter>
			<parameter name="type">
				<para>Say application type.</para>
				<enumlist>
					<enum name="alpha">
						<para>Files played by SayAlpha(). Default if none is specified.</para>
					</enum>
					<enum name="digits">
						<para>Files played by SayDigits().</para>
					</enum>
					<enum name="money">
						<para>Files played by SayMoney(). Currently supported for English and US dollars only.</para>
					</enum>
					<enum name="number">
						<para>Files played by SayNumber(). Currently supported for English only.</para>
					</enum>
					<enum name="ordinal">
						<para>Files played by SayOrdinal(). Currently supported for English only.</para>
					</enum>
					<enum name="phonetic">
						<para>Files played by SayPhonetic().</para>
					</enum>
				</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>Returns the files that would be played by a Say application. These filenames could then be
			passed directly into Playback, BackGround, Read, Queue, or any application which supports
			playback of multiple ampersand-delimited files.</para>
			<example title="Read using the number 123">
			 same => n,Read(response,${SAYFILES(123,number)})
			</example>
		</description>
		<see-also>
			<ref type="application">SayAlpha</ref>
			<ref type="application">SayDigits</ref>
			<ref type="application">SayMoney</ref>
			<ref type="application">SayNumber</ref>
			<ref type="application">SayOrdinal</ref>
			<ref type="application">SayPhonetic</ref>
		</see-also>
	</function>
 ***/
static int sayfile_exec(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *value, *type, *files;
	const char *lang;
	struct ast_str *filenames = NULL;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(value);
		AST_APP_ARG(type);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "SAYFILES requires an argument\n");
		return 0;
	}

	AST_STANDARD_APP_ARGS(args, data);

	value = args.value;
	type = (ast_strlen_zero(args.type) ? "alpha" : args.type);
	lang = (chan ? ast_channel_language(chan) : "en"); /* No chan for unit tests */

	if (!strcmp(type, "alpha")) {
		filenames = ast_get_character_str(value, lang, AST_SAY_CASE_NONE);
	} else if (!strcmp(type, "phonetic")) {
		filenames = ast_get_phonetic_str(value, lang);
	} else if (!strcmp(type, "digits")) {
		filenames = ast_get_digit_str(value, lang);
	} else if (!strcmp(type, "number")) {
		int num;
		if (ast_str_to_int(value, &num)) {
			ast_log(LOG_WARNING, "Invalid numeric argument: %s\n", value);
		} else {
			filenames = ast_get_number_str(num, lang);
		}
	} else if (!strcmp(type, "ordinal")) {
		int num;
		if (ast_str_to_int(value, &num)) {
			ast_log(LOG_WARNING, "Invalid numeric argument: %s\n", value);
		} else {
			filenames = ast_get_ordinal_str(num, lang);
		}
	} else if (!strcmp(type, "money")) {
		filenames = ast_get_money_str(value, lang);
	} else {
		ast_log(LOG_WARNING, "Invalid say type specified: %s\n", type);
	}

	if (!filenames) {
		return -1;
	}

	files = ast_str_buffer(filenames);
	snprintf(buf, len, "%s", files);
	ast_free(filenames);

	return 0;
}

static struct ast_custom_function sayfiles = {
	.name = "SAYFILES",
	.read = sayfile_exec,
};

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_SAYFILES_function)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_str *expr, *result;

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_SAYFILES_function";
		info->category = "/funcs/func_sayfiles/";
		info->summary = "Test SAYFILES function substitution";
		info->description =
			"Executes a series of variable substitutions using the SAYFILES function and ensures that the expected results are received.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing SAYFILES() substitution ...\n");

	if (!(expr = ast_str_create(16))) {
		return AST_TEST_FAIL;
	}
	if (!(result = ast_str_create(16))) {
		ast_free(expr);
		return AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(hi Th3re,alpha)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "letters/h&letters/i&letters/space&letters/t&letters/h&digits/3&letters/r&letters/e") != 0) {
		ast_test_status_update(test, "SAYFILES(hi Th3re,alpha) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(phreak,phonetic)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "phonetic/p_p&phonetic/h_p&phonetic/r_p&phonetic/e_p&phonetic/a_p&phonetic/k_p") != 0) {
		ast_test_status_update(test, "SAYFILES(phreak,phonetic) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(35,digits)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/3&digits/5") != 0) {
		ast_test_status_update(test, "SAYFILES(35,digits) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(35,number)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/30&digits/5") != 0) {
		ast_test_status_update(test, "SAYFILES(35,number) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(747,number)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/7&digits/hundred&digits/40&digits/7") != 0) {
		ast_test_status_update(test, "SAYFILES(747,number) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(1042,number)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/1&digits/thousand&digits/40&digits/2") != 0) {
		ast_test_status_update(test, "SAYFILES(1042,number) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(0,number)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/0") != 0) {
		ast_test_status_update(test, "SAYFILES(0,digits) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(2001000001,number)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/2&digits/billion&digits/1&digits/million&digits/1") != 0) {
		ast_test_status_update(test, "SAYFILES(2001000001,number) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(7,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/h-7") != 0) {
		ast_test_status_update(test, "SAYFILES(7,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(35,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/30&digits/h-5") != 0) {
		ast_test_status_update(test, "SAYFILES(35,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(1042,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/1&digits/thousand&digits/40&digits/h-2") != 0) {
		ast_test_status_update(test, "SAYFILES(1042,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(11042,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/11&digits/thousand&digits/40&digits/h-2") != 0) {
		ast_test_status_update(test, "SAYFILES(11042,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(40000,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/40&digits/h-thousand") != 0) {
		ast_test_status_update(test, "SAYFILES(40000,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(43638,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/40&digits/3&digits/thousand&digits/6&digits/hundred&digits/30&digits/h-8") != 0) {
		ast_test_status_update(test, "SAYFILES(43638,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(1000000,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/1&digits/h-million") != 0) {
		ast_test_status_update(test, "SAYFILES(1000000,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(1000001,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/1&digits/million&digits/h-1") != 0) {
		ast_test_status_update(test, "SAYFILES(1000001,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(2001000001,ordinal)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/2&digits/billion&digits/1&digits/million&digits/h-1") != 0) {
		ast_test_status_update(test, "SAYFILES(2001000001,ordinal) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(0,money)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/0&cents") != 0) {
		ast_test_status_update(test, "SAYFILES(0,money) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(0.01,money)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/1&cent") != 0) {
		ast_test_status_update(test, "SAYFILES(0.01,money) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(0.42,money)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/40&digits/2&cents") != 0) {
		ast_test_status_update(test, "SAYFILES(0.42,money) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(1.00,money)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/1&letters/dollar") != 0) {
		ast_test_status_update(test, "SAYFILES(1.00,money) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(1.42,money)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/1&letters/dollar_&and&digits/40&digits/2&cents") != 0) {
		ast_test_status_update(test, "SAYFILES(1.42,money) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(2.00,money)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/2&dollars") != 0) {
		ast_test_status_update(test, "SAYFILES(2.00,money) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${SAYFILES(2.42,money)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "digits/2&dollars&and&digits/40&digits/2&cents") != 0) {
		ast_test_status_update(test, "SAYFILES(2.42,money) test failed ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_free(expr);
	ast_free(result);

	return res;
}
#endif

static int unload_module(void)
{
	AST_TEST_UNREGISTER(test_SAYFILES_function);
	return ast_custom_function_unregister(&sayfiles);
}

static int load_module(void)
{
	AST_TEST_REGISTER(test_SAYFILES_function);
	return ast_custom_function_register(&sayfiles);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Say application files");
