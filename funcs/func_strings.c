/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005-2006, Digium, Inc.
 * Portions Copyright (C) 2005, Tilghman Lesher.  All rights reserved.
 * Portions Copyright (C) 2005, Anthony Minessale II
 * Portions Copyright (C) 2021, Naveen Albert
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
 * \author Anthony Minessale II
 * \author Naveen Albert
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <regex.h>
#include <ctype.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/localtime.h"
#include "asterisk/test.h"

AST_THREADSTORAGE(result_buf);
AST_THREADSTORAGE(tmp_buf);

/*** DOCUMENTATION
	<function name="FIELDQTY" language="en_US">
		<synopsis>
			Count the fields with an arbitrary delimiter
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="delim" required="true" />
		</syntax>
		<description>
			<para>The delimiter may be specified as a special or extended ASCII character, by encoding it.  The characters
			<literal>\n</literal>, <literal>\r</literal>, and <literal>\t</literal> are all recognized as the newline,
			carriage return, and tab characters, respectively.  Also, octal and hexadecimal specifications are recognized
			by the patterns <literal>\0nnn</literal> and <literal>\xHH</literal>, respectively.  For example, if you wanted
			to encode a comma as the delimiter, you could use either <literal>\054</literal> or <literal>\x2C</literal>.</para>
			<para>Example: If ${example} contains <literal>ex-amp-le</literal>, then ${FIELDQTY(example,-)} returns 3.</para>
		</description>
	</function>
	<function name="FIELDNUM" language="en_US">
		<synopsis>
			Return the 1-based offset of a field in a list
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="delim" required="true" />
			<parameter name="value" required="true" />
		</syntax>
		<description>
			<para>Search the variable named <replaceable>varname</replaceable> for the string <replaceable>value</replaceable>
			delimited by <replaceable>delim</replaceable> and return a 1-based offset as to its location. If not found
			or an error occured, return <literal>0</literal>.</para>
			<para>The delimiter may be specified as a special or extended ASCII character, by encoding it.  The characters
			<literal>\n</literal>, <literal>\r</literal>, and <literal>\t</literal> are all recognized as the newline,
			carriage return, and tab characters, respectively.  Also, octal and hexadecimal specifications are recognized
			by the patterns <literal>\0nnn</literal> and <literal>\xHH</literal>, respectively.  For example, if you wanted
			to encode a comma as the delimiter, you could use either <literal>\054</literal> or <literal>\x2C</literal>.</para>
		        <para>Example: If ${example} contains <literal>ex-amp-le</literal>, then ${FIELDNUM(example,-,amp)} returns 2.</para>
		</description>
	</function>
	<function name="LISTFILTER" language="en_US">
		<synopsis>Remove an item from a list, by name.</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="delim" required="true" default="," />
			<parameter name="value" required="true" />
		</syntax>
		<description>
			<para>Remove <replaceable>value</replaceable> from the list contained in the <replaceable>varname</replaceable>
			variable, where the list delimiter is specified by the <replaceable>delim</replaceable> parameter.  This is
			very useful for removing a single channel name from a list of channels, for example.</para>
		</description>
	</function>
	<function name="FILTER" language="en_US">
		<synopsis>
			Filter the string to include only the allowed characters
		</synopsis>
		<syntax>
			<parameter name="allowed-chars" required="true" />
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>Permits all characters listed in <replaceable>allowed-chars</replaceable>,
			filtering all others outs. In addition to literally listing the characters,
			you may also use ranges of characters (delimited by a <literal>-</literal></para>
			<para>Hexadecimal characters started with a <literal>\x</literal>(i.e. \x20)</para>
			<para>Octal characters started with a <literal>\0</literal> (i.e. \040)</para>
			<para>Also <literal>\t</literal>,<literal>\n</literal> and <literal>\r</literal> are recognized.</para>
			<note><para>If you want the <literal>-</literal> character it needs to be prefixed with a
			<literal>\</literal></para></note>
		</description>
	</function>
	<function name="REPLACE" language="en_US">
		<synopsis>
			Replace a set of characters in a given string with another character.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="find-chars" required="true" />
			<parameter name="replace-char" required="false" />
		</syntax>
		<description>
			<para>Iterates through a string replacing all the <replaceable>find-chars</replaceable> with
			<replaceable>replace-char</replaceable>.  <replaceable>replace-char</replaceable> may be either
			empty or contain one character.  If empty, all <replaceable>find-chars</replaceable> will be
			deleted from the output.</para>
			<note><para>The replacement only occurs in the output.  The original variable is not
			altered.</para></note>
		</description>
	</function>
	<function name="STRREPLACE" language="en_US">
		<synopsis>
			Replace instances of a substring within a string with another string.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="find-string" required="true" />
			<parameter name="replace-string" required="false" />
			<parameter name="max-replacements" required="false" />
		</syntax>
		<description>
			<para>Searches for all instances of the <replaceable>find-string</replaceable> in provided variable and
			replaces them with <replaceable>replace-string</replaceable>.  If <replaceable>replace-string</replaceable>
			is an empty string, this will effectively delete that substring.  If <replaceable>max-replacements</replaceable>
			is specified, this function will stop after performing replacements <replaceable>max-replacements</replaceable> times.</para>
			<note><para>The replacement only occurs in the output.  The original variable is not altered.</para></note>
		</description>
	</function>
	<function name="STRBETWEEN" language="en_US">
		<synopsis>
			Inserts a substring between each character in a string.
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="insert-string" required="true" />
		</syntax>
		<description>
			<para>Inserts a substring <replaceable>find-string</replaceable> between each character in
			<replaceable>varname</replaceable>.</para>
			<note><para>The replacement only occurs in the output.  The original variable is not altered.</para></note>
			<example title="Add half-second pause between dialed digits">
				same => n,Set(digits=5551212)
				same => n,SendDTMF(${STRBETWEEN(digits,w)) ; this will send 5w5w5w1w2w1w2
			</example>
		</description>
	</function>
	<function name="PASSTHRU" language="en_US">
		<synopsis>
			Pass the given argument back as a value.
		</synopsis>
		<syntax>
			<parameter name="string" required="false" />
		</syntax>
		<description>
			<para>Literally returns the given <replaceable>string</replaceable>.  The intent is to permit
			other dialplan functions which take a variable name as an argument to be able to take a literal
			string, instead.</para>
			<note><para>The functions which take a variable name need to be passed var and not
			${var}.  Similarly, use PASSTHRU() and not ${PASSTHRU()}.</para></note>
			<para>Example: ${CHANNEL} contains SIP/321-1</para>
			<para>         ${CUT(PASSTHRU(${CUT(CHANNEL,-,1)}),/,2)}) will return 321</para>
		</description>
	</function>
	<function name="REGEX" language="en_US">
		<synopsis>
			Check string against a regular expression.
		</synopsis>
		<syntax argsep=" ">
			<parameter name="&quot;regular expression&quot;" required="true" />
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>Return <literal>1</literal> on regular expression match or <literal>0</literal> otherwise</para>
			<para>Please note that the space following the double quotes separating the
			regex from the data is optional and if present, is skipped. If a space is
			desired at the beginning of the data, then put two spaces there; the second
			will not be skipped.</para>
		</description>
	</function>
	<application name="ClearHash" language="en_US">
		<synopsis>
			Clear the keys from a specified hashname.
		</synopsis>
		<syntax>
			<parameter name="hashname" required="true" />
		</syntax>
		<description>
			<para>Clears all keys out of the specified <replaceable>hashname</replaceable>.</para>
		</description>
	</application>
	<function name="HASH" language="en_US">
		<synopsis>
			Implementation of a dialplan associative array
		</synopsis>
		<syntax>
			<parameter name="hashname" required="true" />
			<parameter name="hashkey" />
		</syntax>
		<description>
			<para>In two arguments mode, gets and sets values to corresponding keys within
			a named associative array. The single-argument mode will only work when assigned
			to from a function defined by func_odbc</para>
		</description>
	</function>
	<function name="HASHKEYS" language="en_US">
		<synopsis>
			Retrieve the keys of the HASH() function.
		</synopsis>
		<syntax>
			<parameter name="hashname" required="true" />
		</syntax>
		<description>
			<para>Returns a comma-delimited list of the current keys of the associative array
			defined by the HASH() function. Note that if you iterate over the keys of
			the result, adding keys during iteration will cause the result of the HASHKEYS()
			function to change.</para>
		</description>
	</function>
	<function name="KEYPADHASH" language="en_US">
		<synopsis>
			Hash the letters in string into equivalent keypad numbers.
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>Example: ${KEYPADHASH(Les)} returns "537"</para>
		</description>
	</function>
	<function name="ARRAY" language="en_US">
		<synopsis>
			Allows setting multiple variables at once.
		</synopsis>
		<syntax>
			<parameter name="var1" required="true" />
			<parameter name="var2" required="false" multiple="true" />
			<parameter name="varN" required="false" />
		</syntax>
		<description>
			<para>The comma-delimited list passed as a value to which the function is set will
			be interpreted as a set of values to which the comma-delimited list of
			variable names in the argument should be set.</para>
			<para>Example: Set(ARRAY(var1,var2)=1,2) will set var1 to 1 and var2 to 2</para>
		</description>
	</function>
	<function name="STRPTIME" language="en_US">
		<synopsis>
			Returns the epoch of the arbitrary date/time string structured as described by the format.
		</synopsis>
		<syntax>
			<parameter name="datetime" required="true" />
			<parameter name="timezone" required="true" />
			<parameter name="format" required="true" />
		</syntax>
		<description>
			<para>This is useful for converting a date into <literal>EPOCH</literal> time,
			possibly to pass to an application like SayUnixTime or to calculate the difference
			between the two date strings</para>
			<para>Example: ${STRPTIME(2006-03-01 07:30:35,America/Chicago,%Y-%m-%d %H:%M:%S)} returns 1141219835</para>
		</description>
	</function>
	<function name="STRFTIME" language="en_US">
		<synopsis>
			Returns the current date/time in the specified format.
		</synopsis>
		<syntax>
			<parameter name="epoch" />
			<parameter name="timezone" />
			<parameter name="format" />
		</syntax>
		<description>
			<para>STRFTIME supports all of the same formats as the underlying C function
			<emphasis>strftime(3)</emphasis>.
			It also supports the following format: <literal>%[n]q</literal> - fractions of a second,
			with leading zeros.</para>
			<para>Example: <literal>%3q</literal> will give milliseconds and <literal>%1q</literal>
			will give tenths of a second. The default is set at milliseconds (n=3).
			The common case is to use it in combination with %S, as in <literal>%S.%3q</literal>.</para>
		</description>
		<see-also>
			<ref type="manpage">strftime(3)</ref>
		</see-also>
	</function>
	<function name="EVAL" language="en_US">
		<synopsis>
			Evaluate stored variables
		</synopsis>
		<syntax>
			<parameter name="variable" required="true" />
		</syntax>
		<description>
			<para>Using EVAL basically causes a string to be evaluated twice.
			When a variable or expression is in the dialplan, it will be
			evaluated at runtime. However, if the results of the evaluation
			is in fact another variable or expression, using EVAL will have it
			evaluated a second time.</para>
			<para>Example: If the <variable>MYVAR</variable> contains
			<variable>OTHERVAR</variable>, then the result of ${EVAL(
			<variable>MYVAR</variable>)} in the dialplan will be the
			contents of <variable>OTHERVAR</variable>. Normally just
			putting <variable>MYVAR</variable> in the dialplan the result
			would be <variable>OTHERVAR</variable>.</para>
		</description>
	</function>
	<function name="TOUPPER" language="en_US">
		<synopsis>
			Convert string to all uppercase letters.
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>Example: ${TOUPPER(Example)} returns "EXAMPLE"</para>
		</description>
	</function>
	<function name="TOLOWER" language="en_US">
		<synopsis>
			Convert string to all lowercase letters.
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>Example: ${TOLOWER(Example)} returns "example"</para>
		</description>
	</function>
	<function name="LEN" language="en_US">
		<synopsis>
			Return the length of the string given.
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>Example: ${LEN(example)} returns 7</para>
		</description>
	</function>
	<function name="QUOTE" language="en_US">
		<synopsis>
			Quotes a given string, escaping embedded quotes as necessary
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>Example: ${QUOTE(ab"c"de)} will return ""ab\"c\"de""</para>
		</description>
	</function>
	<function name="CSV_QUOTE" language="en_US">
		<synopsis>
			Quotes a given string for use in a CSV file, escaping embedded quotes as necessary
		</synopsis>
		<syntax>
			<parameter name="string" required="true" />
		</syntax>
		<description>
			<para>Example: ${CSV_QUOTE("a,b" 123)} will return """a,b"" 123"</para>
		</description>
	</function>
	<function name="SHIFT" language="en_US">
		<synopsis>
			Removes and returns the first item off of a variable containing delimited text
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="delimiter" required="false" default="," />
		</syntax>
		<description>
			<para>Example:</para>
			<para>exten => s,1,Set(array=one,two,three)</para>
			<para>exten => s,n,While($["${SET(var=${SHIFT(array)})}" != ""])</para>
			<para>exten => s,n,NoOp(var is ${var})</para>
			<para>exten => s,n,EndWhile</para>
			<para>This would iterate over each value in array, left to right, and
				would result in NoOp(var is one), NoOp(var is two), and
				NoOp(var is three) being executed.
			</para>
		</description>
	</function>
	<function name="POP" language="en_US">
		<synopsis>
			Removes and returns the last item off of a variable containing delimited text
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="delimiter" required="false" default="," />
		</syntax>
		<description>
			<para>Example:</para>
			<para>exten => s,1,Set(array=one,two,three)</para>
			<para>exten => s,n,While($["${SET(var=${POP(array)})}" != ""])</para>
			<para>exten => s,n,NoOp(var is ${var})</para>
			<para>exten => s,n,EndWhile</para>
			<para>This would iterate over each value in array, right to left, and
				would result in NoOp(var is three), NoOp(var is two), and
				NoOp(var is one) being executed.
			</para>
		</description>
	</function>
	<function name="PUSH" language="en_US">
		<synopsis>
			Appends one or more values to the end of a variable containing delimited text
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="delimiter" required="false" default="," />
		</syntax>
		<description>
			<para>Example: Set(PUSH(array)=one,two,three) would append one,
				two, and three to the end of the values stored in the variable
				"array".
			</para>
		</description>
	</function>
	<function name="UNSHIFT" language="en_US">
		<synopsis>
			Inserts one or more values to the beginning of a variable containing delimited text
		</synopsis>
		<syntax>
			<parameter name="varname" required="true" />
			<parameter name="delimiter" required="false" default="," />
		</syntax>
		<description>
			<para>Example: Set(UNSHIFT(array)=one,two,three) would insert one,
				two, and three before the values stored in the variable
				"array".
			</para>
		</description>
	</function>
 ***/

static int function_fieldqty_helper(struct ast_channel *chan, const char *cmd,
			     char *parse, char *buf, struct ast_str **sbuf, ssize_t len)
{
	char *varsubst;
	struct ast_str *str = ast_str_thread_get(&result_buf, 16);
	int fieldcount = 0;
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(varname);
			     AST_APP_ARG(delim);
		);
	char delim[2] = "";
	size_t delim_used;

	if (!str) {
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);
	if (args.delim) {
		ast_get_encoded_char(args.delim, delim, &delim_used);

		varsubst = ast_alloca(strlen(args.varname) + 4);

		sprintf(varsubst, "${%s}", args.varname);
		ast_str_substitute_variables(&str, 0, chan, varsubst);
		if (ast_str_strlen(str) == 0) {
			fieldcount = 0;
		} else {
			char *varval = ast_str_buffer(str);
			while (strsep(&varval, delim)) {
				fieldcount++;
			}
		}
	} else {
		fieldcount = 1;
	}
	if (sbuf) {
		ast_str_set(sbuf, len, "%d", fieldcount);
	} else {
		snprintf(buf, len, "%d", fieldcount);
	}

	return 0;
}

static int function_fieldqty(struct ast_channel *chan, const char *cmd,
			     char *parse, char *buf, size_t len)
{
	return function_fieldqty_helper(chan, cmd, parse, buf, NULL, len);
}

static int function_fieldqty_str(struct ast_channel *chan, const char *cmd,
				 char *parse, struct ast_str **buf, ssize_t len)
{
	return function_fieldqty_helper(chan, cmd, parse, NULL, buf, len);
}

static struct ast_custom_function fieldqty_function = {
	.name = "FIELDQTY",
	.read = function_fieldqty,
	.read2 = function_fieldqty_str,
};

static int function_fieldnum_helper(struct ast_channel *chan, const char *cmd,
				char *parse, char *buf, struct ast_str **sbuf, ssize_t len)
{
	char *varsubst, *field;
	struct ast_str *str = ast_str_thread_get(&result_buf, 16);
	int fieldindex = 0, res = 0;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(varname);
		AST_APP_ARG(delim);
		AST_APP_ARG(field);
	);
	char delim[2] = "";
	size_t delim_used;

	if (!str) {
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc < 3) {
		ast_log(LOG_ERROR, "Usage: FIELDNUM(<listname>,<delimiter>,<fieldvalue>)\n");
		res = -1;
	} else {
		varsubst = ast_alloca(strlen(args.varname) + 4);
		sprintf(varsubst, "${%s}", args.varname);

		ast_str_substitute_variables(&str, 0, chan, varsubst);

		if (ast_str_strlen(str) == 0 || ast_strlen_zero(args.delim)) {
			fieldindex = 0;
		} else if (ast_get_encoded_char(args.delim, delim, &delim_used) == -1) {
			res = -1;
		} else {
			char *varval = ast_str_buffer(str);

			while ((field = strsep(&varval, delim)) != NULL) {
				fieldindex++;

				if (!strcasecmp(field, args.field)) {
					break;
				}
			}

			if (!field) {
				fieldindex = 0;
			}

			res = 0;
		}
	}

	if (sbuf) {
		ast_str_set(sbuf, len, "%d", fieldindex);
	} else {
		snprintf(buf, len, "%d", fieldindex);
	}

	return res;
}

static int function_fieldnum(struct ast_channel *chan, const char *cmd,
			     char *parse, char *buf, size_t len)
{
	return function_fieldnum_helper(chan, cmd, parse, buf, NULL, len);
}

static int function_fieldnum_str(struct ast_channel *chan, const char *cmd,
				 char *parse, struct ast_str **buf, ssize_t len)
{
	return function_fieldnum_helper(chan, cmd, parse, NULL, buf, len);
}

static struct ast_custom_function fieldnum_function = {
	.name = "FIELDNUM",
	.read = function_fieldnum,
	.read2 = function_fieldnum_str,
};

static int listfilter(struct ast_channel *chan, const char *cmd, char *parse, char *buf, struct ast_str **bufstr, ssize_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(listname);
		AST_APP_ARG(delimiter);
		AST_APP_ARG(fieldvalue);
	);
	struct ast_str *orig_list = ast_str_thread_get(&tmp_buf, 16);
	const char *begin, *cur, *next;
	int dlen, flen, first = 1;
	struct ast_str *result, **result_ptr = &result;
	char *delim, *varsubst;

	AST_STANDARD_APP_ARGS(args, parse);

	if (buf) {
		if (!(result = ast_str_thread_get(&result_buf, 16))) {
			return -1;
		}
	} else {
		/* Place the result directly into the output buffer */
		result_ptr = bufstr;
	}

	if (args.argc < 3) {
		ast_log(LOG_ERROR, "Usage: LISTFILTER(<listname>,<delimiter>,<fieldvalue>)\n");
		return -1;
	}

	varsubst = ast_alloca(strlen(args.listname) + 4);
	sprintf(varsubst, "${%s}", args.listname);

	/* If we don't lock the channel, the variable could disappear out from underneath us. */
	if (chan) {
		ast_channel_lock(chan);
	}
	ast_str_substitute_variables(&orig_list, 0, chan, varsubst);
	if (!ast_str_strlen(orig_list)) {
		if (chan) {
			ast_channel_unlock(chan);
		}
		return -1;
	}

	/* If the string isn't there, just copy out the string and be done with it. */
	if (!strstr(ast_str_buffer(orig_list), args.fieldvalue)) {
		if (buf) {
			ast_copy_string(buf, ast_str_buffer(orig_list), len);
		} else {
			ast_str_set(result_ptr, len, "%s", ast_str_buffer(orig_list));
		}
		if (chan) {
			ast_channel_unlock(chan);
		}
		return 0;
	}

	dlen = strlen(args.delimiter);
	delim = ast_alloca(dlen + 1);
	ast_get_encoded_str(args.delimiter, delim, dlen + 1);

	if ((dlen = strlen(delim)) == 0) {
		delim = ",";
		dlen = 1;
	}

	flen = strlen(args.fieldvalue);

	ast_str_reset(*result_ptr);
	/* Enough space for any result */
	if (len > -1) {
		ast_str_make_space(result_ptr, len ? len : ast_str_strlen(orig_list) + 1);
	}

	begin = ast_str_buffer(orig_list);
	next = strstr(begin, delim);

	do {
		/* Find next boundary */
		if (next) {
			cur = next;
			next = strstr(cur + dlen, delim);
		} else {
			cur = strchr(begin + dlen, '\0');
		}

		if (flen == cur - begin && !strncmp(begin, args.fieldvalue, flen)) {
			/* Skip field */
			begin += flen + dlen;
		} else {
			/* Copy field to output */
			if (!first) {
				ast_str_append(result_ptr, len, "%s", delim);
			}

			ast_str_append_substr(result_ptr, len, begin, cur - begin);
			first = 0;
			begin = cur + dlen;
		}
	} while (*cur != '\0');
	if (chan) {
		ast_channel_unlock(chan);
	}

	if (buf) {
		ast_copy_string(buf, ast_str_buffer(result), len);
	}

	return 0;
}

static int listfilter_read(struct ast_channel *chan, const char *cmd, char *parse, char *buf, size_t len)
{
	return listfilter(chan, cmd, parse, buf, NULL, len);
}

static int listfilter_read2(struct ast_channel *chan, const char *cmd, char *parse, struct ast_str **buf, ssize_t len)
{
	return listfilter(chan, cmd, parse, NULL, buf, len);
}

static struct ast_custom_function listfilter_function = {
	.name = "LISTFILTER",
	.read = listfilter_read,
	.read2 = listfilter_read2,
};

static int filter(struct ast_channel *chan, const char *cmd, char *parse, char *buf,
		  size_t len)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(allowed);
			     AST_APP_ARG(string);
	);
	char *outbuf = buf;
	unsigned char ac;
	char allowed[256] = "";
	size_t allowedlen = 0;
	int32_t bitfield[8] = { 0, }; /* 256 bits */

	AST_STANDARD_RAW_ARGS(args, parse);

	if (!args.string) {
		ast_log(LOG_ERROR, "Usage: FILTER(<allowed-chars>,<string>)\n");
		return -1;
	}

	if (args.allowed[0] == '"' && !ast_opt_dont_warn) {
		ast_log(LOG_WARNING, "FILTER allowed characters includes the quote (\") character.  This may not be what you want.\n");
	}

	/* Expand ranges */
	for (; *(args.allowed);) {
		char c1 = 0, c2 = 0;
		size_t consumed = 0;

		if (ast_get_encoded_char(args.allowed, &c1, &consumed))
			return -1;
		args.allowed += consumed;

		if (*(args.allowed) == '-') {
			if (ast_get_encoded_char(args.allowed + 1, &c2, &consumed))
				c2 = c1;
			args.allowed += consumed + 1;

			if ((unsigned char) c2 < (unsigned char) c1 && !ast_opt_dont_warn) {
				ast_log(LOG_WARNING, "Range wrapping in FILTER(%s,%s).  This may not be what you want.\n", parse, args.string);
			}

			/*!\note
			 * Looks a little strange, until you realize that we can overflow
			 * the size of a char.
			 */
			for (ac = (unsigned char) c1; ac != (unsigned char) c2; ac++) {
				bitfield[ac / 32] |= 1 << (ac % 32);
			}
			bitfield[ac / 32] |= 1 << (ac % 32);

			ast_debug(4, "c1=%d, c2=%d\n", c1, c2);
		} else {
			ac = (unsigned char) c1;
			ast_debug(4, "c1=%d, consumed=%d, args.allowed=%s\n", c1, (int) consumed, args.allowed - consumed);
			bitfield[ac / 32] |= 1 << (ac % 32);
		}
	}

	for (ac = 1; ac != 0; ac++) {
		if (bitfield[ac / 32] & (1 << (ac % 32))) {
			allowed[allowedlen++] = ac;
		}
	}

	ast_debug(1, "Allowed: %s\n", allowed);

	for (; *(args.string) && (buf + len - 1 > outbuf); (args.string)++) {
		if (strchr(allowed, *(args.string)))
			*outbuf++ = *(args.string);
	}
	*outbuf = '\0';

	return 0;
}

static struct ast_custom_function filter_function = {
	.name = "FILTER",
	.read = filter,
};

static int replace(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(varname);
		AST_APP_ARG(find);
		AST_APP_ARG(replace);
	);
	char *strptr, *varsubst;
	RAII_VAR(struct ast_str *, str, ast_str_create(16), ast_free);
	char find[256]; /* Only 256 characters possible */
	char replace[2] = "";
	size_t unused;

	AST_STANDARD_APP_ARGS(args, data);

	if (!str) {
		return -1;
	}

	if (args.argc < 2) {
		ast_log(LOG_ERROR, "Usage: %s(<varname>,<search-chars>[,<replace-char>])\n", cmd);
		return -1;
	}

	/* Decode escapes */
	ast_get_encoded_str(args.find, find, sizeof(find));
	ast_get_encoded_char(args.replace, replace, &unused);

	if (ast_strlen_zero(find) || ast_strlen_zero(args.varname)) {
		ast_log(LOG_ERROR, "The characters to search for and the variable name must not be empty.\n");
		return -1;
	}

	varsubst = ast_alloca(strlen(args.varname) + 4);
	sprintf(varsubst, "${%s}", args.varname);
	ast_str_substitute_variables(&str, 0, chan, varsubst);

	if (!ast_str_strlen(str)) {
		/* Blank, nothing to replace */
		return -1;
	}

	ast_debug(3, "String to search: (%s)\n", ast_str_buffer(str));
	ast_debug(3, "Characters to find: (%s)\n", find);
	ast_debug(3, "Character to replace with: (%s)\n", replace);

	for (strptr = ast_str_buffer(str); *strptr; strptr++) {
		/* buf is already a mutable buffer, so we construct the result
		 * directly there */
		if (strchr(find, *strptr)) {
			if (ast_strlen_zero(replace)) {
				memmove(strptr, strptr + 1, strlen(strptr + 1) + 1);
				strptr--;
			} else {
				/* Replace character */
				*strptr = *replace;
			}
		}
	}

	ast_str_set(buf, len, "%s", ast_str_buffer(str));
	return 0;
}

static struct ast_custom_function replace_function = {
	.name = "REPLACE",
	.read2 = replace,
};

static int strreplace(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	char *varsubstr; /* substring for input var */
	char *start; /* Starting pos of substring search. */
	char *end; /* Ending pos of substring search. */
	int find_size; /* length of given find-string */
	unsigned max_matches; /* number of matches we find before terminating search */
	unsigned count; /* loop counter */
	struct ast_str *str = ast_str_thread_get(&result_buf, 16); /* Holds the data obtained from varname */

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(varname);
		AST_APP_ARG(find_string);
		AST_APP_ARG(replace_string);
		AST_APP_ARG(max_replacements);
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);

	/* Guarantee output string is empty to start with. */
	ast_str_reset(*buf);

	if (!str) {
		/* We failed to allocate str, forget it.  We failed. */
		return -1;
	}

	/* Parse the arguments. */
	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc < 2) {
		/* Didn't receive enough arguments to do anything */
		ast_log(LOG_ERROR,
			"Usage: %s(<varname>,<find-string>[,<replace-string>,[<max-replacements>]])\n",
			cmd);
		return -1;
	}

	/* No var name specified. Return failure, string is already empty. */
	if (ast_strlen_zero(args.varname)) {
		return -1;
	}

	/* Zero length find strings are a no-no. Kill the function if we run into one. */
	if (ast_strlen_zero(args.find_string)) {
		ast_log(LOG_ERROR, "No <find-string> specified\n");
		return -1;
	}
	find_size = strlen(args.find_string);

	/* set varsubstr to the matching variable */
	varsubstr = ast_alloca(strlen(args.varname) + 4);
	sprintf(varsubstr, "${%s}", args.varname);
	ast_str_substitute_variables(&str, 0, chan, varsubstr);

	/* Determine how many replacements are allowed. */
	if (!args.max_replacements
		|| (max_matches = atoi(args.max_replacements)) <= 0) {
		/* Unlimited replacements are allowed. */
		max_matches = -1;
	}

	/* Generate the search and replaced string. */
	start = ast_str_buffer(str);
	for (count = 0; count < max_matches; ++count) {
		end = strstr(start, args.find_string);
		if (!end) {
			/* Did not find a matching substring in the remainder. */
			break;
		}

		/* Replace the found substring. */
		*end = '\0';
		ast_str_append(buf, len, "%s", start);
		if (args.replace_string) {
			/* Append the replacement string */
			ast_str_append(buf, len, "%s", args.replace_string);
		}
		start = end + find_size;
	}
	ast_str_append(buf, len, "%s", start);

	return 0;
}

static struct ast_custom_function strreplace_function = {
	.name = "STRREPLACE",
	.read2 = strreplace,
};

static int strbetween(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	int c, origsize;
	char *varsubstr, *origstr;
	struct ast_str *str = ast_str_thread_get(&result_buf, 16); /* Holds the data obtained from varname */

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(varname);
		AST_APP_ARG(insert_string);
		AST_APP_ARG(other);	/* Any remining unused arguments */
	);

	ast_str_reset(*buf);

	if (!str) {
		ast_log(LOG_ERROR, "Couldn't obtain string\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (args.argc != 2 || ast_strlen_zero(args.varname)) {
		ast_log(LOG_ERROR, "Usage: %s(<varname>,<insert-string>)\n", cmd);
		return -1;
	}

	varsubstr = ast_alloca(strlen(args.varname) + 4);
	sprintf(varsubstr, "${%s}", args.varname);
	ast_str_substitute_variables(&str, 0, chan, varsubstr);
	origstr = ast_str_buffer(str);
	origsize = strlen(origstr);
	for (c = 0; c < origsize; c++) {
		ast_str_append(buf, len, "%c", origstr[c]);
		/* no insert after the last character */
		if (c < (origsize - 1)) {
			ast_str_append(buf, len, "%s", args.insert_string);
		}
	}

	return 0;
}

static struct ast_custom_function strbetween_function = {
	.name = "STRBETWEEN",
	.read2 = strbetween,
};

static int regex(struct ast_channel *chan, const char *cmd, char *parse, char *buf,
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

	ast_debug(1, "FUNCTION REGEX (%s)(%s)\n", args.reg, args.str);

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
	.read = regex,
};

#define HASH_PREFIX	"~HASH~%s~"
#define HASH_FORMAT	HASH_PREFIX "%s~"

static char *app_clearhash = "ClearHash";

/* This function probably should migrate to main/pbx.c, as pbx_builtin_clearvar_prefix() */
static void clearvar_prefix(struct ast_channel *chan, const char *prefix)
{
	struct ast_var_t *var;
	int len = strlen(prefix);
	AST_LIST_TRAVERSE_SAFE_BEGIN(ast_channel_varshead(chan), var, entries) {
		if (strncmp(prefix, ast_var_name(var), len) == 0) {
			AST_LIST_REMOVE_CURRENT(entries);
			ast_free(var);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END
}

static int exec_clearhash(struct ast_channel *chan, const char *data)
{
	char prefix[80];
	snprintf(prefix, sizeof(prefix), HASH_PREFIX, data ? (char *)data : "null");
	clearvar_prefix(chan, prefix);
	return 0;
}

static int array(struct ast_channel *chan, const char *cmd, char *var,
		 const char *value)
{
	AST_DECLARE_APP_ARGS(arg1,
			     AST_APP_ARG(var)[100];
	);
	AST_DECLARE_APP_ARGS(arg2,
			     AST_APP_ARG(val)[100];
	);
	char *origvar = "", *value2, varname[256];
	int i, ishash = 0;

	if (!var) {
		return -1;
	}
	value2 = ast_strdupa(value);

	if (!strcmp(cmd, "HASH")) {
		const char *var2 = pbx_builtin_getvar_helper(chan, "~ODBCFIELDS~");
		origvar = var;
		if (var2)
			var = ast_strdupa(var2);
		else {
			if (chan)
				ast_autoservice_stop(chan);
			return -1;
		}
		ishash = 1;
	}

	/* The functions this will generally be used with are SORT and ODBC_*, which
	 * both return comma-delimited lists.  However, if somebody uses literal lists,
	 * their commas will be translated to vertical bars by the load, and I don't
	 * want them to be surprised by the result.  Hence, we prefer commas as the
	 * delimiter, but we'll fall back to vertical bars if commas aren't found.
	 */
	ast_debug(1, "array (%s=%s)\n", var, S_OR(value2, ""));
	AST_STANDARD_APP_ARGS(arg1, var);

	AST_STANDARD_APP_ARGS(arg2, value2);

	for (i = 0; i < arg1.argc; i++) {
		ast_debug(1, "array set value (%s=%s)\n", arg1.var[i],
				S_OR(arg2.val[i], ""));
		if (i < arg2.argc) {
			if (ishash) {
				if (origvar[0] == '_') {
					if (origvar[1] == '_') {
						snprintf(varname, sizeof(varname), "__" HASH_FORMAT, origvar + 2, arg1.var[i]);
					} else {
						snprintf(varname, sizeof(varname), "_" HASH_FORMAT, origvar + 1, arg1.var[i]);
					}
				} else {
					snprintf(varname, sizeof(varname), HASH_FORMAT, origvar, arg1.var[i]);
				}

				pbx_builtin_setvar_helper(chan, varname, arg2.val[i]);
			} else {
				pbx_builtin_setvar_helper(chan, arg1.var[i], arg2.val[i]);
			}
		} else {
			/* We could unset the variable, by passing a NULL, but due to
			 * pushvar semantics, that could create some undesired behavior. */
			if (ishash) {
				snprintf(varname, sizeof(varname), HASH_FORMAT, origvar, arg1.var[i]);
				pbx_builtin_setvar_helper(chan, varname, "");
			} else {
				pbx_builtin_setvar_helper(chan, arg1.var[i], "");
			}
		}
	}

	return 0;
}

static const char *get_key(const struct ast_str *prefix, const struct ast_var_t *var)
{
	const char *prefix_name = ast_str_buffer(prefix);
	const char *var_name = ast_var_name(var);
	int prefix_len;
	int var_len;

	if (ast_strlen_zero(var_name)) {
		return NULL;
	}

	prefix_len = ast_str_strlen(prefix);
	var_len = strlen(var_name);

	/*
	 * Make sure we only match on non-empty, hash function created keys. If valid
	 * then return a pointer to the variable that's just after the prefix.
	 */
	return var_len > (prefix_len + 1) && var_name[var_len - 1] == '~' &&
		strncmp(prefix_name, var_name, prefix_len) == 0 ? var_name + prefix_len : NULL;
}

static int hashkeys_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_var_t *newvar;
	struct ast_str *prefix = ast_str_alloca(80);
	size_t buf_len;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_str_set(&prefix, -1, HASH_PREFIX, data);
	memset(buf, 0, len);

	AST_LIST_TRAVERSE(ast_channel_varshead(chan), newvar, entries) {
		const char *key = get_key(prefix, newvar);

		if (key) {
			strncat(buf, key, len - strlen(buf) - 1);
			/* Replace the trailing ~ */
			buf[strlen(buf) - 1] = ',';
		}
	}
	/* Trim the trailing comma */
	buf_len = strlen(buf);
	if (buf_len) {
		buf[buf_len - 1] = '\0';
	}
	return 0;
}

static int hashkeys_read2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	struct ast_var_t *newvar;
	struct ast_str *prefix = ast_str_alloca(80);

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_str_set(&prefix, -1, HASH_PREFIX, data);

	AST_LIST_TRAVERSE(ast_channel_varshead(chan), newvar, entries) {
		const char *key = get_key(prefix, newvar);

		if (key) {
			char *tmp;

			ast_str_append(buf, len, "%s", key);
			/* Replace the trailing ~ */
			tmp = ast_str_buffer(*buf);
			tmp[ast_str_strlen(*buf) - 1] = ',';
		}
	}

	ast_str_truncate(*buf, -1);
	return 0;
}

static int hash_write(struct ast_channel *chan, const char *cmd, char *var, const char *value)
{
	char varname[256];
	AST_DECLARE_APP_ARGS(arg,
		AST_APP_ARG(hashname);
		AST_APP_ARG(hashkey);
	);

	if (!strchr(var, ',')) {
		/* Single argument version */
		return array(chan, "HASH", var, value);
	}

	AST_STANDARD_APP_ARGS(arg, var);
	if (arg.hashname[0] == '_') {
		if (arg.hashname[1] == '_') {
			snprintf(varname, sizeof(varname), "__" HASH_FORMAT, arg.hashname + 2, arg.hashkey);
		} else {
			snprintf(varname, sizeof(varname), "_" HASH_FORMAT, arg.hashname + 1, arg.hashkey);
		}
	} else {
		snprintf(varname, sizeof(varname), HASH_FORMAT, arg.hashname, arg.hashkey);
	}
	pbx_builtin_setvar_helper(chan, varname, value);

	return 0;
}

static int hash_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char varname[256];
	const char *varvalue;
	AST_DECLARE_APP_ARGS(arg,
		AST_APP_ARG(hashname);
		AST_APP_ARG(hashkey);
	);

	AST_STANDARD_APP_ARGS(arg, data);
	if (arg.argc == 2) {
		snprintf(varname, sizeof(varname), HASH_FORMAT, arg.hashname, arg.hashkey);
		varvalue = pbx_builtin_getvar_helper(chan, varname);
		if (varvalue)
			ast_copy_string(buf, varvalue, len);
		else
			*buf = '\0';
	} else if (arg.argc == 1) {
		char colnames[4096];
		int i;
		AST_DECLARE_APP_ARGS(arg2,
			AST_APP_ARG(col)[100];
		);

		if (!chan) {
			ast_log(LOG_WARNING, "No channel and only 1 parameter was provided to %s function.\n", cmd);
			return -1;
		}

		/* Get column names, in no particular order */
		hashkeys_read(chan, "HASHKEYS", arg.hashname, colnames, sizeof(colnames));
		pbx_builtin_setvar_helper(chan, "~ODBCFIELDS~", colnames);

		AST_STANDARD_APP_ARGS(arg2, colnames);
		*buf = '\0';

		/* Now get the corresponding column values, in exactly the same order */
		for (i = 0; i < arg2.argc; i++) {
			snprintf(varname, sizeof(varname), HASH_FORMAT, arg.hashname, arg2.col[i]);
			varvalue = pbx_builtin_getvar_helper(chan, varname);
			strncat(buf, varvalue, len - strlen(buf) - 1);
			strncat(buf, ",", len - strlen(buf) - 1);
		}

		/* Strip trailing comma */
		buf[strlen(buf) - 1] = '\0';
	}

	return 0;
}

static struct ast_custom_function hash_function = {
	.name = "HASH",
	.write = hash_write,
	.read = hash_read,
};

static struct ast_custom_function hashkeys_function = {
	.name = "HASHKEYS",
	.read = hashkeys_read,
	.read2 = hashkeys_read2,
};

static struct ast_custom_function array_function = {
	.name = "ARRAY",
	.write = array,
};

static int quote(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *bufptr = buf, *dataptr = data;

	if (len < 3){ /* at least two for quotes and one for binary zero */
		ast_log(LOG_ERROR, "Not enough buffer\n");
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "No argument specified!\n");
		ast_copy_string(buf, "\"\"", len);
		return 0;
	}

	*bufptr++ = '"';
	for (; bufptr < buf + len - 3; dataptr++) {
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
	.read = quote,
};

static int csv_quote(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	char *bufptr = buf, *dataptr = data;

	if (len < 3) { /* at least two for quotes and one for binary zero */
		ast_log(LOG_ERROR, "Not enough buffer\n");
		return -1;
	}

	if (ast_strlen_zero(data)) {
		ast_copy_string(buf, "\"\"", len);
		return 0;
	}

	*bufptr++ = '"';
	for (; bufptr < buf + len - 3; dataptr++){
		if (*dataptr == '"') {
			*bufptr++ = '"';
			*bufptr++ = '"';
		} else if (*dataptr == '\0') {
			break;
		} else {
			*bufptr++ = *dataptr;
		}
	}
	*bufptr++ = '"';
	*bufptr='\0';
	return 0;
}

static struct ast_custom_function csv_quote_function = {
	.name = "CSV_QUOTE",
	.read = csv_quote,
};

static int len(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t buflen)
{
	int length = 0;

	if (data)
		length = strlen(data);

	snprintf(buf, buflen, "%d", length);

	return 0;
}

static struct ast_custom_function len_function = {
	.name = "LEN",
	.read = len,
	.read_max = 12,
};

static int acf_strftime(struct ast_channel *chan, const char *cmd, char *parse,
			char *buf, size_t buflen)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(epoch);
			     AST_APP_ARG(timezone);
			     AST_APP_ARG(format);
	);
	struct timeval when;
	struct ast_tm tm;

	buf[0] = '\0';

	AST_STANDARD_APP_ARGS(args, parse);

	ast_get_timeval(args.epoch, &when, ast_tvnow(), NULL);
	ast_localtime(&when, &tm, args.timezone);

	if (!args.format)
		args.format = "%c";

	if (ast_strftime(buf, buflen, args.format, &tm) <= 0)
		ast_log(LOG_WARNING, "C function strftime() output nothing?!!\n");

	buf[buflen - 1] = '\0';

	return 0;
}

static struct ast_custom_function strftime_function = {
	.name = "STRFTIME",
	.read = acf_strftime,
};

static int acf_strptime(struct ast_channel *chan, const char *cmd, char *data,
			char *buf, size_t buflen)
{
	AST_DECLARE_APP_ARGS(args,
			     AST_APP_ARG(timestring);
			     AST_APP_ARG(timezone);
			     AST_APP_ARG(format);
	);
	struct ast_tm tm;

	buf[0] = '\0';

	if (!data) {
		ast_log(LOG_ERROR,
				"Asterisk function STRPTIME() requires an argument.\n");
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.format)) {
		ast_log(LOG_ERROR,
				"No format supplied to STRPTIME(<timestring>,<timezone>,<format>)");
		return -1;
	}

	if (!ast_strptime(args.timestring, args.format, &tm)) {
		ast_log(LOG_WARNING, "STRPTIME() found no time specified within the string\n");
	} else {
		struct timeval when;
		when = ast_mktime(&tm, args.timezone);
		snprintf(buf, buflen, "%d", (int) when.tv_sec);
	}

	return 0;
}

static struct ast_custom_function strptime_function = {
	.name = "STRPTIME",
	.read = acf_strptime,
};

static int function_eval(struct ast_channel *chan, const char *cmd, char *data,
			 char *buf, size_t buflen)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "EVAL requires an argument: EVAL(<string>)\n");
		return -1;
	}

	pbx_substitute_variables_helper(chan, data, buf, buflen - 1);

	return 0;
}

static int function_eval2(struct ast_channel *chan, const char *cmd, char *data,
			 struct ast_str **buf, ssize_t buflen)
{
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "EVAL requires an argument: EVAL(<string>)\n");
		return -1;
	}

	ast_str_substitute_variables(buf, buflen, chan, data);

	return 0;
}

static struct ast_custom_function eval_function = {
	.name = "EVAL",
	.read = function_eval,
	.read2 = function_eval2,
};

static int keypadhash(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t buflen)
{
	char *bufptr, *dataptr;

	for (bufptr = buf, dataptr = data; bufptr < buf + buflen - 1; dataptr++) {
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
	buf[buflen - 1] = '\0';

	return 0;
}

static struct ast_custom_function keypadhash_function = {
	.name = "KEYPADHASH",
	.read = keypadhash,
};

static int string_toupper(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t buflen)
{
	char *bufptr = buf, *dataptr = data;

	while ((bufptr < buf + buflen - 1) && (*bufptr++ = toupper(*dataptr++)));

	return 0;
}

static int string_toupper2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t buflen)
{
	char *bufptr, *dataptr = data;

	if (buflen > -1) {
		ast_str_make_space(buf, buflen > 0 ? buflen : strlen(data) + 1);
	}
	bufptr = ast_str_buffer(*buf);
	while ((bufptr < ast_str_buffer(*buf) + ast_str_size(*buf) - 1) && (*bufptr++ = toupper(*dataptr++)));
	ast_str_update(*buf);

	return 0;
}

static struct ast_custom_function toupper_function = {
	.name = "TOUPPER",
	.read = string_toupper,
	.read2 = string_toupper2,
};

static int string_tolower(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t buflen)
{
	char *bufptr = buf, *dataptr = data;

	while ((bufptr < buf + buflen - 1) && (*bufptr++ = tolower(*dataptr++)));

	return 0;
}

static int string_tolower2(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t buflen)
{
	char *bufptr, *dataptr = data;

	if (buflen > -1) {
		ast_str_make_space(buf, buflen > 0 ? buflen : strlen(data) + 1);
	}
	bufptr = ast_str_buffer(*buf);
	while ((bufptr < ast_str_buffer(*buf) + ast_str_size(*buf) - 1) && (*bufptr++ = tolower(*dataptr++)));
	ast_str_update(*buf);

	return 0;
}

static struct ast_custom_function tolower_function = {
	.name = "TOLOWER",
	.read = string_tolower,
	.read2 = string_tolower2,
};

static int shift_pop(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
#define beginning	(cmd[0] == 'S') /* SHIFT */
	char *after, delimiter[2] = ",", *varsubst;
	size_t unused;
	struct ast_str *before = ast_str_thread_get(&result_buf, 16);
	char *(*search_func)(const char *s, int c) = (beginning ? strchr : strrchr);
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(var);
		AST_APP_ARG(delimiter);
	);

	if (!before) {
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.var)) {
		ast_log(LOG_WARNING, "%s requires a variable name\n", cmd);
		return -1;
	}

	varsubst = ast_alloca(strlen(args.var) + 4);
	sprintf(varsubst, "${%s}", args.var);
	ast_str_substitute_variables(&before, 0, chan, varsubst);

	if (args.argc > 1 && !ast_strlen_zero(args.delimiter)) {
		ast_get_encoded_char(args.delimiter, delimiter, &unused);
	}

	if (!ast_str_strlen(before)) {
		/* Nothing to pop */
		return -1;
	}

	if (!(after = search_func(ast_str_buffer(before), delimiter[0]))) {
		/* Only one entry in array */
		ast_str_set(buf, len, "%s", ast_str_buffer(before));
		pbx_builtin_setvar_helper(chan, args.var, "");
	} else {
		*after++ = '\0';
		ast_str_set(buf, len, "%s", beginning ? ast_str_buffer(before) : after);
		pbx_builtin_setvar_helper(chan, args.var, beginning ? after : ast_str_buffer(before));
	}

	return 0;
#undef beginning
}

static struct ast_custom_function shift_function = {
	.name = "SHIFT",
	.read2 = shift_pop,
};

static struct ast_custom_function pop_function = {
	.name = "POP",
	.read2 = shift_pop,
};

static int unshift_push(struct ast_channel *chan, const char *cmd, char *data, const char *new_value)
{
#define beginning	(cmd[0] == 'U') /* UNSHIFT */
	char delimiter[2] = ",", *varsubst;
	size_t unused;
	struct ast_str *buf, *previous_value;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(var);
		AST_APP_ARG(delimiter);
	);
	const char *stripped_var;

	if (!(buf = ast_str_thread_get(&result_buf, 16)) ||
		!(previous_value = ast_str_thread_get(&tmp_buf, 16))) {
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.var)) {
		ast_log(LOG_WARNING, "%s requires a variable name\n", cmd);
		return -1;
	}

	if (args.argc > 1 && !ast_strlen_zero(args.delimiter)) {
		ast_get_encoded_char(args.delimiter, delimiter, &unused);
	}

	/* UNSHIFT and PUSH act as ways of setting a variable, so we need to be
	 * sure to skip leading underscores if they appear. However, we only want
	 * to skip up to two since that is the maximum number that can be used to
	 * indicate variable inheritance. Any further underscores are part of the
	 * variable name.
	 */
	stripped_var = args.var + MIN(strspn(args.var, "_"), 2);
	varsubst = ast_alloca(strlen(stripped_var) + 4);
	sprintf(varsubst, "${%s}", stripped_var);
	ast_str_substitute_variables(&previous_value, 0, chan, varsubst);

	if (!ast_str_strlen(previous_value)) {
		ast_str_set(&buf, 0, "%s", new_value);
	} else {
		ast_str_set(&buf, 0, "%s%c%s",
			beginning ? new_value : ast_str_buffer(previous_value),
			delimiter[0],
			beginning ? ast_str_buffer(previous_value) : new_value);
	}

	pbx_builtin_setvar_helper(chan, args.var, ast_str_buffer(buf));

	return 0;
#undef beginning
}

static struct ast_custom_function push_function = {
	.name = "PUSH",
	.write = unshift_push,
};

static struct ast_custom_function unshift_function = {
	.name = "UNSHIFT",
	.write = unshift_push,
};

static int passthru(struct ast_channel *chan, const char *cmd, char *data, struct ast_str **buf, ssize_t len)
{
	ast_str_set(buf, len, "%s", data);
	return 0;
}

static struct ast_custom_function passthru_function = {
	.name = "PASSTHRU",
	.read2 = passthru,
};

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(test_FIELDNUM)
{
	int i, res = AST_TEST_PASS;
	struct ast_channel *chan;
	struct ast_str *str;
	char expression[256];
	struct {
		const char *fields;
		const char *delim;
		const char *field;
		const char *expected;
	} test_args[] = {
		{"abc,def,ghi,jkl", "\\,",     "ghi", "3"},
		{"abc def ghi jkl", " ",       "abc", "1"},
		{"abc/def/ghi/jkl", "\\\\x2f", "def", "2"},
		{"abc$def$ghi$jkl", "",        "ghi", "0"},
		{"abc,def,ghi,jkl", "-",       "",    "0"},
		{"abc-def-ghi-jkl", "-",       "mno", "0"}
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "func_FIELDNUM_test";
		info->category = "/funcs/func_strings/";
		info->summary = "Test FIELDNUM function";
		info->description = "Verify FIELDNUM behavior";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(chan = ast_dummy_channel_alloc())) {
		ast_test_status_update(test, "Unable to allocate dummy channel\n");
		return AST_TEST_FAIL;
	}

	if (!(str = ast_str_create(16))) {
		ast_test_status_update(test, "Unable to allocate dynamic string buffer\n");
		ast_channel_release(chan);
		return AST_TEST_FAIL;
	}

	for (i = 0; i < ARRAY_LEN(test_args); i++) {
		struct ast_var_t *var = ast_var_assign("FIELDS", test_args[i].fields);
		if (!var) {
			ast_test_status_update(test, "Out of memory\n");
			res = AST_TEST_FAIL;
			break;
		}

		AST_LIST_INSERT_HEAD(ast_channel_varshead(chan), var, entries);

		snprintf(expression, sizeof(expression), "${FIELDNUM(%s,%s,%s)}", var->name, test_args[i].delim, test_args[i].field);
		ast_str_substitute_variables(&str, 0, chan, expression);

		AST_LIST_REMOVE(ast_channel_varshead(chan), var, entries);
		ast_var_delete(var);

		if (strcasecmp(ast_str_buffer(str), test_args[i].expected)) {
			ast_test_status_update(test, "Evaluation of '%s' returned '%s' instead of the expected value '%s'\n",
				expression, ast_str_buffer(str), test_args[i].expected);
			res = AST_TEST_FAIL;
			break;
		}
	}

	ast_free(str);
	ast_channel_release(chan);

	return res;
}

AST_TEST_DEFINE(test_REPLACE)
{
	int i, res = AST_TEST_PASS;
	struct ast_channel *chan;
	struct ast_str *str;
	char expression[256];
	struct {
		const char *test_string;
		const char *find_chars;
		const char *replace_char;
		const char *expected;
	} test_args[] = {
		{"abc,def", "\\,", "-", "abc-def"},
		{"abc,abc", "bc",  "a", "aaa,aaa"},
		{"abc,def", "x",   "?", "abc,def"},
		{"abc,def", "\\,", "",  "abcdef"}
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "func_REPLACE_test";
		info->category = "/funcs/func_strings/";
		info->summary = "Test REPLACE function";
		info->description = "Verify REPLACE behavior";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (!(chan = ast_dummy_channel_alloc())) {
		ast_test_status_update(test, "Unable to allocate dummy channel\n");
		return AST_TEST_FAIL;
	}

	if (!(str = ast_str_create(16))) {
		ast_test_status_update(test, "Unable to allocate dynamic string buffer\n");
		ast_channel_release(chan);
		return AST_TEST_FAIL;
	}

	for (i = 0; i < ARRAY_LEN(test_args); i++) {
		struct ast_var_t *var = ast_var_assign("TEST_STRING", test_args[i].test_string);
		if (!var) {
			ast_test_status_update(test, "Out of memory\n");
			res = AST_TEST_FAIL;
			break;
		}

		AST_LIST_INSERT_HEAD(ast_channel_varshead(chan), var, entries);

		snprintf(expression, sizeof(expression), "${REPLACE(%s,%s,%s)}", var->name, test_args[i].find_chars, test_args[i].replace_char);
		ast_str_substitute_variables(&str, 0, chan, expression);

		AST_LIST_REMOVE(ast_channel_varshead(chan), var, entries);
		ast_var_delete(var);

		if (strcasecmp(ast_str_buffer(str), test_args[i].expected)) {
			ast_test_status_update(test, "Evaluation of '%s' returned '%s' instead of the expected value '%s'\n",
				expression, ast_str_buffer(str), test_args[i].expected);
			res = AST_TEST_FAIL;
			break;
		}
	}

	ast_free(str);
	ast_channel_release(chan);

	return res;
}

AST_TEST_DEFINE(test_FILTER)
{
	int i, res = AST_TEST_PASS;
	const char *test_strings[][2] = {
		{"A-R",            "DAHDI"},
		{"A\\-R",          "A"},
		{"\\x41-R",        "DAHDI"},
		{"0-9A-Ca-c",      "0042133333A12212"},
		{"0-9a-cA-C_+\\-", "0042133333A12212"},
		{NULL,             NULL},
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "func_FILTER_test";
		info->category = "/funcs/func_strings/";
		info->summary = "Test FILTER function";
		info->description = "Verify FILTER behavior";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	for (i = 0; test_strings[i][0]; i++) {
		char tmp[256], tmp2[256] = "";
		snprintf(tmp, sizeof(tmp), "${FILTER(%s,0042133333&DAHDI/g1/2212)}", test_strings[i][0]);
		pbx_substitute_variables_helper(NULL, tmp, tmp2, sizeof(tmp2) - 1);
		if (strcmp(test_strings[i][1], tmp2)) {
			ast_test_status_update(test, "Format string '%s' substituted to '%s'.  Expected '%s'.\n", test_strings[i][0], tmp2, test_strings[i][1]);
			res = AST_TEST_FAIL;
		}
	}
	return res;
}

AST_TEST_DEFINE(test_STRREPLACE)
{
	int i, res = AST_TEST_PASS;
	struct ast_channel *chan; /* dummy channel */
	struct ast_str *str; /* fancy string for holding comparing value */

	const char *test_strings[][5] = {
		{"Weasels have eaten my telephone system", "have eaten my", "are eating our", "", "Weasels are eating our telephone system"}, /*Test normal conditions */
		{"Did you know twenty plus two is twenty-two?", "twenty", "thirty", NULL, "Did you know thirty plus two is thirty-two?"}, /* Test no third comma */
		{"foofoofoofoofoofoofoo", "foofoo", "bar", NULL, "barbarbarfoo"}, /* Found string within previous match */
		{"My pet dog once ate a dog who sat on a dog while eating a corndog.", "dog", "cat", "3", "My pet cat once ate a cat who sat on a cat while eating a corndog."},
		{"One and one and one is three", "and", "plus", "1", "One plus one and one is three"}, /* Test <max-replacements> = 1*/
		{"", "fhqwagads", "spelunker", NULL, ""}, /* Empty primary string */
		{"Part of this string is missing.", "missing", NULL, NULL, "Part of this string is ."}, /* Empty replace string */
		{"'Accidentally' left off a bunch of stuff.", NULL, NULL, NULL, ""}, /* Deliberate error test from too few args */
		{"This test will also error.", "", "", "", ""}, /* Deliberate error test from blank find string */
		{"This is an \"escape character\" test.", "\\\"escape character\\\"", "evil", NULL, "This is an evil test."}
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "func_STRREPLACE_test";
		info->category = "/funcs/func_strings/";
		info->summary = "Test STRREPLACE function";
		info->description = "Verify STRREPLACE behavior";
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
		char tmp[512], tmp2[512] = "";

		struct ast_var_t *var = ast_var_assign("test_string", test_strings[i][0]);
		if (!var) {
			ast_test_status_update(test, "Unable to allocate variable\n");
			ast_free(str);
			ast_channel_release(chan);
			return AST_TEST_FAIL;
		}

		AST_LIST_INSERT_HEAD(ast_channel_varshead(chan), var, entries);

		if (test_strings[i][3]) {
			snprintf(tmp, sizeof(tmp), "${STRREPLACE(%s,%s,%s,%s)}", "test_string", test_strings[i][1], test_strings[i][2], test_strings[i][3]);
		} else if (test_strings[i][2]) {
			snprintf(tmp, sizeof(tmp), "${STRREPLACE(%s,%s,%s)}", "test_string", test_strings[i][1], test_strings[i][2]);
		} else if (test_strings[i][1]) {
			snprintf(tmp, sizeof(tmp), "${STRREPLACE(%s,%s)}", "test_string", test_strings[i][1]);
		} else {
			snprintf(tmp, sizeof(tmp), "${STRREPLACE(%s)}", "test_string");
		}
		ast_str_substitute_variables(&str, 0, chan, tmp);
		if (strcmp(test_strings[i][4], ast_str_buffer(str))) {
			ast_test_status_update(test, "Format string '%s' substituted to '%s'.  Expected '%s'.\n", test_strings[i][0], tmp2, test_strings[i][4]);
			res = AST_TEST_FAIL;
		}
	}

	ast_free(str);
	ast_channel_release(chan);

	return res;
}

AST_TEST_DEFINE(test_STRBETWEEN)
{
	int i, res = AST_TEST_PASS;
	struct ast_channel *chan; /* dummy channel */
	struct ast_str *str; /* fancy string for holding comparing value */

	const char *test_strings[][5] = {
		{"0", "w", "0"},
		{"30", "w", "3w0"},
		{"212", "w", "2w1w2"},
		{"212", "55", "2551552"},
		{"212", " ", "2 1 2"},
		{"", "w", ""},
		{"555", "", "555"},
		{"abcdefg", "_", "a_b_c_d_e_f_g"},
		{"A", "A", "A"},
		{"AA", "B", "ABA"},
		{"AAA", "B", "ABABA"},
	};

	switch (cmd) {
	case TEST_INIT:
		info->name = "func_STRBETWEEN";
		info->category = "/funcs/func_strings/";
		info->summary = "Test STRBETWEEN function";
		info->description = "Verify STRBETWEEN behavior";
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
		char tmp[512], tmp2[512] = "";

		struct ast_var_t *var = ast_var_assign("test_string", test_strings[i][0]);
		if (!var) {
			ast_test_status_update(test, "Unable to allocate variable\n");
			ast_free(str);
			ast_channel_release(chan);
			return AST_TEST_FAIL;
		}

		AST_LIST_INSERT_HEAD(ast_channel_varshead(chan), var, entries);

		if (test_strings[i][1]) {
			snprintf(tmp, sizeof(tmp), "${STRBETWEEN(%s,%s)}", "test_string", test_strings[i][1]);
		} else {
			snprintf(tmp, sizeof(tmp), "${STRBETWEEN(%s)}", "test_string");
		}
		ast_str_substitute_variables(&str, 0, chan, tmp);
		if (strcmp(test_strings[i][2], ast_str_buffer(str))) {
			ast_test_status_update(test, "Format string '%s' substituted to '%s'.  Expected '%s'.\n", test_strings[i][0], tmp2, test_strings[i][2]);
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
	int res = 0;

	AST_TEST_UNREGISTER(test_FIELDNUM);
	AST_TEST_UNREGISTER(test_REPLACE);
	AST_TEST_UNREGISTER(test_FILTER);
	AST_TEST_UNREGISTER(test_STRREPLACE);
	AST_TEST_UNREGISTER(test_STRBETWEEN);
	res |= ast_custom_function_unregister(&fieldqty_function);
	res |= ast_custom_function_unregister(&fieldnum_function);
	res |= ast_custom_function_unregister(&filter_function);
	res |= ast_custom_function_unregister(&replace_function);
	res |= ast_custom_function_unregister(&strreplace_function);
	res |= ast_custom_function_unregister(&strbetween_function);
	res |= ast_custom_function_unregister(&listfilter_function);
	res |= ast_custom_function_unregister(&regex_function);
	res |= ast_custom_function_unregister(&array_function);
	res |= ast_custom_function_unregister(&quote_function);
	res |= ast_custom_function_unregister(&csv_quote_function);
	res |= ast_custom_function_unregister(&len_function);
	res |= ast_custom_function_unregister(&strftime_function);
	res |= ast_custom_function_unregister(&strptime_function);
	res |= ast_custom_function_unregister(&eval_function);
	res |= ast_custom_function_unregister(&keypadhash_function);
	res |= ast_custom_function_unregister(&hashkeys_function);
	res |= ast_custom_function_unregister(&hash_function);
	res |= ast_unregister_application(app_clearhash);
	res |= ast_custom_function_unregister(&toupper_function);
	res |= ast_custom_function_unregister(&tolower_function);
	res |= ast_custom_function_unregister(&shift_function);
	res |= ast_custom_function_unregister(&pop_function);
	res |= ast_custom_function_unregister(&push_function);
	res |= ast_custom_function_unregister(&unshift_function);
	res |= ast_custom_function_unregister(&passthru_function);

	return res;
}

static int load_module(void)
{
	int res = 0;

	AST_TEST_REGISTER(test_FIELDNUM);
	AST_TEST_REGISTER(test_REPLACE);
	AST_TEST_REGISTER(test_FILTER);
	AST_TEST_REGISTER(test_STRREPLACE);
	AST_TEST_REGISTER(test_STRBETWEEN);
	res |= ast_custom_function_register(&fieldqty_function);
	res |= ast_custom_function_register(&fieldnum_function);
	res |= ast_custom_function_register(&filter_function);
	res |= ast_custom_function_register(&replace_function);
	res |= ast_custom_function_register(&strreplace_function);
	res |= ast_custom_function_register(&strbetween_function);
	res |= ast_custom_function_register(&listfilter_function);
	res |= ast_custom_function_register(&regex_function);
	res |= ast_custom_function_register(&array_function);
	res |= ast_custom_function_register(&quote_function);
	res |= ast_custom_function_register(&csv_quote_function);
	res |= ast_custom_function_register(&len_function);
	res |= ast_custom_function_register(&strftime_function);
	res |= ast_custom_function_register(&strptime_function);
	res |= ast_custom_function_register(&eval_function);
	res |= ast_custom_function_register(&keypadhash_function);
	res |= ast_custom_function_register(&hashkeys_function);
	res |= ast_custom_function_register(&hash_function);
	res |= ast_register_application_xml(app_clearhash, exec_clearhash);
	res |= ast_custom_function_register(&toupper_function);
	res |= ast_custom_function_register(&tolower_function);
	res |= ast_custom_function_register(&shift_function);
	res |= ast_custom_function_register(&pop_function);
	res |= ast_custom_function_register(&push_function);
	res |= ast_custom_function_register(&unshift_function);
	res |= ast_custom_function_register(&passthru_function);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "String handling dialplan functions");
