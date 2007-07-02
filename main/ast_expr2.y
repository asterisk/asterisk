%{
/* Written by Pace Willisson (pace@blitz.com) 
 * and placed in the public domain.
 *
 * Largely rewritten by J.T. Conklin (jtc@wimsey.com)
 *
 * And then overhauled twice by Steve Murphy (murf@digium.com)
 * to add double-quoted strings, allow mult. spaces, improve
 * error messages, and then to fold in a flex scanner for the 
 * yylex operation.
 *
 * $FreeBSD: src/bin/expr/expr.y,v 1.16 2000/07/22 10:59:36 se Exp $
 */

#include <sys/types.h>
#include <stdio.h>

#ifdef STANDALONE /* I guess somewhere, the feature is set in the asterisk includes */
#ifndef __USE_ISOC99
#define __USE_ISOC99 1
#endif
#endif

#ifdef __USE_ISOC99
#define FP___PRINTF "%.16Lg"
#define FP___FMOD   fmodl
#define FP___STRTOD  strtold
#define FP___TYPE    long double
#else
#define FP___PRINTF "%.8g"
#define FP___FMOD   fmod
#define FP___STRTOD  strtod
#define FP___TYPE    double
#endif

#include <stdlib.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#include <math.h>
#include <locale.h>
#include <unistd.h>
#include <ctype.h>
#if !defined(SOLARIS) && !defined(__CYGWIN__)
	/* #include <err.h> */
#else
#define quad_t int64_t
#endif
#include <errno.h>
#include <regex.h>
#include <limits.h>

#include "asterisk.h"
#include "asterisk/ast_expr.h"
#include "asterisk/logger.h"

#if defined(LONG_LONG_MIN) && !defined(QUAD_MIN)
#define QUAD_MIN LONG_LONG_MIN
#endif
#if defined(LONG_LONG_MAX) && !defined(QUAD_MAX)
#define QUAD_MAX LONG_LONG_MAX
#endif

#  if ! defined(QUAD_MIN)
#   define QUAD_MIN     (-0x7fffffffffffffffLL-1)
#  endif
#  if ! defined(QUAD_MAX)
#   define QUAD_MAX     (0x7fffffffffffffffLL)
#  endif
#define YYENABLE_NLS 0
#define YYPARSE_PARAM parseio
#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner
#define YYERROR_VERBOSE 1
extern char extra_error_message[4095];
extern int extra_error_message_supplied;

enum valtype {
	AST_EXPR_number, AST_EXPR_numeric_string, AST_EXPR_string
} ;

#ifdef STANDALONE
void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...) __attribute__ ((format (printf,5,6)));
#endif

struct val {
	enum valtype type;
	union {
		char *s;
		FP___TYPE i; /* either long double, or just double, on a bad day */
	} u;
} ;

typedef void *yyscan_t;

struct parse_io
{
	char *string;
	struct val *val;
	yyscan_t scanner;
};
 
static int		chk_div __P((FP___TYPE, FP___TYPE));
static int		chk_minus __P((FP___TYPE, FP___TYPE, FP___TYPE));
static int		chk_plus __P((FP___TYPE, FP___TYPE, FP___TYPE));
static int		chk_times __P((FP___TYPE, FP___TYPE, FP___TYPE));
static void		free_value __P((struct val *));
static int		is_zero_or_null __P((struct val *));
static int		isstring __P((struct val *));
static struct val	*make_number __P((FP___TYPE));
static struct val	*make_str __P((const char *));
static struct val	*op_and __P((struct val *, struct val *));
static struct val	*op_colon __P((struct val *, struct val *));
static struct val	*op_eqtilde __P((struct val *, struct val *));
static struct val	*op_div __P((struct val *, struct val *));
static struct val	*op_eq __P((struct val *, struct val *));
static struct val	*op_ge __P((struct val *, struct val *));
static struct val	*op_gt __P((struct val *, struct val *));
static struct val	*op_le __P((struct val *, struct val *));
static struct val	*op_lt __P((struct val *, struct val *));
static struct val	*op_cond __P((struct val *, struct val *, struct val *));
static struct val	*op_minus __P((struct val *, struct val *));
static struct val	*op_negate __P((struct val *));
static struct val	*op_compl __P((struct val *));
static struct val	*op_ne __P((struct val *, struct val *));
static struct val	*op_or __P((struct val *, struct val *));
static struct val	*op_plus __P((struct val *, struct val *));
static struct val	*op_rem __P((struct val *, struct val *));
static struct val	*op_times __P((struct val *, struct val *));
static int		to_number __P((struct val *));
static void		to_string __P((struct val *));

/* uh, if I want to predeclare yylex with a YYLTYPE, I have to predeclare the yyltype... sigh */
typedef struct yyltype
{
  int first_line;
  int first_column;

  int last_line;
  int last_column;
} yyltype;

# define YYLTYPE yyltype
# define YYLTYPE_IS_TRIVIAL 1

/* we will get warning about no prototype for yylex! But we can't
   define it here, we have no definition yet for YYSTYPE. */

int		ast_yyerror(const char *,YYLTYPE *, struct parse_io *);
 
/* I wanted to add args to the yyerror routine, so I could print out
   some useful info about the error. Not as easy as it looks, but it
   is possible. */
#define ast_yyerror(x) ast_yyerror(x,&yyloc,parseio)
#define DESTROY(x) {if((x)->type == AST_EXPR_numeric_string || (x)->type == AST_EXPR_string) free((x)->u.s); (x)->u.s = 0; free(x);}
%}
 
%pure-parser
%locations
/* %debug  for when you are having big problems */

/* %name-prefix="ast_yy" */

%union
{
	struct val *val;
}

%{
extern int		ast_yylex __P((YYSTYPE *, YYLTYPE *, yyscan_t));
%}
%left <val> TOK_COND TOK_COLONCOLON
%left <val> TOK_OR
%left <val> TOK_AND
%left <val> TOK_EQ TOK_GT TOK_LT TOK_GE TOK_LE TOK_NE
%left <val> TOK_PLUS TOK_MINUS
%left <val> TOK_MULT TOK_DIV TOK_MOD
%right <val> TOK_COMPL
%left <val> TOK_COLON TOK_EQTILDE
%left <val> TOK_RP TOK_LP


%token <val> TOKEN
%type <val> start expr


%destructor {  free_value($$); }  expr TOKEN TOK_COND TOK_COLONCOLON TOK_OR TOK_AND TOK_EQ 
                                 TOK_GT TOK_LT TOK_GE TOK_LE TOK_NE TOK_PLUS TOK_MINUS TOK_MULT TOK_DIV TOK_MOD TOK_COMPL TOK_COLON TOK_EQTILDE 
                                 TOK_RP TOK_LP

%%

start: expr { ((struct parse_io *)parseio)->val = (struct val *)calloc(sizeof(struct val),1);
              ((struct parse_io *)parseio)->val->type = $1->type;
              if( $1->type == AST_EXPR_number )
				  ((struct parse_io *)parseio)->val->u.i = $1->u.i;
              else
				  ((struct parse_io *)parseio)->val->u.s = $1->u.s; 
			  free($1);
			}
	| {/* nothing */ ((struct parse_io *)parseio)->val = (struct val *)calloc(sizeof(struct val),1);
              ((struct parse_io *)parseio)->val->type = AST_EXPR_string;
			  ((struct parse_io *)parseio)->val->u.s = strdup(""); 
			}

	;

expr:	TOKEN   { $$= $1;}
	| TOK_LP expr TOK_RP { $$ = $2; 
	                       @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						   @$.first_line=0; @$.last_line=0;
							DESTROY($1); DESTROY($3); }
	| expr TOK_OR expr { $$ = op_or ($1, $3);
						DESTROY($2);	
                         @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						 @$.first_line=0; @$.last_line=0;}
	| expr TOK_AND expr { $$ = op_and ($1, $3); 
						DESTROY($2);	
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
                          @$.first_line=0; @$.last_line=0;}
	| expr TOK_EQ expr { $$ = op_eq ($1, $3);
						DESTROY($2);	
	                     @$.first_column = @1.first_column; @$.last_column = @3.last_column;
						 @$.first_line=0; @$.last_line=0;}
	| expr TOK_GT expr { $$ = op_gt ($1, $3);
						DESTROY($2);	
                         @$.first_column = @1.first_column; @$.last_column = @3.last_column;
						 @$.first_line=0; @$.last_line=0;}
	| expr TOK_LT expr { $$ = op_lt ($1, $3); 
						DESTROY($2);	
	                     @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						 @$.first_line=0; @$.last_line=0;}
	| expr TOK_GE expr  { $$ = op_ge ($1, $3); 
						DESTROY($2);	
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_LE expr  { $$ = op_le ($1, $3); 
						DESTROY($2);	
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_NE expr  { $$ = op_ne ($1, $3); 
						DESTROY($2);	
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_PLUS expr { $$ = op_plus ($1, $3); 
						DESTROY($2);	
	                       @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						   @$.first_line=0; @$.last_line=0;}
	| expr TOK_MINUS expr { $$ = op_minus ($1, $3); 
						DESTROY($2);	
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| TOK_MINUS expr %prec TOK_COMPL { $$ = op_negate ($2); 
						DESTROY($1);	
	                        @$.first_column = @1.first_column; @$.last_column = @2.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| TOK_COMPL expr   { $$ = op_compl ($2); 
						DESTROY($1);	
	                        @$.first_column = @1.first_column; @$.last_column = @2.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| expr TOK_MULT expr { $$ = op_times ($1, $3); 
						DESTROY($2);	
	                       @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						   @$.first_line=0; @$.last_line=0;}
	| expr TOK_DIV expr { $$ = op_div ($1, $3); 
						DESTROY($2);	
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_MOD expr { $$ = op_rem ($1, $3); 
						DESTROY($2);	
	                      @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
						  @$.first_line=0; @$.last_line=0;}
	| expr TOK_COLON expr { $$ = op_colon ($1, $3); 
						DESTROY($2);	
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| expr TOK_EQTILDE expr { $$ = op_eqtilde ($1, $3); 
						DESTROY($2);	
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	| expr TOK_COND expr TOK_COLONCOLON expr  { $$ = op_cond ($1, $3, $5); 
						DESTROY($2);	
						DESTROY($4);	
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	;

%%

static struct val *
make_number (FP___TYPE i)
{
	struct val *vp;

	vp = (struct val *) malloc (sizeof (*vp));
	if (vp == NULL) {
		ast_log(LOG_WARNING, "malloc() failed\n");
		return(NULL);
	}

	vp->type = AST_EXPR_number;
	vp->u.i  = i;
	return vp; 
}

static struct val *
make_str (const char *s)
{
	struct val *vp;
	size_t i;
	int isint; /* this started out being a test for an integer, but then ended up being a test for a float */

	vp = (struct val *) malloc (sizeof (*vp));
	if (vp == NULL || ((vp->u.s = strdup (s)) == NULL)) {
		ast_log(LOG_WARNING,"malloc() failed\n");
		return(NULL);
	}

	for (i = 0, isint = (isdigit(s[0]) || s[0] == '-' || s[0]=='.'); isint && i < strlen(s); i++)
	{
		if (!isdigit(s[i]) && s[i] != '.') {
			isint = 0;
			break;
		}
	}
	if (isint)
		vp->type = AST_EXPR_numeric_string;
	else	
		vp->type = AST_EXPR_string;

	return vp;
}


static void
free_value (struct val *vp)
{	
	if (vp==NULL) {
		return;
	}
	if (vp->type == AST_EXPR_string || vp->type == AST_EXPR_numeric_string)
		free (vp->u.s);	
	free(vp);
}


static int
to_number (struct val *vp)
{
	FP___TYPE i;
	
	if (vp == NULL) {
		ast_log(LOG_WARNING,"vp==NULL in to_number()\n");
		return(0);
	}

	if (vp->type == AST_EXPR_number)
		return 1;

	if (vp->type == AST_EXPR_string)
		return 0;

	/* vp->type == AST_EXPR_numeric_string, make it numeric */
	errno = 0;
	i  = FP___STRTOD(vp->u.s, (char**)0); /* either strtod, or strtold on a good day */
	if (errno != 0) {
		ast_log(LOG_WARNING,"Conversion of %s to number under/overflowed!\n", vp->u.s);
		free(vp->u.s);
		vp->u.s = 0;
		return(0);
	}
	free (vp->u.s);
	vp->u.i = i;
	vp->type = AST_EXPR_number;
	return 1;
}

static void
strip_quotes(struct val *vp)
{
	if (vp->type != AST_EXPR_string && vp->type != AST_EXPR_numeric_string)
		return;
	
	if( vp->u.s[0] == '"' && vp->u.s[strlen(vp->u.s)-1] == '"' )
	{
		char *f, *t;
		f = vp->u.s;
		t = vp->u.s;
		
		while( *f )
		{
			if( *f  && *f != '"' )
				*t++ = *f++;
			else
				f++;
		}
		*t = *f;
	}
}

static void
to_string (struct val *vp)
{
	char *tmp;

	if (vp->type == AST_EXPR_string || vp->type == AST_EXPR_numeric_string)
		return;

	tmp = malloc ((size_t)25);
	if (tmp == NULL) {
		ast_log(LOG_WARNING,"malloc() failed\n");
		return;
	}

	sprintf(tmp, FP___PRINTF, vp->u.i);
	vp->type = AST_EXPR_string;
	vp->u.s  = tmp;
}


static int
isstring (struct val *vp)
{
	/* only TRUE if this string is not a valid number */
	return (vp->type == AST_EXPR_string);
}


static int
is_zero_or_null (struct val *vp)
{
	if (vp->type == AST_EXPR_number) {
		return (vp->u.i == 0);
	} else {
		return (*vp->u.s == 0 || (to_number(vp) && vp->u.i == 0));
	}
	/* NOTREACHED */
}

#ifdef STANDALONE

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
	va_list vars;
	va_start(vars,fmt);
	
        printf("LOG: lev:%d file:%s  line:%d func: %s  ",
                   level, file, line, function);
	vprintf(fmt, vars);
	fflush(stdout);
	va_end(vars);
}


int main(int argc,char **argv) {
	char s[4096];
	char out[4096];
	FILE *infile;
	
	if( !argv[1] )
		exit(20);
	
	if( access(argv[1],F_OK)== 0 )
	{
		int ret;
		
		infile = fopen(argv[1],"r");
		if( !infile )
		{
			printf("Sorry, couldn't open %s for reading!\n", argv[1]);
			exit(10);
		}
		while( fgets(s,sizeof(s),infile) )
		{
			if( s[strlen(s)-1] == '\n' )
				s[strlen(s)-1] = 0;
			
			ret = ast_expr(s, out, sizeof(out));
			printf("Expression: %s    Result: [%d] '%s'\n",
				   s, ret, out);
		}
		fclose(infile);
	}
	else
	{
		if (ast_expr(argv[1], s, sizeof(s)))
			printf("=====%s======\n",s);
		else
			printf("No result\n");
	}
}

#endif

#undef ast_yyerror
#define ast_yyerror(x) ast_yyerror(x, YYLTYPE *yylloc, struct parse_io *parseio)

/* I put the ast_yyerror func in the flex input file,
   because it refers to the buffer state. Best to
   let it access the BUFFER stuff there and not trying
   define all the structs, macros etc. in this file! */


static struct val *
op_or (struct val *a, struct val *b)
{
	if (is_zero_or_null (a)) {
		free_value (a);
		return (b);
	} else {
		free_value (b);
		return (a);
	}
}
		
static struct val *
op_and (struct val *a, struct val *b)
{
	if (is_zero_or_null (a) || is_zero_or_null (b)) {
		free_value (a);
		free_value (b);
		return (make_number ((double)0.0));
	} else {
		free_value (b);
		return (a);
	}
}

static struct val *
op_eq (struct val *a, struct val *b)
{
	struct val *r; 

	if (isstring (a) || isstring (b)) {
		to_string (a);
		to_string (b);	
		r = make_number ((FP___TYPE)(strcoll (a->u.s, b->u.s) == 0));
	} else {
#ifdef DEBUG_FOR_CONVERSIONS
		char buffer[2000];
		sprintf(buffer,"Converting '%s' and '%s' ", a->u.s, b->u.s);
#endif
		(void)to_number(a);
		(void)to_number(b);
#ifdef DEBUG_FOR_CONVERSIONS
		ast_log(LOG_WARNING,"%s to '%lld' and '%lld'\n", buffer, a->u.i, b->u.i);
#endif
		r = make_number ((FP___TYPE)(a->u.i == b->u.i));
	}

	free_value (a);
	free_value (b);
	return r;
}

static struct val *
op_gt (struct val *a, struct val *b)
{
	struct val *r;

	if (isstring (a) || isstring (b)) {
		to_string (a);
		to_string (b);
		r = make_number ((FP___TYPE)(strcoll (a->u.s, b->u.s) > 0));
	} else {
		(void)to_number(a);
		(void)to_number(b);
		r = make_number ((FP___TYPE)(a->u.i > b->u.i));
	}

	free_value (a);
	free_value (b);
	return r;
}

static struct val *
op_lt (struct val *a, struct val *b)
{
	struct val *r;

	if (isstring (a) || isstring (b)) {
		to_string (a);
		to_string (b);
		r = make_number ((FP___TYPE)(strcoll (a->u.s, b->u.s) < 0));
	} else {
		(void)to_number(a);
		(void)to_number(b);
		r = make_number ((FP___TYPE)(a->u.i < b->u.i));
	}

	free_value (a);
	free_value (b);
	return r;
}

static struct val *
op_ge (struct val *a, struct val *b)
{
	struct val *r;

	if (isstring (a) || isstring (b)) {
		to_string (a);
		to_string (b);
		r = make_number ((FP___TYPE)(strcoll (a->u.s, b->u.s) >= 0));
	} else {
		(void)to_number(a);
		(void)to_number(b);
		r = make_number ((FP___TYPE)(a->u.i >= b->u.i));
	}

	free_value (a);
	free_value (b);
	return r;
}

static struct val *
op_le (struct val *a, struct val *b)
{
	struct val *r;

	if (isstring (a) || isstring (b)) {
		to_string (a);
		to_string (b);
		r = make_number ((FP___TYPE)(strcoll (a->u.s, b->u.s) <= 0));
	} else {
		(void)to_number(a);
		(void)to_number(b);
		r = make_number ((FP___TYPE)(a->u.i <= b->u.i));
	}

	free_value (a);
	free_value (b);
	return r;
}

static struct val *
op_cond (struct val *a, struct val *b, struct val *c)
{
	struct val *r;

	if( isstring(a) )
	{
		if( strlen(a->u.s) && strcmp(a->u.s, "\"\"") != 0 && strcmp(a->u.s,"0") != 0 )
		{
			free_value(a);
			free_value(c);
			r = b;
		}
		else
		{
			free_value(a);
			free_value(b);
			r = c;
		}
	}
	else
	{
		(void)to_number(a);
		if( a->u.i )
		{
			free_value(a);
			free_value(c);
			r = b;
		}
		else
		{
			free_value(a);
			free_value(b);
			r = c;
		}
	}
	return r;
}

static struct val *
op_ne (struct val *a, struct val *b)
{
	struct val *r;

	if (isstring (a) || isstring (b)) {
		to_string (a);
		to_string (b);
		r = make_number ((FP___TYPE)(strcoll (a->u.s, b->u.s) != 0));
	} else {
		(void)to_number(a);
		(void)to_number(b);
		r = make_number ((FP___TYPE)(a->u.i != b->u.i));
	}

	free_value (a);
	free_value (b);
	return r;
}

static int
chk_plus (FP___TYPE a, FP___TYPE b, FP___TYPE r)
{
	/* sum of two positive numbers must be positive */
	if (a > 0 && b > 0 && r <= 0)
		return 1;
	/* sum of two negative numbers must be negative */
	if (a < 0 && b < 0 && r >= 0)
		return 1;
	/* all other cases are OK */
	return 0;
}

static struct val *
op_plus (struct val *a, struct val *b)
{
	struct val *r;

	if (!to_number (a)) {
		if( !extra_error_message_supplied )
			ast_log(LOG_WARNING,"non-numeric argument\n");
		if (!to_number (b)) {
			free_value(a);
			free_value(b);
			return make_number(0);
		} else {
			free_value(a);
			return (b);
		}
	} else if (!to_number(b)) {
		free_value(b);
		return (a);
	}

	r = make_number (a->u.i + b->u.i);
	if (chk_plus (a->u.i, b->u.i, r->u.i)) {
		ast_log(LOG_WARNING,"overflow\n");
	}
	free_value (a);
	free_value (b);
	return r;
}

static int
chk_minus (FP___TYPE a, FP___TYPE b, FP___TYPE r)
{
	/* special case subtraction of QUAD_MIN */
	if (b == QUAD_MIN) {
		if (a >= 0)
			return 1;
		else
			return 0;
	}
	/* this is allowed for b != QUAD_MIN */
	return chk_plus (a, -b, r);
}

static struct val *
op_minus (struct val *a, struct val *b)
{
	struct val *r;

	if (!to_number (a)) {
		if( !extra_error_message_supplied )
			ast_log(LOG_WARNING, "non-numeric argument\n");
		if (!to_number (b)) {
			free_value(a);
			free_value(b);
			return make_number(0);
		} else {
			r = make_number(0 - b->u.i);
			free_value(a);
			free_value(b);
			return (r);
		}
	} else if (!to_number(b)) {
		if( !extra_error_message_supplied )
			ast_log(LOG_WARNING, "non-numeric argument\n");
		free_value(b);
		return (a);
	}

	r = make_number (a->u.i - b->u.i);
	if (chk_minus (a->u.i, b->u.i, r->u.i)) {
		ast_log(LOG_WARNING, "overflow\n");
	}
	free_value (a);
	free_value (b);
	return r;
}

static struct val *
op_negate (struct val *a)
{
	struct val *r;

	if (!to_number (a) ) {
		free_value(a);
		if( !extra_error_message_supplied )
			ast_log(LOG_WARNING, "non-numeric argument\n");
		return make_number(0);
	}

	r = make_number (- a->u.i);
	if (chk_minus (0, a->u.i, r->u.i)) {
		ast_log(LOG_WARNING, "overflow\n");
	}
	free_value (a);
	return r;
}

static struct val *
op_compl (struct val *a)
{
	int v1 = 1;
	struct val *r;
	
	if( !a )
	{
		v1 = 0;
	}
	else
	{
		switch( a->type )
		{
		case AST_EXPR_number:
			if( a->u.i == 0 )
				v1 = 0;
			break;
			
		case AST_EXPR_string:
			if( a->u.s == 0 )
				v1 = 0;
			else
			{
				if( a->u.s[0] == 0 )
					v1 = 0;
				else if (strlen(a->u.s) == 1 && a->u.s[0] == '0' )
					v1 = 0;
			}
			break;
			
		case AST_EXPR_numeric_string:
			if( a->u.s == 0 )
				v1 = 0;
			else
			{
				if( a->u.s[0] == 0 )
					v1 = 0;
				else if (strlen(a->u.s) == 1 && a->u.s[0] == '0' )
					v1 = 0;
			}
			break;
		}
	}
	
	r = make_number (!v1);
	free_value (a);
	return r;
}

static int
chk_times (FP___TYPE a, FP___TYPE b, FP___TYPE r)
{
	/* special case: first operand is 0, no overflow possible */
	if (a == 0)
		return 0;
	/* cerify that result of division matches second operand */
	if (r / a != b)
		return 1;
	return 0;
}

static struct val *
op_times (struct val *a, struct val *b)
{
	struct val *r;

	if (!to_number (a) || !to_number (b)) {
		free_value(a);
		free_value(b);
		if( !extra_error_message_supplied )
			ast_log(LOG_WARNING, "non-numeric argument\n");
		return(make_number(0));
	}

	r = make_number (a->u.i * b->u.i);
	if (chk_times (a->u.i, b->u.i, r->u.i)) {
		ast_log(LOG_WARNING, "overflow\n");
	}
	free_value (a);
	free_value (b);
	return (r);
}

static int
chk_div (FP___TYPE a, FP___TYPE b)
{
	/* div by zero has been taken care of before */
	/* only QUAD_MIN / -1 causes overflow */
	if (a == QUAD_MIN && b == -1)
		return 1;
	/* everything else is OK */
	return 0;
}

static struct val *
op_div (struct val *a, struct val *b)
{
	struct val *r;

	if (!to_number (a)) {
		free_value(a);
		free_value(b);
		if( !extra_error_message_supplied )
			ast_log(LOG_WARNING, "non-numeric argument\n");
		return make_number(0);
	} else if (!to_number (b)) {
		free_value(a);
		free_value(b);
		if( !extra_error_message_supplied )
			ast_log(LOG_WARNING, "non-numeric argument\n");
		return make_number(INT_MAX);
	}

	if (b->u.i == 0) {
		ast_log(LOG_WARNING, "division by zero\n");		
		free_value(a);
		free_value(b);
		return make_number(INT_MAX);
	}

	r = make_number (a->u.i / b->u.i);
	if (chk_div (a->u.i, b->u.i)) {
		ast_log(LOG_WARNING, "overflow\n");
	}
	free_value (a);
	free_value (b);
	return r;
}
	
static struct val *
op_rem (struct val *a, struct val *b)
{
	struct val *r;

	if (!to_number (a) || !to_number (b)) {
		if( !extra_error_message_supplied )
			ast_log(LOG_WARNING, "non-numeric argument\n");
		free_value(a);
		free_value(b);
		return make_number(0);
	}

	if (b->u.i == 0) {
		ast_log(LOG_WARNING, "div by zero\n");
		free_value(a);
		return(b);
	}

	r = make_number (FP___FMOD(a->u.i, b->u.i)); /* either fmod or fmodl if FP___TYPE is available */
	/* chk_rem necessary ??? */
	free_value (a);
	free_value (b);
	return r;
}
	

static struct val *
op_colon (struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	int eval;
	struct val *v;

	/* coerce to both arguments to strings */
	to_string(a);
	to_string(b);
	/* strip double quotes from both -- they'll screw up the pattern, and the search string starting at ^ */
	strip_quotes(a);
	strip_quotes(b);
	/* compile regular expression */
	if ((eval = regcomp (&rp, b->u.s, REG_EXTENDED)) != 0) {
		regerror (eval, &rp, errbuf, sizeof(errbuf));
		ast_log(LOG_WARNING,"regcomp() error : %s",errbuf);
		free_value(a);
		free_value(b);
		return make_str("");		
	}

	/* compare string against pattern */
	/* remember that patterns are anchored to the beginning of the line */
	if (regexec(&rp, a->u.s, (size_t)2, rm, 0) == 0 && rm[0].rm_so == 0) {
		if (rm[1].rm_so >= 0) {
			*(a->u.s + rm[1].rm_eo) = '\0';
			v = make_str (a->u.s + rm[1].rm_so);

		} else {
			v = make_number ((FP___TYPE)(rm[0].rm_eo - rm[0].rm_so));
		}
	} else {
		if (rp.re_nsub == 0) {
			v = make_number ((FP___TYPE)0);
		} else {
			v = make_str ("");
		}
	}

	/* free arguments and pattern buffer */
	free_value (a);
	free_value (b);
	regfree (&rp);

	return v;
}
	

static struct val *
op_eqtilde (struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	int eval;
	struct val *v;

	/* coerce to both arguments to strings */
	to_string(a);
	to_string(b);
	/* strip double quotes from both -- they'll screw up the pattern, and the search string starting at ^ */
	strip_quotes(a);
	strip_quotes(b);
	/* compile regular expression */
	if ((eval = regcomp (&rp, b->u.s, REG_EXTENDED)) != 0) {
		regerror (eval, &rp, errbuf, sizeof(errbuf));
		ast_log(LOG_WARNING,"regcomp() error : %s",errbuf);
		free_value(a);
		free_value(b);
		return make_str("");		
	}

	/* compare string against pattern */
	/* remember that patterns are anchored to the beginning of the line */
	if (regexec(&rp, a->u.s, (size_t)2, rm, 0) == 0 ) {
		if (rm[1].rm_so >= 0) {
			*(a->u.s + rm[1].rm_eo) = '\0';
			v = make_str (a->u.s + rm[1].rm_so);

		} else {
			v = make_number ((FP___TYPE)(rm[0].rm_eo - rm[0].rm_so));
		}
	} else {
		if (rp.re_nsub == 0) {
			v = make_number ((FP___TYPE)0.0);
		} else {
			v = make_str ("");
		}
	}

	/* free arguments and pattern buffer */
	free_value (a);
	free_value (b);
	regfree (&rp);

	return v;
}
