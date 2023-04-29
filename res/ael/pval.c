
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 *
 * Steve Murphy <murf@parsetree.com>
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
 * \brief Compile symbolic Asterisk Extension Logic into Asterisk extensions, version 2.
 *
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#define ASTMM_LIBC ASTMM_REDIRECT
#include "asterisk.h"

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <sys/stat.h>

#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/callerid.h"
#include "asterisk/pval.h"
#include "asterisk/ael_structs.h"
#ifdef AAL_ARGCHECK
#include "asterisk/argdesc.h"
#endif
#include "asterisk/utils.h"

extern int localized_pbx_load_module(void);

static char expr_output[2096];
#define BUF_SIZE 2000

/* these functions are in ../ast_expr2.fl */

static int errs, warns;
static int notes;
#ifdef STANDALONE
static int extensions_dot_conf_loaded = 0;
#endif
static char *registrar = "pbx_ael";

static pval *current_db;
static pval *current_context;
static pval *current_extension;

static const char *match_context;
static const char *match_exten;
static const char *match_label;
static int in_abstract_context;
static int count_labels; /* true, put matcher in label counting mode */
static int label_count;  /* labels are only meant to be counted in a context or exten */
static int return_on_context_match;
static pval *last_matched_label;
struct pval *match_pval(pval *item);
static void check_timerange(pval *p);
static void check_dow(pval *DOW);
static void check_day(pval *DAY);
static void check_month(pval *MON);
static void check_expr2_input(pval *expr, char *str);
static int extension_matches(pval *here, const char *exten, const char *pattern);
static void check_goto(pval *item);
static void find_pval_goto_item(pval *item, int lev);
static void find_pval_gotos(pval *item, int lev);
static int check_break(pval *item);
static int check_continue(pval *item);
static void check_label(pval *item);
static void check_macro_returns(pval *macro);

static struct pval *find_label_in_current_context(char *exten, char *label, pval *curr_cont);
static struct pval *find_first_label_in_current_context(char *label, pval *curr_cont);
static void print_pval_list(FILE *fin, pval *item, int depth);

static struct pval *find_label_in_current_extension(const char *label, pval *curr_ext);
static struct pval *find_label_in_current_db(const char *context, const char *exten, const char *label);
static pval *get_goto_target(pval *item);
static int label_inside_case(pval *label);
static void attach_exten(struct ael_extension **list, struct ael_extension *newmem);
static void fix_gotos_in_extensions(struct ael_extension *exten);
static pval *get_extension_or_contxt(pval *p);
static pval *get_contxt(pval *p);
static void remove_spaces_before_equals(char *str);

/* PRETTY PRINTER FOR AEL:  ============================================================================= */

static void print_pval(FILE *fin, pval *item, int depth)
{
	int i;
	pval *lp;

	for (i=0; i<depth; i++) {
		fprintf(fin, "\t"); /* depth == indentation */
	}

	switch ( item->type ) {
	case PV_WORD:
		fprintf(fin,"%s;\n", item->u1.str); /* usually, words are encapsulated in something else */
		break;

	case PV_MACRO:
		fprintf(fin,"macro %s(", item->u1.str);
		for (lp=item->u2.arglist; lp; lp=lp->next) {
			if (lp != item->u2.arglist )
				fprintf(fin,", ");
			fprintf(fin,"%s", lp->u1.str);
		}
		fprintf(fin,") {\n");
		print_pval_list(fin,item->u3.macro_statements,depth+1);
		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"};\n\n");
		break;

	case PV_CONTEXT:
		if ( item->u3.abstract )
			fprintf(fin,"abstract context %s {\n", item->u1.str);
		else
			fprintf(fin,"context %s {\n", item->u1.str);
		print_pval_list(fin,item->u2.statements,depth+1);
		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"};\n\n");
		break;

	case PV_MACRO_CALL:
		fprintf(fin,"&%s(", item->u1.str);
		for (lp=item->u2.arglist; lp; lp=lp->next) {
			if ( lp != item->u2.arglist )
				fprintf(fin,", ");
			fprintf(fin,"%s", lp->u1.str);
		}
		fprintf(fin,");\n");
		break;

	case PV_APPLICATION_CALL:
		fprintf(fin,"%s(", item->u1.str);
		for (lp=item->u2.arglist; lp; lp=lp->next) {
			if ( lp != item->u2.arglist )
				fprintf(fin,",");
			fprintf(fin,"%s", lp->u1.str);
		}
		fprintf(fin,");\n");
		break;

	case PV_CASE:
		fprintf(fin,"case %s:\n", item->u1.str);
		print_pval_list(fin,item->u2.statements, depth+1);
		break;

	case PV_PATTERN:
		fprintf(fin,"pattern %s:\n", item->u1.str);
		print_pval_list(fin,item->u2.statements, depth+1);
		break;

	case PV_DEFAULT:
		fprintf(fin,"default:\n");
		print_pval_list(fin,item->u2.statements, depth+1);
		break;

	case PV_CATCH:
		fprintf(fin,"catch %s {\n", item->u1.str);
		print_pval_list(fin,item->u2.statements, depth+1);
		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"};\n");
		break;

	case PV_SWITCHES:
		fprintf(fin,"switches {\n");
		print_pval_list(fin,item->u1.list,depth+1);
		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"};\n");
		break;

	case PV_ESWITCHES:
		fprintf(fin,"eswitches {\n");
		print_pval_list(fin,item->u1.list,depth+1);
		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"};\n");
		break;

	case PV_INCLUDES:
		fprintf(fin,"includes {\n");
		for (lp=item->u1.list; lp; lp=lp->next) {
			for (i=0; i<depth+1; i++) {
				fprintf(fin,"\t"); /* depth == indentation */
			}
			fprintf(fin,"%s", lp->u1.str); /* usually, words are encapsulated in something else */
			if (lp->u2.arglist)
				fprintf(fin,"|%s|%s|%s|%s",
						lp->u2.arglist->u1.str,
						lp->u2.arglist->next->u1.str,
						lp->u2.arglist->next->next->u1.str,
						lp->u2.arglist->next->next->next->u1.str
					);
			fprintf(fin,";\n"); /* usually, words are encapsulated in something else */
		}

		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"};\n");
		break;

	case PV_STATEMENTBLOCK:
		fprintf(fin,"{\n");
		print_pval_list(fin,item->u1.list, depth+1);
		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"}\n");
		break;

	case PV_VARDEC:
		fprintf(fin,"%s=%s;\n", item->u1.str, item->u2.val);
		break;

	case PV_LOCALVARDEC:
		fprintf(fin,"local %s=%s;\n", item->u1.str, item->u2.val);
		break;

	case PV_GOTO:
		fprintf(fin,"goto %s", item->u1.list->u1.str);
		if ( item->u1.list->next )
			fprintf(fin,",%s", item->u1.list->next->u1.str);
		if ( item->u1.list->next && item->u1.list->next->next )
			fprintf(fin,",%s", item->u1.list->next->next->u1.str);
		fprintf(fin,"\n");
		break;

	case PV_LABEL:
		fprintf(fin,"%s:\n", item->u1.str);
		break;

	case PV_FOR:
		fprintf(fin,"for (%s; %s; %s)\n", item->u1.for_init, item->u2.for_test, item->u3.for_inc);
		print_pval_list(fin,item->u4.for_statements,depth+1);
		break;

	case PV_WHILE:
		fprintf(fin,"while (%s)\n", item->u1.str);
		print_pval_list(fin,item->u2.statements,depth+1);
		break;

	case PV_BREAK:
		fprintf(fin,"break;\n");
		break;

	case PV_RETURN:
		fprintf(fin,"return;\n");
		break;

	case PV_CONTINUE:
		fprintf(fin,"continue;\n");
		break;

	case PV_RANDOM:
	case PV_IFTIME:
	case PV_IF:
		if ( item->type == PV_IFTIME ) {

			fprintf(fin,"ifTime ( %s|%s|%s|%s )\n",
					item->u1.list->u1.str,
					item->u1.list->next->u1.str,
					item->u1.list->next->next->u1.str,
					item->u1.list->next->next->next->u1.str
					);
		} else if ( item->type == PV_RANDOM ) {
			fprintf(fin,"random ( %s )\n", item->u1.str );
		} else
			fprintf(fin,"if ( %s )\n", item->u1.str);
		if ( item->u2.statements && item->u2.statements->next ) {
			for (i=0; i<depth; i++) {
				fprintf(fin,"\t"); /* depth == indentation */
			}
			fprintf(fin,"{\n");
			print_pval_list(fin,item->u2.statements,depth+1);
			for (i=0; i<depth; i++) {
				fprintf(fin,"\t"); /* depth == indentation */
			}
			if ( item->u3.else_statements )
				fprintf(fin,"}\n");
			else
				fprintf(fin,"};\n");
		} else if (item->u2.statements ) {
			print_pval_list(fin,item->u2.statements,depth+1);
		} else {
			if (item->u3.else_statements )
				fprintf(fin, " {} ");
			else
				fprintf(fin, " {}; ");
		}
		if ( item->u3.else_statements ) {
			for (i=0; i<depth; i++) {
				fprintf(fin,"\t"); /* depth == indentation */
			}
			fprintf(fin,"else\n");
			print_pval_list(fin,item->u3.else_statements, depth);
		}
		break;

	case PV_SWITCH:
		fprintf(fin,"switch( %s ) {\n", item->u1.str);
		print_pval_list(fin,item->u2.statements,depth+1);
		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"}\n");
		break;

	case PV_EXTENSION:
		if ( item->u4.regexten )
			fprintf(fin, "regexten ");
		if ( item->u3.hints )
			fprintf(fin,"hints(%s) ", item->u3.hints);

		fprintf(fin,"%s => ", item->u1.str);
		print_pval_list(fin,item->u2.statements,depth+1);
		fprintf(fin,"\n");
		break;

	case PV_IGNOREPAT:
		fprintf(fin,"ignorepat => %s;\n", item->u1.str);
		break;

	case PV_GLOBALS:
		fprintf(fin,"globals {\n");
		print_pval_list(fin,item->u1.statements,depth+1);
		for (i=0; i<depth; i++) {
			fprintf(fin,"\t"); /* depth == indentation */
		}
		fprintf(fin,"}\n");
		break;
	}
}

static void print_pval_list(FILE *fin, pval *item, int depth)
{
	pval *i;

	for (i=item; i; i=i->next) {
		print_pval(fin, i, depth);
	}
}

void ael2_print(char *fname, pval *tree)
{
	FILE *fin = fopen(fname,"w");
	if ( !fin ) {
		ast_log(LOG_ERROR, "Couldn't open %s for writing.\n", fname);
		return;
	}
	print_pval_list(fin, tree, 0);
	fclose(fin);
}


/* EMPTY TEMPLATE FUNCS FOR AEL TRAVERSAL:  ============================================================================= */

void traverse_pval_template(pval *item, int depth);
void traverse_pval_item_template(pval *item, int depth);


void traverse_pval_item_template(pval *item, int depth)/* depth comes in handy for a pretty print (indentation),
														  but you may not need it */
{
	pval *lp;

	switch ( item->type ) {
	case PV_WORD:
		/* fields: item->u1.str == string associated with this (word). */
		break;

	case PV_MACRO:
		/* fields: item->u1.str     == name of macro
		           item->u2.arglist == pval list of PV_WORD arguments of macro, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg

				   item->u3.macro_statements == pval list of statements in macro body.
		*/
		for (lp=item->u2.arglist; lp; lp=lp->next) {

		}
		traverse_pval_item_template(item->u3.macro_statements,depth+1);
		break;

	case PV_CONTEXT:
		/* fields: item->u1.str     == name of context
		           item->u2.statements == pval list of statements in context body
				   item->u3.abstract == int 1 if an abstract keyword were present
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		break;

	case PV_MACRO_CALL:
		/* fields: item->u1.str     == name of macro to call
		           item->u2.arglist == pval list of PV_WORD arguments of macro call, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg
		*/
		for (lp=item->u2.arglist; lp; lp=lp->next) {
		}
		break;

	case PV_APPLICATION_CALL:
		/* fields: item->u1.str     == name of application to call
		           item->u2.arglist == pval list of PV_WORD arguments of macro call, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg
		*/
		for (lp=item->u2.arglist; lp; lp=lp->next) {
		}
		break;

	case PV_CASE:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		break;

	case PV_PATTERN:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		break;

	case PV_DEFAULT:
		/* fields:
		           item->u2.statements == pval list of statements under the case
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		break;

	case PV_CATCH:
		/* fields: item->u1.str     == name of extension to catch
		           item->u2.statements == pval list of statements in context body
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		break;

	case PV_SWITCHES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		traverse_pval_item_template(item->u1.list,depth+1);
		break;

	case PV_ESWITCHES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		traverse_pval_item_template(item->u1.list,depth+1);
		break;

	case PV_INCLUDES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		           item->u2.arglist  == pval list of 4 PV_WORD elements for time values
		*/
		traverse_pval_item_template(item->u1.list,depth+1);
		traverse_pval_item_template(item->u2.arglist,depth+1);
		break;

	case PV_STATEMENTBLOCK:
		/* fields: item->u1.list     == pval list of statements in block, one per entry in the list
		*/
		traverse_pval_item_template(item->u1.list,depth+1);
		break;

	case PV_LOCALVARDEC:
	case PV_VARDEC:
		/* fields: item->u1.str     == variable name
		           item->u2.val     == variable value to assign
		*/
		break;

	case PV_GOTO:
		/* fields: item->u1.list     == pval list of PV_WORD target names, up to 3, in order as given by user.
		           item->u1.list->u1.str  == where the data on a PV_WORD will always be.
		*/

		if ( item->u1.list->next )
			;
		if ( item->u1.list->next && item->u1.list->next->next )
			;

		break;

	case PV_LABEL:
		/* fields: item->u1.str     == label name
		*/
		break;

	case PV_FOR:
		/* fields: item->u1.for_init     == a string containing the initializer
		           item->u2.for_test     == a string containing the loop test
		           item->u3.for_inc      == a string containing the loop increment

				   item->u4.for_statements == a pval list of statements in the for ()
		*/
		traverse_pval_item_template(item->u4.for_statements,depth+1);
		break;

	case PV_WHILE:
		/* fields: item->u1.str        == the while conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the while ()
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		break;

	case PV_BREAK:
		/* fields: none
		*/
		break;

	case PV_RETURN:
		/* fields: none
		*/
		break;

	case PV_CONTINUE:
		/* fields: none
		*/
		break;

	case PV_IFTIME:
		/* fields: item->u1.list        == there are 4 linked PV_WORDs here.

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		if ( item->u3.else_statements ) {
			traverse_pval_item_template(item->u3.else_statements,depth+1);
		}
		break;

	case PV_RANDOM:
		/* fields: item->u1.str        == the random number expression, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		if ( item->u3.else_statements ) {
			traverse_pval_item_template(item->u3.else_statements,depth+1);
		}
		break;

	case PV_IF:
		/* fields: item->u1.str        == the if conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		if ( item->u3.else_statements ) {
			traverse_pval_item_template(item->u3.else_statements,depth+1);
		}
		break;

	case PV_SWITCH:
		/* fields: item->u1.str        == the switch expression

				   item->u2.statements == a pval list of statements in the switch,
				   							(will be case statements, most likely!)
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		break;

	case PV_EXTENSION:
		/* fields: item->u1.str        == the extension name, label, whatever it's called

				   item->u2.statements == a pval list of statements in the extension
				   item->u3.hints      == a char * hint argument
				   item->u4.regexten   == an int boolean. non-zero says that regexten was specified
		*/
		traverse_pval_item_template(item->u2.statements,depth+1);
		break;

	case PV_IGNOREPAT:
		/* fields: item->u1.str        == the ignorepat data
		*/
		break;

	case PV_GLOBALS:
		/* fields: item->u1.statements     == pval list of statements, usually vardecs
		*/
		traverse_pval_item_template(item->u1.statements,depth+1);
		break;
	}
}

void traverse_pval_template(pval *item, int depth) /* depth comes in handy for a pretty print (indentation),
													  but you may not need it */
{
	pval *i;

	for (i=item; i; i=i->next) {
		traverse_pval_item_template(i, depth);
	}
}


/* SEMANTIC CHECKING FOR AEL:  ============================================================================= */

/*   (not all that is syntactically legal is good! */


static void check_macro_returns(pval *macro)
{
	pval *i;
	if (!macro->u3.macro_statements)
	{
		pval *z = calloc(1, sizeof(struct pval));
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The macro %s is empty! I will insert a return.\n",
				macro->filename, macro->startline, macro->endline, macro->u1.str);

		z->type = PV_RETURN;
		z->startline = macro->startline;
		z->endline = macro->endline;
		z->startcol = macro->startcol;
		z->endcol = macro->endcol;
		z->filename = strdup(macro->filename);

		macro->u3.macro_statements = z;
		return;
	}
	for (i=macro->u3.macro_statements; i; i=i->next) {
		/* if the last statement in the list is not return, then insert a return there */
		if (i->next == NULL) {
			if (i->type != PV_RETURN) {
				pval *z = calloc(1, sizeof(struct pval));
				ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The macro %s does not end with a return; I will insert one.\n",
						macro->filename, macro->startline, macro->endline, macro->u1.str);

				z->type = PV_RETURN;
				z->startline = macro->startline;
				z->endline = macro->endline;
				z->startcol = macro->startcol;
				z->endcol = macro->endcol;
				z->filename = strdup(macro->filename);

				i->next = z;
				return;
			}
		}
	}
	return;
}



static int extension_matches(pval *here, const char *exten, const char *pattern)
{
	int err1;
	regex_t preg;

	/* simple case, they match exactly, the pattern and exten name */
	if (strcmp(pattern,exten) == 0)
		return 1;

	if (pattern[0] == '_') {
		char reg1[2000];
		const char *p;
		char *r = reg1;

		if ( strlen(pattern)*5 >= 2000 ) /* safety valve */ {
			ast_log(LOG_ERROR,"Error: The pattern %s is way too big. Pattern matching cancelled.\n",
					pattern);
			return 0;
		}
		/* form a regular expression from the pattern, and then match it against exten */
		*r++ = '^'; /* what if the extension is a pattern ?? */
		*r++ = '_'; /* what if the extension is a pattern ?? */
		*r++ = '?';
		for (p=pattern+1; *p; p++) {
			switch ( *p ) {
			case 'X':
				*r++ = '[';
				*r++ = '0';
				*r++ = '-';
				*r++ = '9';
				*r++ = 'X';
				*r++ = ']';
				break;

			case 'Z':
				*r++ = '[';
				*r++ = '1';
				*r++ = '-';
				*r++ = '9';
				*r++ = 'Z';
				*r++ = ']';
				break;

			case 'N':
				*r++ = '[';
				*r++ = '2';
				*r++ = '-';
				*r++ = '9';
				*r++ = 'N';
				*r++ = ']';
				break;

			case '[':
				while ( *p && *p != ']' ) {
					*r++ = *p++;
				}
				*r++ = ']';
				if ( *p != ']') {
					ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The extension pattern '%s' is missing a closing bracket \n",
							here->filename, here->startline, here->endline, pattern);
				}
				break;

			case '.':
			case '!':
				*r++ = '.';
				*r++ = '*';
				break;
			case '*': /* regex metacharacter */
			case '+': /* regex metacharacter */
				*r++ = '\\';
				/* fall through */
			default:
				*r++ = *p;
				break;

			}
		}
		*r++ = '$'; /* what if the extension is a pattern ?? */
		*r++ = *p++; /* put in the closing null */
		err1 = regcomp(&preg, reg1, REG_NOSUB|REG_EXTENDED);
		if ( err1 ) {
			char errmess[500];
			regerror(err1,&preg,errmess,sizeof(errmess));
			regfree(&preg);
			ast_log(LOG_WARNING, "Regcomp of %s failed, error code %d\n",
					reg1, err1);
			return 0;
		}
		err1 = regexec(&preg, exten, 0, 0, 0);
		regfree(&preg);

		if ( err1 ) {
			/* ast_log(LOG_NOTICE,"*****************************[%d]Extension %s did not match %s(%s)\n",
			   err1,exten, pattern, reg1); */
			return 0; /* no match */
		} else {
			/* ast_log(LOG_NOTICE,"*****************************Extension %s matched %s\n",
			   exten, pattern); */
			return 1;
		}
	}

	return 0;
}


static void check_expr2_input(pval *expr, char *str)
{
	int spaces = strspn(str,"\t \n");
	if ( !strncmp(str+spaces,"$[",2) ) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The expression '%s' is redundantly wrapped in '$[ ]'. \n",
				expr->filename, expr->startline, expr->endline, str);
		warns++;
	}
}

static void check_includes(pval *includes)
{
	struct pval *p4;
	for (p4=includes->u1.list; p4; p4=p4->next) {
		/* for each context pointed to, find it, then find a context/label that matches the
		   target here! */
		char *incl_context = p4->u1.str;
		/* find a matching context name */
		struct pval *that_other_context = find_context(incl_context);
		if (!that_other_context && strcmp(incl_context, "parkedcalls") != 0) {
			ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The included context '%s' cannot be found.\n\
 (You may ignore this warning if '%s' exists in extensions.conf, or is created by another module. I cannot check for those.)\n",
					includes->filename, includes->startline, includes->endline, incl_context, incl_context);
			warns++;
		}
	}
}


static void check_timerange(pval *p)
{
	char *times;
	char *e;
	int s1, s2;
	int e1, e2;

	times = ast_strdupa(p->u1.str);

	/* Star is all times */
	if (ast_strlen_zero(times) || !strcmp(times, "*")) {
		return;
	}
	/* Otherwise expect a range */
	e = strchr(times, '-');
	if (!e) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The time range format (%s) requires a '-' surrounded by two 24-hour times of day!\n",
				p->filename, p->startline, p->endline, times);
		warns++;
		return;
	}
	*e = '\0';
	e++;
	while (*e && !isdigit(*e))
		e++;
	if (!*e) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The time range format (%s) is missing the end time!\n",
				p->filename, p->startline, p->endline, p->u1.str);
		warns++;
	}
	if (sscanf(times, "%2d:%2d", &s1, &s2) != 2) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The start time (%s) isn't quite right!\n",
				p->filename, p->startline, p->endline, times);
		warns++;
	}
	if (sscanf(e, "%2d:%2d", &e1, &e2) != 2) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The end time (%s) isn't quite right!\n",
				p->filename, p->startline, p->endline, times);
		warns++;
	}

	s1 = s1 * 30 + s2/2;
	if ((s1 < 0) || (s1 >= 24*30)) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The start time (%s) is out of range!\n",
				p->filename, p->startline, p->endline, times);
		warns++;
	}
	e1 = e1 * 30 + e2/2;
	if ((e1 < 0) || (e1 >= 24*30)) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The end time (%s) is out of range!\n",
				p->filename, p->startline, p->endline, e);
		warns++;
	}
	return;
}

static char *days[] =
{
	"sun",
	"mon",
	"tue",
	"wed",
	"thu",
	"fri",
	"sat",
};

/*! \brief  get_dow: Get day of week */
static void check_dow(pval *DOW)
{
	char *dow;
	char *c;
	/* The following line is coincidence, really! */
	int s, e;

	dow = ast_strdupa(DOW->u1.str);

	/* Check for all days */
	if (ast_strlen_zero(dow) || !strcmp(dow, "*"))
		return;
	/* Get start and ending days */
	c = strchr(dow, '-');
	if (c) {
		*c = '\0';
		c++;
	} else
		c = NULL;
	/* Find the start */
	s = 0;
	while ((s < 7) && strcasecmp(dow, days[s])) s++;
	if (s >= 7) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The day (%s) must be one of 'sun', 'mon', 'tue', 'wed', 'thu', 'fri', or 'sat'!\n",
				DOW->filename, DOW->startline, DOW->endline, dow);
		warns++;
	}
	if (c) {
		e = 0;
		while ((e < 7) && strcasecmp(c, days[e])) e++;
		if (e >= 7) {
			ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The end day (%s) must be one of 'sun', 'mon', 'tue', 'wed', 'thu', 'fri', or 'sat'!\n",
					DOW->filename, DOW->startline, DOW->endline, c);
			warns++;
		}
	} else
		e = s;
}

static void check_day(pval *DAY)
{
	char *day;
	char *c;
	/* The following line is coincidence, really! */
	int s, e;

	day = ast_strdupa(DAY->u1.str);

	/* Check for all days */
	if (ast_strlen_zero(day) || !strcmp(day, "*")) {
		return;
	}
	/* Get start and ending days */
	c = strchr(day, '-');
	if (c) {
		*c = '\0';
		c++;
	}
	/* Find the start */
	if (sscanf(day, "%2d", &s) != 1) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The start day of month (%s) must be a number!\n",
				DAY->filename, DAY->startline, DAY->endline, day);
		warns++;
	}
	else if ((s < 1) || (s > 31)) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The start day of month (%s) must be a number in the range [1-31]!\n",
				DAY->filename, DAY->startline, DAY->endline, day);
		warns++;
	}
	s--;
	if (c) {
		if (sscanf(c, "%2d", &e) != 1) {
			ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The end day of month (%s) must be a number!\n",
					DAY->filename, DAY->startline, DAY->endline, c);
			warns++;
		}
		else if ((e < 1) || (e > 31)) {
			ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The end day of month (%s) must be a number in the range [1-31]!\n",
					DAY->filename, DAY->startline, DAY->endline, day);
			warns++;
		}
		e--;
	} else
		e = s;
}

static char *months[] =
{
	"jan",
	"feb",
	"mar",
	"apr",
	"may",
	"jun",
	"jul",
	"aug",
	"sep",
	"oct",
	"nov",
	"dec",
};

static void check_month(pval *MON)
{
	char *mon;
	char *c;
	/* The following line is coincidence, really! */
	int s, e;

	mon = ast_strdupa(MON->u1.str);

	/* Check for all days */
	if (ast_strlen_zero(mon) || !strcmp(mon, "*"))
		return ;
	/* Get start and ending days */
	c = strchr(mon, '-');
	if (c) {
		*c = '\0';
		c++;
	}
	/* Find the start */
	s = 0;
	while ((s < 12) && strcasecmp(mon, months[s])) s++;
	if (s >= 12) {
		ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The start month (%s) must be a one of: 'jan', 'feb', ..., 'dec'!\n",
				MON->filename, MON->startline, MON->endline, mon);
		warns++;
	}
	if (c) {
		e = 0;
		while ((e < 12) && strcasecmp(mon, months[e])) e++;
		if (e >= 12) {
			ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The end month (%s) must be a one of: 'jan', 'feb', ..., 'dec'!\n",
					MON->filename, MON->startline, MON->endline, c);
			warns++;
		}
	} else
		e = s;
}

static int check_break(pval *item)
{
	pval *p = item;

	while( p && p->type != PV_MACRO && p->type != PV_CONTEXT ) /* early cutout, sort of */ {
		/* a break is allowed in WHILE, FOR, CASE, DEFAULT, PATTERN; otherwise, it don't make
		   no sense */
		if( p->type == PV_CASE || p->type == PV_DEFAULT || p->type == PV_PATTERN
			|| p->type == PV_WHILE || p->type == PV_FOR   ) {
			return 1;
		}
		p = p->dad;
	}
	ast_log(LOG_ERROR,"Error: file %s, line %d-%d: 'break' not in switch, for, or while statement!\n",
			item->filename, item->startline, item->endline);
	errs++;

	return 0;
}

static int check_continue(pval *item)
{
	pval *p = item;

	while( p && p->type != PV_MACRO && p->type != PV_CONTEXT ) /* early cutout, sort of */ {
		/* a break is allowed in WHILE, FOR, CASE, DEFAULT, PATTERN; otherwise, it don't make
		   no sense */
		if( p->type == PV_WHILE || p->type == PV_FOR   ) {
			return 1;
		}
		p = p->dad;
	}
	ast_log(LOG_ERROR,"Error: file %s, line %d-%d: 'continue' not in 'for' or 'while' statement!\n",
			item->filename, item->startline, item->endline);
	errs++;

	return 0;
}

static struct pval *in_macro(pval *item)
{
	struct pval *curr;
	curr = item;
	while( curr ) {
		if( curr->type == PV_MACRO  ) {
			return curr;
		}
		curr = curr->dad;
	}
	return 0;
}

static struct pval *in_context(pval *item)
{
	struct pval *curr;
	curr = item;
	while( curr ) {
		if( curr->type == PV_MACRO || curr->type == PV_CONTEXT ) {
			return curr;
		}
		curr = curr->dad;
	}
	return 0;
}


/* general purpose goto finder */

static void check_label(pval *item)
{
	struct pval *curr;
	struct pval *x;
	int alright = 0;

	/* A label outside an extension just plain does not make sense! */

	curr = item;

	while( curr ) {
		if( curr->type == PV_MACRO || curr->type == PV_EXTENSION   ) {
			alright = 1;
			break;
		}
		curr = curr->dad;
	}
	if( !alright )
	{
		ast_log(LOG_ERROR,"Error: file %s, line %d-%d: Label %s is not within an extension or macro!\n",
				item->filename, item->startline, item->endline, item->u1.str);
		errs++;
	}


	/* basically, ensure that a label is not repeated in a context. Period.
	   The method:  well, for each label, find the first label in the context
	   with the same name. If it's not the current label, then throw an error. */


	/* printf("==== check_label:   ====\n"); */
	if( !current_extension )
		curr = current_context;
	else
		curr = current_extension;

	x = find_first_label_in_current_context((char *)item->u1.str, curr);
	/* printf("Hey, check_label found with item = %x, and x is %x, and currcont is %x, label name is %s\n", item,x, current_context, (char *)item->u1.str); */
	if( x && x != item )
	{
		ast_log(LOG_ERROR,"Error: file %s, line %d-%d: Duplicate label %s! Previously defined at file %s, line %d.\n",
				item->filename, item->startline, item->endline, item->u1.str, x->filename, x->startline);
		errs++;
	}
	/* printf("<<<<< check_label:   ====\n"); */
}

static pval *get_goto_target(pval *item)
{
	/* just one item-- the label should be in the current extension */
	pval *curr_ext = get_extension_or_contxt(item); /* containing exten, or macro */
	pval *curr_cont;

	if (!item->u1.list) {
		return NULL;
	}

	if (!item->u1.list->next && !strstr((item->u1.list)->u1.str,"${")) {
		struct pval *x = find_label_in_current_extension((char*)((item->u1.list)->u1.str), curr_ext);
			return x;
	}

	curr_cont = get_contxt(item);

	/* TWO items */
	if (item->u1.list->next && !item->u1.list->next->next) {
		if (!strstr((item->u1.list)->u1.str,"${")
			&& !strstr(item->u1.list->next->u1.str,"${") ) /* Don't try to match variables */ {
			struct pval *x = find_label_in_current_context((char *)item->u1.list->u1.str, (char *)item->u1.list->next->u1.str, curr_cont);
				return x;
		}
	}

	/* All 3 items! */
	if (item->u1.list->next && item->u1.list->next->next) {
		/* all three */
		pval *first = item->u1.list;
		pval *second = item->u1.list->next;
		pval *third = item->u1.list->next->next;

		if (!strstr((item->u1.list)->u1.str,"${")
			&& !strstr(item->u1.list->next->u1.str,"${")
			&& !strstr(item->u1.list->next->next->u1.str,"${")) /* Don't try to match variables */ {
			struct pval *x = find_label_in_current_db((char*)first->u1.str, (char*)second->u1.str, (char*)third->u1.str);
			if (!x) {

				struct pval *p3;
				struct pval *that_context = find_context(item->u1.list->u1.str);

				/* the target of the goto could be in an included context!! Fancy that!! */
				/* look for includes in the current context */
				if (that_context) {
					for (p3=that_context->u2.statements; p3; p3=p3->next) {
						if (p3->type == PV_INCLUDES) {
							struct pval *p4;
							for (p4=p3->u1.list; p4; p4=p4->next) {
								/* for each context pointed to, find it, then find a context/label that matches the
								   target here! */
								char *incl_context = p4->u1.str;
								/* find a matching context name */
								struct pval *that_other_context = find_context(incl_context);
								if (that_other_context) {
									struct pval *x3;
									x3 = find_label_in_current_context((char *)item->u1.list->next->u1.str, (char *)item->u1.list->next->next->u1.str, that_other_context);
									if (x3) {
										return x3;
									}
								}
							}
						}
					}
				}
			}
			return x;
		}
	}
	return NULL;
}

static void check_goto(pval *item)
{
	if (!item->u1.list) {
		return;
	}

	/* check for the target of the goto-- does it exist? */
	if ( !(item->u1.list)->next && !(item->u1.list)->u1.str ) {
		ast_log(LOG_ERROR,"Error: file %s, line %d-%d: goto:  empty label reference found!\n",
				item->filename, item->startline, item->endline);
		errs++;
	}

	/* just one item-- the label should be in the current extension */
	if (!item->u1.list->next && !strstr(item->u1.list->u1.str,"${")) {
		struct pval *z = get_extension_or_contxt(item);
		struct pval *x = 0;
		if (z)
			x = find_label_in_current_extension((char*)((item->u1.list)->u1.str), z); /* if in macro, use current context instead */
		/* printf("Called find_label_in_current_extension with arg %s; current_extension is %x: %d\n",
		   (char*)((item->u1.list)->u1.str), current_extension?current_extension:current_context, current_extension?current_extension->type:current_context->type); */
		if (!x) {
			ast_log(LOG_ERROR,"Error: file %s, line %d-%d: goto:  no label %s exists in the current extension!\n",
					item->filename, item->startline, item->endline, item->u1.list->u1.str);
			errs++;
		}
		else
			return;
	}

	/* TWO items */
	if (item->u1.list->next && !item->u1.list->next->next) {
		/* two items */
		/* printf("Calling find_label_in_current_context with args %s, %s\n",
		   (char*)((item->u1.list)->u1.str), (char *)item->u1.list->next->u1.str); */
		if (!strstr((item->u1.list)->u1.str,"${")
			&& !strstr(item->u1.list->next->u1.str,"${") ) /* Don't try to match variables */ {
			struct pval *z = get_contxt(item);
			struct pval *x = 0;

			if (z)
				x = find_label_in_current_context((char *)item->u1.list->u1.str, (char *)item->u1.list->next->u1.str, z);

			if (!x) {
				ast_log(LOG_ERROR,"Error: file %s, line %d-%d: goto:  no label '%s,%s' exists in the current context, or any of its inclusions!\n",
						item->filename, item->startline, item->endline, item->u1.list->u1.str, item->u1.list->next->u1.str );
				errs++;
			}
			else
				return;
		}
	}

	/* All 3 items! */
	if (item->u1.list->next && item->u1.list->next->next) {
		/* all three */
		pval *first = item->u1.list;
		pval *second = item->u1.list->next;
		pval *third = item->u1.list->next->next;

		/* printf("Calling find_label_in_current_db with args %s, %s, %s\n",
		   (char*)first->u1.str, (char*)second->u1.str, (char*)third->u1.str); */
		if (!strstr((item->u1.list)->u1.str,"${")
			&& !strstr(item->u1.list->next->u1.str,"${")
			&& !strstr(item->u1.list->next->next->u1.str,"${")) /* Don't try to match variables */ {
			struct pval *x = find_label_in_current_db((char*)first->u1.str, (char*)second->u1.str, (char*)third->u1.str);
			if (!x) {
				struct pval *p3;
				struct pval *found = 0;
				struct pval *that_context = find_context(item->u1.list->u1.str);

				/* the target of the goto could be in an included context!! Fancy that!! */
				/* look for includes in the current context */
				if (that_context) {
					for (p3=that_context->u2.statements; p3; p3=p3->next) {
						if (p3->type == PV_INCLUDES) {
							struct pval *p4;
							for (p4=p3->u1.list; p4; p4=p4->next) {
								/* for each context pointed to, find it, then find a context/label that matches the
								   target here! */
								char *incl_context = p4->u1.str;
								/* find a matching context name */
								struct pval *that_other_context = find_context(incl_context);
								if (that_other_context) {
									struct pval *x3;
									x3 = find_label_in_current_context((char *)item->u1.list->next->u1.str, (char *)item->u1.list->next->next->u1.str, that_other_context);
									if (x3) {
										found = x3;
										break;
									}
								}
							}
						}
					}
					if (!found) {
						ast_log(LOG_ERROR,"Error: file %s, line %d-%d: goto:  no label %s|%s exists in the context %s or its inclusions!\n",
								item->filename, item->startline, item->endline, item->u1.list->next->u1.str, item->u1.list->next->next->u1.str, item->u1.list->u1.str );
						errs++;
					} else {
						struct pval *mac = in_macro(item); /* is this goto inside a macro? */
						if( mac ) {    /* yes! */
							struct pval *targ = in_context(found);
							if( mac != targ )
							{
								ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: It's bad form to have a goto in a macro to a target outside the macro!\n",
										item->filename, item->startline, item->endline);
								warns++;
							}
						}
					}
				} else {
					/* here is where code would go to check for target existence in extensions.conf files */
#ifdef STANDALONE
					struct pbx_find_info pfiq = {.stacklen = 0 };
					extern int localized_pbx_load_module(void);
					/* if this is a standalone, we will need to make sure the
					   localized load of extensions.conf is done */
					if (!extensions_dot_conf_loaded) {
						localized_pbx_load_module();
						extensions_dot_conf_loaded++;
					}

					pbx_find_extension(NULL, NULL, &pfiq, first->u1.str, second->u1.str, atoi(third->u1.str),
											atoi(third->u1.str) ? NULL : third->u1.str, NULL,
											atoi(third->u1.str) ? E_MATCH : E_FINDLABEL);

					if (pfiq.status != STATUS_SUCCESS) {
						ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: goto:  Couldn't find goto target %s|%s|%s, not even in extensions.conf!\n",
								item->filename, item->startline, item->endline, first->u1.str, second->u1.str, third->u1.str);
						warns++;
					}
#else
					ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: goto:  Couldn't find goto target %s|%s|%s in the AEL code!\n",
							item->filename, item->startline, item->endline, first->u1.str, second->u1.str, third->u1.str);
					warns++;
#endif
				}
			} else {
				struct pval *mac = in_macro(item); /* is this goto inside a macro? */
				if( mac ) {    /* yes! */
					struct pval *targ = in_context(x);
					if( mac != targ )
					{
						ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: It's bad form to have a goto in a macro to a target outside the macro!\n",
								item->filename, item->startline, item->endline);
						warns++;
					}
				}
			}
		}
	}
}


static void find_pval_goto_item(pval *item, int lev)
{
	struct pval *p4;

	if (lev>100) {
		ast_log(LOG_ERROR,"find_pval_goto in infinite loop! item_type: %u\n\n", item->type);
		return;
	}

	switch ( item->type ) {
	case PV_MACRO:
		/* fields: item->u1.str     == name of macro
		           item->u2.arglist == pval list of PV_WORD arguments of macro, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg

				   item->u3.macro_statements == pval list of statements in macro body.
		*/

		/* printf("Descending into macro %s at line %d\n", item->u1.str, item->startline); */
		find_pval_gotos(item->u3.macro_statements,lev+1); /* if we're just searching for a context, don't bother descending into them */

		break;

	case PV_CONTEXT:
		/* fields: item->u1.str     == name of context
		           item->u2.statements == pval list of statements in context body
				   item->u3.abstract == int 1 if an abstract keyword were present
		*/
		break;

	case PV_CASE:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		/* printf("Descending into Case of %s\n", item->u1.str); */
		find_pval_gotos(item->u2.statements,lev+1);
		break;

	case PV_PATTERN:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		/* printf("Descending into Pattern of %s\n", item->u1.str); */
		find_pval_gotos(item->u2.statements,lev+1);
		break;

	case PV_DEFAULT:
		/* fields:
		           item->u2.statements == pval list of statements under the case
		*/
		/* printf("Descending into default\n"); */
		find_pval_gotos(item->u2.statements,lev+1);
		break;

	case PV_CATCH:
		/* fields: item->u1.str     == name of extension to catch
		           item->u2.statements == pval list of statements in context body
		*/
		/* printf("Descending into catch of %s\n", item->u1.str); */
		find_pval_gotos(item->u2.statements,lev+1);
		break;

	case PV_STATEMENTBLOCK:
		/* fields: item->u1.list     == pval list of statements in block, one per entry in the list
		*/
		/* printf("Descending into statement block\n"); */
		find_pval_gotos(item->u1.list,lev+1);
		break;

	case PV_GOTO:
		/* fields: item->u1.list     == pval list of PV_WORD target names, up to 3, in order as given by user.
		           item->u1.list->u1.str  == where the data on a PV_WORD will always be.
		*/
		check_goto(item);  /* THE WHOLE FUNCTION OF THIS ENTIRE ROUTINE!!!! */
		break;

	case PV_INCLUDES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		for (p4=item->u1.list; p4; p4=p4->next) {
			/* for each context pointed to, find it, then find a context/label that matches the
			   target here! */
			char *incl_context = p4->u1.str;
			/* find a matching context name */
			struct pval *that_context = find_context(incl_context);
			if (that_context && that_context->u2.statements) {
				/* printf("Descending into include of '%s' at line %d; that_context=%s, that_context type=%d\n", incl_context, item->startline, that_context->u1.str, that_context->type); */
				find_pval_gotos(that_context->u2.statements,lev+1); /* keep working up the includes */
			}
		}
		break;

	case PV_FOR:
		/* fields: item->u1.for_init     == a string containing the initializer
		           item->u2.for_test     == a string containing the loop test
		           item->u3.for_inc      == a string containing the loop increment

				   item->u4.for_statements == a pval list of statements in the for ()
		*/
		/* printf("Descending into for at line %d\n", item->startline); */
		find_pval_gotos(item->u4.for_statements,lev+1);
		break;

	case PV_WHILE:
		/* fields: item->u1.str        == the while conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the while ()
		*/
		/* printf("Descending into while at line %d\n", item->startline); */
		find_pval_gotos(item->u2.statements,lev+1);
		break;

	case PV_RANDOM:
		/* fields: item->u1.str        == the random number expression, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		 fall thru to PV_IF */

	case PV_IFTIME:
		/* fields: item->u1.list        == the time values, 4 of them, as PV_WORD structs in a list

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		fall thru to PV_IF*/
	case PV_IF:
		/* fields: item->u1.str        == the if conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		/* printf("Descending into random/iftime/if at line %d\n", item->startline); */
		find_pval_gotos(item->u2.statements,lev+1);

		if (item->u3.else_statements) {
			/* printf("Descending into random/iftime/if's ELSE at line %d\n", item->startline); */
			find_pval_gotos(item->u3.else_statements,lev+1);
		}
		break;

	case PV_SWITCH:
		/* fields: item->u1.str        == the switch expression

				   item->u2.statements == a pval list of statements in the switch,
				   							(will be case statements, most likely!)
		*/
		/* printf("Descending into switch at line %d\n", item->startline); */
		find_pval_gotos(item->u3.else_statements,lev+1);
		break;

	case PV_EXTENSION:
		/* fields: item->u1.str        == the extension name, label, whatever it's called

				   item->u2.statements == a pval list of statements in the extension
				   item->u3.hints      == a char * hint argument
				   item->u4.regexten   == an int boolean. non-zero says that regexten was specified
		*/

		/* printf("Descending into extension %s at line %d\n", item->u1.str, item->startline); */
		find_pval_gotos(item->u2.statements,lev+1);
		break;

	default:
		break;
	}
}

static void find_pval_gotos(pval *item,int lev)
{
	pval *i;

	for (i=item; i; i=i->next) {
		/* printf("About to call pval_goto_item, itemcount=%d, itemtype=%d\n", item_count, i->type); */
		find_pval_goto_item(i, lev);
	}
}



/* general purpose label finder */
static struct pval *match_pval_item(pval *item)
{
	pval *x;

	switch ( item->type ) {
	case PV_MACRO:
		/* fields: item->u1.str     == name of macro
		           item->u2.arglist == pval list of PV_WORD arguments of macro, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg

				   item->u3.macro_statements == pval list of statements in macro body.
		*/
		/* printf("    matching in MACRO %s, match_context=%s; retoncontmtch=%d; \n", item->u1.str, match_context, return_on_context_match); */
		if (!strcmp(match_context,"*") || !strcmp(item->u1.str, match_context)) {

			/* printf("MACRO: match context is: %s\n", match_context); */

			if (return_on_context_match && !strcmp(item->u1.str, match_context)) /* if we're just searching for a context, don't bother descending into them */ {
				/* printf("Returning on matching macro %s\n", match_context); */
				return item;
			}


			if (!return_on_context_match) {
				/* printf("Descending into matching macro %s/%s\n", match_context, item->u1.str); */
				if ((x=match_pval(item->u3.macro_statements)))  {
					/* printf("Responded with pval match %x\n", x); */
					return x;
				}
			}
		} else {
			/* printf("Skipping context/macro %s\n", item->u1.str); */
		}

		break;

	case PV_CONTEXT:
		/* fields: item->u1.str     == name of context
		           item->u2.statements == pval list of statements in context body
				   item->u3.abstract == int 1 if an abstract keyword were present
		*/
		/* printf("    matching in CONTEXT\n"); */
		if (!strcmp(match_context,"*") || !strcmp(item->u1.str, match_context)) {
			if (return_on_context_match && !strcmp(item->u1.str, match_context)) {
				/* printf("Returning on matching context %s\n", match_context); */
				/* printf("non-CONTEXT: Responded with pval match %x\n", x); */
				return item;
			}

			if (!return_on_context_match ) {
				/* printf("Descending into matching context %s\n", match_context); */
				if ((x=match_pval(item->u2.statements))) /* if we're just searching for a context, don't bother descending into them */ {
					/* printf("CONTEXT: Responded with pval match %x\n", x); */
					return x;
				}
			}
		} else {
			/* printf("Skipping context/macro %s\n", item->u1.str); */
		}
		break;

	case PV_CASE:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		/* printf("    matching in CASE\n"); */
		if ((x=match_pval(item->u2.statements))) {
			/* printf("CASE: Responded with pval match %x\n", x); */
			return x;
		}
		break;

	case PV_PATTERN:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		/* printf("    matching in PATTERN\n"); */
		if ((x=match_pval(item->u2.statements))) {
			/* printf("PATTERN: Responded with pval match %x\n", x); */
			return x;
		}
		break;

	case PV_DEFAULT:
		/* fields:
		           item->u2.statements == pval list of statements under the case
		*/
		/* printf("    matching in DEFAULT\n"); */
		if ((x=match_pval(item->u2.statements))) {
			/* printf("DEFAULT: Responded with pval match %x\n", x); */
			return x;
		}
		break;

	case PV_CATCH:
		/* fields: item->u1.str     == name of extension to catch
		           item->u2.statements == pval list of statements in context body
		*/
		/* printf("    matching in CATCH\n"); */
		if ((x=match_pval(item->u2.statements))) {
			/* printf("CATCH: Responded with pval match %x\n", x); */
			return x;
		}
		break;

	case PV_STATEMENTBLOCK:
		/* fields: item->u1.list     == pval list of statements in block, one per entry in the list
		*/
		/* printf("    matching in STATEMENTBLOCK\n"); */
		if ((x=match_pval(item->u1.list))) {
			/* printf("STATEMENTBLOCK: Responded with pval match %x\n", x); */
			return x;
		}
		break;

	case PV_LABEL:
		/* fields: item->u1.str     == label name
		*/
		/* printf("PV_LABEL %s (cont=%s, exten=%s\n",
		   item->u1.str, current_context->u1.str, (current_extension?current_extension->u1.str:"<macro>"));*/

		if (count_labels) {
			if (!strcmp(match_label, item->u1.str)) {
				label_count++;
				last_matched_label = item;
			}

		} else {
			if (!strcmp(match_label, item->u1.str)) {
				/* printf("LABEL: Responded with pval match %x\n", x); */
				return item;
			}
		}
		break;

	case PV_FOR:
		/* fields: item->u1.for_init     == a string containing the initializer
		           item->u2.for_test     == a string containing the loop test
		           item->u3.for_inc      == a string containing the loop increment

				   item->u4.for_statements == a pval list of statements in the for ()
		*/
		/* printf("    matching in FOR\n"); */
		if ((x=match_pval(item->u4.for_statements))) {
			/* printf("FOR: Responded with pval match %x\n", x);*/
			return x;
		}
		break;

	case PV_WHILE:
		/* fields: item->u1.str        == the while conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the while ()
		*/
		/* printf("    matching in WHILE\n"); */
		if ((x=match_pval(item->u2.statements))) {
			/* printf("WHILE: Responded with pval match %x\n", x); */
			return x;
		}
		break;

	case PV_RANDOM:
		/* fields: item->u1.str        == the random number expression, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		 fall thru to PV_IF */

	case PV_IFTIME:
		/* fields: item->u1.list        == the time values, 4 of them, as PV_WORD structs in a list

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		fall thru to PV_IF*/
	case PV_IF:
		/* fields: item->u1.str        == the if conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		/* printf("    matching in IF/IFTIME/RANDOM\n"); */
		if ((x=match_pval(item->u2.statements))) {
			return x;
		}
		if (item->u3.else_statements) {
			if ((x=match_pval(item->u3.else_statements))) {
				/* printf("IF/IFTIME/RANDOM: Responded with pval match %x\n", x); */
				return x;
			}
		}
		break;

	case PV_SWITCH:
		/* fields: item->u1.str        == the switch expression

				   item->u2.statements == a pval list of statements in the switch,
				   							(will be case statements, most likely!)
		*/
		/* printf("    matching in SWITCH\n"); */
		if ((x=match_pval(item->u2.statements))) {
			/* printf("SWITCH: Responded with pval match %x\n", x); */
			return x;
		}
		break;

	case PV_EXTENSION:
		/* fields: item->u1.str        == the extension name, label, whatever it's called

				   item->u2.statements == a pval list of statements in the extension
				   item->u3.hints      == a char * hint argument
				   item->u4.regexten   == an int boolean. non-zero says that regexten was specified
		*/
		/* printf("    matching in EXTENSION\n"); */
		if (!strcmp(match_exten,"*") || extension_matches(item, match_exten, item->u1.str) ) {
			/* printf("Descending into matching exten %s => %s\n", match_exten, item->u1.str); */
			if (strcmp(match_label,"1") == 0) {
				if (item->u2.statements) {
					struct pval *p5 = item->u2.statements;
					while (p5 && p5->type == PV_LABEL)  /* find the first non-label statement in this context. If it exists, there's a "1" */
						p5 = p5->next;
					if (p5)
						return p5;
					else
						return 0;
				}
				else
					return 0;
			}

			if ((x=match_pval(item->u2.statements))) {
				/* printf("EXTENSION: Responded with pval match %x\n", x); */
				return x;
			}
		} else {
			/* printf("Skipping exten %s\n", item->u1.str); */
		}
		break;
	default:
		/* printf("    matching in default = %d\n", item->type); */
		break;
	}
	return 0;
}

struct pval *match_pval(pval *item)
{
	pval *i;

	for (i=item; i; i=i->next) {
		pval *x;
		/* printf("   -- match pval: item %d\n", i->type); */

		if ((x = match_pval_item(i))) {
			/* printf("match_pval: returning x=%x\n", (int)x); */
			return x; /* cut the search short */
		}
	}
	return 0;
}

#if 0
int count_labels_in_current_context(char *label)
{
	label_count = 0;
	count_labels = 1;
	return_on_context_match = 0;
	match_pval(current_context->u2.statements);

	return label_count;
}
#endif

struct pval *find_first_label_in_current_context(char *label, pval *curr_cont)
{
	/* printf("  --- Got args %s, %s\n", exten, label); */
	struct pval *ret;
	struct pval *p3;

	count_labels = 0;
	return_on_context_match = 0;
	match_context = "*";
	match_exten = "*";
	match_label = label;

	ret =  match_pval(curr_cont);
	if (ret)
		return ret;

	/* the target of the goto could be in an included context!! Fancy that!! */
	/* look for includes in the current context */
	for (p3=curr_cont->u2.statements; p3; p3=p3->next) {
		if (p3->type == PV_INCLUDES) {
			struct pval *p4;
			for (p4=p3->u1.list; p4; p4=p4->next) {
				/* for each context pointed to, find it, then find a context/label that matches the
				   target here! */
				char *incl_context = p4->u1.str;
				/* find a matching context name */
				struct pval *that_context = find_context(incl_context);
				if (that_context) {
					struct pval *x3;
					x3 = find_first_label_in_current_context(label, that_context);
					if (x3) {
						return x3;
					}
				}
			}
		}
	}
	return 0;
}

struct pval *find_label_in_current_context(char *exten, char *label, pval *curr_cont)
{
	/* printf("  --- Got args %s, %s\n", exten, label); */
	struct pval *ret;
	struct pval *p3;

	count_labels = 0;
	return_on_context_match = 0;
	match_context = "*";
	match_exten = exten;
	match_label = label;
	ret =  match_pval(curr_cont->u2.statements);
	if (ret)
		return ret;

	/* the target of the goto could be in an included context!! Fancy that!! */
	/* look for includes in the current context */
	for (p3=curr_cont->u2.statements; p3; p3=p3->next) {
		if (p3->type == PV_INCLUDES) {
			struct pval *p4;
			for (p4=p3->u1.list; p4; p4=p4->next) {
				/* for each context pointed to, find it, then find a context/label that matches the
				   target here! */
				char *incl_context = p4->u1.str;
				/* find a matching context name */
				struct pval *that_context = find_context(incl_context);
				if (that_context) {
					struct pval *x3;
					x3 = find_label_in_current_context(exten, label, that_context);
					if (x3) {
						return x3;
					}
				}
			}
		}
	}
	return 0;
}

static struct pval *find_label_in_current_extension(const char *label, pval *curr_ext)
{
	/* printf("  --- Got args %s\n", label); */
	count_labels = 0;
	return_on_context_match = 0;
	match_context = "*";
	match_exten = "*";
	match_label = label;
	return match_pval(curr_ext);
}

static struct pval *find_label_in_current_db(const char *context, const char *exten, const char *label)
{
	/* printf("  --- Got args %s, %s, %s\n", context, exten, label); */
	count_labels = 0;
	return_on_context_match = 0;

	match_context = context;
	match_exten = exten;
	match_label = label;

	return match_pval(current_db);
}


struct pval *find_macro(char *name)
{
	return_on_context_match = 1;
	count_labels = 0;
	match_context = name;
	match_exten = "*";  /* don't really need to set these, shouldn't be reached */
	match_label = "*";
	return match_pval(current_db);
}

struct pval *find_context(char *name)
{
	return_on_context_match = 1;
	count_labels = 0;
	match_context = name;
	match_exten = "*";  /* don't really need to set these, shouldn't be reached */
	match_label = "*";
	return match_pval(current_db);
}

int is_float(char *arg )
{
	char *s;
	for (s=arg; *s; s++) {
		if (*s != '.' && (*s < '0' || *s > '9'))
			return 0;
	}
	return 1;
}
int is_int(char *arg )
{
	char *s;
	for (s=arg; *s; s++) {
		if (*s < '0' || *s > '9')
			return 0;
	}
	return 1;
}
int is_empty(char *arg)
{
	if (!arg)
		return 1;
	if (*arg == 0)
		return 1;
	while (*arg) {
		if (*arg != ' ' && *arg != '\t')
			return 0;
		arg++;
	}
	return 1;
}

#ifdef AAL_ARGCHECK
int option_matches_j( struct argdesc *should, pval *is, struct argapp *app)
{
	struct argchoice *ac;
	char *opcop,*q,*p;

	switch (should->dtype) {
	case ARGD_OPTIONSET:
		if ( strstr(is->u1.str,"${") )
			return 0;  /* no checking anything if there's a var reference in there! */

		opcop = ast_strdupa(is->u1.str);

		for (q=opcop;*q;q++) { /* erase the innards of X(innard) type arguments, so we don't get confused later */
			if ( *q == '(' ) {
				p = q+1;
				while (*p && *p != ')' )
					*p++ = '+';
				q = p+1;
			}
		}

		for (ac=app->opts; ac; ac=ac->next) {
			if (strlen(ac->name)>1  && strchr(ac->name,'(') == 0 && strcmp(ac->name,is->u1.str) == 0) /* multichar option, no parens, and a match? */
				return 0;
		}
		for (ac=app->opts; ac; ac=ac->next) {
			if (strlen(ac->name)==1  ||  strchr(ac->name,'(')) {
				char *p = strchr(opcop,ac->name[0]);  /* wipe out all matched options in the user-supplied string */

				if (p && *p == 'j') {
					ast_log(LOG_ERROR, "Error: file %s, line %d-%d: The j option in the %s application call is not appropriate for AEL!\n",
							is->filename, is->startline, is->endline, app->name);
					errs++;
				}

				if (p) {
					*p = '+';
					if (ac->name[1] == '(') {
						if (*(p+1) != '(') {
							ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The %c option in the %s application call should have an (argument), but doesn't!\n",
									is->filename, is->startline, is->endline, ac->name[0], app->name);
							warns++;
						}
					}
				}
			}
		}
		for (q=opcop; *q; q++) {
			if ( *q != '+' && *q != '(' && *q != ')') {
				ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: The %c option in the %s application call is not available as an option!\n",
						is->filename, is->startline, is->endline, *q, app->name);
				warns++;
			}
		}
		return 1;
		break;
	default:
		return 0;
	}

}

int option_matches( struct argdesc *should, pval *is, struct argapp *app)
{
	struct argchoice *ac;
	char *opcop;

	switch (should->dtype) {
	case ARGD_STRING:
		if (is_empty(is->u1.str) && should->type == ARGD_REQUIRED)
			return 0;
		if (is->u1.str && strlen(is->u1.str) > 0) /* most will match */
			return 1;
		break;

	case ARGD_INT:
		if (is_int(is->u1.str))
			return 1;
		else
			return 0;
		break;

	case ARGD_FLOAT:
		if (is_float(is->u1.str))
			return 1;
		else
			return 0;
		break;

	case ARGD_ENUM:
		if( !is->u1.str || strlen(is->u1.str) == 0 )
			return 1; /* a null arg in the call will match an enum, I guess! */
		for (ac=should->choices; ac; ac=ac->next) {
			if (strcmp(ac->name,is->u1.str) == 0)
				return 1;
		}
		return 0;
		break;

	case ARGD_OPTIONSET:
		opcop = ast_strdupa(is->u1.str);

		for (ac=app->opts; ac; ac=ac->next) {
			if (strlen(ac->name)>1  && strchr(ac->name,'(') == 0 && strcmp(ac->name,is->u1.str) == 0) /* multichar option, no parens, and a match? */
				return 1;
		}
		for (ac=app->opts; ac; ac=ac->next) {
			if (strlen(ac->name)==1  ||  strchr(ac->name,'(')) {
				char *p = strchr(opcop,ac->name[0]);  /* wipe out all matched options in the user-supplied string */

				if (p) {
					*p = '+';
					if (ac->name[1] == '(') {
						if (*(p+1) == '(') {
							char *q = p+1;
							while (*q && *q != ')') {
								*q++ = '+';
							}
							*q = '+';
						}
					}
				}
			}
		}
		return 1;
		break;
	case ARGD_VARARG:
		return 1; /* matches anything */
		break;
	}
	return 1; /* unless some for-sure match or non-match returns, then it must be close enough ... */
}
#endif

int check_app_args(pval* appcall, pval *arglist, struct argapp *app)
{
#ifdef AAL_ARGCHECK
	struct argdesc *ad = app->args;
	pval *pa;
	int z;

	for (pa = arglist; pa; pa=pa->next) {
		if (!ad) {
			ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: Extra argument %s not in application call to %s !\n",
					arglist->filename, arglist->startline, arglist->endline, pa->u1.str, app->name);
			warns++;
			return 1;
		} else {
			/* find the first entry in the ad list that will match */
			do {
				if ( ad->dtype == ARGD_VARARG ) /* once we hit the VARARG, all bets are off. Discontinue the comparisons */
					break;

				z= option_matches( ad, pa, app);
				if (!z) {
					if ( !arglist )
						arglist=appcall;

					if (ad->type == ARGD_REQUIRED) {
						ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: Required argument %s not in application call to %s !\n",
								arglist->filename, arglist->startline, arglist->endline, ad->dtype==ARGD_OPTIONSET?"options":ad->name, app->name);
						warns++;
						return 1;
					}
				} else if (z && ad->dtype == ARGD_OPTIONSET) {
					option_matches_j( ad, pa, app);
				}
				ad = ad->next;
			} while (ad && !z);
		}
	}
	/* any app nodes left, that are not optional? */
	for ( ; ad; ad=ad->next) {
		if (ad->type == ARGD_REQUIRED && ad->dtype != ARGD_VARARG) {
			if ( !arglist )
				arglist=appcall;
			ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: Required argument %s not in application call to %s !\n",
					arglist->filename, arglist->startline, arglist->endline, ad->dtype==ARGD_OPTIONSET?"options":ad->name, app->name);
			warns++;
			return 1;
		}
	}
	return 0;
#else
	return 0;
#endif
}

void check_switch_expr(pval *item, struct argapp *apps)
{
#ifdef AAL_ARGCHECK
	/* get and clean the variable name */
	char *buff1, *p;
	struct argapp *a,*a2;
	struct appsetvar *v,*v2;
	struct argchoice *c;
	pval *t;

	p = item->u1.str;
	while (p && *p && (*p == ' ' || *p == '\t' || *p == '$' || *p == '{' ) )
		p++;

	buff1 = ast_strdupa(p);

	while (strlen(buff1) > 0 && ( buff1[strlen(buff1)-1] == '}' || buff1[strlen(buff1)-1] == ' ' || buff1[strlen(buff1)-1] == '\t'))
		buff1[strlen(buff1)-1] = 0;
	/* buff1 now contains the variable name */
	v = 0;
	for (a=apps; a; a=a->next) {
		for (v=a->setvars;v;v=v->next) {
			if (strcmp(v->name,buff1) == 0) {
				break;
			}
		}
		if ( v )
			break;
	}
	if (v && v->vals) {
		/* we have a match, to a variable that has a set of determined values */
		int def= 0;
		int pat = 0;
		int f1 = 0;

		/* first of all, does this switch have a default case ? */
		for (t=item->u2.statements; t; t=t->next) {
			if (t->type == PV_DEFAULT) {
				def =1;
				break;
			}
			if (t->type == PV_PATTERN) {
				pat++;
			}
		}
		if (def || pat) /* nothing to check. All cases accounted for! */
			return;
		for (c=v->vals; c; c=c->next) {
			f1 = 0;
			for (t=item->u2.statements; t; t=t->next) {
				if (t->type == PV_CASE || t->type == PV_PATTERN) {
					if (!strcmp(t->u1.str,c->name)) {
						f1 = 1;
						break;
					}
				}
			}
			if (!f1) {
				ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: switch with expression(%s) does not handle the case of %s !\n",
						item->filename, item->startline, item->endline, item->u1.str, c->name);
				warns++;
			}
		}
		/* next, is there an app call in the current exten, that would set this var? */
		f1 = 0;
		t = current_extension->u2.statements;
		if ( t && t->type == PV_STATEMENTBLOCK )
			t = t->u1.statements;
		for (; t && t != item; t=t->next) {
			if (t->type == PV_APPLICATION_CALL) {
				/* find the application that matches the u1.str */
				for (a2=apps; a2; a2=a2->next) {
					if (strcasecmp(a2->name, t->u1.str)==0) {
						for (v2=a2->setvars; v2; v2=v2->next) {
							if (strcmp(v2->name, buff1) == 0) {
								/* found an app that sets the var */
								f1 = 1;
								break;
							}
						}
					}
					if (f1)
						break;
				}
			}
			if (f1)
				break;
		}

		/* see if it sets the var */
		if (!f1) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: Couldn't find an application call in this extension that sets the  expression (%s) value!\n",
					item->filename, item->startline, item->endline, item->u1.str);
			warns++;
		}
	}
#else
	pval *t,*tl=0,*p2;
	int def= 0;

	/* first of all, does this switch have a default case ? */
	for (t=item->u2.statements; t; t=t->next) {
		if (t->type == PV_DEFAULT) {
			def =1;
			break;
		}
		tl = t;
	}
	if (def) /* nothing to check. All cases accounted for! */
		return;
	/* if no default, warn and insert a default case at the end */
	p2 = tl->next = calloc(1, sizeof(struct pval));

	p2->type = PV_DEFAULT;
	p2->startline = tl->startline;
	p2->endline = tl->endline;
	p2->startcol = tl->startcol;
	p2->endcol = tl->endcol;
	p2->filename = strdup(tl->filename);
	ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: A default case was automatically added to the switch.\n",
			p2->filename, p2->startline, p2->endline);
	warns++;

#endif
}

static void check_context_names(void)
{
	pval *i,*j;
	for (i=current_db; i; i=i->next) {
		if (i->type == PV_CONTEXT || i->type == PV_MACRO) {
			for (j=i->next; j; j=j->next) {
				if ( j->type == PV_CONTEXT || j->type == PV_MACRO ) {
					if ( !strcmp(i->u1.str, j->u1.str) && !(i->u3.abstract&2) && !(j->u3.abstract&2) )
					{
						ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: The context name (%s) is also declared in file %s, line %d-%d! (and neither is marked 'extend')\n",
								i->filename, i->startline, i->endline, i->u1.str,  j->filename, j->startline, j->endline);
						warns++;
					}
				}
			}
		}
	}
}

static void check_abstract_reference(pval *abstract_context)
{
	pval *i,*j;
	/* find some context includes that reference this context */


	/* otherwise, print out a warning */
	for (i=current_db; i; i=i->next) {
		if (i->type == PV_CONTEXT) {
			for (j=i->u2. statements; j; j=j->next) {
				if ( j->type == PV_INCLUDES ) {
					struct pval *p4;
					for (p4=j->u1.list; p4; p4=p4->next) {
						/* for each context pointed to, find it, then find a context/label that matches the
						   target here! */
						if ( !strcmp(p4->u1.str, abstract_context->u1.str) )
							return; /* found a match! */
					}
				}
			}
		}
	}
	ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: Couldn't find a reference to this abstract context (%s) in any other context!\n",
			abstract_context->filename, abstract_context->startline, abstract_context->endline, abstract_context->u1.str);
	warns++;
}


void check_pval_item(pval *item, struct argapp *apps, int in_globals)
{
	pval *lp;
#ifdef AAL_ARGCHECK
	struct argapp *app, *found;
#endif
	struct pval *macro_def;
	struct pval *app_def;

	char errmsg[4096];
	char *strp;

	switch (item->type) {
	case PV_WORD:
		/* fields: item->u1.str == string associated with this (word).
		           item->u2.arglist  == pval list of 4 PV_WORD elements for time values (only in PV_INCLUDES) */
		break;

	case PV_MACRO:
		/* fields: item->u1.str     == name of macro
		           item->u2.arglist == pval list of PV_WORD arguments of macro, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg

				   item->u3.macro_statements == pval list of statements in macro body.
		*/
		in_abstract_context = 0;
		current_context = item;
		current_extension = 0;

		check_macro_returns(item);

		for (lp=item->u2.arglist; lp; lp=lp->next) {

		}
		check_pval(item->u3.macro_statements, apps,in_globals);
		break;

	case PV_CONTEXT:
		/* fields: item->u1.str     == name of context
		           item->u2.statements == pval list of statements in context body
				   item->u3.abstract == int 1 if an abstract keyword were present
		*/
		current_context = item;
		current_extension = 0;
		if ( item->u3.abstract ) {
			in_abstract_context = 1;
			check_abstract_reference(item);
		} else
			in_abstract_context = 0;
		check_pval(item->u2.statements, apps,in_globals);
		break;

	case PV_MACRO_CALL:
		/* fields: item->u1.str     == name of macro to call
		           item->u2.arglist == pval list of PV_WORD arguments of macro call, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg
		*/
#ifdef STANDALONE
		/* if this is a standalone, we will need to make sure the
		   localized load of extensions.conf is done */
		if (!extensions_dot_conf_loaded) {
			localized_pbx_load_module();
			extensions_dot_conf_loaded++;
		}
#endif
		macro_def = find_macro(item->u1.str);
		if (!macro_def) {
#ifdef STANDALONE
			struct pbx_find_info pfiq = {.stacklen = 0 };
			struct pbx_find_info pfiq2 = {.stacklen = 0 };

			/* look for the macro in the extensions.conf world */
			pbx_find_extension(NULL, NULL, &pfiq, item->u1.str, "s", 1, NULL, NULL, E_MATCH);

			if (pfiq.status != STATUS_SUCCESS) {
				char namebuf2[256];
				snprintf(namebuf2, 256, "macro-%s", item->u1.str);

				/* look for the macro in the extensions.conf world */
				pbx_find_extension(NULL, NULL, &pfiq2, namebuf2, "s", 1, NULL, NULL, E_MATCH);

				if (pfiq2.status == STATUS_SUCCESS) {
					ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: macro call to non-existent %s! (macro-%s was found in the extensions.conf stuff, but we are using gosubs!)\n",
							item->filename, item->startline, item->endline, item->u1.str, item->u1.str);
					warns++;
				} else {
					ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: macro call to non-existent %s! (Not even in the extensions.conf stuff!)\n",
							item->filename, item->startline, item->endline, item->u1.str);
					warns++;
				}
			}
#else
			ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: macro call to %s cannot be found in the AEL code!\n",
					item->filename, item->startline, item->endline, item->u1.str);
			warns++;

#endif
#ifdef THIS_IS_1DOT4
			char namebuf2[256];
			snprintf(namebuf2, 256, "macro-%s", item->u1.str);

			/* look for the macro in the extensions.conf world */
			pbx_find_extension(NULL, NULL, &pfiq, namebuf2, "s", 1, NULL, NULL, E_MATCH);

			if (pfiq.status != STATUS_SUCCESS) {
				ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: macro call to %s was not found in the AEL, nor the extensions.conf !\n",
						item->filename, item->startline, item->endline, item->u1.str);
				warns++;
			}

#endif

		} else if (macro_def->type != PV_MACRO) {
			ast_log(LOG_ERROR,"Error: file %s, line %d-%d: macro call to %s references a context, not a macro!\n",
					item->filename, item->startline, item->endline, item->u1.str);
			errs++;
		} else {
			/* macro_def is a MACRO, so do the args match in number? */
			int hereargs = 0;
			int thereargs = 0;

			for (lp=item->u2.arglist; lp; lp=lp->next) {
				hereargs++;
			}
			for (lp=macro_def->u2.arglist; lp; lp=lp->next) {
				thereargs++;
			}
			if (hereargs != thereargs ) {
				ast_log(LOG_ERROR, "Error: file %s, line %d-%d: The macro call to %s has %d arguments, but the macro definition has %d arguments\n",
						item->filename, item->startline, item->endline, item->u1.str, hereargs, thereargs);
				errs++;
			}
		}
		break;

	case PV_APPLICATION_CALL:
		/* fields: item->u1.str     == name of application to call
		           item->u2.arglist == pval list of PV_WORD arguments of macro call, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg
		*/
		/* Need to check to see if the application is available! */
		app_def = find_context(item->u1.str);
		if (app_def && app_def->type == PV_MACRO) {
			ast_log(LOG_ERROR,"Error: file %s, line %d-%d: application call to %s references an existing macro, but had no & preceding it!\n",
					item->filename, item->startline, item->endline, item->u1.str);
			errs++;
		}
		if (strcasecmp(item->u1.str,"GotoIf") == 0
			|| strcasecmp(item->u1.str,"GotoIfTime") == 0
			|| strcasecmp(item->u1.str,"while") == 0
			|| strcasecmp(item->u1.str,"endwhile") == 0
			|| strcasecmp(item->u1.str,"random") == 0
			|| strcasecmp(item->u1.str,"gosub") == 0
			|| strcasecmp(item->u1.str,"gosubif") == 0
			|| strcasecmp(item->u1.str,"continuewhile") == 0
			|| strcasecmp(item->u1.str,"endwhile") == 0
			|| strcasecmp(item->u1.str,"execif") == 0
			|| strcasecmp(item->u1.str,"execiftime") == 0
			|| strcasecmp(item->u1.str,"exitwhile") == 0
			|| strcasecmp(item->u1.str,"goto") == 0
			|| strcasecmp(item->u1.str,"macro") == 0
			|| strcasecmp(item->u1.str,"macroexclusive") == 0
			|| strcasecmp(item->u1.str,"macroif") == 0
			|| strcasecmp(item->u1.str,"stackpop") == 0
			|| strcasecmp(item->u1.str,"execIf") == 0 ) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: application call to %s affects flow of control, and needs to be re-written using AEL if, while, goto, etc. keywords instead!\n",
					item->filename, item->startline, item->endline, item->u1.str);
			warns++;
		}
		if (strcasecmp(item->u1.str,"macroexit") == 0) {
				ast_log(LOG_WARNING, "Warning: file %s, line %d-%d: I am converting the MacroExit call here to a return statement.\n",
						item->filename, item->startline, item->endline);
				item->type = PV_RETURN;
				free(item->u1.str);
				item->u1.str = 0;
		}

#ifdef AAL_ARGCHECK
		found = 0;
		for (app=apps; app; app=app->next) {
			if (strcasecmp(app->name, item->u1.str) == 0) {
				found =app;
				break;
			}
		}
		if (!found) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: application call to %s not listed in applist database!\n",
					item->filename, item->startline, item->endline, item->u1.str);
			warns++;
		} else
			check_app_args(item, item->u2.arglist, app);
#endif
		break;

	case PV_CASE:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		/* Make sure sequence of statements under case is terminated with  goto, return, or break */
		/* find the last statement */
		check_pval(item->u2.statements, apps,in_globals);
		break;

	case PV_PATTERN:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		/* Make sure sequence of statements under case is terminated with  goto, return, or break */
		/* find the last statement */

		check_pval(item->u2.statements, apps,in_globals);
		break;

	case PV_DEFAULT:
		/* fields:
		           item->u2.statements == pval list of statements under the case
		*/

		check_pval(item->u2.statements, apps,in_globals);
		break;

	case PV_CATCH:
		/* fields: item->u1.str     == name of extension to catch
		           item->u2.statements == pval list of statements in context body
		*/
		check_pval(item->u2.statements, apps,in_globals);
		break;

	case PV_SWITCHES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		check_pval(item->u1.list, apps,in_globals);
		break;

	case PV_ESWITCHES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		check_pval(item->u1.list, apps,in_globals);
		break;

	case PV_INCLUDES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		check_pval(item->u1.list, apps,in_globals);
		check_includes(item);
		for (lp=item->u1.list; lp; lp=lp->next){
			char *incl_context = lp->u1.str;
			struct pval *that_context = find_context(incl_context);

			if ( lp->u2.arglist ) {
				check_timerange(lp->u2.arglist);
				check_dow(lp->u2.arglist->next);
				check_day(lp->u2.arglist->next->next);
				check_month(lp->u2.arglist->next->next->next);
			}

			if (that_context) {
				find_pval_gotos(that_context->u2.statements,0);

			}
		}
		break;

	case PV_STATEMENTBLOCK:
		/* fields: item->u1.list     == pval list of statements in block, one per entry in the list
		*/
		check_pval(item->u1.list, apps,in_globals);
		break;

	case PV_VARDEC:
		/* fields: item->u1.str     == variable name
		           item->u2.val     == variable value to assign
		*/
		/* the RHS of a vardec is encapsulated in a $[] expr. Is it legal? */
		if( !in_globals ) { /* don't check stuff inside the globals context; no wrapping in $[ ] there... */
			snprintf(errmsg,sizeof(errmsg), "file %s, line %d, columns %d-%d, variable declaration expr '%s':", item->filename, item->startline, item->startcol, item->endcol, item->u2.val);
			ast_expr_register_extra_error_info(errmsg);
			ast_expr(item->u2.val, expr_output, sizeof(expr_output),NULL);
			ast_expr_clear_extra_error_info();
			if ( strpbrk(item->u2.val,"~!-+<>=*/&^") && !strstr(item->u2.val,"${") ) {
				ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: expression %s has operators, but no variables. Interesting...\n",
						item->filename, item->startline, item->endline, item->u2.val);
				warns++;
			}
			check_expr2_input(item,item->u2.val);
		}
		break;

	case PV_LOCALVARDEC:
		/* fields: item->u1.str     == variable name
		           item->u2.val     == variable value to assign
		*/
		/* the RHS of a vardec is encapsulated in a $[] expr. Is it legal? */
		snprintf(errmsg,sizeof(errmsg), "file %s, line %d, columns %d-%d, variable declaration expr '%s':", item->filename, item->startline, item->startcol, item->endcol, item->u2.val);
		ast_expr_register_extra_error_info(errmsg);
		ast_expr(item->u2.val, expr_output, sizeof(expr_output),NULL);
		ast_expr_clear_extra_error_info();
		if ( strpbrk(item->u2.val,"~!-+<>=*/&^") && !strstr(item->u2.val,"${") ) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: expression %s has operators, but no variables. Interesting...\n",
					item->filename, item->startline, item->endline, item->u2.val);
			warns++;
		}
		check_expr2_input(item,item->u2.val);
		break;

	case PV_GOTO:
		/* fields: item->u1.list     == pval list of PV_WORD target names, up to 3, in order as given by user.
		           item->u1.list->u1.str  == where the data on a PV_WORD will always be.
		*/
		/* don't check goto's in abstract contexts */
		if ( in_abstract_context )
			break;

		check_goto(item);
		break;

	case PV_LABEL:
		/* fields: item->u1.str     == label name
		*/
		if ( strspn(item->u1.str, "0123456789") == strlen(item->u1.str) ) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: label '%s' is numeric, this is bad practice!\n",
					item->filename, item->startline, item->endline, item->u1.str);
			warns++;
		}

		check_label(item);
		break;

	case PV_FOR:
		/* fields: item->u1.for_init     == a string containing the initializer
		           item->u2.for_test     == a string containing the loop test
		           item->u3.for_inc      == a string containing the loop increment

				   item->u4.for_statements == a pval list of statements in the for ()
		*/
		snprintf(errmsg,sizeof(errmsg),"file %s, line %d, columns %d-%d, for test expr '%s':", item->filename, item->startline, item->startcol, item->endcol, item->u2.for_test);
		ast_expr_register_extra_error_info(errmsg);

		strp = strchr(item->u1.for_init, '=');
		if (strp) {
			ast_expr(strp+1, expr_output, sizeof(expr_output),NULL);
		}
		ast_expr(item->u2.for_test, expr_output, sizeof(expr_output),NULL);
		strp = strchr(item->u3.for_inc, '=');
		if (strp) {
			ast_expr(strp+1, expr_output, sizeof(expr_output),NULL);
		}
		if ( strpbrk(item->u2.for_test,"~!-+<>=*/&^") && !strstr(item->u2.for_test,"${") ) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: expression %s has operators, but no variables. Interesting...\n",
					item->filename, item->startline, item->endline, item->u2.for_test);
			warns++;
		}
		if ( strpbrk(item->u3.for_inc,"~!-+<>=*/&^") && !strstr(item->u3.for_inc,"${") ) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: expression %s has operators, but no variables. Interesting...\n",
					item->filename, item->startline, item->endline, item->u3.for_inc);
			warns++;
		}
		check_expr2_input(item,item->u2.for_test);
		check_expr2_input(item,item->u3.for_inc);

		ast_expr_clear_extra_error_info();
		check_pval(item->u4.for_statements, apps,in_globals);
		break;

	case PV_WHILE:
		/* fields: item->u1.str        == the while conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the while ()
		*/
		snprintf(errmsg,sizeof(errmsg),"file %s, line %d, columns %d-%d, while expr '%s':", item->filename, item->startline, item->startcol, item->endcol, item->u1.str);
		ast_expr_register_extra_error_info(errmsg);
		ast_expr(item->u1.str, expr_output, sizeof(expr_output),NULL);
		ast_expr_clear_extra_error_info();
		if ( strpbrk(item->u1.str,"~!-+<>=*/&^") && !strstr(item->u1.str,"${") ) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: expression %s has operators, but no variables. Interesting...\n",
					item->filename, item->startline, item->endline, item->u1.str);
			warns++;
		}
		check_expr2_input(item,item->u1.str);
		check_pval(item->u2.statements, apps,in_globals);
		break;

	case PV_BREAK:
		/* fields: none
		*/
		check_break(item);
		break;

	case PV_RETURN:
		/* fields: none
		*/
		break;

	case PV_CONTINUE:
		/* fields: none
		*/
		check_continue(item);
		break;

	case PV_RANDOM:
		/* fields: item->u1.str        == the random number expression, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		snprintf(errmsg,sizeof(errmsg),"file %s, line %d, columns %d-%d, random expr '%s':", item->filename, item->startline, item->startcol, item->endcol, item->u1.str);
		ast_expr_register_extra_error_info(errmsg);
		ast_expr(item->u1.str, expr_output, sizeof(expr_output),NULL);
		ast_expr_clear_extra_error_info();
		if ( strpbrk(item->u1.str,"~!-+<>=*/&^") && !strstr(item->u1.str,"${") ) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: random expression '%s' has operators, but no variables. Interesting...\n",
					item->filename, item->startline, item->endline, item->u1.str);
			warns++;
		}
		check_expr2_input(item,item->u1.str);
		check_pval(item->u2.statements, apps,in_globals);
		if (item->u3.else_statements) {
			check_pval(item->u3.else_statements, apps,in_globals);
		}
		break;

	case PV_IFTIME:
		/* fields: item->u1.list        == the if time values, 4 of them, each in PV_WORD, linked list

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		if ( item->u2.arglist ) {
			check_timerange(item->u1.list);
			check_dow(item->u1.list->next);
			check_day(item->u1.list->next->next);
			check_month(item->u1.list->next->next->next);
		}

		check_pval(item->u2.statements, apps,in_globals);
		if (item->u3.else_statements) {
			check_pval(item->u3.else_statements, apps,in_globals);
		}
		break;

	case PV_IF:
		/* fields: item->u1.str        == the if conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		snprintf(errmsg,sizeof(errmsg),"file %s, line %d, columns %d-%d, if expr '%s':", item->filename, item->startline, item->startcol, item->endcol, item->u1.str);
		ast_expr_register_extra_error_info(errmsg);
		ast_expr(item->u1.str, expr_output, sizeof(expr_output),NULL);
		ast_expr_clear_extra_error_info();
		if ( strpbrk(item->u1.str,"~!-+<>=*/&^") && !strstr(item->u1.str,"${") ) {
			ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: expression '%s' has operators, but no variables. Interesting...\n",
					item->filename, item->startline, item->endline, item->u1.str);
			warns++;
		}
		check_expr2_input(item,item->u1.str);
		check_pval(item->u2.statements, apps,in_globals);
		if (item->u3.else_statements) {
			check_pval(item->u3.else_statements, apps,in_globals);
		}
		break;

	case PV_SWITCH:
		/* fields: item->u1.str        == the switch expression

				   item->u2.statements == a pval list of statements in the switch,
				   							(will be case statements, most likely!)
		*/
		/* we can check the switch expression, see if it matches any of the app variables...
           if it does, then, are all the possible cases accounted for? */
		check_switch_expr(item, apps);
		check_pval(item->u2.statements, apps,in_globals);
		break;

	case PV_EXTENSION:
		/* fields: item->u1.str        == the extension name, label, whatever it's called

				   item->u2.statements == a pval list of statements in the extension
				   item->u3.hints      == a char * hint argument
				   item->u4.regexten   == an int boolean. non-zero says that regexten was specified
		*/
		current_extension = item ;

		check_pval(item->u2.statements, apps,in_globals);
		break;

	case PV_IGNOREPAT:
		/* fields: item->u1.str        == the ignorepat data
		*/
		break;

	case PV_GLOBALS:
		/* fields: item->u1.statements     == pval list of statements, usually vardecs
		*/
		in_abstract_context = 0;
		check_pval(item->u1.statements, apps, 1);
		break;
	default:
		break;
	}
}

void check_pval(pval *item, struct argapp *apps, int in_globals)
{
	pval *i;

	/* checks to do:
	   1. Do goto's point to actual labels?
	   2. Do macro calls reference a macro?
	   3. Does the number of macro args match the definition?
	   4. Is a macro call missing its & at the front?
	   5. Application calls-- we could check syntax for existing applications,
	      but I need some sort of universal description bnf for a general
		  sort of method for checking arguments, in number, maybe even type, at least.
		  Don't want to hand code checks for hundreds of applications.
	*/

	for (i=item; i; i=i->next) {
		check_pval_item(i,apps,in_globals);
	}
}

void ael2_semantic_check(pval *item, int *arg_errs, int *arg_warns, int *arg_notes)
{

#ifdef AAL_ARGCHECK
	int argapp_errs =0;
	char *rfilename;
#endif
	struct argapp *apps=0;

	if (!item)
		return; /* don't check an empty tree */
#ifdef AAL_ARGCHECK
	rfilename = ast_alloca(10 + strlen(ast_config_AST_VAR_DIR));
	sprintf(rfilename, "%s/applist", ast_config_AST_VAR_DIR);

	apps = argdesc_parse(rfilename, &argapp_errs); /* giveth */
#endif
	current_db = item;
	errs = warns = notes = 0;

	check_context_names();
	check_pval(item, apps, 0);

#ifdef AAL_ARGCHECK
	argdesc_destroy(apps);  /* taketh away */
#endif
	current_db = 0;

	*arg_errs = errs;
	*arg_warns = warns;
	*arg_notes = notes;
}

/* =============================================================================================== */
/* "CODE" GENERATOR -- Convert the AEL representation to asterisk extension language */
/* =============================================================================================== */

static int control_statement_count;

struct ael_priority *new_prio(void)
{
	struct ael_priority *x = (struct ael_priority *)calloc(sizeof(struct ael_priority),1);
	return x;
}

struct ael_extension *new_exten(void)
{
	struct ael_extension *x = (struct ael_extension *)calloc(sizeof(struct ael_extension),1);
	return x;
}

void linkprio(struct ael_extension *exten, struct ael_priority *prio, struct ael_extension *mother_exten)
{
	char *p1, *p2;

	if (!exten->plist) {
		exten->plist = prio;
		exten->plist_last = prio;
	} else {
		exten->plist_last->next = prio;
		exten->plist_last = prio;
	}
	if( !prio->exten )
		prio->exten = exten; /* don't override the switch value */
	/* The following code will cause all priorities within an extension
	   to have ${EXTEN} or ${EXTEN: replaced with ~~EXTEN~~, which is
	   set just before the first switch in an exten. The switches
	   will muck up the original ${EXTEN} value, so we save it away
	   and the user accesses this copy instead. */
	if (prio->appargs && ((mother_exten && mother_exten->has_switch) || exten->has_switch) ) {
		while ((p1 = strstr(prio->appargs, "${EXTEN}"))) {
			p2 = malloc(strlen(prio->appargs)+5);
			*p1 = 0;
			strcpy(p2, prio->appargs);
			strcat(p2, "${~~EXTEN~~}");
			if (*(p1+8))
				strcat(p2, p1+8);
			free(prio->appargs);
			prio->appargs = p2;
		}
		while ((p1 = strstr(prio->appargs, "${EXTEN:"))) {
			p2 = malloc(strlen(prio->appargs)+5);
			*p1 = 0;
			strcpy(p2, prio->appargs);
			strcat(p2, "${~~EXTEN~~:");
			if (*(p1+8))
				strcat(p2, p1+8);
			free(prio->appargs);
			prio->appargs = p2;
		}
	}
}

void destroy_extensions(struct ael_extension *exten)
{
	struct ael_extension *ne, *nen;
	for (ne=exten; ne; ne=nen) {
		struct ael_priority *pe, *pen;

		if (ne->name)
			free(ne->name);

		/* cidmatch fields are allocated with name, and freed when
		   the name field is freed. Don't do a free for this field,
		   unless you LIKE to see a crash! */

		if (ne->hints)
			free(ne->hints);

		for (pe=ne->plist; pe; pe=pen) {
			pen = pe->next;
			if (pe->app)
				free(pe->app);
			pe->app = 0;
			if (pe->appargs)
				free(pe->appargs);
			pe->appargs = 0;
			pe->origin = 0;
			pe->goto_true = 0;
			pe->goto_false = 0;
			free(pe);
		}
		nen = ne->next_exten;
		ne->next_exten = 0;
		ne->plist =0;
		ne->plist_last = 0;
		ne->next_exten = 0;
		ne->loop_break = 0;
		ne->loop_continue = 0;
		free(ne);
	}
}

static int label_inside_case(pval *label)
{
	pval *p = label;

	while( p && p->type != PV_MACRO && p->type != PV_CONTEXT ) /* early cutout, sort of */ {
		if( p->type == PV_CASE || p->type == PV_DEFAULT || p->type == PV_PATTERN ) {
			return 1;
		}

		p = p->dad;
	}
	return 0;
}

static void linkexten(struct ael_extension *exten, struct ael_extension *add)
{
	add->next_exten = exten->next_exten; /* this will reverse the order. Big deal. */
	exten->next_exten = add;
}

static void remove_spaces_before_equals(char *str)
{
	char *p;
	while( str && *str && *str != '=' )
	{
		if( *str == ' ' || *str == '\n' || *str == '\r' || *str == '\t' )
		{
			p = str;
			while( *p )
			{
				*p = *(p+1);
				p++;
			}
		}
		else
			str++;
	}
}

/* =============================================================================================== */
/* "CODE" GENERATOR -- Convert the AEL representation to asterisk extension language */
/* =============================================================================================== */

static void gen_match_to_pattern(char *pattern, char *result)
{
	/* the result will be a string that will be matched by pattern */
	char *p=pattern, *t=result;
	while (*p) {
		if (*p == 'x' || *p == 'n' || *p == 'z' || *p == 'X' || *p == 'N' || *p == 'Z')
			*t++ = '9';
		else if (*p == '[') {
			char *z = p+1;
			while (*z != ']')
				z++;
			if (*(z+1)== ']')
				z++;
			*t++=*(p+1); /* use the first char in the set */
			p = z;
		} else {
			*t++ = *p;
		}
		p++;
	}
	*t++ = 0; /* cap it off */
}

/* ==== a set of routines to search for a switch statement contained in the pval description */

int find_switch_item(pval *item);
int contains_switch(pval *item);


int find_switch_item(pval *item)
{
	switch ( item->type ) {
	case PV_LOCALVARDEC:
		/* fields: item->u1.str == string associated with this (word). */
		break;

	case PV_WORD:
		/* fields: item->u1.str == string associated with this (word). */
		break;

	case PV_MACRO:
		/* fields: item->u1.str     == name of macro
		           item->u2.arglist == pval list of PV_WORD arguments of macro, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg

				   item->u3.macro_statements == pval list of statements in macro body.
		*/
		/* had better not see this */
		if (contains_switch(item->u3.macro_statements))
			return 1;
		break;

	case PV_CONTEXT:
		/* fields: item->u1.str     == name of context
		           item->u2.statements == pval list of statements in context body
				   item->u3.abstract == int 1 if an abstract keyword were present
		*/
		/* had better not see this */
		if (contains_switch(item->u2.statements))
			return 1;
		break;

	case PV_MACRO_CALL:
		/* fields: item->u1.str     == name of macro to call
		           item->u2.arglist == pval list of PV_WORD arguments of macro call, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg
		*/
		break;

	case PV_APPLICATION_CALL:
		/* fields: item->u1.str     == name of application to call
		           item->u2.arglist == pval list of PV_WORD arguments of macro call, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg
		*/
		break;

	case PV_CASE:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		/* had better not see this */
		if (contains_switch(item->u2.statements))
			return 1;
		break;

	case PV_PATTERN:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		/* had better not see this */
		if (contains_switch(item->u2.statements))
			return 1;
		break;

	case PV_DEFAULT:
		/* fields:
		           item->u2.statements == pval list of statements under the case
		*/
		/* had better not see this */
		if (contains_switch(item->u2.statements))
			return 1;
		break;

	case PV_CATCH:
		/* fields: item->u1.str     == name of extension to catch
		           item->u2.statements == pval list of statements in context body
		*/
		/* had better not see this */
		if (contains_switch(item->u2.statements))
			return 1;
		break;

	case PV_SWITCHES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		break;

	case PV_ESWITCHES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		break;

	case PV_INCLUDES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		           item->u2.arglist  == pval list of 4 PV_WORD elements for time values
		*/
		break;

	case PV_STATEMENTBLOCK:
		/* fields: item->u1.list     == pval list of statements in block, one per entry in the list
		*/
		if (contains_switch(item->u1.list) )
			return 1;
		break;

	case PV_VARDEC:
		/* fields: item->u1.str     == variable name
		           item->u2.val     == variable value to assign
		*/
		break;

	case PV_GOTO:
		/* fields: item->u1.list     == pval list of PV_WORD target names, up to 3, in order as given by user.
		           item->u1.list->u1.str  == where the data on a PV_WORD will always be.
		*/
		break;

	case PV_LABEL:
		/* fields: item->u1.str     == label name
		*/
		break;

	case PV_FOR:
		/* fields: item->u1.for_init     == a string containing the initializer
		           item->u2.for_test     == a string containing the loop test
		           item->u3.for_inc      == a string containing the loop increment

				   item->u4.for_statements == a pval list of statements in the for ()
		*/
		if (contains_switch(item->u4.for_statements))
			return 1;
		break;

	case PV_WHILE:
		/* fields: item->u1.str        == the while conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the while ()
		*/
		if (contains_switch(item->u2.statements))
			return 1;
		break;

	case PV_BREAK:
		/* fields: none
		*/
		break;

	case PV_RETURN:
		/* fields: none
		*/
		break;

	case PV_CONTINUE:
		/* fields: none
		*/
		break;

	case PV_IFTIME:
		/* fields: item->u1.list        == there are 4 linked PV_WORDs here.

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		if (contains_switch(item->u2.statements))
			return 1;
		if ( item->u3.else_statements ) {
			if (contains_switch(item->u3.else_statements))
				return 1;
		}
		break;

	case PV_RANDOM:
		/* fields: item->u1.str        == the random number expression, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		if (contains_switch(item->u2.statements))
			return 1;
		if ( item->u3.else_statements ) {
			if (contains_switch(item->u3.else_statements))
				return 1;
		}
		break;

	case PV_IF:
		/* fields: item->u1.str        == the if conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		if (contains_switch(item->u2.statements))
			return 1;
		if ( item->u3.else_statements ) {
			if (contains_switch(item->u3.else_statements))
				return 1;
		}
		break;

	case PV_SWITCH:
		/* fields: item->u1.str        == the switch expression

				   item->u2.statements == a pval list of statements in the switch,
				   							(will be case statements, most likely!)
		*/
		return 1; /* JACKPOT */
		break;

	case PV_EXTENSION:
		/* fields: item->u1.str        == the extension name, label, whatever it's called

				   item->u2.statements == a pval list of statements in the extension
				   item->u3.hints      == a char * hint argument
				   item->u4.regexten   == an int boolean. non-zero says that regexten was specified
		*/
		if (contains_switch(item->u2.statements))
			return 1;
		break;

	case PV_IGNOREPAT:
		/* fields: item->u1.str        == the ignorepat data
		*/
		break;

	case PV_GLOBALS:
		/* fields: item->u1.statements     == pval list of statements, usually vardecs
		*/
		break;
	}
	return 0;
}

int contains_switch(pval *item)
{
	pval *i;

	for (i=item; i; i=i->next) {
		if (find_switch_item(i))
			return 1;
	}
	return 0;
}


static int gen_prios(struct ael_extension *exten, char *label, pval *statement, struct ael_extension *mother_exten, struct ast_context *this_context )
{
	pval *p,*p2,*p3;
	struct ael_priority *pr;
	struct ael_priority *for_init, *for_test, *for_inc, *for_loop, *for_end;
	struct ael_priority *while_test, *while_loop, *while_end;
	struct ael_priority *switch_set, *switch_test, *switch_end, *fall_thru, *switch_empty;
	struct ael_priority *if_test, *if_end, *if_skip, *if_false;
#ifdef OLD_RAND_ACTION
	struct ael_priority *rand_test, *rand_end, *rand_skip;
#endif
	RAII_VAR(char *, buf1, NULL, free);
	RAII_VAR(char *, buf2, NULL, free);
	RAII_VAR(char *, new_label, NULL, free);
	char *strp, *strp2;
	int default_exists;
	int local_control_statement_count;
	int first;
	struct ael_priority *loop_break_save;
	struct ael_priority *loop_continue_save;
	struct ael_extension *switch_case,*switch_null;

	if (!(buf1 = malloc(BUF_SIZE))) {
		return -1;
	}
	if (!(buf2 = malloc(BUF_SIZE))) {
		return -1;
	}
	if (!(new_label = malloc(BUF_SIZE))) {
		return -1;
	}

	if ((mother_exten && !mother_exten->checked_switch) || (exten && !exten->checked_switch)) {
		if (contains_switch(statement)) { /* only run contains_switch if you haven't checked before */
			if (mother_exten) {
				if (!mother_exten->has_switch) {
					for (first = 1; first >= 0; first--) {
						switch_set = new_prio();
						switch_set->type = AEL_APPCALL;
						switch_set->app = strdup("MSet");
						/* Are we likely inside a gosub subroutine? */
						if (!strcmp(mother_exten->name, "~~s~~") && first) {
							/* If we're not actually within a gosub, this will fail, but the
							 * second time through, it will get set.  If we are within gosub,
							 * the second time through is redundant, but acceptable. */
							switch_set->appargs = strdup("LOCAL(~~EXTEN~~)=${EXTEN}");
						} else {
							switch_set->appargs = strdup("~~EXTEN~~=${EXTEN}");
							first = 0;
						}
						linkprio(exten, switch_set, mother_exten);
						mother_exten->has_switch = 1;
						mother_exten->checked_switch = 1;
						if (exten) {
							exten->has_switch = 1;
							exten->checked_switch = 1;
						}
					}
				}
			} else if (exten) {
				if (!exten->has_switch) {
					for (first = 1; first >= 0; first--) {
						switch_set = new_prio();
						switch_set->type = AEL_APPCALL;
						switch_set->app = strdup("MSet");
						/* Are we likely inside a gosub subroutine? */
						if (!strcmp(exten->name, "~~s~~")) {
							/* If we're not actually within a gosub, this will fail, but the
							 * second time through, it will get set.  If we are within gosub,
							 * the second time through is redundant, but acceptable. */
							switch_set->appargs = strdup("LOCAL(~~EXTEN~~)=${EXTEN}");
						} else {
							switch_set->appargs = strdup("~~EXTEN~~=${EXTEN}");
							first = 0;
						}
						linkprio(exten, switch_set, mother_exten);
						exten->has_switch = 1;
						exten->checked_switch = 1;
						if (mother_exten) {
							mother_exten->has_switch = 1;
							mother_exten->checked_switch = 1;
						}
					}
				}
			}
		} else {
			if (mother_exten) {
				mother_exten->checked_switch = 1;
			}
			if (exten) {
				exten->checked_switch = 1;
			}
		}
	}
	for (p=statement; p; p=p->next) {
		switch (p->type) {
		case PV_VARDEC:
			pr = new_prio();
			pr->type = AEL_APPCALL;
			snprintf(buf1, BUF_SIZE, "%s=$[%s]", p->u1.str, p->u2.val);
			pr->app = strdup("MSet");
			remove_spaces_before_equals(buf1);
			pr->appargs = strdup(buf1);
			pr->origin = p;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_LOCALVARDEC:
			pr = new_prio();
			pr->type = AEL_APPCALL;
			snprintf(buf1, BUF_SIZE, "LOCAL(%s)=$[%s]", p->u1.str, p->u2.val);
			pr->app = strdup("MSet");
			remove_spaces_before_equals(buf1);
			pr->appargs = strdup(buf1);
			pr->origin = p;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_GOTO:
			pr = new_prio();
			pr->type = AEL_APPCALL;
			p->u2.goto_target = get_goto_target(p);
			if( p->u2.goto_target ) {
				p->u3.goto_target_in_case = label_inside_case(p->u2.goto_target);
			}

			if (!p->u1.list->next) /* just one */ {
				pr->app = strdup("Goto");
				if (!mother_exten)
					pr->appargs = strdup(p->u1.list->u1.str);
				else {  /* for the case of simple within-extension gotos in case/pattern/default statement blocks: */
					snprintf(buf1, BUF_SIZE, "%s,%s", mother_exten->name, p->u1.list->u1.str);
					pr->appargs = strdup(buf1);
				}

			} else if (p->u1.list->next && !p->u1.list->next->next) /* two */ {
				snprintf(buf1, BUF_SIZE, "%s,%s", p->u1.list->u1.str, p->u1.list->next->u1.str);
				pr->app = strdup("Goto");
				pr->appargs = strdup(buf1);
			} else if (p->u1.list->next && p->u1.list->next->next) {
				snprintf(buf1, BUF_SIZE, "%s,%s,%s", p->u1.list->u1.str,
						p->u1.list->next->u1.str,
						p->u1.list->next->next->u1.str);
				pr->app = strdup("Goto");
				pr->appargs = strdup(buf1);
			}
			pr->origin = p;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_LABEL:
			pr = new_prio();
			pr->type = AEL_LABEL;
			pr->origin = p;
			p->u3.compiled_label = exten;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_FOR:
			control_statement_count++;
			loop_break_save = exten->loop_break; /* save them, then restore before leaving */
			loop_continue_save = exten->loop_continue;
			snprintf(new_label, BUF_SIZE, "for_%s_%d", label, control_statement_count);
			for_init = new_prio();
			for_inc = new_prio();
			for_test = new_prio();
			for_loop = new_prio();
			for_end = new_prio();
			for_init->type = AEL_APPCALL;
			for_inc->type = AEL_APPCALL;
			for_test->type = AEL_FOR_CONTROL;
			for_test->goto_false = for_end;
			for_loop->type = AEL_CONTROL1; /* simple goto */
			for_end->type = AEL_APPCALL;
			for_init->app = strdup("MSet");

			strcpy(buf2,p->u1.for_init);
			remove_spaces_before_equals(buf2);
			strp = strchr(buf2, '=');
			if (strp) {
				strp2 = strchr(p->u1.for_init, '=');
				*(strp+1) = 0;
				strcat(buf2,"$[");
				strncat(buf2,strp2+1, BUF_SIZE-strlen(strp2+1)-2);
				strcat(buf2,"]");
				for_init->appargs = strdup(buf2);
			} else {
				strp2 = p->u1.for_init;
				while (*strp2 && isspace(*strp2))
					strp2++;
				if (*strp2 == '&') { /* itsa macro call */
					char *strp3 = strp2+1;
					while (*strp3 && isspace(*strp3))
						strp3++;
					strcpy(buf2, strp3);
					strp3 = strchr(buf2,'(');
					if (strp3) {
						*strp3 = ',';
					}
					strp3 = strrchr(buf2, ')');
					if (strp3)
						*strp3 = 0; /* remove the closing paren */
					for_init->appargs = strdup(buf2);
					free(for_init->app);
					for_init->app = strdup("Gosub");
				} else {  /* must be a regular app call */
					char *strp3;
					strcpy(buf2, strp2);
					strp3 = strchr(buf2,'(');
					if (strp3) {
						*strp3 = 0;
						free(for_init->app);
						for_init->app = strdup(buf2);
						for_init->appargs = strdup(strp3+1);
						strp3 = strrchr(for_init->appargs, ')');
						if (strp3)
							*strp3 = 0; /* remove the closing paren */
					}
				}
			}

			strcpy(buf2,p->u3.for_inc);
			remove_spaces_before_equals(buf2);
			strp = strchr(buf2, '=');
			if (strp) {  /* there's an = in this part; that means an assignment. set it up */
				strp2 = strchr(p->u3.for_inc, '=');
				*(strp+1) = 0;
				strcat(buf2,"$[");
				strncat(buf2,strp2+1, BUF_SIZE-strlen(strp2+1)-2);
				strcat(buf2,"]");
				for_inc->appargs = strdup(buf2);
				for_inc->app = strdup("MSet");
			} else {
				strp2 = p->u3.for_inc;
				while (*strp2 && isspace(*strp2))
					strp2++;
				if (*strp2 == '&') { /* itsa macro call  */
					char *strp3 = strp2+1;
					while (*strp3 && isspace(*strp3))
						strp3++;
					strcpy(buf2, strp3);
					strp3 = strchr(buf2,'(');
					if (strp3) {
						*strp3 = ',';
					}
					strp3 = strrchr(buf2, ')');
					if (strp3)
						*strp3 = 0; /* remove the closing paren */

					for_inc->appargs = strdup(buf2);

					for_inc->app = strdup("Gosub");
				} else {  /* must be a regular app call */
					char *strp3;
					strcpy(buf2, strp2);
					strp3 = strchr(buf2,'(');
					if (strp3) {
						*strp3 = 0;
						for_inc->app = strdup(buf2);
						for_inc->appargs = strdup(strp3+1);
						strp3 = strrchr(for_inc->appargs, ')');
						if (strp3)
							*strp3 = 0; /* remove the closing paren */
					}
				}
			}
			snprintf(buf1, BUF_SIZE, "$[%s]",p->u2.for_test);
			for_test->app = 0;
			for_test->appargs = strdup(buf1);
			for_loop->goto_true = for_test;
			snprintf(buf1, BUF_SIZE, "Finish for_%s_%d", label, control_statement_count);
			for_end->app = strdup("NoOp");
			for_end->appargs = strdup(buf1);
			/* link & load! */
			linkprio(exten, for_init, mother_exten);
			linkprio(exten, for_test, mother_exten);

			/* now, put the body of the for loop here */
			exten->loop_break = for_end;
			exten->loop_continue = for_inc;

			if (gen_prios(exten, new_label, p->u4.for_statements, mother_exten, this_context)) { /* this will link in all the statements here */
				return -1;
			}

			linkprio(exten, for_inc, mother_exten);
			linkprio(exten, for_loop, mother_exten);
			linkprio(exten, for_end, mother_exten);


			exten->loop_break = loop_break_save;
			exten->loop_continue = loop_continue_save;
			for_loop->origin = p;
			break;

		case PV_WHILE:
			control_statement_count++;
			loop_break_save = exten->loop_break; /* save them, then restore before leaving */
			loop_continue_save = exten->loop_continue;
			snprintf(new_label, BUF_SIZE, "while_%s_%d", label, control_statement_count);
			while_test = new_prio();
			while_loop = new_prio();
			while_end = new_prio();
			while_test->type = AEL_FOR_CONTROL;
			while_test->goto_false = while_end;
			while_loop->type = AEL_CONTROL1; /* simple goto */
			while_end->type = AEL_APPCALL;
			snprintf(buf1, BUF_SIZE, "$[%s]",p->u1.str);
			while_test->app = 0;
			while_test->appargs = strdup(buf1);
			while_loop->goto_true = while_test;
			snprintf(buf1, BUF_SIZE, "Finish while_%s_%d", label, control_statement_count);
			while_end->app = strdup("NoOp");
			while_end->appargs = strdup(buf1);

			linkprio(exten, while_test, mother_exten);

			/* now, put the body of the for loop here */
			exten->loop_break = while_end;
			exten->loop_continue = while_test;

			if (gen_prios(exten, new_label, p->u2.statements, mother_exten, this_context)) { /* this will link in all the while body statements here */
				return -1;
			}

			linkprio(exten, while_loop, mother_exten);
			linkprio(exten, while_end, mother_exten);


			exten->loop_break = loop_break_save;
			exten->loop_continue = loop_continue_save;
			while_loop->origin = p;
			break;

		case PV_SWITCH:
			control_statement_count++;
			local_control_statement_count = control_statement_count;
			loop_break_save = exten->loop_break; /* save them, then restore before leaving */
			loop_continue_save = exten->loop_continue;
			snprintf(new_label, BUF_SIZE, "sw_%s_%d", label, control_statement_count);
			switch_test = new_prio();
			switch_end = new_prio();
			switch_test->type = AEL_APPCALL;
			switch_end->type = AEL_APPCALL;
			snprintf(buf1, BUF_SIZE, "sw_%d_%s,10", control_statement_count, p->u1.str);
			switch_test->app = strdup("Goto");
			switch_test->appargs = strdup(buf1);
			snprintf(buf1, BUF_SIZE, "Finish switch_%s_%d", label, control_statement_count);
			switch_end->app = strdup("NoOp");
			switch_end->appargs = strdup(buf1);
			switch_end->origin = p;
			switch_end->exten = exten;

			linkprio(exten, switch_test, mother_exten);
			linkprio(exten, switch_end, mother_exten);

			exten->loop_break = switch_end;
			exten->loop_continue = 0;
			default_exists = 0;

			for (p2=p->u2.statements; p2; p2=p2->next) {
				/* now, for each case/default put the body of the for loop here */
				if (p2->type == PV_CASE) {
					/* ok, generate a extension and link it in */
					switch_case = new_exten();
					if (mother_exten && mother_exten->checked_switch) {
						switch_case->has_switch = mother_exten->has_switch;
						switch_case->checked_switch = mother_exten->checked_switch;
					}
					if (exten && exten->checked_switch) {
						switch_case->has_switch = exten->has_switch;
						switch_case->checked_switch = exten->checked_switch;
					}
					switch_case->context = this_context;
					switch_case->is_switch = 1;
					/* the break/continue locations are inherited from parent */
					switch_case->loop_break = exten->loop_break;
					switch_case->loop_continue = exten->loop_continue;

					linkexten(exten,switch_case);
					snprintf(buf1, BUF_SIZE, "sw_%d_%s", local_control_statement_count, p2->u1.str);
					switch_case->name = strdup(buf1);
					snprintf(new_label, BUF_SIZE, "sw_%s_%s_%d", label, p2->u1.str, local_control_statement_count);

					if (gen_prios(switch_case, new_label, p2->u2.statements, exten, this_context)) { /* this will link in all the case body statements here */
						return -1;
					}

					/* here is where we write code to "fall thru" to the next case... if there is one... */
					for (p3=p2->u2.statements; p3; p3=p3->next) {
						if (!p3->next)
							break;
					}
					/* p3 now points the last statement... */
					if (!p3 || ( p3->type != PV_GOTO && p3->type != PV_BREAK && p3->type != PV_RETURN) ) {
						/* is there a following CASE/PATTERN/DEFAULT? */
						if (p2->next && p2->next->type == PV_CASE) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							snprintf(buf1, BUF_SIZE, "sw_%d_%s,10", local_control_statement_count, p2->next->u1.str);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (p2->next && p2->next->type == PV_PATTERN) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							gen_match_to_pattern(p2->next->u1.str, buf2);
							snprintf(buf1, BUF_SIZE, "sw_%d_%s,10", local_control_statement_count, buf2);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (p2->next && p2->next->type == PV_DEFAULT) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							snprintf(buf1, BUF_SIZE, "sw_%d_.,10", local_control_statement_count);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (!p2->next) {
							fall_thru = new_prio();
							fall_thru->type = AEL_CONTROL1;
							fall_thru->goto_true = switch_end;
							fall_thru->app = strdup("Goto");
							linkprio(switch_case, fall_thru, mother_exten);
						}
					}
					if (switch_case->return_needed) { /* returns don't generate a goto eoe (end of extension) any more, just a Return() app call) */
						char buf[2000];
						struct ael_priority *np2 = new_prio();
						np2->type = AEL_APPCALL;
						np2->app = strdup("NoOp");
						snprintf(buf, BUF_SIZE, "End of Extension %s", switch_case->name);
						np2->appargs = strdup(buf);
						linkprio(switch_case, np2, mother_exten);
						switch_case-> return_target = np2;
					}
				} else if (p2->type == PV_PATTERN) {
					/* ok, generate a extension and link it in */
					switch_case = new_exten();
					if (mother_exten && mother_exten->checked_switch) {
						switch_case->has_switch = mother_exten->has_switch;
						switch_case->checked_switch = mother_exten->checked_switch;
					}
					if (exten && exten->checked_switch) {
						switch_case->has_switch = exten->has_switch;
						switch_case->checked_switch = exten->checked_switch;
					}
					switch_case->context = this_context;
					switch_case->is_switch = 1;
					/* the break/continue locations are inherited from parent */
					switch_case->loop_break = exten->loop_break;
					switch_case->loop_continue = exten->loop_continue;

					linkexten(exten,switch_case);
					snprintf(buf1, BUF_SIZE, "_sw_%d_%s", local_control_statement_count, p2->u1.str);
					switch_case->name = strdup(buf1);
					snprintf(new_label, BUF_SIZE, "sw_%s_%s_%d", label, p2->u1.str, local_control_statement_count);

					if (gen_prios(switch_case, new_label, p2->u2.statements, exten, this_context)) { /* this will link in all the while body statements here */
						return -1;
					}
					/* here is where we write code to "fall thru" to the next case... if there is one... */
					for (p3=p2->u2.statements; p3; p3=p3->next) {
						if (!p3->next)
							break;
					}
					/* p3 now points the last statement... */
					if (!p3 || ( p3->type != PV_GOTO && p3->type != PV_BREAK && p3->type != PV_RETURN)) {
						/* is there a following CASE/PATTERN/DEFAULT? */
						if (p2->next && p2->next->type == PV_CASE) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							snprintf(buf1, BUF_SIZE, "sw_%d_%s,10", local_control_statement_count, p2->next->u1.str);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (p2->next && p2->next->type == PV_PATTERN) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							gen_match_to_pattern(p2->next->u1.str, buf2);
							snprintf(buf1, BUF_SIZE, "sw_%d_%s,10", local_control_statement_count, buf2);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (p2->next && p2->next->type == PV_DEFAULT) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							snprintf(buf1, BUF_SIZE, "sw_%d_.,10", local_control_statement_count);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (!p2->next) {
							fall_thru = new_prio();
							fall_thru->type = AEL_CONTROL1;
							fall_thru->goto_true = switch_end;
							fall_thru->app = strdup("Goto");
							linkprio(switch_case, fall_thru, mother_exten);
						}
					}
					if (switch_case->return_needed) { /* returns don't generate a goto eoe (end of extension) any more, just a Return() app call) */
						char buf[2000];
						struct ael_priority *np2 = new_prio();
						np2->type = AEL_APPCALL;
						np2->app = strdup("NoOp");
						snprintf(buf,sizeof(buf),"End of Extension %s", switch_case->name);
						np2->appargs = strdup(buf);
						linkprio(switch_case, np2, mother_exten);
						switch_case-> return_target = np2;
					}
				} else if (p2->type == PV_DEFAULT) {
					/* ok, generate a extension and link it in */
					switch_case = new_exten();
					if (mother_exten && mother_exten->checked_switch) {
						switch_case->has_switch = mother_exten->has_switch;
						switch_case->checked_switch = mother_exten->checked_switch;
					}
					if (exten && exten->checked_switch) {
						switch_case->has_switch = exten->has_switch;
						switch_case->checked_switch = exten->checked_switch;
					}
					switch_case->context = this_context;
					switch_case->is_switch = 1;

					/* new: the default case intros a pattern with ., which covers ALMOST everything.
					   but it doesn't cover a NULL pattern. So, we'll define a null extension to match
					   that goto's the default extension. */

					default_exists++;
					switch_null = new_exten();
					if (mother_exten && mother_exten->checked_switch) {
						switch_null->has_switch = mother_exten->has_switch;
						switch_null->checked_switch = mother_exten->checked_switch;
					}
					if (exten && exten->checked_switch) {
						switch_null->has_switch = exten->has_switch;
						switch_null->checked_switch = exten->checked_switch;
					}
					switch_null->context = this_context;
					switch_null->is_switch = 1;
					switch_empty = new_prio();
					snprintf(buf1, BUF_SIZE, "sw_%d_.,10", local_control_statement_count);
					switch_empty->app = strdup("Goto");
					switch_empty->appargs = strdup(buf1);
					linkprio(switch_null, switch_empty, mother_exten);
					snprintf(buf1, BUF_SIZE, "sw_%d_", local_control_statement_count);
					switch_null->name = strdup(buf1);
					switch_null->loop_break = exten->loop_break;
					switch_null->loop_continue = exten->loop_continue;
					linkexten(exten,switch_null);

					/* the break/continue locations are inherited from parent */
					switch_case->loop_break = exten->loop_break;
					switch_case->loop_continue = exten->loop_continue;
					linkexten(exten,switch_case);
					snprintf(buf1, BUF_SIZE, "_sw_%d_.", local_control_statement_count);
					switch_case->name = strdup(buf1);

					snprintf(new_label, BUF_SIZE, "sw_%s_default_%d", label, local_control_statement_count);

					if (gen_prios(switch_case, new_label, p2->u2.statements, exten, this_context)) { /* this will link in all the default:  body statements here */
						return -1;
					}

					/* here is where we write code to "fall thru" to the next case... if there is one... */
					for (p3=p2->u2.statements; p3; p3=p3->next) {
						if (!p3->next)
							break;
					}
					/* p3 now points the last statement... */
					if (!p3 || (p3->type != PV_GOTO && p3->type != PV_BREAK && p3->type != PV_RETURN)) {
						/* is there a following CASE/PATTERN/DEFAULT? */
						if (p2->next && p2->next->type == PV_CASE) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							snprintf(buf1, BUF_SIZE, "sw_%d_%s,10", local_control_statement_count, p2->next->u1.str);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (p2->next && p2->next->type == PV_PATTERN) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							gen_match_to_pattern(p2->next->u1.str, buf2);
							snprintf(buf1, BUF_SIZE, "sw_%d_%s,10", local_control_statement_count, buf2);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (p2->next && p2->next->type == PV_DEFAULT) {
							fall_thru = new_prio();
							fall_thru->type = AEL_APPCALL;
							fall_thru->app = strdup("Goto");
							snprintf(buf1, BUF_SIZE, "sw_%d_.,10", local_control_statement_count);
							fall_thru->appargs = strdup(buf1);
							linkprio(switch_case, fall_thru, mother_exten);
						} else if (!p2->next) {
							fall_thru = new_prio();
							fall_thru->type = AEL_CONTROL1;
							fall_thru->goto_true = switch_end;
							fall_thru->app = strdup("Goto");
							linkprio(switch_case, fall_thru, mother_exten);
						}
					}
					if (switch_case->return_needed) { /* returns don't generate a goto eoe (end of extension) any more, just a Return() app call) */
						char buf[2000];
						struct ael_priority *np2 = new_prio();
						np2->type = AEL_APPCALL;
						np2->app = strdup("NoOp");
						snprintf(buf,sizeof(buf),"End of Extension %s", switch_case->name);
						np2->appargs = strdup(buf);
						linkprio(switch_case, np2, mother_exten);
						switch_case-> return_target = np2;
					}
				} else {
					/* what could it be??? */
				}
			}

			exten->loop_break = loop_break_save;
			exten->loop_continue = loop_continue_save;
			switch_test->origin = p;
			switch_end->origin = p;
			break;

		case PV_MACRO_CALL:
			pr = new_prio();
			pr->type = AEL_APPCALL;
			snprintf(buf1, BUF_SIZE, "%s,~~s~~,1", p->u1.str);
			first = 1;
			for (p2 = p->u2.arglist; p2; p2 = p2->next) {
				if (first)
				{
					strcat(buf1,"(");
					first = 0;
				}
				else
					strcat(buf1,",");
				strcat(buf1,p2->u1.str);
			}
			if (!first)
				strcat(buf1,")");

			pr->app = strdup("Gosub");
			pr->appargs = strdup(buf1);
			pr->origin = p;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_APPLICATION_CALL:
			pr = new_prio();
			pr->type = AEL_APPCALL;
			buf1[0] = 0;
			for (p2 = p->u2.arglist; p2; p2 = p2->next) {
				if (p2 != p->u2.arglist )
					strcat(buf1,",");
				strcat(buf1,p2->u1.str);
			}
			pr->app = strdup(p->u1.str);
			pr->appargs = strdup(buf1);
			pr->origin = p;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_BREAK:
			pr = new_prio();
			pr->type = AEL_CONTROL1; /* simple goto */
			pr->goto_true = exten->loop_break;
			pr->origin = p;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_RETURN: /* hmmmm */
			pr = new_prio();
			pr->type = AEL_RETURN; /* simple Return */
			/* exten->return_needed++; */
			pr->app = strdup("Return");
			pr->appargs = strdup("");
			pr->origin = p;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_CONTINUE:
			pr = new_prio();
			pr->type = AEL_CONTROL1; /* simple goto */
			pr->goto_true = exten->loop_continue;
			pr->origin = p;
			linkprio(exten, pr, mother_exten);
			break;

		case PV_IFTIME:
			control_statement_count++;
			snprintf(new_label, BUF_SIZE, "iftime_%s_%d", label, control_statement_count);

			if_test = new_prio();
			if_test->type = AEL_IFTIME_CONTROL;
			snprintf(buf1, BUF_SIZE, "%s,%s,%s,%s",
					 p->u1.list->u1.str,
					 p->u1.list->next->u1.str,
					 p->u1.list->next->next->u1.str,
					 p->u1.list->next->next->next->u1.str);
			if_test->app = 0;
			if_test->appargs = strdup(buf1);
			if_test->origin = p;

			if_end = new_prio();
			if_end->type = AEL_APPCALL;
			snprintf(buf1, BUF_SIZE, "Finish iftime_%s_%d", label, control_statement_count);
			if_end->app = strdup("NoOp");
			if_end->appargs = strdup(buf1);

			if (p->u3.else_statements) {
				if_skip = new_prio();
				if_skip->type = AEL_CONTROL1; /* simple goto */
				if_skip->goto_true = if_end;
				if_skip->origin  = p;

			} else {
				if_skip = 0;

				if_test->goto_false = if_end;
			}

			if_false = new_prio();
			if_false->type = AEL_CONTROL1;
			if (p->u3.else_statements) {
				if_false->goto_true = if_skip; /* +1 */
			} else {
				if_false->goto_true = if_end;
			}

			/* link & load! */
			linkprio(exten, if_test, mother_exten);
			linkprio(exten, if_false, mother_exten);

			/* now, put the body of the if here */

			if (gen_prios(exten, new_label, p->u2.statements, mother_exten, this_context)) { /* this will link in all the statements here */
				return -1;
			}

			if (p->u3.else_statements) {
				linkprio(exten, if_skip, mother_exten);
				if (gen_prios(exten, new_label, p->u3.else_statements, mother_exten, this_context)) { /* this will link in all the statements here */
					return -1;
				}
			}

			linkprio(exten, if_end, mother_exten);

			break;

		case PV_RANDOM:
		case PV_IF:
			control_statement_count++;
			snprintf(new_label, BUF_SIZE, "if_%s_%d", label, control_statement_count);

			if_test = new_prio();
			if_end = new_prio();
			if_test->type = AEL_IF_CONTROL;
			if_end->type = AEL_APPCALL;
			if ( p->type == PV_RANDOM )
				snprintf(buf1, BUF_SIZE, "$[${RAND(0,99)} < (%s)]", p->u1.str);
			else
				snprintf(buf1, BUF_SIZE, "$[%s]", p->u1.str);
			if_test->app = 0;
			if_test->appargs = strdup(buf1);
			snprintf(buf1, BUF_SIZE, "Finish if_%s_%d", label, control_statement_count);
			if_end->app = strdup("NoOp");
			if_end->appargs = strdup(buf1);
			if_test->origin = p;

			if (p->u3.else_statements) {
				if_skip = new_prio();
				if_skip->type = AEL_CONTROL1; /* simple goto */
				if_skip->goto_true = if_end;
				if_test->goto_false = if_skip;;
			} else {
				if_skip = 0;
				if_test->goto_false = if_end;;
			}

			/* link & load! */
			linkprio(exten, if_test, mother_exten);

			/* now, put the body of the if here */

			if (gen_prios(exten, new_label, p->u2.statements, mother_exten, this_context)) { /* this will link in all the statements here */
				return -1;
			}

			if (p->u3.else_statements) {
				linkprio(exten, if_skip, mother_exten);
				if (gen_prios(exten, new_label, p->u3.else_statements, mother_exten, this_context)) { /* this will link in all the statements here */
					return -1;
				}
			}

			linkprio(exten, if_end, mother_exten);

			break;

		case PV_STATEMENTBLOCK:
			if (gen_prios(exten, label, p->u1.list, mother_exten, this_context)) { /* recurse into the block */
				return -1;
			}
			break;

		case PV_CATCH:
			control_statement_count++;
			/* generate an extension with name of catch, put all catch stats
			   into this exten! */
			switch_case = new_exten();
			if (mother_exten && mother_exten->checked_switch) {
				switch_case->has_switch = mother_exten->has_switch;
				switch_case->checked_switch = mother_exten->checked_switch;
			}
			if (exten && exten->checked_switch) {
				switch_case->has_switch = exten->has_switch;
				switch_case->checked_switch = exten->checked_switch;
			}

			switch_case->context = this_context;
			linkexten(exten,switch_case);
			switch_case->name = strdup(p->u1.str);
			snprintf(new_label, BUF_SIZE, "catch_%s_%d",p->u1.str, control_statement_count);

			if (gen_prios(switch_case, new_label, p->u2.statements, mother_exten,this_context)) { /* this will link in all the catch body statements here */
				return -1;
			}
			if (switch_case->return_needed) { /* returns now generate a Return() app call, no longer a goto to the end of the exten */
				char buf[2000];
				struct ael_priority *np2 = new_prio();
				np2->type = AEL_APPCALL;
				np2->app = strdup("NoOp");
				snprintf(buf,sizeof(buf),"End of Extension %s", switch_case->name);
				np2->appargs = strdup(buf);
				linkprio(switch_case, np2, mother_exten);
				switch_case-> return_target = np2;
			}

			break;
		default:
			break;
		}
	}
	return 0;
}

void set_priorities(struct ael_extension *exten)
{
	int i;
	struct ael_priority *pr;
	do {
		if (exten->is_switch)
			i = 10;
		else if (exten->regexten)
			i=2;
		else
			i=1;

		for (pr=exten->plist; pr; pr=pr->next) {
			pr->priority_num = i;

			if (!pr->origin || (pr->origin && pr->origin->type != PV_LABEL) ) /* Labels don't show up in the dialplan,
												  but we want them to point to the right
												  priority, which would be the next line
												  after the label; */
				i++;
		}

		exten = exten->next_exten;
	} while ( exten );
}

void add_extensions(struct ael_extension *exten)
{
	struct ael_priority *pr;
	char *label=0;
	char realext[AST_MAX_EXTENSION];
	if (!exten) {
		ast_log(LOG_WARNING, "This file is Empty!\n" );
		return;
	}
	do {
		struct ael_priority *last = 0;

		pbx_substitute_variables_helper(NULL, exten->name, realext, sizeof(realext) - 1);
		if (exten->hints) {
			if (ast_add_extension2(exten->context, 0 /*no replace*/, realext, PRIORITY_HINT, NULL, exten->cidmatch,
								  exten->hints, NULL, ast_free_ptr, registrar, NULL, 0)) {
				ast_log(LOG_WARNING, "Unable to add step at priority 'hint' of extension '%s'\n",
						exten->name);
			}
		}

		for (pr=exten->plist; pr; pr=pr->next) {
			char app[2000];
			char appargs[2000];

			/* before we can add the extension, we need to prep the app/appargs;
			   the CONTROL types need to be done after the priority numbers are calculated.
			*/
			if (pr->type == AEL_LABEL) /* don't try to put labels in the dialplan! */ {
				last = pr;
				continue;
			}

			if (pr->app)
				strcpy(app, pr->app);
			else
				app[0] = 0;
			if (pr->appargs )
				strcpy(appargs, pr->appargs);
			else
				appargs[0] = 0;
			switch( pr->type ) {
			case AEL_APPCALL:
				/* easy case. Everything is all set up */
				break;

			case AEL_CONTROL1: /* FOR loop, WHILE loop, BREAK, CONTINUE, IF, IFTIME */
				/* simple, unconditional goto. */
				strcpy(app,"Goto");
				if (pr->goto_true->origin && pr->goto_true->origin->type == PV_SWITCH ) {
					snprintf(appargs,sizeof(appargs),"%s,%d", pr->goto_true->exten->name, pr->goto_true->priority_num);
				} else if (pr->goto_true->origin && pr->goto_true->origin->type == PV_IFTIME && pr->goto_true->origin->u3.else_statements ) {
					snprintf(appargs,sizeof(appargs),"%d", pr->goto_true->priority_num+1);
				} else
					snprintf(appargs,sizeof(appargs),"%d", pr->goto_true->priority_num);
				break;

			case AEL_FOR_CONTROL:  /* WHILE loop test, FOR loop test */
				strcpy(app,"GotoIf");
				snprintf(appargs,sizeof(appargs),"%s?%d:%d", pr->appargs, pr->priority_num+1, pr->goto_false->priority_num);
				break;

			case AEL_IF_CONTROL:
				strcpy(app,"GotoIf");
				if (pr->origin->u3.else_statements )
					snprintf(appargs,sizeof(appargs),"%s?%d:%d", pr->appargs, pr->priority_num+1, pr->goto_false->priority_num+1);
				else
					snprintf(appargs,sizeof(appargs),"%s?%d:%d", pr->appargs, pr->priority_num+1, pr->goto_false->priority_num);
				break;

			case AEL_RAND_CONTROL:
				strcpy(app,"Random");
				snprintf(appargs,sizeof(appargs),"%s:%d", pr->appargs, pr->goto_true->priority_num+1);
				break;

			case AEL_IFTIME_CONTROL:
				strcpy(app,"GotoIfTime");
				snprintf(appargs,sizeof(appargs),"%s?%d", pr->appargs, pr->priority_num+2);
				break;

			case AEL_RETURN:
				strcpy(app,"Return");
				appargs[0] = 0;
				break;

			default:
				break;
			}
			if (last && last->type == AEL_LABEL ) {
				label = last->origin->u1.str;
			}
			else
				label = 0;

			if (ast_add_extension2(exten->context, 0 /*no replace*/, realext, pr->priority_num, (label?label:NULL), exten->cidmatch,
								  app, strdup(appargs), ast_free_ptr, registrar, NULL, 0)) {
				ast_log(LOG_WARNING, "Unable to add step at priority '%d' of extension '%s'\n", pr->priority_num,
						exten->name);
			}
			last = pr;
		}
		exten = exten->next_exten;
	} while ( exten );
}

static void attach_exten(struct ael_extension **list, struct ael_extension *newmem)
{
	/* travel to the end of the list... */
	struct ael_extension *lptr;
	if( !*list ) {
		*list = newmem;
		return;
	}
	lptr = *list;

	while( lptr->next_exten ) {
		lptr = lptr->next_exten;
	}
	/* lptr should now pointing to the last element in the list; it has a null next_exten pointer */
	lptr->next_exten = newmem;
}

static pval *get_extension_or_contxt(pval *p)
{
	while( p && p->type != PV_EXTENSION && p->type != PV_CONTEXT && p->type != PV_MACRO ) {

		p = p->dad;
	}

	return p;
}

static pval *get_contxt(pval *p)
{
	while( p && p->type != PV_CONTEXT && p->type != PV_MACRO ) {

		p = p->dad;
	}

	return p;
}

static void fix_gotos_in_extensions(struct ael_extension *exten)
{
	struct ael_extension *e;
	for(e=exten;e;e=e->next_exten) {

		struct ael_priority *p;
		for(p=e->plist;p;p=p->next) {

			if( p->origin && p->origin->type == PV_GOTO && p->origin->u3.goto_target_in_case ) {

				/* fix the extension of the goto target to the actual extension in the post-compiled dialplan */

				pval *target = p->origin->u2.goto_target;
				struct ael_extension *z = target->u3.compiled_label;
				pval *pv2 = p->origin;
				char buf1[500];
				char *apparg_save = p->appargs;

				p->appargs = 0;
				if (!pv2->u1.list->next) /* just one  -- it won't hurt to repeat the extension */ {
					snprintf(buf1,sizeof(buf1),"%s,%s", z->name, pv2->u1.list->u1.str);
					p->appargs = strdup(buf1);

				} else if (pv2->u1.list->next && !pv2->u1.list->next->next) /* two */ {
					snprintf(buf1,sizeof(buf1),"%s,%s", z->name, pv2->u1.list->next->u1.str);
					p->appargs = strdup(buf1);
				} else if (pv2->u1.list->next && pv2->u1.list->next->next) {
					snprintf(buf1,sizeof(buf1),"%s,%s,%s", pv2->u1.list->u1.str,
							 z->name,
							 pv2->u1.list->next->next->u1.str);
					p->appargs = strdup(buf1);
				}
				else
					printf("WHAT? The goto doesn't fall into one of three cases for GOTO????\n");

				if( apparg_save ) {
					free(apparg_save);
				}
			}
		}
	}
}

static int context_used(struct ael_extension *exten_list, struct ast_context *context)
{
	struct ael_extension *exten;
	/* Check the simple elements first */
	if (ast_walk_context_extensions(context, NULL) || ast_context_includes_count(context) || ast_context_ignorepats_count(context) || ast_context_switches_count(context)) {
		return 1;
	}
	for (exten = exten_list; exten; exten = exten->next_exten) {
		if (exten->context == context) {
			return 1;
		}
	}
	return 0;
}

int ast_compile_ael2(struct ast_context **local_contexts, struct ast_hashtab *local_table, struct pval *root)
{
	pval *p,*p2;
	struct ast_context *context;
	char buf[2000];
	struct ael_extension *exten;
	struct ael_extension *exten_list = 0;

	/* Reset the counter so that we get consistent labels between reloads */
	control_statement_count = 0;

	for (p=root; p; p=p->next ) { /* do the globals first, so they'll be there
									 when we try to eval them */
		switch (p->type) {
		case PV_GLOBALS:
			/* just VARDEC elements */
			for (p2=p->u1.list; p2; p2=p2->next) {
				char buf2[2000];
				snprintf(buf2,sizeof(buf2),"%s=%s", p2->u1.str, p2->u2.val);
				pbx_builtin_setvar(NULL, buf2);
			}
			break;
		default:
			break;
		}
	}

	for (p=root; p; p=p->next ) {
		pval *lp;
		int argc;

		switch (p->type) {
		case PV_MACRO:

			context = ast_context_find_or_create(local_contexts, local_table, p->u1.str, registrar);

			exten = new_exten();
			exten->context = context;
			exten->name = strdup("~~s~~");
			argc = 1;
			for (lp=p->u2.arglist; lp; lp=lp->next) {
				/* for each arg, set up a "Set" command */
				struct ael_priority *np2 = new_prio();
				np2->type = AEL_APPCALL;
				np2->app = strdup("MSet");
				snprintf(buf,sizeof(buf),"LOCAL(%s)=${ARG%d}", lp->u1.str, argc++);
				remove_spaces_before_equals(buf);
				np2->appargs = strdup(buf);
				linkprio(exten, np2, NULL);
			}

			/* CONTAINS APPCALLS, CATCH, just like extensions... */
			if (gen_prios(exten, p->u1.str, p->u3.macro_statements, 0, context)) {
				return -1;
			}
			if (exten->return_needed) {  /* most likely, this will go away */
				struct ael_priority *np2 = new_prio();
				np2->type = AEL_APPCALL;
				np2->app = strdup("NoOp");
				snprintf(buf,sizeof(buf),"End of Macro %s-%s",p->u1.str, exten->name);
				np2->appargs = strdup(buf);
				linkprio(exten, np2, NULL);
				exten-> return_target = np2;
			}

			set_priorities(exten);
			attach_exten(&exten_list, exten);
			break;

		case PV_GLOBALS:
			/* already done */
			break;

		case PV_CONTEXT:
			context = ast_context_find_or_create(local_contexts, local_table, p->u1.str, registrar);

			/* contexts contain: ignorepat, includes, switches, eswitches, extensions,  */
			for (p2=p->u2.statements; p2; p2=p2->next) {
				pval *p3;
				char *s3;

				switch (p2->type) {
				case PV_EXTENSION:
					exten = new_exten();
					exten->name = strdup(p2->u1.str);
					exten->context = context;

					if( (s3=strchr(exten->name, '/') ) != 0 )
					{
						*s3 = 0;
						exten->cidmatch = s3+1;
					}

					if ( p2->u3.hints )
						exten->hints = strdup(p2->u3.hints);
					exten->regexten = p2->u4.regexten;
					if (gen_prios(exten, p->u1.str, p2->u2.statements, 0, context)) {
						return -1;
					}
					if (exten->return_needed) { /* returns don't generate a goto eoe (end of extension) any more, just a Return() app call) */
						struct ael_priority *np2 = new_prio();
						np2->type = AEL_APPCALL;
						np2->app = strdup("NoOp");
						snprintf(buf,sizeof(buf),"End of Extension %s", exten->name);
						np2->appargs = strdup(buf);
						linkprio(exten, np2, NULL);
						exten-> return_target = np2;
					}
					/* is the last priority in the extension a label? Then add a trailing no-op */
					if ( exten->plist_last && exten->plist_last->type == AEL_LABEL ) {
						struct ael_priority *np2 = new_prio();
						np2->type = AEL_APPCALL;
						np2->app = strdup("NoOp");
						snprintf(buf,sizeof(buf),"A NoOp to follow a trailing label %s", exten->plist_last->origin->u1.str);
						np2->appargs = strdup(buf);
						linkprio(exten, np2, NULL);
					}

					set_priorities(exten);
					attach_exten(&exten_list, exten);
					break;

				case PV_IGNOREPAT:
					ast_context_add_ignorepat2(context, p2->u1.str, registrar);
					break;

				case PV_INCLUDES:
					for (p3 = p2->u1.list; p3 ;p3=p3->next) {
						if ( p3->u2.arglist ) {
							snprintf(buf,sizeof(buf), "%s,%s,%s,%s,%s",
									 p3->u1.str,
									 p3->u2.arglist->u1.str,
									 p3->u2.arglist->next->u1.str,
									 p3->u2.arglist->next->next->u1.str,
									 p3->u2.arglist->next->next->next->u1.str);
							ast_context_add_include2(context, buf, registrar);
						} else
							ast_context_add_include2(context, p3->u1.str, registrar);
					}
					break;

				case PV_SWITCHES:
					for (p3 = p2->u1.list; p3 ;p3=p3->next) {
						char *c = strchr(p3->u1.str, '/');
						if (c) {
							*c = '\0';
							c++;
						} else
							c = "";

						ast_context_add_switch2(context, p3->u1.str, c, 0, registrar);
					}
					break;

				case PV_ESWITCHES:
					for (p3 = p2->u1.list; p3 ;p3=p3->next) {
						char *c = strchr(p3->u1.str, '/');
						if (c) {
							*c = '\0';
							c++;
						} else
							c = "";

						ast_context_add_switch2(context, p3->u1.str, c, 1, registrar);
					}
					break;
				default:
					break;
				}
			}

			break;

		default:
			/* huh? what? */
			break;

		}
	}

	/* Create default "h" bubble context */
	if (ast_custom_function_find("DIALPLAN_EXISTS") && ast_custom_function_find("STACK_PEEK")) {
		int i;
		const char *h_context = "ael-builtin-h-bubble";
		struct ael_priority *np;
		struct {
			int priority;
			const char *app;
			const char *arg;
		} steps[] = {
			/* Start high, to avoid conflict with existing h extensions */
			{ 1, "Goto", "9991" },
			/* Save the context, because after the StackPop, it disappears */
			{ 9991, "Set", "~~parentcxt~~=${STACK_PEEK(1,c,1)}" },
			/* If we're not in a Gosub frame, exit */
			{ 9992, "GotoIf", "$[\"${~~parentcxt~~}\"=\"\"]?9996" },
			/* Check for an "h" extension in that context */
			{ 9993, "GotoIf", "${DIALPLAN_EXISTS(${~~parentcxt~~},h,1)}?9994:9996" },
			/* Pop off the stack frame to prevent an infinite loop */
			{ 9994, "StackPop", "" },
			/* Finally, go there. */
			{ 9995, "Goto", "${~~parentcxt~~},h,1" },
			/* Just an empty priority for jumping out early */
			{ 9996, "NoOp", "" }
		};
		context = ast_context_find_or_create(local_contexts, local_table, h_context, registrar);
		if (context_used(exten_list, context)) {
			int found = 0;
			while (!found) {
				/* Pick a new context name that is not used. */
				char h_context_template[] = "/tmp/ael-builtin-h-bubble-XXXXXX";
				int fd = mkstemp(h_context_template);
				unlink(h_context_template);
				close(fd);
				context = ast_context_find_or_create(local_contexts, local_table, h_context_template + 5, registrar);
				found = !context_used(exten_list, context);
			}
			h_context = ast_get_context_name(context);
		}
		exten = new_exten();
		exten->context = context;
		exten->name = strdup("h");

		for (i = 0; i < ARRAY_LEN(steps); i++) {
			np = new_prio();
			np->type = AEL_APPCALL;
			np->priority_num = steps[i].priority;
			np->app = strdup(steps[i].app);
			np->appargs = strdup(steps[i].arg);
			linkprio(exten, np, NULL);
		}
		attach_exten(&exten_list, exten);

		/* Include the default "h" bubble context in each macro context */
		for (exten = exten_list; exten; exten = exten->next_exten) {
			/* All macros contain a "~~s~~" extension, and it's the first created.  If
			 * we perchance get a non-macro context, it's no big deal; the logic is
			 * designed to exit out smoothly if not called from within a Gosub. */
			if (!strcmp(exten->name, "~~s~~")) {
				ast_context_add_include2(exten->context, h_context, registrar);
			}
		}
	}

	/* moved these from being done after a macro or extension were processed,
	   to after all processing is done, for the sake of fixing gotos to labels inside cases... */
	/* I guess this would be considered 2nd pass of compiler now... */
	fix_gotos_in_extensions(exten_list); /* find and fix extension ref in gotos to labels that are in case statements */
	add_extensions(exten_list);   /* actually makes calls to create priorities in ast_contexts -- feeds dialplan to asterisk */
	destroy_extensions(exten_list);  /* all that remains is an empty husk, discard of it as is proper */

	return 0;
}


/* DESTROY the PVAL tree ============================================================================ */



void destroy_pval_item(pval *item)
{
	if (item == NULL) {
		ast_log(LOG_WARNING, "null item\n");
		return;
	}

	if (item->filename)
		free(item->filename);

	switch (item->type) {
	case PV_WORD:
		/* fields: item->u1.str == string associated with this (word). */
		if (item->u1.str )
			free(item->u1.str);
		if ( item->u2.arglist )
			destroy_pval(item->u2.arglist);
		break;

	case PV_MACRO:
		/* fields: item->u1.str     == name of macro
		           item->u2.arglist == pval list of PV_WORD arguments of macro, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg

				   item->u3.macro_statements == pval list of statements in macro body.
		*/
		destroy_pval(item->u2.arglist);
		if (item->u1.str )
			free(item->u1.str);
		destroy_pval(item->u3.macro_statements);
		break;

	case PV_CONTEXT:
		/* fields: item->u1.str     == name of context
		           item->u2.statements == pval list of statements in context body
				   item->u3.abstract == int 1 if an abstract keyword were present
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.statements);
		break;

	case PV_MACRO_CALL:
		/* fields: item->u1.str     == name of macro to call
		           item->u2.arglist == pval list of PV_WORD arguments of macro call, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.arglist);
		break;

	case PV_APPLICATION_CALL:
		/* fields: item->u1.str     == name of application to call
		           item->u2.arglist == pval list of PV_WORD arguments of macro call, as given by user
				   item->u2.arglist->u1.str  == argument
				   item->u2.arglist->next   == next arg
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.arglist);
		break;

	case PV_CASE:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.statements);
		break;

	case PV_PATTERN:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.statements);
		break;

	case PV_DEFAULT:
		/* fields:
		           item->u2.statements == pval list of statements under the case
		*/
		destroy_pval(item->u2.statements);
		break;

	case PV_CATCH:
		/* fields: item->u1.str     == name of extension to catch
		           item->u2.statements == pval list of statements in context body
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.statements);
		break;

	case PV_SWITCHES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		destroy_pval(item->u1.list);
		break;

	case PV_ESWITCHES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		*/
		destroy_pval(item->u1.list);
		break;

	case PV_INCLUDES:
		/* fields: item->u1.list     == pval list of PV_WORD elements, one per entry in the list
		           item->u2.arglist  == pval list of 4 PV_WORD elements for time values
		*/
		destroy_pval(item->u1.list);
		break;

	case PV_STATEMENTBLOCK:
		/* fields: item->u1.list     == pval list of statements in block, one per entry in the list
		*/
		destroy_pval(item->u1.list);
		break;

	case PV_LOCALVARDEC:
	case PV_VARDEC:
		/* fields: item->u1.str     == variable name
		           item->u2.val     == variable value to assign
		*/
		if (item->u1.str)
			free(item->u1.str);
		if (item->u2.val)
			free(item->u2.val);
		break;

	case PV_GOTO:
		/* fields: item->u1.list     == pval list of PV_WORD target names, up to 3, in order as given by user.
		           item->u1.list->u1.str  == where the data on a PV_WORD will always be.
		*/

		destroy_pval(item->u1.list);
		break;

	case PV_LABEL:
		/* fields: item->u1.str     == label name
		*/
		if (item->u1.str)
			free(item->u1.str);
		break;

	case PV_FOR:
		/* fields: item->u1.for_init     == a string containing the initializer
		           item->u2.for_test     == a string containing the loop test
		           item->u3.for_inc      == a string containing the loop increment

				   item->u4.for_statements == a pval list of statements in the for ()
		*/
		if (item->u1.for_init)
			free(item->u1.for_init);
		if (item->u2.for_test)
			free(item->u2.for_test);
		if (item->u3.for_inc)
			free(item->u3.for_inc);
		destroy_pval(item->u4.for_statements);
		break;

	case PV_WHILE:
		/* fields: item->u1.str        == the while conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the while ()
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.statements);
		break;

	case PV_BREAK:
		/* fields: none
		*/
		break;

	case PV_RETURN:
		/* fields: none
		*/
		break;

	case PV_CONTINUE:
		/* fields: none
		*/
		break;

	case PV_IFTIME:
		/* fields: item->u1.list        == the 4 time values, in PV_WORD structs, linked list

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		destroy_pval(item->u1.list);
		destroy_pval(item->u2.statements);
		if (item->u3.else_statements) {
			destroy_pval(item->u3.else_statements);
		}
		break;

	case PV_RANDOM:
		/* fields: item->u1.str        == the random percentage, as supplied by user

				   item->u2.statements == a pval list of statements in the true part ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		fall thru to If */
	case PV_IF:
		/* fields: item->u1.str        == the if conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the if ()
				   item->u3.else_statements == a pval list of statements in the else
											   (could be zero)
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.statements);
		if (item->u3.else_statements) {
			destroy_pval(item->u3.else_statements);
		}
		break;

	case PV_SWITCH:
		/* fields: item->u1.str        == the switch expression

				   item->u2.statements == a pval list of statements in the switch,
				   							(will be case statements, most likely!)
		*/
		if (item->u1.str)
			free(item->u1.str);
		destroy_pval(item->u2.statements);
		break;

	case PV_EXTENSION:
		/* fields: item->u1.str        == the extension name, label, whatever it's called

				   item->u2.statements == a pval list of statements in the extension
				   item->u3.hints      == a char * hint argument
				   item->u4.regexten   == an int boolean. non-zero says that regexten was specified
		*/
		if (item->u1.str)
			free(item->u1.str);
		if (item->u3.hints)
			free(item->u3.hints);
		destroy_pval(item->u2.statements);
		break;

	case PV_IGNOREPAT:
		/* fields: item->u1.str        == the ignorepat data
		*/
		if (item->u1.str)
			free(item->u1.str);
		break;

	case PV_GLOBALS:
		/* fields: item->u1.statements     == pval list of statements, usually vardecs
		*/
		destroy_pval(item->u1.statements);
		break;
	}
	free(item);
}

void destroy_pval(pval *item)
{
	pval *i,*nxt;

	for (i=item; i; i=nxt) {
		nxt = i->next;

		destroy_pval_item(i);
	}
}

#ifdef AAL_ARGCHECK
static char *ael_funclist[] =
{
	"AGENT",
	"ARRAY",
	"BASE64_DECODE",
	"BASE64_ENCODE",
	"CALLERID",
	"CDR",
	"CHANNEL",
	"CHECKSIPDOMAIN",
	"CHECK_MD5",
	"CURL",
	"CUT",
	"DB",
	"DB_EXISTS",
	"DUNDILOOKUP",
	"ENUMLOOKUP",
	"ENV",
	"EVAL",
	"EXISTS",
	"FIELDQTY",
	"FILTER",
	"GROUP",
	"GROUP_COUNT",
	"GROUP_LIST",
	"GROUP_MATCH_COUNT",
	"IAXPEER",
	"IF",
	"IFTIME",
	"ISNULL",
	"KEYPADHASH",
	"LANGUAGE",
	"LEN",
	"MATH",
	"MD5",
	"MUSICCLASS",
	"QUEUEAGENTCOUNT",
	"QUEUE_MEMBER_COUNT",
	"QUEUE_MEMBER_LIST",
	"QUOTE",
	"RAND",
	"REGEX",
	"SET",
	"SHA1",
	"SIPCHANINFO",
	"SIPPEER",
	"SIP_HEADER",
	"SORT",
	"STAT",
	"STRFTIME",
	"STRPTIME",
	"TIMEOUT",
	"TXTCIDNAME",
	"URIDECODE",
	"URIENCODE",
	"VMCOUNT"
};


int ael_is_funcname(char *name)
{
	int s,t;
	t = sizeof(ael_funclist)/sizeof(char*);
	s = 0;
	while ((s < t) && strcasecmp(name, ael_funclist[s]))
		s++;
	if ( s < t )
		return 1;
	else
		return 0;
}
#endif


/* PVAL PI */

/* ----------------- implementation ------------------- */


int  pvalCheckType( pval *p, char *funcname, pvaltype type )
{
	if (p->type != type)
	{
		ast_log(LOG_ERROR, "Func: %s the pval passed is not appropriate for this function!\n", funcname);
		return 0;
	}
	return 1;
}


pval *pvalCreateNode( pvaltype type )
{
	pval *p = calloc(1,sizeof(pval)); /* why, oh why, don't I use ast_calloc? Way, way, way too messy if I do! */
					  /* remember, this can be used externally or internally to asterisk */
	if (p) {
		p->type = type;
	}
	return p;
}

pvaltype pvalObjectGetType( pval *p )
{
	return p->type;
}


void pvalWordSetString( pval *p, char *str)
{
	if (!pvalCheckType(p, "pvalWordSetString", PV_WORD))
		return;
	p->u1.str = str;
}

char *pvalWordGetString( pval *p )
{
	if (!pvalCheckType(p, "pvalWordGetString", PV_WORD))
		return 0;
	return p->u1.str;
}


void pvalMacroSetName( pval *p, char *name)
{
	if (!pvalCheckType(p, "pvalMacroSetName", PV_MACRO))
		return;
	p->u1.str = name;
}

char *pvalMacroGetName( pval *p )
{
	if (!pvalCheckType(p, "pvalMacroGetName", PV_MACRO))
		return 0;
	return p->u1.str;
}

void pvalMacroSetArglist( pval *p, pval *arglist )
{
	if (!pvalCheckType(p, "pvalMacroSetArglist", PV_MACRO))
		return;
	p->u2.arglist = arglist;
}

void pvalMacroAddArg( pval *p, pval *arg ) /* single arg only! */
{
	if (!pvalCheckType(p, "pvalMacroAddArg", PV_MACRO))
		return;
	if (!p->u2.arglist)
		p->u2.arglist = arg;
	else
		linku1(p->u2.arglist, arg);

}

pval *pvalMacroWalkArgs( pval *p, pval **arg )
{
	if (!pvalCheckType(p, "pvalMacroWalkArgs", PV_MACRO))
		return 0;
	if (!(*arg))
		*arg = p->u2.arglist;
	else {
		*arg = (*arg)->next;
	}
	return *arg;
}

void pvalMacroAddStatement( pval *p, pval *statement )
{
	if (!pvalCheckType(p, "pvalMacroAddStatement", PV_MACRO))
		return;
	if (!p->u3.macro_statements)
		p->u3.macro_statements = statement;
	else
		linku1(p->u3.macro_statements, statement);


}

pval *pvalMacroWalkStatements( pval *p, pval **next_statement )
{
	if (!pvalCheckType(p, "pvalMacroWalkStatements", PV_MACRO))
		return 0;
	if (!(*next_statement))
		*next_statement = p->u3.macro_statements;
	else {
		*next_statement = (*next_statement)->next;
	}
	return *next_statement;
}



void pvalContextSetName( pval *p, char *name)
{
	if (!pvalCheckType(p, "pvalContextSetName", PV_CONTEXT))
		return;
	p->u1.str = name;
}

char *pvalContextGetName( pval *p )
{
	if (!pvalCheckType(p, "pvalContextGetName", PV_CONTEXT))
		return 0;
	return p->u1.str;
}

void pvalContextSetAbstract( pval *p )
{
	if (!pvalCheckType(p, "pvalContextSetAbstract", PV_CONTEXT))
		return;
	p->u3.abstract = 1;
}

void pvalContextUnsetAbstract( pval *p )
{
	if (!pvalCheckType(p, "pvalContextUnsetAbstract", PV_CONTEXT))
		return;
	p->u3.abstract = 0;
}

int  pvalContextGetAbstract( pval *p )
{
	if (!pvalCheckType(p, "pvalContextGetAbstract", PV_CONTEXT))
		return 0;
	return p->u3.abstract;
}



void pvalContextAddStatement( pval *p, pval *statement) /* this includes SWITCHES, INCLUDES, IGNOREPAT, etc */
{
	if (!pvalCheckType(p, "pvalContextAddStatement", PV_CONTEXT))
		return;
	if (!p->u2.statements)
		p->u2.statements = statement;
	else
		linku1(p->u2.statements, statement);
}

pval *pvalContextWalkStatements( pval *p, pval **statements )
{
	if (!pvalCheckType(p, "pvalContextWalkStatements", PV_CONTEXT))
		return 0;
	if (!(*statements))
		*statements = p->u2.statements;
	else {
		*statements = (*statements)->next;
	}
	return *statements;
}


void pvalMacroCallSetMacroName( pval *p, char *name )
{
	if (!pvalCheckType(p, "pvalMacroCallSetMacroName", PV_MACRO_CALL))
		return;
	p->u1.str = name;
}

char* pvalMacroCallGetMacroName( pval *p )
{
	if (!pvalCheckType(p, "pvalMacroCallGetMacroName", PV_MACRO_CALL))
		return 0;
	return p->u1.str;
}

void pvalMacroCallSetArglist( pval *p, pval *arglist )
{
	if (!pvalCheckType(p, "pvalMacroCallSetArglist", PV_MACRO_CALL))
		return;
	p->u2.arglist = arglist;
}

void pvalMacroCallAddArg( pval *p, pval *arg )
{
	if (!pvalCheckType(p, "pvalMacroCallGetAddArg", PV_MACRO_CALL))
		return;
	if (!p->u2.arglist)
		p->u2.arglist = arg;
	else
		linku1(p->u2.arglist, arg);
}

pval *pvalMacroCallWalkArgs( pval *p, pval **args )
{
	if (!pvalCheckType(p, "pvalMacroCallWalkArgs", PV_MACRO_CALL))
		return 0;
	if (!(*args))
		*args = p->u2.arglist;
	else {
		*args = (*args)->next;
	}
	return *args;
}


void pvalAppCallSetAppName( pval *p, char *name )
{
	if (!pvalCheckType(p, "pvalAppCallSetAppName", PV_APPLICATION_CALL))
		return;
	p->u1.str = name;
}

char* pvalAppCallGetAppName( pval *p )
{
	if (!pvalCheckType(p, "pvalAppCallGetAppName", PV_APPLICATION_CALL))
		return 0;
	return p->u1.str;
}

void pvalAppCallSetArglist( pval *p, pval *arglist )
{
	if (!pvalCheckType(p, "pvalAppCallSetArglist", PV_APPLICATION_CALL))
		return;
	p->u2.arglist = arglist;
}

void pvalAppCallAddArg( pval *p, pval *arg )
{
	if (!pvalCheckType(p, "pvalAppCallAddArg", PV_APPLICATION_CALL))
		return;
	if (!p->u2.arglist)
		p->u2.arglist = arg;
	else
		linku1(p->u2.arglist, arg);
}

pval *pvalAppCallWalkArgs( pval *p, pval **args )
{
	if (!pvalCheckType(p, "pvalAppCallWalkArgs", PV_APPLICATION_CALL))
		return 0;
	if (!(*args))
		*args = p->u2.arglist;
	else {
		*args = (*args)->next;
	}
	return *args;
}


void pvalCasePatSetVal( pval *p, char *val )
{
	if (!pvalCheckType(p, "pvalAppCallWalkArgs", PV_APPLICATION_CALL))
		return;
	p->u1.str = val;
}

char* pvalCasePatGetVal( pval *p )
{
	return p->u1.str;
}

void pvalCasePatDefAddStatement( pval *p, pval *statement )
{
	if (!p->u2.arglist)
		p->u2.statements = statement;
	else
		linku1(p->u2.statements, statement);
}

pval *pvalCasePatDefWalkStatements( pval *p, pval **statement )
{
	if (!(*statement))
		*statement = p->u2.statements;
	else {
		*statement = (*statement)->next;
	}
	return *statement;
}


void pvalCatchSetExtName( pval *p, char *name )
{
	if (!pvalCheckType(p, "pvalCatchSetExtName", PV_CATCH))
		return;
	p->u1.str = name;
}

char* pvalCatchGetExtName( pval *p )
{
	if (!pvalCheckType(p, "pvalCatchGetExtName", PV_CATCH))
		return 0;
	return p->u1.str;
}

void pvalCatchSetStatement( pval *p, pval *statement )
{
	if (!pvalCheckType(p, "pvalCatchSetStatement", PV_CATCH))
		return;
	p->u2.statements = statement;
}

pval *pvalCatchGetStatement( pval *p )
{
	if (!pvalCheckType(p, "pvalCatchGetStatement", PV_CATCH))
		return 0;
	return p->u2.statements;
}


void pvalSwitchesAddSwitch( pval *p, char *name )
{
	pval *s;
	if (!pvalCheckType(p, "pvalSwitchesAddSwitch", PV_SWITCHES))
		return;
	s = pvalCreateNode(PV_WORD);
	s->u1.str = name;
	p->u1.list = linku1(p->u1.list, s);
}

char* pvalSwitchesWalkNames( pval *p, pval **next_item )
{
	if (!pvalCheckType(p, "pvalSwitchesWalkNames", PV_SWITCHES))
		return 0;
	if (!(*next_item))
		*next_item = p->u1.list;
	else {
		*next_item = (*next_item)->next;
	}
	return (*next_item)->u1.str;
}

void pvalESwitchesAddSwitch( pval *p, char *name )
{
	pval *s;
	if (!pvalCheckType(p, "pvalESwitchesAddSwitch", PV_ESWITCHES))
		return;
	s = pvalCreateNode(PV_WORD);
	s->u1.str = name;
	p->u1.list = linku1(p->u1.list, s);
}

char* pvalESwitchesWalkNames( pval *p, pval **next_item )
{
	if (!pvalCheckType(p, "pvalESwitchesWalkNames", PV_ESWITCHES))
		return 0;
	if (!(*next_item))
		*next_item = p->u1.list;
	else {
		*next_item = (*next_item)->next;
	}
	return (*next_item)->u1.str;
}


void pvalIncludesAddInclude( pval *p, const char *include )
{
	pval *s;
	if (!pvalCheckType(p, "pvalIncludesAddSwitch", PV_INCLUDES))
		return;
	s = pvalCreateNode(PV_WORD);
	s->u1.str = (char *)include;
	p->u1.list = linku1(p->u1.list, s);
}
 /* an include is a WORD with string set to path */

void pvalIncludesAddIncludeWithTimeConstraints( pval *p, const char *include, char *hour_range, char *dom_range, char *dow_range, char *month_range )
{
	pval *hr;
	pval *dom;
	pval *dow;
	pval *mon;
	pval *s;

	if (!pvalCheckType(p, "pvalIncludeAddIncludeWithTimeConstraints", PV_INCLUDES)) {
		return;
	}

	hr = pvalCreateNode(PV_WORD);
	dom = pvalCreateNode(PV_WORD);
	dow = pvalCreateNode(PV_WORD);
	mon = pvalCreateNode(PV_WORD);
	s = pvalCreateNode(PV_WORD);

	if (!hr || !dom || !dow || !mon || !s) {
		destroy_pval(hr);
		destroy_pval(dom);
		destroy_pval(dow);
		destroy_pval(mon);
		destroy_pval(s);
		return;
	}

	s->u1.str = (char *)include;
	p->u1.list = linku1(p->u1.list, s);

	hr->u1.str = hour_range;
	dom->u1.str = dom_range;
	dow->u1.str = dow_range;
	mon->u1.str = month_range;

	s->u2.arglist = hr;

	hr->next = dom;
	dom->next = dow;
	dow->next = mon;
	mon->next = 0;
}
 /* is this right??? come back and correct it */ /*the ptr is to the WORD */
void pvalIncludeGetTimeConstraints( pval *p, char **hour_range, char **dom_range, char **dow_range, char **month_range )
{
	if (!pvalCheckType(p, "pvalIncludeGetTimeConstraints", PV_WORD))
		return;
	if (p->u2.arglist) {
		*hour_range = p->u2.arglist->u1.str;
		*dom_range = p->u2.arglist->next->u1.str;
		*dow_range = p->u2.arglist->next->next->u1.str;
		*month_range = p->u2.arglist->next->next->next->u1.str;
	} else {
		*hour_range = 0;
		*dom_range = 0;
		*dow_range = 0;
		*month_range = 0;
	}
}
 /* is this right??? come back and correct it */ /*the ptr is to the WORD */
char* pvalIncludesWalk( pval *p, pval **next_item )
{
	if (!pvalCheckType(p, "pvalIncludesWalk", PV_INCLUDES))
		return 0;
	if (!(*next_item))
		*next_item = p->u1.list;
	else {
		*next_item = (*next_item)->next;
	}
	return (*next_item)->u1.str;
}


void pvalStatementBlockAddStatement( pval *p, pval *statement)
{
	if (!pvalCheckType(p, "pvalStatementBlockAddStatement", PV_STATEMENTBLOCK))
		return;
	p->u1.list = linku1(p->u1.list, statement);
}

pval *pvalStatementBlockWalkStatements( pval *p, pval **next_statement)
{
	if (!pvalCheckType(p, "pvalStatementBlockWalkStatements", PV_STATEMENTBLOCK))
		return 0;
	if (!(*next_statement))
		*next_statement = p->u1.list;
	else {
		*next_statement = (*next_statement)->next;
	}
	return *next_statement;
}

void pvalVarDecSetVarname( pval *p, char *name )
{
	if (!pvalCheckType(p, "pvalVarDecSetVarname", PV_VARDEC))
		return;
	p->u1.str = name;
}

void pvalVarDecSetValue( pval *p, char *value )
{
	if (!pvalCheckType(p, "pvalVarDecSetValue", PV_VARDEC))
		return;
	p->u2.val = value;
}

char* pvalVarDecGetVarname( pval *p )
{
	if (!pvalCheckType(p, "pvalVarDecGetVarname", PV_VARDEC))
		return 0;
	return p->u1.str;
}

char* pvalVarDecGetValue( pval *p )
{
	if (!pvalCheckType(p, "pvalVarDecGetValue", PV_VARDEC))
		return 0;
	return p->u2.val;
}

void pvalGotoSetTarget( pval *p, char *context, char *exten, char *label )
{
	pval *con, *ext, *pri;

	if (!pvalCheckType(p, "pvalGotoSetTarget", PV_GOTO))
		return;
	if (context && strlen(context)) {
		con = pvalCreateNode(PV_WORD);
		ext = pvalCreateNode(PV_WORD);
		pri = pvalCreateNode(PV_WORD);

		con->u1.str = context;
		ext->u1.str = exten;
		pri->u1.str = label;

		con->next = ext;
		ext->next = pri;
		p->u1.list = con;
	} else if (exten && strlen(exten)) {
		ext = pvalCreateNode(PV_WORD);
		pri = pvalCreateNode(PV_WORD);

		ext->u1.str = exten;
		pri->u1.str = label;

		ext->next = pri;
		p->u1.list = ext;
	} else {
		pri = pvalCreateNode(PV_WORD);

		pri->u1.str = label;

		p->u1.list = pri;
	}
}

void pvalGotoGetTarget( pval *p, char **context, char **exten, char **label )
{
	if (!pvalCheckType(p, "pvalGotoGetTarget", PV_GOTO))
		return;
	if (p->u1.list && p->u1.list->next && p->u1.list->next->next) {
		*context = p->u1.list->u1.str;
		*exten = p->u1.list->next->u1.str;
		*label = p->u1.list->next->next->u1.str;

	} else if (p->u1.list && p->u1.list->next ) {
		*exten = p->u1.list->u1.str;
		*label = p->u1.list->next->u1.str;
		*context = 0;

	} else if (p->u1.list) {
		*label = p->u1.list->u1.str;
		*context = 0;
		*exten = 0;

	} else {
		*context = 0;
		*exten = 0;
		*label = 0;
	}
}


void pvalLabelSetName( pval *p, char *name )
{
	if (!pvalCheckType(p, "pvalLabelSetName", PV_LABEL))
		return;
	p->u1.str = name;
}

char* pvalLabelGetName( pval *p )
{
	if (!pvalCheckType(p, "pvalLabelGetName", PV_LABEL))
		return 0;
	return p->u1.str;
}


void pvalForSetInit( pval *p, char *init )
{
	if (!pvalCheckType(p, "pvalForSetInit", PV_FOR))
		return;
	p->u1.for_init = init;
}

void pvalForSetTest( pval *p, char *test )
{
	if (!pvalCheckType(p, "pvalForSetTest", PV_FOR))
		return;
	p->u2.for_test = test;
}

void pvalForSetInc( pval *p, char *inc )
{
	if (!pvalCheckType(p, "pvalForSetInc", PV_FOR))
		return;
	p->u3.for_inc = inc;
}

void pvalForSetStatement( pval *p, pval *statement )
{
	if (!pvalCheckType(p, "pvalForSetStatement", PV_FOR))
		return;
	p->u4.for_statements = statement;
}

char* pvalForGetInit( pval *p )
{
	if (!pvalCheckType(p, "pvalForGetInit", PV_FOR))
		return 0;
	return p->u1.for_init;
}

char* pvalForGetTest( pval *p )
{
	if (!pvalCheckType(p, "pvalForGetTest", PV_FOR))
		return 0;
	return p->u2.for_test;
}

char* pvalForGetInc( pval *p )
{
	if (!pvalCheckType(p, "pvalForGetInc", PV_FOR))
		return 0;
	return p->u3.for_inc;
}

pval* pvalForGetStatement( pval *p )
{
	if (!pvalCheckType(p, "pvalForGetStatement", PV_FOR))
		return 0;
	return p->u4.for_statements;
}



void pvalIfSetCondition( pval *p, char *expr )
{
	if (!pvalCheckType(p, "pvalIfSetCondition", PV_IF))
		return;
	p->u1.str = expr;
}

char* pvalIfGetCondition( pval *p )
{
	if (!pvalCheckType(p, "pvalIfGetCondition", PV_IFTIME))
		return 0;
	return p->u1.str;
}

void pvalIfTimeSetCondition( pval *p, char *hour_range, char *dow_range, char *dom_range, char *mon_range )  /* time range format: 24-hour format begin-end|dow range|dom range|month range */
{
	pval *hr;
	pval *dow;
	pval *dom;
	pval *mon;

	if (!pvalCheckType(p, "pvalIfTimeSetCondition", PV_IFTIME)) {
		return;
	}

	hr = pvalCreateNode(PV_WORD);
	dow = pvalCreateNode(PV_WORD);
	dom = pvalCreateNode(PV_WORD);
	mon = pvalCreateNode(PV_WORD);

	if (!hr || !dom || !dow || !mon) {
		destroy_pval(hr);
		destroy_pval(dom);
		destroy_pval(dow);
		destroy_pval(mon);
		return;
	}

	pvalWordSetString(hr, hour_range);
	pvalWordSetString(dow, dow_range);
	pvalWordSetString(dom, dom_range);
	pvalWordSetString(mon, mon_range);
	dom->next = mon;
	dow->next = dom;
	hr->next = dow;
	p->u1.list = hr;
}

 /* is this right??? come back and correct it */
void pvalIfTimeGetCondition( pval *p, char **hour_range, char **dow_range, char **dom_range, char **month_range )
{
	if (!pvalCheckType(p, "pvalIfTimeGetCondition", PV_IFTIME))
		return;
	*hour_range = p->u1.list->u1.str;
	*dow_range = p->u1.list->next->u1.str;
	*dom_range = p->u1.list->next->next->u1.str;
	*month_range = p->u1.list->next->next->next->u1.str;
}

void pvalRandomSetCondition( pval *p, char *percent )
{
	if (!pvalCheckType(p, "pvalRandomSetCondition", PV_RANDOM))
		return;
	p->u1.str = percent;
}

char* pvalRandomGetCondition( pval *p )
{
	if (!pvalCheckType(p, "pvalRandomGetCondition", PV_RANDOM))
		return 0;
	return p->u1.str;
}

void pvalConditionalSetThenStatement( pval *p, pval *statement )
{
	p->u2.statements = statement;
}

void pvalConditionalSetElseStatement( pval *p, pval *statement )
{
	p->u3.else_statements = statement;
}

pval* pvalConditionalGetThenStatement( pval *p )
{
	return p->u2.statements;
}

pval* pvalConditionalGetElseStatement( pval *p )
{
	return p->u3.else_statements;
}

void pvalSwitchSetTestexpr( pval *p, char *expr )
{
	if (!pvalCheckType(p, "pvalSwitchSetTestexpr", PV_SWITCH))
		return;
	p->u1.str = expr;
}

char* pvalSwitchGetTestexpr( pval *p )
{
	if (!pvalCheckType(p, "pvalSwitchGetTestexpr", PV_SWITCH))
		return 0;
	return p->u1.str;
}

void pvalSwitchAddCase( pval *p, pval *Case )
{
	if (!pvalCheckType(p, "pvalSwitchAddCase", PV_SWITCH))
		return;
	if (!pvalCheckType(Case, "pvalSwitchAddCase", PV_CASE))
		return;
	if (!p->u2.statements)
		p->u2.statements = Case;
	else
		linku1(p->u2.statements, Case);
}

pval* pvalSwitchWalkCases( pval *p, pval **next_case )
{
	if (!pvalCheckType(p, "pvalSwitchWalkCases", PV_SWITCH))
		return 0;
	if (!(*next_case))
		*next_case = p->u2.statements;
	else {
		*next_case = (*next_case)->next;
	}
	return *next_case;
}


void pvalExtenSetName( pval *p, char *name )
{
	if (!pvalCheckType(p, "pvalExtenSetName", PV_EXTENSION))
		return;
	p->u1.str = name;
}

char* pvalExtenGetName( pval *p )
{
	if (!pvalCheckType(p, "pvalExtenGetName", PV_EXTENSION))
		return 0;
	return p->u1.str;
}

void pvalExtenSetRegexten( pval *p )
{
	if (!pvalCheckType(p, "pvalExtenSetRegexten", PV_EXTENSION))
		return;
	p->u4.regexten = 1;
}

void pvalExtenUnSetRegexten( pval *p )
{
	if (!pvalCheckType(p, "pvalExtenUnSetRegexten", PV_EXTENSION))
		return;
	p->u4.regexten = 0;
}

int pvalExtenGetRegexten( pval *p )
{
	if (!pvalCheckType(p, "pvalExtenGetRegexten", PV_EXTENSION))
		return 0;
	return p->u4.regexten;
}

void pvalExtenSetHints( pval *p, char *hints )
{
	if (!pvalCheckType(p, "pvalExtenSetHints", PV_EXTENSION))
		return;
	p->u3.hints = hints;
}

char* pvalExtenGetHints( pval *p )
{
	if (!pvalCheckType(p, "pvalExtenGetHints", PV_EXTENSION))
		return 0;
	return p->u3.hints;
}

void pvalExtenSetStatement( pval *p, pval *statement )
{
	if (!pvalCheckType(p, "pvalExtenSetStatement", PV_EXTENSION))
		return;
	p->u2.statements = statement;
}

pval* pvalExtenGetStatement( pval *p )
{
	if (!pvalCheckType(p, "pvalExtenGetStatement", PV_EXTENSION))
		return 0;
	return p->u2.statements;
}


void pvalIgnorePatSetPattern( pval *p, char *pat )
{
	if (!pvalCheckType(p, "pvalIgnorePatSetPattern", PV_IGNOREPAT))
		return;
	p->u1.str = pat;
}

char* pvalIgnorePatGetPattern( pval *p )
{
	if (!pvalCheckType(p, "pvalIgnorePatGetPattern", PV_IGNOREPAT))
		return 0;
	return p->u1.str;
}


void pvalGlobalsAddStatement( pval *p, pval *statement )
{
	if (p->type != PV_GLOBALS) {
		ast_log(LOG_ERROR, "pvalGlobalsAddStatement called where first arg is not a Globals!\n");
	} else {
		if (!p->u1.statements) {
			p->u1.statements = statement;
		} else {
			p->u1.statements = linku1(p->u1.statements,statement);
		}
	}
}

pval* pvalGlobalsWalkStatements( pval *p, pval **next_statement )
{
	if (!pvalCheckType(p, "pvalGlobalsWalkStatements", PV_GLOBALS))
		return 0;
	if (!*next_statement) {
		*next_statement = p;
		return p;
	} else {
		*next_statement = (*next_statement)->next;
		return (*next_statement)->next;
	}
}


void pvalTopLevAddObject( pval *p, pval *contextOrObj )
{
	if (p) {
		linku1(p,contextOrObj);
	} else {
		ast_log(LOG_ERROR, "First arg to pvalTopLevel is NULL!\n");
	}
}

pval *pvalTopLevWalkObjects(pval *p, pval **next_obj )
{
	if (!*next_obj) {
		*next_obj = p;
		return p;
	} else {
		*next_obj = (*next_obj)->next;
		return (*next_obj)->next;
	}
}

/* append second element to the list in the first one via next pointers */
pval * linku1(pval *head, pval *tail)
{
	if (!head)
		return tail;
	if (tail) {
		if (!head->next) {
			head->next = tail;
		} else {
			head->u1_last->next = tail;
		}
		head->u1_last = tail;
		tail->prev = head; /* the dad link only points to containers */
	}
	return head;
}
