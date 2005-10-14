/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Digium, Inc.
 * Portions Copyright (C) 2005, Tilghman Lesher.  All rights reserved.
 * Portions Copyright (C) 2005, Anthony Minessale II
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

/*
 *
 * String manipulation dialplan functions
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "asterisk.h"

/* ASTERISK_FILE_VERSION(__FILE__, "$Revision$") */

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/localtime.h"

static char *function_fieldqty(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	char *varname, *varval, workspace[256];
	char *delim = ast_strdupa(data);
	int fieldcount = 0;

	if (delim) {
		varname = strsep(&delim, "|");
		pbx_retrieve_variable(chan, varname, &varval, workspace, sizeof(workspace), NULL);
		while (strsep(&varval, delim))
			fieldcount++;
		snprintf(buf, len, "%d", fieldcount);
	} else {
		ast_log(LOG_ERROR, "Out of memory\n");
		strncpy(buf, "1", len);
	}
	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function fieldqty_function = {
	.name = "FIELDQTY",
	.synopsis = "Count the fields, with an arbitrary delimiter",
	.syntax = "FIELDQTY(<varname>,<delim>)",
	.read = function_fieldqty,
};

static char *builtin_function_regex(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *arg, *earg = NULL, *tmp, errstr[256] = "";
	int errcode;
	regex_t regexbuf;

	ast_copy_string(buf, "0", len);
	
	tmp = ast_strdupa(data);
	if (!tmp) {
		ast_log(LOG_ERROR, "Out of memory in %s(%s)\n", cmd, data);
		return buf;
	}

	/* Regex in quotes */
	arg = strchr(tmp, '"');
	if (arg) {
		arg++;
		earg = strrchr(arg, '"');
		if (earg) {
			*earg++ = '\0';
			/* Skip over any spaces before the data we are checking */
			while (*earg == ' ')
				earg++;
		}
	} else {
		arg = tmp;
	}

	if ((errcode = regcomp(&regexbuf, arg, REG_EXTENDED | REG_NOSUB))) {
		regerror(errcode, &regexbuf, errstr, sizeof(errstr));
		ast_log(LOG_WARNING, "Malformed input %s(%s): %s\n", cmd, data, errstr);
	} else {
		if (!regexec(&regexbuf, earg ? earg : "", 0, NULL, 0))
			ast_copy_string(buf, "1", len); 
	}
	regfree(&regexbuf);

	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function regex_function = {
	.name = "REGEX",
	.synopsis = "Regular Expression: Returns 1 if data matches regular expression.",
	.syntax = "REGEX(\"<regular expression>\" <data>)",
	.read = builtin_function_regex,
};

static char *builtin_function_len(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	int length = 0;
	if (data) {
		length = strlen(data);
	}
	snprintf(buf, len, "%d", length);
	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function len_function = {
	.name = "LEN",
	.synopsis = "Returns the length of the argument given",
	.syntax = "LEN(<string>)",
	.read = builtin_function_len,
};

static char *acf_strftime(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	char *format, *epoch, *timezone;
	long epochi;
	struct tm time;

	if (data) {
		format = ast_strdupa(data);
		if (format) {
			epoch = strsep(&format, "|");
			timezone = strsep(&format, "|");

			if (epoch && !ast_strlen_zero(epoch) && sscanf(epoch, "%ld", &epochi) == 1) {
			} else {
				struct timeval tv = ast_tvnow();
				epochi = tv.tv_sec;
			}

			ast_localtime(&epochi, &time, timezone);

			if (!format) {
				format = "%c";
			}

			buf[0] = '\0';
			if (! strftime(buf, len, format, &time)) {
				ast_log(LOG_WARNING, "C function strftime() output nothing?!!\n");
			}
			buf[len - 1] = '\0';

			return buf;
		} else {
			ast_log(LOG_ERROR, "Out of memory\n");
		}
	} else {
		ast_log(LOG_ERROR, "Asterisk function STRFTIME() requires an argument.\n");
	}
	return "";
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function strftime_function = {
	.name = "STRFTIME",
	.synopsis = "Returns the current date/time in a specified format.",
	.syntax = "STRFTIME([<epoch>][,[timezone][,format]])",
	.read = acf_strftime,
};

static char *function_eval(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len) 
{
	memset(buf, 0, len);

	if (!data || ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "EVAL requires an argument: EVAL(<string>)\n");
		return buf;
	}

	pbx_substitute_variables_helper(chan, data, buf, len - 1);

	return buf;
}

#ifndef BUILTIN_FUNC
static
#endif
struct ast_custom_function eval_function = {
	.name = "EVAL",
	.synopsis = "Evaluate stored variables.",
	.syntax = "EVAL(<variable>)",
	.desc = "Using EVAL basically causes a string to be evaluated twice.\n"
		"When a variable or expression is in the dialplan, it will be\n"
		"evaluated at runtime. However, if the result of the evaluation\n"
		"is in fact a variable or expression, using EVAL will have it\n"
		"evaluated a second time. For example, if the variable ${MYVAR}\n"
		"contains \"${OTHERVAR}\", then the result of putting ${EVAL(${MYVAR})}\n"
		"in the dialplan will be the contents of the variable, OTHERVAR.\n"
		"Normally, by just putting ${MYVAR} in the dialplan, you would be\n"
		"left with \"${OTHERVAR}\".\n", 
	.read = function_eval,
};

