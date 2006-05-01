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
%x paren semic argg

/* prefix used for various globally-visible functions and variables.
 * This renames also yywrap, but since we do not use it, we just
 * add option noyywrap to remove it.
 */
%option prefix="ael_yy"
%option noyywrap

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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "asterisk.h"
#include "asterisk/logger.h"
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
	YY_BUFFER_STATE bufstate;
};

static struct stackelement  include_stack[MAX_INCLUDE_DEPTH];
static int include_stack_index = 0;

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


NOPARENS	[^()\[\]\{\}]*

NOARGG		[^(),\{\}\[\]]*

NOSEMIC		[^;()\{\}\[\]]*

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
macro		{ STORE_POS; return KW_MACRO;};
globals		{ STORE_POS; return KW_GLOBALS;}
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

\n		{ my_lineno++; my_col = 1; }
[ ]+		{ my_col += yyleng; }
[\t]+		{ my_col += (yyleng*8)-(my_col%8); }

[-a-zA-Z0-9'"_/.\<\>\*\+!$#\[\]][-a-zA-Z0-9'"_/.!\*\+\<\>\{\}$#\[\]]*	{
		STORE_POS;
		yylval->str = strdup(yytext);
		prev_word = yylval->str;
		return word;
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
			yylval->str = strdup(yytext);
			prev_word = 0;
			return word;
		}
		parencount--;
		if ( parencount >= 0) {
			yymore();
		} else {
			STORE_LOC;
			yylval->str = strdup(yytext);
			yylval->str[yyleng-1] = '\0'; /* trim trailing ')' */
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
			yylval->str = strdup(yytext);
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
			yylval->str = strdup(yytext);
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
			yylval->str = strdup(yytext);
			yylval->str[yyleng-1] = '\0'; /* trim trailing ')' */
			unput(')');
			return word;
		}
	}

<argg>{NOARGG}\,	{
		if( parencount != 0) { /* printf("Folding in a comma!\n"); */
			yymore();
		} else  {
			STORE_LOC;
			if( !strcmp(yytext,"," ) )
				return COMMA;
			yylval->str = strdup(yytext);
			yylval->str[yyleng-1] = '\0';
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
			yylval->str = strdup(yytext);
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
			yylval->str = strdup(yytext);
			return word;
		}
		yymore();
	}

<semic>{NOSEMIC};	{
		STORE_LOC;
		yylval->str = strdup(yytext);
		yylval->str[yyleng-1] = '\0';
		unput(';');
		BEGIN(0);
		return word;
	}

\#include[ \t]+\"[^\"]+\" {
		FILE *in1;
		char fnamebuf[1024],*p1,*p2;
		int error = 1;	/* don't use the file if set */
		p1 = strchr(yytext,'"');
		p2 = strrchr(yytext,'"');
		if ( include_stack_index >= MAX_INCLUDE_DEPTH ) {
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Includes nested too deeply! Wow!!! How did you do that?\n", my_file, my_lineno, my_col);
		} else if ( (int)(p2-p1) > sizeof(fnamebuf) - 1 ) {
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Filename is incredibly way too long (%d chars!). Inclusion ignored!\n", my_file, my_lineno, my_col, yyleng - 10);
		} else {
			int i;
			strncpy(fnamebuf, p1, p2-p1);
			fnamebuf[p2-p1] = 0;
			for (i=0; i<include_stack_index; i++) {
				if ( !strcmp(fnamebuf,include_stack[i].fname )) {
					ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Nice Try!!! But %s has already been included (perhaps by another file), and would cause an infinite loop of file inclusions!!! Include directive ignored\n",
						my_file, my_lineno, my_col, fnamebuf);
					break;
				}
			}
			if (i == include_stack_index)
				error = 0;	/* we can use this file */
		}
		if ( !error ) {	/* valid file name */
			*p2 = 0;
			/* relative vs. absolute */
			if ( *(p1+1) != '/' ) {
				/* XXX must check overflows */
				strcpy(fnamebuf,ast_config_AST_CONFIG_DIR);
				strcat(fnamebuf,"/");
				strcat(fnamebuf,p1+1);
			} else
				strcpy(fnamebuf,p1+1);
			in1 = fopen( fnamebuf, "r" );
			if ( ! in1 ) {
				ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Couldn't find the include file: %s; ignoring the Include directive!\n", my_file, my_lineno, my_col, fnamebuf);
			} else {
				char *buffer;
				struct stat stats;
				stat(fnamebuf, &stats);
				buffer = (char*)malloc(stats.st_size+1);
				fread(buffer, 1, stats.st_size, in1);
				buffer[stats.st_size] = 0;
				ast_log(LOG_NOTICE,"  --Read in included file %s, %d chars\n",fnamebuf, (int)stats.st_size);
				fclose(in1);

				include_stack[include_stack_index].fname = my_file;
				my_file = strdup(fnamebuf);
				include_stack[include_stack_index].lineno = my_lineno;
				include_stack[include_stack_index].colno = my_col+yyleng;
				include_stack[include_stack_index++].bufstate = YY_CURRENT_BUFFER;

				yy_switch_to_buffer(ael_yy_scan_string (buffer ,yyscanner),yyscanner);
				free(buffer);
				my_lineno = 1;
				my_col = 1;
				BEGIN(INITIAL);
			}
		}
	}

<<EOF>>		{
		if ( --include_stack_index < 0 ) {
			yyterminate();
		} else {
			free(my_file);
			yy_delete_buffer( YY_CURRENT_BUFFER, yyscanner );
			yy_switch_to_buffer(include_stack[include_stack_index].bufstate, yyscanner );
			my_lineno = include_stack[include_stack_index].lineno;
			my_col    = include_stack[include_stack_index].colno;
			my_file   = include_stack[include_stack_index].fname;
		}
	}

%%

static void pbcpush(char x)
{
	pbcstack[pbcpos++] = x;
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
	struct pval *pval;
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
	my_file = strdup(filename);
	stat(filename, &stats);
	buffer = (char*)malloc(stats.st_size+2);
	fread(buffer, 1, stats.st_size, fin);
	buffer[stats.st_size]=0;
	fclose(fin);

	ael_yy_scan_string (buffer ,io->scanner);
	ael_yyset_lineno(1 , io->scanner);

	/* ael_yyset_in (fin , io->scanner);	OLD WAY */

	ael_yyparse(io);


	pval = io->pval;
	*errors = io->syntax_error_count;

	ael_yylex_destroy(io->scanner);
	free(buffer);
	free(io);

	return pval;
}
