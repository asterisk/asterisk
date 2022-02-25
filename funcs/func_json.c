/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Naveen Albert
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
 * \brief JSON parsing function
 *
 * \author Naveen Albert <asterisk@phreaknet.org>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/test.h"
#include "asterisk/app.h"
#include "asterisk/conversions.h"

/*** DOCUMENTATION
	<function name="JSON_DECODE" language="en_US">
		<since>
			<version>16.24.0</version>
			<version>18.10.0</version>
			<version>19.2.0</version>
		</since>
		<synopsis>
			Returns the string value of a JSON object key from a string containing a
			JSON array.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true">
				<para>The name of the variable containing the JSON string to parse.</para>
			</parameter>
			<parameter name="item" required="true">
				<para>The name of the key whose value to return.</para>
			</parameter>
		</syntax>
		<description>
			<para>The JSON_DECODE function retrieves the value of the given variable name
			and parses it as JSON, returning the value at a specified key. If the key cannot
			be found, an empty string is returned.</para>
		</description>
		<see-also>
			<ref type="function">CURL</ref>
		</see-also>
	</function>
 ***/

AST_THREADSTORAGE(result_buf);

static int json_decode_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_json *json, *jsonval;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(varname);
		AST_APP_ARG(key);
	);
	char *varsubst, *result2;
	const char *result = NULL;
	struct ast_str *str = ast_str_thread_get(&result_buf, 16);

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.varname)) {
		ast_log(LOG_WARNING, "%s requires a variable name\n", cmd);
		return -1;
	}
	if (ast_strlen_zero(args.key)) {
		ast_log(LOG_WARNING, "%s requires a key\n", cmd);
		return -1;
	}

	varsubst = ast_alloca(strlen(args.varname) + 4); /* +4 for ${} and null terminator */
	if (!varsubst) {
		ast_log(LOG_ERROR, "Failed to allocate string\n");
		return -1;
	}
	sprintf(varsubst, "${%s}", args.varname); /* safe, because of the above allocation */
	ast_str_substitute_variables(&str, 0, chan, varsubst);
	if (ast_str_strlen(str) == 0) {
		ast_debug(1, "Variable '%s' contains no data, nothing to search!\n", args.varname);
		return -1; /* empty json string */
	}

	ast_debug(1, "Parsing JSON: %s\n", ast_str_buffer(str));

	json = ast_json_load_str(str, NULL);

	if (!json) {
		ast_log(LOG_WARNING, "Failed to parse as JSON: %s\n", ast_str_buffer(str));
		return -1;
	}

	jsonval = ast_json_object_get(json, args.key);
	if (!jsonval) { /* no error or warning should be thrown */
		ast_debug(1, "Could not find key '%s' in parsed JSON\n", args.key);
		ast_json_free(json);
		return -1;
	}
	switch(ast_json_typeof(jsonval)) {
		int r;
		case AST_JSON_STRING:
			result = ast_json_string_get(jsonval);
			snprintf(buf, len, "%s", result);
			break;
		case AST_JSON_INTEGER:
			r = ast_json_integer_get(jsonval);
			snprintf(buf, len, "%d", r); /* the snprintf below is mutually exclusive with this one */
			break;
		default:
			result2 = ast_json_dump_string(jsonval);
			snprintf(buf, len, "%s", result2);
			ast_json_free(result2);
			break;
	}
	ast_json_free(json);

	return 0;
}

static struct ast_custom_function json_decode_function = {
	.name = "JSON_DECODE",
	.read = json_decode_read,
};

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_JSON_DECODE)
{
	int i, res = AST_TEST_PASS;
	struct ast_channel *chan; /* dummy channel */
	struct ast_str *str; /* fancy string for holding comparing value */

	const char *test_strings[][5] = {
		{"{\"city\": \"Anytown\", \"state\": \"USA\"}", "city", "Anytown"},
		{"{\"city\": \"Anytown\", \"state\": \"USA\"}", "state", "USA"},
		{"{\"city\": \"Anytown\", \"state\": \"USA\"}", "blah", ""},
		{"{\"key1\": \"123\", \"key2\": \"456\"}", "key1", "123"},
		{"{\"key1\": 123, \"key2\": 456}", "key1", "123"},
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "func_JSON_DECODE";
		info->category = "/funcs/func_json/";
		info->summary = "Test JSON_DECODE function";
		info->description = "Verify JSON_DECODE behavior";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(chan = ast_dummy_channel_alloc())) {
		ast_test_status_update(test, "Unable to allocate dummy channel\n");
		return AST_TEST_FAIL;
	}

	if (!(str = ast_str_create(64))) {
		ast_test_status_update(test, "Unable to allocate dynamic string buffer\n");
		ast_channel_release(chan);
		return AST_TEST_FAIL;
	}

	for (i = 0; i < ARRAY_LEN(test_strings); i++) {
		char tmp[512], tmp2[512] = "";

		struct ast_var_t *var = ast_var_assign("test_string", test_strings[i][0]);
		if (!var) {
			ast_test_status_update(test, "Unable to allocate variable\n");
			ast_free(str);
			ast_channel_release(chan);
			return AST_TEST_FAIL;
		}

		AST_LIST_INSERT_HEAD(ast_channel_varshead(chan), var, entries);

		snprintf(tmp, sizeof(tmp), "${JSON_DECODE(%s,%s)}", "test_string", test_strings[i][1]);

		ast_str_substitute_variables(&str, 0, chan, tmp);
		if (strcmp(test_strings[i][2], ast_str_buffer(str))) {
			ast_test_status_update(test, "Format string '%s' substituted to '%s'.  Expected '%s'.\n", test_strings[i][0], tmp2, test_strings[i][2]);
			res = AST_TEST_FAIL;
		}
	}

	ast_free(str);
	ast_channel_release(chan);

	return res;
}
#endif

static int unload_module(void)
{
	int res;

	AST_TEST_UNREGISTER(test_JSON_DECODE);
	res = ast_custom_function_unregister(&json_decode_function);

	return res;
}

static int load_module(void)
{
	int res;

	AST_TEST_REGISTER(test_JSON_DECODE);
	res = ast_custom_function_register(&json_decode_function);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "JSON decoding function");
