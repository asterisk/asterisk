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
 * \brief Flex scanner description of tokens used in AEL2 .
 *
 */

/*
 * Start with flex options:
 *
 * %x describes the contexts we have: paren, semic and argg, plus INITIAL
 */
%x paren semic argg  comment curlystate wordstate brackstate

/* prefix used for various globally-visible functions and variables.
 * This renames also yywrap, but since we do not use it, we just
 * add option noyywrap to remove it.
 */
%option prefix="ael_yy"
%option noyywrap 8bit

/* I specify this option to suppress flex generating code with ECHO
  in it. This generates compiler warnings in some systems; We've
  seen the fwrite generate Unused variable warnings with 4.1.2 gcc.
  Some systems have tweaked flex ECHO macro to keep the compiler
  happy.  To keep the warning message from getting output, I added
  a default rule at the end of the patterns section */
%option nodefault

/* yyfree normally just frees its arg. It can be null sometimes,
   which some systems will complain about, so, we'll define our own version */
%option noyyfree

/* batch gives a bit more performance if we are using it in
 * a non-interactive mode. We probably don't care much.
 */
%option batch

/* outfile is the filename to be used instead of lex.yy.c */
%option outfile="ael_lex.c"

/*
 * These are not supported in flex 2.5.4, but we need them
 * at the moment:
 * reentrant produces a thread-safe parser. Not 100% sure that
 * we require it, though.
 * bison-bridge passes an additional yylval argument to yylex().
 * bison-locations is probably not needed.
 */
%option reentrant
%option bison-bridge
%option bison-locations

%{
#define WRAP_LIBC_MALLOC
#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>

#if !defined(GLOB_ABORTED)
#define GLOB_ABORTED GLOB_ABEND
#endif

#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/hashtab.h"
#include "ael/ael.tab.h"
#include "asterisk/ael_structs.h"

/*
 * A stack to keep track of matching brackets ( [ { } ] )
 */
static char pbcstack[400];	/* XXX missing size checks */
static int pbcpos = 0;
static void pbcpush(char x);
static int pbcpop(char x);
static int parencount = 0;

/*
 * A similar stack to keep track of matching brackets ( [ { } ] ) in word tokens surrounded by ${ ... }
 */
static char pbcstack2[400];	/* XXX missing size checks */
static int pbcpos2 = 0;
static void pbcpush2(char x);
static int pbcpop2(char x);
static int parencount2 = 0;

/*
 * A similar stack to keep track of matching brackets ( [ { } ] ) in word tokens surrounded by $[ ... ]
 */
static char pbcstack3[400];	/* XXX missing size checks */
static int pbcpos3 = 0;
static void pbcpush3(char x);
static int pbcpop3(char x);
static int parencount3 = 0;


/*
 * current line, column and filename, updated as we read the input.
 */
static int my_lineno = 1;	/* current line in the source */
static int my_col = 1;		/* current column in the source */
char *my_file = 0;		/* used also in the bison code */
char *prev_word;		/* XXX document it */

#define MAX_INCLUDE_DEPTH 50

/*
 * flex is not too smart, and generates global functions
 * without prototypes so the compiler may complain.
 * To avoid that, we declare the prototypes here,
 * even though these functions are not used.
 */
int ael_yyget_column  (yyscan_t yyscanner);
void ael_yyset_column (int  column_no , yyscan_t yyscanner);

int ael_yyparse (struct parse_io *);

/*
 * A stack to process include files.
 * As we switch into the new file we need to store the previous
 * state to restore it later.
 */
struct stackelement {
	char *fname;
	int lineno;
	int colno;
	glob_t globbuf;        /* the current globbuf */
	int globbuf_pos;   /* where we are in the current globbuf */
	YY_BUFFER_STATE bufstate;
};

static struct stackelement  include_stack[MAX_INCLUDE_DEPTH];
static int include_stack_index = 0;
static void setup_filestack(char *fnamebuf, int fnamebuf_siz, glob_t *globbuf, int globpos, yyscan_t xscan, int create);

/*
 * if we use the @n feature of bison, we must supply the start/end
 * location of tokens in the structure pointed by yylloc.
 * Simple tokens are just assumed to be on the same line, so
 * the line number is constant, and the column is incremented
 * by the length of the token.
 */
#ifdef FLEX_BETA	/* set for 2.5.33 */

/* compute the total number of lines and columns in the text
 * passed as argument.
 */
static void pbcwhere(const char *text, int *line, int *col )
{
	int loc_line = *line;
	int loc_col = *col;
	char c;
	while ( (c = *text++) ) {
		if ( c == '\t' ) {
			loc_col += 8 - (loc_col % 8);
		} else if ( c == '\n' ) {
			loc_line++;
			loc_col = 1;
		} else
			loc_col++;
	}
	*line = loc_line;
	*col = loc_col;
}

#define	STORE_POS do {							\
		yylloc->first_line = yylloc->last_line = my_lineno;	\
		yylloc->first_column=my_col;				\
		yylloc->last_column=my_col+yyleng-1;			\
		my_col+=yyleng;						\
	} while (0)

#define	STORE_LOC do {					\
		yylloc->first_line = my_lineno;		\
		yylloc->first_column=my_col;		\
		pbcwhere(yytext, &my_lineno, &my_col);	\
		yylloc->last_line = my_lineno;		\
		yylloc->last_column = my_col - 1;	\
	} while (0)
#else
#define	STORE_POS
#define	STORE_LOC
#endif
%}

KEYWORD     (context|abstract|extend|macro|globals|local|ignorepat|switch|if|ifTime|random|regexten|hint|else|goto|jump|return|break|continue|for|while|case|default|pattern|catch|switches|eswitches|includes)

NOPARENS	([^()\[\]\{\}]|\\[()\[\]\{\}])*

NOARGG		([^(),\{\}\[\]]|\\[,()\[\]\{\}])*

NOSEMIC		([^;()\{\}\[\]]|\\[;()\[\]\{\}])*

HIBIT		[\x80-\xff]

%%

\{		{ STORE_POS; return LC;}
\}		{ STORE_POS; return RC;}
\(		{ STORE_POS; return LP;}
\)		{ STORE_POS; return RP;}
\;		{ STORE_POS; return SEMI;}
\=		{ STORE_POS; return EQ;}
\,		{ STORE_POS; return COMMA;}
\:		{ STORE_POS; return COLON;}
\&		{ STORE_POS; return AMPER;}
\|		{ STORE_POS; return BAR;}
\=\>		{ STORE_POS; return EXTENMARK;}
\@		{ STORE_POS; return AT;}
\/\/[^\n]*	{/*comment*/}
context		{ STORE_POS; return KW_CONTEXT;}
abstract	{ STORE_POS; return KW_ABSTRACT;}
extend		{ STORE_POS; return KW_EXTEND;}
macro		{ STORE_POS; return KW_MACRO;};
globals		{ STORE_POS; return KW_GLOBALS;}
local		{ STORE_POS; return KW_LOCAL;}
ignorepat	{ STORE_POS; return KW_IGNOREPAT;}
switch		{ STORE_POS; return KW_SWITCH;}
if		{ STORE_POS; return KW_IF;}
ifTime		{ STORE_POS; return KW_IFTIME;}
random		{ STORE_POS; return KW_RANDOM;}
regexten	{ STORE_POS; return KW_REGEXTEN;}
hint		{ STORE_POS; return KW_HINT;}
else		{ STORE_POS; return KW_ELSE;}
goto		{ STORE_POS; return KW_GOTO;}
jump		{ STORE_POS; return KW_JUMP;}
return		{ STORE_POS; return KW_RETURN;}
break		{ STORE_POS; return KW_BREAK;}
continue	{ STORE_POS; return KW_CONTINUE;}
for		{ STORE_POS; return KW_FOR;}
while		{ STORE_POS; return KW_WHILE;}
case		{ STORE_POS; return KW_CASE;}
default		{ STORE_POS; return KW_DEFAULT;}
pattern		{ STORE_POS; return KW_PATTERN;}
catch		{ STORE_POS; return KW_CATCH;}
switches	{ STORE_POS; return KW_SWITCHES;}
eswitches	{ STORE_POS; return KW_ESWITCHES;}
includes	{ STORE_POS; return KW_INCLUDES;}
"/*"            { BEGIN(comment); my_col += 2; }

<comment>[^*\n]*	{ my_col += yyleng; }
<comment>[^*\n]*\n	{ ++my_lineno; my_col=1;}
<comment>"*"+[^*/\n]*	{ my_col += yyleng; }
<comment>"*"+[^*/\n]*\n 	{ ++my_lineno; my_col=1;}
<comment>"*/"		{ my_col += 2; BEGIN(INITIAL); } /* the nice thing about comments is that you know exactly what ends them */

\n		{ my_lineno++; my_col = 1; }
[ ]+		{ my_col += yyleng; }
[\t]+		{ my_col += (yyleng*8)-(my_col%8); }

({KEYWORD}?[-a-zA-Z0-9'"_/.\<\>\*\+!$#\[\]]|{HIBIT}|(\\.)|(\$\{)|(\$\[)) { 
      /* boy did I open a can of worms when I changed the lexical token "word". 
  	  	 all the above keywords can be used as a beginning to a "word".-
		 before, a "word" would match a longer sequence than the above	 
	     keywords, and all would be well. But now "word" is a single char		
	     and feeds into a statemachine sort of sequence from there on. So...
		 I added the {KEYWORD}? to the beginning of the word match sequence */

		if (!strcmp(yytext,"${")) {
		   	parencount2 = 0;
			pbcpos2 = 0;
			pbcpush2('{');	/* push '{' so the last pcbpop (parencount2 = -1) will succeed */
			BEGIN(curlystate);
			yymore();
		} else if (!strcmp(yytext,"$[")) {
		   	parencount3 = 0;
			pbcpos3 = 0;
			pbcpush3('[');	/* push '[' so the last pcbpop (parencount3 = -1) will succeed */
			BEGIN(brackstate);
			yymore();
		} else {
		    BEGIN(wordstate);
			yymore();
		}
	}

<wordstate>[-a-zA-Z0-9'"_/.\<\>\*\+!$#\[\]] { yymore(); /* Keep going */ }
<wordstate>{HIBIT} { yymore(); /* Keep going */ }
<wordstate>(\\.)  { yymore(); /* Keep Going */ }
<wordstate>(\$\{)  { /* the beginning of a ${} construct. prepare and pop into curlystate */
	   	parencount2 = 0;
		pbcpos2 = 0;
		pbcpush2('{');	/* push '{' so the last pcbpop (parencount2 = -1) will succeed */
		BEGIN(curlystate);
		yymore();
	}
<wordstate>(\$\[)  { /* the beginning of a $[] construct. prepare and pop into brackstate */
	   	parencount3 = 0;
		pbcpos3 = 0;
		pbcpush3('[');	/* push '[' so the last pcbpop (parencount3 = -1) will succeed */
		BEGIN(brackstate);
		yymore();
	}
<wordstate>([^a-zA-Z0-9\x80-\xff\x2d'"_/.\<\>\*\+!$#\[\]]) {
		/* a non-word constituent char, like a space, tab, curly, paren, etc */
		char c = yytext[yyleng-1];
		STORE_POS;
		yylval->str = malloc(yyleng);
		strncpy(yylval->str, yytext, yyleng);
		yylval->str[yyleng-1] = 0;
		unput(c);  /* put this ending char back in the stream */
		BEGIN(0);
		return word;
	}


<curlystate>{NOPARENS}\}	{
		if ( pbcpop2('}') ) {	/* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ')' in expression: %s !\n", my_file, my_lineno, my_col, yytext);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = 0;
			return word;
		}
		parencount2--;
		if ( parencount2 >= 0) {
			yymore();
		} else {
			BEGIN(wordstate); /* Finished with the current ${} construct. Return to word gathering state */
			yymore();
		}
	}

<curlystate>{NOPARENS}[\(\[\{]	{ 
		char c = yytext[yyleng-1];
		if (c == '{')
			parencount2++;
		pbcpush2(c);
		yymore();
	}

<curlystate>{NOPARENS}[\]\)]	{ 
		char c = yytext[yyleng-1];
		if ( pbcpop2(c))  { /* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched '%c' in expression!\n",
				my_file, my_lineno, my_col, c);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = 0;
			return word;
		}
		yymore();
	}


<brackstate>{NOPARENS}\]	{
		if ( pbcpop3(']') ) {	/* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ')' in expression: %s !\n", my_file, my_lineno, my_col, yytext);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = 0;
			return word;
		}
		parencount3--;
		if ( parencount3 >= 0) {
			yymore();
		} else {
			BEGIN(wordstate); /* Finished with the current ${} construct. Return to word gathering state */
			yymore();
		}
	}

<brackstate>{NOPARENS}[\(\[\{]	{ 
		char c = yytext[yyleng-1];
		if (c == '[')
			parencount3++;
		pbcpush3(c);
		yymore();
	}

<brackstate>{NOPARENS}[\}\)]	{ 
		char c = yytext[yyleng-1];
		if ( pbcpop3(c))  { /* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched '%c' in expression!\n",
				my_file, my_lineno, my_col, c);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = 0;
			return word;
		}
		yymore();
	}


	/*
	 * context used for arguments of if_head, random_head, switch_head,
	 * for (last statement), while (XXX why not iftime_head ?).
	 * End with the matching parentheses.
	 * A comma at the top level is valid here, unlike in argg where it
	 * is an argument separator so it must be returned as a token.
	 */
<paren>{NOPARENS}\)	{
		if ( pbcpop(')') ) {	/* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ')' in expression: %s !\n", my_file, my_lineno, my_col, yytext);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = 0;
			prev_word = 0;
			return word;
		}
		parencount--;
		if ( parencount >= 0) {
			yymore();
		} else {
			STORE_LOC;
			yylval->str = malloc(yyleng);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng-1] = 0;
			unput(')');
			BEGIN(0);
			return word;
		}
	}

<paren>{NOPARENS}[\(\[\{]	{
		char c = yytext[yyleng-1];
		if (c == '(')
			parencount++;
		pbcpush(c);
		yymore();
	}

<paren>{NOPARENS}[\]\}]	{
		char c = yytext[yyleng-1];
		if ( pbcpop(c))  { /* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched '%c' in expression!\n",
				my_file, my_lineno, my_col, c);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = 0;
			return word;
		}
		yymore();
	}


	/*
	 * handlers for arguments to a macro or application calls.
	 * We enter this context when we find the initial '(' and
	 * stay here until we close all matching parentheses,
	 * and find the comma (argument separator) or the closing ')'
	 * of the (external) call, which happens when parencount == 0
	 * before the decrement.
	 */
<argg>{NOARGG}[\(\[\{]	  {
		char c = yytext[yyleng-1];
		if (c == '(')
			parencount++;
		pbcpush(c);
		yymore();
	}

<argg>{NOARGG}\)	{
		if ( pbcpop(')') ) { /* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ')' in expression!\n", my_file, my_lineno, my_col);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = 0;
			return word;
		}

		parencount--;
		if( parencount >= 0){
			yymore();
		} else {
			STORE_LOC;
			BEGIN(0);
			if ( !strcmp(yytext, ")") )
				return RP;
			yylval->str = malloc(yyleng);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng-1] = '\0'; /* trim trailing ')' */
			unput(')');
			return word;
		}
	}

<argg>{NOARGG}\,	{
		if( parencount != 0) { /* ast_log(LOG_NOTICE,"Folding in a comma!\n"); */
			yymore();
		} else  {
			STORE_LOC;
			if( !strcmp(yytext,"," ) )
				return COMMA;
			yylval->str = malloc(yyleng);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng-1] = '\0'; /* trim trailing ',' */
			unput(',');
			return word;
		}
	}

<argg>{NOARGG}[\]\}]	{
		char c = yytext[yyleng-1];
		if ( pbcpop(c) ) { /* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched '%c' in expression!\n", my_file, my_lineno, my_col, c);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = '\0';
			return word;
		}
		yymore();
	}

	/*
	 * context used to find tokens in the right hand side of assignments,
	 * or in the first and second operand of a 'for'. As above, match
	 * commas and use ';' as a separator (hence return it as a separate token).
	 */
<semic>{NOSEMIC}[\(\[\{]	{
		char c = yytext[yyleng-1];
		yymore();
		pbcpush(c);
	}

<semic>{NOSEMIC}[\)\]\}]	{
		char c = yytext[yyleng-1];
		if ( pbcpop(c) ) { /* error */
			STORE_LOC;
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched '%c' in expression!\n", my_file, my_lineno, my_col, c);
			BEGIN(0);
			yylval->str = malloc(yyleng+1);
			strncpy(yylval->str, yytext, yyleng);
			yylval->str[yyleng] = '\0';
			return word;
		}
		yymore();
	}

<semic>{NOSEMIC};	{
		STORE_LOC;
		yylval->str = malloc(yyleng);
		strncpy(yylval->str, yytext, yyleng);
		yylval->str[yyleng-1] = '\0'; /* trim trailing ';' */
		unput(';');
		BEGIN(0);
		return word;
	}

\#include[ \t]+\"[^\"]+\" {
		char fnamebuf[1024],*p1,*p2;
		int glob_ret;
		glob_t globbuf;        /* the current globbuf */
		int globbuf_pos = -1;   /* where we are in the current globbuf */
		globbuf.gl_offs = 0;	/* initialize it to silence gcc */
		
		p1 = strchr(yytext,'"');
		p2 = strrchr(yytext,'"');
		if ( include_stack_index >= MAX_INCLUDE_DEPTH ) {
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Includes nested too deeply! Wow!!! How did you do that?\n", my_file, my_lineno, my_col);
		} else if ( (int)(p2-p1) > sizeof(fnamebuf) - 1 ) {
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Filename is incredibly way too long (%d chars!). Inclusion ignored!\n", my_file, my_lineno, my_col, yyleng - 10);
		} else {
			strncpy(fnamebuf, p1+1, p2-p1-1);
			fnamebuf[p2-p1-1] = 0;
		if (fnamebuf[0] != '/') {
		   char fnamebuf2[1024];
		   snprintf(fnamebuf2,sizeof(fnamebuf2), "%s/%s", (char *)ast_config_AST_CONFIG_DIR, fnamebuf);
		   ast_copy_string(fnamebuf,fnamebuf2,sizeof(fnamebuf));
		}
#ifdef SOLARIS
			glob_ret = glob(fnamebuf, GLOB_NOCHECK, NULL, &globbuf);
#else
			glob_ret = glob(fnamebuf, GLOB_NOMAGIC|GLOB_BRACE, NULL, &globbuf);
#endif
			if (glob_ret == GLOB_NOSPACE) {
				ast_log(LOG_WARNING,
					"Glob Expansion of pattern '%s' failed: Not enough memory\n", fnamebuf);
			} else if (glob_ret  == GLOB_ABORTED) {
				ast_log(LOG_WARNING,
					"Glob Expansion of pattern '%s' failed: Read error\n", fnamebuf);
			} else if (glob_ret  == GLOB_NOMATCH) {
				ast_log(LOG_WARNING,
					"Glob Expansion of pattern '%s' failed: No matches!\n", fnamebuf);
			} else {
			  globbuf_pos = 0;
			}
		}
		if (globbuf_pos > -1) {
			setup_filestack(fnamebuf, sizeof(fnamebuf), &globbuf, 0, yyscanner, 1);
		}
	}


<<EOF>>		{
		char fnamebuf[2048];
		if (include_stack_index > 0 && include_stack[include_stack_index-1].globbuf_pos < include_stack[include_stack_index-1].globbuf.gl_pathc-1) {
			yy_delete_buffer( YY_CURRENT_BUFFER, yyscanner );
			include_stack[include_stack_index-1].globbuf_pos++;
			setup_filestack(fnamebuf, sizeof(fnamebuf), &include_stack[include_stack_index-1].globbuf, include_stack[include_stack_index-1].globbuf_pos, yyscanner, 0);
			/* finish this */			
			
		} else {
			if (include_stack[include_stack_index].fname) {
				free(include_stack[include_stack_index].fname);
				include_stack[include_stack_index].fname = 0;
			}
			if (my_file) {
				free(my_file);
				my_file = 0;
			}
			if ( --include_stack_index < 0 ) {
				yyterminate();
			} else {
				globfree(&include_stack[include_stack_index].globbuf);
				include_stack[include_stack_index].globbuf_pos = -1;
				
				yy_delete_buffer( YY_CURRENT_BUFFER, yyscanner );
				yy_switch_to_buffer(include_stack[include_stack_index].bufstate, yyscanner );
				my_lineno = include_stack[include_stack_index].lineno;
				my_col    = include_stack[include_stack_index].colno;
				my_file   = strdup(include_stack[include_stack_index].fname);
			}
		}
	}

<*>.|\n		{ /* default rule */ ast_log(LOG_ERROR,"Unhandled char(s): %s\n", yytext); }

%%

static void pbcpush(char x)
{
	pbcstack[pbcpos++] = x;
}

void ael_yyfree(void *ptr, yyscan_t yyscanner)
{
	if (ptr)
		free( (char*) ptr );
}

static int pbcpop(char x)
{
	if (   ( x == ')' && pbcstack[pbcpos-1] == '(' )
		|| ( x == ']' && pbcstack[pbcpos-1] == '[' )
		|| ( x == '}' && pbcstack[pbcpos-1] == '{' )) {
		pbcpos--;
		return 0;
	}
	return 1; /* error */
}

static void pbcpush2(char x)
{
	pbcstack2[pbcpos2++] = x;
}

static int pbcpop2(char x)
{
	if (   ( x == ')' && pbcstack2[pbcpos2-1] == '(' )
		|| ( x == ']' && pbcstack2[pbcpos2-1] == '[' )
		|| ( x == '}' && pbcstack2[pbcpos2-1] == '{' )) {
		pbcpos2--;
		return 0;
	}
	return 1; /* error */
}

static void pbcpush3(char x)
{
	pbcstack3[pbcpos3++] = x;
}

static int pbcpop3(char x)
{
	if (   ( x == ')' && pbcstack3[pbcpos3-1] == '(' )
		|| ( x == ']' && pbcstack3[pbcpos3-1] == '[' )
		|| ( x == '}' && pbcstack3[pbcpos3-1] == '{' )) {
		pbcpos3--;
		return 0;
	}
	return 1; /* error */
}

static int c_prevword(void)
{
	char *c = prev_word;
	if (c == NULL)
		return 0;
	while ( *c ) {
		switch (*c) {
		case '{':
		case '[':
		case '(':
			pbcpush(*c);
			break;
		case '}':
		case ']':
		case ')':
			if (pbcpop(*c))
				return 1;
			break;
		}
		c++;
	}
	return 0;
}


/*
 * The following three functions, reset_*, are used in the bison
 * code to switch context. As a consequence, we need to
 * declare them global and add a prototype so that the
 * compiler does not complain.
 *
 * NOTE: yyg is declared because it is used in the BEGIN macros,
 * though that should be hidden as the macro changes
 * depending on the flex options that we use - in particular,
 * %reentrant changes the way the macro is declared;
 * without %reentrant, BEGIN uses yystart instead of yyg
 */

void reset_parencount(yyscan_t yyscanner );
void reset_parencount(yyscan_t yyscanner )
{
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	parencount = 0;
	pbcpos = 0;
	pbcpush('(');	/* push '(' so the last pcbpop (parencount= -1) will succeed */
	c_prevword();
	BEGIN(paren);
}

void reset_semicount(yyscan_t yyscanner );
void reset_semicount(yyscan_t yyscanner )
{
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	pbcpos = 0;
	BEGIN(semic);
}

void reset_argcount(yyscan_t yyscanner );
void reset_argcount(yyscan_t yyscanner )
{
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	parencount = 0;
	pbcpos = 0;
	pbcpush('(');	/* push '(' so the last pcbpop (parencount= -1) will succeed */
	c_prevword();
	BEGIN(argg);
}

/* used elsewhere, but some local vars */
struct pval *ael2_parse(char *filename, int *errors)
{
	struct pval *pvalue;
	struct parse_io *io;
	char *buffer;
	struct stat stats;
	FILE *fin;

	/* extern int ael_yydebug; */

	io = calloc(sizeof(struct parse_io),1);
	/* reset the global counters */
	prev_word = 0;
	my_lineno = 1;
	include_stack_index=0;
	my_col = 0;
	/* ael_yydebug = 1; */
	ael_yylex_init(&io->scanner);
	fin = fopen(filename,"r");
	if ( !fin ) {
		ast_log(LOG_ERROR,"File %s could not be opened\n", filename);
		*errors = 1;
		return 0;
	}
	if (my_file)
		free(my_file);
	my_file = strdup(filename);
	if (stat(filename, &stats)) {
		ast_log(LOG_WARNING, "failed to populate stats from file '%s'\n", filename);
	}
	buffer = (char*)malloc(stats.st_size+2);
	if (fread(buffer, 1, stats.st_size, fin) != stats.st_size) {
		ast_log(LOG_ERROR, "fread() failed: %s\n", strerror(errno));
	}			
	buffer[stats.st_size]=0;
	fclose(fin);

	ael_yy_scan_string (buffer ,io->scanner);
	ael_yyset_lineno(1 , io->scanner);

	/* ael_yyset_in (fin , io->scanner);	OLD WAY */

	ael_yyparse(io);


	pvalue = io->pval;
	*errors = io->syntax_error_count;

	ael_yylex_destroy(io->scanner);
	free(buffer);
	free(io);

	return pvalue;
}

static void setup_filestack(char *fnamebuf2, int fnamebuf_siz, glob_t *globbuf, int globpos, yyscan_t yyscanner, int create)
{
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	int error, i;
	FILE *in1;
	char fnamebuf[2048];

	if (globbuf && globbuf->gl_pathv && globbuf->gl_pathc > 0)
#if defined(STANDALONE) || defined(LOW_MEMORY) || defined(STANDALONE_AEL)
			strncpy(fnamebuf, globbuf->gl_pathv[globpos], fnamebuf_siz);
#else
			ast_copy_string(fnamebuf, globbuf->gl_pathv[globpos], fnamebuf_siz);
#endif
	else {
		ast_log(LOG_ERROR,"Include file name not present!\n");
		return;
	}
	for (i=0; i<include_stack_index; i++) {
		if ( !strcmp(fnamebuf,include_stack[i].fname )) {
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Nice Try!!! But %s has already been included (perhaps by another file), and would cause an infinite loop of file inclusions!!! Include directive ignored\n",
				my_file, my_lineno, my_col, fnamebuf);
			break;
		}
	}
	error = 1;
	if (i == include_stack_index)
		error = 0;	/* we can use this file */
	if ( !error ) {	/* valid file name */
		/* relative vs. absolute */
		if (fnamebuf[0] != '/')
			snprintf(fnamebuf2, fnamebuf_siz, "%s/%s", ast_config_AST_CONFIG_DIR, fnamebuf);
		else
#if defined(STANDALONE) || defined(LOW_MEMORY) || defined(STANDALONE_AEL)
			strncpy(fnamebuf2, fnamebuf, fnamebuf_siz);
#else
			ast_copy_string(fnamebuf2, fnamebuf, fnamebuf_siz);
#endif
		in1 = fopen( fnamebuf2, "r" );

		if ( ! in1 ) {
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Couldn't find the include file: %s; ignoring the Include directive!\n", my_file, my_lineno, my_col, fnamebuf2);
		} else {
			char *buffer;
			struct stat stats;
			if (stat(fnamebuf2, &stats)) {
				ast_log(LOG_WARNING, "Failed to populate stats from file '%s'\n", fnamebuf2);
			}
			buffer = (char*)malloc(stats.st_size+1);
			if (fread(buffer, 1, stats.st_size, in1) != stats.st_size) {
				ast_log(LOG_ERROR, "fread() failed: %s\n", strerror(errno));
			}			
			buffer[stats.st_size] = 0;
			ast_debug(1, "  --Read in included file %s, %d chars\n",fnamebuf2, (int)stats.st_size);
			fclose(in1);
			if (include_stack[include_stack_index].fname) {
			   	free(include_stack[include_stack_index].fname);
				include_stack[include_stack_index].fname = 0;
			}
			include_stack[include_stack_index].fname = strdup(S_OR(my_file, "<none>"));
			include_stack[include_stack_index].lineno = my_lineno;
			include_stack[include_stack_index].colno = my_col+yyleng;
			if (my_file)
				free(my_file);
			my_file = strdup(fnamebuf2);
			if (create)
				include_stack[include_stack_index].globbuf = *globbuf;

			include_stack[include_stack_index].globbuf_pos = 0;

			include_stack[include_stack_index].bufstate = YY_CURRENT_BUFFER;
			if (create)
				include_stack_index++;
			yy_switch_to_buffer(ael_yy_scan_string (buffer ,yyscanner),yyscanner);
			free(buffer);
			my_lineno = 1;
			my_col = 1;
			BEGIN(INITIAL);
		}
	}
}
