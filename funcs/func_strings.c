/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005-2006, Digium, Inc.
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

/*! \file
 *
 * \brief String manipulation dialplan functions
 *
 * \author Tilghman Lesher
 * \author Anothony Minessale II 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <regex.h>

#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/localtime.h"

static int function_fieldqty(struct ast_channel *chan, char *cmd,
			     char *parse, char *buf, size_t len)
{
	char *varsubst, varval[8192] = "", *varval2 = varval;
	int fieldcount = 0;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(varname);
			     AST_APP_ARG(delim);
		);

	AST_STANDARD_APP_ARGS(args, parse);
	if (args.delim) {
		varsubst = alloca(strlen(args.varname) + 4);

		sprintf(varsubst, "${%s}", args.varname);
		pbx_substitute_variables_helper(chan, varsubst, varval, sizeof(varval) - 1);
		if (ast_strlen_zero(varval2))
			fieldcount = 0;
		else {
			while (strsep(&varval2, args.delim))
				fieldcount++;
		}
	} else {
		fieldcount = 1;
	}
	snprintf(buf, len, "%d", fieldcount);

	return 0;
}

static struct ast_custom_function fieldqty_function = {
	.name = "FIELDQTY",
	.synopsis = "Count the fields, with an arbitrary delimiter",
	.syntax = "FIELDQTY(<varname>|<delim>)",
	.read = function_fieldqty,
};

static int filter(struct ast_channel *chan, char *cmd, char *parse, char *buf,
		  size_t len)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(allowed);
			     AST_APP_ARG(string);
	);
	char *outbuf = buf;

	AST_STANDARD_APP_ARGS(args, parse);

	if (!args.string) {
		ast_log(LOG_ERROR, "Usage: FILTER(<allowed-chars>|<string>)\n");
		return -1;
	}

	for (; *(args.string) && (buf + len - 1 > outbuf); (args.string)++) {
		if (strchr(args.allowed, *(args.string)))
			*outbuf++ = *(args.string);
	}
	*outbuf = '\0';

	return 0;
}

static struct ast_custom_function filter_function = {
	.name = "FILTER",
	.synopsis = "Filter the string to include only the allowed characters",
	.syntax = "FILTER(<allowed-chars>|<string>)",
	.read = filter,
};

static int regex(struct ast_channel *chan, char *cmd, char *parse, char *buf,
		 size_t len)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(null);
			     AST_APP_ARG(reg);
			     AST_APP_ARG(str);
	);
	int errcode;
	regex_t regexbuf;

	buf[0] = '\0';

	AST_NONSTANDARD_APP_ARGS(args, parse, '"');

	if (args.argc != 3) {
		ast_log(LOG_ERROR, "Unexpected arguments: should have been in the form '\"<regex>\" <string>'\n");
		return -1;
	}
	if ((*args.str == ' ') || (*args.str == '\t'))
		args.str++;

	if (option_debug)
		ast_log(LOG_DEBUG, "FUNCTION REGEX (%s)(%s)\n", args.reg, args.str);

	if ((errcode = regcomp(&regexbuf, args.reg, REG_EXTENDED | REG_NOSUB))) {
		regerror(errcode, &regexbuf, buf, len);
		ast_log(LOG_WARNING, "Malformed input %s(%s): %s\n", cmd, parse, buf);
		return -1;
	}
	
	strcpy(buf, regexec(&regexbuf, args.str, 0, NULL, 0) ? "0" : "1");

	regfree(&regexbuf);

	return 0;
}

static struct ast_custom_function regex_function = {
	.name = "REGEX",
	.synopsis = "Regular Expression",
	.desc =  
		"Returns 1 if data matches regular expression, or 0 otherwise.\n"
		"Please note that the space following the double quotes separating the regex from the data\n"
		"is optional and if present, is skipped. If a space is desired at the beginning of the data,\n"
	        "then put two spaces there; the second will not be skipped.\n",
	.syntax = "REGEX(\"<regular expression>\" <data>)",
	.read = regex,
};

static int strecmp(const char *pre, const char *post)
{
	int res;
	for (; *pre && *post; pre++, post++) {
		if (*pre == '"') {
			post--;
			continue;
		} else if (*pre == '\\') {
			pre++;
		}
		if ((res = strncmp(pre, post, 1))) {
			return res;
		}
	}
	return strncmp(pre, post, 1);
}

static int array(struct ast_channel *chan, char *cmd, char *var,
		 const char *value)
{
	AST_DECLARE_APP_ARGS(arg1,
			     AST_APP_ARG(var)[100];
	);
	AST_DECLARE_APP_ARGS(arg2,
			     AST_APP_ARG(val)[100];
	);
	char *value2;
	int i;

	if (chan) {
		const char *value3;
		ast_mutex_lock(&chan->lock);
		if ((value3 = pbx_builtin_getvar_helper(chan, "~ODBCVALUES~")) && strecmp(value3, value) == 0) {
			value = ast_strdupa(value3);
		}
		ast_mutex_unlock(&chan->lock);
	}

	value2 = ast_strdupa(value);
	if (!var || !value2)
		return -1;

	/* The functions this will generally be used with are SORT and ODBC_*, which
	 * both return comma-delimited lists.  However, if somebody uses literal lists,
	 * their commas will be translated to vertical bars by the load, and I don't
	 * want them to be surprised by the result.  Hence, we prefer commas as the
	 * delimiter, but we'll fall back to vertical bars if commas aren't found.
	 */
	if (option_debug)
		ast_log(LOG_DEBUG, "array (%s=%s)\n", var, value2);
	if (strchr(var, ','))
		AST_NONSTANDARD_APP_ARGS(arg1, var, ',');
	else
		AST_STANDARD_APP_ARGS(arg1, var);

	if (strchr(value2, ','))
		AST_NONSTANDARD_APP_ARGS(arg2, value2, ',');
	else
		AST_STANDARD_APP_ARGS(arg2, value2);

	for (i = 0; i < arg1.argc; i++) {
		if (option_debug)
			ast_log(LOG_DEBUG, "array set value (%s=%s)\n", arg1.var[i],
				arg2.val[i]);
		if (i < arg2.argc) {
			pbx_builtin_setvar_helper(chan, arg1.var[i], arg2.val[i]);
		} else {
			/* We could unset the variable, by passing a NULL, but due to
			 * pushvar semantics, that could create some undesired behavior. */
			pbx_builtin_setvar_helper(chan, arg1.var[i], "");
		}
	}

	return 0;
}

static struct ast_custom_function array_function = {
	.name = "ARRAY",
	.synopsis = "Allows setting multiple variables at once",
	.syntax = "ARRAY(var1[|var2[...][|varN]])",
	.write = array,
	.desc =
		"The comma-separated list passed as a value to which the function is set will\n"
		"be interpreted as a set of values to which the comma-separated list of\n"
		"variable names in the argument should be set.\n"
		"Hence, Set(ARRAY(var1|var2)=1\\,2) will set var1 to 1 and var2 to 2\n"
		"Note: remember to either backslash your commas in extensions.conf or quote the\n"
		"entire argument, since Set can take multiple arguments itself.\n",
};

static int acf_sprintf(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
#define SPRINTF_FLAG	0
#define SPRINTF_WIDTH	1
#define SPRINTF_PRECISION	2
#define SPRINTF_LENGTH	3
#define SPRINTF_CONVERSION	4
	int i, state = -1, argcount = 0;
	char *formatstart = NULL, *bufptr = buf;
	char formatbuf[256] = "";
	int tmpi;
	double tmpd;
	AST_DECLARE_APP_ARGS(arg,
				AST_APP_ARG(format);
				AST_APP_ARG(var)[100];
	);

	AST_STANDARD_APP_ARGS(arg, data);

	/* Scan the format, converting each argument into the requisite format type. */
	for (i = 0; arg.format[i]; i++) {
		switch (state) {
		case SPRINTF_FLAG:
			if (strchr("#0- +'I", arg.format[i]))
				break;
			state = SPRINTF_WIDTH;
		case SPRINTF_WIDTH:
			if (arg.format[i] >= '0' && arg.format[i] <= '9')
				break;

			/* Next character must be a period to go into a precision */
			if (arg.format[i] == '.') {
				state = SPRINTF_PRECISION;
			} else {
				state = SPRINTF_LENGTH;
				i--;
			}
			break;
		case SPRINTF_PRECISION:
			if (arg.format[i] >= '0' && arg.format[i] <= '9')
				break;
			state = SPRINTF_LENGTH;
		case SPRINTF_LENGTH:
			if (strchr("hl", arg.format[i])) {
				if (arg.format[i + 1] == arg.format[i])
					i++;
				state = SPRINTF_CONVERSION;
				break;
			} else if (strchr("Lqjzt", arg.format[i])) {
				state = SPRINTF_CONVERSION;
				break;
			}
			state = SPRINTF_CONVERSION;
		case SPRINTF_CONVERSION:
			if (strchr("diouxXc", arg.format[i])) {
				/* Integer */

				/* Isolate this format alone */
				ast_copy_string(formatbuf, formatstart, sizeof(formatbuf));
				formatbuf[&arg.format[i] - formatstart + 1] = '\0';

				/* Convert the argument into the required type */
				if (arg.var[argcount]) {
					if (sscanf(arg.var[argcount++], "%d", &tmpi) != 1) {
						ast_log(LOG_ERROR, "Argument '%s' is not an integer number for format '%s'\n", arg.var[argcount - 1], formatbuf);
						goto sprintf_fail;
					}
				} else {
					ast_log(LOG_ERROR, "SPRINTF() has more format specifiers than arguments!\n");
					goto sprintf_fail;
				}

				/* Format the argument */
				snprintf(bufptr, buf + len - bufptr, formatbuf, tmpi);

				/* Update the position of the next parameter to print */
				bufptr = strchr(buf, '\0');
			} else if (strchr("eEfFgGaA", arg.format[i])) {
				/* Double */

				/* Isolate this format alone */
				ast_copy_string(formatbuf, formatstart, sizeof(formatbuf));
				formatbuf[&arg.format[i] - formatstart + 1] = '\0';

				/* Convert the argument into the required type */
				if (arg.var[argcount]) {
					if (sscanf(arg.var[argcount++], "%lf", &tmpd) != 1) {
						ast_log(LOG_ERROR, "Argument '%s' is not a floating point number for format '%s'\n", arg.var[argcount - 1], formatbuf);
						goto sprintf_fail;
					}
				} else {
					ast_log(LOG_ERROR, "SPRINTF() has more format specifiers than arguments!\n");
					goto sprintf_fail;
				}

				/* Format the argument */
				snprintf(bufptr, buf + len - bufptr, formatbuf, tmpd);

				/* Update the position of the next parameter to print */
				bufptr = strchr(buf, '\0');
			} else if (arg.format[i] == 's') {
				/* String */

				/* Isolate this format alone */
				ast_copy_string(formatbuf, formatstart, sizeof(formatbuf));
				formatbuf[&arg.format[i] - formatstart + 1] = '\0';

				/* Format the argument */
				snprintf(bufptr, buf + len - bufptr, formatbuf, arg.var[argcount++]);

				/* Update the position of the next parameter to print */
				bufptr = strchr(buf, '\0');
			} else if (arg.format[i] == '%') {
				/* Literal data to copy */
				*bufptr++ = arg.format[i];
			} else {
				/* Not supported */

				/* Isolate this format alone */
				ast_copy_string(formatbuf, formatstart, sizeof(formatbuf));
				formatbuf[&arg.format[i] - formatstart + 1] = '\0';

				ast_log(LOG_ERROR, "Format type not supported: '%s' with argument '%s'\n", formatbuf, arg.var[argcount++]);
				goto sprintf_fail;
			}
			state = -1;
			break;
		default:
			if (arg.format[i] == '%') {
				state = SPRINTF_FLAG;
				formatstart = &arg.format[i];
				break;
			} else {
				/* Literal data to copy */
				*bufptr++ = arg.format[i];
			}
		}
	}
	*bufptr = '\0';
	return 0;
sprintf_fail:
	return -1;
}

static struct ast_custom_function sprintf_function = {
	.name = "SPRINTF",
	.synopsis = "Format a variable according to a format string",
	.syntax = "SPRINTF(<format>|<arg1>[|...<argN>])",
	.read = acf_sprintf,
	.desc =
"Parses the format string specified and returns a string matching that format.\n"
"Supports most options supported by sprintf(3).  Returns a shortened string if\n"
"a format specifier is not recognized.\n",
};

static int quote(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	char *bufptr = buf, *dataptr = data;
	*bufptr++ = '"';
	for (; bufptr < buf + len - 1; dataptr++) {
		if (*dataptr == '\\') {
			*bufptr++ = '\\';
			*bufptr++ = '\\';
		} else if (*dataptr == '"') {
			*bufptr++ = '\\';
			*bufptr++ = '"';
		} else if (*dataptr == '\0') {
			break;
		} else {
			*bufptr++ = *dataptr;
		}
	}
	*bufptr++ = '"';
	*bufptr = '\0';
	return 0;
}

static struct ast_custom_function quote_function = {
	.name = "QUOTE",
	.synopsis = "Quotes a given string, escaping embedded quotes as necessary",
	.syntax = "QUOTE(<string>)",
	.read = quote,
};


static int len(struct ast_channel *chan, char *cmd, char *data, char *buf,
	       size_t len)
{
	int length = 0;

	if (data)
		length = strlen(data);

	snprintf(buf, len, "%d", length);

	return 0;
}

static struct ast_custom_function len_function = {
	.name = "LEN",
	.synopsis = "Returns the length of the argument given",
	.syntax = "LEN(<string>)",
	.read = len,
};

static int acf_strftime(struct ast_channel *chan, char *cmd, char *parse,
			char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(epoch);
			     AST_APP_ARG(timezone);
			     AST_APP_ARG(format);
	);
	time_t epochi;
	struct tm tm;

	buf[0] = '\0';

	AST_STANDARD_APP_ARGS(args, parse);

	ast_get_time_t(args.epoch, &epochi, time(NULL), NULL);
	ast_localtime(&epochi, &tm, args.timezone);

	if (!args.format)
		args.format = "%c";

	if (!strftime(buf, len, args.format, &tm))
		ast_log(LOG_WARNING, "C function strftime() output nothing?!!\n");

	buf[len - 1] = '\0';

	return 0;
}

static struct ast_custom_function strftime_function = {
	.name = "STRFTIME",
	.synopsis = "Returns the current date/time in a specified format.",
	.syntax = "STRFTIME([<epoch>][|[timezone][|format]])",
	.read = acf_strftime,
};

static int acf_strptime(struct ast_channel *chan, char *cmd, char *data,
			char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(timestring);
			     AST_APP_ARG(timezone);
			     AST_APP_ARG(format);
	);
	struct tm time;

	memset(&time, 0, sizeof(struct tm));

	buf[0] = '\0';

	if (!data) {
		ast_log(LOG_ERROR,
				"Asterisk function STRPTIME() requires an argument.\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.format)) {
		ast_log(LOG_ERROR,
				"No format supplied to STRPTIME(<timestring>|<timezone>|<format>)");
		return -1;
	}

	if (!strptime(args.timestring, args.format, &time)) {
		ast_log(LOG_WARNING, "C function strptime() output nothing?!!\n");
	} else {
		/* Since strptime(3) does not check DST, force ast_mktime() to calculate it. */
		time.tm_isdst = -1;
		snprintf(buf, len, "%d", (int) ast_mktime(&time, args.timezone));
	}

	return 0;
}

static struct ast_custom_function strptime_function = {
	.name = "STRPTIME",
	.synopsis =
		"Returns the epoch of the arbitrary date/time string structured as described in the format.",
	.syntax = "STRPTIME(<datetime>|<timezone>|<format>)",
	.desc =
		"This is useful for converting a date into an EPOCH time, possibly to pass to\n"
		"an application like SayUnixTime or to calculate the difference between two\n"
		"date strings.\n"
		"\n"
		"Example:\n"
		"  ${STRPTIME(2006-03-01 07:30:35|America/Chicago|%Y-%m-%d %H:%M:%S)} returns 1141219835\n",
	.read = acf_strptime,
};

static int function_eval(struct ast_channel *chan, char *cmd, char *data,
			 char *buf, size_t len)
{
	memset(buf, 0, len);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "EVAL requires an argument: EVAL(<string>)\n");
		return -1;
	}

	pbx_substitute_variables_helper(chan, data, buf, len - 1);

	return 0;
}

static struct ast_custom_function eval_function = {
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

static int keypadhash(struct ast_channel *chan, char *cmd, char *data, char *buf, size_t len)
{
	char *bufptr, *dataptr;

	for (bufptr = buf, dataptr = data; bufptr < buf + len - 1; dataptr++) {
		if (*dataptr == '\0') {
			*bufptr++ = '\0';
			break;
		} else if (*dataptr == '1') {
			*bufptr++ = '1';
		} else if (strchr("AaBbCc2", *dataptr)) {
			*bufptr++ = '2';
		} else if (strchr("DdEeFf3", *dataptr)) {
			*bufptr++ = '3';
		} else if (strchr("GgHhIi4", *dataptr)) {
			*bufptr++ = '4';
		} else if (strchr("JjKkLl5", *dataptr)) {
			*bufptr++ = '5';
		} else if (strchr("MmNnOo6", *dataptr)) {
			*bufptr++ = '6';
		} else if (strchr("PpQqRrSs7", *dataptr)) {
			*bufptr++ = '7';
		} else if (strchr("TtUuVv8", *dataptr)) {
			*bufptr++ = '8';
		} else if (strchr("WwXxYyZz9", *dataptr)) {
			*bufptr++ = '9';
		} else if (*dataptr == '0') {
			*bufptr++ = '0';
		}
	}
	buf[len - 1] = '\0';

	return 0;
}

static struct ast_custom_function keypadhash_function = {
	.name = "KEYPADHASH",
	.synopsis = "Hash the letters in the string into the equivalent keypad numbers.",
	.syntax = "KEYPADHASH(<string>)",
	.read = keypadhash,
	.desc = "Example:  ${KEYPADHASH(Les)} returns \"537\"\n",
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&fieldqty_function);
	res |= ast_custom_function_unregister(&filter_function);
	res |= ast_custom_function_unregister(&regex_function);
	res |= ast_custom_function_unregister(&array_function);
	res |= ast_custom_function_unregister(&quote_function);
	res |= ast_custom_function_unregister(&len_function);
	res |= ast_custom_function_unregister(&strftime_function);
	res |= ast_custom_function_unregister(&strptime_function);
	res |= ast_custom_function_unregister(&eval_function);
	res |= ast_custom_function_unregister(&keypadhash_function);
	res |= ast_custom_function_unregister(&sprintf_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&fieldqty_function);
	res |= ast_custom_function_register(&filter_function);
	res |= ast_custom_function_register(&regex_function);
	res |= ast_custom_function_register(&array_function);
	res |= ast_custom_function_register(&quote_function);
	res |= ast_custom_function_register(&len_function);
	res |= ast_custom_function_register(&strftime_function);
	res |= ast_custom_function_register(&strptime_function);
	res |= ast_custom_function_register(&eval_function);
	res |= ast_custom_function_register(&keypadhash_function);
	res |= ast_custom_function_register(&sprintf_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "String handling dialplan functions");
