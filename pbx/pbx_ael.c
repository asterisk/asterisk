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
	<depend>res_ael_share</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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
#include "asterisk/callerid.h"
#include "asterisk/ael_structs.h"
#include "asterisk/pval.h"
#ifdef AAL_ARGCHECK
#include "asterisk/argdesc.h"
#endif

/* these functions are in ../ast_expr2.fl */

#define DEBUG_READ   (1 << 0)
#define DEBUG_TOKENS (1 << 1)
#define DEBUG_MACROS (1 << 2)
#define DEBUG_CONTEXTS (1 << 3)

static char *config = "extensions.ael";
static char *registrar = "pbx_ael";
static int pbx_load_module(void);
static int warns, errs;
static struct pval *current_db;

#ifndef AAL_ARGCHECK
/* for the time being, short circuit all the AAL related structures
   without permanently removing the code; after/during the AAL 
   development, this code can be properly re-instated 
*/

#endif

#ifdef AAL_ARGCHECK
int option_matches_j( struct argdesc *should, pval *is, struct argapp *app);
int option_matches( struct argdesc *should, pval *is, struct argapp *app);
int ael_is_funcname(char *name);
#endif

int check_app_args(pval *appcall, pval *arglist, struct argapp *app);
void check_pval(pval *item, struct argapp *apps, int in_globals);
void check_pval_item(pval *item, struct argapp *apps, int in_globals);
void check_switch_expr(pval *item, struct argapp *apps);
void ast_expr_register_extra_error_info(char *errmsg);
void ast_expr_clear_extra_error_info(void);
struct pval *find_macro(char *name);
struct pval *find_context(char *name);
struct pval *find_context(char *name);
struct pval *find_macro(char *name);
struct ael_priority *new_prio(void);
struct ael_extension *new_exten(void);
void linkprio(struct ael_extension *exten, struct ael_priority *prio);
void destroy_extensions(struct ael_extension *exten);
void set_priorities(struct ael_extension *exten);
void add_extensions(struct ael_extension *exten);
void ast_compile_ael2(struct ast_context **local_contexts, struct pval *root);
void destroy_pval(pval *item);
void destroy_pval_item(pval *item);
int is_float(char *arg );
int is_int(char *arg );
int is_empty(char *arg);

static const char *match_context;
static const char *match_exten;
static const char *match_label;
static int count_labels; /* true, put matcher in label counting mode */
static int return_on_context_match;
struct pval *match_pval(pval *item);
static void check_goto(pval *item);
static void find_pval_goto_item(pval *item, int lev);
static void find_pval_gotos(pval *item, int lev);

static struct pval *find_label_in_current_context(char *exten, char *label, pval *curr_cont);
static struct pval *find_first_label_in_current_context(char *label, pval *curr_cont);
static void print_pval_list(FILE *fin, pval *item, int depth);

static struct pval *find_label_in_current_extension(const char *label, pval *curr_ext);
static struct pval *find_label_in_current_db(const char *context, const char *exten, const char *label);
static pval *get_extension_or_contxt(pval *p);
static pval *get_contxt(pval *p);
/* static void substitute_commas(char *str); */
#ifdef NOMORE
/*! \brief I am adding this code to substitute commas with vertbars in the args to apps */
static void substitute_commas(char *str)
{
	char *p = str;
	
	while (p && *p)
	{
		if (*p == ',' && ((p != str && *(p-1) != '\\')
				|| p == str))
			*p = '|';
		if (*p == '\\' && *(p+1) == ',') { /* learning experience: the '\,' is turned into just ',' by pbx_config; So we need to do the same */
			char *q = p;
			while (*q) {  /* move the ',' and everything after it up 1 char */
				*q = *(q+1);
				q++;
			}
		}
		p++;
	}
}
#endif

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
			if ( lp->u2.arglist )
				fprintf(fin,"|%s|%s|%s|%s", 
						lp->u2.arglist->u1.str,
						lp->u2.arglist->next->u1.str,
						lp->u2.arglist->next->next->u1.str,
						lp->u2.arglist->next->next->next->u1.str
					);
			fprintf(fin,";\n"); /* usually, words are encapsulated in something else */
		}
		
		print_pval_list(fin,item->u1.list,depth+1);
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
		fprintf(fin,"};\n");
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
		
		fprintf(fin,"%s => \n", item->u1.str);
		print_pval_list(fin,item->u2.statements,depth+1);
		break;
			
	case PV_IGNOREPAT:
		fprintf(fin,"ignorepat => %s\n", item->u1.str);
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

#if 0
static void ael2_print(char *fname, pval *tree)
{
	FILE *fin = fopen(fname,"w");
	if ( !fin ) {
		ast_log(LOG_ERROR, "Couldn't open %s for writing.\n", fname);
		return;
	}
	print_pval_list(fin, tree, 0);
	fclose(fin);
}
#endif



/* SEMANTIC CHECKING FOR AEL:  ============================================================================= */

/*   (not all that is syntactically legal is good! */


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


static void check_goto(pval *item)
{
	/* check for the target of the goto-- does it exist? */
	if ( !(item->u1.list)->next && !(item->u1.list)->u1.str ) {
		ast_log(LOG_ERROR,"Error: file %s, line %d-%d: goto:  empty label reference found!\n",
				item->filename, item->startline, item->endline);
		errs++;
	}
	
	/* just one item-- the label should be in the current extension */
	
	if (item->u1.list && !item->u1.list->next && !strstr((item->u1.list)->u1.str,"${")) {
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
				ast_log(LOG_ERROR,"Error: file %s, line %d-%d: goto:  no label %s|%s exists in the current context, or any of its inclusions!\n",
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
					ast_log(LOG_WARNING,"Warning: file %s, line %d-%d: goto:  no context %s could be found that matches the goto target!\n",
							item->filename, item->startline, item->endline, item->u1.list->u1.str);
					warns++; /* this is just a warning, because this context could be in extensions.conf or somewhere */
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
		ast_log(LOG_ERROR,"find_pval_goto in infinite loop!\n\n");
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
			
		/* printf("Descending into matching macro %s\n", match_context); */
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
		find_pval_gotos(item->u2.statements,lev+1);
		break;
			
	case PV_PATTERN:
		/* fields: item->u1.str     == value of case
		           item->u2.statements == pval list of statements under the case
		*/
		find_pval_gotos(item->u2.statements,lev+1);
		break;
			
	case PV_DEFAULT:
		/* fields: 
		           item->u2.statements == pval list of statements under the case
		*/
		find_pval_gotos(item->u2.statements,lev+1);
		break;
			
	case PV_CATCH:
		/* fields: item->u1.str     == name of extension to catch
		           item->u2.statements == pval list of statements in context body
		*/
		find_pval_gotos(item->u2.statements,lev+1);
		break;
			
	case PV_STATEMENTBLOCK:
		/* fields: item->u1.list     == pval list of statements in block, one per entry in the list
		*/
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
			if (that_context) {
				find_pval_gotos(that_context,lev+1); /* keep working up the includes */
			}
		}
		break;
		
	case PV_FOR:
		/* fields: item->u1.for_init     == a string containing the initalizer
		           item->u2.for_test     == a string containing the loop test
		           item->u3.for_inc      == a string containing the loop increment

				   item->u4.for_statements == a pval list of statements in the for ()
		*/
		find_pval_gotos(item->u4.for_statements,lev+1);
		break;
			
	case PV_WHILE:
		/* fields: item->u1.str        == the while conditional, as supplied by user

				   item->u2.statements == a pval list of statements in the while ()
		*/
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
		find_pval_gotos(item->u2.statements,lev+1);

		if (item->u3.else_statements) {
			find_pval_gotos(item->u3.else_statements,lev+1);
		}
		break;
			
	case PV_SWITCH:
		/* fields: item->u1.str        == the switch expression

				   item->u2.statements == a pval list of statements in the switch, 
				   							(will be case statements, most likely!)
		*/
		find_pval_gotos(item->u3.else_statements,lev+1);
		break;
			
	case PV_EXTENSION:
		/* fields: item->u1.str        == the extension name, label, whatever it's called

				   item->u2.statements == a pval list of statements in the extension
				   item->u3.hints      == a char * hint argument
				   item->u4.regexten   == an int boolean. non-zero says that regexten was specified
		*/

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
		
		find_pval_goto_item(i, lev);
	}
}



struct pval *find_first_label_in_current_context(char *label, pval *curr_cont)
{
	/* printf("  --- Got args %s, %s\n", exten, label); */
	struct pval *ret;
	struct pval *p3;
	struct pval *startpt = ((curr_cont->type==PV_MACRO)?curr_cont->u3.macro_statements: curr_cont->u2.statements);
	
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
	for (p3=startpt; p3; p3=p3->next) {
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
	struct pval *startpt;
	
	count_labels = 0;
	return_on_context_match = 0;
	match_context = "*";
	match_exten = exten;
	match_label = label;
	if (curr_cont->type == PV_MACRO)
		startpt = curr_cont->u3.macro_statements;
	else
		startpt = curr_cont->u2.statements;

	ret =  match_pval(startpt);
	if (ret)
		return ret;
					
	/* the target of the goto could be in an included context!! Fancy that!! */
	/* look for includes in the current context */
	for (p3=startpt; p3; p3=p3->next) {
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



/* =============================================================================================== */
/* "CODE" GENERATOR -- Convert the AEL representation to asterisk extension language */
/* =============================================================================================== */

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

static int aeldebug = 0;

/* interface stuff */

/* if all the below are static, who cares if they are present? */

static int pbx_load_module(void)
{
	int errs, sem_err, sem_warn, sem_note;
	char *rfilename;
	struct ast_context *local_contexts=NULL, *con;
	struct pval *parse_tree;

	ast_log(LOG_NOTICE, "Starting AEL load process.\n");
	if (config[0] == '/')
		rfilename = (char *)config;
	else {
		rfilename = alloca(strlen(config) + strlen(ast_config_AST_CONFIG_DIR) + 2);
		sprintf(rfilename, "%s/%s", ast_config_AST_CONFIG_DIR, config);
	}
	ast_log(LOG_NOTICE, "AEL load process: calculated config file name '%s'.\n", rfilename);

	if (access(rfilename,R_OK) != 0) {
		ast_log(LOG_NOTICE, "File %s not found; AEL declining load\n", rfilename);
		return AST_MODULE_LOAD_DECLINE;
	}
	
	parse_tree = ael2_parse(rfilename, &errs);
	ast_log(LOG_NOTICE, "AEL load process: parsed config file name '%s'.\n", rfilename);
	ael2_semantic_check(parse_tree, &sem_err, &sem_warn, &sem_note);
	if (errs == 0 && sem_err == 0) {
		ast_log(LOG_NOTICE, "AEL load process: checked config file name '%s'.\n", rfilename);
		ast_compile_ael2(&local_contexts, parse_tree);
		ast_log(LOG_NOTICE, "AEL load process: compiled config file name '%s'.\n", rfilename);
		
		ast_merge_contexts_and_delete(&local_contexts, registrar);
		ast_log(LOG_NOTICE, "AEL load process: merged config file name '%s'.\n", rfilename);
		for (con = ast_walk_contexts(NULL); con; con = ast_walk_contexts(con))
			ast_context_verify_includes(con);
		ast_log(LOG_NOTICE, "AEL load process: verified config file name '%s'.\n", rfilename);
	} else {
		ast_log(LOG_ERROR, "Sorry, but %d syntax errors and %d semantic errors were detected. It doesn't make sense to compile.\n", errs, sem_err);
		destroy_pval(parse_tree); /* free up the memory */
		return AST_MODULE_LOAD_DECLINE;
	}
	destroy_pval(parse_tree); /* free up the memory */
	
	return AST_MODULE_LOAD_SUCCESS;
}

/* CLI interface */
static int ael2_debug_read(int fd, int argc, char *argv[])
{
	aeldebug |= DEBUG_READ;
	return 0;
}

static int ael2_debug_tokens(int fd, int argc, char *argv[])
{
	aeldebug |= DEBUG_TOKENS;
	return 0;
}

static int ael2_debug_macros(int fd, int argc, char *argv[])
{
	aeldebug |= DEBUG_MACROS;
	return 0;
}

static int ael2_debug_contexts(int fd, int argc, char *argv[])
{
	aeldebug |= DEBUG_CONTEXTS;
	return 0;
}

static int ael2_no_debug(int fd, int argc, char *argv[])
{
	aeldebug = 0;
	return 0;
}

static int ael2_reload(int fd, int argc, char *argv[])
{
	return (pbx_load_module());
}

static struct ast_cli_entry cli_ael_no_debug = {
	{ "ael", "no", "debug", NULL },
	ael2_no_debug, NULL,
	NULL };

static struct ast_cli_entry cli_ael[] = {
	{ { "ael", "reload", NULL },
	ael2_reload, "Reload AEL configuration" },

	{ { "ael", "debug", "read", NULL },
	ael2_debug_read, "Enable AEL read debug (does nothing)" },

	{ { "ael", "debug", "tokens", NULL },
	ael2_debug_tokens, "Enable AEL tokens debug (does nothing)" },

	{ { "ael", "debug", "macros", NULL },
	ael2_debug_macros, "Enable AEL macros debug (does nothing)" },

	{ { "ael", "debug", "contexts", NULL },
	ael2_debug_contexts, "Enable AEL contexts debug (does nothing)" },

	{ { "ael", "nodebug", NULL },
	ael2_no_debug, "Disable AEL debug messages",
	NULL, NULL, &cli_ael_no_debug },
};

static int unload_module(void)
{
	ast_context_destroy(NULL, registrar);
	ast_cli_unregister_multiple(cli_ael, sizeof(cli_ael) / sizeof(struct ast_cli_entry));
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_ael, sizeof(cli_ael) / sizeof(struct ast_cli_entry));
	return (pbx_load_module());
}

static int reload(void)
{
	return pbx_load_module();
}

#ifdef STANDALONE_AEL
#define AST_MODULE "ael"
int ael_external_load_module(void);
int ael_external_load_module(void)
{
        pbx_load_module();
        return 1;
}
#endif

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk Extension Language Compiler",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );

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
