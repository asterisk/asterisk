%{
/* Written by Pace Willisson (pace@blitz.com) 
 * and placed in the public domain.
 *
 * Largely rewritten by J.T. Conklin (jtc@wimsey.com)
 *
 * $FreeBSD: src/bin/expr/expr.y,v 1.16 2000/07/22 10:59:36 se Exp $
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <regex.h>
#include <limits.h>
#include <asterisk/ast_expr.h>
#include <asterisk/logger.h>

#  if ! defined(QUAD_MIN)
#   define QUAD_MIN     (-0x7fffffffffffffffL-1)
#  endif
#  if ! defined(QUAD_MAX)
#   define QUAD_MAX     (0x7fffffffffffffffL)
#  endif

#define YYPARSE_PARAM kota
#define YYLEX_PARAM kota

/* #define ast_log fprintf
#define LOG_WARNING stderr */
  
enum valtype {
	integer, numeric_string, string
} ;

struct val {
	enum valtype type;
	union {
		char *s;
		quad_t i;
	} u;
} ;

struct parser_control {
	struct val *result;
	int pipa;
	char *arg_orig;
	char *argv;
	char *ptrptr;
	int firsttoken;
} ;

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
static struct val	*op_div __P((struct val *, struct val *));
static struct val	*op_eq __P((struct val *, struct val *));
static struct val	*op_ge __P((struct val *, struct val *));
static struct val	*op_gt __P((struct val *, struct val *));
static struct val	*op_le __P((struct val *, struct val *));
static struct val	*op_lt __P((struct val *, struct val *));
static struct val	*op_minus __P((struct val *, struct val *));
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

static int		ast_yyerror __P((const char *,YYLTYPE *, struct parser_control *));

#define ast_yyerror(x) ast_yyerror(x,&yyloc,kota)

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
static int		ast_yylex __P((YYSTYPE *, YYLTYPE *, struct parser_control *));
%}


%left <val> '|'
%left <val> '&'
%left <val> '=' '>' '<' GE LE NE
%left <val> '+' '-'
%left <val> '*' '/' '%'
%left <val> ':'

%token <val> TOKEN
%type <val> start expr

%%

start: expr { ((struct parser_control *)kota)->result = $$; }
	;

expr:	TOKEN
	| '(' expr ')' { $$ = $2; @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '|' expr { $$ = op_or ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '&' expr { $$ = op_and ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '=' expr { $$ = op_eq ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '>' expr { $$ = op_gt ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '<' expr { $$ = op_lt ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr GE expr  { $$ = op_ge ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr LE expr  { $$ = op_le ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr NE expr  { $$ = op_ne ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '+' expr { $$ = op_plus ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '-' expr { $$ = op_minus ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '*' expr { $$ = op_times ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '/' expr { $$ = op_div ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr '%' expr { $$ = op_rem ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	| expr ':' expr { $$ = op_colon ($1, $3); @$.first_column = @1.first_column; @$.last_column = @3.last_column; @$.first_line=0; @$.last_line=0;}
	;


%%

static struct val *
make_integer (i)
quad_t i;
{
	struct val *vp;

	vp = (struct val *) malloc (sizeof (*vp));
	if (vp == NULL) {
		ast_log(LOG_WARNING, "malloc() failed\n");
		return(NULL);
	}

	vp->type = integer;
	vp->u.i  = i;
	return vp; 
}

static struct val *
make_str (s)
const char *s;
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
		vp->type = numeric_string;
	else	
		vp->type = string;

	return vp;
}


static void
free_value (vp)
struct val *vp;
{	
	if (vp==NULL) {
		return;
	}
	if (vp->type == string || vp->type == numeric_string)
		free (vp->u.s);	
}


static quad_t
to_integer (vp)
struct val *vp;
{
	quad_t i;

	if (vp == NULL) {
		ast_log(LOG_WARNING,"vp==NULL in to_integer()\n");
		return(0);
	}

	if (vp->type == integer)
		return 1;

	if (vp->type == string)
		return 0;

	/* vp->type == numeric_string, make it numeric */
	errno = 0;
	i  = strtoq(vp->u.s, (char**)NULL, 10);
	if (errno != 0) {
		free(vp->u.s);
		ast_log(LOG_WARNING,"overflow\n");
		return(0);
	}
	free (vp->u.s);
	vp->u.i = i;
	vp->type = integer;
	return 1;
}

static void
to_string (vp)
struct val *vp;
{
	char *tmp;

	if (vp->type == string || vp->type == numeric_string)
		return;

	tmp = malloc ((size_t)25);
	if (tmp == NULL) {
		ast_log(LOG_WARNING,"malloc() failed\n");
		return;
	}

	sprintf (tmp, "%lld", (long long)vp->u.i);
	vp->type = string;
	vp->u.s  = tmp;
}


static int
isstring (vp)
struct val *vp;
{
	/* only TRUE if this string is not a valid integer */
	return (vp->type == string);
}

static int
ast_yylex (YYSTYPE *lvalp, YYLTYPE *yylloc, struct parser_control *karoto)
{
	char *p=0;
	char *t1=0;

	if (karoto->firsttoken==1) {
		t1 = karoto->argv;
		karoto->firsttoken = 0;
	} else {
		t1 = karoto->ptrptr;
	}
	
	while(*t1 && *t1 == ' ' )  /* we can remove worries about leading/multiple spaces being present */
		t1++;
	karoto->ptrptr = t1;
	yylloc->first_column = t1 - karoto->argv;
	
	while( *t1 && *t1 != ' ' && *t1 != '"') /* find the next space or quote */
		t1++;
	if( *t1 == ' ' )
	{
		*t1 = 0;
		p = karoto->ptrptr;
		karoto->ptrptr = t1+1;
		yylloc->last_column = t1 - karoto->argv;
	}
	else if (*t1 == '"' )
	{
		/* opening quote. find the closing quote */
		char *t2=t1+1;
		while( *t2 && *t2 != '"')
			t2++;
		if( *t2 == '"' )
		{
			*t2 = 0;
			karoto->ptrptr = t2+1;
			p = t1+1;
		}
		else
		{
			/* NOT GOOD -- no closing quote! */
			p = t1;
			karoto->ptrptr = t2;
		}
		yylloc->last_column = t2 - karoto->argv;
	}
	else if( *t1 == 0 )
	{
		/* we are done. That was quick */
		p = karoto->ptrptr;
		yylloc->last_column = t1 - karoto->argv;
	}
	if( *p == 0 )
		p = 0;
	
	if (p==NULL) {
		return (0);
	}


	if (strlen (p) == 1) {
		if (strchr ("|&=<>+-*/%:()", *p))
			return (*p);
	} else if (strlen (p) == 2 && p[1] == '=') {
		switch (*p) {
		case '>': return (GE);
		case '<': return (LE);
		case '!': return (NE);
		}
	}

	lvalp->val = make_str (p);
	return (TOKEN);
}

static int
is_zero_or_null (vp)
struct val *vp;
{
	if (vp->type == integer) {
		return (vp->u.i == 0);
	} else {
		return (*vp->u.s == 0 || (to_integer (vp) && vp->u.i == 0));
	}
	/* NOTREACHED */
}

char *ast_expr (char *arg)
{
	struct parser_control karoto;

	char *kota;
	char *pirouni;
	
	kota=strdup(arg);
	karoto.result = NULL;
	karoto.firsttoken=1;
	karoto.argv=kota;
	karoto.arg_orig = arg;
	/* ast_yydebug = 1; */
	
	ast_yyparse ((void *)&karoto);

	free(kota);

	if (karoto.result==NULL) {
		pirouni=strdup("0");
		return(pirouni);
	} else {
		if (karoto.result->type == integer) {
			pirouni=malloc(256);
			sprintf (pirouni,"%lld", (long long)karoto.result->u.i);
		}
		else {
			pirouni=strdup(karoto.result->u.s);
		}
		free(karoto.result);
	}
	return(pirouni);
}

#ifdef STANDALONE

int main(int argc,char **argv) {
	char *s;

	s=ast_expr(argv[1]);

	printf("=====%s======\n",s);
}

#endif

#undef ast_yyerror
#define ast_yyerror(x) ast_yyerror(x, YYLTYPE *yylloc, struct parser_control *karoto)

static int
ast_yyerror (const char *s)
{	
	char spacebuf[8000]; /* best safe than sorry */
	char spacebuf2[8000]; /* best safe than sorry */
	int i=0;
	spacebuf[0] = 0;
	
	if( yylloc->first_column > 7990 ) /* if things get out of whack, why crash? */
		yylloc->first_column = 7990;
	if( yylloc->last_column > 7990 )
		yylloc->last_column = 7990;
	for(i=0;i<yylloc->first_column;i++) spacebuf[i] = ' ';
	for(   ;i<yylloc->last_column;i++) spacebuf[i] = '^';
	spacebuf[i] = 0;

	for(i=0;i<karoto->ptrptr-karoto->argv;i++) spacebuf2[i] = ' ';
	spacebuf2[i++]='^';
	spacebuf2[i]= 0;

	ast_log(LOG_WARNING,"ast_yyerror(): syntax error: %s; Input:\n%s\n%s\n%s\n",s, 
			karoto->arg_orig,spacebuf,spacebuf2);
	return(0);
}


static struct val *
op_or (a, b)
struct val *a, *b;
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
op_and (a, b)
struct val *a, *b;
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
op_eq (a, b)
struct val *a, *b;
{
	struct val *r; 

	if (isstring (a) || isstring (b)) {
		to_string (a);
		to_string (b);	
		r = make_integer ((quad_t)(strcoll (a->u.s, b->u.s) == 0));
	} else {
		(void)to_integer(a);
		(void)to_integer(b);
		r = make_integer ((quad_t)(a->u.i == b->u.i));
	}

	free_value (a);
	free_value (b);
	return r;
}

static struct val *
op_gt (a, b)
struct val *a, *b;
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
op_lt (a, b)
struct val *a, *b;
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
op_ge (a, b)
struct val *a, *b;
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
op_le (a, b)
struct val *a, *b;
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
op_ne (a, b)
struct val *a, *b;
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
chk_plus (a, b, r)
quad_t a, b, r;
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
op_plus (a, b)
struct val *a, *b;
{
	struct val *r;

	if (!to_integer (a) || !to_integer (b)) {
		ast_log(LOG_WARNING,"non-numeric argument\n");
		free_value(a);
		free_value(b);
		return(NULL);
	}

	r = make_integer (/*(quad_t)*/(a->u.i + b->u.i));
	if (chk_plus (a->u.i, b->u.i, r->u.i)) {
		ast_log(LOG_WARNING,"overflow\n");
		free_value(a);
		free_value(b);
		return(NULL);
	}
	free_value (a);
	free_value (b);
	return r;
}

static int
chk_minus (a, b, r)
quad_t a, b, r;
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
op_minus (a, b)
struct val *a, *b;
{
	struct val *r;

	if (!to_integer (a) || !to_integer (b)) {
		free_value(a);
		free_value(b);
		ast_log(LOG_WARNING, "non-numeric argument\n");
		return(NULL);
	}

	r = make_integer (/*(quad_t)*/(a->u.i - b->u.i));
	if (chk_minus (a->u.i, b->u.i, r->u.i)) {
		free_value(a);
		free_value(b);
		ast_log(LOG_WARNING, "overload\n");
		return(NULL);
	}
	free_value (a);
	free_value (b);
	return r;
}

static int
chk_times (a, b, r)
quad_t a, b, r;
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
op_times (a, b)
struct val *a, *b;
{
	struct val *r;

	if (!to_integer (a) || !to_integer (b)) {
		free_value(a);
		free_value(b);
		ast_log(LOG_WARNING, "non-numeric argument\n");
		return(NULL);
	}

	r = make_integer (/*(quad_t)*/(a->u.i * b->u.i));
	if (chk_times (a->u.i, b->u.i, r->u.i)) {
		ast_log(LOG_WARNING, "overflow\n");
		free_value(a);
		free_value(b);
		return(NULL);
	}
	free_value (a);
	free_value (b);
	return (r);
}

static int
chk_div (a, b)
quad_t a, b;
{
	/* div by zero has been taken care of before */
	/* only QUAD_MIN / -1 causes overflow */
	if (a == QUAD_MIN && b == -1)
		return 1;
	/* everything else is OK */
	return 0;
}

static struct val *
op_div (a, b)
struct val *a, *b;
{
	struct val *r;

	if (!to_integer (a) || !to_integer (b)) {
		free_value(a);
		free_value(b);
		ast_log(LOG_WARNING, "non-numeric argument\n");
		return(NULL);
	}

	if (b->u.i == 0) {
		ast_log(LOG_WARNING, "division by zero\n");		
		free_value(a);
		free_value(b);
		return(NULL);
	}

	r = make_integer (/*(quad_t)*/(a->u.i / b->u.i));
	if (chk_div (a->u.i, b->u.i)) {
		ast_log(LOG_WARNING, "overflow\n");
		free_value(a);
		free_value(b);
		return(NULL);
	}
	free_value (a);
	free_value (b);
	return r;
}
	
static struct val *
op_rem (a, b)
struct val *a, *b;
{
	struct val *r;

	if (!to_integer (a) || !to_integer (b)) {
		ast_log(LOG_WARNING, "non-numeric argument\n");
		free_value(a);
		free_value(b);
		return(NULL);
	}

	if (b->u.i == 0) {
		ast_log(LOG_WARNING, "div by zero\n");
		free_value(a);
		free_value(b);
		return(NULL);
	}

	r = make_integer (/*(quad_t)*/(a->u.i % b->u.i));
	/* chk_rem necessary ??? */
	free_value (a);
	free_value (b);
	return r;
}
	
static struct val *
op_colon (a, b)
struct val *a, *b;
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	int eval;
	struct val *v;

	/* coerce to both arguments to strings */
	to_string(a);
	to_string(b);

	/* compile regular expression */
	if ((eval = regcomp (&rp, b->u.s, 0)) != 0) {
		regerror (eval, &rp, errbuf, sizeof(errbuf));
		ast_log(LOG_WARNING,"regcomp() error : %s",errbuf);
		free_value(a);
		free_value(b);
		return(NULL);		
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
