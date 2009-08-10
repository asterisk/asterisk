/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2006, Andy Powell 
 *
 * Updated by Mark Spencer <markster@digium.com>
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
 * \brief Math related dialplan function
 *
 * \author Andy Powell
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/config.h"

enum TypeOfFunctions {
	ADDFUNCTION,
	DIVIDEFUNCTION,
	MULTIPLYFUNCTION,
	SUBTRACTFUNCTION,
	MODULUSFUNCTION,
	GTFUNCTION,
	LTFUNCTION,
	GTEFUNCTION,
	LTEFUNCTION,
	EQFUNCTION
};

enum TypeOfResult {
	FLOAT_RESULT,
	INT_RESULT,
	HEX_RESULT,
	CHAR_RESULT
};

static int math(struct ast_channel *chan, char *cmd, char *parse,
		char *buf, size_t len)
{
	double fnum1;
	double fnum2;
	double ftmp = 0;
	char *op;
	int iaction = -1;
	int type_of_result = FLOAT_RESULT;
	char *mvalue1, *mvalue2 = NULL, *mtype_of_result;
	int negvalue1 = 0;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(argv0);
			     AST_APP_ARG(argv1);
	);

	if (ast_strlen_zero(parse)) {
		ast_log(LOG_WARNING, "Syntax: MATH(<number1><op><number 2>[,<type_of_result>]) - missing argument!\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 1) {
		ast_log(LOG_WARNING, "Syntax: MATH(<number1><op><number 2>[,<type_of_result>]) - missing argument!\n");
		return -1;
	}

	mvalue1 = args.argv0;

	if (mvalue1[0] == '-') {
		negvalue1 = 1;
		mvalue1++;
	}

	if ((op = strchr(mvalue1, '*'))) {
		iaction = MULTIPLYFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '/'))) {
		iaction = DIVIDEFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '%'))) {
		iaction = MODULUSFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '>'))) {
		iaction = GTFUNCTION;
		*op = '\0';
		if (*(op + 1) == '=') {
			*++op = '\0';
			iaction = GTEFUNCTION;
		}
	} else if ((op = strchr(mvalue1, '<'))) {
		iaction = LTFUNCTION;
		*op = '\0';
		if (*(op + 1) == '=') {
			*++op = '\0';
			iaction = LTEFUNCTION;
		}
	} else if ((op = strchr(mvalue1, '='))) {
		*op = '\0';
		if (*(op + 1) == '=') {
			*++op = '\0';
			iaction = EQFUNCTION;
		} else
			op = NULL;
	} else if ((op = strchr(mvalue1, '+'))) {
		iaction = ADDFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '-'))) { /* subtraction MUST always be last, in case we have a negative first number */
		iaction = SUBTRACTFUNCTION;
		*op = '\0';
	}

	if (op)
		mvalue2 = op + 1;

	/* detect wanted type of result */
	mtype_of_result = args.argv1;
	if (mtype_of_result) {
		if (!strcasecmp(mtype_of_result, "float")
		    || !strcasecmp(mtype_of_result, "f"))
			type_of_result = FLOAT_RESULT;
		else if (!strcasecmp(mtype_of_result, "int")
			 || !strcasecmp(mtype_of_result, "i"))
			type_of_result = INT_RESULT;
		else if (!strcasecmp(mtype_of_result, "hex")
			 || !strcasecmp(mtype_of_result, "h"))
			type_of_result = HEX_RESULT;
		else if (!strcasecmp(mtype_of_result, "char")
			 || !strcasecmp(mtype_of_result, "c"))
			type_of_result = CHAR_RESULT;
		else {
			ast_log(LOG_WARNING, "Unknown type of result requested '%s'.\n",
					mtype_of_result);
			return -1;
		}
	}

	if (!mvalue1 || !mvalue2) {
		ast_log(LOG_WARNING,
				"Supply all the parameters - just this once, please\n");
		return -1;
	}

	if (sscanf(mvalue1, "%30lf", &fnum1) != 1) {
		ast_log(LOG_WARNING, "'%s' is not a valid number\n", mvalue1);
		return -1;
	}

	if (sscanf(mvalue2, "%30lf", &fnum2) != 1) {
		ast_log(LOG_WARNING, "'%s' is not a valid number\n", mvalue2);
		return -1;
	}

	if (negvalue1)
		fnum1 = 0 - fnum1;

	switch (iaction) {
	case ADDFUNCTION:
		ftmp = fnum1 + fnum2;
		break;
	case DIVIDEFUNCTION:
		if (fnum2 <= 0)
			ftmp = 0;			/* can't do a divide by 0 */
		else
			ftmp = (fnum1 / fnum2);
		break;
	case MULTIPLYFUNCTION:
		ftmp = (fnum1 * fnum2);
		break;
	case SUBTRACTFUNCTION:
		ftmp = (fnum1 - fnum2);
		break;
	case MODULUSFUNCTION:
		{
			int inum1 = fnum1;
			int inum2 = fnum2;

			ftmp = (inum1 % inum2);

			break;
		}
	case GTFUNCTION:
		ast_copy_string(buf, (fnum1 > fnum2) ? "TRUE" : "FALSE", len);
		break;
	case LTFUNCTION:
		ast_copy_string(buf, (fnum1 < fnum2) ? "TRUE" : "FALSE", len);
		break;
	case GTEFUNCTION:
		ast_copy_string(buf, (fnum1 >= fnum2) ? "TRUE" : "FALSE", len);
		break;
	case LTEFUNCTION:
		ast_copy_string(buf, (fnum1 <= fnum2) ? "TRUE" : "FALSE", len);
		break;
	case EQFUNCTION:
		ast_copy_string(buf, (fnum1 == fnum2) ? "TRUE" : "FALSE", len);
		break;
	default:
		ast_log(LOG_WARNING,
				"Something happened that neither of us should be proud of %d\n",
				iaction);
		return -1;
	}

	if (iaction < GTFUNCTION || iaction > EQFUNCTION) {
		if (type_of_result == FLOAT_RESULT)
			snprintf(buf, len, "%f", ftmp);
		else if (type_of_result == INT_RESULT)
			snprintf(buf, len, "%i", (int) ftmp);
		else if (type_of_result == HEX_RESULT)
			snprintf(buf, len, "%x", (unsigned int) ftmp);
		else if (type_of_result == CHAR_RESULT)
			snprintf(buf, len, "%c", (unsigned char) ftmp);
	}

	return 0;
}

static struct ast_custom_function math_function = {
	.name = "MATH",
	.synopsis = "Performs Mathematical Functions",
	.syntax = "MATH(<number1><op><number 2>[,<type_of_result>])",
	.desc = "Perform calculation on number 1 to number 2. Valid ops are: \n"
		"    +,-,/,*,%,<,>,>=,<=,==\n"
		"and behave as their C equivalents.\n"
		"<type_of_result> - wanted type of result:\n"
		"	f, float - float(default)\n"
		"	i, int - integer,\n"
		"	h, hex - hex,\n"
		"	c, char - char\n"
		"Example: Set(i=${MATH(123%16,int)}) - sets var i=11",
	.read = math
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&math_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&math_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Mathematical dialplan function");
