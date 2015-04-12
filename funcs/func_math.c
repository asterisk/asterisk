/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2004 - 2006, Andy Powell
 *
 * Updated by Mark Spencer <markster@digium.com>
 * Updated by Nir Simionovich <nirs@greenfieldtech.net>
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
 * \author Nir Simionovich <nirs@greenfieldtech.net>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE(__FILE__)

#include <math.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/config.h"
#include "asterisk/test.h"

/*** DOCUMENTATION
	<function name="MATH" language="en_US">
		<synopsis>
			Performs Mathematical Functions.
		</synopsis>
		<syntax>
			<parameter name="expression" required="true">
				<para>Is of the form:
				<replaceable>number1</replaceable><replaceable>op</replaceable><replaceable>number2</replaceable>
				where the possible values for <replaceable>op</replaceable>
				are:</para>
				<para>+,-,/,*,%,&lt;&lt;,&gt;&gt;,^,AND,OR,XOR,&lt;,&gt;,&lt;=,&gt;=,== (and behave as their C equivalents)</para>
			</parameter>
			<parameter name="type">
				<para>Wanted type of result:</para>
				<para>f, float - float(default)</para>
				<para>i, int - integer</para>
				<para>h, hex - hex</para>
				<para>c, char - char</para>
			</parameter>
		</syntax>
		<description>
			<para>Performs mathematical functions based on two parameters and an operator.  The returned
			value type is <replaceable>type</replaceable></para>
			<para>Example: Set(i=${MATH(123%16,int)}) - sets var i=11</para>
		</description>
	</function>
	<function name="INC" language="en_US">
		<synopsis>
			Increments the value of a variable, while returning the updated value to the dialplan
		</synopsis>
		<syntax>
			<parameter name="variable" required="true">
				<para>
				The variable name to be manipulated, without the braces.
				</para>
			</parameter>
		</syntax>
		<description>
			<para>Increments the value of a variable, while returning the updated value to the dialplan</para>
			<para>Example: INC(MyVAR) - Increments MyVar</para>
			<para>Note: INC(${MyVAR}) - Is wrong, as INC expects the variable name, not its value</para>
		</description>
	</function>
	<function name="DEC" language="en_US">
		<synopsis>
			Decrements the value of a variable, while returning the updated value to the dialplan
		</synopsis>
		<syntax>
			<parameter name="variable" required="true">
				<para>
				The variable name to be manipulated, without the braces.
				</para>
			</parameter>
		</syntax>
		<description>
			<para>Decrements the value of a variable, while returning the updated value to the dialplan</para>
			<para>Example: DEC(MyVAR) - Decrements MyVar</para>
			<para>Note: DEC(${MyVAR}) - Is wrong, as DEC expects the variable name, not its value</para>
		</description>
	</function>
 ***/

enum TypeOfFunctions {
	ADDFUNCTION,
	DIVIDEFUNCTION,
	MULTIPLYFUNCTION,
	SUBTRACTFUNCTION,
	MODULUSFUNCTION,
	POWFUNCTION,
	SHLEFTFUNCTION,
	SHRIGHTFUNCTION,
	BITWISEANDFUNCTION,
	BITWISEXORFUNCTION,
	BITWISEORFUNCTION,
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

static int math(struct ast_channel *chan, const char *cmd, char *parse,
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
	} else if ((op = strchr(mvalue1, '^'))) {
		iaction = POWFUNCTION;
		*op = '\0';
	} else if ((op = strstr(mvalue1, "AND"))) {
		iaction = BITWISEANDFUNCTION;
		*op = '\0';
		op += 2;
	} else if ((op = strstr(mvalue1, "XOR"))) {
		iaction = BITWISEXORFUNCTION;
		*op = '\0';
		op += 2;
	} else if ((op = strstr(mvalue1, "OR"))) {
		iaction = BITWISEORFUNCTION;
		*op = '\0';
		++op;
	} else if ((op = strchr(mvalue1, '>'))) {
		iaction = GTFUNCTION;
		*op = '\0';
		if (*(op + 1) == '=') {
			iaction = GTEFUNCTION;
			++op;
		} else if (*(op + 1) == '>') {
			iaction = SHRIGHTFUNCTION;
			++op;
		}
	} else if ((op = strchr(mvalue1, '<'))) {
		iaction = LTFUNCTION;
		*op = '\0';
		if (*(op + 1) == '=') {
			iaction = LTEFUNCTION;
			++op;
		} else if (*(op + 1) == '<') {
			iaction = SHLEFTFUNCTION;
			++op;
		}
	} else if ((op = strchr(mvalue1, '='))) {
		*op = '\0';
		if (*(op + 1) == '=') {
			iaction = EQFUNCTION;
			++op;
		} else
			op = NULL;
	} else if ((op = strchr(mvalue1, '+'))) {
		iaction = ADDFUNCTION;
		*op = '\0';
	} else if ((op = strchr(mvalue1, '-'))) { /* subtraction MUST always be last, in case we have a negative second number */
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

	if (!mvalue2) {
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

			if (inum2 == 0) {
				ftmp = 0;
			} else {
				ftmp = (inum1 % inum2);
			}

			break;
		}
	case POWFUNCTION:
		ftmp = pow(fnum1, fnum2);
		break;
	case SHLEFTFUNCTION:
		{
			int inum1 = fnum1;
			int inum2 = fnum2;

			ftmp = (inum1 << inum2);
			break;
		}
	case SHRIGHTFUNCTION:
		{
			int inum1 = fnum1;
			int inum2 = fnum2;

			ftmp = (inum1 >> inum2);
			break;
		}
	case BITWISEANDFUNCTION:
		{
			int inum1 = fnum1;
			int inum2 = fnum2;
			ftmp = (inum1 & inum2);
			break;
		}
	case BITWISEXORFUNCTION:
		{
			int inum1 = fnum1;
			int inum2 = fnum2;
			ftmp = (inum1 ^ inum2);
			break;
		}
	case BITWISEORFUNCTION:
		{
			int inum1 = fnum1;
			int inum2 = fnum2;
			ftmp = (inum1 | inum2);
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

static int crement_function_read(struct ast_channel *chan, const char *cmd,
                     char *data, char *buf, size_t len)
{
	int ret = -1;
	int int_value = 0;
	int modify_orig = 0;
	const char *var;
	char endchar = 0, returnvar[12]; /* If you need a variable longer than 11 digits - something is way wrong */

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Syntax: %s(<data>) - missing argument!\n", cmd);
		return -1;
	}

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);

	if (!(var = pbx_builtin_getvar_helper(chan, data))) {
		ast_log(LOG_NOTICE, "Failed to obtain variable %s, bailing out\n", data);
		ast_channel_unlock(chan);
		return -1;
	}

	if (ast_strlen_zero(var)) {
		ast_log(LOG_NOTICE, "Variable %s doesn't exist - are you sure you wrote it correctly?\n", data);
		ast_channel_unlock(chan);
		return -1;
	}

	if (sscanf(var, "%30d%1c", &int_value, &endchar) == 0 || endchar != 0) {
		ast_log(LOG_NOTICE, "The content of ${%s} is not a numeric value - bailing out!\n", data);
		ast_channel_unlock(chan);
		return -1;
	}

	/* now we'll actually do something useful */
	if (!strcasecmp(cmd, "INC")) {              /* Increment variable */
		int_value++;
		modify_orig = 1;
	} else if (!strcasecmp(cmd, "DEC")) {       /* Decrement variable */
		int_value--;
		modify_orig = 1;
	}

	if (snprintf(returnvar, sizeof(returnvar), "%d", int_value) > 0) {
		pbx_builtin_setvar_helper(chan, data, returnvar);
		if (modify_orig) {
			ast_copy_string(buf, returnvar, len);
		}
		ret = 0;
	} else {
		pbx_builtin_setvar_helper(chan, data, "0");
		if (modify_orig) {
			ast_copy_string(buf, "0", len);
		}
		ast_log(LOG_NOTICE, "Variable %s refused to be %sREMENTED, setting value to 0", data, cmd);
		ret = 0;
	}

	ast_channel_unlock(chan);

	return ret;
}


static struct ast_custom_function math_function = {
	.name = "MATH",
	.read = math
};

static struct ast_custom_function increment_function = {
	.name = "INC",
	.read = crement_function_read,
};

static struct ast_custom_function decrement_function = {
	.name = "DEC",
	.read = crement_function_read,
};

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_MATH_function)
{
	enum ast_test_result_state res = AST_TEST_PASS;
	struct ast_str *expr, *result;

	switch (cmd) {
	case TEST_INIT:
		info->name = "test_MATH_function";
		info->category = "/main/pbx/";
		info->summary = "Test MATH function substitution";
		info->description =
			"Executes a series of variable substitutions using the MATH function and ensures that the expected results are received.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_status_update(test, "Testing MATH() substitution ...\n");

	if (!(expr = ast_str_create(16))) {
		return AST_TEST_FAIL;
	}
	if (!(result = ast_str_create(16))) {
		ast_free(expr);
		return AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${MATH(170 AND 63,i)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "42") != 0) {
		ast_test_status_update(test, "Expected result '42' not returned! ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_str_set(&expr, 0, "${MATH(170AND63,i)}");
	ast_str_substitute_variables(&result, 0, NULL, ast_str_buffer(expr));
	if (strcmp(ast_str_buffer(result), "42") != 0) {
		ast_test_status_update(test, "Expected result '42' not returned! ('%s')\n",
				ast_str_buffer(result));
		res = AST_TEST_FAIL;
	}

	ast_free(expr);
	ast_free(result);

	return res;
}
#endif

static int unload_module(void)
{
	int res = 0;

	res |= ast_custom_function_unregister(&math_function);
	res |= ast_custom_function_unregister(&increment_function);
	res |= ast_custom_function_unregister(&decrement_function);
	AST_TEST_UNREGISTER(test_MATH_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	res |= ast_custom_function_register(&math_function);
	res |= ast_custom_function_register(&increment_function);
	res |= ast_custom_function_register(&decrement_function);
	AST_TEST_REGISTER(test_MATH_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Mathematical dialplan function");
