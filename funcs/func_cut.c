/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2003-2006 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_cut__v003@the-tilghman.com>
 *
 * This code is released by the author with no restrictions on usage.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 */

/*! \file
 * 
 * \brief CUT function
 *
 * \author Tilghman Lesher <app_cut__v003@the-tilghman.com>
 *
 * \ingroup functions
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

/*** DOCUMENTATION
	<function name="SORT" language="en_US">
		<synopsis>
			Sorts a list of key/vals into a list of keys, based upon the vals.	
		</synopsis>
		<syntax>
			<parameter name="keyval" required="true" argsep=":">
				<argument name="key1" required="true" />
				<argument name="val1" required="true" />
			</parameter>
			<parameter name="keyvaln" multiple="true" argsep=":">
				<argument name="key2" required="true" />
				<argument name="val2" required="true" />
			</parameter>
		</syntax>
		<description>
			<para>Takes a comma-separated list of keys and values, each separated by a colon, and returns a
			comma-separated list of the keys, sorted by their values.  Values will be evaluated as
			floating-point numbers.</para>
		</description>
	</function>
	<function name="CUT" language="en_US">
		<synopsis>
			Slices and dices strings, based upon a named delimiter.		
		</synopsis>
		<syntax>
			<parameter name="varname" required="true">
				<para>Variable you want cut</para>
			</parameter>
			<parameter name="char-delim" required="true">
				<para>Delimiter, defaults to <literal>-</literal></para>
			</parameter>
			<parameter name="range-spec" required="true">
				<para>Number of the field you want (1-based offset), may also be specified as a range (with <literal>-</literal>)
				or group of ranges and fields (with <literal>&amp;</literal>)</para>
			</parameter>
		</syntax>
		<description>
			<para>Cut out information from a string (<replaceable>varname</replaceable>), based upon a named delimiter.</para>
		</description>	
	</function>
 ***/

struct sortable_keys {
	char *key;
	float value;
};

static int sort_subroutine(const void *arg1, const void *arg2)
{
	const struct sortable_keys *one=arg1, *two=arg2;
	if (one->value < two->value)
		return -1;
	else if (one->value == two->value)
		return 0;
	else
		return 1;
}

#define ERROR_NOARG	(-1)
#define ERROR_NOMEM	(-2)
#define ERROR_USAGE	(-3)

static int sort_internal(struct ast_channel *chan, char *data, char *buffer, size_t buflen)
{
	char *strings, *ptrkey, *ptrvalue;
	int count=1, count2, element_count=0;
	struct sortable_keys *sortable_keys;

	*buffer = '\0';

	if (!data)
		return ERROR_NOARG;

	strings = ast_strdupa(data);

	for (ptrkey = strings; *ptrkey; ptrkey++) {
		if (*ptrkey == ',')
			count++;
	}

	sortable_keys = alloca(count * sizeof(struct sortable_keys));

	memset(sortable_keys, 0, count * sizeof(struct sortable_keys));

	/* Parse each into a struct */
	count2 = 0;
	while ((ptrkey = strsep(&strings, ","))) {
		ptrvalue = strchr(ptrkey, ':');
		if (!ptrvalue) {
			count--;
			continue;
		}
		*ptrvalue++ = '\0';
		sortable_keys[count2].key = ptrkey;
		sscanf(ptrvalue, "%30f", &sortable_keys[count2].value);
		count2++;
	}

	/* Sort the structs */
	qsort(sortable_keys, count, sizeof(struct sortable_keys), sort_subroutine);

	for (count2 = 0; count2 < count; count2++) {
		int blen = strlen(buffer);
		if (element_count++) {
			strncat(buffer + blen, ",", buflen - blen - 1);
			blen++;
		}
		strncat(buffer + blen, sortable_keys[count2].key, buflen - blen - 1);
	}

	return 0;
}

static int cut_internal(struct ast_channel *chan, char *data, struct ast_str **buf, ssize_t buflen)
{
	char *parse, ds[2], *var_expr;
	size_t delim_consumed;
	struct ast_str *var_value;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(varname);
		AST_APP_ARG(delimiter);
		AST_APP_ARG(field);
	);

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	/* Check arguments */
	if (args.argc < 3) {
		return ERROR_NOARG;
	} else if (!(var_expr = alloca(strlen(args.varname) + 4))) {
		return ERROR_NOMEM;
	}

	/* Get the value of the variable named in the 1st argument */
	snprintf(var_expr, strlen(args.varname) + 4, "${%s}", args.varname);
	var_value = ast_str_create(16);
	ast_str_substitute_variables(&var_value, 0, chan, var_expr);

	/* Copy delimiter from 2nd argument to ds[] possibly decoding backslash escapes */
	if (ast_get_encoded_char(args.delimiter, ds, &delim_consumed)) {
		ast_copy_string(ds, "-", sizeof(ds));
	}
	ds[1] = '\0';

	if (ast_str_strlen(var_value)) {
		int curfieldnum = 1;
		char *curfieldptr = ast_str_buffer(var_value);
		int out_field_count = 0;

		while (curfieldptr != NULL && args.field != NULL) {
			char *next_range = strsep(&(args.field), "&");
			int start_field, stop_field;
			char trashchar;

			if (sscanf(next_range, "%30d-%30d", &start_field, &stop_field) == 2) {
				/* range with both start and end */
			} else if (sscanf(next_range, "-%30d", &stop_field) == 1) {
				/* range with end only */
				start_field = 1;
			} else if ((sscanf(next_range, "%30d%1c", &start_field, &trashchar) == 2) && (trashchar == '-')) {
				/* range with start only */
				stop_field = INT_MAX;
			} else if (sscanf(next_range, "%30d", &start_field) == 1) {
				/* single number */
				stop_field = start_field;
			} else {
				/* invalid field spec */
				ast_free(var_value);
				return ERROR_USAGE;
			}

			/* Get to start, if not there already */
			while (curfieldptr != NULL && curfieldnum < start_field) {
				strsep(&curfieldptr, ds);
				curfieldnum++;
			}

			/* Most frequent problem is the expectation of reordering fields */
			if (curfieldnum > start_field) {
				ast_log(LOG_WARNING, "We're already past the field you wanted?\n");
			}

			/* Output fields until we either run out of fields or stop_field is reached */
			while (curfieldptr != NULL && curfieldnum <= stop_field) {
				char *field_value = strsep(&curfieldptr, ds);
				ast_str_append(buf, buflen, "%s%s", out_field_count++ ? ds : "", field_value);
				curfieldnum++;
			}
		}
	}
	ast_free(var_value);
	return 0;
}

static int acf_sort_exec(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int ret = -1;

	switch (sort_internal(chan, data, buf, len)) {
	case ERROR_NOARG:
		ast_log(LOG_ERROR, "SORT() requires an argument\n");
		break;
	case ERROR_NOMEM:
		ast_log(LOG_ERROR, "Out of memory\n");
		break;
	case 0:
		ret = 0;
		break;
	default:
		ast_log(LOG_ERROR, "Unknown internal error\n");
	}

	return ret;
}

static int acf_cut_exec(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	int ret = -1;
	struct ast_str *str = ast_str_create(16);

	switch (cut_internal(chan, data, &str, len)) {
	case ERROR_NOARG:
		ast_log(LOG_ERROR, "Syntax: CUT(<varname>,<char-delim>,<range-spec>) - missing argument!\n");
		break;
	case ERROR_NOMEM:
		ast_log(LOG_ERROR, "Out of memory\n");
		break;
	case ERROR_USAGE:
		ast_log(LOG_ERROR, "Usage: CUT(<varname>,<char-delim>,<range-spec>)\n");
		break;
	case 0:
		ret = 0;
		ast_copy_string(buf, ast_str_buffer(str), len);
		break;
	default:
		ast_log(LOG_ERROR, "Unknown internal error\n");
	}
	ast_free(str);
	return ret;
}

static int acf_cut_exec2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	int ret = -1;

	switch (cut_internal(chan, data, buf, len)) {
	case ERROR_NOARG:
		ast_log(LOG_ERROR, "Syntax: CUT(<varname>,<char-delim>,<range-spec>) - missing argument!\n");
		break;
	case ERROR_NOMEM:
		ast_log(LOG_ERROR, "Out of memory\n");
		break;
	case ERROR_USAGE:
		ast_log(LOG_ERROR, "Usage: CUT(<varname>,<char-delim>,<range-spec>)\n");
		break;
	case 0:
		ret = 0;
		break;
	default:
		ast_log(LOG_ERROR, "Unknown internal error\n");
	}

	return ret;
}

static struct ast_custom_function acf_sort = {
	.name = "SORT",
	.read = acf_sort_exec,
};

static struct ast_custom_function acf_cut = {
	.name = "CUT",
	.read = acf_cut_exec,
	.read2 = acf_cut_exec2,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&acf_cut);
	res |= ast_custom_function_unregister(&acf_sort);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&acf_cut);
	res |= ast_custom_function_register(&acf_sort);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Cut out information from a string");
