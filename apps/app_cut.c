/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (c) 2003 Tilghman Lesher.  All rights reserved.
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
 * \brief Cut application
 *
 * \ingroup applications
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/app.h"

/* Maximum length of any variable */
#define MAXRESULT	1024

static char *tdesc = "Cut out information from a string";

static char *app_cut = "Cut";

static char *cut_synopsis = "Splits a variable's contents using the specified delimiter";

static char *cut_descrip =
"  Cut(newvar=varname,delimiter,fieldspec): This applicaiton will split the\n"
"contents of a variable based on the given delimeter and store the result in\n"
"a new variable.\n"
"Parameters:\n"
"  newvar    - new variable created from result string\n"
"  varname   - variable you want cut\n"
"  delimiter - defaults to '-'\n"
"  fieldspec - number of the field you want (1-based offset)\n"
"              may also be specified as a range (with -)\n"
"              or group of ranges and fields (with &)\n"
"This application has been deprecated in favor of the CUT function.\n";

static char *app_sort = "Sort";
static char *app_sort_synopsis = "Sorts a list of keywords and values";
static char *app_sort_descrip =
"  Sort(newvar=key1:val1[,key2:val2[[...],keyN:valN]]): This application will\n"
"sort the list provided in ascending order. The result will be stored in the\n"
"specified variable name.\n"
"  This applicaiton has been deprecated in favor of the SORT function.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

struct sortable_keys {
	char *key;
	float value;
};

static int sort_subroutine(const void *arg1, const void *arg2)
{
	const struct sortable_keys *one=arg1, *two=arg2;
	if (one->value < two->value) {
		return -1;
	} else if (one->value == two->value) {
		return 0;
	} else {
		return 1;
	}
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

	if (!data) {
		return ERROR_NOARG;
	}

	strings = ast_strdupa((char *)data);
	if (!strings) {
		return ERROR_NOMEM;
	}

	for (ptrkey = strings; *ptrkey; ptrkey++) {
		if (*ptrkey == '|') {
			count++;
		}
	}

	sortable_keys = alloca(count * sizeof(struct sortable_keys));
	if (!sortable_keys) {
		return ERROR_NOMEM;
	}

	memset(sortable_keys, 0, count * sizeof(struct sortable_keys));

	/* Parse each into a struct */
	count2 = 0;
	while ((ptrkey = strsep(&strings, "|"))) {
		ptrvalue = index(ptrkey, ':');
		if (!ptrvalue) {
			count--;
			continue;
		}
		*ptrvalue = '\0';
		ptrvalue++;
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
		}
		strncat(buffer + blen + 1, sortable_keys[count2].key, buflen - blen - 2);
	}

	return 0;
}

static int cut_internal(struct ast_channel *chan, char *data, char *buffer, size_t buflen)
{
	char *s, *args[3], *varname=NULL, *delimiter=NULL, *field=NULL;
	int args_okay = 0;

	memset(buffer, 0, buflen);

	/* Check and parse arguments */
	if (data) {
		s = ast_strdupa((char *)data);
		if (s) {
			ast_app_separate_args(s, '|', args, 3);
			varname = args[0];
			delimiter = args[1];
			field = args[2];

			if (field) {
				args_okay = 1;
			}
		} else {
			return ERROR_NOMEM;
		}
	}

	if (args_okay) {
		char d, ds[2];
		char *tmp = alloca(strlen(varname) + 4);
		char varvalue[MAXRESULT], *tmp2=varvalue;

		if (tmp) {
			snprintf(tmp, strlen(varname) + 4, "${%s}", varname);
			memset(varvalue, 0, sizeof(varvalue));
		} else {
			return ERROR_NOMEM;
		}

		if (delimiter[0])
			d = delimiter[0];
		else
			d = '-';

		/* String form of the delimiter, for use with strsep(3) */
		snprintf(ds, sizeof(ds), "%c", d);

		pbx_substitute_variables_helper(chan, tmp, tmp2, MAXRESULT - 1);

		if (tmp2) {
			int curfieldnum = 1;
			while ((tmp2 != NULL) && (field != NULL)) {
				char *nextgroup = strsep(&field, "&");
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
					while ((tmp2 != (char *)NULL + 1) && (curfieldnum < num1)) {
						tmp2 = index(tmp2, d) + 1;
						curfieldnum++;
					}
				}

				/* Most frequent problem is the expectation of reordering fields */
				if ((num1 > 0) && (curfieldnum > num1)) {
					ast_log(LOG_WARNING, "We're already past the field you wanted?\n");
				}

				/* Re-null tmp2 if we added 1 to NULL */
				if (tmp2 == (char *)NULL + 1)
					tmp2 = NULL;

				/* Output fields until we either run out of fields or num2 is reached */
				while ((tmp2 != NULL) && (curfieldnum <= num2)) {
					char *tmp3 = strsep(&tmp2, ds);
					int curlen = strlen(buffer);

					if (curlen) {
						snprintf(buffer + curlen, buflen - curlen, "%c%s", d, tmp3);
					} else {
						snprintf(buffer, buflen, "%s", tmp3);
					}

					curfieldnum++;
				}
			}
		}
	}
	return 0;
}

static int sort_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *varname, *strings, result[512] = "";
	static int dep_warning=0;

	if (!dep_warning) {
		ast_log(LOG_WARNING, "The application Sort is deprecated.  Please use the SORT() function instead.\n");
		dep_warning=1;
	}

	if (!data) {
		ast_log(LOG_ERROR, "Sort() requires an argument\n");
		return 0;
	}

	LOCAL_USER_ADD(u);

	strings = ast_strdupa((char *)data);
	if (!strings) {
		ast_log(LOG_ERROR, "Out of memory\n");
		LOCAL_USER_REMOVE(u);
		return 0;
	}

	varname = strsep(&strings, "=");
	switch (sort_internal(chan, strings, result, sizeof(result))) {
	case ERROR_NOARG:
		ast_log(LOG_ERROR, "Sort() requires an argument\n");
		res = 0;
		break;
	case ERROR_NOMEM:
		ast_log(LOG_ERROR, "Out of memory\n");
		res = -1;
		break;
	case 0:
		pbx_builtin_setvar_helper(chan, varname, result);
		res = 0;
		break;
	default:
		ast_log(LOG_ERROR, "Unknown internal error\n");
		res = -1;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static int cut_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *s, *newvar=NULL, result[512];
	static int dep_warning = 0;

	LOCAL_USER_ADD(u);

	if (!dep_warning) {
		ast_log(LOG_WARNING, "The application Cut is deprecated.  Please use the CUT() function instead.\n");
		dep_warning=1;
	}

	/* Check and parse arguments */
	if (data) {
		s = ast_strdupa((char *)data);
		if (s) {
			newvar = strsep(&s, "=");
		} else {
			ast_log(LOG_ERROR, "Out of memory\n");
			LOCAL_USER_REMOVE(u);
			return -1;
		}
	}

	switch (cut_internal(chan, s, result, sizeof(result))) {
	case ERROR_NOARG:
		ast_log(LOG_ERROR, "Cut() requires an argument\n");
		res = 0;
		break;
	case ERROR_NOMEM:
		ast_log(LOG_ERROR, "Out of memory\n");
		res = -1;
		break;
	case ERROR_USAGE:
		ast_log(LOG_ERROR, "Usage: %s\n", cut_synopsis);
		res = 0;
		break;
	case 0:
		pbx_builtin_setvar_helper(chan, newvar, result);
		res = 0;
		break;
	default:
		ast_log(LOG_ERROR, "Unknown internal error\n");
		res = -1;
	}
	LOCAL_USER_REMOVE(u);
	return res;
}

static char *acf_sort_exec(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct localuser *u;

	LOCAL_USER_ACF_ADD(u);

	switch (sort_internal(chan, data, buf, len)) {
	case ERROR_NOARG:
		ast_log(LOG_ERROR, "SORT() requires an argument\n");
		break;
	case ERROR_NOMEM:
		ast_log(LOG_ERROR, "Out of memory\n");
		break;
	case 0:
		break;
	default:
		ast_log(LOG_ERROR, "Unknown internal error\n");
	}
	LOCAL_USER_REMOVE(u);
	return buf;
}

static char *acf_cut_exec(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	struct localuser *u;

	LOCAL_USER_ACF_ADD(u);

	switch (cut_internal(chan, data, buf, len)) {
	case ERROR_NOARG:
		ast_log(LOG_ERROR, "Cut() requires an argument\n");
		break;
	case ERROR_NOMEM:
		ast_log(LOG_ERROR, "Out of memory\n");
		break;
	case ERROR_USAGE:
		ast_log(LOG_ERROR, "Usage: %s\n", cut_synopsis);
		break;
	case 0:
		break;
	default:
		ast_log(LOG_ERROR, "Unknown internal error\n");
	}
	LOCAL_USER_REMOVE(u);
	return buf;
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

int unload_module(void)
{
	int res;

	res = ast_custom_function_unregister(&acf_cut);
	res |= ast_custom_function_unregister(&acf_sort);
	res |= ast_unregister_application(app_sort);
	res |= ast_unregister_application(app_cut);

	STANDARD_HANGUP_LOCALUSERS;

	return res;
}

int load_module(void)
{
	int res;

	res = ast_custom_function_register(&acf_cut);
	res |= ast_custom_function_register(&acf_sort);
	res |= ast_register_application(app_sort, sort_exec, app_sort_synopsis, app_sort_descrip);
	res |= ast_register_application(app_cut, cut_exec, cut_synopsis, cut_descrip);

	return res;
}

char *description(void)
{
	return tdesc;
}

int usecount(void)
{
	int res;
	STANDARD_USECOUNT(res);
	return res;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
