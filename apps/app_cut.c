/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Cut application
 *
 * Copyright (c) 2003 Tilghman Lesher.  All rights reserved.
 *
 * Tilghman Lesher <app_cut__v003@the-tilghman.com>
 *
 * $Id$
 *
 * This code is released by the author with no restrictions on usage.
 *
 */

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

/* Maximum length of any variable */
#define MAXRESULT	1024

static char *tdesc = "Cuts up variables";

static char *app_cut = "Cut";

static char *cut_synopsis = "Splits a variable's content using the specified delimiter";

static char *cut_descrip =
"Usage: Cut(newvar=varname,delimiter,fieldspec)\n"
"  newvar    - new variable created from result string\n"
"  varname   - variable you want cut\n"
"  delimiter - defaults to '-'\n"
"  fieldspec - number of the field you want (1-based offset)\n"
"            may also be specified as a range (with -)\n"
"            or group of ranges and fields (with &)\n" 
"  Returns 0 or -1 on hangup or error.\n";

STANDARD_LOCAL_USER;

LOCAL_USER_DECL;

static int cut_exec(struct ast_channel *chan, void *data)
{
	int res=0;
	struct localuser *u;
	char *s, *newvar=NULL, *varname=NULL, *delimiter=NULL, *field=NULL;
	int args_okay = 0;

	LOCAL_USER_ADD(u);

	/* Check and parse arguments */
	if (data) {
		s = ast_strdupa((char *)data);
		if (s) {
			newvar = strsep(&s, "=");
			if (newvar && (newvar[0] != '\0')) {
				varname = strsep(&s, "|");
				if (varname && (varname[0] != '\0')) {
					delimiter = strsep(&s, "|");
					if (delimiter) {
						field = strsep(&s, "|");
						if (field) {
							args_okay = 1;
						}
					}
				}
			}
		} else {
			ast_log(LOG_ERROR, "Out of memory\n");
			res = -1;
		}
	}

	if (args_okay) {
		char d, ds[2];
		char *tmp = alloca(strlen(varname) + 4);
		char varvalue[MAXRESULT], *tmp2=varvalue;
		char retstring[MAXRESULT];

		memset(retstring, 0, MAXRESULT);

		if (tmp) {
			snprintf(tmp, strlen(varname) + 4, "${%s}", varname);
			memset(varvalue, 0, sizeof(varvalue));
		} else {
			ast_log(LOG_ERROR, "Out of memory");
			return -1;
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
					ast_log(LOG_ERROR, "Cut(): Illegal range '%s'\n", nextgroup);
					ast_log(LOG_ERROR, "Usage: %s\n", cut_synopsis);
					return -1;
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
					ast_log(LOG_WARNING, "Cut(): we're already past the field you wanted?\n");
				}

				/* Re-null tmp2 if we added 1 to NULL */
				if (tmp2 == (char *)NULL + 1)
					tmp2 = NULL;

				/* Output fields until we either run out of fields or num2 is reached */
				while ((tmp2 != NULL) && (curfieldnum <= num2)) {
					char *tmp3 = strsep(&tmp2, ds);
					int curlen = strlen(retstring);

					if (strlen(retstring)) {
						snprintf(retstring + curlen, MAXRESULT - curlen, "%c%s", d, tmp3);
					} else {
						snprintf(retstring, MAXRESULT, "%s", tmp3);
					}

					curfieldnum++;
				}
			}
		}

		pbx_builtin_setvar_helper(chan, newvar, retstring);
	}

	LOCAL_USER_REMOVE(u);
	return res;
}

int unload_module(void)
{
	STANDARD_HANGUP_LOCALUSERS;
	return ast_unregister_application(app_cut);
}

int load_module(void)
{
	return ast_register_application(app_cut, cut_exec, cut_synopsis, cut_descrip);
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
