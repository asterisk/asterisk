
/* A Bison parser, made by GNU Bison 2.4.1.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

      Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.4.1"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1

/* Using locations.  */
#define YYLSP_NEEDED 1

/* Substitute the variable and function names.  */
#define yyparse         ast_yyparse
#define yylex           ast_yylex
#define yyerror         ast_yyerror
#define yylval          ast_yylval
#define yychar          ast_yychar
#define yydebug         ast_yydebug
#define yynerrs         ast_yynerrs
#define yylloc          ast_yylloc

/* Copy the first part of user declarations.  */

/* Line 189 of yacc.c  */
#line 1 "ast_expr2.y"

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

#if defined(STANDALONE) || defined(STANDALONE2)
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


/* Line 189 of yacc.c  */
#line 419 "ast_expr2.c"

/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TOK_COMMA = 258,
     TOK_COLONCOLON = 259,
     TOK_COND = 260,
     TOK_OR = 261,
     TOK_AND = 262,
     TOK_NE = 263,
     TOK_LE = 264,
     TOK_GE = 265,
     TOK_LT = 266,
     TOK_GT = 267,
     TOK_EQ = 268,
     TOK_MINUS = 269,
     TOK_PLUS = 270,
     TOK_MOD = 271,
     TOK_DIV = 272,
     TOK_MULT = 273,
     TOK_COMPL = 274,
     TOK_TILDETILDE = 275,
     TOK_EQTILDE = 276,
     TOK_COLON = 277,
     TOK_LP = 278,
     TOK_RP = 279,
     TOKEN = 280
   };
#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{

/* Line 214 of yacc.c  */
#line 345 "ast_expr2.y"

	struct val *val;
	struct expr_node *arglist;



/* Line 214 of yacc.c  */
#line 487 "ast_expr2.c"
} YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
#endif

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */

/* Line 264 of yacc.c  */
#line 350 "ast_expr2.y"

extern int		ast_yylex __P((YYSTYPE *, YYLTYPE *, yyscan_t));


/* Line 264 of yacc.c  */
#line 517 "ast_expr2.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int yyi)
#else
static int
YYID (yyi)
    int yyi;
#endif
{
  return yyi;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
	     && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss_alloc;
  YYSTYPE yyvs_alloc;
  YYLTYPE yyls_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE) + sizeof (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)				\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack_alloc, Stack, yysize);			\
	Stack = &yyptr->Stack_alloc;					\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  11
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   159

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  26
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  4
/* YYNRULES -- Number of rules.  */
#define YYNRULES  28
/* YYNRULES -- Number of states.  */
#define YYNSTATES  54

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   280

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint8 yyprhs[] =
{
       0,     0,     3,     5,     6,     8,    12,    15,    20,    22,
      26,    30,    34,    38,    42,    46,    50,    54,    58,    62,
      66,    69,    72,    76,    80,    84,    88,    92,    98
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      27,     0,    -1,    29,    -1,    -1,    29,    -1,    28,     3,
      29,    -1,    28,     3,    -1,    25,    23,    28,    24,    -1,
      25,    -1,    23,    29,    24,    -1,    29,     6,    29,    -1,
      29,     7,    29,    -1,    29,    13,    29,    -1,    29,    12,
      29,    -1,    29,    11,    29,    -1,    29,    10,    29,    -1,
      29,     9,    29,    -1,    29,     8,    29,    -1,    29,    15,
      29,    -1,    29,    14,    29,    -1,    14,    29,    -1,    19,
      29,    -1,    29,    18,    29,    -1,    29,    17,    29,    -1,
      29,    16,    29,    -1,    29,    22,    29,    -1,    29,    21,
      29,    -1,    29,     5,    29,     4,    29,    -1,    29,    20,
      29,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   374,   374,   382,   389,   390,   396,   405,   411,   412,
     416,   420,   424,   428,   432,   436,   440,   444,   448,   452,
     456,   460,   464,   468,   472,   476,   480,   484,   489
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "TOK_COMMA", "TOK_COLONCOLON",
  "TOK_COND", "TOK_OR", "TOK_AND", "TOK_NE", "TOK_LE", "TOK_GE", "TOK_LT",
  "TOK_GT", "TOK_EQ", "TOK_MINUS", "TOK_PLUS", "TOK_MOD", "TOK_DIV",
  "TOK_MULT", "TOK_COMPL", "TOK_TILDETILDE", "TOK_EQTILDE", "TOK_COLON",
  "TOK_LP", "TOK_RP", "TOKEN", "$accept", "start", "arglist", "expr", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    26,    27,    27,    28,    28,    28,    29,    29,    29,
      29,    29,    29,    29,    29,    29,    29,    29,    29,    29,
      29,    29,    29,    29,    29,    29,    29,    29,    29
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     0,     1,     3,     2,     4,     1,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       2,     2,     3,     3,     3,     3,     3,     5,     3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     0,     0,     8,     0,     2,    20,    21,     0,
       0,     1,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     9,
       0,     4,     0,    10,    11,    17,    16,    15,    14,    13,
      12,    19,    18,    24,    23,    22,    28,    26,    25,     6,
       7,     0,     5,    27
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,     5,    30,     6
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -18
static const yytype_int16 yypact[] =
{
     118,   118,   118,   118,   -15,     6,    65,   -17,   -17,    25,
     118,   -18,   118,   118,   118,   118,   118,   118,   118,   118,
     118,   118,   118,   118,   118,   118,   118,   118,   118,   -18,
       4,    65,    47,    98,   113,   130,   130,   130,   130,   130,
     130,   137,   137,   -17,   -17,   -17,   -18,   -18,   -18,   118,
     -18,   118,    65,    82
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -18,   -18,   -18,    -1
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] =
{
       7,     8,     9,    26,    27,    28,    11,    49,    10,    31,
       0,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    44,    45,    46,    47,    48,    50,     0,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,     0,    26,    27,    28,    52,    29,
      53,    51,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,     0,    26,    27,    28,
      12,    13,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,     0,    26,    27,    28,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,     0,    26,    27,    28,    14,    15,    16,    17,    18,
      19,    20,    21,    22,    23,    24,    25,     0,    26,    27,
      28,    15,    16,    17,    18,    19,    20,    21,    22,    23,
      24,    25,     1,    26,    27,    28,     0,     2,     0,     0,
       0,     3,     0,     4,    21,    22,    23,    24,    25,     0,
      26,    27,    28,    23,    24,    25,     0,    26,    27,    28
};

static const yytype_int8 yycheck[] =
{
       1,     2,     3,    20,    21,    22,     0,     3,    23,    10,
      -1,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,    28,    24,    -1,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    -1,    20,    21,    22,    49,    24,
      51,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    -1,    20,    21,    22,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    -1,    20,    21,    22,     6,     7,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    17,
      18,    -1,    20,    21,    22,     7,     8,     9,    10,    11,
      12,    13,    14,    15,    16,    17,    18,    -1,    20,    21,
      22,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    14,    20,    21,    22,    -1,    19,    -1,    -1,
      -1,    23,    -1,    25,    14,    15,    16,    17,    18,    -1,
      20,    21,    22,    16,    17,    18,    -1,    20,    21,    22
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    14,    19,    23,    25,    27,    29,    29,    29,    29,
      23,     0,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    20,    21,    22,    24,
      28,    29,    29,    29,    29,    29,    29,    29,    29,    29,
      29,    29,    29,    29,    29,    29,    29,    29,    29,     3,
      24,     4,    29,    29
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (&yylval, &yylloc, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval, &yylloc)
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value, Location); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
#endif
{
  if (!yyvaluep)
    return;
  YYUSE (yylocationp);
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  YY_LOCATION_PRINT (yyoutput, *yylocationp);
  YYFPRINTF (yyoutput, ": ");
  yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *yybottom, yytype_int16 *yytop)
#else
static void
yy_stack_print (yybottom, yytop)
    yytype_int16 *yybottom;
    yytype_int16 *yytop;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yylsp, yyrule)
    YYSTYPE *yyvsp;
    YYLTYPE *yylsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       , &(yylsp[(yyi + 1) - (yynrhs)])		       );
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, yylsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yydestruct (yymsg, yytype, yyvaluep, yylocationp)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  YYUSE (yyvaluep);
  YYUSE (yylocationp);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {
      case 4: /* "TOK_COLONCOLON" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1479 "ast_expr2.c"
	break;
      case 5: /* "TOK_COND" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1488 "ast_expr2.c"
	break;
      case 6: /* "TOK_OR" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1497 "ast_expr2.c"
	break;
      case 7: /* "TOK_AND" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1506 "ast_expr2.c"
	break;
      case 8: /* "TOK_NE" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1515 "ast_expr2.c"
	break;
      case 9: /* "TOK_LE" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1524 "ast_expr2.c"
	break;
      case 10: /* "TOK_GE" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1533 "ast_expr2.c"
	break;
      case 11: /* "TOK_LT" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1542 "ast_expr2.c"
	break;
      case 12: /* "TOK_GT" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1551 "ast_expr2.c"
	break;
      case 13: /* "TOK_EQ" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1560 "ast_expr2.c"
	break;
      case 14: /* "TOK_MINUS" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1569 "ast_expr2.c"
	break;
      case 15: /* "TOK_PLUS" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1578 "ast_expr2.c"
	break;
      case 16: /* "TOK_MOD" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1587 "ast_expr2.c"
	break;
      case 17: /* "TOK_DIV" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1596 "ast_expr2.c"
	break;
      case 18: /* "TOK_MULT" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1605 "ast_expr2.c"
	break;
      case 19: /* "TOK_COMPL" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1614 "ast_expr2.c"
	break;
      case 20: /* "TOK_TILDETILDE" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1623 "ast_expr2.c"
	break;
      case 21: /* "TOK_EQTILDE" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1632 "ast_expr2.c"
	break;
      case 22: /* "TOK_COLON" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1641 "ast_expr2.c"
	break;
      case 23: /* "TOK_LP" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1650 "ast_expr2.c"
	break;
      case 24: /* "TOK_RP" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1659 "ast_expr2.c"
	break;
      case 25: /* "TOKEN" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1668 "ast_expr2.c"
	break;
      case 29: /* "expr" */

/* Line 1000 of yacc.c  */
#line 368 "ast_expr2.y"
	{  free_value((yyvaluep->val)); };

/* Line 1000 of yacc.c  */
#line 1677 "ast_expr2.c"
	break;

      default:
	break;
    }
}

/* Prevent warnings from -Wmissing-prototypes.  */
#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */





/*-------------------------.
| yyparse or yypush_parse.  |
`-------------------------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
/* The lookahead symbol.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;

/* Location data for the lookahead symbol.  */
YYLTYPE yylloc;

    /* Number of syntax errors so far.  */
    int yynerrs;

    int yystate;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus;

    /* The stacks and their tools:
       `yyss': related to states.
       `yyvs': related to semantic values.
       `yyls': related to locations.

       Refer to the stacks thru separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* The state stack.  */
    yytype_int16 yyssa[YYINITDEPTH];
    yytype_int16 *yyss;
    yytype_int16 *yyssp;

    /* The semantic value stack.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs;
    YYSTYPE *yyvsp;

    /* The location stack.  */
    YYLTYPE yylsa[YYINITDEPTH];
    YYLTYPE *yyls;
    YYLTYPE *yylsp;

    /* The locations where the error started and ended.  */
    YYLTYPE yyerror_range[2];

    YYSIZE_T yystacksize;

  int yyn;
  int yyresult;
  /* Lookahead token as an internal (translated) token number.  */
  int yytoken;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  yytoken = 0;
  yyss = yyssa;
  yyvs = yyvsa;
  yyls = yylsa;
  yystacksize = YYINITDEPTH;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY; /* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */
  yyssp = yyss;
  yyvsp = yyvs;
  yylsp = yyls;

#if YYLTYPE_IS_TRIVIAL
  /* Initialize the default location before parsing starts.  */
  yylloc.first_line   = yylloc.last_line   = 1;
  yylloc.first_column = yylloc.last_column = 1;
#endif

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;
	YYLTYPE *yyls1 = yyls;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yyls1, yysize * sizeof (*yylsp),
		    &yystacksize);

	yyls = yyls1;
	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss_alloc, yyss);
	YYSTACK_RELOCATE (yyvs_alloc, yyvs);
	YYSTACK_RELOCATE (yyls_alloc, yyls);
#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid lookahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token.  */
  yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;
  *++yylsp = yylloc;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location.  */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:

/* Line 1455 of yacc.c  */
#line 374 "ast_expr2.y"
    { ((struct parse_io *)parseio)->val = (struct val *)calloc(sizeof(struct val),1);
              ((struct parse_io *)parseio)->val->type = (yyvsp[(1) - (1)].val)->type;
              if( (yyvsp[(1) - (1)].val)->type == AST_EXPR_number )
				  ((struct parse_io *)parseio)->val->u.i = (yyvsp[(1) - (1)].val)->u.i;
              else
				  ((struct parse_io *)parseio)->val->u.s = (yyvsp[(1) - (1)].val)->u.s;
			  free((yyvsp[(1) - (1)].val));
			;}
    break;

  case 3:

/* Line 1455 of yacc.c  */
#line 382 "ast_expr2.y"
    {/* nothing */ ((struct parse_io *)parseio)->val = (struct val *)calloc(sizeof(struct val),1);
              ((struct parse_io *)parseio)->val->type = AST_EXPR_string;
			  ((struct parse_io *)parseio)->val->u.s = strdup("");
			;}
    break;

  case 4:

/* Line 1455 of yacc.c  */
#line 389 "ast_expr2.y"
    { (yyval.arglist) = alloc_expr_node(AST_EXPR_NODE_VAL); (yyval.arglist)->val = (yyvsp[(1) - (1)].val);;}
    break;

  case 5:

/* Line 1455 of yacc.c  */
#line 390 "ast_expr2.y"
    {struct expr_node *x = alloc_expr_node(AST_EXPR_NODE_VAL);
                                 struct expr_node *t;
								 DESTROY((yyvsp[(2) - (3)].val));
                                 for (t=(yyvsp[(1) - (3)].arglist);t->right;t=t->right)
						         	  ;
                                 (yyval.arglist) = (yyvsp[(1) - (3)].arglist); t->right = x; x->val = (yyvsp[(3) - (3)].val);;}
    break;

  case 6:

/* Line 1455 of yacc.c  */
#line 396 "ast_expr2.y"
    {struct expr_node *x = alloc_expr_node(AST_EXPR_NODE_VAL);
                                 struct expr_node *t;  /* NULL args should OK */
								 DESTROY((yyvsp[(2) - (2)].val));
                                 for (t=(yyvsp[(1) - (2)].arglist);t->right;t=t->right)
						         	  ;
                                 (yyval.arglist) = (yyvsp[(1) - (2)].arglist); t->right = x; x->val = make_str("");;}
    break;

  case 7:

/* Line 1455 of yacc.c  */
#line 405 "ast_expr2.y"
    { (yyval.val) = op_func((yyvsp[(1) - (4)].val),(yyvsp[(3) - (4)].arglist), ((struct parse_io *)parseio)->chan);
		                            DESTROY((yyvsp[(2) - (4)].val));
									DESTROY((yyvsp[(4) - (4)].val));
									DESTROY((yyvsp[(1) - (4)].val));
									destroy_arglist((yyvsp[(3) - (4)].arglist));
                                  ;}
    break;

  case 8:

/* Line 1455 of yacc.c  */
#line 411 "ast_expr2.y"
    {(yyval.val) = (yyvsp[(1) - (1)].val);;}
    break;

  case 9:

/* Line 1455 of yacc.c  */
#line 412 "ast_expr2.y"
    { (yyval.val) = (yyvsp[(2) - (3)].val);
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						   (yyloc).first_line=0; (yyloc).last_line=0;
							DESTROY((yyvsp[(1) - (3)].val)); DESTROY((yyvsp[(3) - (3)].val)); ;}
    break;

  case 10:

/* Line 1455 of yacc.c  */
#line 416 "ast_expr2.y"
    { (yyval.val) = op_or ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
                         (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 11:

/* Line 1455 of yacc.c  */
#line 420 "ast_expr2.y"
    { (yyval.val) = op_and ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
                          (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 12:

/* Line 1455 of yacc.c  */
#line 424 "ast_expr2.y"
    { (yyval.val) = op_eq ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                     (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 13:

/* Line 1455 of yacc.c  */
#line 428 "ast_expr2.y"
    { (yyval.val) = op_gt ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
                         (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 14:

/* Line 1455 of yacc.c  */
#line 432 "ast_expr2.y"
    { (yyval.val) = op_lt ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                     (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 15:

/* Line 1455 of yacc.c  */
#line 436 "ast_expr2.y"
    { (yyval.val) = op_ge ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 16:

/* Line 1455 of yacc.c  */
#line 440 "ast_expr2.y"
    { (yyval.val) = op_le ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 17:

/* Line 1455 of yacc.c  */
#line 444 "ast_expr2.y"
    { (yyval.val) = op_ne ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 18:

/* Line 1455 of yacc.c  */
#line 448 "ast_expr2.y"
    { (yyval.val) = op_plus ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						   (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 19:

/* Line 1455 of yacc.c  */
#line 452 "ast_expr2.y"
    { (yyval.val) = op_minus ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 20:

/* Line 1455 of yacc.c  */
#line 456 "ast_expr2.y"
    { (yyval.val) = op_negate ((yyvsp[(2) - (2)].val));
						DESTROY((yyvsp[(1) - (2)].val));
	                        (yyloc).first_column = (yylsp[(1) - (2)]).first_column; (yyloc).last_column = (yylsp[(2) - (2)]).last_column;
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 21:

/* Line 1455 of yacc.c  */
#line 460 "ast_expr2.y"
    { (yyval.val) = op_compl ((yyvsp[(2) - (2)].val));
						DESTROY((yyvsp[(1) - (2)].val));
	                        (yyloc).first_column = (yylsp[(1) - (2)]).first_column; (yyloc).last_column = (yylsp[(2) - (2)]).last_column;
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 22:

/* Line 1455 of yacc.c  */
#line 464 "ast_expr2.y"
    { (yyval.val) = op_times ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						   (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 23:

/* Line 1455 of yacc.c  */
#line 468 "ast_expr2.y"
    { (yyval.val) = op_div ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 24:

/* Line 1455 of yacc.c  */
#line 472 "ast_expr2.y"
    { (yyval.val) = op_rem ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 25:

/* Line 1455 of yacc.c  */
#line 476 "ast_expr2.y"
    { (yyval.val) = op_colon ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 26:

/* Line 1455 of yacc.c  */
#line 480 "ast_expr2.y"
    { (yyval.val) = op_eqtilde ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 27:

/* Line 1455 of yacc.c  */
#line 484 "ast_expr2.y"
    { (yyval.val) = op_cond ((yyvsp[(1) - (5)].val), (yyvsp[(3) - (5)].val), (yyvsp[(5) - (5)].val));
						DESTROY((yyvsp[(2) - (5)].val));
						DESTROY((yyvsp[(4) - (5)].val));
	                        (yyloc).first_column = (yylsp[(1) - (5)]).first_column; (yyloc).last_column = (yylsp[(3) - (5)]).last_column;
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 28:

/* Line 1455 of yacc.c  */
#line 489 "ast_expr2.y"
    { (yyval.val) = op_tildetilde ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val));
						DESTROY((yyvsp[(2) - (3)].val));
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;



/* Line 1455 of yacc.c  */
#line 2283 "ast_expr2.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }

  yyerror_range[0] = yylloc;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval, &yylloc);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  yyerror_range[0] = yylsp[1-yylen];
  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      yyerror_range[0] = *yylsp;
      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  *++yyvsp = yylval;

  yyerror_range[1] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the lookahead.  YYLOC is available though.  */
  YYLLOC_DEFAULT (yyloc, (yyerror_range - 1), 2);
  *++yylsp = yyloc;

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#if !defined(yyoverflow) || YYERROR_VERBOSE
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval, &yylloc);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}



/* Line 1675 of yacc.c  */
#line 495 "ast_expr2.y"


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

	if (asprintf(&tmp, FP___PRINTF, vp->u.i) == -1) {
		ast_log(LOG_WARNING, "asprintf() failed\n");
		return;
	}

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
	if (vs == NULL) {
		ast_log(LOG_WARNING, "malloc() failed\n");
		free_value(a);
		free_value(b);
		return NULL;
	}

	strcpy(vs,a->u.s);
	strcat(vs,b->u.s);

	v = make_str(vs);

	free(vs);

	/* free arguments */
	free_value(a);
	free_value(b);

	return v;
}
