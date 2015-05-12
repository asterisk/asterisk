/* A Bison parser, made by GNU Bison 2.5.  */

/* Bison implementation for Yacc-like parsers in C
   
      Copyright (C) 1984, 1989-1990, 2000-2011 Free Software Foundation, Inc.
   
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
#define YYBISON_VERSION "2.5"

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
#define yyparse         ael_yyparse
#define yylex           ael_yylex
#define yyerror         ael_yyerror
#define yylval          ael_yylval
#define yychar          ael_yychar
#define yydebug         ael_yydebug
#define yynerrs         ael_yynerrs
#define yylloc          ael_yylloc

/* Copy the first part of user declarations.  */

/* Line 268 of yacc.c  */
#line 1 "ael.y"

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

#define ASTMM_LIBC ASTMM_REDIRECT
#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/hashtab.h"
#include "asterisk/ael_structs.h"
#include "asterisk/utils.h"

extern struct ast_flags ast_compat;

pval * linku1(pval *head, pval *tail);
static void set_dads(pval *dad, pval *child_list);
void reset_parencount(yyscan_t yyscanner);
void reset_semicount(yyscan_t yyscanner);
void reset_argcount(yyscan_t yyscanner );
 
#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner
#define YYERROR_VERBOSE 1

extern char *my_file;
#ifdef AAL_ARGCHECK
int ael_is_funcname(char *name);
#endif
static char *ael_token_subst(const char *mess);



/* Line 268 of yacc.c  */
#line 137 "ael.tab.c"

/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 1
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
     KW_CONTEXT = 258,
     LC = 259,
     RC = 260,
     LP = 261,
     RP = 262,
     SEMI = 263,
     EQ = 264,
     COMMA = 265,
     COLON = 266,
     AMPER = 267,
     BAR = 268,
     AT = 269,
     KW_MACRO = 270,
     KW_GLOBALS = 271,
     KW_IGNOREPAT = 272,
     KW_SWITCH = 273,
     KW_IF = 274,
     KW_IFTIME = 275,
     KW_ELSE = 276,
     KW_RANDOM = 277,
     KW_ABSTRACT = 278,
     KW_EXTEND = 279,
     EXTENMARK = 280,
     KW_GOTO = 281,
     KW_JUMP = 282,
     KW_RETURN = 283,
     KW_BREAK = 284,
     KW_CONTINUE = 285,
     KW_REGEXTEN = 286,
     KW_HINT = 287,
     KW_FOR = 288,
     KW_WHILE = 289,
     KW_CASE = 290,
     KW_PATTERN = 291,
     KW_DEFAULT = 292,
     KW_CATCH = 293,
     KW_SWITCHES = 294,
     KW_ESWITCHES = 295,
     KW_INCLUDES = 296,
     KW_LOCAL = 297,
     word = 298
   };
#endif



#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
{

/* Line 293 of yacc.c  */
#line 59 "ael.y"

	int	intval;		/* integer value, typically flags */
	char	*str;		/* strings */
	struct pval *pval;	/* full objects */



/* Line 293 of yacc.c  */
#line 224 "ael.tab.c"
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

/* Line 343 of yacc.c  */
#line 65 "ael.y"

	/* declaring these AFTER the union makes things a lot simpler! */
void yyerror(YYLTYPE *locp, struct parse_io *parseio, char const *s);
int ael_yylex (YYSTYPE * yylval_param, YYLTYPE * yylloc_param , void * yyscanner);

/* create a new object with start-end marker */
pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column);

/* create a new object with start-end marker, simplified interface.
 * Must be declared here because YYLTYPE is not known before
 */
static pval *npval2(pvaltype type, YYLTYPE *first, YYLTYPE *last);

/* another frontend for npval, this time for a string */
static pval *nword(char *string, YYLTYPE *pos);

/* update end position of an object, return the object */
static pval *update_last(pval *, YYLTYPE *);


/* Line 343 of yacc.c  */
#line 271 "ael.tab.c"

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
# if defined YYENABLE_NLS && YYENABLE_NLS
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
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
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
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS && (defined __STDC__ || defined __C99__FUNC__ \
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

# define YYCOPY_NEEDED 1

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

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
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
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  17
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   371

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  44
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  56
/* YYNRULES -- Number of rules.  */
#define YYNRULES  143
/* YYNRULES -- Number of states.  */
#define YYNSTATES  283

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   298

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
      25,    26,    27,    28,    29,    30,    31,    32,    33,    34,
      35,    36,    37,    38,    39,    40,    41,    42,    43
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     5,     7,    10,    13,    15,    17,    19,
      21,    23,    25,    32,    34,    35,    37,    40,    43,    52,
      57,    58,    61,    64,    65,    71,    72,    79,    80,    82,
      86,    89,    90,    93,    96,    98,   100,   102,   104,   106,
     108,   110,   113,   115,   120,   124,   130,   135,   143,   152,
     153,   156,   159,   165,   167,   175,   176,   181,   184,   187,
     192,   194,   197,   199,   202,   206,   210,   214,   216,   219,
     223,   225,   228,   232,   238,   242,   244,   246,   250,   254,
     257,   258,   259,   260,   273,   277,   279,   283,   286,   289,
     290,   296,   299,   302,   305,   309,   311,   314,   315,   317,
     321,   325,   331,   337,   343,   349,   350,   353,   356,   361,
     362,   368,   372,   373,   377,   381,   384,   386,   387,   389,
     390,   394,   395,   398,   403,   407,   412,   413,   416,   418,
     420,   426,   431,   436,   437,   441,   447,   450,   452,   456,
     459,   463,   466,   471
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      45,     0,    -1,    46,    -1,    47,    -1,    46,    47,    -1,
      46,     1,    -1,    49,    -1,    51,    -1,    52,    -1,     8,
      -1,    43,    -1,    37,    -1,    50,     3,    48,     4,    59,
       5,    -1,    23,    -1,    -1,    24,    -1,    24,    23,    -1,
      23,    24,    -1,    15,    43,     6,    58,     7,     4,    92,
       5,    -1,    16,     4,    53,     5,    -1,    -1,    53,    54,
      -1,     1,    53,    -1,    -1,    43,     9,    55,    43,     8,
      -1,    -1,    42,    43,     9,    57,    43,     8,    -1,    -1,
      43,    -1,    58,    10,    43,    -1,    58,     1,    -1,    -1,
      59,    60,    -1,     1,    59,    -1,    62,    -1,    99,    -1,
      94,    -1,    95,    -1,    61,    -1,    54,    -1,    56,    -1,
      43,     1,    -1,     8,    -1,    17,    25,    43,     8,    -1,
      43,    25,    74,    -1,    43,    14,    43,    25,    74,    -1,
      31,    43,    25,    74,    -1,    32,     6,    70,     7,    43,
      25,    74,    -1,    31,    32,     6,    70,     7,    43,    25,
      74,    -1,    -1,    63,    74,    -1,     1,    63,    -1,    71,
      11,    71,    11,    71,    -1,    43,    -1,    64,    13,    71,
      13,    71,    13,    71,    -1,    -1,     6,    67,    69,     7,
      -1,    19,    66,    -1,    22,    66,    -1,    20,     6,    65,
       7,    -1,    43,    -1,    43,    43,    -1,    43,    -1,    70,
      43,    -1,    70,    11,    43,    -1,    70,    12,    43,    -1,
      70,    14,    43,    -1,    43,    -1,    43,    43,    -1,    43,
      43,    43,    -1,    43,    -1,    43,    43,    -1,    72,    11,
      43,    -1,    18,    66,     4,    90,     5,    -1,     4,    63,
       5,    -1,    54,    -1,    56,    -1,    26,    80,     8,    -1,
      27,    82,     8,    -1,    43,    11,    -1,    -1,    -1,    -1,
      33,     6,    75,    43,     8,    76,    43,     8,    77,    43,
       7,    74,    -1,    34,    66,    74,    -1,    73,    -1,    12,
      83,     8,    -1,    87,     8,    -1,    43,     8,    -1,    -1,
      87,     9,    78,    43,     8,    -1,    29,     8,    -1,    28,
       8,    -1,    30,     8,    -1,    68,    74,    79,    -1,     8,
      -1,    21,    74,    -1,    -1,    72,    -1,    72,    13,    72,
      -1,    72,    10,    72,    -1,    72,    13,    72,    13,    72,
      -1,    72,    10,    72,    10,    72,    -1,    37,    13,    72,
      13,    72,    -1,    37,    10,    72,    10,    72,    -1,    -1,
      10,    43,    -1,    72,    81,    -1,    72,    81,    14,    48,
      -1,    -1,    43,     6,    84,    89,     7,    -1,    43,     6,
       7,    -1,    -1,    43,     6,    86,    -1,    85,    89,     7,
      -1,    85,     7,    -1,    43,    -1,    -1,    69,    -1,    -1,
      89,    10,    88,    -1,    -1,    90,    91,    -1,    35,    43,
      11,    63,    -1,    37,    11,    63,    -1,    36,    43,    11,
      63,    -1,    -1,    92,    93,    -1,    74,    -1,    99,    -1,
      38,    43,     4,    63,     5,    -1,    39,     4,    96,     5,
      -1,    40,     4,    96,     5,    -1,    -1,    96,    43,     8,
      -1,    96,    43,    14,    43,     8,    -1,     1,    96,    -1,
      48,    -1,    48,    13,    65,    -1,    97,     8,    -1,    98,
      97,     8,    -1,    98,     1,    -1,    41,     4,    98,     5,
      -1,    41,     4,     5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   191,   191,   194,   195,   196,   199,   200,   201,   202,
     205,   206,   209,   218,   219,   220,   221,   222,   225,   231,
     237,   238,   239,   242,   242,   248,   248,   255,   256,   257,
     258,   261,   262,   263,   266,   267,   268,   269,   270,   271,
     272,   273,   274,   277,   282,   286,   294,   299,   304,   313,
     314,   315,   321,   331,   335,   343,   343,   347,   350,   353,
     364,   365,   377,   378,   387,   396,   405,   416,   417,   427,
     440,   441,   450,   461,   470,   473,   474,   475,   478,   481,
     484,   485,   486,   484,   492,   496,   497,   498,   499,   502,
     502,   535,   536,   537,   538,   542,   545,   546,   549,   550,
     553,   556,   560,   564,   568,   574,   575,   579,   582,   588,
     588,   593,   601,   601,   612,   619,   622,   623,   626,   627,
     630,   633,   634,   637,   641,   645,   651,   652,   655,   656,
     657,   663,   668,   673,   674,   675,   686,   689,   690,   697,
     698,   699,   702,   705
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "KW_CONTEXT", "LC", "RC", "LP", "RP",
  "SEMI", "EQ", "COMMA", "COLON", "AMPER", "BAR", "AT", "KW_MACRO",
  "KW_GLOBALS", "KW_IGNOREPAT", "KW_SWITCH", "KW_IF", "KW_IFTIME",
  "KW_ELSE", "KW_RANDOM", "KW_ABSTRACT", "KW_EXTEND", "EXTENMARK",
  "KW_GOTO", "KW_JUMP", "KW_RETURN", "KW_BREAK", "KW_CONTINUE",
  "KW_REGEXTEN", "KW_HINT", "KW_FOR", "KW_WHILE", "KW_CASE", "KW_PATTERN",
  "KW_DEFAULT", "KW_CATCH", "KW_SWITCHES", "KW_ESWITCHES", "KW_INCLUDES",
  "KW_LOCAL", "word", "$accept", "file", "objects", "object",
  "context_name", "context", "opt_abstract", "macro", "globals",
  "global_statements", "assignment", "$@1", "local_assignment", "$@2",
  "arglist", "elements", "element", "ignorepat", "extension", "statements",
  "timerange", "timespec", "test_expr", "$@3", "if_like_head", "word_list",
  "hint_word", "word3_list", "goto_word", "switch_statement", "statement",
  "$@4", "$@5", "$@6", "$@7", "opt_else", "target", "opt_pri",
  "jumptarget", "macro_call", "$@8", "application_call_head", "$@9",
  "application_call", "opt_word", "eval_arglist", "case_statements",
  "case_statement", "macro_statements", "macro_statement", "switches",
  "eswitches", "switchlist", "included_entry", "includeslist", "includes", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296,   297,   298
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    44,    45,    46,    46,    46,    47,    47,    47,    47,
      48,    48,    49,    50,    50,    50,    50,    50,    51,    52,
      53,    53,    53,    55,    54,    57,    56,    58,    58,    58,
      58,    59,    59,    59,    60,    60,    60,    60,    60,    60,
      60,    60,    60,    61,    62,    62,    62,    62,    62,    63,
      63,    63,    64,    64,    65,    67,    66,    68,    68,    68,
      69,    69,    70,    70,    70,    70,    70,    71,    71,    71,
      72,    72,    72,    73,    74,    74,    74,    74,    74,    74,
      75,    76,    77,    74,    74,    74,    74,    74,    74,    78,
      74,    74,    74,    74,    74,    74,    79,    79,    80,    80,
      80,    80,    80,    80,    80,    81,    81,    82,    82,    84,
      83,    83,    86,    85,    87,    87,    88,    88,    89,    89,
      89,    90,    90,    91,    91,    91,    92,    92,    93,    93,
      93,    94,    95,    96,    96,    96,    96,    97,    97,    98,
      98,    98,    99,    99
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     1,     6,     1,     0,     1,     2,     2,     8,     4,
       0,     2,     2,     0,     5,     0,     6,     0,     1,     3,
       2,     0,     2,     2,     1,     1,     1,     1,     1,     1,
       1,     2,     1,     4,     3,     5,     4,     7,     8,     0,
       2,     2,     5,     1,     7,     0,     4,     2,     2,     4,
       1,     2,     1,     2,     3,     3,     3,     1,     2,     3,
       1,     2,     3,     5,     3,     1,     1,     3,     3,     2,
       0,     0,     0,    12,     3,     1,     3,     2,     2,     0,
       5,     2,     2,     2,     3,     1,     2,     0,     1,     3,
       3,     5,     5,     5,     5,     0,     2,     2,     4,     0,
       5,     3,     0,     3,     3,     2,     1,     0,     1,     0,
       3,     0,     2,     4,     3,     4,     0,     2,     1,     1,
       5,     4,     4,     0,     3,     5,     2,     1,     3,     2,
       3,     2,     4,     3
};

/* YYDEFACT[STATE-NAME] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
      14,     9,     0,     0,    13,    15,     0,     0,     3,     6,
       0,     7,     8,     0,     0,    17,    16,     1,     5,     4,
       0,    27,     0,     0,    11,    10,     0,    28,     0,    22,
      19,     0,    21,     0,    30,     0,     0,    23,     0,     0,
     126,    29,     0,    33,    12,    42,     0,     0,     0,     0,
       0,     0,     0,     0,    39,    40,    32,    38,    34,    36,
      37,    35,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    41,     0,     0,     0,    18,    95,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    75,    76,     0,    85,   128,   119,     0,   127,
     129,    24,     0,     0,     0,    62,     0,     0,     0,     0,
     143,   137,     0,     0,    25,     0,    44,     0,     0,     0,
       0,    55,     0,    57,     0,    58,     0,    70,    98,     0,
     105,     0,    92,    91,    93,    80,     0,     0,   112,    88,
      79,    97,   115,    60,   118,     0,    87,    89,    43,     0,
      46,     0,     0,     0,     0,    63,   136,   131,     0,   132,
       0,   139,   141,   142,     0,     0,     0,    51,    74,    50,
     109,    86,     0,   121,    53,     0,     0,     0,     0,     0,
      71,     0,     0,     0,    77,     0,   107,    78,     0,    84,
       0,   113,     0,    94,    61,   114,   117,     0,     0,     0,
      64,    65,    66,   134,     0,   138,   140,     0,    45,   111,
     119,     0,     0,    68,     0,    59,     0,     0,     0,   100,
      72,    99,   106,     0,     0,     0,    96,   116,   120,     0,
       0,     0,     0,    26,     0,    56,    73,     0,     0,     0,
     122,    69,    67,     0,     0,     0,     0,     0,     0,   108,
      81,   130,    90,     0,    47,   135,   110,     0,     0,     0,
       0,     0,   104,   103,   102,   101,     0,    48,     0,     0,
     124,     0,    52,     0,   123,   125,     0,    82,    54,     0,
       0,     0,    83
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     6,     7,     8,   111,     9,    10,    11,    12,    23,
      92,    42,    93,   165,    28,    39,    56,    57,    58,   118,
     175,   176,   122,   172,    94,   144,   106,   177,   128,    95,
     169,   188,   266,   279,   197,   193,   129,   186,   131,   120,
     210,    97,   191,    98,   228,   145,   212,   240,    62,    99,
      59,    60,   108,   112,   113,    61
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -211
static const yytype_int16 yypact[] =
{
     166,  -211,   -32,    15,    12,    36,    40,   316,  -211,  -211,
      75,  -211,  -211,    82,    17,  -211,  -211,  -211,  -211,  -211,
     -28,    57,    17,     0,  -211,  -211,   127,  -211,     6,   109,
    -211,   152,  -211,   136,  -211,   169,   143,  -211,   136,   117,
    -211,  -211,   144,   272,  -211,  -211,   170,   -15,   191,   197,
     199,   201,   168,   137,  -211,  -211,  -211,  -211,  -211,  -211,
    -211,  -211,   180,   204,   172,   219,   202,   185,    25,    25,
      28,   217,  -211,   186,   266,    90,  -211,  -211,   190,   229,
     229,   230,   229,    21,   194,   240,   241,   242,   246,   229,
     210,   312,  -211,  -211,   266,  -211,  -211,     1,    61,  -211,
    -211,  -211,   248,   185,   266,  -211,    68,    25,    24,    29,
    -211,   247,   254,    20,  -211,   238,  -211,    19,   212,   258,
     263,  -211,   271,  -211,   233,  -211,   126,   234,   183,   275,
      95,   279,  -211,  -211,  -211,  -211,   266,   286,  -211,  -211,
    -211,   270,  -211,   236,  -211,   140,  -211,  -211,  -211,    78,
    -211,   255,   259,   262,   264,  -211,   267,  -211,    69,  -211,
     233,  -211,  -211,  -211,   289,   282,   266,   266,  -211,  -211,
     294,  -211,   283,  -211,    70,   293,   315,   317,   194,   194,
    -211,   194,   284,   194,  -211,   287,   319,  -211,   291,  -211,
      90,  -211,   266,  -211,  -211,  -211,   292,   295,   298,   304,
    -211,  -211,  -211,  -211,   299,  -211,  -211,   328,  -211,  -211,
     283,   330,   135,   300,   301,  -211,   301,   104,    73,   132,
    -211,   116,  -211,   -28,   337,   239,  -211,  -211,  -211,   338,
     322,   266,   340,  -211,   173,  -211,  -211,   306,   307,   341,
    -211,  -211,   308,   342,   343,   194,   194,   194,   194,  -211,
    -211,  -211,  -211,   266,  -211,  -211,  -211,   345,   346,    19,
     301,   301,   347,   347,   347,   347,   310,  -211,    19,    19,
     266,   348,  -211,   351,   266,   266,   301,  -211,  -211,   320,
     353,   266,  -211
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -211,  -211,  -211,   355,   -19,  -211,  -211,  -211,  -211,   344,
      64,  -211,   -29,  -211,  -211,   326,  -211,  -211,  -211,  -114,
    -211,   205,    46,  -211,  -211,   195,   265,  -210,   -82,  -211,
     -62,  -211,  -211,  -211,  -211,  -211,  -211,  -211,  -211,  -211,
    -211,  -211,  -211,  -211,  -211,   159,  -211,  -211,  -211,  -211,
    -211,  -211,   -34,   257,  -211,   309
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -134
static const yytype_int16 yytable[] =
{
      96,    26,   130,   167,   243,    30,   244,    34,   142,    24,
      55,    13,   116,    35,    55,    25,    36,    65,    22,    14,
     117,   162,   -20,   -49,   -49,   163,   107,   -49,    66,   157,
    -133,   -49,   141,   110,   159,   109,    15,   -49,   -49,   -49,
      17,   -49,   150,    31,   143,   -49,   -49,   -49,   -49,   -49,
     271,   272,   -49,   -49,   -49,   -49,   -49,    24,   126,    16,
     -20,   -49,   -49,    25,   127,    24,   278,   158,  -133,   146,
     147,    25,   158,   156,   189,   151,   225,   203,    20,   152,
     153,   -67,   154,   204,   182,   198,   246,    32,    21,   152,
     153,   117,   154,    32,   -49,   -49,   217,   218,   -49,   219,
      27,   221,   -49,    54,   208,   185,   182,    54,   -49,   -49,
     -49,   155,   -49,   213,   245,   182,   -49,   -49,   -49,   -49,
     -49,   155,    44,   -49,   -49,    45,   123,   182,   125,   248,
     226,    33,   -49,   -49,    46,   136,   178,    38,    72,   179,
     236,   -31,   247,   182,   -31,   270,    37,   195,    47,    48,
     196,    73,    31,   -31,   274,   275,    49,    50,    51,    52,
      53,    37,    74,   262,   263,   264,   265,   -31,   -31,   254,
     237,   238,   239,    40,     1,   -31,   -31,   -31,   -31,   -31,
     256,     2,     3,   196,    75,    76,    41,    63,    77,     4,
       5,   267,    78,   181,   182,    64,   183,    67,    79,    80,
      81,    68,    82,    69,   249,    70,    83,    84,    85,    86,
      87,    71,   101,    88,    89,   102,    75,   168,    90,   282,
      77,    51,    52,    91,    78,   103,   114,   104,   105,   115,
      79,    80,    81,   119,    82,   121,   124,   127,    83,    84,
      85,    86,    87,    75,   251,    88,    89,    77,   132,   133,
     134,    78,   135,   137,    52,    91,   148,    79,    80,    81,
     160,    82,   161,   166,   170,    83,    84,    85,    86,    87,
      75,   171,    88,    89,    77,   173,   174,   180,    78,   194,
      45,    52,    91,   184,    79,    80,    81,   187,    82,    46,
     190,   192,    83,    84,    85,    86,    87,   206,   199,    88,
      89,   209,   200,    47,    48,   201,   214,   202,    52,    91,
     158,    49,    50,    51,    52,    53,    -2,    18,   138,   -14,
     139,    37,   215,   140,     1,   207,   143,   220,   216,   231,
     222,     2,     3,   223,   224,   227,   233,   235,   229,     4,
       5,   230,   232,   241,   242,   250,   252,   253,   255,   257,
     258,   213,   259,   273,   261,   260,   268,   269,   182,   277,
     281,   276,    19,   280,    43,   205,    29,   211,   149,   234,
     164,   100
};

#define yypact_value_is_default(yystate) \
  ((yystate) == (-211))

#define yytable_value_is_error(yytable_value) \
  YYID (0)

static const yytype_uint16 yycheck[] =
{
      62,    20,    84,   117,   214,     5,   216,     1,     7,    37,
      39,    43,    74,     7,    43,    43,    10,    32,     1,     4,
       1,     1,     5,     4,     5,     5,     1,     8,    43,     5,
       5,    12,    94,     5,     5,    69,    24,    18,    19,    20,
       0,    22,   104,    43,    43,    26,    27,    28,    29,    30,
     260,   261,    33,    34,    35,    36,    37,    37,    37,    23,
      43,    42,    43,    43,    43,    37,   276,    43,    43,     8,
       9,    43,    43,   107,   136,     7,   190,     8,     3,    11,
      12,    11,    14,    14,    11,     7,    13,    23,     6,    11,
      12,     1,    14,    29,     4,     5,   178,   179,     8,   181,
      43,   183,    12,    39,   166,    10,    11,    43,    18,    19,
      20,    43,    22,    43,    10,    11,    26,    27,    28,    29,
      30,    43,     5,    33,    34,     8,    80,    11,    82,    13,
     192,     4,    42,    43,    17,    89,    10,     1,     1,    13,
       5,     5,    10,    11,     8,   259,     9,     7,    31,    32,
      10,    14,    43,    17,   268,   269,    39,    40,    41,    42,
      43,     9,    25,   245,   246,   247,   248,    31,    32,   231,
      35,    36,    37,     4,     8,    39,    40,    41,    42,    43,
       7,    15,    16,    10,     4,     5,    43,    43,     8,    23,
      24,   253,    12,    10,    11,    25,    13,     6,    18,    19,
      20,     4,    22,     4,   223,     4,    26,    27,    28,    29,
      30,    43,     8,    33,    34,    43,     4,     5,    38,   281,
       8,    41,    42,    43,    12,     6,     9,    25,    43,    43,
      18,    19,    20,    43,    22,     6,     6,    43,    26,    27,
      28,    29,    30,     4,     5,    33,    34,     8,     8,     8,
       8,    12,     6,    43,    42,    43,     8,    18,    19,    20,
      13,    22,     8,    25,     6,    26,    27,    28,    29,    30,
       4,     8,    33,    34,     8,     4,    43,    43,    12,    43,
       8,    42,    43,     8,    18,    19,    20,     8,    22,    17,
       4,    21,    26,    27,    28,    29,    30,     8,    43,    33,
      34,     7,    43,    31,    32,    43,    13,    43,    42,    43,
      43,    39,    40,    41,    42,    43,     0,     1,     6,     3,
       8,     9,     7,    11,     8,    43,    43,    43,    11,    25,
      43,    15,    16,    14,    43,    43,     8,     7,    43,    23,
      24,    43,    43,    43,    43,     8,     8,    25,     8,    43,
      43,    43,    11,    43,    11,    13,    11,    11,    11,     8,
       7,    13,     7,    43,    38,   160,    22,   172,   103,   210,
     113,    62
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     8,    15,    16,    23,    24,    45,    46,    47,    49,
      50,    51,    52,    43,     4,    24,    23,     0,     1,    47,
       3,     6,     1,    53,    37,    43,    48,    43,    58,    53,
       5,    43,    54,     4,     1,     7,    10,     9,     1,    59,
       4,    43,    55,    59,     5,     8,    17,    31,    32,    39,
      40,    41,    42,    43,    54,    56,    60,    61,    62,    94,
      95,    99,    92,    43,    25,    32,    43,     6,     4,     4,
       4,    43,     1,    14,    25,     4,     5,     8,    12,    18,
      19,    20,    22,    26,    27,    28,    29,    30,    33,    34,
      38,    43,    54,    56,    68,    73,    74,    85,    87,    93,
      99,     8,    43,     6,    25,    43,    70,     1,    96,    96,
       5,    48,    97,    98,     9,    43,    74,     1,    63,    43,
      83,     6,    66,    66,     6,    66,    37,    43,    72,    80,
      72,    82,     8,     8,     8,     6,    66,    43,     6,     8,
      11,    74,     7,    43,    69,    89,     8,     9,     8,    70,
      74,     7,    11,    12,    14,    43,    96,     5,    43,     5,
      13,     8,     1,     5,    97,    57,    25,    63,     5,    74,
       6,     8,    67,     4,    43,    64,    65,    71,    10,    13,
      43,    10,    11,    13,     8,    10,    81,     8,    75,    74,
       4,    86,    21,    79,    43,     7,    10,    78,     7,    43,
      43,    43,    43,     8,    14,    65,     8,    43,    74,     7,
      84,    69,    90,    43,    13,     7,    11,    72,    72,    72,
      43,    72,    43,    14,    43,    63,    74,    43,    88,    43,
      43,    25,    43,     8,    89,     7,     5,    35,    36,    37,
      91,    43,    43,    71,    71,    10,    13,    10,    13,    48,
       8,     5,     8,    25,    74,     8,     7,    43,    43,    11,
      13,    11,    72,    72,    72,    72,    76,    74,    11,    11,
      63,    71,    71,    43,    63,    63,    13,     8,    71,    77,
      43,     7,    74
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
   Once GCC version 2 has supplanted version 1, this can go.  However,
   YYFAIL appears to be in use.  Nevertheless, it is formally deprecated
   in Bison 2.4.2's NEWS entry, where a plan to phase it out is
   discussed.  */

#define YYFAIL		goto yyerrlab
#if defined YYFAIL
  /* This is here to suppress warnings from the GCC cpp's
     -Wunused-macros.  Normally we don't worry about that warning, but
     some users do, and we want to make it easy for users to remove
     YYFAIL uses, which will produce warnings from Bison 2.5.  */
#endif

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (&yylloc, parseio, YY_("syntax error: cannot back up")); \
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
# if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
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
		  Type, Value, Location, parseio); \
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
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, struct parse_io *parseio)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp, parseio)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
    struct parse_io *parseio;
#endif
{
  if (!yyvaluep)
    return;
  YYUSE (yylocationp);
  YYUSE (parseio);
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
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp, struct parse_io *parseio)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep, yylocationp, parseio)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
    struct parse_io *parseio;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  YY_LOCATION_PRINT (yyoutput, *yylocationp);
  YYFPRINTF (yyoutput, ": ");
  yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp, parseio);
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
yy_reduce_print (YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule, struct parse_io *parseio)
#else
static void
yy_reduce_print (yyvsp, yylsp, yyrule, parseio)
    YYSTYPE *yyvsp;
    YYLTYPE *yylsp;
    int yyrule;
    struct parse_io *parseio;
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
		       , &(yylsp[(yyi + 1) - (yynrhs)])		       , parseio);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, yylsp, Rule, parseio); \
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

/* Copy into *YYMSG, which is of size *YYMSG_ALLOC, an error message
   about the unexpected token YYTOKEN for the state stack whose top is
   YYSSP.

   Return 0 if *YYMSG was successfully written.  Return 1 if *YYMSG is
   not large enough to hold the message.  In that case, also set
   *YYMSG_ALLOC to the required number of bytes.  Return 2 if the
   required number of bytes is too large to store.  */
static int
yysyntax_error (YYSIZE_T *yymsg_alloc, char **yymsg,
                yytype_int16 *yyssp, int yytoken)
{
  YYSIZE_T yysize0 = yytnamerr (0, yytname[yytoken]);
  YYSIZE_T yysize = yysize0;
  YYSIZE_T yysize1;
  enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
  /* Internationalized format string. */
  const char *yyformat = 0;
  /* Arguments of yyformat. */
  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
  /* Number of reported tokens (one for the "unexpected", one per
     "expected"). */
  int yycount = 0;

  /* There are many possibilities here to consider:
     - Assume YYFAIL is not used.  It's too flawed to consider.  See
       <http://lists.gnu.org/archive/html/bison-patches/2009-12/msg00024.html>
       for details.  YYERROR is fine as it does not invoke this
       function.
     - If this state is a consistent state with a default action, then
       the only way this function was invoked is if the default action
       is an error action.  In that case, don't check for expected
       tokens because there are none.
     - The only way there can be no lookahead present (in yychar) is if
       this state is a consistent state with a default action.  Thus,
       detecting the absence of a lookahead is sufficient to determine
       that there is no unexpected or expected token to report.  In that
       case, just report a simple "syntax error".
     - Don't assume there isn't a lookahead just because this state is a
       consistent state with a default action.  There might have been a
       previous inconsistent state, consistent state with a non-default
       action, or user semantic action that manipulated yychar.
     - Of course, the expected token list depends on states to have
       correct lookahead information, and it depends on the parser not
       to perform extra reductions after fetching a lookahead from the
       scanner and before detecting a syntax error.  Thus, state merging
       (from LALR or IELR) and default reductions corrupt the expected
       token list.  However, the list is correct for canonical LR with
       one exception: it will still contain any token that will not be
       accepted due to an error action in a later state.
  */
  if (yytoken != YYEMPTY)
    {
      int yyn = yypact[*yyssp];
      yyarg[yycount++] = yytname[yytoken];
      if (!yypact_value_is_default (yyn))
        {
          /* Start YYX at -YYN if negative to avoid negative indexes in
             YYCHECK.  In other words, skip the first -YYN actions for
             this state because they are default actions.  */
          int yyxbegin = yyn < 0 ? -yyn : 0;
          /* Stay within bounds of both yycheck and yytname.  */
          int yychecklim = YYLAST - yyn + 1;
          int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
          int yyx;

          for (yyx = yyxbegin; yyx < yyxend; ++yyx)
            if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR
                && !yytable_value_is_error (yytable[yyx + yyn]))
              {
                if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
                  {
                    yycount = 1;
                    yysize = yysize0;
                    break;
                  }
                yyarg[yycount++] = yytname[yyx];
                yysize1 = yysize + yytnamerr (0, yytname[yyx]);
                if (! (yysize <= yysize1
                       && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
                  return 2;
                yysize = yysize1;
              }
        }
    }

  switch (yycount)
    {
# define YYCASE_(N, S)                      \
      case N:                               \
        yyformat = S;                       \
      break
      YYCASE_(0, YY_("syntax error"));
      YYCASE_(1, YY_("syntax error, unexpected %s"));
      YYCASE_(2, YY_("syntax error, unexpected %s, expecting %s"));
      YYCASE_(3, YY_("syntax error, unexpected %s, expecting %s or %s"));
      YYCASE_(4, YY_("syntax error, unexpected %s, expecting %s or %s or %s"));
      YYCASE_(5, YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s"));
# undef YYCASE_
    }

  yysize1 = yysize + yystrlen (yyformat);
  if (! (yysize <= yysize1 && yysize1 <= YYSTACK_ALLOC_MAXIMUM))
    return 2;
  yysize = yysize1;

  if (*yymsg_alloc < yysize)
    {
      *yymsg_alloc = 2 * yysize;
      if (! (yysize <= *yymsg_alloc
             && *yymsg_alloc <= YYSTACK_ALLOC_MAXIMUM))
        *yymsg_alloc = YYSTACK_ALLOC_MAXIMUM;
      return 1;
    }

  /* Avoid sprintf, as that infringes on the user's name space.
     Don't have undefined behavior even if the translation
     produced a string with the wrong number of "%s"s.  */
  {
    char *yyp = *yymsg;
    int yyi = 0;
    while ((*yyp = *yyformat) != '\0')
      if (*yyp == '%' && yyformat[1] == 's' && yyi < yycount)
        {
          yyp += yytnamerr (yyp, yyarg[yyi++]);
          yyformat += 2;
        }
      else
        {
          yyp++;
          yyformat++;
        }
  }
  return 0;
}
#endif /* YYERROR_VERBOSE */

/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp, struct parse_io *parseio)
#else
static void
yydestruct (yymsg, yytype, yyvaluep, yylocationp, parseio)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
    struct parse_io *parseio;
#endif
{
  YYUSE (yyvaluep);
  YYUSE (yylocationp);
  YYUSE (parseio);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {
      case 43: /* "word" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1506 "ael.tab.c"
	break;
      case 46: /* "objects" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1518 "ael.tab.c"
	break;
      case 47: /* "object" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1530 "ael.tab.c"
	break;
      case 48: /* "context_name" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1539 "ael.tab.c"
	break;
      case 49: /* "context" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1551 "ael.tab.c"
	break;
      case 51: /* "macro" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1563 "ael.tab.c"
	break;
      case 52: /* "globals" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1575 "ael.tab.c"
	break;
      case 53: /* "global_statements" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1587 "ael.tab.c"
	break;
      case 54: /* "assignment" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1599 "ael.tab.c"
	break;
      case 56: /* "local_assignment" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1611 "ael.tab.c"
	break;
      case 58: /* "arglist" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1623 "ael.tab.c"
	break;
      case 59: /* "elements" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1635 "ael.tab.c"
	break;
      case 60: /* "element" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1647 "ael.tab.c"
	break;
      case 61: /* "ignorepat" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1659 "ael.tab.c"
	break;
      case 62: /* "extension" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1671 "ael.tab.c"
	break;
      case 63: /* "statements" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1683 "ael.tab.c"
	break;
      case 64: /* "timerange" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1692 "ael.tab.c"
	break;
      case 65: /* "timespec" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1704 "ael.tab.c"
	break;
      case 66: /* "test_expr" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1713 "ael.tab.c"
	break;
      case 68: /* "if_like_head" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1725 "ael.tab.c"
	break;
      case 69: /* "word_list" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1734 "ael.tab.c"
	break;
      case 71: /* "word3_list" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1743 "ael.tab.c"
	break;
      case 72: /* "goto_word" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1752 "ael.tab.c"
	break;
      case 73: /* "switch_statement" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1764 "ael.tab.c"
	break;
      case 74: /* "statement" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1776 "ael.tab.c"
	break;
      case 79: /* "opt_else" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1788 "ael.tab.c"
	break;
      case 80: /* "target" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1800 "ael.tab.c"
	break;
      case 81: /* "opt_pri" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1809 "ael.tab.c"
	break;
      case 82: /* "jumptarget" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1821 "ael.tab.c"
	break;
      case 83: /* "macro_call" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1833 "ael.tab.c"
	break;
      case 85: /* "application_call_head" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1845 "ael.tab.c"
	break;
      case 87: /* "application_call" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1857 "ael.tab.c"
	break;
      case 88: /* "opt_word" */

/* Line 1391 of yacc.c  */
#line 183 "ael.y"
	{ free((yyvaluep->str));};

/* Line 1391 of yacc.c  */
#line 1866 "ael.tab.c"
	break;
      case 89: /* "eval_arglist" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1878 "ael.tab.c"
	break;
      case 90: /* "case_statements" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1890 "ael.tab.c"
	break;
      case 91: /* "case_statement" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1902 "ael.tab.c"
	break;
      case 92: /* "macro_statements" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1914 "ael.tab.c"
	break;
      case 93: /* "macro_statement" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1926 "ael.tab.c"
	break;
      case 94: /* "switches" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1938 "ael.tab.c"
	break;
      case 95: /* "eswitches" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1950 "ael.tab.c"
	break;
      case 96: /* "switchlist" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1962 "ael.tab.c"
	break;
      case 97: /* "included_entry" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1974 "ael.tab.c"
	break;
      case 98: /* "includeslist" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1986 "ael.tab.c"
	break;
      case 99: /* "includes" */

/* Line 1391 of yacc.c  */
#line 170 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};

/* Line 1391 of yacc.c  */
#line 1998 "ael.tab.c"
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
int yyparse (struct parse_io *parseio);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */


/*----------.
| yyparse.  |
`----------*/

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
yyparse (struct parse_io *parseio)
#else
int
yyparse (parseio)
    struct parse_io *parseio;
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
    YYLTYPE yyerror_range[3];

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

#if defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL
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
  if (yypact_value_is_default (yyn))
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
      if (yytable_value_is_error (yyn))
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

/* Line 1806 of yacc.c  */
#line 191 "ael.y"
    { (yyval.pval) = parseio->pval = (yyvsp[(1) - (1)].pval); }
    break;

  case 3:

/* Line 1806 of yacc.c  */
#line 194 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 4:

/* Line 1806 of yacc.c  */
#line 195 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); }
    break;

  case 5:

/* Line 1806 of yacc.c  */
#line 196 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (2)].pval);}
    break;

  case 6:

/* Line 1806 of yacc.c  */
#line 199 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 7:

/* Line 1806 of yacc.c  */
#line 200 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 8:

/* Line 1806 of yacc.c  */
#line 201 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 9:

/* Line 1806 of yacc.c  */
#line 202 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */}
    break;

  case 10:

/* Line 1806 of yacc.c  */
#line 205 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); }
    break;

  case 11:

/* Line 1806 of yacc.c  */
#line 206 "ael.y"
    { (yyval.str) = strdup("default"); }
    break;

  case 12:

/* Line 1806 of yacc.c  */
#line 209 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[(1) - (6)]), &(yylsp[(6) - (6)]));
		(yyval.pval)->u1.str = (yyvsp[(3) - (6)].str);
		(yyval.pval)->u2.statements = (yyvsp[(5) - (6)].pval);
		set_dads((yyval.pval),(yyvsp[(5) - (6)].pval));
		(yyval.pval)->u3.abstract = (yyvsp[(1) - (6)].intval);}
    break;

  case 13:

/* Line 1806 of yacc.c  */
#line 218 "ael.y"
    { (yyval.intval) = 1; }
    break;

  case 14:

/* Line 1806 of yacc.c  */
#line 219 "ael.y"
    { (yyval.intval) = 0; }
    break;

  case 15:

/* Line 1806 of yacc.c  */
#line 220 "ael.y"
    { (yyval.intval) = 2; }
    break;

  case 16:

/* Line 1806 of yacc.c  */
#line 221 "ael.y"
    { (yyval.intval)=3; }
    break;

  case 17:

/* Line 1806 of yacc.c  */
#line 222 "ael.y"
    { (yyval.intval)=3; }
    break;

  case 18:

/* Line 1806 of yacc.c  */
#line 225 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[(1) - (8)]), &(yylsp[(8) - (8)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (8)].str); (yyval.pval)->u2.arglist = (yyvsp[(4) - (8)].pval); (yyval.pval)->u3.macro_statements = (yyvsp[(7) - (8)].pval);
        set_dads((yyval.pval),(yyvsp[(7) - (8)].pval));}
    break;

  case 19:

/* Line 1806 of yacc.c  */
#line 231 "ael.y"
    {
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.statements = (yyvsp[(3) - (4)].pval);
        set_dads((yyval.pval),(yyvsp[(3) - (4)].pval));}
    break;

  case 20:

/* Line 1806 of yacc.c  */
#line 237 "ael.y"
    { (yyval.pval) = NULL; }
    break;

  case 21:

/* Line 1806 of yacc.c  */
#line 238 "ael.y"
    {(yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); }
    break;

  case 22:

/* Line 1806 of yacc.c  */
#line 239 "ael.y"
    {(yyval.pval)=(yyvsp[(2) - (2)].pval);}
    break;

  case 23:

/* Line 1806 of yacc.c  */
#line 242 "ael.y"
    { reset_semicount(parseio->scanner); }
    break;

  case 24:

/* Line 1806 of yacc.c  */
#line 242 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (5)].str);
		(yyval.pval)->u2.val = (yyvsp[(4) - (5)].str); }
    break;

  case 25:

/* Line 1806 of yacc.c  */
#line 248 "ael.y"
    { reset_semicount(parseio->scanner); }
    break;

  case 26:

/* Line 1806 of yacc.c  */
#line 248 "ael.y"
    {
		(yyval.pval) = npval2(PV_LOCALVARDEC, &(yylsp[(1) - (6)]), &(yylsp[(6) - (6)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (6)].str);
		(yyval.pval)->u2.val = (yyvsp[(5) - (6)].str); }
    break;

  case 27:

/* Line 1806 of yacc.c  */
#line 255 "ael.y"
    { (yyval.pval) = NULL; }
    break;

  case 28:

/* Line 1806 of yacc.c  */
#line 256 "ael.y"
    { (yyval.pval) = nword((yyvsp[(1) - (1)].str), &(yylsp[(1) - (1)])); }
    break;

  case 29:

/* Line 1806 of yacc.c  */
#line 257 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (3)].pval), nword((yyvsp[(3) - (3)].str), &(yylsp[(3) - (3)]))); }
    break;

  case 30:

/* Line 1806 of yacc.c  */
#line 258 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (2)].pval);}
    break;

  case 31:

/* Line 1806 of yacc.c  */
#line 261 "ael.y"
    {(yyval.pval)=0;}
    break;

  case 32:

/* Line 1806 of yacc.c  */
#line 262 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); }
    break;

  case 33:

/* Line 1806 of yacc.c  */
#line 263 "ael.y"
    { (yyval.pval)=(yyvsp[(2) - (2)].pval);}
    break;

  case 34:

/* Line 1806 of yacc.c  */
#line 266 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 35:

/* Line 1806 of yacc.c  */
#line 267 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 36:

/* Line 1806 of yacc.c  */
#line 268 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 37:

/* Line 1806 of yacc.c  */
#line 269 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 38:

/* Line 1806 of yacc.c  */
#line 270 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 39:

/* Line 1806 of yacc.c  */
#line 271 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 40:

/* Line 1806 of yacc.c  */
#line 272 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 41:

/* Line 1806 of yacc.c  */
#line 273 "ael.y"
    {free((yyvsp[(1) - (2)].str)); (yyval.pval)=0;}
    break;

  case 42:

/* Line 1806 of yacc.c  */
#line 274 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */}
    break;

  case 43:

/* Line 1806 of yacc.c  */
#line 277 "ael.y"
    {
		(yyval.pval) = npval2(PV_IGNOREPAT, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.str = (yyvsp[(3) - (4)].str);}
    break;

  case 44:

/* Line 1806 of yacc.c  */
#line 282 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (3)].str);
		(yyval.pval)->u2.statements = (yyvsp[(3) - (3)].pval); set_dads((yyval.pval),(yyvsp[(3) - (3)].pval));}
    break;

  case 45:

/* Line 1806 of yacc.c  */
#line 286 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (5)]), &(yylsp[(3) - (5)]));
		(yyval.pval)->u1.str = malloc(strlen((yyvsp[(1) - (5)].str))+strlen((yyvsp[(3) - (5)].str))+2);
		strcpy((yyval.pval)->u1.str,(yyvsp[(1) - (5)].str));
		strcat((yyval.pval)->u1.str,"@");
		strcat((yyval.pval)->u1.str,(yyvsp[(3) - (5)].str));
		free((yyvsp[(1) - (5)].str));
		(yyval.pval)->u2.statements = (yyvsp[(5) - (5)].pval); set_dads((yyval.pval),(yyvsp[(5) - (5)].pval));}
    break;

  case 46:

/* Line 1806 of yacc.c  */
#line 294 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (4)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (4)].pval); set_dads((yyval.pval),(yyvsp[(4) - (4)].pval));
		(yyval.pval)->u4.regexten=1;}
    break;

  case 47:

/* Line 1806 of yacc.c  */
#line 299 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (7)]), &(yylsp[(7) - (7)]));
		(yyval.pval)->u1.str = (yyvsp[(5) - (7)].str);
		(yyval.pval)->u2.statements = (yyvsp[(7) - (7)].pval); set_dads((yyval.pval),(yyvsp[(7) - (7)].pval));
		(yyval.pval)->u3.hints = (yyvsp[(3) - (7)].str);}
    break;

  case 48:

/* Line 1806 of yacc.c  */
#line 304 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (8)]), &(yylsp[(8) - (8)]));
		(yyval.pval)->u1.str = (yyvsp[(6) - (8)].str);
		(yyval.pval)->u2.statements = (yyvsp[(8) - (8)].pval); set_dads((yyval.pval),(yyvsp[(8) - (8)].pval));
		(yyval.pval)->u4.regexten=1;
		(yyval.pval)->u3.hints = (yyvsp[(4) - (8)].str);}
    break;

  case 49:

/* Line 1806 of yacc.c  */
#line 313 "ael.y"
    { (yyval.pval) = NULL; }
    break;

  case 50:

/* Line 1806 of yacc.c  */
#line 314 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); }
    break;

  case 51:

/* Line 1806 of yacc.c  */
#line 315 "ael.y"
    {(yyval.pval)=(yyvsp[(2) - (2)].pval);}
    break;

  case 52:

/* Line 1806 of yacc.c  */
#line 321 "ael.y"
    {
		if (asprintf(&(yyval.str), "%s:%s:%s", (yyvsp[(1) - (5)].str), (yyvsp[(3) - (5)].str), (yyvsp[(5) - (5)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (5)].str));
			free((yyvsp[(3) - (5)].str));
			free((yyvsp[(5) - (5)].str));
		}
	}
    break;

  case 53:

/* Line 1806 of yacc.c  */
#line 331 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); }
    break;

  case 54:

/* Line 1806 of yacc.c  */
#line 335 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (7)].str), &(yylsp[(1) - (7)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (7)].str), &(yylsp[(3) - (7)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (7)].str), &(yylsp[(5) - (7)]));
		(yyval.pval)->next->next->next = nword((yyvsp[(7) - (7)].str), &(yylsp[(7) - (7)])); }
    break;

  case 55:

/* Line 1806 of yacc.c  */
#line 343 "ael.y"
    { reset_parencount(parseio->scanner); }
    break;

  case 56:

/* Line 1806 of yacc.c  */
#line 343 "ael.y"
    { (yyval.str) = (yyvsp[(3) - (4)].str); }
    break;

  case 57:

/* Line 1806 of yacc.c  */
#line 347 "ael.y"
    {
		(yyval.pval)= npval2(PV_IF, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (2)].str); }
    break;

  case 58:

/* Line 1806 of yacc.c  */
#line 350 "ael.y"
    {
		(yyval.pval) = npval2(PV_RANDOM, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)]));
		(yyval.pval)->u1.str=(yyvsp[(2) - (2)].str);}
    break;

  case 59:

/* Line 1806 of yacc.c  */
#line 353 "ael.y"
    {
		(yyval.pval) = npval2(PV_IFTIME, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.list = (yyvsp[(3) - (4)].pval);
		prev_word = 0; }
    break;

  case 60:

/* Line 1806 of yacc.c  */
#line 364 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str);}
    break;

  case 61:

/* Line 1806 of yacc.c  */
#line 365 "ael.y"
    {
		if (asprintf(&((yyval.str)), "%s%s", (yyvsp[(1) - (2)].str), (yyvsp[(2) - (2)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (2)].str));
			free((yyvsp[(2) - (2)].str));
			prev_word = (yyval.str);
		}
	}
    break;

  case 62:

/* Line 1806 of yacc.c  */
#line 377 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); }
    break;

  case 63:

/* Line 1806 of yacc.c  */
#line 378 "ael.y"
    {
		if (asprintf(&((yyval.str)), "%s %s", (yyvsp[(1) - (2)].str), (yyvsp[(2) - (2)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (2)].str));
			free((yyvsp[(2) - (2)].str));
		}
	}
    break;

  case 64:

/* Line 1806 of yacc.c  */
#line 387 "ael.y"
    {
		if (asprintf(&((yyval.str)), "%s:%s", (yyvsp[(1) - (3)].str), (yyvsp[(3) - (3)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (3)].str));
			free((yyvsp[(3) - (3)].str));
		}
	}
    break;

  case 65:

/* Line 1806 of yacc.c  */
#line 396 "ael.y"
    {  /* there are often '&' in hints */
		if (asprintf(&((yyval.str)), "%s&%s", (yyvsp[(1) - (3)].str), (yyvsp[(3) - (3)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (3)].str));
			free((yyvsp[(3) - (3)].str));
		}
	}
    break;

  case 66:

/* Line 1806 of yacc.c  */
#line 405 "ael.y"
    {
		if (asprintf(&((yyval.str)), "%s@%s", (yyvsp[(1) - (3)].str), (yyvsp[(3) - (3)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (3)].str));
			free((yyvsp[(3) - (3)].str));
		}
	}
    break;

  case 67:

/* Line 1806 of yacc.c  */
#line 416 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str);}
    break;

  case 68:

/* Line 1806 of yacc.c  */
#line 417 "ael.y"
    {
		if (asprintf(&((yyval.str)), "%s%s", (yyvsp[(1) - (2)].str), (yyvsp[(2) - (2)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (2)].str));
			free((yyvsp[(2) - (2)].str));
			prev_word = (yyval.str);
		}			
	}
    break;

  case 69:

/* Line 1806 of yacc.c  */
#line 427 "ael.y"
    {
		if (asprintf(&((yyval.str)), "%s%s%s", (yyvsp[(1) - (3)].str), (yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (3)].str));
			free((yyvsp[(2) - (3)].str));
			free((yyvsp[(3) - (3)].str));
			prev_word=(yyval.str);
		}
	}
    break;

  case 70:

/* Line 1806 of yacc.c  */
#line 440 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str);}
    break;

  case 71:

/* Line 1806 of yacc.c  */
#line 441 "ael.y"
    {
		if (asprintf(&((yyval.str)), "%s%s", (yyvsp[(1) - (2)].str), (yyvsp[(2) - (2)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (2)].str));
			free((yyvsp[(2) - (2)].str));
		}
	}
    break;

  case 72:

/* Line 1806 of yacc.c  */
#line 450 "ael.y"
    {
		if (asprintf(&((yyval.str)), "%s:%s", (yyvsp[(1) - (3)].str), (yyvsp[(3) - (3)].str)) < 0) {
			ast_log(LOG_WARNING, "asprintf() failed\n");
			(yyval.str) = NULL;
		} else {
			free((yyvsp[(1) - (3)].str));
			free((yyvsp[(3) - (3)].str));
		}
	}
    break;

  case 73:

/* Line 1806 of yacc.c  */
#line 461 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCH, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (5)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (5)].pval); set_dads((yyval.pval),(yyvsp[(4) - (5)].pval));}
    break;

  case 74:

/* Line 1806 of yacc.c  */
#line 470 "ael.y"
    {
		(yyval.pval) = npval2(PV_STATEMENTBLOCK, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.list = (yyvsp[(2) - (3)].pval); set_dads((yyval.pval),(yyvsp[(2) - (3)].pval));}
    break;

  case 75:

/* Line 1806 of yacc.c  */
#line 473 "ael.y"
    { (yyval.pval) = (yyvsp[(1) - (1)].pval); }
    break;

  case 76:

/* Line 1806 of yacc.c  */
#line 474 "ael.y"
    { (yyval.pval) = (yyvsp[(1) - (1)].pval); }
    break;

  case 77:

/* Line 1806 of yacc.c  */
#line 475 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.list = (yyvsp[(2) - (3)].pval);}
    break;

  case 78:

/* Line 1806 of yacc.c  */
#line 478 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.list = (yyvsp[(2) - (3)].pval);}
    break;

  case 79:

/* Line 1806 of yacc.c  */
#line 481 "ael.y"
    {
		(yyval.pval) = npval2(PV_LABEL, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (2)].str); }
    break;

  case 80:

/* Line 1806 of yacc.c  */
#line 484 "ael.y"
    {reset_semicount(parseio->scanner);}
    break;

  case 81:

/* Line 1806 of yacc.c  */
#line 485 "ael.y"
    {reset_semicount(parseio->scanner);}
    break;

  case 82:

/* Line 1806 of yacc.c  */
#line 486 "ael.y"
    {reset_parencount(parseio->scanner);}
    break;

  case 83:

/* Line 1806 of yacc.c  */
#line 486 "ael.y"
    { /* XXX word_list maybe ? */
		(yyval.pval) = npval2(PV_FOR, &(yylsp[(1) - (12)]), &(yylsp[(12) - (12)]));
		(yyval.pval)->u1.for_init = (yyvsp[(4) - (12)].str);
		(yyval.pval)->u2.for_test=(yyvsp[(7) - (12)].str);
		(yyval.pval)->u3.for_inc = (yyvsp[(10) - (12)].str);
		(yyval.pval)->u4.for_statements = (yyvsp[(12) - (12)].pval); set_dads((yyval.pval),(yyvsp[(12) - (12)].pval));}
    break;

  case 84:

/* Line 1806 of yacc.c  */
#line 492 "ael.y"
    {
		(yyval.pval) = npval2(PV_WHILE, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (3)].str);
		(yyval.pval)->u2.statements = (yyvsp[(3) - (3)].pval); set_dads((yyval.pval),(yyvsp[(3) - (3)].pval));}
    break;

  case 85:

/* Line 1806 of yacc.c  */
#line 496 "ael.y"
    { (yyval.pval) = (yyvsp[(1) - (1)].pval); }
    break;

  case 86:

/* Line 1806 of yacc.c  */
#line 497 "ael.y"
    { (yyval.pval) = update_last((yyvsp[(2) - (3)].pval), &(yylsp[(2) - (3)])); }
    break;

  case 87:

/* Line 1806 of yacc.c  */
#line 498 "ael.y"
    { (yyval.pval) = update_last((yyvsp[(1) - (2)].pval), &(yylsp[(2) - (2)])); }
    break;

  case 88:

/* Line 1806 of yacc.c  */
#line 499 "ael.y"
    {
		(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (2)].str);}
    break;

  case 89:

/* Line 1806 of yacc.c  */
#line 502 "ael.y"
    {reset_semicount(parseio->scanner);}
    break;

  case 90:

/* Line 1806 of yacc.c  */
#line 502 "ael.y"
    {
		char *bufx;
		int tot=0;
		pval *pptr;
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u2.val=(yyvsp[(4) - (5)].str);
		/* rebuild the original string-- this is not an app call, it's an unwrapped vardec, with a func call on the LHS */
		/* string to big to fit in the buffer? */
		tot+=strlen((yyvsp[(1) - (5)].pval)->u1.str);
		for(pptr=(yyvsp[(1) - (5)].pval)->u2.arglist;pptr;pptr=pptr->next) {
			tot+=strlen(pptr->u1.str);
			tot++; /* for a sep like a comma */
		}
		tot+=4; /* for safety */
		bufx = calloc(1, tot);
		strcpy(bufx,(yyvsp[(1) - (5)].pval)->u1.str);
		strcat(bufx,"(");
		/* XXX need to advance the pointer or the loop is very inefficient */
		for (pptr=(yyvsp[(1) - (5)].pval)->u2.arglist;pptr;pptr=pptr->next) {
			if ( pptr != (yyvsp[(1) - (5)].pval)->u2.arglist )
				strcat(bufx,",");
			strcat(bufx,pptr->u1.str);
		}
		strcat(bufx,")");
#ifdef AAL_ARGCHECK
		if ( !ael_is_funcname((yyvsp[(1) - (5)].pval)->u1.str) )
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Function call? The name %s is not in my internal list of function names\n",
				my_file, (yylsp[(1) - (5)]).first_line, (yylsp[(1) - (5)]).first_column, (yylsp[(1) - (5)]).last_column, (yyvsp[(1) - (5)].pval)->u1.str);
#endif
		(yyval.pval)->u1.str = bufx;
		destroy_pval((yyvsp[(1) - (5)].pval)); /* the app call it is not, get rid of that chain */
		prev_word = 0;
	}
    break;

  case 91:

/* Line 1806 of yacc.c  */
#line 535 "ael.y"
    { (yyval.pval) = npval2(PV_BREAK, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)])); }
    break;

  case 92:

/* Line 1806 of yacc.c  */
#line 536 "ael.y"
    { (yyval.pval) = npval2(PV_RETURN, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)])); }
    break;

  case 93:

/* Line 1806 of yacc.c  */
#line 537 "ael.y"
    { (yyval.pval) = npval2(PV_CONTINUE, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)])); }
    break;

  case 94:

/* Line 1806 of yacc.c  */
#line 538 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[(1) - (3)].pval), &(yylsp[(2) - (3)]));
		(yyval.pval)->u2.statements = (yyvsp[(2) - (3)].pval); set_dads((yyval.pval),(yyvsp[(2) - (3)].pval));
		(yyval.pval)->u3.else_statements = (yyvsp[(3) - (3)].pval);set_dads((yyval.pval),(yyvsp[(3) - (3)].pval));}
    break;

  case 95:

/* Line 1806 of yacc.c  */
#line 542 "ael.y"
    { (yyval.pval)=0; }
    break;

  case 96:

/* Line 1806 of yacc.c  */
#line 545 "ael.y"
    { (yyval.pval) = (yyvsp[(2) - (2)].pval); }
    break;

  case 97:

/* Line 1806 of yacc.c  */
#line 546 "ael.y"
    { (yyval.pval) = NULL ; }
    break;

  case 98:

/* Line 1806 of yacc.c  */
#line 549 "ael.y"
    { (yyval.pval) = nword((yyvsp[(1) - (1)].str), &(yylsp[(1) - (1)])); }
    break;

  case 99:

/* Line 1806 of yacc.c  */
#line 550 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (3)].str), &(yylsp[(1) - (3)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (3)].str), &(yylsp[(3) - (3)])); }
    break;

  case 100:

/* Line 1806 of yacc.c  */
#line 553 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (3)].str), &(yylsp[(1) - (3)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (3)].str), &(yylsp[(3) - (3)])); }
    break;

  case 101:

/* Line 1806 of yacc.c  */
#line 556 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (5)].str), &(yylsp[(1) - (5)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (5)].str), &(yylsp[(3) - (5)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (5)].str), &(yylsp[(5) - (5)])); }
    break;

  case 102:

/* Line 1806 of yacc.c  */
#line 560 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (5)].str), &(yylsp[(1) - (5)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (5)].str), &(yylsp[(3) - (5)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (5)].str), &(yylsp[(5) - (5)])); }
    break;

  case 103:

/* Line 1806 of yacc.c  */
#line 564 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[(1) - (5)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (5)].str), &(yylsp[(3) - (5)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (5)].str), &(yylsp[(5) - (5)])); }
    break;

  case 104:

/* Line 1806 of yacc.c  */
#line 568 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[(1) - (5)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (5)].str), &(yylsp[(3) - (5)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (5)].str), &(yylsp[(5) - (5)])); }
    break;

  case 105:

/* Line 1806 of yacc.c  */
#line 574 "ael.y"
    { (yyval.str) = strdup("1"); }
    break;

  case 106:

/* Line 1806 of yacc.c  */
#line 575 "ael.y"
    { (yyval.str) = (yyvsp[(2) - (2)].str); }
    break;

  case 107:

/* Line 1806 of yacc.c  */
#line 579 "ael.y"
    {			/* ext[, pri] default 1 */
		(yyval.pval) = nword((yyvsp[(1) - (2)].str), &(yylsp[(1) - (2)]));
		(yyval.pval)->next = nword((yyvsp[(2) - (2)].str), &(yylsp[(2) - (2)])); }
    break;

  case 108:

/* Line 1806 of yacc.c  */
#line 582 "ael.y"
    {	/* context, ext, pri */
		(yyval.pval) = nword((yyvsp[(4) - (4)].str), &(yylsp[(4) - (4)]));
		(yyval.pval)->next = nword((yyvsp[(1) - (4)].str), &(yylsp[(1) - (4)]));
		(yyval.pval)->next->next = nword((yyvsp[(2) - (4)].str), &(yylsp[(2) - (4)])); }
    break;

  case 109:

/* Line 1806 of yacc.c  */
#line 588 "ael.y"
    {reset_argcount(parseio->scanner);}
    break;

  case 110:

/* Line 1806 of yacc.c  */
#line 588 "ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		(yyval.pval) = npval2(PV_MACRO_CALL, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (5)].str);
		(yyval.pval)->u2.arglist = (yyvsp[(4) - (5)].pval);}
    break;

  case 111:

/* Line 1806 of yacc.c  */
#line 593 "ael.y"
    {
		(yyval.pval)= npval2(PV_MACRO_CALL, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (3)].str); }
    break;

  case 112:

/* Line 1806 of yacc.c  */
#line 601 "ael.y"
    {reset_argcount(parseio->scanner);}
    break;

  case 113:

/* Line 1806 of yacc.c  */
#line 601 "ael.y"
    {
		if (strcasecmp((yyvsp[(1) - (3)].str),"goto") == 0) {
			(yyval.pval) = npval2(PV_GOTO, &(yylsp[(1) - (3)]), &(yylsp[(2) - (3)]));
			free((yyvsp[(1) - (3)].str)); /* won't be using this */
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Suggestion: Use the goto statement instead of the Goto() application call in AEL.\n", my_file, (yylsp[(1) - (3)]).first_line, (yylsp[(1) - (3)]).first_column, (yylsp[(1) - (3)]).last_column );
		} else {
			(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[(1) - (3)]), &(yylsp[(2) - (3)]));
			(yyval.pval)->u1.str = (yyvsp[(1) - (3)].str);
		} }
    break;

  case 114:

/* Line 1806 of yacc.c  */
#line 612 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[(1) - (3)].pval), &(yylsp[(3) - (3)]));
 		if( (yyval.pval)->type == PV_GOTO )
			(yyval.pval)->u1.list = (yyvsp[(2) - (3)].pval);
	 	else
			(yyval.pval)->u2.arglist = (yyvsp[(2) - (3)].pval);
	}
    break;

  case 115:

/* Line 1806 of yacc.c  */
#line 619 "ael.y"
    { (yyval.pval) = update_last((yyvsp[(1) - (2)].pval), &(yylsp[(2) - (2)])); }
    break;

  case 116:

/* Line 1806 of yacc.c  */
#line 622 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str) ;}
    break;

  case 117:

/* Line 1806 of yacc.c  */
#line 623 "ael.y"
    { (yyval.str) = strdup(""); }
    break;

  case 118:

/* Line 1806 of yacc.c  */
#line 626 "ael.y"
    { (yyval.pval) = nword((yyvsp[(1) - (1)].str), &(yylsp[(1) - (1)])); }
    break;

  case 119:

/* Line 1806 of yacc.c  */
#line 627 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		(yyval.pval)->u1.str = strdup(""); }
    break;

  case 120:

/* Line 1806 of yacc.c  */
#line 630 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (3)].pval), nword((yyvsp[(3) - (3)].str), &(yylsp[(3) - (3)]))); }
    break;

  case 121:

/* Line 1806 of yacc.c  */
#line 633 "ael.y"
    { (yyval.pval) = NULL; }
    break;

  case 122:

/* Line 1806 of yacc.c  */
#line 634 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); }
    break;

  case 123:

/* Line 1806 of yacc.c  */
#line 637 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[(1) - (4)]), &(yylsp[(3) - (4)])); /* XXX 3 or 4 ? */
		(yyval.pval)->u1.str = (yyvsp[(2) - (4)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (4)].pval); set_dads((yyval.pval),(yyvsp[(4) - (4)].pval));}
    break;

  case 124:

/* Line 1806 of yacc.c  */
#line 641 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.str = NULL;
		(yyval.pval)->u2.statements = (yyvsp[(3) - (3)].pval);set_dads((yyval.pval),(yyvsp[(3) - (3)].pval));}
    break;

  case 125:

/* Line 1806 of yacc.c  */
#line 645 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)])); /* XXX@3 or @4 ? */
		(yyval.pval)->u1.str = (yyvsp[(2) - (4)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (4)].pval);set_dads((yyval.pval),(yyvsp[(4) - (4)].pval));}
    break;

  case 126:

/* Line 1806 of yacc.c  */
#line 651 "ael.y"
    { (yyval.pval) = NULL; }
    break;

  case 127:

/* Line 1806 of yacc.c  */
#line 652 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); }
    break;

  case 128:

/* Line 1806 of yacc.c  */
#line 655 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 129:

/* Line 1806 of yacc.c  */
#line 656 "ael.y"
    { (yyval.pval)=(yyvsp[(1) - (1)].pval);}
    break;

  case 130:

/* Line 1806 of yacc.c  */
#line 657 "ael.y"
    {
		(yyval.pval) = npval2(PV_CATCH, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (5)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (5)].pval); set_dads((yyval.pval),(yyvsp[(4) - (5)].pval));}
    break;

  case 131:

/* Line 1806 of yacc.c  */
#line 663 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[(1) - (4)]), &(yylsp[(2) - (4)]));
		(yyval.pval)->u1.list = (yyvsp[(3) - (4)].pval); set_dads((yyval.pval),(yyvsp[(3) - (4)].pval));}
    break;

  case 132:

/* Line 1806 of yacc.c  */
#line 668 "ael.y"
    {
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[(1) - (4)]), &(yylsp[(2) - (4)]));
		(yyval.pval)->u1.list = (yyvsp[(3) - (4)].pval); set_dads((yyval.pval),(yyvsp[(3) - (4)].pval));}
    break;

  case 133:

/* Line 1806 of yacc.c  */
#line 673 "ael.y"
    { (yyval.pval) = NULL; }
    break;

  case 134:

/* Line 1806 of yacc.c  */
#line 674 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (3)].pval),nword((yyvsp[(2) - (3)].str), &(yylsp[(2) - (3)]))); }
    break;

  case 135:

/* Line 1806 of yacc.c  */
#line 675 "ael.y"
    {
	  char *x;
	  if (asprintf(&x,"%s@%s", (yyvsp[(2) - (5)].str), (yyvsp[(4) - (5)].str)) < 0) {
		ast_log(LOG_WARNING, "asprintf() failed\n");
		(yyval.pval) = NULL;
	  } else {
		free((yyvsp[(2) - (5)].str));
		free((yyvsp[(4) - (5)].str));
		(yyval.pval) = linku1((yyvsp[(1) - (5)].pval),nword(x, &(yylsp[(2) - (5)])));
	  }
	}
    break;

  case 136:

/* Line 1806 of yacc.c  */
#line 686 "ael.y"
    {(yyval.pval)=(yyvsp[(2) - (2)].pval);}
    break;

  case 137:

/* Line 1806 of yacc.c  */
#line 689 "ael.y"
    { (yyval.pval) = nword((yyvsp[(1) - (1)].str), &(yylsp[(1) - (1)])); }
    break;

  case 138:

/* Line 1806 of yacc.c  */
#line 690 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (3)].str), &(yylsp[(1) - (3)]));
		(yyval.pval)->u2.arglist = (yyvsp[(3) - (3)].pval);
		prev_word=0; /* XXX sure ? */ }
    break;

  case 139:

/* Line 1806 of yacc.c  */
#line 697 "ael.y"
    { (yyval.pval) = (yyvsp[(1) - (2)].pval); }
    break;

  case 140:

/* Line 1806 of yacc.c  */
#line 698 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (3)].pval), (yyvsp[(2) - (3)].pval)); }
    break;

  case 141:

/* Line 1806 of yacc.c  */
#line 699 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (2)].pval);}
    break;

  case 142:

/* Line 1806 of yacc.c  */
#line 702 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.list = (yyvsp[(3) - (4)].pval);set_dads((yyval.pval),(yyvsp[(3) - (4)].pval));}
    break;

  case 143:

/* Line 1806 of yacc.c  */
#line 705 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));}
    break;



/* Line 1806 of yacc.c  */
#line 3590 "ael.tab.c"
      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
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
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYEMPTY : YYTRANSLATE (yychar);

  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (&yylloc, parseio, YY_("syntax error"));
#else
# define YYSYNTAX_ERROR yysyntax_error (&yymsg_alloc, &yymsg, \
                                        yyssp, yytoken)
      {
        char const *yymsgp = YY_("syntax error");
        int yysyntax_error_status;
        yysyntax_error_status = YYSYNTAX_ERROR;
        if (yysyntax_error_status == 0)
          yymsgp = yymsg;
        else if (yysyntax_error_status == 1)
          {
            if (yymsg != yymsgbuf)
              YYSTACK_FREE (yymsg);
            yymsg = (char *) YYSTACK_ALLOC (yymsg_alloc);
            if (!yymsg)
              {
                yymsg = yymsgbuf;
                yymsg_alloc = sizeof yymsgbuf;
                yysyntax_error_status = 2;
              }
            else
              {
                yysyntax_error_status = YYSYNTAX_ERROR;
                yymsgp = yymsg;
              }
          }
        yyerror (&yylloc, parseio, yymsgp);
        if (yysyntax_error_status == 2)
          goto yyexhaustedlab;
      }
# undef YYSYNTAX_ERROR
#endif
    }

  yyerror_range[1] = yylloc;

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
		      yytoken, &yylval, &yylloc, parseio);
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

  yyerror_range[1] = yylsp[1-yylen];
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
      if (!yypact_value_is_default (yyn))
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

      yyerror_range[1] = *yylsp;
      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp, yylsp, parseio);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  *++yyvsp = yylval;

  yyerror_range[2] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the lookahead.  YYLOC is available though.  */
  YYLLOC_DEFAULT (yyloc, yyerror_range, 2);
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
  yyerror (&yylloc, parseio, YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval, &yylloc, parseio);
    }
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp, yylsp, parseio);
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



/* Line 2067 of yacc.c  */
#line 710 "ael.y"


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


static char *ael_token_subst(const char *mess)
{
	/* calc a length, malloc, fill, and return; yyerror had better free it! */
	int len=0,i;
	const char *p;
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
	res = calloc(1, len+1);
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

struct pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column)
{
	pval *z = calloc(1, sizeof(struct pval));
	z->type = type;
	z->startline = first_line;
	z->endline = last_line;
	z->startcol = first_column;
	z->endcol = last_column;
	z->filename = strdup(S_OR(my_file, "<none>"));
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

/* this routine adds a dad ptr to each element in the list */
static void set_dads(struct pval *dad, struct pval *child_list)
{
	struct pval *t;
	
	for(t=child_list;t;t=t->next)  /* simple stuff */
		t->dad = dad;
}


