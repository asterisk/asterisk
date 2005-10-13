/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * Compile symbolic Asterisk Extension Logic into Asterisk extensions
 * 
 */

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/callerid.h"

struct stringlink {
	struct stringlink *next;
	char data[0];
};

#define FILLIN_BREAK		1
#define FILLIN_CONTINUE		2

struct fillin {
	struct fillin *next;
	char exten[AST_MAX_EXTENSION];
	int priority;
	int type;
};

#ifdef __AST_DEBUG_MALLOC
static void FREE(void *ptr)
{
	free(ptr);
}
#else
#define FREE free
#endif

#define DEBUG_READ   (1 << 0)
#define DEBUG_TOKENS (1 << 1)
#define DEBUG_MACROS (1 << 2)
#define DEBUG_CONTEXTS (1 << 3)

static int aeldebug = 0;

static char *dtext = "Asterisk Extension Language Compiler";
static char *config = "extensions.ael";
static char *registrar = "pbx_ael";

static char *__grab_token(char *src, const char *filename, int lineno, int link)
{
	char *c;
	char *b;
	char *a;
	int level = 0;
	char *ret;
#if 0
	if (aeldebug || DEBUG_TOKENS) 
		ast_verbose("Searching for token in '%s'!\n", src);
#endif
	c = src;
	while(*c) {
		if ((*c == '\\')) {
			c++;
			if (!*c)
				c--;
		} else {
			if ((*c == '{') || (*c == '(')) {
				level++;
			} else if ((*c == '}') || (*c == ')')) {
				if (level)
					level--;
				else
					ast_log(LOG_WARNING, "Syntax error at line %d of '%s', too many closing braces!\n", lineno, filename);
			} else if ((*c == ';') && !level) {
				/* Got a token! */
				*c = '\0';
				b = c;
				b--;
				c++;
				while((b > src) && (*b < 33)) { 
					*b = '\0'; 
					b--; 
				}
				a = src;
				while(*a && (*a < 33))
					a++;
				if (link) {
					ret = malloc(strlen(a) + sizeof(struct stringlink) + 1);
					if (ret)
						strcpy(ret + sizeof(struct stringlink), a);
				} else
					ret = strdup(a);
				/* Save remainder */
				memmove(src, c, strlen(c) + 1);
				return ret;
			}
		}
		c++;
	}
	return NULL;		
}

static char *grab_token(char *src, const char *filename, int lineno)
{
	return __grab_token(src, filename, lineno, 0);
}

static struct stringlink *arg_parse(char *args, const char *filename, int lineno)
{
	struct stringlink *cur, *prev=NULL, *root=NULL;
	if (args) {
		if (aeldebug & DEBUG_TOKENS) 
			ast_verbose("Parsing args '%s'!\n", args);
		if (args[0] == '{') {
			/* Strip mandatory '}' from end */
			args[strlen(args) - 1] = '\0';
			while ((cur = (struct stringlink *)__grab_token(args + 1, filename, lineno, 1))) {
				cur->next = NULL;
				if (prev)
					prev->next = cur;
				else
					root = cur;
				prev = cur;
			}
		} else if (*args) {
			root = malloc(sizeof(struct stringlink) + strlen(args) + 1);
			if (root) {
				strcpy(root->data, args);
				root->next = NULL;
			}
		}
	}
	return root;
}

static char *grab_else(char *args, const char *filename, int lineno)
{
	char *ret = NULL;
	int level=0;
	char *c;
	if (args) {
		if (args[0] == '{') {
			c = args;
			while(*c) {
				if (*c == '{')
					level++;
				else if (*c == '}') {
					level--;
					if (!level) {
						c++;
						while(*c && (*c < 33)) { *c = '\0'; c++; };
						if (!strncasecmp(c, "else", 4) && 
							((c[4] == '{') || (c[4] < 33))) {
								/* Ladies and gentlemen, we have an else clause */
							*c = '\0';
							c += 4;
							while(*c && (*c < 33)) c++;
							ret = c;
							if (aeldebug & DEBUG_TOKENS)
								ast_verbose("Returning else clause '%s'\n", c);
						}
						break;
					}
				}
				c++;
			}
		}
	}
	return ret;
}

static struct stringlink *param_parse(char *parms, const char *macro, const char *filename, int lineno)
{
	char *s, *e;
	struct stringlink *root = NULL, *prev=NULL, *cur;
	if (!parms || !*parms)
		return NULL;
	if (*parms != '(') {
		ast_log(LOG_NOTICE, "Syntax error in parameter list for macro '%s' at about line %d of %s: Expecting '(' but got '%c'\n", macro, lineno, filename, *parms);
		return NULL;
	}
	s = parms + 1;
	while(*s) {
		while(*s && (*s < 33)) s++;
		e = s;
		while(*e &&  (*e != ')') && (*e != ',')) {
			if (*e < 33)
				*e = '\0';
			e++;
		}
		if (*e) {
			/* Strip token */
			*e = '\0';
			e++;
			/* Skip over whitespace */
			while(*e && (*e < 33)) e++;
			/* Link */
			cur = malloc(strlen(s) + sizeof(struct stringlink) + 1);
			if (cur) {
				cur->next = NULL;
				strcpy(cur->data, s);
				if (prev)
					prev->next = cur;
				else
					root = cur;
				prev = cur;
			}
			s = e;
		}
	}
	return root;
}

static void arg_free(struct stringlink *cur)
{
	struct stringlink *last;
	while(cur) {
		last = cur;
		cur = cur->next;
		free(last);
	}
}

static void handle_globals(struct stringlink *vars)
{
	while(vars) {
		pbx_builtin_setvar(NULL, vars->data);
		vars = vars->next;
	}
}

static struct stringlink *split_token(char *token, const char *filename, int lineno)
{
	char *args, *p;
	struct stringlink *argv;
	args = token;
	while (*args && (*args > 32) && (*args != '{') && (*args != '(')) args++;
	if (*args) {
		p = args;
		while (*args && (*args < 33))
			args++;
		if (*args != '(') {
			*p = '\0';
		} else {
			while (*args && (*args != ')')) args++;
			if (*args == ')') {
				args++;
				while (*args && (*args < 33)) args++;
			}
		}
		if (!*args)
			args = NULL;
	} else args = NULL;
	argv = arg_parse(args, filename, lineno);
	if (args)
		*args = '\0';
	return argv;
}

static int matches_keyword(const char *data, const char *keyword)
{
	char c;
	if (!strncasecmp(data, keyword, strlen(keyword))) {
		c = data[strlen(keyword)];
		if ((c < 33) || (c == '(') || (c == '{'))
			return 1;
	}
	return 0;
}

static struct stringlink *split_params(char *token, const char *filename, int lineno)
{
	char *params;
	struct stringlink *paramv;
	params = token;
	while(*params && (*params > 32) && (*params != '(')) params++;
	if (*params) {
		if (*params != '(') {
			*params = '\0';
			params++;
			while(*params && (*params < 33))
				params++;
		}
		if (!*params)
			params = NULL;
	} else params = NULL;
	paramv = param_parse(params, token, filename, lineno);
	if (params)
		*params = '\0';
	return paramv;
}

static const char *get_case(char *s, char **restout, int *pattern)
{
	char *newcase=NULL;
	char *rest=NULL;
	if (!strncasecmp(s, "case", 4) && s[4] && ((s[4] < 33) || (s[4] == ':'))) {
		newcase = s + 4;
		while (*newcase && (*newcase < 33)) newcase++;
		rest = newcase;
		*pattern = 0;
	} else if (!strncasecmp(s, "pattern", 7) && s[7] && ((s[7] < 33) || (s[7] == ':'))) {
		newcase = s + 8;
		while (*newcase && (*newcase < 33)) newcase++;
		rest = newcase;
		*pattern = 1;
	} else if (!strncasecmp(s, "default", 7) && ((s[7] < 33) || (s[7] == ':'))) {
		newcase = ".";
		rest = s + 7;
		while (*rest && (*rest < 33)) rest++;
		*pattern = 1;
	}

	if (rest) {
		while (*rest && (*rest > 32) && (*rest != ':')) rest++;
		if (*rest) {
			*rest = 0;
			rest++;
			while (*rest && ((*rest == ':') || (*rest < 33))) rest++;
			*restout = rest;
		} else {
			*restout = "";
		}
	} else
		*restout = s;
	if (aeldebug & DEBUG_TOKENS)
		ast_verbose("GETCASE: newcase is '%s', rest = '%s'\n", newcase, *restout);
	return newcase;
}

static void fillin_free(struct fillin *fillin)
{
	struct fillin *cur, *next;
	cur =  fillin;
	while(cur) {
		next = cur->next;
		free(cur);
		cur = next;
	}
}

static void fillin_process(struct ast_context *con, struct fillin *fillin, const char *filename, int lineno, const char *breakexten, int breakprio, const char *contexten, int contprio)
{
	struct fillin *cur;
	char *app;
	char mdata[AST_MAX_EXTENSION + 20];
	cur = fillin;
	while(cur) {
		if (cur->type == FILLIN_BREAK) {
			if (breakexten && breakprio) {
				app = "Goto";
				snprintf(mdata, sizeof(mdata), "%s|%d", breakexten, breakprio);
			} else {
				app = "NoOp";
				snprintf(mdata, sizeof(mdata), "Invalid break");
				ast_log(LOG_NOTICE, "Ignoring inappropriate break around line %d of %s\n", lineno, filename);
			}
			if (ast_add_extension2(con, 0, cur->exten, cur->priority, NULL, NULL, app, strdup(mdata), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of break '%s'\n", cur->priority, cur->exten);
		} else if (cur->type == FILLIN_CONTINUE) {
			if (contexten && contprio) {
				app = "Goto";
				snprintf(mdata, sizeof(mdata), "%s|%d", contexten, contprio);
			} else {
				app = "NoOp";
				snprintf(mdata, sizeof(mdata), "Invalid continue");
				ast_log(LOG_NOTICE, "Ignoring inappropriate continue around line %d of %s\n", lineno, filename);
			}
			if (ast_add_extension2(con, 0, cur->exten, cur->priority, NULL, NULL, app, strdup(mdata), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of continue '%s'\n", cur->priority, cur->exten);
		} else {
			ast_log(LOG_WARNING, "Whoa, unknown fillin type '%d'\n", cur->type);
		}
		cur = cur->next;
	}
}

static int match_assignment(char *variable, char **value)
{
	char *c;
	char *ws;
	int inpar = 0;
	c = variable;
	
	while (*c && (*c > 32)) {
		if(*c == ')' && (inpar > 0)) {
			inpar--;
		} else if(*c == '(' && (inpar >= 0)) {
			inpar++;
		} else if(*c == '=' && (inpar == 0)) {
			break;
		}
		c++;
	} 
	ws = c;
	while (*c && (*c < 33)) c++;
	if (*c == '=') {
		*ws = '\0';
		*c = '\0';
		c++;
		while ((*c) && (*c < 33)) c++;
		*value = c;
		return 1;
	}
	return 0;
}

static int matches_label(char *data, char **rest)
{
	char last = 0;
	char *start = data;
	while (*data > 32) {
		last = *data;
		data++;
	}
	if (last != ':') {
		while (*data && (*data < 33)) data++;
		last = *data;
		data++;
	}
	if (last == ':') {
		*rest = data;
		/* Go back and trim up the label */
		while(*start && ((*start > 32) && (*start != ':'))) start++;
		*start = '\0';
		return 1;
	}
	return 0;
}

static char *argument_end(char *str)
{
	int level=0;
	while(*++str) {
		switch(*str) {
		case '(':
			level++;
			break;
		case ')':
			if(level)
				level--;
			else
				return str;
			break;
		default:
			break;
		}
	}
	return NULL;
}

static int build_step(const char *what, const char *name, const char *filename, int lineno, struct ast_context *con, char *exten, int *pos, char *data, struct fillin **fillout, char **label);
static int __build_step(const char *what, const char *name, const char *filename, int lineno, struct ast_context *con, char *exten, int *pos, char *data, struct fillin **fillout, char **label)
{
	char *app;
	char *args;
	char *c;
	char *margs=NULL;
	char *oargs;
	char *rest;
	const char *curcase, *newcase;
	struct stringlink *swargs, *cur;
	int cpos;
	int mlen;
	int pattern = 0;
	struct fillin *fillin;
	while (*data && (*data < 33)) data++;
	if (matches_label(data, &c)) {
		*label = data;
		data = c;
		while (*data && (*data < 33)) data++;
	}
	if (!data || ast_strlen_zero(data))
		return 0;
	if (matches_keyword(data, "switch")) {
		fillin = NULL;
		/* Switch */
		args = data + strlen("switch");
		while ((*args < 33) && (*args != '(')) args++;
		if ((*args == '(') && (c = argument_end(args))) {
			args++;
			*c = '\0';
			c++;
			if (aeldebug & DEBUG_TOKENS)
				ast_verbose("--SWITCH on : %s\n", args);
			mlen = strlen(exten) + 128 + strlen(args) + strlen(name);
			margs = alloca(mlen);
			app = "Goto";
			sprintf(margs, "sw-%s-%d-%s|1", name, *pos, args);
			ast_process_quotes_and_slashes(margs, ',', '|');
			oargs = args;
			args = margs;
			if (ast_add_extension2(con, 0, exten, *pos, *label, NULL, app, strdup(args), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
			else {
				*label = NULL;
				(*pos)++;
			}
			app = "NoOp";
			sprintf(margs, "Finish switch-%s-%d", name, *pos - 1);
			if (ast_add_extension2(con, 0, exten, *pos, *label, NULL, app, strdup(args), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
			else {
				*label = NULL;
				(*pos)++;
			}
			while(*c && (*c < 33)) c++;
			if (aeldebug & DEBUG_TOKENS)
				ast_verbose("ARG Parsing '%s'\n", c);
			swargs = arg_parse(c, filename, lineno);
			cur = swargs;
			curcase = NULL;
			while(cur) {
				if ((newcase = get_case(cur->data, &rest, &pattern))) {
					if (aeldebug & DEBUG_TOKENS)
						ast_verbose("--NEWCASE: '%s'!\n", newcase);
					if (curcase) {
						/* Handle fall through */
						char tmp[strlen(newcase) + strlen(name) + 40];
						sprintf(tmp, "sw-%s-%d-%s|%d", name, *pos - 2, newcase, 1);
						ast_add_extension2(con, 0, margs, cpos, NULL, NULL, "Goto", strdup(tmp), FREE, registrar);
					}
					curcase = newcase;
					cpos = 1;
					if (pattern)
						snprintf(margs, mlen, "_sw-%s-%d-%s", name, *pos - 2, curcase);
					else
						snprintf(margs, mlen, "sw-%s-%d-%s", name, *pos - 2, curcase);
					if (!strcasecmp(rest, "break")) {
						char tmp[strlen(exten) + 10];
						sprintf(tmp, "%s|%d", exten, *pos - 1);
						ast_add_extension2(con, 0, exten, cpos, *label, NULL, "Goto", strdup(tmp), FREE, registrar);
						curcase = NULL;
						*label = NULL;
					} else
						build_step("switch", margs, filename, lineno, con, margs, &cpos, rest, &fillin, label);
				} else if (curcase) {
					if (aeldebug & DEBUG_TOKENS)
						ast_verbose("Building statement from '%s'\n", rest);
					if (!strcasecmp(rest, "break")) {
						char tmp[strlen(exten) + 10];
						sprintf(tmp, "%s|%d", exten, *pos - 1);
						ast_add_extension2(con, 0, margs, cpos, *label, NULL, "Goto", strdup(tmp), FREE, registrar);
						curcase = NULL;
						*label = NULL;
					} else
						build_step("switch", margs, filename, lineno, con, margs, &cpos, rest, &fillin, label);
				} else 
					ast_log(LOG_WARNING, "Unreachable code in switch at about line %d of %s\n", lineno, filename);
				if (aeldebug & DEBUG_TOKENS)
					ast_verbose("--SWARG: %s\n", cur->data);
				cur = cur->next;
			}
			/* Can't do anything with these */
			fillin_process(con, fillin, filename, lineno, NULL, 0, NULL, 0);
			fillin_free(fillin);
			arg_free(swargs);
		} else
			ast_log(LOG_WARNING, "Syntax error in switch declaration in %s around line %d!\n", filename, lineno); 
			
	} else if (matches_keyword(data, "if")) {
		/* If... */
		args = data + strlen("if");
		while ((*args < 33) && (*args != '(')) args++;
		if ((*args == '(') && (c = argument_end(args))) {
			int ifblock;
			int ifstart;
			int elsestart;
			int ifend;
			int ifskip;
			char *elses;
			char *iflabel;
			args++;
			*c = '\0';
			c++;
			while(*c && (*c < 33)) c++;
			if (aeldebug & DEBUG_TOKENS)
				ast_verbose("--IF on : '%s' : '%s'\n", args, c);
			mlen = strlen(exten) + 128 + strlen(args) + strlen(name);
			margs = alloca(mlen);
			/* Remember where the ifblock starts, and skip over */
			ifblock = (*pos)++;
			iflabel = *label;
			*label = NULL;
			/* Remember where the start of the ifblock is */
			ifstart = *pos;
			snprintf(margs, mlen, "if-%s-%d", name, ifblock);
			/* Now process the block of the if */
			if (aeldebug & DEBUG_TOKENS)
				ast_verbose("Searching for elses in '%s'\n", c);
			elses = grab_else(c, filename, lineno);
			build_step("if", margs, filename, lineno, con, exten, pos, c, fillout, label);
			if (elses) {
				/* Reserve a goto to exit the if */
				ifskip = *pos;
				(*pos)++;
				elsestart = *pos;
				build_step("else", margs, filename, lineno, con, exten, pos, elses, fillout, label);
			} else {
				elsestart = *pos;
				ifskip = 0;
			}
			ifend = *pos;
			(*pos)++;
			app = "NoOp";
			snprintf(margs, mlen, "Finish if-%s-%d", name, ifblock);
			if (ast_add_extension2(con, 0, exten, ifend, *label, NULL, app, strdup(margs), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
			*label = NULL;
			app = "GotoIf";
			snprintf(margs, mlen, "$[ %s ]?%d:%d", args, ifstart, elsestart);
			if (ast_add_extension2(con, 0, exten, ifblock, iflabel, NULL, app, strdup(margs), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
			if (ifskip) {
				/* Skip as appropriate around else clause */
				snprintf(margs, mlen, "%d", ifend);
				if (ast_add_extension2(con, 0, exten, ifskip, NULL, NULL, "Goto", strdup(margs), FREE, registrar))
					ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
			}
		} else
			ast_log(LOG_WARNING, "Syntax error in if declaration in %s around line %d!\n", filename, lineno); 
	} else if (matches_keyword(data, "while")) {
		/* While... */
		fillin = NULL;
		args = data + strlen("while");
		while ((*args < 33) && (*args != '(')) args++;
		if ((*args == '(') && (c = argument_end(args))) {
			int whileblock;
			int whilestart;
			int whileend;
			char *whilelabel;
			args++;
			*c = '\0';
			c++;
			while(*c && (*c < 33)) c++;
			if (aeldebug & DEBUG_TOKENS)
				ast_verbose("--WHILE on : '%s' : '%s'\n", args, c);
			mlen = strlen(exten) + 128 + strlen(args) + strlen(name);
			margs = alloca(mlen);
			/* Remember where to put the conditional, and keep its position */
			whilestart = (*pos);
			whilelabel = *label;
			*label = NULL;
			(*pos)++;
			/* Remember where the whileblock starts */
			whileblock = (*pos);
			snprintf(margs, mlen, "while-%s-%d", name, whilestart);
			build_step("while", margs, filename, lineno, con, exten, pos, c, &fillin, label);
			/* Close the loop */
			app = "Goto";
			snprintf(margs, mlen, "%d", whilestart);
			if (ast_add_extension2(con, 0, exten, (*pos)++, *label, NULL, app, strdup(margs), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
			*label = NULL;
			whileend = (*pos);
			/* Place trailer */
			app = "NoOp";
			snprintf(margs, mlen, "Finish while-%s-%d", name, whilestart);
			if (ast_add_extension2(con, 0, exten, (*pos)++, *label, NULL, app, strdup(margs), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
			*label = NULL;
			app = "GotoIf";
			snprintf(margs, mlen, "$[ %s ]?%d:%d", args, whileblock, whileend);
			if (ast_add_extension2(con, 0, exten, whilestart, whilelabel, NULL, app, strdup(margs), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
			fillin_process(con, fillin, filename, lineno, exten, whileend, exten, whilestart);
			fillin_free(fillin);
		} else
			ast_log(LOG_WARNING, "Syntax error in while declaration in %s around line %d!\n", filename, lineno); 
	} else if (matches_keyword(data, "jump")) {
		char *p;
		/* Jump... */
		fillin = NULL;
		args = data + strlen("jump");
		while(*args && (*args < 33)) args++;
		if (aeldebug & DEBUG_TOKENS)
			ast_verbose("--JUMP to : '%s'\n", args);
		p = strchr(args, ',');
		if (p) {
			*p = '\0';
			p++;
		} else
			p = "1";
		c = strchr(args, '@');
		if (c) {
			*c = '\0';
			c++;
		}
		mlen = strlen(exten) + 128 + strlen(args) + strlen(name) + (c ? strlen(c) : 0);
		margs = alloca(mlen);
		if (c) 
			snprintf(margs, mlen, "%s|%s|%s", c,args, p);
		else
			snprintf(margs, mlen, "%s|%s", args, p);
		app = "Goto";
		if (ast_add_extension2(con, 0, exten, (*pos)++, *label, NULL, app, strdup(margs), FREE, registrar))
			ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
		*label = NULL;
	} else if (matches_keyword(data, "goto")) {
		/* Jump... */
		fillin = NULL;
		args = data + strlen("goto");
		while(*args && (*args < 33)) args++;
		if (aeldebug & DEBUG_TOKENS)
			ast_verbose("--GOTO to : '%s'\n", args);
		app = "Goto";
		if (ast_add_extension2(con, 0, exten, (*pos)++, *label, NULL, app, strdup(args), FREE, registrar))
			ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
		*label = NULL;
	} else if (matches_keyword(data, "for")) {
		/* While... */
		fillin = NULL;
		args = data + strlen("for");
		while ((*args < 33) && (*args != '(')) args++;
		if ((*args == '(') && (c = argument_end(args))) {
			int forblock;
			int forprep;
			int forstart;
			int forend;
			struct stringlink *fields;
			char *tmp;
			char *forlabel = NULL;
			args++;
			*c = '\0';
			c++;
			while(*c && (*c < 33)) c++;
			/* Parse arguments first */
			tmp = alloca(strlen(args) + 10);
			if (tmp) {
				snprintf(tmp, strlen(args) + 10, "{%s;}", args);
				fields = arg_parse(tmp, filename, lineno);
			} else
				fields = NULL;
			if (fields && fields->next && fields->next->next) {
				if (aeldebug & DEBUG_TOKENS)
					ast_verbose("--FOR ('%s' ; '%s' ; '%s') : '%s'\n", fields->data, fields->next->data, fields->next->next->data, c);
				mlen = strlen(exten) + 128 + strlen(args) + strlen(name);
				margs = alloca(mlen);
				forprep = *pos;
				snprintf(margs, mlen, "for-%s-%d", name, forprep);
				fillin = NULL;
				build_step("while", margs, filename, lineno, con, exten, pos, fields->data, &fillin, label);
				/* Remember where to put the conditional, and keep its position */
				forstart = (*pos);
				forlabel = *label;
				(*pos)++;
				*label = NULL;
				/* Remember where the whileblock starts */
				forblock = (*pos);
				build_step("for", margs, filename, lineno, con, exten, pos, fields->next->next->data, &fillin, label);
				build_step("for", margs, filename, lineno, con, exten, pos, c, &fillin, label);
				/* Close the loop */
				app = "Goto";
				snprintf(margs, mlen, "%d", forstart);
				if (ast_add_extension2(con, 0, exten, (*pos)++, *label, NULL, app, strdup(margs), FREE, registrar))
					ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
				*label = NULL;
				forend = (*pos);
				/* Place trailer */
				app = "NoOp";
				snprintf(margs, mlen, "Finish for-%s-%d", name, forprep);
				if (ast_add_extension2(con, 0, exten, (*pos)++, *label, NULL, app, strdup(margs), FREE, registrar))
					ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
				*label = NULL;
				app = "GotoIf";
				snprintf(margs, mlen, "$[ %s ]?%d:%d", fields->next->data, forblock, forend);
				if (ast_add_extension2(con, 0, exten, forstart, forlabel, NULL, app, strdup(margs), FREE, registrar))
					ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", forstart, what, name);
				fillin_process(con, fillin, filename, lineno, exten, forend, exten, forstart);
				fillin_free(fillin);
			} else
				ast_log(LOG_NOTICE, "Improper for declaration in %s around line %d!\n", filename, lineno); 
			arg_free(fields);
		} else
			ast_log(LOG_WARNING, "Syntax error in for declaration in %s around line %d!\n", filename, lineno); 
			
	} else if (!strcasecmp(data, "break") || !strcasecmp(data, "continue")) {
		struct fillin *fi;
		fi = malloc(sizeof(struct fillin));
		if (fi) {
			memset(fi, 0, sizeof(struct fillin));
			if (!strcasecmp(data, "break"))
				fi->type = FILLIN_BREAK;
			else
				fi->type = FILLIN_CONTINUE;
			ast_copy_string(fi->exten, exten, sizeof(fi->exten));
			fi->priority = (*pos)++;
			fi->next = *fillout;
			*fillout = fi;
		}
	} else if (match_assignment(data, &rest)) {
		if (aeldebug & DEBUG_TOKENS)
			ast_verbose("ASSIGN  '%s' = '%s'\n", data, rest);
		mlen = strlen(rest) + strlen(data) + 20;
		margs = alloca(mlen);
		snprintf(margs, mlen, "%s=$[ %s ]", data, rest);
		app = "Set";
		if (ast_add_extension2(con, 0, exten, *pos, *label, NULL, app, strdup(margs), FREE, registrar))
			ast_log(LOG_WARNING, "Unable to add assignment at priority '%d' of %s '%s'\n", *pos, what, name);
		else {
			*label = NULL;
			(*pos)++;
		}
	} else {
		app = data;
		args = app;
		while (*args && (*args > 32) && (*args != '(')) args++;
			if (*args != '(') {
			while(*args && (*args != '(')) { *args = '\0'; args++; };
		}
		if (*args == '(') {
			*args = '\0';
			args++;
			/* Got arguments, trim trailing ')' */
			c = args + strlen(args) - 1;
			while((c >= args) && (*c < 33) && (*c != ')')) { *c = '\0'; c--; };
			if ((c >= args) && (*c == ')')) *c = '\0';
		} else
			args = "";
		ast_process_quotes_and_slashes(args, ',', '|');
		if (app[0] == '&') {
			app++;
			margs = alloca(strlen(args) + strlen(app) + 10);
			sprintf(margs, "%s|%s", app, args);
			args = margs;
			app = "Macro";
		}
		if (aeldebug & DEBUG_TOKENS)
			ast_verbose("-- APP: '%s', ARGS: '%s'\n", app, args);
		if (ast_add_extension2(con, 0, exten, *pos, *label, NULL, app, strdup(args), FREE, registrar))
			ast_log(LOG_WARNING, "Unable to add step at priority '%d' of %s '%s'\n", *pos, what, name);
		else {
			(*pos)++;
			*label = NULL;
		}
	}
	return 0;
}

static int build_step(const char *what, const char *name, const char *filename, int lineno, struct ast_context *con, char *exten, int *pos, char *data, struct fillin **fillout, char **label)
{
	struct stringlink *args, *cur;
	int res=0;
	struct fillin *fillin=NULL;
	int dropfill = 0;
	char *labelin = NULL;
	if (!fillout) {
		fillout = &fillin;
		dropfill = 1;
	}
	if (!label) {
		label = &labelin;
	};
	args = arg_parse(data, filename, lineno);
	cur = args;
	while(cur) {
		res |= __build_step(what, name, filename, lineno, con, exten, pos, cur->data, fillout, label);
		cur = cur->next;
	}
	arg_free(args);
	if (dropfill) {
		fillin_process(con, fillin, filename, lineno, NULL, 0, NULL, 0);
		fillin_free(fillin);
	}
	return res;
}

static int parse_catch(char *data, char **catch, char **rest)
{
	/* Skip the word 'catch' */
	data += 5;
	while (*data && (*data < 33)) data++;
	/* Here's the extension */
	*catch = data;
	if (!*data)
		return 0;
	while (*data && (*data > 32)) data++;
	if (!*data)
		return 0;
	/* Trim any trailing spaces */
	*data = '\0';
	data++;
	while(*data && (*data < 33)) data++;
	if (!*data)
		return 0;
	*rest = data;
	return 1;
}

static void handle_macro(struct ast_context **local_contexts, struct stringlink *vars, const char *filename, int lineno)
{
	struct stringlink *argv;
	struct stringlink *paramv;
	struct stringlink *cur;
	struct ast_context *con;
	struct fillin *fillin;
	char *catch, *rest;
	char name[256];
	int pos;
	int cpos;

	if (aeldebug & DEBUG_MACROS)
		ast_verbose("Root macro def is '%s'\n", vars->data);
	argv = split_token(vars->data, filename, lineno);
	paramv = split_params(vars->data, filename, lineno);
	if (aeldebug & DEBUG_MACROS) 
		ast_verbose("Found macro '%s'\n", vars->data);
	snprintf(name, sizeof(name), "macro-%s", vars->data);
	con = ast_context_create(local_contexts, name, registrar);
	if (con) {
		pos = 1;
		cur = paramv;
		while(cur) {
			if (aeldebug & DEBUG_MACROS)
				ast_verbose("  PARAM => '%s'\n", cur->data);
			snprintf(name, sizeof(name), "%s=${ARG%d}", cur->data, pos);
			if (ast_add_extension2(con, 0, "s", pos, NULL, NULL, "Set", strdup(name), FREE, registrar))
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of macro '%s'\n", pos, vars->data);
			else
				pos++;
			cur = cur->next;
		}
		cur = argv;
		while(cur) {
			if (aeldebug & DEBUG_MACROS)
				ast_verbose("  STEP => '%s'\n", cur->data);
			if (matches_keyword(cur->data, "catch")) {
				if (aeldebug & DEBUG_MACROS)
					ast_verbose("--CATCH: '%s'\n", cur->data);
				if (parse_catch(cur->data, &catch, &rest)) {
					cpos = 1;
					build_step("catch", catch, filename, lineno, con, catch, &cpos, rest, NULL, NULL);
				} else
					ast_log(LOG_NOTICE, "Parse error for catch at about line %d of %s\n", lineno, filename);
			} else {
				fillin = NULL;
				build_step("macro", vars->data, filename, lineno, con, "s", &pos, cur->data, NULL, NULL);
			}
			cur = cur->next;
		}
	} else
		ast_log(LOG_WARNING, "Unable to create context '%s'\n", name);
	arg_free(argv);
	if (vars->next)
		ast_log(LOG_NOTICE, "Ignoring excess tokens in macro definition around line %d of %s!\n", lineno, filename);
}

static int matches_extension(char *exten, char **extout)
{
	char *c;
	*extout = NULL;
	c = exten;
	while(*c && (*c > 32)) c++;
	if (*c) {
		*c = '\0';
		c++;
		while(*c && (*c < 33)) c++;
		if (*c) {
			if (*c == '=') {
				*c = '\0';
				c++;
				if (*c == '>')
					c++;
				while (*c && (*c < 33)) c++;
				*extout = c;
				return 1;
			}
		}
	}
	return 0;
}

static void parse_keyword(char *s, char **o)
{
	char *c;
	c = s;
	while((*c) && (*c > 32)) c++;
	if (*c) {
		*c = '\0';
		c++;
		while(*c && (*c < 33)) c++;
		*o = c;
	} else
		*o = NULL;
}

static void handle_context(struct ast_context **local_contexts, struct stringlink *vars, const char *filename, int lineno)
{
	struct stringlink *argv;
	struct stringlink *paramv;
	struct stringlink *cur2;
	struct stringlink *argv2;
	struct stringlink *cur;
	struct ast_context *con;
	char *rest;
	char *c;
	char name[256];
	int pos;

	if (aeldebug & DEBUG_CONTEXTS)
		ast_verbose("Root context def is '%s'\n", vars->data);
	argv = split_token(vars->data, filename, lineno);
	paramv = split_params(vars->data, filename, lineno);
	if (aeldebug & DEBUG_CONTEXTS) 
		ast_verbose("Found context '%s'\n", vars->data);
	snprintf(name, sizeof(name), "%s", vars->data);
	con = ast_context_create(local_contexts, name, registrar);
	if (con) {
		cur = argv;
		while(cur) {
			if (matches_keyword(cur->data, "includes")) {
				if (aeldebug & DEBUG_CONTEXTS)
					ast_verbose("--INCLUDES: '%s'\n", cur->data);
				parse_keyword(cur->data, &rest);
				if (rest) {
					argv2 = arg_parse(rest, filename, lineno);
					cur2 = argv2;
					while(cur2) {
						ast_context_add_include2(con, cur2->data, registrar);
						cur2 = cur2->next;
					}
					arg_free(argv2);
				}
			} else if (matches_keyword(cur->data, "ignorepat")) {
				if (aeldebug & DEBUG_CONTEXTS)
					ast_verbose("--IGNOREPAT: '%s'\n", cur->data);
				parse_keyword(cur->data, &rest);
				if (rest) {
					argv2 = arg_parse(rest, filename, lineno);
					cur2 = argv2;
					while(cur2) {
						ast_context_add_ignorepat2(con, cur2->data, registrar);
						cur2 = cur2->next;
					}
					arg_free(argv2);
				}
			} else if (matches_keyword(cur->data, "switches") || matches_keyword(cur->data, "eswitches")) {
				if (aeldebug & DEBUG_CONTEXTS)
					ast_verbose("--[E]SWITCH: '%s'\n", cur->data);
				parse_keyword(cur->data, &rest);
				if (rest) {
					argv2 = arg_parse(rest, filename, lineno);
					cur2 = argv2;
					while(cur2) {
						c = strchr(cur2->data, '/');
						if (c) {
							*c = '\0';
							c++;
						} else
							c = "";
						ast_context_add_switch2(con, cur2->data, c, (cur->data[0] == 'e'), registrar);
						cur2 = cur2->next;
					}
					arg_free(argv2);
				}
			} else if (matches_extension(cur->data, &rest)) {
				if (aeldebug & DEBUG_CONTEXTS)
					ast_verbose("Extension: '%s' => '%s'\n", cur->data, rest);
				pos = 1;
				build_step("extension", cur->data, filename, lineno, con, cur->data, &pos, rest, NULL, NULL);
			}
			cur = cur->next;
		}
	} else
			ast_log(LOG_WARNING, "Unable to create context '%s'\n", name);
	arg_free(argv);
	if (vars->next)
		ast_log(LOG_NOTICE, "Ignoring excess tokens in macro definition around line %d of %s!\n", lineno, filename);
}

static int handle_root_token(struct ast_context **local_contexts, char *token, int level, const char *filename, int lineno)
{
	struct stringlink *argv, *cur;
	argv = split_token(token, filename, lineno);
	if (aeldebug & DEBUG_TOKENS) {
		ast_verbose("Found root token '%s' at level %d (%s:%d)!\n", token, level, filename, lineno);
		cur = argv;
		while(cur) {
			ast_verbose("   ARG => '%s'\n", cur->data);
			cur = cur->next;
		}
	}
	if (!strcasecmp(token, "globals")) {
		handle_globals(argv);
	} else if (!strcasecmp(token, "macro")) {
		handle_macro(local_contexts, argv, filename, lineno);
	} else if (!strcasecmp(token, "context")) {
		handle_context(local_contexts, argv, filename, lineno);
	} else {
		ast_log(LOG_NOTICE, "Unknown root token '%s'\n", token);
	}
	arg_free(argv);
	return 0;
}


static int ast_ael_compile(struct ast_context **local_contexts, const char *filename)
{
	char *rfilename;
	char *buf, *tbuf;
	int bufsiz;
	FILE *f;
	char *c;
	char *token;
	int lineno=0;

	if (filename[0] == '/')
		rfilename = (char *)filename;
	else {
		rfilename = alloca(strlen(filename) + strlen(ast_config_AST_CONFIG_DIR) + 2);
		sprintf(rfilename, "%s/%s", ast_config_AST_CONFIG_DIR, filename);
	}
	
	f = fopen(rfilename, "r");
	if (!f) {
		ast_log(LOG_WARNING, "Unable to open '%s': %s\n", rfilename, strerror(errno));
		return -1;
	}
	buf = malloc(4096);
	if (!buf) {
		ast_log(LOG_WARNING, "Out of memory!\n");
		fclose(f);
		return -1;
	}
	buf[0] = 0;
	bufsiz = 4096;
	while(!feof(f)) {
		if (bufsiz - strlen(buf) < 2048) {
			bufsiz += 4096;
			tbuf = realloc(buf, bufsiz);
			if (tbuf) {
				buf = tbuf;
			} else {
				free(buf);
				ast_log(LOG_WARNING, "Out of memory!\n");
				fclose(f);
			}
		}
		if (fgets(buf + strlen(buf), bufsiz - strlen(buf), f)) {
			lineno++;
			while(*buf && buf[strlen(buf) - 1] < 33)
				buf[strlen(buf) - 1] = '\0';
			c = strstr(buf, "//");
			if (c)
				*c = '\0';
			if (*buf) {
				if (aeldebug & DEBUG_READ)
					ast_verbose("Newly composed line '%s'\n", buf);
				while((token = grab_token(buf, filename, lineno))) {
					handle_root_token(local_contexts, token, 0, filename, lineno);
					free(token);
				}
			}
		}
	};
	free(buf);
	fclose(f);
	return 0;
}

static int pbx_load_module(void)
{
	struct ast_context *local_contexts=NULL, *con;
	ast_ael_compile(&local_contexts, config);
	ast_merge_contexts_and_delete(&local_contexts, registrar);
	for (con = ast_walk_contexts(NULL); con; con = ast_walk_contexts(con))
		ast_context_verify_includes(con);

	return 0;
}

/* CLI interface */
static int ael_debug_read(int fd, int argc, char *argv[])
{
	aeldebug |= DEBUG_READ;
	return 0;
}

static int ael_debug_tokens(int fd, int argc, char *argv[])
{
	aeldebug |= DEBUG_TOKENS;
	return 0;
}

static int ael_debug_macros(int fd, int argc, char *argv[])
{
	aeldebug |= DEBUG_MACROS;
	return 0;
}

static int ael_debug_contexts(int fd, int argc, char *argv[])
{
	aeldebug |= DEBUG_CONTEXTS;
	return 0;
}

static int ael_no_debug(int fd, int argc, char *argv[])
{
	aeldebug = 0;
	return 0;
}

static int ael_reload(int fd, int argc, char *argv[])
{
	ast_context_destroy(NULL, registrar);
	return (pbx_load_module());
}

static struct ast_cli_entry  ael_cli[] = {
	{ { "ael", "reload", NULL }, ael_reload, "Reload AEL configuration"},
	{ { "ael", "debug", "read", NULL }, ael_debug_read, "Enable AEL read debug"},
	{ { "ael", "debug", "tokens", NULL }, ael_debug_tokens, "Enable AEL tokens debug"},
	{ { "ael", "debug", "macros", NULL }, ael_debug_macros, "Enable AEL macros debug"},
	{ { "ael", "debug", "contexts", NULL }, ael_debug_contexts, "Enable AEL contexts debug"},
	{ { "ael", "no", "debug", NULL }, ael_no_debug, "Disable AEL debug messages"},
};

/*
 * Standard module functions ...
 */
int unload_module(void)
{
	ast_context_destroy(NULL, registrar);
	ast_cli_unregister_multiple(ael_cli, sizeof(ael_cli)/ sizeof(ael_cli[0]));
	return 0;
}


int load_module(void)
{
	ast_cli_register_multiple(ael_cli, sizeof(ael_cli)/ sizeof(ael_cli[0]));
	return (pbx_load_module());
}

int reload(void)
{
	unload_module();
	return (load_module());
}

int usecount(void)
{
	return 0;
}

char *description(void)
{
	return dtext;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}
