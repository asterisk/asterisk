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
 * \brief Flex scanner description of tokens used in AEL2 .
 *
 */#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "asterisk.h"
#include "asterisk/logger.h"
#include "ael/ael.tab.h"
#include "asterisk/ael_structs.h"

static char pbcstack[400];
static int pbcpos = 0;

static int parencount = 0;
static int commaout = 0;
int my_lineno = 1;
int my_col = 0;
char *my_file = 0;
char *prev_word;
#define MAX_INCLUDE_DEPTH 50

void reset_parencount(yyscan_t yyscanner );
void reset_semicount(yyscan_t yyscanner );
void reset_argcount(yyscan_t yyscanner );
struct pval *ael2_parse(char *filename, int *errors);
int ael_yyget_column  (yyscan_t yyscanner);
void ael_yyset_column (int  column_no , yyscan_t yyscanner);
int ael_yyparse (struct parse_io *);
static void pbcpush(char x);
static int pbcpop(char x);
static void pbcwhere(char *text, int *line, int *col );
static int c_prevword(void);

struct stackelement {
	char *fname;
	int lineno;
	int colno;
	YY_BUFFER_STATE bufstate;
};
struct stackelement  include_stack[MAX_INCLUDE_DEPTH];
int include_stack_index = 0;

%}

%x paren semic argg
%option prefix="ael_yy"
%option batch
%option outfile="ael_lex.c"
%option reentrant
%option bison-bridge
%option bison-locations
/* %option yylineno I've tried hard, but haven't been able to use this */
%option noyywrap

%%
\{	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return LC;}
\}	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return RC;}
\(	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return LP;}
\)	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return RP;}
\;	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return SEMI;}
\=	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return EQ;}
\,	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return COMMA;}
\:	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return COLON;}
\&	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return AMPER;}
\|	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return BAR;}
\=\>	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return EXTENMARK;}
\@	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return AT;}
\/\/[^\n]*  {/*comment*/}
context	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_CONTEXT;}
abstract	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_ABSTRACT;}
macro	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_MACRO;};
globals	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_GLOBALS;}
ignorepat	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_IGNOREPAT;}
switch	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_SWITCH;}
if	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_IF;}
ifTime	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_IFTIME;}
random	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_RANDOM;}
regexten	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_REGEXTEN;}
hint	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_HINT;}
else	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_ELSE;}
goto	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_GOTO;}
jump	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_JUMP;}
return	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_RETURN;}
break	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_BREAK;}
continue	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_CONTINUE;}
for	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_FOR;}
while	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_WHILE;}
case	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_CASE;}
default	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_DEFAULT;}
pattern	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_PATTERN;}
catch	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_CATCH;}
switches	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_SWITCHES;}
eswitches	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_ESWITCHES;}
includes	{yylloc->first_line = yylloc->last_line = my_lineno; yylloc->last_column=my_col+yyleng-1; yylloc->first_column=my_col;my_col+=yyleng;return KW_INCLUDES;}

\n		{my_lineno++;my_col=0;}
[ ]+	{/* nothing */ my_col+=yyleng;}
[	]+	{/* nothing */ int wid = 8-(my_col%8); my_col+=wid;}

[-a-zA-Z0-9'"_/.\<\>\*\+!$#\[\]][-a-zA-Z0-9'"_/.!\*\+\<\>\{\}$#\[\]]*	{
		yylloc->first_line = yylloc->last_line = my_lineno;
		yylloc->last_column=my_col+yyleng-1;
		yylloc->first_column=my_col; /* set up the ptr */
		my_col+=yyleng;
		yylval->str = strdup(yytext);
		/* printf("\nGot WORD %s[%d][%d:%d]\n", yylval->str, my_lineno ,yylloc->first_column,yylloc->last_column );  */
		prev_word = yylval->str;
		return word;
	}

<paren>[^()\[\]\{\}]*\)	{
		yylloc->first_line = my_lineno;
		yylloc->first_column=my_col;
		if ( pbcpop(')') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ')' in expression: %s !\n", my_file, my_lineno+l4, c4, yytext);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			prev_word = 0;
			return word;
		}
		parencount--;
		if ( parencount >= 0) {
			yymore();
		} else {
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=c4;
			yylval->str = strdup(yytext);
			*(yylval->str+strlen(yylval->str)-1)=0;
			/* printf("Got paren word %s\n", yylval->str); */
			unput(')');
			my_col=c4;
			my_lineno += l4;
			BEGIN(0);
			return word;
		}
	}

<paren>[^()\[\]\{\}]*\(	{
		yylloc->first_line = my_lineno; yylloc->first_column=my_col;
		parencount++;
		pbcpush('(');
		yymore();
	}

<paren>[^()\[\]\{\}]*\[	{yylloc->first_line = my_lineno;yylloc->first_column=my_col; yymore(); pbcpush('['); }

<paren>[^()\[\]\{\}]*\]	{
		yylloc->first_line = my_lineno;yylloc->first_column=my_col;
		if ( pbcpop(']') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ']' in expression!\n", my_file, my_lineno+l4, c4);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			return word;
		}
		yymore();
	}

<paren>[^()\[\]\{\}]*\{	{yylloc->first_line = my_lineno;yylloc->first_column=my_col;  yymore(); pbcpush('{'); }

<paren>[^()\[\]\{\}]*\}	{
		yylloc->first_line = my_lineno;
		yylloc->first_column=my_col;
		if ( pbcpop('}') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched '}' in expression!\n", my_file, my_lineno+l4, c4);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			return word;
		}
		yymore();
	}

<argg>[^(),\{\}\[\]]*\)	{
		/* printf("ARGG:%s\n",yytext); */
		int linecount = 0;
		int colcount = my_col;
		char *pt = yytext;

		yylloc->first_line = my_lineno;
		yylloc->first_column=my_col;
		if ( pbcpop(')') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ')' in expression!\n", my_file, my_lineno+l4, c4);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			return word;
		}


		while (*pt) {
			if (*pt == '\n') {
				linecount++;
				colcount=0;
			}
			pt++;
			colcount++;
		}
		yylloc->last_line = my_lineno+linecount;
		yylloc->last_column=colcount;
		parencount--;
		if( parencount >= 0){
			yymore();
		} else {
			yylval->str = strdup(yytext);
			if(yyleng > 1 )
				*(yylval->str+yyleng-1)=0;
			/* printf("Got argg word '%s'\n", yylval->str);  */
			BEGIN(0);
			if ( !strcmp(yylval->str,")") ) {
				free(yylval->str);
				yylval->str = 0;
				my_col+=1;
				return RP;
			} else {
				unput(')');
				my_col=colcount;
				my_lineno+=linecount;
				return word;
			}
		}
	}

<argg>[^(),\{\}\[\]]*\(	  {
		/* printf("ARGG:%s\n",yytext); */
		/* printf("GOT AN LP!!!\n"); */
		yylloc->first_line = my_lineno;
		yylloc->first_column=my_col;
		parencount++;
		pbcpush('(');
		yymore();
	}

<argg>[^(),\{\}\[\]]*\,	{
		/* printf("ARGG:%s\n",yytext); */
		if( parencount != 0) {
			/* printf("Folding in a comma!\n"); */
			yymore();
		} else  {
			/* printf("got a comma!\n\n");  */
			int linecount = 0;
			int colcount = my_col;
			char *pt;

			pt = yytext;
			while (*pt) {
				if ( *pt == '\n' ) {
					linecount++;
					colcount=0;
				}
				pt++;
				colcount++;
			}
			yylloc->first_line = my_lineno;
			yylloc->last_line = my_lineno+linecount;
			yylloc->last_column=colcount;
			yylloc->first_column=my_col;
			if( !commaout ) {
				if( !strcmp(yytext,"," ) ) {
					commaout = 0;
					my_col+=1;
					return COMMA;
				}
				yylval->str = strdup(yytext); /* printf("Got argg2 word %s\n", yylval->str); */
				unput(',');
				commaout = 1;
				if(yyleng > 1 )
				*(yylval->str+yyleng-1)=0;
				my_lineno+=linecount;
				my_col=colcount;
				return word;
			} else {
				commaout = 0;
				my_col+=1;
				return COMMA;
			}
		}
	}

<argg>[^(),\{\}\[\]]*\{	{
		/*printf("ARGG:%s\n",yytext);*/
		yylloc->first_line = my_lineno;
		yylloc->first_column=my_col;
		pbcpush('{'); yymore();
	}

<argg>[^(),\{\}\[\]]*\}	{
		/*printf("ARGG:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col;
		if ( pbcpop('}') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched '}' in expression!\n", my_file, my_lineno+l4, my_col+c4);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=my_col+c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			return word;
		}
		yymore();
	}

<argg>[^(),\{\}\[\]]*\[	{/*printf("ARGG:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col; yymore(); pbcpush('['); }

<argg>[^(),\{\}\[\]]*\]	{/*printf("ARGG:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col;
		if ( pbcpop(']') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ']' in expression!\n", my_file, my_lineno+l4, c4);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			return word;
		}
		yymore();
	}

<semic>[^;()\{\}\[\]]*\[	{/*printf("SEMIC:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col; yymore(); pbcpush('['); }

<semic>[^;()\{\}\[\]]*\]	{/*printf("SEMIC:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col;
		if ( pbcpop(']') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ']' in expression!\n", my_file, my_lineno+l4, c4);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			return word;
		}
		yymore();
	}

<semic>[^;()\{\}\[\]]*\{	{/*printf("SEMIC:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col; yymore(); pbcpush('{');}

<semic>[^;()\{\}\[\]]*\}	{/*printf("SEMIC:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col;
		if ( pbcpop('}') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched '}' in expression!\n", my_file, my_lineno+l4, my_col+c4);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=my_col+c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			return word;
		}
		yymore();
	}

<semic>[^;()\{\}\[\]]*\(	{/*printf("SEMIC:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col; yymore(); pbcpush('(');}

<semic>[^;()\{\}\[\]]*\)	{/*printf("SEMIC:%s\n",yytext);*/yylloc->first_line = my_lineno;yylloc->first_column=my_col;
		if ( pbcpop(')') ) {
			/* error */
			int l4,c4;
			pbcwhere(yytext, &l4, &c4);
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Mismatched ')' in expression!\n", my_file, my_lineno+l4, my_col+c4);
			BEGIN(0);
			yylloc->last_line = my_lineno+l4;
			yylloc->last_column=my_col+c4;
			my_col=c4;
			my_lineno += l4;
			yylval->str = strdup(yytext);
			return word;
		}
		yymore();
	}

<semic>[^;()\{\}\[\]]*;	{
		int linecount = 0;
		int colcount = my_col;
		char *pt = yytext;
		while (*pt) {
			if ( *pt == '\n' ) {
				linecount++;
				colcount=0;
			}
			pt++;
			colcount++;
		}
		yylloc->first_line = my_lineno;
		yylloc->last_line = my_lineno+linecount;
		yylloc->last_column=colcount;
		yylloc->first_column=my_col;
		yylval->str = strdup(yytext);
		if(yyleng > 1)
			*(yylval->str+yyleng-1)=0;
		/* printf("Got semic word %s\n", yylval->str); */
		unput(';');
		BEGIN(0);
		my_col=colcount;
		my_lineno += linecount;
		return word;
	}

\#include[ \t]+\"[^\"]+\" {
		FILE *in1;
		char fnamebuf[1024],*p1,*p2;
		if ( include_stack_index >= MAX_INCLUDE_DEPTH ) {
			ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Includes nested too deeply! Wow!!! How did you do that?\n", my_file, my_lineno, my_col);
		} else {
			p1 = strchr(yytext,'"');
			p2 = strrchr(yytext,'"');
			if ( (int)(p2-p1) > 1023 ) {
				ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Filename is incredibly way too long (%d chars!). Inclusion ignored!\n", my_file, my_lineno, my_col, yyleng - 10);
			} else {
				int i;
				int found = 0;
				strncpy(fnamebuf,p1,p2-p1);
				fnamebuf[p2-p1] = 0;
				for (i=0; i<include_stack_index; i++) {
					if ( !strcmp(fnamebuf,include_stack[i].fname )) {
						ast_log(LOG_ERROR,"File=%s, line=%d, column=%d: Nice Try!!! But %s has already been included (perhaps by another file), and would cause an infinite loop of file inclusions!!! Include directive ignored\n",
							my_file, my_lineno, my_col, fnamebuf);
						found=1;
						break;
					}
				}
				if ( !found ) {
					*p2 = 0;
					/* relative vs. absolute */
					if ( *(p1+1) != '/' ) {
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
	int ret = 0;
	while ( c && *c ) {
		switch (*c) {
		case '{': pbcpush('{');break;
		case '}': ret = pbcpop('}');break;
		case '[':pbcpush('[');break;
		case ']':ret = pbcpop(']');break;
		case '(':pbcpush('(');break;
		case ')':ret = pbcpop(')'); break;
		}
		if( ret )
			return 1;
		c++;
	}
	return 0;
}

static void pbcwhere(char *text, int *line, int *col )
{
	int loc_line = 0;
	int loc_col = 0;
	while ( *text ) {
		if ( *text == '\n' ) {
			loc_line++;
			loc_col = 1;
		} else {
			loc_col++;
		}
		text++;
	}
	*line = loc_line;
	*col = loc_col;
}

void reset_parencount(yyscan_t yyscanner )
{
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	parencount = 0;
	pbcpos = 0;
	pbcpush('(');
	c_prevword();
	BEGIN(paren);
}

void reset_semicount(yyscan_t yyscanner )
{
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	pbcpos = 0;
	BEGIN(semic);
}

void reset_argcount(yyscan_t yyscanner )
{
	struct yyguts_t * yyg = (struct yyguts_t*)yyscanner;
	parencount = 0;
	pbcpos = 0;
	commaout = 0;
	pbcpush('(');
	c_prevword();
	BEGIN(argg);
}


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
