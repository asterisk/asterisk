/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, CFWare, LLC
 *
 * Corey Farrell <git@cfware.com>
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
 * \brief PBX variables routines.
 *
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/app.h"
#include "asterisk/ast_expr.h"
#include "asterisk/chanvars.h"
#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/module.h"
#include "asterisk/paths.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_channels.h"
#include "pbx_private.h"

/*** DOCUMENTATION
	<application name="Set" language="en_US">
		<synopsis>
			Set channel variable or function value.
		</synopsis>
		<syntax argsep="=">
			<parameter name="name" required="true" />
			<parameter name="value" required="true" />
		</syntax>
		<description>
			<para>This function can be used to set the value of channel variables or dialplan functions.
			When setting variables, if the variable name is prefixed with <literal>_</literal>,
			the variable will be inherited into channels created from the current channel.
			If the variable name is prefixed with <literal>__</literal>, the variable will be
			inherited into channels created from the current channel and all children channels.</para>
			<note><para>If (and only if), in <filename>/etc/asterisk/asterisk.conf</filename>, you have
			a <literal>[compat]</literal> category, and you have <literal>app_set = 1.4</literal> under that, then
			the behavior of this app changes, and strips surrounding quotes from the right hand side as
			it did previously in 1.4.
			The advantages of not stripping out quoting, and not caring about the separator characters (comma and vertical bar)
			were sufficient to make these changes in 1.6. Confusion about how many backslashes would be needed to properly
			protect separators and quotes in various database access strings has been greatly
			reduced by these changes.</para></note>
		</description>
		<see-also>
			<ref type="application">MSet</ref>
			<ref type="function">GLOBAL</ref>
			<ref type="function">SET</ref>
			<ref type="function">ENV</ref>
		</see-also>
	</application>
	<application name="MSet" language="en_US">
		<synopsis>
			Set channel variable(s) or function value(s).
		</synopsis>
		<syntax>
			<parameter name="set1" required="true" argsep="=">
				<argument name="name1" required="true" />
				<argument name="value1" required="true" />
			</parameter>
			<parameter name="set2" multiple="true" argsep="=">
				<argument name="name2" required="true" />
				<argument name="value2" required="true" />
			</parameter>
		</syntax>
		<description>
			<para>This function can be used to set the value of channel variables or dialplan functions.
			When setting variables, if the variable name is prefixed with <literal>_</literal>,
			the variable will be inherited into channels created from the current channel
			If the variable name is prefixed with <literal>__</literal>, the variable will be
			inherited into channels created from the current channel and all children channels.
			MSet behaves in a similar fashion to the way Set worked in 1.2/1.4 and is thus
			prone to doing things that you may not expect. For example, it strips surrounding
			double-quotes from the right-hand side (value). If you need to put a separator
			character (comma or vert-bar), you will need to escape them by inserting a backslash
			before them. Avoid its use if possible.</para>
			<para>This application allows up to 99 variables to be set at once.</para>
		</description>
		<see-also>
			<ref type="application">Set</ref>
		</see-also>
	</application>
 ***/

AST_RWLOCK_DEFINE_STATIC(globalslock);
static struct varshead globals = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

/*!
 * \brief extract offset:length from variable name.
 * \return 1 if there is a offset:length part, which is
 * trimmed off (values go into variables)
 */
static int parse_variable_name(char *var, int *offset, int *length, int *isfunc)
{
	int parens = 0;

	*offset = 0;
	*length = INT_MAX;
	*isfunc = 0;
	for (; *var; var++) {
		if (*var == '(') {
			(*isfunc)++;
			parens++;
		} else if (*var == ')') {
			parens--;
		} else if (*var == ':' && parens == 0) {
			*var++ = '\0';
			sscanf(var, "%30d:%30d", offset, length);
			return 1; /* offset:length valid */
		}
	}
	return 0;
}

/*!
 *\brief takes a substring. It is ok to call with value == workspace.
 * \param value
 * \param offset < 0 means start from the end of the string and set the beginning
 *   to be that many characters back.
 * \param length is the length of the substring, a value less than 0 means to leave
 * that many off the end.
 * \param workspace
 * \param workspace_len
 * Always return a copy in workspace.
 */
static char *substring(const char *value, int offset, int length, char *workspace, size_t workspace_len)
{
	char *ret = workspace;
	int lr;	/* length of the input string after the copy */

	ast_copy_string(workspace, value, workspace_len); /* always make a copy */

	lr = strlen(ret); /* compute length after copy, so we never go out of the workspace */

	/* Quick check if no need to do anything */
	if (offset == 0 && length >= lr)	/* take the whole string */
		return ret;

	if (offset < 0)	{	/* translate negative offset into positive ones */
		offset = lr + offset;
		if (offset < 0) /* If the negative offset was greater than the length of the string, just start at the beginning */
			offset = 0;
	}

	/* too large offset result in empty string so we know what to return */
	if (offset >= lr)
		return ret + lr;	/* the final '\0' */

	ret += offset;		/* move to the start position */
	if (length >= 0 && length < lr - offset)	/* truncate if necessary */
		ret[length] = '\0';
	else if (length < 0) {
		if (lr > offset - length) /* After we remove from the front and from the rear, is there anything left? */
			ret[lr + length - offset] = '\0';
		else
			ret[0] = '\0';
	}

	return ret;
}

static const char *ast_str_substring(struct ast_str *value, int offset, int length)
{
	int lr;	/* length of the input string after the copy */

	lr = ast_str_strlen(value); /* compute length after copy, so we never go out of the workspace */

	/* Quick check if no need to do anything */
	if (offset == 0 && length >= lr)	/* take the whole string */
		return ast_str_buffer(value);

	if (offset < 0)	{	/* translate negative offset into positive ones */
		offset = lr + offset;
		if (offset < 0) /* If the negative offset was greater than the length of the string, just start at the beginning */
			offset = 0;
	}

	/* too large offset result in empty string so we know what to return */
	if (offset >= lr) {
		ast_str_reset(value);
		return ast_str_buffer(value);
	}

	if (offset > 0) {
		/* Go ahead and chop off the beginning */
		memmove(ast_str_buffer(value), ast_str_buffer(value) + offset, ast_str_strlen(value) - offset + 1);
		lr -= offset;
	}

	if (length >= 0 && length < lr) {	/* truncate if necessary */
		ast_str_truncate(value, length);
	} else if (length < 0) {
		if (lr > -length) { /* After we remove from the front and from the rear, is there anything left? */
			ast_str_truncate(value, lr + length);
		} else {
			ast_str_reset(value);
		}
	} else {
		/* Nothing to do, but update the buffer length */
		ast_str_update(value);
	}

	return ast_str_buffer(value);
}

/*! \brief  Support for Asterisk built-in variables in the dialplan

\note	See also
	- \ref AstVar	Channel variables
	- \ref AstCauses The HANGUPCAUSE variable
 */
void pbx_retrieve_variable(struct ast_channel *c, const char *var, char **ret, char *workspace, int workspacelen, struct varshead *headp)
{
	struct ast_str *str = ast_str_create(16);
	const char *cret;

	cret = ast_str_retrieve_variable(&str, 0, c, headp, var);
	ast_copy_string(workspace, ast_str_buffer(str), workspacelen);
	*ret = cret ? workspace : NULL;
	ast_free(str);
}

const char *ast_str_retrieve_variable(struct ast_str **str, ssize_t maxlen, struct ast_channel *c, struct varshead *headp, const char *var)
{
	const char not_found = '\0';
	char *tmpvar;
	const char *ret;
	const char *s;	/* the result */
	int offset, length;
	int i, need_substring;
	struct varshead *places[2] = { headp, &globals };	/* list of places where we may look */
	char workspace[20];

	if (c) {
		ast_channel_lock(c);
		places[0] = ast_channel_varshead(c);
	}
	/*
	 * Make a copy of var because parse_variable_name() modifies the string.
	 * Then if called directly, we might need to run substring() on the result;
	 * remember this for later in 'need_substring', 'offset' and 'length'
	 */
	tmpvar = ast_strdupa(var);	/* parse_variable_name modifies the string */
	need_substring = parse_variable_name(tmpvar, &offset, &length, &i /* ignored */);

	/*
	 * Look first into predefined variables, then into variable lists.
	 * Variable 's' points to the result, according to the following rules:
	 * s == &not_found (set at the beginning) means that we did not find a
	 *	matching variable and need to look into more places.
	 * If s != &not_found, s is a valid result string as follows:
	 * s = NULL if the variable does not have a value;
	 *	you typically do this when looking for an unset predefined variable.
	 * s = workspace if the result has been assembled there;
	 *	typically done when the result is built e.g. with an snprintf(),
	 *	so we don't need to do an additional copy.
	 * s != workspace in case we have a string, that needs to be copied
	 *	(the ast_copy_string is done once for all at the end).
	 *	Typically done when the result is already available in some string.
	 */
	s = &not_found;	/* default value */
	if (c) {	/* This group requires a valid channel */
		/* Names with common parts are looked up a piece at a time using strncmp. */
		if (!strncmp(var, "CALL", 4)) {
			if (!strncmp(var + 4, "ING", 3)) {
				if (!strcmp(var + 7, "PRES")) {			/* CALLINGPRES */
					ast_str_set(str, maxlen, "%d",
						ast_party_id_presentation(&ast_channel_caller(c)->id));
					s = ast_str_buffer(*str);
				} else if (!strcmp(var + 7, "ANI2")) {		/* CALLINGANI2 */
					ast_str_set(str, maxlen, "%d", ast_channel_caller(c)->ani2);
					s = ast_str_buffer(*str);
				} else if (!strcmp(var + 7, "TON")) {		/* CALLINGTON */
					ast_str_set(str, maxlen, "%d", ast_channel_caller(c)->id.number.plan);
					s = ast_str_buffer(*str);
				} else if (!strcmp(var + 7, "TNS")) {		/* CALLINGTNS */
					ast_str_set(str, maxlen, "%d", ast_channel_dialed(c)->transit_network_select);
					s = ast_str_buffer(*str);
				}
			}
		} else if (!strcmp(var, "HINT")) {
			s = ast_str_get_hint(str, maxlen, NULL, 0, c, ast_channel_context(c), ast_channel_exten(c)) ? ast_str_buffer(*str) : NULL;
		} else if (!strcmp(var, "HINTNAME")) {
			s = ast_str_get_hint(NULL, 0, str, maxlen, c, ast_channel_context(c), ast_channel_exten(c)) ? ast_str_buffer(*str) : NULL;
		} else if (!strcmp(var, "EXTEN")) {
			s = ast_channel_exten(c);
		} else if (!strcmp(var, "CONTEXT")) {
			s = ast_channel_context(c);
		} else if (!strcmp(var, "PRIORITY")) {
			ast_str_set(str, maxlen, "%d", ast_channel_priority(c));
			s = ast_str_buffer(*str);
		} else if (!strcmp(var, "CHANNEL")) {
			s = ast_channel_name(c);
		} else if (!strcmp(var, "UNIQUEID")) {
			s = ast_channel_uniqueid(c);
		} else if (!strcmp(var, "HANGUPCAUSE")) {
			ast_str_set(str, maxlen, "%d", ast_channel_hangupcause(c));
			s = ast_str_buffer(*str);
		}
	}
	if (s == &not_found) { /* look for more */
		if (!strcmp(var, "EPOCH")) {
			ast_str_set(str, maxlen, "%d", (int) time(NULL));
			s = ast_str_buffer(*str);
		} else if (!strcmp(var, "SYSTEMNAME")) {
			s = ast_config_AST_SYSTEM_NAME;
		} else if (!strcmp(var, "ASTCACHEDIR")) {
			s = ast_config_AST_CACHE_DIR;
		} else if (!strcmp(var, "ASTETCDIR")) {
			s = ast_config_AST_CONFIG_DIR;
		} else if (!strcmp(var, "ASTMODDIR")) {
			s = ast_config_AST_MODULE_DIR;
		} else if (!strcmp(var, "ASTVARLIBDIR")) {
			s = ast_config_AST_VAR_DIR;
		} else if (!strcmp(var, "ASTDBDIR")) {
			s = ast_config_AST_DB;
		} else if (!strcmp(var, "ASTKEYDIR")) {
			s = ast_config_AST_KEY_DIR;
		} else if (!strcmp(var, "ASTDATADIR")) {
			s = ast_config_AST_DATA_DIR;
		} else if (!strcmp(var, "ASTAGIDIR")) {
			s = ast_config_AST_AGI_DIR;
		} else if (!strcmp(var, "ASTSPOOLDIR")) {
			s = ast_config_AST_SPOOL_DIR;
		} else if (!strcmp(var, "ASTRUNDIR")) {
			s = ast_config_AST_RUN_DIR;
		} else if (!strcmp(var, "ASTLOGDIR")) {
			s = ast_config_AST_LOG_DIR;
		} else if (!strcmp(var, "ASTSBINDIR")) {
			s = ast_config_AST_SBIN_DIR;
		} else if (!strcmp(var, "ENTITYID")) {
			ast_eid_to_str(workspace, sizeof(workspace), &ast_eid_default);
			s = workspace;
		}
	}
	/* if not found, look into chanvars or global vars */
	for (i = 0; s == &not_found && i < ARRAY_LEN(places); i++) {
		struct ast_var_t *variables;
		if (!places[i])
			continue;
		if (places[i] == &globals)
			ast_rwlock_rdlock(&globalslock);
		AST_LIST_TRAVERSE(places[i], variables, entries) {
			if (!strcmp(ast_var_name(variables), var)) {
				s = ast_var_value(variables);
				break;
			}
		}
		if (places[i] == &globals)
			ast_rwlock_unlock(&globalslock);
	}
	if (s == &not_found || s == NULL) {
		ast_debug(5, "Result of '%s' is NULL\n", var);
		ret = NULL;
	} else {
		ast_debug(5, "Result of '%s' is '%s'\n", var, s);
		if (s != ast_str_buffer(*str)) {
			ast_str_set(str, maxlen, "%s", s);
		}
		ret = ast_str_buffer(*str);
		if (need_substring) {
			ret = ast_str_substring(*str, offset, length);
			ast_debug(2, "Final result of '%s' is '%s'\n", var, ret);
		}
	}

	if (c) {
		ast_channel_unlock(c);
	}
	return ret;
}

void ast_str_substitute_variables_full(struct ast_str **buf, ssize_t maxlen, struct ast_channel *c, struct varshead *headp, const char *templ, size_t *used)
{
	/* Substitutes variables into buf, based on string templ */
	const char *whereweare;
	struct ast_str *substr1 = ast_str_create(16);
	struct ast_str *substr2 = NULL;
	struct ast_str *substr3 = ast_str_create(16);

	ast_str_reset(*buf);

	if (!substr1 || !substr3) {
		if (used) {
			*used = ast_str_strlen(*buf);
		}
		ast_free(substr1);
		ast_free(substr3);
		return;
	}

	whereweare = templ;
	while (!ast_strlen_zero(whereweare)) {
		const char *nextvar = NULL;
		const char *nextexp = NULL;
		const char *nextthing;
		const char *vars;
		const char *vare;
		char *finalvars;
		int pos;
		int brackets;
		int needsub;
		int len;

		/* reset our buffer */
		ast_str_reset(substr3);

		/* Determine how much simply needs to be copied to the output buf. */
		nextthing = strchr(whereweare, '$');
		if (nextthing) {
			pos = nextthing - whereweare;
			switch (nextthing[1]) {
			case '{':
				/* Variable substitution */
				nextvar = nextthing;
				break;
			case '[':
				/* Expression substitution */
				nextexp = nextthing;
				break;
			default:
				/* '$' is not part of a substitution so include it too. */
				++pos;
				break;
			}
		} else {
			/* We're copying the whole remaining string */
			pos = strlen(whereweare);
		}

		if (pos) {
			/* Copy that many bytes */
			ast_str_append_substr(buf, maxlen, whereweare, pos);

			whereweare += pos;
		}

		if (nextvar) {
			int offset;
			int offset2;
			int isfunction;
			int res;

			/* We have a variable.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextvar + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					brackets++;
					vare++;
				} else if (vare[0] == '{') {
					brackets++;
				} else if (vare[0] == '}') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '[')) {
					needsub++;
					vare++;
				}
				vare++;
			}
			len = vare - vars;
			if (brackets) {
				ast_log(LOG_WARNING, "Error in extension logic (missing '}')\n");
			} else {
				/* Don't count the closing '}' in the length. */
				--len;
			}

			/* Skip totally over variable string */
			whereweare = vare;

			/* Store variable name expression to lookup. */
			ast_str_set_substr(&substr1, 0, vars, len);
			ast_debug(5, "Evaluating '%s' (from '%s' len %d)\n", ast_str_buffer(substr1), vars, len);

			/* Substitute if necessary */
			if (needsub) {
				if (!substr2) {
					substr2 = ast_str_create(16);
					if (!substr2) {
						continue;
					}
				}
				ast_str_substitute_variables_full(&substr2, 0, c, headp, ast_str_buffer(substr1), NULL);
				finalvars = ast_str_buffer(substr2);
			} else {
				finalvars = ast_str_buffer(substr1);
			}

			parse_variable_name(finalvars, &offset, &offset2, &isfunction);
			if (isfunction) {
				/* Evaluate function */
				if (c || !headp) {
					res = ast_func_read2(c, finalvars, &substr3, 0);
				} else {
					struct varshead old;
					struct ast_channel *bogus;

					bogus = ast_dummy_channel_alloc();
					if (bogus) {
						old = *ast_channel_varshead(bogus);
						*ast_channel_varshead(bogus) = *headp;
						res = ast_func_read2(bogus, finalvars, &substr3, 0);
						/* Don't deallocate the varshead that was passed in */
						*ast_channel_varshead(bogus) = old;
						ast_channel_unref(bogus);
					} else {
						ast_log(LOG_ERROR, "Unable to allocate bogus channel for function value substitution.\n");
						res = -1;
					}
				}
				ast_debug(2, "Function %s result is '%s'\n",
					finalvars, res ? "" : ast_str_buffer(substr3));
			} else {
				/* Retrieve variable value */
				ast_str_retrieve_variable(&substr3, 0, c, headp, finalvars);
				res = 0;
			}
			if (!res) {
				ast_str_substring(substr3, offset, offset2);
				ast_str_append(buf, maxlen, "%s", ast_str_buffer(substr3));
			}
		} else if (nextexp) {
			/* We have an expression.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextexp + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '[')) {
					needsub++;
					brackets++;
					vare++;
				} else if (vare[0] == '[') {
					brackets++;
				} else if (vare[0] == ']') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					vare++;
				}
				vare++;
			}
			len = vare - vars;
			if (brackets) {
				ast_log(LOG_WARNING, "Error in extension logic (missing ']')\n");
			} else {
				/* Don't count the closing ']' in the length. */
				--len;
			}

			/* Skip totally over expression */
			whereweare = vare;

			/* Store expression to evaluate. */
			ast_str_set_substr(&substr1, 0, vars, len);

			/* Substitute if necessary */
			if (needsub) {
				if (!substr2) {
					substr2 = ast_str_create(16);
					if (!substr2) {
						continue;
					}
				}
				ast_str_substitute_variables_full(&substr2, 0, c, headp, ast_str_buffer(substr1), NULL);
				finalvars = ast_str_buffer(substr2);
			} else {
				finalvars = ast_str_buffer(substr1);
			}

			if (ast_str_expr(&substr3, 0, c, finalvars)) {
				ast_debug(2, "Expression result is '%s'\n", ast_str_buffer(substr3));
			}
			ast_str_append(buf, maxlen, "%s", ast_str_buffer(substr3));
		}
	}
	if (used) {
		*used = ast_str_strlen(*buf);
	}
	ast_free(substr1);
	ast_free(substr2);
	ast_free(substr3);
}

void ast_str_substitute_variables(struct ast_str **buf, ssize_t maxlen, struct ast_channel *chan, const char *templ)
{
	ast_str_substitute_variables_full(buf, maxlen, chan, NULL, templ, NULL);
}

void ast_str_substitute_variables_varshead(struct ast_str **buf, ssize_t maxlen, struct varshead *headp, const char *templ)
{
	ast_str_substitute_variables_full(buf, maxlen, NULL, headp, templ, NULL);
}

void pbx_substitute_variables_helper_full(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int count, size_t *used)
{
	pbx_substitute_variables_helper_full_location(c, headp, cp1, cp2, count, used, NULL, NULL, 0);
}

void pbx_substitute_variables_helper_full_location(struct ast_channel *c, struct varshead *headp, const char *cp1, char *cp2, int count, size_t *used, char *context, char *exten, int pri)
{
	/* Substitutes variables into cp2, based on string cp1, cp2 NO LONGER NEEDS TO BE ZEROED OUT!!!!  */
	const char *whereweare;
	const char *orig_cp2 = cp2;
	char ltmp[VAR_BUF_SIZE];
	char var[VAR_BUF_SIZE];

	*cp2 = 0; /* just in case nothing ends up there */
	whereweare = cp1;
	while (!ast_strlen_zero(whereweare) && count) {
		char *nextvar = NULL;
		char *nextexp = NULL;
		char *nextthing;
		char *vars;
		char *vare;
		int length;
		int pos;
		int brackets;
		int needsub;
		int len;

		/* Determine how much simply needs to be copied to the output buf. */
		nextthing = strchr(whereweare, '$');
		if (nextthing) {
			pos = nextthing - whereweare;
			switch (nextthing[1]) {
			case '{':
				/* Variable substitution */
				nextvar = nextthing;
				break;
			case '[':
				/* Expression substitution */
				nextexp = nextthing;
				break;
			default:
				/* '$' is not part of a substitution so include it too. */
				++pos;
				break;
			}
		} else {
			/* We're copying the whole remaining string */
			pos = strlen(whereweare);
		}

		if (pos) {
			/* Can't copy more than 'count' bytes */
			if (pos > count)
				pos = count;

			/* Copy that many bytes */
			memcpy(cp2, whereweare, pos);

			count -= pos;
			cp2 += pos;
			whereweare += pos;
			*cp2 = 0;
		}

		if (nextvar) {
			int offset;
			int offset2;
			int isfunction;
			char *cp4 = NULL;
			char workspace[VAR_BUF_SIZE] = "";

			/* We have a variable.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextvar + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					brackets++;
					vare++;
				} else if (vare[0] == '{') {
					brackets++;
				} else if (vare[0] == '}') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '[')) {
					needsub++;
					vare++;
				}
				vare++;
			}
			len = vare - vars;
			if (brackets) {
				ast_log(LOG_WARNING, "Error in extension logic (missing '}')\n");
			} else {
				/* Don't count the closing '}' in the length. */
				--len;
			}

			/* Skip totally over variable string */
			whereweare = vare;

			/* Store variable name expression to lookup (and truncate). */
			ast_copy_string(var, vars, len + 1);

			/* Substitute if necessary */
			if (needsub) {
				pbx_substitute_variables_helper_full_location(c, headp, var, ltmp, VAR_BUF_SIZE - 1, NULL, context, exten, pri);
				vars = ltmp;
			} else {
				vars = var;
			}

			parse_variable_name(vars, &offset, &offset2, &isfunction);
			if (isfunction) {
				/* Evaluate function */
				if (c || !headp)
					cp4 = ast_func_read(c, vars, workspace, VAR_BUF_SIZE) ? NULL : workspace;
				else {
					struct varshead old;
					struct ast_channel *bogus;

					bogus = ast_dummy_channel_alloc();
					if (bogus) {
						old = *ast_channel_varshead(bogus);
						*ast_channel_varshead(bogus) = *headp;
						cp4 = ast_func_read(bogus, vars, workspace, VAR_BUF_SIZE) ? NULL : workspace;
						/* Don't deallocate the varshead that was passed in */
						*ast_channel_varshead(bogus) = old;
						ast_channel_unref(bogus);
					} else {
						ast_log(LOG_ERROR, "Unable to allocate bogus channel for function value substitution.\n");
						cp4 = NULL;
					}
				}
				ast_debug(2, "Function %s result is '%s'\n", vars, cp4 ? cp4 : "(null)");
			} else {
				/* Retrieve variable value */
				/* For dialplan location, if we were told what to substitute explicitly, use that instead */
				if (exten && !strcmp(vars, "EXTEN")) {
					ast_copy_string(workspace, exten, VAR_BUF_SIZE);
					cp4 = workspace;
				} else if (context && !strcmp(vars, "CONTEXT")) {
					ast_copy_string(workspace, context, VAR_BUF_SIZE);
					cp4 = workspace;
				} else if (pri && !strcmp(vars, "PRIORITY")) {
					snprintf(workspace, VAR_BUF_SIZE, "%d", pri);
					cp4 = workspace;
				} else {
					pbx_retrieve_variable(c, vars, &cp4, workspace, VAR_BUF_SIZE, headp);
				}
			}
			if (cp4) {
				cp4 = substring(cp4, offset, offset2, workspace, VAR_BUF_SIZE);

				length = strlen(cp4);
				if (length > count)
					length = count;
				memcpy(cp2, cp4, length);
				count -= length;
				cp2 += length;
				*cp2 = 0;
			}
		} else if (nextexp) {
			/* We have an expression.  Find the start and end, and determine
			   if we are going to have to recursively call ourselves on the
			   contents */
			vars = vare = nextexp + 2;
			brackets = 1;
			needsub = 0;

			/* Find the end of it */
			while (brackets && *vare) {
				if ((vare[0] == '$') && (vare[1] == '[')) {
					needsub++;
					brackets++;
					vare++;
				} else if (vare[0] == '[') {
					brackets++;
				} else if (vare[0] == ']') {
					brackets--;
				} else if ((vare[0] == '$') && (vare[1] == '{')) {
					needsub++;
					vare++;
				}
				vare++;
			}
			len = vare - vars;
			if (brackets) {
				ast_log(LOG_WARNING, "Error in extension logic (missing ']')\n");
			} else {
				/* Don't count the closing ']' in the length. */
				--len;
			}

			/* Skip totally over expression */
			whereweare = vare;

			/* Store expression to evaluate (and truncate). */
			ast_copy_string(var, vars, len + 1);

			/* Substitute if necessary */
			if (needsub) {
				pbx_substitute_variables_helper_full_location(c, headp, var, ltmp, VAR_BUF_SIZE - 1, NULL, context, exten, pri);
				vars = ltmp;
			} else {
				vars = var;
			}

			length = ast_expr(vars, cp2, count, c);
			if (length) {
				ast_debug(1, "Expression result is '%s'\n", cp2);
				count -= length;
				cp2 += length;
				*cp2 = 0;
			}
		}
	}
	if (used) {
		*used = cp2 - orig_cp2;
	}
}

void pbx_substitute_variables_helper(struct ast_channel *c, const char *cp1, char *cp2, int count)
{
	pbx_substitute_variables_helper_full(c, (c) ? ast_channel_varshead(c) : NULL, cp1, cp2, count, NULL);
}

void pbx_substitute_variables_varshead(struct varshead *headp, const char *cp1, char *cp2, int count)
{
	pbx_substitute_variables_helper_full(NULL, headp, cp1, cp2, count, NULL);
}

/*! \brief CLI support for listing global variables in a parseable way */
static char *handle_show_globals(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i = 0;
	struct ast_var_t *newvariable;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan show globals";
		e->usage =
			"Usage: dialplan show globals\n"
			"       List current global dialplan variables and their values\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_rwlock_rdlock(&globalslock);
	AST_LIST_TRAVERSE (&globals, newvariable, entries) {
		i++;
		ast_cli(a->fd, "   %s=%s\n", ast_var_name(newvariable), ast_var_value(newvariable));
	}
	ast_rwlock_unlock(&globalslock);
	ast_cli(a->fd, "\n    -- %d variable(s)\n", i);

	return CLI_SUCCESS;
}

/*! \brief CLI support for listing chanvar's variables in a parseable way */
static char *handle_show_chanvar(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;
	struct ast_var_t *var;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan show chanvar";
		e->usage =
			"Usage: dialplan show chanvar <channel>\n"
			"       List current channel variables and their values\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 3);
	}

	if (a->argc != e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	chan = ast_channel_get_by_name(a->argv[e->args]);
	if (!chan) {
		ast_cli(a->fd, "Channel '%s' not found\n", a->argv[e->args]);
		return CLI_FAILURE;
	}

	ast_channel_lock(chan);
	AST_LIST_TRAVERSE(ast_channel_varshead(chan), var, entries) {
		ast_cli(a->fd, "%s=%s\n", ast_var_name(var), ast_var_value(var));
	}
	ast_channel_unlock(chan);

	ast_channel_unref(chan);
	return CLI_SUCCESS;
}

/*! \brief CLI support for executing function */
static char *handle_eval_function(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *c = NULL;
	char *fn, *substituted;
	int ret;
	char workspace[1024];

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan eval function";
		e->usage =
			"Usage: dialplan eval function <name(args)>\n"
			"       Evaluate a dialplan function call\n"
			"       A dummy channel is used to evaluate\n"
			"       the function call, so only global\n"
			"       variables should be used.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args + 1) {
		return CLI_SHOWUSAGE;
	}

	c = ast_dummy_channel_alloc();
	if (!c) {
		ast_cli(a->fd, "Unable to allocate bogus channel for function evaluation.\n");
		return CLI_FAILURE;
	}

	fn = (char *) a->argv[3];
	pbx_substitute_variables_helper(c, fn, workspace, sizeof(workspace));
	substituted = ast_strdupa(workspace);
	workspace[0] = '\0';
	ret = ast_func_read(c, substituted, workspace, sizeof(workspace));

	c = ast_channel_unref(c);

	ast_cli(a->fd, "Return Value: %s (%d)\n", ret ? "Failure" : "Success", ret);
	ast_cli(a->fd, "Result: %s\n", workspace);

	return CLI_SUCCESS;
}

static char *handle_set_global(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan set global";
		e->usage =
			"Usage: dialplan set global <name> <value>\n"
			"       Set global dialplan variable <name> to <value>\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args + 2)
		return CLI_SHOWUSAGE;

	pbx_builtin_setvar_helper(NULL, a->argv[3], a->argv[4]);
	ast_cli(a->fd, "\n    -- Global variable '%s' set to '%s'\n", a->argv[3], a->argv[4]);

	return CLI_SUCCESS;
}

static char *handle_set_chanvar(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;
	const char *chan_name, *var_name, *var_value;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan set chanvar";
		e->usage =
			"Usage: dialplan set chanvar <channel> <varname> <value>\n"
			"       Set channel variable <varname> to <value>\n";
		return NULL;
	case CLI_GENERATE:
		return ast_complete_channels(a->line, a->word, a->pos, a->n, 3);
	}

	if (a->argc != e->args + 3)
		return CLI_SHOWUSAGE;

	chan_name = a->argv[e->args];
	var_name = a->argv[e->args + 1];
	var_value = a->argv[e->args + 2];

	if (!(chan = ast_channel_get_by_name(chan_name))) {
		ast_cli(a->fd, "Channel '%s' not found\n", chan_name);
		return CLI_FAILURE;
	}

	pbx_builtin_setvar_helper(chan, var_name, var_value);

	chan = ast_channel_unref(chan);

	ast_cli(a->fd, "\n    -- Channel variable '%s' set to '%s' for '%s'\n",  var_name, var_value, chan_name);

	return CLI_SUCCESS;
}

int pbx_builtin_serialize_variables(struct ast_channel *chan, struct ast_str **buf)
{
	struct ast_var_t *variables;
	const char *var, *val;
	int total = 0;

	if (!chan)
		return 0;

	ast_str_reset(*buf);

	ast_channel_lock(chan);

	AST_LIST_TRAVERSE(ast_channel_varshead(chan), variables, entries) {
		if ((var = ast_var_name(variables)) && (val = ast_var_value(variables))
		   /* && !ast_strlen_zero(var) && !ast_strlen_zero(val) */
		   ) {
			if (ast_str_append(buf, 0, "%s=%s\n", var, val) < 0) {
				ast_log(LOG_ERROR, "Data Buffer Size Exceeded!\n");
				break;
			} else
				total++;
		} else
			break;
	}

	ast_channel_unlock(chan);

	return total;
}

const char *pbx_builtin_getvar_helper(struct ast_channel *chan, const char *name)
{
	struct ast_var_t *variables;
	const char *ret = NULL;
	int i;
	struct varshead *places[2] = { NULL, &globals };

	if (!name)
		return NULL;

	if (chan) {
		ast_channel_lock(chan);
		places[0] = ast_channel_varshead(chan);
	}

	for (i = 0; i < 2; i++) {
		if (!places[i])
			continue;
		if (places[i] == &globals)
			ast_rwlock_rdlock(&globalslock);
		AST_LIST_TRAVERSE(places[i], variables, entries) {
			if (!strcmp(name, ast_var_name(variables))) {
				ret = ast_var_value(variables);
				break;
			}
		}
		if (places[i] == &globals)
			ast_rwlock_unlock(&globalslock);
		if (ret)
			break;
	}

	if (chan)
		ast_channel_unlock(chan);

	return ret;
}

void pbx_builtin_pushvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;

	if (name[strlen(name)-1] == ')') {
		char *function = ast_strdupa(name);

		ast_log(LOG_WARNING, "Cannot push a value onto a function\n");
		ast_func_write(chan, function, value);
		return;
	}

	if (chan) {
		ast_channel_lock(chan);
		headp = ast_channel_varshead(chan);
	} else {
		ast_rwlock_wrlock(&globalslock);
		headp = &globals;
	}

	if (value && (newvariable = ast_var_assign(name, value))) {
		if (headp == &globals)
			ast_verb(2, "Setting global variable '%s' to '%s'\n", name, value);
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
	}

	if (chan)
		ast_channel_unlock(chan);
	else
		ast_rwlock_unlock(&globalslock);
}

int pbx_builtin_setvar_helper(struct ast_channel *chan, const char *name, const char *value)
{
	struct ast_var_t *newvariable;
	struct varshead *headp;
	const char *nametail = name;
	/*! True if the old value was not an empty string. */
	int old_value_existed = 0;

	if (name[strlen(name) - 1] == ')') {
		char *function = ast_strdupa(name);

		return ast_func_write(chan, function, value);
	}

	if (chan) {
		ast_channel_lock(chan);
		headp = ast_channel_varshead(chan);
	} else {
		ast_rwlock_wrlock(&globalslock);
		headp = &globals;
	}

	/* For comparison purposes, we have to strip leading underscores */
	if (*nametail == '_') {
		nametail++;
		if (*nametail == '_')
			nametail++;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(headp, newvariable, entries) {
		if (strcmp(ast_var_name(newvariable), nametail) == 0) {
			/* there is already such a variable, delete it */
			AST_LIST_REMOVE_CURRENT(entries);
			old_value_existed = !ast_strlen_zero(ast_var_value(newvariable));
			ast_var_delete(newvariable);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (value && (newvariable = ast_var_assign(name, value))) {
		if (headp == &globals) {
			ast_verb(2, "Setting global variable '%s' to '%s'\n", name, value);
		}
		AST_LIST_INSERT_HEAD(headp, newvariable, entries);
		ast_channel_publish_varset(chan, name, value);
	} else if (old_value_existed) {
		/* We just deleted a non-empty dialplan variable. */
		ast_channel_publish_varset(chan, name, "");
	}

	if (chan)
		ast_channel_unlock(chan);
	else
		ast_rwlock_unlock(&globalslock);
	return 0;
}

int pbx_builtin_setvar(struct ast_channel *chan, const char *data)
{
	char *name, *value, *mydata;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Set requires one variable name/value pair.\n");
		return 0;
	}

	mydata = ast_strdupa(data);
	name = strsep(&mydata, "=");
	value = mydata;
	if (!value) {
		ast_log(LOG_WARNING, "Set requires an '=' to be a valid assignment.\n");
		return 0;
	}

	if (strchr(name, ' ')) {
		ast_log(LOG_WARNING, "Please avoid unnecessary spaces on variables as it may lead to unexpected results ('%s' set to '%s').\n", name, mydata);
	}

	pbx_builtin_setvar_helper(chan, name, value);

	return 0;
}

int pbx_builtin_setvar_multiple(struct ast_channel *chan, const char *vdata)
{
	char *data;
	int x;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(pair)[99]; /* parse up to 99 variables */
	);
	AST_DECLARE_APP_ARGS(pair,
		AST_APP_ARG(name);
		AST_APP_ARG(value);
	);

	if (ast_strlen_zero(vdata)) {
		ast_log(LOG_WARNING, "MSet requires at least one variable name/value pair.\n");
		return 0;
	}

	data = ast_strdupa(vdata);
	AST_STANDARD_APP_ARGS(args, data);

	for (x = 0; x < args.argc; x++) {
		AST_NONSTANDARD_APP_ARGS(pair, args.pair[x], '=');
		if (pair.argc == 2) {
			pbx_builtin_setvar_helper(chan, pair.name, pair.value);
			if (strchr(pair.name, ' '))
				ast_log(LOG_WARNING, "Please avoid unnecessary spaces on variables as it may lead to unexpected results ('%s' set to '%s').\n", pair.name, pair.value);
		} else if (!chan) {
			ast_log(LOG_WARNING, "MSet: ignoring entry '%s' with no '='\n", pair.name);
		} else {
			ast_log(LOG_WARNING, "MSet: ignoring entry '%s' with no '=' (in %s@%s:%d\n", pair.name, ast_channel_exten(chan), ast_channel_context(chan), ast_channel_priority(chan));
		}
	}

	return 0;
}

void pbx_builtin_clear_globals(void)
{
	struct ast_var_t *vardata;

	ast_rwlock_wrlock(&globalslock);
	while ((vardata = AST_LIST_REMOVE_HEAD(&globals, entries)))
		ast_var_delete(vardata);
	ast_rwlock_unlock(&globalslock);
}

static struct ast_cli_entry vars_cli[] = {
	AST_CLI_DEFINE(handle_show_globals, "Show global dialplan variables"),
	AST_CLI_DEFINE(handle_show_chanvar, "Show channel variables"),
	AST_CLI_DEFINE(handle_eval_function, "Evaluate dialplan function"),
	AST_CLI_DEFINE(handle_set_global, "Set global dialplan variable"),
	AST_CLI_DEFINE(handle_set_chanvar, "Set a channel variable"),
};

static void unload_pbx_variables(void)
{
	ast_cli_unregister_multiple(vars_cli, ARRAY_LEN(vars_cli));
	ast_unregister_application("Set");
	ast_unregister_application("MSet");
	pbx_builtin_clear_globals();
}

int load_pbx_variables(void)
{
	int res = 0;

	res |= ast_cli_register_multiple(vars_cli, ARRAY_LEN(vars_cli));
	res |= ast_register_application2("Set", pbx_builtin_setvar, NULL, NULL, NULL);
	res |= ast_register_application2("MSet", pbx_builtin_setvar_multiple, NULL, NULL, NULL);
	ast_register_cleanup(unload_pbx_variables);

	return res;
}
