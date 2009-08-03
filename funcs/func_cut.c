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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

/* Maximum length of any variable */
#define MAXRESULT	1024

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

	memset(buffer, 0, buflen);

	if (!data)
		return ERROR_NOARG;

	strings = ast_strdupa(data);

	for (ptrkey = strings; *ptrkey; ptrkey++) {
		if (*ptrkey == '|')
			count++;
	}

	sortable_keys = alloca(count * sizeof(struct sortable_keys));

	memset(sortable_keys, 0, count * sizeof(struct sortable_keys));

	/* Parse each into a struct */
	count2 = 0;
	while ((ptrkey = strsep(&strings, "|"))) {
		ptrvalue = strchr(ptrkey, ':');
		if (!ptrvalue) {
			count--;
			continue;
		}
		*ptrvalue++ = '\0';
		sortable_keys[count2].key = ptrkey;
		sscanf(ptrvalue, "%f", &sortable_keys[count2].value);
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

static int cut_internal(struct ast_channel *chan, char *data, char *buffer, size_t buflen)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(varname);
		AST_APP_ARG(delimiter);
		AST_APP_ARG(field);
	);

	memset(buffer, 0, buflen); 

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	/* Check and parse arguments */
	if(args.argc < 3){
		return ERROR_NOARG;
	} else {
		char d, ds[2];
		char *tmp = alloca(strlen(args.varname) + 4);
		char varvalue[MAXRESULT], *tmp2=varvalue;

		if (tmp) {
			snprintf(tmp, strlen(args.varname) + 4, "${%s}", args.varname);
			memset(varvalue, 0, sizeof(varvalue));
		} else {
			return ERROR_NOMEM;
		}

		if (args.delimiter[0] == '\\') {
			if (args.delimiter[1] == 'n')
				d = '\n';
			else if (args.delimiter[1] == 't')
				d = '\t';
			else if (args.delimiter[1] == 'r')
				d = '\r';
			else if (args.delimiter[1])
				d = args.delimiter[1];
			else
				d = '-';
		} else if (args.delimiter[0])
			d = args.delimiter[0];
		else
			d = '-';

		/* String form of the delimiter, for use with strsep(3) */
		snprintf(ds, sizeof(ds), "%c", d);

		pbx_substitute_variables_helper(chan, tmp, tmp2, MAXRESULT - 1);

		if (tmp2) {
			int curfieldnum = 1, firstfield = 1;
			while (tmp2 != NULL && args.field != NULL) {
				char *nextgroup = strsep(&(args.field), "&");
				int num1 = 0, num2 = MAXRESULT;
				char trashchar;

				if (sscanf(nextgroup, "%d-%d", &num1, &num2) == 2) {
					/* range with both start and end */
				} else if (sscanf(nextgroup, "-%d", &num2) == 1) {
					/* range with end */
					num1 = 0;
				} else if ((sscanf(nextgroup, "%d%c", &num1, &trashchar) == 2) && (trashchar == '-')) {
					/* range with start */
					num2 = MAXRESULT;
				} else if (sscanf(nextgroup, "%d", &num1) == 1) {
					/* single number */
					num2 = num1;
				} else {
					return ERROR_USAGE;
				}

				/* Get to start, if any */
				if (num1 > 0) {
					while (tmp2 != (char *)NULL + 1 && curfieldnum < num1) {
						tmp2 = strchr(tmp2, d) + 1;
						curfieldnum++;
					}
				}

				/* Most frequent problem is the expectation of reordering fields */
				if ((num1 > 0) && (curfieldnum > num1))
					ast_log(LOG_WARNING, "We're already past the field you wanted?\n");

				/* Re-null tmp2 if we added 1 to NULL */
				if (tmp2 == (char *)NULL + 1)
					tmp2 = NULL;

				/* Output fields until we either run out of fields or num2 is reached */
				while (tmp2 != NULL && curfieldnum <= num2) {
					char *tmp3 = strsep(&tmp2, ds);
					int curlen = strlen(buffer);

					if (firstfield) {
						snprintf(buffer, buflen, "%s", tmp3);
						firstfield = 0;
					} else {
						snprintf(buffer + curlen, buflen - curlen, "%c%s", d, tmp3);
					}
					curfieldnum++;
				}
			}
		}
	}
	return 0;
}

static int acf_sort_exec(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct ast_module_user *u;
	int ret = -1;

	u = ast_module_user_add(chan);

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

	ast_module_user_remove(u);

	return ret;
}

static int acf_cut_exec(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	int ret = -1;
	struct ast_module_user *u = NULL;

	if (chan) {
		u = ast_module_user_add(chan);
	}

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

	if (chan) {
		ast_module_user_remove(u);
	}

	return ret;
}

struct ast_custom_function acf_sort = {
	.name = "SORT",
	.synopsis = "Sorts a list of key/vals into a list of keys, based upon the vals",
	.syntax = "SORT(key1:val1[...][,keyN:valN])",
	.desc =
"Takes a comma-separated list of keys and values, each separated by a colon, and returns a\n"
"comma-separated list of the keys, sorted by their values.  Values will be evaluated as\n"
"floating-point numbers.\n",
	.read = acf_sort_exec,
};

struct ast_custom_function acf_cut = {
	.name = "CUT",
	.synopsis = "Slices and dices strings, based upon a named delimiter.",
	.syntax = "CUT(<varname>,<char-delim>,<range-spec>)",
	.desc =
"  varname    - variable you want cut\n"
"  char-delim - defaults to '-'\n"
"  range-spec - number of the field you want (1-based offset)\n"
"             may also be specified as a range (with -)\n"
"             or group of ranges and fields (with &)\n",
	.read = acf_cut_exec,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&acf_cut);
	res |= ast_custom_function_unregister(&acf_sort);

	ast_module_user_hangup_all();

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
