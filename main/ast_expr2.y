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

#define ASTMM_LIBC ASTMM_REDIRECT
#include "asterisk.h"

#include <sys/types.h>
#include <stdio.h>

#if !defined(STANDALONE) && !defined(STANDALONE2)	\
	
ASTERISK_REGISTER_FILE()
#else
#ifndef __USE_ISOC99
#define __USE_ISOC99 1
#endif
#endif

#ifdef __USE_ISOC99
#define FP___PRINTF "%.18Lg"
#define FP___TYPE    long double
#else
#define FP___PRINTF "%.16g"
#define FP___TYPE    double
#endif

#ifdef HAVE_COSL
#define FUNC_COS   cosl
#elif defined(HAVE_COS)
#define FUNC_COS	(long double)cos
#endif

#ifdef HAVE_SINL
#define FUNC_SIN   sinl
#elif defined(HAVE_SIN)
#define FUNC_SIN	(long double)sin
#endif

#ifdef HAVE_TANL
#define FUNC_TAN   tanl
#elif defined(HAVE_TAN)
#define FUNC_TAN	(long double)tan
#endif

#ifdef HAVE_ACOSL
#define FUNC_ACOS   acosl
#elif defined(HAVE_ACOS)
#define FUNC_ACOS	(long double)acos
#endif

#ifdef HAVE_ASINL
#define FUNC_ASIN   asinl
#elif defined(HAVE_ASIN)
#define FUNC_ASIN	(long double)asin
#endif

#ifdef HAVE_ATANL
#define FUNC_ATAN   atanl
#elif defined(HAVE_ATAN)
#define FUNC_ATAN	(long double)atan
#endif

#ifdef HAVE_ATAN2L
#define FUNC_ATAN2   atan2l
#elif defined(HAVE_ATAN2)
#define FUNC_ATAN2	(long double)atan2
#endif

#ifdef HAVE_POWL
#define FUNC_POW   powl
#elif defined(HAVE_POW)
#define FUNC_POW	(long double)pow
#endif

#ifdef HAVE_SQRTL
#define FUNC_SQRT   sqrtl
#elif defined(HAVE_SQRT)
#define FUNC_SQRT	(long double)sqrt
#endif

#ifdef HAVE_RINTL
#define FUNC_RINT   rintl
#elif defined(HAVE_RINT)
#define FUNC_RINT	(long double)rint
#endif

#ifdef HAVE_EXPL
#define FUNC_EXP   expl
#elif defined(HAVE_EXP)
#define FUNC_EXP	(long double)exp
#endif

#ifdef HAVE_LOGL
#define FUNC_LOG   logl
#elif defined(HAVE_LOG)
#define FUNC_LOG	(long double)log
#endif

#ifdef HAVE_REMAINDERL
#define FUNC_REMAINDER   remainderl
#elif defined(HAVE_REMAINDER)
#define FUNC_REMAINDER	(long double)remainder
#endif

#ifdef HAVE_FMODL
#define FUNC_FMOD   fmodl
#elif defined(HAVE_FMOD)
#define FUNC_FMOD	(long double)fmod
#endif

#ifdef HAVE_STRTOLD
#define FUNC_STRTOD  strtold
#elif defined(HAVE_STRTOD)
#define FUNC_STRTOD  (long double)strtod
#endif

#ifdef HAVE_FLOORL
#define FUNC_FLOOR      floorl
#elif defined(HAVE_FLOOR)
#define FUNC_FLOOR	(long double)floor
#endif

#ifdef HAVE_CEILL
#define FUNC_CEIL      ceill
#elif defined(HAVE_CEIL)
#define FUNC_CEIL	(long double)ceil
#endif

#ifdef HAVE_ROUNDL
#define FUNC_ROUND     roundl
#elif defined(HAVE_ROUND)
#define FUNC_ROUND     (long double)round
#endif

#ifdef HAVE_TRUNCL
#define FUNC_TRUNC     truncl
#elif defined(HAVE_TRUNC)
#define FUNC_TRUNC     (long double)trunc
#endif

/*! \note
 * Oddly enough, some platforms have some ISO C99 functions, but not others, so
 * we define the missing functions in terms of their mathematical identities.
 */
#ifdef HAVE_EXP2L
#define FUNC_EXP2       exp2l
#elif (defined(HAVE_EXPL) && defined(HAVE_LOGL))
#define	FUNC_EXP2(x)	expl((x) * logl(2.0))
#elif (defined(HAVE_EXP) && defined(HAVE_LOG))
#define	FUNC_EXP2(x)	(long double)exp((x) * log(2.0))
#endif

#ifdef HAVE_EXP10L
#define FUNC_EXP10       exp10l
#elif (defined(HAVE_EXPL) && defined(HAVE_LOGL))
#define	FUNC_EXP10(x)	expl((x) * logl(10.0))
#elif (defined(HAVE_EXP) && defined(HAVE_LOG))
#define	FUNC_EXP10(x)	(long double)exp((x) * log(10.0))
#endif

#ifdef HAVE_LOG2L
#define FUNC_LOG2       log2l
#elif defined(HAVE_LOGL)
#define	FUNC_LOG2(x)	(logl(x) / logl(2.0))
#elif defined(HAVE_LOG10L)
#define	FUNC_LOG2(x)	(log10l(x) / log10l(2.0))
#elif defined(HAVE_LOG2)
#define FUNC_LOG2       (long double)log2
#elif defined(HAVE_LOG)
#define	FUNC_LOG2(x)	((long double)log(x) / log(2.0))
#endif

#ifdef HAVE_LOG10L
#define FUNC_LOG10       log10l
#elif defined(HAVE_LOGL)
#define	FUNC_LOG10(x)	(logl(x) / logl(10.0))
#elif defined(HAVE_LOG2L)
#define	FUNC_LOG10(x)	(log2l(x) / log2l(10.0))
#elif defined(HAVE_LOG10)
#define	FUNC_LOG10(x)	(long double)log10(x)
#elif defined(HAVE_LOG)
#define	FUNC_LOG10(x)	((long double)log(x) / log(10.0))
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

#include "asterisk/ast_expr.h"
#include "asterisk/logger.h"
#if !defined(STANDALONE) && !defined(STANDALONE2)
#include "asterisk/pbx.h"
#endif

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

#if defined(STANDALONE) || defined(STANDALONE2)
void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...) __attribute__ ((format (printf,5,6)));
#endif

struct val {
	enum valtype type;
	union {
		char *s;
		FP___TYPE i; /* either long double, or just double, on a bad day */
	} u;
} ;

enum node_type {
	AST_EXPR_NODE_COMMA, AST_EXPR_NODE_STRING, AST_EXPR_NODE_VAL
} ;

struct expr_node 
{
	enum node_type type;
	struct val *val;
	struct expr_node *left;
	struct expr_node *right;
};


typedef void *yyscan_t;

struct parse_io
{
	char *string;
	struct val *val;
	yyscan_t scanner;
	struct ast_channel *chan;
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
static struct val	*op_tildetilde __P((struct val *, struct val *));
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
static struct val   *op_func(struct val *funcname, struct expr_node *arglist, struct ast_channel *chan);
static int		to_number __P((struct val *));
static void		to_string __P((struct val *));
static struct expr_node *alloc_expr_node(enum node_type);
static void destroy_arglist(struct expr_node *arglist);

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
	struct expr_node *arglist;
}

%{
extern int		ast_yylex __P((YYSTYPE *, YYLTYPE *, yyscan_t));
%}
%left <val> TOK_COMMA
%left <val> TOK_COND TOK_COLONCOLON
%left <val> TOK_OR
%left <val> TOK_AND
%left <val> TOK_EQ TOK_GT TOK_LT TOK_GE TOK_LE TOK_NE
%left <val> TOK_PLUS TOK_MINUS
%left <val> TOK_MULT TOK_DIV TOK_MOD 
%right <val> TOK_COMPL
%left <val> TOK_COLON TOK_EQTILDE TOK_TILDETILDE
%left <val> TOK_RP TOK_LP

%token <val> TOKEN
%type <arglist> arglist
%type <val> start expr

%destructor {  free_value($$); }  expr TOKEN TOK_COND TOK_COLONCOLON TOK_OR TOK_AND TOK_EQ 
                                 TOK_GT TOK_LT TOK_GE TOK_LE TOK_NE TOK_PLUS TOK_MINUS TOK_MULT TOK_DIV TOK_MOD TOK_COMPL TOK_COLON TOK_EQTILDE 
                                 TOK_RP TOK_LP TOK_TILDETILDE

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

arglist: expr { $$ = alloc_expr_node(AST_EXPR_NODE_VAL); $$->val = $1;}
       | arglist TOK_COMMA expr %prec TOK_RP {struct expr_node *x = alloc_expr_node(AST_EXPR_NODE_VAL);
                                 struct expr_node *t;
								 DESTROY($2);
                                 for (t=$1;t->right;t=t->right)
						         	  ;
                                 $$ = $1; t->right = x; x->val = $3;}
       | arglist TOK_COMMA %prec TOK_RP {struct expr_node *x = alloc_expr_node(AST_EXPR_NODE_VAL);
                                 struct expr_node *t;  /* NULL args should OK */
								 DESTROY($2);
                                 for (t=$1;t->right;t=t->right)
						         	  ;
                                 $$ = $1; t->right = x; x->val = make_str("");}
       ;

expr: 
      TOKEN TOK_LP arglist TOK_RP { $$ = op_func($1,$3, ((struct parse_io *)parseio)->chan);
		                            DESTROY($2);
									DESTROY($4);
									DESTROY($1);
									destroy_arglist($3);
                                  }
    | TOKEN {$$ = $1;}
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
	| expr TOK_TILDETILDE expr { $$ = op_tildetilde ($1, $3); 
						DESTROY($2);	
	                        @$.first_column = @1.first_column; @$.last_column = @3.last_column; 
							@$.first_line=0; @$.last_line=0;}
	;

%%

static struct expr_node *alloc_expr_node(enum node_type nt)
{
	struct expr_node *x = calloc(1,sizeof(struct expr_node));
	if (!x) {
		ast_log(LOG_ERROR, "Allocation for expr_node FAILED!!\n");
		return 0;
	}
	x->type = nt;
	return x;
}



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
		if (vp) {
			free(vp);
		}
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
	i  = FUNC_STRTOD(vp->u.s, (char**)0); /* either strtod, or strtold on a good day */
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

#ifdef STANDALONE2

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
			
			ret = ast_expr(s, out, sizeof(out), NULL);
			printf("Expression: %s    Result: [%d] '%s'\n",
				   s, ret, out);
		}
		fclose(infile);
	}
	else
	{
		if (ast_expr(argv[1], s, sizeof(s), NULL))
			printf("=====%s======\n",s);
		else
			printf("No result\n");
	}
	return 0;
}

#endif

#undef ast_yyerror
#define ast_yyerror(x) ast_yyerror(x, YYLTYPE *yylloc, struct parse_io *parseio)

/* I put the ast_yyerror func in the flex input file,
   because it refers to the buffer state. Best to
   let it access the BUFFER stuff there and not trying
   define all the structs, macros etc. in this file! */

static void destroy_arglist(struct expr_node *arglist)
{
	struct expr_node *arglist_next;
	
	while (arglist)
	{
		arglist_next = arglist->right;
		if (arglist->val)
			free_value(arglist->val);
		arglist->val = 0;
		arglist->right = 0;
		free(arglist);
		arglist = arglist_next;
	}
}

#if !defined(STANDALONE) && !defined(STANDALONE2)
static char *compose_func_args(struct expr_node *arglist)
{
	struct expr_node *t = arglist;
	char *argbuf;
	int total_len = 0;
	
	while (t) {
		if (t != arglist)
			total_len += 1; /* for the sep */
		if (t->val) {
			if (t->val->type == AST_EXPR_number)
				total_len += 25; /* worst case */
			else
				total_len += strlen(t->val->u.s);
		}
		
		t = t->right;
	}
	total_len++; /* for the null */
	ast_log(LOG_NOTICE,"argbuf allocated %d bytes;\n", total_len);
	argbuf = malloc(total_len);
	argbuf[0] = 0;
	t = arglist;
	while (t) {
		char numbuf[30];
		
		if (t != arglist)
			strcat(argbuf,",");
		
		if (t->val) {
			if (t->val->type == AST_EXPR_number) {
				sprintf(numbuf,FP___PRINTF,t->val->u.i);
				strcat(argbuf,numbuf);
			} else
				strcat(argbuf,t->val->u.s);
		}
		t = t->right;
	}
	ast_log(LOG_NOTICE,"argbuf uses %d bytes;\n", (int) strlen(argbuf));
	return argbuf;
}

static int is_really_num(char *str)
{
	if ( strspn(str,"-0123456789. 	") == strlen(str))
		return 1;
	else
		return 0;
}
#endif

static struct val *op_func(struct val *funcname, struct expr_node *arglist, struct ast_channel *chan)
{
	if (strspn(funcname->u.s,"ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789") == strlen(funcname->u.s))
	{
		struct val *result;
		if (0) {
#ifdef FUNC_COS
		} else if (strcmp(funcname->u.s,"COS") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_COS(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_SIN
		} else if (strcmp(funcname->u.s,"SIN") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_SIN(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_TAN
		} else if (strcmp(funcname->u.s,"TAN") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_TAN(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_ACOS
		} else if (strcmp(funcname->u.s,"ACOS") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_ACOS(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_ASIN
		} else if (strcmp(funcname->u.s,"ASIN") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_ASIN(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_ATAN
		} else if (strcmp(funcname->u.s,"ATAN") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_ATAN(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_ATAN2
		} else if (strcmp(funcname->u.s,"ATAN2") == 0) {
			if (arglist && arglist->right && !arglist->right->right && arglist->val && arglist->right->val){
				to_number(arglist->val);
				to_number(arglist->right->val);
				result = make_number(FUNC_ATAN2(arglist->val->u.i, arglist->right->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_POW
		} else if (strcmp(funcname->u.s,"POW") == 0) {
			if (arglist && arglist->right && !arglist->right->right && arglist->val && arglist->right->val){
				to_number(arglist->val);
				to_number(arglist->right->val);
				result = make_number(FUNC_POW(arglist->val->u.i, arglist->right->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_SQRT
		} else if (strcmp(funcname->u.s,"SQRT") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_SQRT(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_FLOOR
		} else if (strcmp(funcname->u.s,"FLOOR") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_FLOOR(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_CEIL
		} else if (strcmp(funcname->u.s,"CEIL") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_CEIL(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_ROUND
		} else if (strcmp(funcname->u.s,"ROUND") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_ROUND(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif /* defined(FUNC_ROUND) */
#ifdef FUNC_RINT
		} else if (strcmp(funcname->u.s,"RINT") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_RINT(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_TRUNC
		} else if (strcmp(funcname->u.s,"TRUNC") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_TRUNC(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif /* defined(FUNC_TRUNC) */
#ifdef FUNC_EXP
		} else if (strcmp(funcname->u.s,"EXP") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_EXP(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_EXP2
		} else if (strcmp(funcname->u.s,"EXP2") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_EXP2(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_EXP10
		} else if (strcmp(funcname->u.s,"EXP10") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_EXP10(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_LOG
		} else if (strcmp(funcname->u.s,"LOG") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_LOG(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_LOG2
		} else if (strcmp(funcname->u.s,"LOG2") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_LOG2(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_LOG10
		} else if (strcmp(funcname->u.s,"LOG10") == 0) {
			if (arglist && !arglist->right && arglist->val){
				to_number(arglist->val);
				result = make_number(FUNC_LOG10(arglist->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
#ifdef FUNC_REMAINDER
		} else if (strcmp(funcname->u.s,"REMAINDER") == 0) {
			if (arglist && arglist->right && !arglist->right->right && arglist->val && arglist->right->val){
				to_number(arglist->val);
				to_number(arglist->right->val);
				result = make_number(FUNC_REMAINDER(arglist->val->u.i, arglist->right->val->u.i));
				return result;
			} else {
				ast_log(LOG_WARNING,"Wrong args to %s() function\n",funcname->u.s);
				return make_number(0.0);
			}
#endif
		} else if (strcmp(funcname->u.s, "ABS") == 0) {
			if (arglist && !arglist->right && arglist->val) {
				to_number(arglist->val);
				result = make_number(arglist->val->u.i < 0 ? arglist->val->u.i * -1 : arglist->val->u.i);
				return result;
			} else {
				ast_log(LOG_WARNING, "Wrong args to %s() function\n", funcname->u.s);
				return make_number(0.0);
			}
		} else {
			/* is this a custom function we should execute and collect the results of? */
#if !defined(STANDALONE) && !defined(STANDALONE2)
			struct ast_custom_function *f = ast_custom_function_find(funcname->u.s);
			if (!chan)
				ast_log(LOG_WARNING,"Hey! chan is NULL.\n");
			if (!f)
				ast_log(LOG_WARNING,"Hey! could not find func %s.\n", funcname->u.s);
			
			if (f && chan) {
				if (f->read) {
					char workspace[512];
					char *argbuf = compose_func_args(arglist);
					f->read(chan, funcname->u.s, argbuf, workspace, sizeof(workspace));
					free(argbuf);
					if (is_really_num(workspace))
						return make_number(FUNC_STRTOD(workspace,(char **)NULL));
					else
						return make_str(workspace);
				} else {
					ast_log(LOG_ERROR,"Error! Function '%s' cannot be read!\n", funcname->u.s);
					return (make_number ((FP___TYPE)0.0));
				}
				
			} else {
				ast_log(LOG_ERROR, "Error! '%s' doesn't appear to be an available function!\n", funcname->u.s);
				return (make_number ((FP___TYPE)0.0));
			}
#else
			ast_log(LOG_ERROR, "Error! '%s' is not available in the standalone version!\n", funcname->u.s);
			return (make_number ((FP___TYPE)0.0));
#endif
		}
	}
	else
	{
		ast_log(LOG_ERROR, "Error! '%s' is not possibly a function name!\n", funcname->u.s);
		return (make_number ((FP___TYPE)0.0));
	}
	return (make_number ((FP___TYPE)0.0));
}


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
		return (make_number ((FP___TYPE)0.0));
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
				else
					v1 = atoi(a->u.s);
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
				else
					v1 = atoi(a->u.s);
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

	r = make_number (FUNC_FMOD(a->u.i, b->u.i)); /* either fmod or fmodl if FP___TYPE is available */
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
		ast_log(LOG_WARNING, "regcomp() error : %s\n", errbuf);
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
		ast_log(LOG_WARNING, "regcomp() error : %s\n", errbuf);
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

static struct val *  /* this is a string concat operator */
op_tildetilde (struct val *a, struct val *b)
{
	struct val *v;
	char *vs;

	/* coerce to both arguments to strings */
	to_string(a);
	to_string(b);
	/* strip double quotes from both -- */
	strip_quotes(a);
	strip_quotes(b);
	
	vs = malloc(strlen(a->u.s)+strlen(b->u.s)+1);
	strcpy(vs,a->u.s);
	strcat(vs,b->u.s);

	v = make_str(vs);

	/* free arguments */
	free_value(a);
	free_value(b);

	return v;
}
