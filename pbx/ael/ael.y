%{
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
 * \brief Bison Grammar description of AEL2.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asterisk/logger.h"
#include "asterisk/utils.h"		/* ast_calloc() */
#include "asterisk/ael_structs.h"

static pval * linku1(pval *head, pval *tail);

void reset_parencount(yyscan_t yyscanner);
void reset_semicount(yyscan_t yyscanner);
void reset_argcount(yyscan_t yyscanner );

#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner
#define YYERROR_VERBOSE 1

extern char *my_file;
#ifdef AAL_ARGCHECK
int ael_is_funcname(char *name);
#endif
static char *ael_token_subst(char *mess);

%}


%union {
	int	intval;		/* integer value, typically flags */
	char	*str;		/* strings */
	struct pval *pval;	/* full objects */
}

%{
	/* declaring these AFTER the union makes things a lot simpler! */
void yyerror(YYLTYPE *locp, struct parse_io *parseio, char const *s);
int ael_yylex (YYSTYPE * yylval_param, YYLTYPE * yylloc_param , void * yyscanner);

/* create a new object with start-end marker */
static pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column);

/* create a new object with start-end marker, simplified interface.
 * Must be declared here because YYLTYPE is not known before
 */
static pval *npval2(pvaltype type, YYLTYPE *first, YYLTYPE *last);

/* another frontend for npval, this time for a string */
static pval *nword(char *string, YYLTYPE *pos);

/* update end position of an object, return the object */
static pval *update_last(pval *, YYLTYPE *);
%}


%token KW_CONTEXT LC RC LP RP SEMI EQ COMMA COLON AMPER BAR AT
%token KW_MACRO KW_GLOBALS KW_IGNOREPAT KW_SWITCH KW_IF KW_IFTIME KW_ELSE KW_RANDOM KW_ABSTRACT
%token EXTENMARK KW_GOTO KW_JUMP KW_RETURN KW_BREAK KW_CONTINUE KW_REGEXTEN KW_HINT
%token KW_FOR KW_WHILE KW_CASE KW_PATTERN KW_DEFAULT KW_CATCH KW_SWITCHES KW_ESWITCHES
%token KW_INCLUDES

%right BAR COMMA

%token <str> word

%type <pval>includes
%type <pval>includeslist
%type <pval>switchlist
%type <pval>eswitches
%type <pval>switches
%type <pval>macro_statement
%type <pval>macro_statements
%type <pval>case_statement
%type <pval>case_statements
%type <pval>eval_arglist
%type <pval>application_call
%type <pval>application_call_head
%type <pval>macro_call
%type <pval>target jumptarget
%type <pval>statement
%type <pval>switch_head

%type <pval>if_head
%type <pval>random_head
%type <pval>iftime_head
%type <pval>statements
%type <pval>extension
%type <pval>ignorepat
%type <pval>element
%type <pval>elements
%type <pval>arglist
%type <pval>global_statement
%type <pval>global_statements
%type <pval>globals
%type <pval>macro
%type <pval>context
%type <pval>object
%type <pval>objects
%type <pval>file
/* XXX lr changes */
%type <pval>opt_else
%type <pval>elements_block
%type <pval>switchlist_block

%type <str>opt_word
%type <str>word_or_default

%type <str>goto_word
%type <str>word_list
%type <str>word3_list
%type <str>includedname

%type <intval>opt_abstract

/*
 * OPTIONS
 */

%locations	/* track source location using @n variables (yylloc in flex) */
%pure-parser	/* pass yylval and yylloc as arguments to yylex(). */
%name-prefix="ael_yy"
/*
 * add an additional argument, parseio, to yyparse(),
 * which is then accessible in the grammar actions
 */
%parse-param {struct parse_io *parseio}

/* there will be two shift/reduce conflicts, they involve the if statement, where a single statement occurs not wrapped in curlies in the "true" section
   the default action to shift will attach the else to the preceeding if. */
%expect 5
%error-verbose

/*
 * declare destructors for objects.
 * The former is for pval, the latter for strings.
 * NOTE: we must not have a destructor for a 'file' object.
 */
%destructor {
		destroy_pval($$);
		prev_word=0;
	}	includes includeslist switchlist eswitches switches
		macro_statement macro_statements case_statement case_statements
		eval_arglist application_call application_call_head
		macro_call target jumptarget statement switch_head
		if_head random_head iftime_head statements extension
		ignorepat element elements arglist global_statement
		global_statements globals macro context object objects
		opt_else
		elements_block switchlist_block

%destructor { free($$);}  word word_list goto_word word3_list includedname opt_word word_or_default


%%

file : objects  { $$ = parseio->pval = $1; }
	;

objects : object {$$=$1;}
	| objects object
		{
			if ( $1 && $2 ) {
				$$=$1;
				linku1($$,$2);
			} else if ( $1 ) {
				$$=$1;
			} else if ( $2 ) {
				$$=$2;
			}
		}
	| objects error {$$=$1;}
	;

object : context {$$=$1;}
	| macro {$$=$1;}
	| globals {$$=$1;}
	| SEMI  {$$=0;/* allow older docs to be read */}
	;

word_or_default : word { $$ = $1; }
	| KW_DEFAULT { $$ = strdup("default"); }
	;

context : opt_abstract KW_CONTEXT word_or_default elements_block {
		$$ = npval2(PV_CONTEXT, &@1, &@4);
		$$->u1.str = $3;
		$$->u2.statements = $4;
		$$->u3.abstract = $1; }
	;

/* optional "abstract" keyword */
opt_abstract: KW_ABSTRACT { $$ = 1; }
	| /* nothing */ { $$ = 0; }
	;

macro : KW_MACRO word LP arglist RP LC macro_statements RC {
		$$ = npval2(PV_MACRO, &@1, &@8);
		$$->u1.str = $2; $$->u2.arglist = $4; $$->u3.macro_statements = $7; }
	| KW_MACRO word LP arglist RP LC  RC {
		$$ = npval2(PV_MACRO, &@1, &@7);
		$$->u1.str = $2; $$->u2.arglist = $4; }
	| KW_MACRO word LP RP LC macro_statements RC {
		$$ = npval2(PV_MACRO, &@1, &@7);
		$$->u1.str = $2;
		$$->u3.macro_statements = $6; }
	| KW_MACRO word LP RP LC  RC {
		$$ = npval2(PV_MACRO, &@1, &@6);
		$$->u1.str = $2; }
	;

globals : KW_GLOBALS LC global_statements RC {
		$$ = npval2(PV_GLOBALS, &@1, &@4);
		$$->u1.statements = $3;}
	| KW_GLOBALS LC RC { /* empty globals is OK */
		$$ = npval2(PV_GLOBALS, &@1, &@3); }
	;

global_statements : global_statement {$$=$1;}
	| global_statements global_statement {$$=$1; linku1($$,$2);}
	| global_statements error {$$=$1;}
	;

global_statement : word EQ { reset_semicount(parseio->scanner); }  word SEMI {
		$$ = npval2(PV_VARDEC, &@1, &@5);
		$$->u1.str = $1;
		$$->u2.val = $4; }
	;

arglist : word { $$= nword($1, &@1); }
	| arglist COMMA word {
		pval *z = npval2(PV_WORD, &@1, &@3);
		z->u1.str = $3;
		$$ = linku1($1,z); }
	| arglist error {$$=$1;}
	;

elements_block : LC RC	{ $$ = NULL; }
	| LC elements RC { $$ = $2; }
	;

elements : element { $$=$1;}
	| error {$$=0;}
	| elements element { if ( $1 && $2 ) {$$=$1; linku1($$,$2);}
				else if ( $1 ) {$$=$1;}
				else if ( $2 ) {$$=$2;} }
	| elements error   { $$=$1;}
	;

element : extension {$$=$1;}
	| includes {$$=$1;}
	| switches {$$=$1;}
	| eswitches {$$=$1;}
	| ignorepat {$$=$1;}
	| word EQ { reset_semicount(parseio->scanner); } word SEMI {
		$$ = npval2(PV_VARDEC, &@1, &@5);
		$$->u1.str = $1;
		$$->u2.val = $4; }
	| word error {free($1); $$=0;}
	| SEMI  {$$=0;/* allow older docs to be read */}
	;

ignorepat : KW_IGNOREPAT EXTENMARK word SEMI {
		$$ = npval2(PV_IGNOREPAT, &@1, &@4);
		$$->u1.str = $3;}
	;

extension : word EXTENMARK statement {
		$$ = npval2(PV_EXTENSION, &@1, &@3);
		$$->u1.str = $1;
		$$->u2.statements = $3; }
	| KW_REGEXTEN word EXTENMARK statement {
		$$ = npval2(PV_EXTENSION, &@1, &@4);
		$$->u1.str = $2;
		$$->u2.statements = $4;
		$$->u4.regexten=1;}
	| KW_HINT LP word3_list RP word EXTENMARK statement {
		$$ = npval2(PV_EXTENSION, &@1, &@7);
		$$->u1.str = $5;
		$$->u2.statements = $7;
		$$->u3.hints = $3;}
	| KW_REGEXTEN KW_HINT LP word3_list RP word EXTENMARK statement {
		$$ = npval2(PV_EXTENSION, &@1, &@8);
		$$->u1.str = $6;
		$$->u2.statements = $8;
		$$->u4.regexten=1;
		$$->u3.hints = $4;}

	;

statements : statement {$$=$1;}
	| statements statement {if ( $1 && $2 ) {$$=$1; linku1($$,$2);}
						 else if ( $1 ) {$$=$1;}
						 else if ( $2 ) {$$=$2;} }
	| statements error {$$=$1;}
	;

if_head : KW_IF LP { reset_parencount(parseio->scanner); }  word_list RP {
		$$= npval2(PV_IF, &@1, &@5);
		$$->u1.str = $4; }
	;

random_head : KW_RANDOM LP { reset_parencount(parseio->scanner); } word_list RP {
		$$ = npval2(PV_RANDOM, &@1, &@5);
		$$->u1.str=$4;}
	;

iftime_head : KW_IFTIME LP word3_list COLON word3_list COLON word3_list
		BAR word3_list BAR word3_list BAR word3_list RP {
		$$ = npval2(PV_IFTIME, &@1, &@5); /* XXX really @5 or more ? */
		$$->u1.list = npval2(PV_WORD, &@3, &@3);
		asprintf(&($$->u1.list->u1.str), "%s:%s:%s", $3, $5, $7);
		free($3);
		free($5);
		free($7);
		$$->u1.list->next = nword($9, &@9);
		$$->u1.list->next->next = nword($11, &@11);
		$$->u1.list->next->next->next = nword($13, &@13);
		prev_word = 0;
	}
	| KW_IFTIME LP word BAR word3_list BAR word3_list BAR word3_list RP {
		$$ = npval2(PV_IFTIME, &@1, &@5); /* XXX @5 or greater ? */
		$$->u1.list = nword($3, &@3);
		$$->u1.list->next = nword($5, &@5);
		$$->u1.list->next->next = nword($7, &@7);
		$$->u1.list->next->next->next = nword($9, &@9);
		prev_word = 0;
	}

	;

/* word_list is a hack to fix a problem with context switching between bison and flex;
   by the time you register a new context with flex, you've already got a look-ahead token
   from the old context, with no way to put it back and start afresh. So, we kludge this
   and merge the words back together. */

word_list : word { $$ = $1;}
	| word word {
		asprintf(&($$), "%s%s", $1, $2);
		free($1);
		free($2);
		prev_word = $$;}
	;

word3_list : word { $$ = $1;}
	| word word {
		asprintf(&($$), "%s%s", $1, $2);
		free($1);
		free($2);
		prev_word = $$;}
	| word word word {
		asprintf(&($$), "%s%s%s", $1, $2, $3);
		free($1);
		free($2);
		free($3);
		prev_word=$$;}
	;

goto_word : word { $$ = $1;}
	| word word {
		asprintf(&($$), "%s%s", $1, $2);
		free($1);
		free($2);}
	| word COLON word {
		asprintf(&($$), "%s:%s", $1, $3);
		free($1);
		free($3);}
	;

switch_head : KW_SWITCH LP { reset_parencount(parseio->scanner); } word RP  LC {
		$$ = npval2(PV_SWITCH, &@1, &@6);
		$$->u1.str = $4; }
	;

/*
 * Definition of a statememt in our language
 */
statement : LC statements RC {
		$$ = npval2(PV_STATEMENTBLOCK, &@1, &@3);
		$$->u1.list = $2; }
	| word EQ {reset_semicount(parseio->scanner);} word SEMI {
		$$ = npval2(PV_VARDEC, &@1, &@5);
		$$->u1.str = $1;
		$$->u2.val = $4; }
	| KW_GOTO target SEMI {
		$$ = npval2(PV_GOTO, &@1, &@3);
		$$->u1.list = $2;}
	| KW_JUMP jumptarget SEMI {
		$$ = npval2(PV_GOTO, &@1, &@3);
		$$->u1.list = $2;}
	| word COLON {
		$$ = npval2(PV_LABEL, &@1, &@2);
		$$->u1.str = $1; }
	| KW_FOR LP {reset_semicount(parseio->scanner);} word SEMI
			{reset_semicount(parseio->scanner);} word SEMI
			{reset_parencount(parseio->scanner);} word RP statement {
		$$ = npval2(PV_FOR, &@1, &@12);
		$$->u1.for_init = $4;
		$$->u2.for_test=$7;
		$$->u3.for_inc = $10;
		$$->u4.for_statements = $12;}
	| KW_WHILE LP {reset_parencount(parseio->scanner);} word RP statement {
		$$ = npval2(PV_WHILE, &@1, &@6);
		$$->u1.str = $4;
		$$->u2.statements = $6; }
	| switch_head RC /* empty list OK */ {
		$$ = update_last($1, &@2); }
	| switch_head case_statements RC {
		$$ = update_last($1, &@3);
		$$->u2.statements = $2;}
	| AMPER macro_call SEMI {
		$$ = update_last($2, &@2); }
	| application_call SEMI {
		$$ = update_last($1, &@2); }
	| word SEMI {
		$$= npval2(PV_APPLICATION_CALL, &@1, &@2);
		$$->u1.str = $1;}
	| application_call EQ {reset_semicount(parseio->scanner);} word SEMI {
		char *bufx;
		int tot=0;
		pval *pptr;
		$$ = npval2(PV_VARDEC, &@1, &@5);
		$$->u2.val=$4;
		/* rebuild the original string-- this is not an app call, it's an unwrapped vardec, with a func call on the LHS */
		/* string to big to fit in the buffer? */
		tot+=strlen($1->u1.str);
		for(pptr=$1->u2.arglist;pptr;pptr=pptr->next) {
			tot+=strlen(pptr->u1.str);
			tot++; /* for a sep like a comma */
		}
		tot+=4; /* for safety */
		bufx = ast_calloc(1, tot);
		strcpy(bufx,$1->u1.str);
		strcat(bufx,"(");
		/* XXX need to advance the pointer or the loop is very inefficient */
		for (pptr=$1->u2.arglist;pptr;pptr=pptr->next) {
			if ( pptr != $1->u2.arglist )
				strcat(bufx,",");
			strcat(bufx,pptr->u1.str);
		}
		strcat(bufx,")");
#ifdef AAL_ARGCHECK
		if ( !ael_is_funcname($1->u1.str) )
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Function call? The name %s is not in my internal list of function names\n",
				my_file, @1.first_line, @1.first_column, @1.last_column, $1->u1.str);
#endif
		$$->u1.str = bufx;
		destroy_pval($1); /* the app call it is not, get rid of that chain */
		prev_word = 0;
	}
	| KW_BREAK SEMI { $$ = npval2(PV_BREAK, &@1, &@2); }
	| KW_RETURN SEMI { $$ = npval2(PV_RETURN, &@1, &@2); }
	| KW_CONTINUE SEMI { $$ = npval2(PV_CONTINUE, &@1, &@2); }
	| random_head statement opt_else {
		$$ = update_last($1, &@2); /* XXX probably @3... */
		$$->u2.statements = $2;
		$$->u3.else_statements = $3;}
	| if_head statement opt_else {
		$$ = update_last($1, &@2); /* XXX probably @3... */
		$$->u2.statements = $2;
		$$->u3.else_statements = $3;}
	| iftime_head statement opt_else {
		$$ = update_last($1, &@2); /* XXX probably @3... */
		$$->u2.statements = $2;
		$$->u3.else_statements = $3;}
	| SEMI { $$=0; }
	;

opt_else : KW_ELSE statement { $$ = $2; }
	| { $$ = NULL ; }

/* XXX unused */
bar_or_comma: BAR | COMMA ;

target : goto_word { $$ = nword($1, &@1); }
	| goto_word BAR goto_word {
		$$ = nword($1, &@1);
		$$->next = nword($3, &@3); }
	| goto_word COMMA goto_word {
		$$ = nword($1, &@1);
		$$->next = nword($3, &@3); }
	| goto_word BAR goto_word BAR goto_word {
		$$ = nword($1, &@1);
		$$->next = nword($3, &@3);
		$$->next->next = nword($5, &@5); }
	| goto_word COMMA goto_word COMMA goto_word {
		$$ = nword($1, &@1);
		$$->next = nword($3, &@3);
		$$->next->next = nword($5, &@5); }
	| KW_DEFAULT BAR goto_word BAR goto_word {
		$$ = nword(strdup("default"), &@1);
		$$->next = nword($3, &@3);
		$$->next->next = nword($5, &@5); }
	| KW_DEFAULT COMMA goto_word COMMA goto_word {
		$$ = nword(strdup("default"), &@1);
		$$->next = nword($3, &@3);
		$$->next->next = nword($5, &@5); }
	;

/* XXX please document the form of jumptarget */
jumptarget : goto_word {
		$$ = nword($1, &@1);
		$$->next = nword(strdup("1"), &@1); }  /*  jump extension[,priority][@context] */
	| goto_word COMMA goto_word {
		$$ = nword($1, &@1);
		$$->next = nword($3, &@3); }
	| goto_word COMMA word AT word {
		$$ = npval2(PV_WORD, &@1, &@1);
		$$->u1.str = $5;	/* XXX must check this */
		$$->next = npval2(PV_WORD, &@3, &@3);
		$$->next->u1.str = $1;
		$$->next->next = npval2(PV_WORD, &@5, &@5);
		$$->next->next->u1.str = $3; }
	| goto_word AT goto_word {
		$$ = npval2(PV_WORD, &@1, &@1);
		$$->u1.str = $3;
		$$->next = npval2(PV_WORD, &@3, &@3);
		$$->next->u1.str = $1;
		$$->next->next = npval2(PV_WORD, &@3, &@3);
		$$->next->next->u1.str = strdup("1"); }
	| goto_word COMMA word AT KW_DEFAULT {
		$$ = npval2(PV_WORD, &@1, &@1);
		$$->u1.str = strdup("default");
		$$->next = npval2(PV_WORD, &@3, &@3);
		$$->next->u1.str = $1;
		$$->next->next = npval2(PV_WORD, &@5, &@5);
		$$->next->next->u1.str = $3; }
	| goto_word AT KW_DEFAULT {
		$$ = npval2(PV_WORD, &@1, &@1);
		$$->u1.str = strdup("default");
		$$->next = npval2(PV_WORD, &@3, &@3);
		$$->next->u1.str = $1;
		$$->next->next = npval2(PV_WORD, &@3, &@3);
		$$->next->next->u1.str = strdup("1"); }
	;

macro_call : word LP {reset_argcount(parseio->scanner);} eval_arglist RP {
		/* XXX original code had @2 but i think we need @5 */
		$$ = npval2(PV_MACRO_CALL, &@1, &@5);
		$$->u1.str = $1;
		$$->u2.arglist = $4;}
	| word LP RP {
		$$= npval2(PV_MACRO_CALL, &@1, &@3);
		$$->u1.str = $1; }
	;

/* XXX application_call_head must be revised. Having 'word LP { ...'
 * just as above should work fine, however it gives a different result.
 */
application_call_head: word LP {reset_argcount(parseio->scanner);} {
		if (strcasecmp($1,"goto") == 0) {
			$$= npval2(PV_GOTO, &@1, &@3);
			free($1); /* won't be using this */
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Suggestion: Use the goto statement instead of the Goto() application call in AEL.\n", my_file, @1.first_line, @1.first_column, @1.last_column );
		} else
			$$= npval2(PV_APPLICATION_CALL, &@1, &@3);
		$$->u1.str = $1; }
	;

application_call : application_call_head eval_arglist RP {
		$$ = update_last($1, &@3);
 		if( $$->type == PV_GOTO )
			$$->u1.list = $2;
	 	else
			$$->u2.arglist = $2;
	}
	| application_call_head RP { $$ = update_last($1, &@2); }
	;

opt_word : word { $$ = $1 }
	| { $$ = strdup(""); }
	;

eval_arglist :  word_list { $$ = nword($1, &@1); }
	| /*nothing! */   {
		$$= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		$$->u1.str = strdup(""); }
	| eval_arglist COMMA  opt_word {
		pval *z = nword($3, &@3);
		$$ = $1;
		linku1($1,z); }
	;

case_statements: case_statement {$$=$1;}
	| case_statements case_statement { if ( $1 && $2 ) {$$=$1; linku1($$,$2);}
						 else if ( $1 ) {$$=$1;}
						 else if ( $2 ) {$$=$2;} }
	;

case_statement: KW_CASE word COLON statements {
		$$ = npval2(PV_CASE, &@1, &@3); /* XXX 3 or 4 ? */
		$$->u1.str = $2;
		$$->u2.statements = $4;}
	| KW_DEFAULT COLON statements {
		$$ = npval2(PV_DEFAULT, &@1, &@3);
		$$->u1.str = NULL;
		$$->u2.statements = $3;}
	| KW_PATTERN word COLON statements {
		$$ = npval2(PV_PATTERN, &@1, &@4); /* XXX@3 or @4 ? */
		$$->u1.str = $2;
		$$->u2.statements = $4;}
	| KW_CASE word COLON {
		$$ = npval2(PV_CASE, &@1, &@3);
		$$->u1.str = $2;}
	| KW_DEFAULT COLON {
		$$ = npval2(PV_DEFAULT, &@1, &@2);
		$$->u1.str = NULL;}
	| KW_PATTERN word COLON  {
		$$ = npval2(PV_PATTERN, &@1, &@3);
		$$->u1.str = $2;}
	;

macro_statements: macro_statement {$$ = $1;}
	| macro_statements macro_statement { if ( $1 && $2 ) {$$=$1; linku1($$,$2);}
						 else if ( $1 ) {$$=$1;}
						 else if ( $2 ) {$$=$2;} }
	;

macro_statement : statement {$$=$1;}
	| KW_CATCH word LC statements RC {
		$$ = npval2(PV_CATCH, &@1, &@5);
		$$->u1.str = $2;
		$$->u2.statements = $4;}
	;

switches : KW_SWITCHES switchlist_block {
		$$ = npval2(PV_SWITCHES, &@1, &@2);
		$$->u1.list = $2; }
	;

eswitches : KW_ESWITCHES switchlist_block {
		$$ = npval2(PV_ESWITCHES, &@1, &@2);
		$$->u1.list = $2; }
	;

switchlist_block : LC switchlist RC { $$ = $2; }
	| LC RC { $$ = NULL; }
	;

switchlist : word SEMI {
		$$ = npval2(PV_WORD, &@1, &@2);
		$$->u1.str = $1;}
	| switchlist word SEMI {
		pval *z = npval2(PV_WORD, &@2, &@3);
		z->u1.str = $2;
		$$=$1;
		linku1($$,z); }
	| switchlist error {$$=$1;}
	;

includeslist : includedname SEMI {
		$$ = npval2(PV_WORD, &@1, &@2);
		$$->u1.str = $1;}
	| includedname BAR word3_list COLON word3_list COLON word3_list
			BAR word3_list BAR word3_list BAR word3_list SEMI {
		$$ = npval2(PV_WORD, &@1, &@2);
		$$->u1.str = $1;
		$$->u2.arglist = npval2(PV_WORD, &@3, &@7);
		asprintf( &($$->u2.arglist->u1.str), "%s:%s:%s", $3, $5, $7);
		free($3);
		free($5);
		free($7);
		$$->u2.arglist->next = nword($9, &@9);
		$$->u2.arglist->next->next = nword($11, &@11);
		$$->u2.arglist->next->next->next = nword($13, &@13);
		prev_word=0;
	}
	| includedname BAR word BAR word3_list BAR word3_list BAR word3_list SEMI {
		$$ = npval2(PV_WORD, &@1, &@2);
		$$->u1.str = $1;
		$$->u2.arglist = nword($3, &@3);
		$$->u2.arglist->next = nword($5, &@5);
		$$->u2.arglist->next->next = nword($7, &@7);
		$$->u2.arglist->next->next->next = nword($9, &@9);
		prev_word=0;
	}
	| includeslist includedname SEMI {
		pval *z = npval2(PV_WORD, &@2, &@3); /* XXX don't we need @1-@3 ?*/
		$$=$1;
		z->u1.str = $2;
		linku1($$,z); }
	| includeslist includedname BAR word3_list COLON word3_list COLON word3_list
			BAR word3_list BAR word3_list BAR word3_list SEMI {
		pval *z = npval2(PV_WORD, &@2, &@3);
		$$=$1; z->u1.str = $2;
		linku1($$,z);
		z->u2.arglist = npval2(PV_WORD, &@4, &@4);
		asprintf( &($$->u2.arglist->u1.str), "%s:%s:%s", $4, $6, $8);
		free($4);
		free($6);
		free($8);
		z->u2.arglist->next = nword($10, &@10);
		z->u2.arglist->next->next = nword($12, &@12);
		z->u2.arglist->next->next->next = nword($14, &@14);
		prev_word=0;
	}
	| includeslist includedname BAR word BAR word3_list BAR word3_list BAR word3_list SEMI {
		pval *z = npval2(PV_WORD, &@2, &@3);
		$$=$1;
		linku1($$,z);
		$$->u2.arglist->u1.str = $4;			/* XXX maybe too early ? */
		z->u1.str = $2;
		z->u2.arglist = npval2(PV_WORD, &@4, &@4);	/* XXX is this correct ? */
		z->u2.arglist->next = nword($6, &@6);
		z->u2.arglist->next->next = nword($8, &@8);
		z->u2.arglist->next->next->next = nword($10, &@10);
		prev_word=0;
	}
	| includeslist error {$$=$1;}
	;

includedname : word { $$ = $1;}
	| KW_DEFAULT {$$=strdup("default");}
	;

includes : KW_INCLUDES LC includeslist RC {
		$$ = npval2(PV_INCLUDES, &@1, &@4);
		$$->u1.list = $3;}
	| KW_INCLUDES LC RC {
		$$ = npval2(PV_INCLUDES, &@1, &@3);}
	;


%%

static char *token_equivs1[] =
{
	"AMPER",
	"AT",
	"BAR",
	"COLON",
	"COMMA",
	"EQ",
	"EXTENMARK",
	"KW_BREAK",
	"KW_CASE",
	"KW_CATCH",
	"KW_CONTEXT",
	"KW_CONTINUE",
	"KW_DEFAULT",
	"KW_ELSE",
	"KW_ESWITCHES",
	"KW_FOR",
	"KW_GLOBALS",
	"KW_GOTO",
	"KW_HINT",
	"KW_IFTIME",
	"KW_IF",
	"KW_IGNOREPAT",
	"KW_INCLUDES"
	"KW_JUMP",
	"KW_MACRO",
	"KW_PATTERN",
	"KW_REGEXTEN",
	"KW_RETURN",
	"KW_SWITCHES",
	"KW_SWITCH",
	"KW_WHILE",
	"LC",
	"LP",
	"RC",
	"RP",
	"SEMI",
};

static char *token_equivs2[] =
{
	"&",
	"@",
	"|",
	":",
	",",
	"=",
	"=>",
	"break",
	"case",
	"catch",
	"context",
	"continue",
	"default",
	"else",
	"eswitches",
	"for",
	"globals",
	"goto",
	"hint",
	"ifTime",
	"if",
	"ignorepat",
	"includes"
	"jump",
	"macro",
	"pattern",
	"regexten",
	"return",
	"switches",
	"switch",
	"while",
	"{",
	"(",
	"}",
	")",
	";",
};


static char *ael_token_subst(char *mess)
{
	/* calc a length, malloc, fill, and return; yyerror had better free it! */
	int len=0,i;
	char *p;
	char *res, *s,*t;
	int token_equivs_entries = sizeof(token_equivs1)/sizeof(char*);

	for (p=mess; *p; p++) {
		for (i=0; i<token_equivs_entries; i++) {
			if ( strncmp(p,token_equivs1[i],strlen(token_equivs1[i])) == 0 )
			{
				len+=strlen(token_equivs2[i])+2;
				p += strlen(token_equivs1[i])-1;
				break;
			}
		}
		len++;
	}
	res = ast_calloc(1, len+1);
	res[0] = 0;
	s = res;
	for (p=mess; *p;) {
		int found = 0;
		for (i=0; i<token_equivs_entries; i++) {
			if ( strncmp(p,token_equivs1[i],strlen(token_equivs1[i])) == 0 ) {
				*s++ = '\'';
				for (t=token_equivs2[i]; *t;) {
					*s++ = *t++;
				}
				*s++ = '\'';
				p += strlen(token_equivs1[i]);
				found = 1;
				break;
			}
		}
		if( !found )
			*s++ = *p++;
	}
	*s++ = 0;
	return res;
}

void yyerror(YYLTYPE *locp, struct parse_io *parseio,  char const *s)
{
	char *s2 = ael_token_subst((char *)s);
	if (locp->first_line == locp->last_line) {
		ast_log(LOG_ERROR, "==== File: %s, Line %d, Cols: %d-%d: Error: %s\n", my_file, locp->first_line, locp->first_column, locp->last_column, s2);
	} else {
		ast_log(LOG_ERROR, "==== File: %s, Line %d Col %d  to Line %d Col %d: Error: %s\n", my_file, locp->first_line, locp->first_column, locp->last_line, locp->last_column, s2);
	}
	free(s2);
	parseio->syntax_error_count++;
}

static struct pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column)
{
	extern char *my_file;
	pval *z = ast_calloc(1, sizeof(struct pval));
	z->type = type;
	z->startline = first_line;
	z->endline = last_line;
	z->startcol = first_column;
	z->endcol = last_column;
	z->filename = strdup(my_file);
	return z;
}

static struct pval *npval2(pvaltype type, YYLTYPE *first, YYLTYPE *last)
{
	return npval(type, first->first_line, last->last_line,
			first->first_column, last->last_column);
}

static struct pval *update_last(pval *obj, YYLTYPE *last)
{
	obj->endline = last->last_line;
	obj->endcol = last->last_column;
	return obj;
}

/* frontend for npval to create a PV_WORD string from the given token */
static pval *nword(char *string, YYLTYPE *pos)
{
	pval *p = npval2(PV_WORD, pos, pos);
	if (p)
		p->u1.str = string;
	return p;
}

/* append second element to the list in the first one */
static pval * linku1(pval *head, pval *tail)
{
	if (!head)
		return tail;
	if (!head->next) {
		head->next = tail;
	} else {
		head->u1_last->next = tail;
	}
	head->u1_last = tail;
	return head;
}

