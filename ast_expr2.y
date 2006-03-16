%{
/* Written by Pace Willisson (pace@blitz.com) 
 * and placed in the public domain.
 *
 * Largely rewritten by J.T. Conklin (jtc@wimsey.com)
 *
 * And then overhauled twice by Steve Murphy (murf@e-tools.com)
 * to add double-quoted strings, allow mult. spaces, improve
 * error messages, and then to fold in a flex scanner for the 
 * yylex operation.
 *
 * $FreeBSD: src/bin/expr/expr.y,v 1.16 2000/07/22 10:59:36 se Exp $
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#if !defined(SOLARIS) && !defined(__CYGWIN__)
#include <err.h>
#else
#define quad_t int64_t
#endif
#include <errno.h>
#include <regex.h>
#include <limits.h>
#include <asterisk/ast_expr.h>
#include <asterisk/logger.h>

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

#define YYPARSE_PARAM parseio
#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner
#define YYERROR_VERBOSE 1

enum valtype {
	AST_EXPR_integer, AST_EXPR_numeric_string, AST_EXPR_string
} ;

#ifdef STANDALONE
void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...) __attribute__ ((format (printf,5,6)));
#endif

struct val {
	enum valtype type;
	union {
		char *s;
		quad_t i;
	} u;
} ;

typedef void *yyscan_t;

struct parse_io
{
	char *string;
	struct val *val;
	yyscan_t scanner;
};
 
static int		chk_div __P((quad_t, quad_t));
static int		chk_minus __P((quad_t, quad_t, quad_t));
static int		chk_plus __P((quad_t, quad_t, quad_t));
static int		chk_times __P((quad_t, quad_t, quad_t));
static void		free_value __P((struct val *));
static int		is_zero_or_null __P((struct val *));
static int		isstring __P((struct val *));
static struct val	*make_integer __P((quad_t));
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
static quad_t		to_integer __P((struct val *));
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
#define DESTROY(x) { \
if ((x)->type == AST_EXPR_numeric_string || (x)->type == AST_EXPR_string) \
	free((x)->u.s); \
	(x)->u.s = 0; \
	free(x); \
}
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

%%

start: expr { ((struct parse_io *)parseio)->val = (struct val *)calloc(sizeof(struct val),1);
              ((struct parse_io *)parseio)->val->type = $1->type;
              if( $1->type == AST_EXPR_integer )
				  ((struct parse_io *)parseio)->val->u.i = $1->u.i;
              else
				  ((struct parse_io *)parseio)->val->u.s = $1->u.s; 
			  free($1);
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
make_integer (quad_t i)
{
	struct val *vp;

	vp = (struct val *) malloc (sizeof (*vp));
	if (vp == NULL) {
		ast_log(LOG_WARNING, "malloc() failed\n");
		return(NULL);
	}

	vp->type = AST_EXPR_integer;
	vp->u.i  = i;
	return vp; 
}

static struct val *
make_str (const char *s)
{
	struct val *vp;
	size_t i;
	int isint;

	vp = (struct val *) malloc (sizeof (*vp));
	if (vp == NULL || ((vp->u.s = strdup (s)) == NULL)) {
		ast_log(LOG_WARNING,"malloc() failed\n");
		return(NULL);
	}

	for(i = 1, isint = isdigit(s[0]) || s[0] == '-';
	    isint && i < strlen(s);
	    i++)
	{
		if(!isdigit(s[i]))
			 isint = 0;
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


static quad_t
to_integer (struct val *vp)
{
	quad_t i;
	
	if (vp == NULL) {
		ast_log(LOG_WARNING,"vp==NULL in to_integer()\n");
		return(0);
	}

	if (vp->type == AST_EXPR_integer)
		return 1;

	if (vp->type == AST_EXPR_string)
		return 0;

	/* vp->type == AST_EXPR_numeric_string, make it numeric */
	errno = 0;
	i  = strtoll(vp->u.s, (char**)NULL, 10);
	if (errno != 0) {
		ast_log(LOG_WARNING,"Conversion of %s to integer under/overflowed!\n", vp->u.s);
		free(vp->u.s);
		vp->u.s = 0;
		return(0);
	}
	free (vp->u.s);
	vp->u.i = i;
	vp->type = AST_EXPR_integer;
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

	sprintf(tmp, "%ld", (long int) vp->u.i);
	vp->type = AST_EXPR_string;
	vp->u.s  = tmp;
}


static int
isstring (struct val *vp)
{
	/* only TRUE if this string is not a valid integer */
	return (vp->type == AST_EXPR_string);
}


static int
is_zero_or_null (struct val *vp)
{
	if (vp->type == AST_EXPR_integer) {
		return (vp->u.i == 0);
	} else {
		return (*vp->u.s == 0 || (to_integer (vp) && vp->u.i == 0));
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
	
	if (ast_expr(argv[1], s, sizeof(s)))
		printf("=====%s======\n",s);
	else
		printf("No result\n");
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
		return (make_integer ((quad_t)0));
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
		r = make_integer ((quad_t)(strcoll (a->u.s, b->u.s) == 0));
	} else {
#ifdef DEBUG_FOR_CONVERSIONS
		char buffer[2000];
		sprintf(buffer,"Converting '%s' and '%s' ", a->u.s, b->u.s);
#endif
		(void)to_integer(a);
		(void)to_integer(b);
#ifdef DEBUG_FOR_CONVERSIONS
		ast_log(LOG_WARNING,"%s to '%lld' and '%lld'\n", buffer, a->u.i, b->u.i);
#endif
		r = make_integer ((quad_t)(a->u.i == b->u.i));
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
		r = make_integer ((quad_t)(strcoll (a->u.s, b->u.s) > 0));
	} else {
		(void)to_integer(a);
		(void)to_integer(b);
		r = make_integer ((quad_t)(a->u.i > b->u.i));
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
		r = make_integer ((quad_t)(strcoll (a->u.s, b->u.s) < 0));
	} else {
		(void)to_integer(a);
		(void)to_integer(b);
		r = make_integer ((quad_t)(a->u.i < b->u.i));
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
		r = make_integer ((quad_t)(strcoll (a->u.s, b->u.s) >= 0));
	} else {
		(void)to_integer(a);
		(void)to_integer(b);
		r = make_integer ((quad_t)(a->u.i >= b->u.i));
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
		r = make_integer ((quad_t)(strcoll (a->u.s, b->u.s) <= 0));
	} else {
		(void)to_integer(a);
		(void)to_integer(b);
		r = make_integer ((quad_t)(a->u.i <= b->u.i));
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
		(void)to_integer(a);
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
		r = make_integer ((quad_t)(strcoll (a->u.s, b->u.s) != 0));
	} else {
		(void)to_integer(a);
		(void)to_integer(b);
		r = make_integer ((quad_t)(a->u.i != b->u.i));
	}

	free_value (a);
	free_value (b);
	return r;
}

static int
chk_plus (quad_t a, quad_t b, quad_t r)
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

	if (!to_integer (a)) {
		ast_log(LOG_WARNING,"non-numeric argument\n");
		if (!to_integer (b)) {
			free_value(a);
			free_value(b);
			return make_integer(0);
		} else {
			free_value(a);
			return (b);
		}
	} else if (!to_integer(b)) {
		free_value(b);
		return (a);
	}

	r = make_integer (/*(quad_t)*/(a->u.i + b->u.i));
	if (chk_plus (a->u.i, b->u.i, r->u.i)) {
		ast_log(LOG_WARNING,"overflow\n");
	}
	free_value (a);
	free_value (b);
	return r;
}

static int
chk_minus (quad_t a, quad_t b, quad_t r)
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

	if (!to_integer (a)) {
		ast_log(LOG_WARNING, "non-numeric argument\n");
		if (!to_integer (b)) {
			free_value(a);
			free_value(b);
			return make_integer(0);
		} else {
			r = make_integer(0 - b->u.i);
			free_value(a);
			free_value(b);
			return (r);
		}
	} else if (!to_integer(b)) {
		ast_log(LOG_WARNING, "non-numeric argument\n");
		free_value(b);
		return (a);
	}

	r = make_integer (/*(quad_t)*/(a->u.i - b->u.i));
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

	if (!to_integer (a) ) {
		free_value(a);
		ast_log(LOG_WARNING, "non-numeric argument\n");
		return make_integer(0);
	}

	r = make_integer (/*(quad_t)*/(- a->u.i));
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
		case AST_EXPR_integer:
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
	
	r = make_integer (!v1);
	free_value (a);
	return r;
}

static int
chk_times (quad_t a, quad_t b, quad_t r)
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

	if (!to_integer (a) || !to_integer (b)) {
		free_value(a);
		free_value(b);
		ast_log(LOG_WARNING, "non-numeric argument\n");
		return(make_integer(0));
	}

	r = make_integer (/*(quad_t)*/(a->u.i * b->u.i));
	if (chk_times (a->u.i, b->u.i, r->u.i)) {
		ast_log(LOG_WARNING, "overflow\n");
	}
	free_value (a);
	free_value (b);
	return (r);
}

static int
chk_div (quad_t a, quad_t b)
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

	if (!to_integer (a)) {
		free_value(a);
		free_value(b);
		ast_log(LOG_WARNING, "non-numeric argument\n");
		return make_integer(0);
	} else if (!to_integer (b)) {
		free_value(a);
		free_value(b);
		ast_log(LOG_WARNING, "non-numeric argument\n");
		return make_integer(INT_MAX);
	}

	if (b->u.i == 0) {
		ast_log(LOG_WARNING, "division by zero\n");		
		free_value(a);
		free_value(b);
		return make_integer(INT_MAX);
	}

	r = make_integer (/*(quad_t)*/(a->u.i / b->u.i));
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

	if (!to_integer (a) || !to_integer (b)) {
		ast_log(LOG_WARNING, "non-numeric argument\n");
		free_value(a);
		free_value(b);
		return make_integer(0);
	}

	if (b->u.i == 0) {
		ast_log(LOG_WARNING, "div by zero\n");
		free_value(a);
		return(b);
	}

	r = make_integer (/*(quad_t)*/(a->u.i % b->u.i));
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
			v = make_integer ((quad_t)(rm[0].rm_eo - rm[0].rm_so));
		}
	} else {
		if (rp.re_nsub == 0) {
			v = make_integer ((quad_t)0);
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
			v = make_integer ((quad_t)(rm[0].rm_eo - rm[0].rm_so));
		}
	} else {
		if (rp.re_nsub == 0) {
			v = make_integer ((quad_t)0);
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
