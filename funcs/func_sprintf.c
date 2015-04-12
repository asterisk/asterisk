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
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <ctype.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"

AST_THREADSTORAGE(result_buf);

/*** DOCUMENTATION
	<function name="SPRINTF" language="en_US">
		<synopsis>
			Format a variable according to a format string.
		</synopsis>
		<syntax>
			<parameter name="format" required="true" />
			<parameter name="arg1" required="true" />
			<parameter name="arg2" multiple="true" />
			<parameter name="argN" />
		</syntax>
		<description>
			<para>Parses the format string specified and returns a string matching 
			that format. Supports most options found in <emphasis>sprintf(3)</emphasis>.
			Returns a shortened string if a format specifier is not recognized.</para>
		</description>
		<see-also>
			<ref type="manpage">sprintf(3)</ref>
		</see-also>
	</function>
 ***/
static int acf_sprintf(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
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
					if (sscanf(arg.var[argcount++], "%30d", &tmpi) != 1) {
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
					if (sscanf(arg.var[argcount++], "%30lf", &tmpd) != 1) {
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
	.read = acf_sprintf,
};

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&sprintf_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&sprintf_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SPRINTF dialplan function");
