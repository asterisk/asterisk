/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021-2022, Naveen Albert
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
				<para>Multiple keys can be listed separated by a hierarchy delimeter, which will recursively index into a nested JSON string to retrieve a specific subkey's value.</para>
			</parameter>
			<parameter name="separator" required="false">
				<para>A single character that delimits a key hierarchy for nested indexing. Default is a period (.)</para>
				<para>This value should not appear in the key or hierarchy of keys itself, except to delimit the hierarchy of keys.</para>
			</parameter>
			<parameter name="options" required="no">
				<optionlist>
					<option name="c">
						<para>For keys that reference a JSON array, return
						the number of items in the array.</para>
						<para>This option has no effect on any other type
						of value.</para>
					</option>
				</optionlist>
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

enum json_option_flags {
	OPT_COUNT = (1 << 0),
};

AST_APP_OPTIONS(json_options, {
	AST_APP_OPTION('c', OPT_COUNT),
});

#define MAX_JSON_STACK 32

static int parse_node(char **key, char *currentkey, char *nestchar, int count, struct ast_json *json, char *buf, size_t len, int *depth)
{
	const char *result = NULL;
	char *previouskey;
	struct ast_json *jsonval = json;

	/* Prevent a huge JSON string from blowing the stack. */
	(*depth)++;
	if (*depth > MAX_JSON_STACK) {
		ast_log(LOG_WARNING, "Max JSON stack (%d) exceeded\n", MAX_JSON_STACK);
		return -1;
	}

	snprintf(buf, len, "%s", ""); /* clear the buffer from previous round if necessary */
	if (!json) { /* no error or warning should be thrown */
		ast_debug(1, "Could not find key '%s' in parsed JSON\n", currentkey);
		return -1;
	}

	switch(ast_json_typeof(jsonval)) {
		unsigned long int size;
		int r;
		double d;

		case AST_JSON_STRING:
			result = ast_json_string_get(jsonval);
			ast_debug(1, "Got JSON string: %s\n", result);
			ast_copy_string(buf, result, len);
			break;
		case AST_JSON_INTEGER:
			r = ast_json_integer_get(jsonval);
			ast_debug(1, "Got JSON integer: %d\n", r);
			snprintf(buf, len, "%d", r); /* the snprintf below is mutually exclusive with this one */
			break;
		case AST_JSON_REAL:
			d = ast_json_real_get(jsonval);
			ast_debug(1, "Got JSON real: %.17g\n", d);
			snprintf(buf, len, "%.17g", d); /* the snprintf below is mutually exclusive with this one */
			break;
		case AST_JSON_ARRAY:
			ast_debug(1, "Got JSON array\n");
			previouskey = currentkey;
			currentkey = strsep(key, nestchar); /* retrieve the desired index */
			size = ast_json_array_size(jsonval);
			ast_debug(1, "Parsed JSON array of size %lu, key: %s\n", size, currentkey);
			if (!currentkey) { /* this is the end, so just dump the array */
				if (count) {
					ast_debug(1, "No key on which to index in the array, so returning count: %lu\n", size);
					snprintf(buf, len, "%lu", size);
					return 0;
				} else {
					char *result2 = ast_json_dump_string(jsonval);
					ast_debug(1, "No key on which to index in the array, so dumping '%s' array\n", previouskey);
					ast_copy_string(buf, result2, len);
					ast_json_free(result2);
				}
			} else if (ast_str_to_int(currentkey, &r) || r < 0) {
				ast_debug(1, "Requested index '%s' is not numeric or is invalid\n", currentkey);
			} else if (r >= size) {
				ast_debug(1, "Requested index '%d' does not exist in parsed array\n", r);
			} else {
				ast_debug(1, "Recursing on index %d in array\n", r);
				if (parse_node(key, currentkey, nestchar, count, ast_json_array_get(jsonval, r), buf, len, depth)) { /* recurse on this node */
					return -1;
				}
			}
			break;
		case AST_JSON_TRUE:
		case AST_JSON_FALSE:
			r = ast_json_is_true(jsonval);
			ast_debug(1, "Got JSON %s for key %s\n", r ? "true" : "false", currentkey);
			snprintf(buf, len, "%d", r); /* the snprintf below is mutually exclusive with this one */
			break;
		case AST_JSON_NULL:
			ast_debug(1, "Got JSON null for key %s\n", currentkey);
			break;
		case AST_JSON_OBJECT:
			ast_debug(1, "Got generic JSON object for key %s\n", currentkey);
			previouskey = currentkey;
			currentkey = strsep(key, nestchar); /* retrieve the desired index */
			if (!currentkey) { /* this is the end, so just dump the object */
				char *result2 = ast_json_dump_string(jsonval);
				ast_copy_string(buf, result2, len);
				ast_json_free(result2);
			} else {
				ast_debug(1, "Recursing on object (key was '%s' and is now '%s')\n", previouskey, currentkey);
				if (parse_node(key, currentkey, nestchar, count, ast_json_object_get(jsonval, currentkey), buf, len, depth)) { /* recurse on this node */
					return -1;
				}
			}
			break;
		default:
			ast_log(LOG_WARNING, "Got unsuported type %d\n", ast_json_typeof(jsonval));
			return -1;
	}
	return 0;
}

static int json_decode_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int count = 0;
	struct ast_flags flags = {0};
	struct ast_json *json = NULL, *start = NULL;
	char *nestchar = "."; /* default delimeter for nesting key indexing is . */
	int index, res, depth = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(varname);
		AST_APP_ARG(key);
		AST_APP_ARG(nestchar);
		AST_APP_ARG(options);
	);
	char *varsubst, *key, *currentkey, *nextkey, *firstkey, *tmp;
	struct ast_str *str = ast_str_thread_get(&result_buf, 16);

	AST_STANDARD_APP_ARGS(args, data);

	if (!ast_strlen_zero(args.options)) {
		ast_app_parse_options(json_options, &flags, NULL, args.options);
		if (ast_test_flag(&flags, OPT_COUNT)) {
			count = 1;
		}
	}

	if (ast_strlen_zero(args.varname)) {
		ast_log(LOG_WARNING, "%s requires a variable name\n", cmd);
		return -1;
	}

	if (ast_strlen_zero(args.key)) {
		ast_log(LOG_WARNING, "%s requires a key\n", cmd);
		return -1;
	}

	key = ast_strdupa(args.key);
	if (!ast_strlen_zero(args.nestchar)) {
		int seplen = strlen(args.nestchar);
		if (seplen != 1) {
			ast_log(LOG_WARNING, "Nesting separator '%s' has length %d and is invalid (must be a single character)\n", args.nestchar, seplen);
		} else {
			nestchar = args.nestchar;
		}
	}

	varsubst = ast_alloca(strlen(args.varname) + 4); /* +4 for ${} and null terminator */
	if (!varsubst) {
		ast_log(LOG_ERROR, "Failed to allocate string\n");
		return -1;
	}
	sprintf(varsubst, "${%s}", args.varname); /* safe, because of the above allocation */
	ast_str_substitute_variables(&str, 0, chan, varsubst);

	ast_debug(1, "Parsing JSON using nesting delimeter '%s'\n", nestchar);

	if (ast_str_strlen(str) == 0) {
		ast_debug(1, "Variable '%s' contains no data, nothing to search!\n", args.varname);
		return -1; /* empty json string */
	}

	/* allow for multiple key nesting */
	currentkey = key;
	firstkey = ast_strdupa(currentkey);
	tmp = strstr(firstkey, nestchar);
	if (tmp) {
		*tmp = '\0';
	}

	/* parse a string as JSON */
	ast_debug(1, "Parsing JSON: %s (key: '%s')\n", ast_str_buffer(str), currentkey);
	if (ast_strlen_zero(currentkey)) {
		ast_debug(1, "Empty JSON key\n");
		return -1;
	}
	if (ast_str_strlen(str) == 0) {
		ast_debug(1, "JSON node '%s', contains no data, nothing to search!\n", currentkey);
		return -1; /* empty json string */
	}

	json = ast_json_load_str(str, NULL);
	if (!json) {
		ast_log(LOG_WARNING, "Failed to parse as JSON: %s\n", ast_str_buffer(str));
		return -1;
	}

	/* parse the JSON object, potentially recursively */
	nextkey = strsep(&key, nestchar);
	if (ast_json_is_object(json)) {
		start = ast_json_object_get(json, firstkey);
	} else {
		if (ast_str_to_int(currentkey, &index)) {
			ast_debug(1, "Requested index '%s' is not numeric or is invalid\n", currentkey);
			return -1;
		}
		start = ast_json_array_get(json, index);
	}

	res = parse_node(&key, nextkey, nestchar, count, start, buf, len, &depth);
	ast_json_unref(json);
	return res;
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

	const char *test_strings[][6] = {
		{"{\"myboolean\": true, \"state\": \"USA\"}", "", "myboolean", "1"},
		{"{\"myboolean\": false, \"state\": \"USA\"}", "", "myboolean", "0"},
		{"{\"myreal\": 1E+2, \"state\": \"USA\"}", "", "myreal", "100"},
		{"{\"myreal\": 1.23, \"state\": \"USA\"}", "", "myreal", "1.23"},
		{"{\"myarray\": [[1]], \"state\": \"USA\"}", "", "myarray.0.0", "1"},
		{"{\"myarray\": [null], \"state\": \"USA\"}", "", "myarray.0", ""},
		{"{\"myarray\": [0, 1], \"state\": \"USA\"}", "", "myarray", "[0,1]"},
		{"[0, 1]", "", "", ""},
		{"[0, 1]", "", "0", "0"},
		{"[0, 1]", "", "foo", ""},
		{"{\"mynull\": null, \"state\": \"USA\"}", "", "mynull", ""},
		{"{\"city\": \"Anytown\", \"state\": \"USA\"}", "", "city", "Anytown"},
		{"{\"city\": \"Anytown\", \"state\": \"USA\"}", "", "state", "USA"},
		{"{\"city\": \"Anytown\", \"state\": \"USA\"}", "", "blah", ""},
		{"{\"key1\": \"123\", \"key2\": \"456\"}", "", "key1", "123"},
		{"{\"key1\": 123, \"key2\": 456}", "", "key1", "123"},
		{"{ \"path\": { \"to\": { \"elem\": \"someVar\" } } }", "/", "path/to/elem", "someVar"},
		{"{ \"path\": { \"to\": { \"elem\": \"someVar\" } } }", "", "path.to.elem2", ""},
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", "/", "path/to/arr/2", ""}, /* nonexistent index */
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", "/", "path/to/arr/-1", ""}, /* bogus index */
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", "/", "path/to/arr/test", ""}, /* bogus index */
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", "", "path.to.arr.test.test2.subkey", ""}, /* bogus index */
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", ",c", "path.to.arr", "2"}, /* test count */
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", "", "path.to.arr", "[\"item0\",\"item1\"]"},
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", ".", "path.to.arr.1", "item1"},
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", "/", "path/to/arr", "[\"item0\",\"item1\"]"},
		{"{ \"path\": { \"to\": { \"arr\": [ \"item0\", \"item1\" ] } } }", "/", "path/to/arr/1", "item1"},
		{"{ \"path\": { \"to\": { \"arr\": [ {\"name\": \"John Smith\", \"phone\": \"123\"}, {\"name\": \"Jane Doe\", \"phone\": \"234\"} ] } } }", ",c", "path.to.arr.0.name", "John Smith"},
		{"{ \"path\": { \"to\": { \"arr\": [ {\"name\": 1, \"phone\": 123}, {\"name\": 2, \"phone\": 234} ] } } }", ",c", "path.to.arr.0.name", "1"},
		{"{ \"path\": { \"to\": { \"arr\": [ {\"name\": [ \"item11\", \"item12\" ], \"phone\": [ \"item13\", \"item14\" ]}, {\"name\": [ \"item15\", \"item16\" ], \"phone\": [ \"item17\", \"item18\" ]} ] } } }", ",c", "path.to.arr.0.name.1", "item12"},
		{"{ \"startId\": \"foobar\", \"abcd\": { \"id\": \"abcd\", \"type\": \"EXT\" }, \"bcde\": { \"id\": \"bcde\", \"type\": \"CONDITION\" }, \"defg\": { \"id\": \"defg\", \"type\": \"EXT\" }, \"efgh\": { \"id\": \"efgh\", \"type\": \"VOICEMAIL\" } }", "", "bcde", "{\"id\":\"bcde\",\"type\":\"CONDITION\"}"},
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
		char tmp[512];

		struct ast_var_t *var = ast_var_assign("test_string", test_strings[i][0]);
		if (!var) {
			ast_test_status_update(test, "Unable to allocate variable\n");
			ast_free(str);
			ast_channel_release(chan);
			return AST_TEST_FAIL;
		}

		AST_LIST_INSERT_HEAD(ast_channel_varshead(chan), var, entries);

		snprintf(tmp, sizeof(tmp), "${JSON_DECODE(%s,%s,%s)}", "test_string", test_strings[i][2], test_strings[i][1]);

		ast_str_substitute_variables(&str, 0, chan, tmp);
		if (strcmp(test_strings[i][3], ast_str_buffer(str))) {
			ast_test_status_update(test, "Format string '%s' substituted to '%s' (key: %s). Expected '%s'.\n", test_strings[i][0], ast_str_buffer(str), test_strings[i][2], test_strings[i][3]);
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

#ifdef TEST_FRAMEWORK
	AST_TEST_UNREGISTER(test_JSON_DECODE);
#endif
	res = ast_custom_function_unregister(&json_decode_function);

	return res;
}

static int load_module(void)
{
	int res;

#ifdef TEST_FRAMEWORK
	AST_TEST_REGISTER(test_JSON_DECODE);
#endif
	res = ast_custom_function_register(&json_decode_function);

	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "JSON decoding function");
