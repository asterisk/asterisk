/* A Bison parser, made by GNU Bison 2.1a.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, when this file is copied by Bison into a
   Bison output file, you may use that output file without restriction.
   This special exception was added by the Free Software Foundation
   in version 1.24 of Bison.  */

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
#define YYBISON_VERSION "2.1a"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Using locations.  */
#define YYLSP_NEEDED 1

/* Substitute the variable and function names.  */
#define yyparse ael_yyparse
#define yylex   ael_yylex
#define yyerror ael_yyerror
#define yylval  ael_yylval
#define yychar  ael_yychar
#define yydebug ael_yydebug
#define yynerrs ael_yynerrs
#define yylloc ael_yylloc

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
     EXTENMARK = 279,
     KW_GOTO = 280,
     KW_JUMP = 281,
     KW_RETURN = 282,
     KW_BREAK = 283,
     KW_CONTINUE = 284,
     KW_REGEXTEN = 285,
     KW_HINT = 286,
     KW_FOR = 287,
     KW_WHILE = 288,
     KW_CASE = 289,
     KW_PATTERN = 290,
     KW_DEFAULT = 291,
     KW_CATCH = 292,
     KW_SWITCHES = 293,
     KW_ESWITCHES = 294,
     KW_INCLUDES = 295,
     KW_LOCAL = 296,
     word = 297
   };
#endif
/* Tokens.  */
#define KW_CONTEXT 258
#define LC 259
#define RC 260
#define LP 261
#define RP 262
#define SEMI 263
#define EQ 264
#define COMMA 265
#define COLON 266
#define AMPER 267
#define BAR 268
#define AT 269
#define KW_MACRO 270
#define KW_GLOBALS 271
#define KW_IGNOREPAT 272
#define KW_SWITCH 273
#define KW_IF 274
#define KW_IFTIME 275
#define KW_ELSE 276
#define KW_RANDOM 277
#define KW_ABSTRACT 278
#define EXTENMARK 279
#define KW_GOTO 280
#define KW_JUMP 281
#define KW_RETURN 282
#define KW_BREAK 283
#define KW_CONTINUE 284
#define KW_REGEXTEN 285
#define KW_HINT 286
#define KW_FOR 287
#define KW_WHILE 288
#define KW_CASE 289
#define KW_PATTERN 290
#define KW_DEFAULT 291
#define KW_CATCH 292
#define KW_SWITCHES 293
#define KW_ESWITCHES 294
#define KW_INCLUDES 295
#define KW_LOCAL 296
#define word 297




/* Copy the first part of user declarations.  */
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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asterisk/logger.h"
#include "asterisk/ael_structs.h"

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

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 54 "ael.y"
{
	int	intval;		/* integer value, typically flags */
	char	*str;		/* strings */
	struct pval *pval;	/* full objects */
}
/* Line 198 of yacc.c.  */
#line 236 "ael.tab.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
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
#line 60 "ael.y"

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


/* Line 221 of yacc.c.  */
#line 281 "ael.tab.c"

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
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
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
#  ifdef __cplusplus
extern "C" {
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
#  ifdef __cplusplus
}
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
  yytype_int16 yyss;
  YYSTYPE yyvs;
    YYLTYPE yyls;
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
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  14
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   331

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  43
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  56
/* YYNRULES -- Number of rules.  */
#define YYNRULES  138
/* YYNRULES -- Number of states.  */
#define YYNSTATES  281

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   297

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
      35,    36,    37,    38,    39,    40,    41,    42
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint16 yyprhs[] =
{
       0,     0,     3,     5,     7,    10,    13,    15,    17,    19,
      21,    23,    25,    32,    34,    35,    44,    49,    50,    53,
      56,    57,    63,    64,    71,    72,    74,    78,    81,    82,
      85,    88,    90,    92,    94,    96,    98,   100,   102,   105,
     107,   112,   116,   121,   129,   138,   139,   142,   145,   151,
     153,   161,   162,   167,   170,   173,   178,   180,   183,   185,
     188,   192,   196,   198,   201,   205,   207,   210,   214,   220,
     224,   226,   228,   232,   236,   239,   240,   241,   242,   255,
     259,   261,   265,   268,   271,   272,   278,   281,   284,   287,
     291,   293,   296,   297,   299,   303,   307,   313,   319,   325,
     331,   332,   335,   338,   343,   344,   350,   354,   355,   359,
     363,   366,   368,   369,   371,   372,   376,   377,   380,   385,
     389,   394,   395,   398,   400,   402,   408,   413,   418,   419,
     423,   429,   432,   434,   438,   441,   445,   448,   453
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      44,     0,    -1,    45,    -1,    46,    -1,    45,    46,    -1,
      45,     1,    -1,    48,    -1,    50,    -1,    51,    -1,     8,
      -1,    42,    -1,    36,    -1,    49,     3,    47,     4,    58,
       5,    -1,    23,    -1,    -1,    15,    42,     6,    57,     7,
       4,    91,     5,    -1,    16,     4,    52,     5,    -1,    -1,
      53,    52,    -1,     1,    52,    -1,    -1,    42,     9,    54,
      42,     8,    -1,    -1,    41,    42,     9,    56,    42,     8,
      -1,    -1,    42,    -1,    57,    10,    42,    -1,    57,     1,
      -1,    -1,    59,    58,    -1,     1,    58,    -1,    61,    -1,
      98,    -1,    93,    -1,    94,    -1,    60,    -1,    53,    -1,
      55,    -1,    42,     1,    -1,     8,    -1,    17,    24,    42,
       8,    -1,    42,    24,    73,    -1,    30,    42,    24,    73,
      -1,    31,     6,    69,     7,    42,    24,    73,    -1,    30,
      31,     6,    69,     7,    42,    24,    73,    -1,    -1,    73,
      62,    -1,     1,    62,    -1,    70,    11,    70,    11,    70,
      -1,    42,    -1,    63,    13,    70,    13,    70,    13,    70,
      -1,    -1,     6,    66,    68,     7,    -1,    19,    65,    -1,
      22,    65,    -1,    20,     6,    64,     7,    -1,    42,    -1,
      42,    42,    -1,    42,    -1,    69,    42,    -1,    69,    11,
      42,    -1,    69,    12,    42,    -1,    42,    -1,    42,    42,
      -1,    42,    42,    42,    -1,    42,    -1,    42,    42,    -1,
      71,    11,    42,    -1,    18,    65,     4,    89,     5,    -1,
       4,    62,     5,    -1,    53,    -1,    55,    -1,    25,    79,
       8,    -1,    26,    81,     8,    -1,    42,    11,    -1,    -1,
      -1,    -1,    32,     6,    74,    42,     8,    75,    42,     8,
      76,    42,     7,    73,    -1,    33,    65,    73,    -1,    72,
      -1,    12,    82,     8,    -1,    86,     8,    -1,    42,     8,
      -1,    -1,    86,     9,    77,    42,     8,    -1,    28,     8,
      -1,    27,     8,    -1,    29,     8,    -1,    67,    73,    78,
      -1,     8,    -1,    21,    73,    -1,    -1,    71,    -1,    71,
      13,    71,    -1,    71,    10,    71,    -1,    71,    13,    71,
      13,    71,    -1,    71,    10,    71,    10,    71,    -1,    36,
      13,    71,    13,    71,    -1,    36,    10,    71,    10,    71,
      -1,    -1,    10,    42,    -1,    71,    80,    -1,    71,    80,
      14,    47,    -1,    -1,    42,     6,    83,    88,     7,    -1,
      42,     6,     7,    -1,    -1,    42,     6,    85,    -1,    84,
      88,     7,    -1,    84,     7,    -1,    42,    -1,    -1,    68,
      -1,    -1,    88,    10,    87,    -1,    -1,    90,    89,    -1,
      34,    42,    11,    62,    -1,    36,    11,    62,    -1,    35,
      42,    11,    62,    -1,    -1,    92,    91,    -1,    73,    -1,
      98,    -1,    37,    42,     4,    62,     5,    -1,    38,     4,
      95,     5,    -1,    39,     4,    95,     5,    -1,    -1,    42,
       8,    95,    -1,    42,    14,    42,     8,    95,    -1,     1,
      95,    -1,    47,    -1,    47,    13,    64,    -1,    96,     8,
      -1,    97,    96,     8,    -1,    97,     1,    -1,    40,     4,
      97,     5,    -1,    40,     4,     5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   186,   186,   189,   190,   191,   194,   195,   196,   197,
     200,   201,   204,   219,   220,   223,   229,   235,   236,   237,
     240,   240,   246,   246,   253,   254,   255,   256,   259,   260,
     261,   264,   265,   266,   267,   268,   269,   270,   271,   272,
     275,   280,   284,   289,   294,   304,   305,   306,   312,   317,
     321,   329,   329,   333,   336,   339,   350,   351,   358,   359,
     363,   367,   373,   374,   379,   387,   388,   392,   398,   407,
     410,   411,   412,   415,   418,   421,   422,   423,   421,   429,
     433,   434,   435,   436,   439,   439,   472,   473,   474,   475,
     479,   482,   483,   486,   487,   490,   493,   497,   501,   505,
     511,   512,   516,   519,   525,   525,   530,   538,   538,   549,
     556,   559,   560,   563,   564,   567,   570,   571,   574,   578,
     582,   588,   589,   592,   593,   594,   600,   605,   610,   611,
     612,   614,   617,   618,   625,   626,   627,   630,   633
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
  "KW_ELSE", "KW_RANDOM", "KW_ABSTRACT", "EXTENMARK", "KW_GOTO", "KW_JUMP",
  "KW_RETURN", "KW_BREAK", "KW_CONTINUE", "KW_REGEXTEN", "KW_HINT",
  "KW_FOR", "KW_WHILE", "KW_CASE", "KW_PATTERN", "KW_DEFAULT", "KW_CATCH",
  "KW_SWITCHES", "KW_ESWITCHES", "KW_INCLUDES", "KW_LOCAL", "word",
  "$accept", "file", "objects", "object", "context_name", "context",
  "opt_abstract", "macro", "globals", "global_statements", "assignment",
  "@1", "local_assignment", "@2", "arglist", "elements", "element",
  "ignorepat", "extension", "statements", "timerange", "timespec",
  "test_expr", "@3", "if_like_head", "word_list", "hint_word",
  "word3_list", "goto_word", "switch_statement", "statement", "@4", "@5",
  "@6", "@7", "opt_else", "target", "opt_pri", "jumptarget", "macro_call",
  "@8", "application_call_head", "@9", "application_call", "opt_word",
  "eval_arglist", "case_statements", "case_statement", "macro_statements",
  "macro_statement", "switches", "eswitches", "switchlist",
  "included_entry", "includeslist", "includes", 0
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
     295,   296,   297
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    43,    44,    45,    45,    45,    46,    46,    46,    46,
      47,    47,    48,    49,    49,    50,    51,    52,    52,    52,
      54,    53,    56,    55,    57,    57,    57,    57,    58,    58,
      58,    59,    59,    59,    59,    59,    59,    59,    59,    59,
      60,    61,    61,    61,    61,    62,    62,    62,    63,    63,
      64,    66,    65,    67,    67,    67,    68,    68,    69,    69,
      69,    69,    70,    70,    70,    71,    71,    71,    72,    73,
      73,    73,    73,    73,    73,    74,    75,    76,    73,    73,
      73,    73,    73,    73,    77,    73,    73,    73,    73,    73,
      73,    78,    78,    79,    79,    79,    79,    79,    79,    79,
      80,    80,    81,    81,    83,    82,    82,    85,    84,    86,
      86,    87,    87,    88,    88,    88,    89,    89,    90,    90,
      90,    91,    91,    92,    92,    92,    93,    94,    95,    95,
      95,    95,    96,    96,    97,    97,    97,    98,    98
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     1,     6,     1,     0,     8,     4,     0,     2,     2,
       0,     5,     0,     6,     0,     1,     3,     2,     0,     2,
       2,     1,     1,     1,     1,     1,     1,     1,     2,     1,
       4,     3,     4,     7,     8,     0,     2,     2,     5,     1,
       7,     0,     4,     2,     2,     4,     1,     2,     1,     2,
       3,     3,     1,     2,     3,     1,     2,     3,     5,     3,
       1,     1,     3,     3,     2,     0,     0,     0,    12,     3,
       1,     3,     2,     2,     0,     5,     2,     2,     2,     3,
       1,     2,     0,     1,     3,     3,     5,     5,     5,     5,
       0,     2,     2,     4,     0,     5,     3,     0,     3,     3,
       2,     1,     0,     1,     0,     3,     0,     2,     4,     3,
       4,     0,     2,     1,     1,     5,     4,     4,     0,     3,
       5,     2,     1,     3,     2,     3,     2,     4,     3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
      14,     9,     0,     0,    13,     0,     0,     3,     6,     0,
       7,     8,     0,     0,     1,     5,     4,     0,    24,     0,
       0,     0,     0,    11,    10,     0,    25,     0,    19,    20,
      16,    18,     0,    27,     0,     0,     0,     0,    39,     0,
       0,     0,     0,     0,     0,     0,     0,    36,    37,     0,
       0,    35,    31,    33,    34,    32,   121,    26,     0,    30,
       0,     0,     0,     0,     0,     0,     0,     0,    38,     0,
      12,    29,     0,    90,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    70,    71,
       0,    80,   123,   114,     0,     0,   121,   124,    21,     0,
       0,     0,    58,     0,     0,     0,     0,     0,   138,   132,
       0,     0,    22,    41,     0,     0,     0,     0,     0,    51,
       0,    53,     0,    54,     0,    65,    93,     0,   100,     0,
      87,    86,    88,    75,     0,     0,   107,    83,    74,    92,
     110,    56,   113,     0,    82,    84,    15,   122,    40,     0,
      42,     0,     0,     0,    59,   131,     0,     0,   126,   127,
       0,   134,   136,   137,     0,     0,    47,    69,    46,   104,
      81,     0,   116,    49,     0,     0,     0,     0,     0,    66,
       0,     0,     0,    72,     0,   102,    73,     0,    79,     0,
     108,     0,    89,    57,   109,   112,     0,     0,     0,    60,
      61,   129,     0,   133,   135,     0,   106,   114,     0,     0,
       0,     0,     0,   116,    63,     0,    55,     0,     0,     0,
      95,    67,    94,   101,     0,     0,     0,    91,   111,   115,
       0,     0,     0,     0,    23,     0,    52,     0,     0,     0,
      68,   117,    64,    62,     0,     0,     0,     0,     0,     0,
     103,    76,   125,    85,     0,    43,   130,   105,     0,     0,
     119,     0,     0,    99,    98,    97,    96,     0,    44,   118,
     120,     0,    48,     0,     0,    77,    50,     0,     0,     0,
      78
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int16 yydefgoto[] =
{
      -1,     5,     6,     7,   109,     8,     9,    10,    11,    21,
      88,    36,    89,   165,    27,    49,    50,    51,    52,   115,
     174,   175,   120,   171,    90,   142,   103,   176,   126,    91,
     116,   187,   267,   277,   196,   192,   127,   185,   129,   118,
     207,    93,   190,    94,   229,   143,   212,   213,    95,    96,
      53,    54,   106,   110,   111,    55
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -210
static const yytype_int16 yypact[] =
{
      66,  -210,   -26,    64,  -210,    88,   110,  -210,  -210,    93,
    -210,  -210,    95,     9,  -210,  -210,  -210,    72,    30,     9,
      96,   137,     9,  -210,  -210,   124,  -210,   102,  -210,  -210,
    -210,  -210,    16,  -210,   154,   120,   144,    16,  -210,   169,
      33,   208,   197,   212,   216,   187,    62,  -210,  -210,   225,
      16,  -210,  -210,  -210,  -210,  -210,   155,  -210,   226,  -210,
     191,   229,   213,   194,    17,    17,     6,   230,  -210,   199,
    -210,  -210,    65,  -210,   196,   236,   236,   237,   236,    79,
     202,   238,   239,   240,   243,   236,   203,   204,  -210,  -210,
     199,  -210,  -210,    -4,   148,   245,   155,  -210,  -210,   244,
     194,   199,  -210,     8,    17,   108,   246,   248,  -210,   241,
     247,    25,  -210,  -210,   119,   251,   119,   252,   249,  -210,
     255,  -210,   218,  -210,    26,   219,   189,   254,   168,   256,
    -210,  -210,  -210,  -210,   199,   259,  -210,  -210,  -210,   250,
    -210,   223,  -210,    69,  -210,  -210,  -210,  -210,  -210,    20,
    -210,   224,   227,   228,  -210,  -210,    17,   231,  -210,  -210,
     218,  -210,  -210,  -210,   260,   232,  -210,  -210,  -210,   265,
    -210,   233,   130,    18,   263,   270,   267,   202,   202,  -210,
     202,   242,   202,  -210,   253,   266,  -210,   257,  -210,    65,
    -210,   199,  -210,  -210,  -210,   258,   261,   262,   264,  -210,
    -210,  -210,   271,  -210,  -210,   273,  -210,   233,   275,   268,
     269,   272,   280,   130,   274,   276,  -210,   276,   179,   106,
     195,  -210,   123,  -210,    72,   278,   282,  -210,  -210,  -210,
     281,   277,   199,    17,  -210,   122,  -210,   279,   283,   119,
    -210,  -210,  -210,   284,   285,   286,   202,   202,   202,   202,
    -210,  -210,  -210,  -210,   199,  -210,  -210,  -210,   119,   119,
    -210,   276,   276,   291,   291,   291,   291,   287,  -210,  -210,
    -210,   292,  -210,   288,   276,  -210,  -210,   289,   299,   199,
    -210
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
    -210,  -210,  -210,   301,   -16,  -210,  -210,  -210,  -210,   121,
     172,  -210,    -9,  -210,  -210,   -25,  -210,  -210,  -210,  -109,
    -210,   107,   -41,  -210,  -210,   138,   192,  -209,   -78,  -210,
     -56,  -210,  -210,  -210,  -210,  -210,  -210,  -210,  -210,  -210,
    -210,  -210,  -210,  -210,  -210,    84,    80,  -210,   217,  -210,
    -210,  -210,   -61,   201,  -210,   -47
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -129
static const yytype_int16 yytable[] =
{
      92,    25,   128,   140,   107,   166,   244,   168,   245,    97,
      19,   108,    59,   113,   -17,   151,    12,    37,   104,   152,
     153,   -28,  -128,    48,    38,    71,   162,   197,    48,   -62,
     163,   152,   153,    39,   139,   121,   177,   123,   141,   178,
      92,    48,    23,   155,   134,   150,    40,    41,    24,    97,
     154,    20,   271,   272,    42,    43,    44,    45,    46,   105,
     214,    23,   154,    68,    61,   276,   114,    24,    13,    72,
     -45,    29,    26,    73,     1,    62,   194,    74,   188,   195,
     226,     2,     3,    75,    76,    77,    69,    78,    14,     4,
      79,    80,    81,    82,    83,   201,    17,    84,    85,   218,
     219,    18,   220,    33,   222,    29,    45,    87,    23,    34,
      -2,    15,    35,   -14,    24,   124,   156,   181,     1,   247,
     114,   125,   157,    72,   -45,     2,     3,    73,    32,   257,
     260,    74,   195,     4,   181,   227,   249,    75,    76,    77,
      28,    78,    30,    31,    79,    80,    81,    82,    83,   269,
     270,    84,    85,   -45,   -45,   -45,   144,   145,    56,    72,
      45,    87,    57,    73,   209,   210,   211,    74,   263,   264,
     265,   266,   256,    75,    76,    77,   255,    78,   184,   181,
      79,    80,    81,    82,    83,    22,    58,    84,    85,   246,
     181,    22,    86,    60,    22,    44,    45,    87,   268,   180,
     181,    64,   182,    72,    47,   248,   181,    73,   250,    47,
     136,    74,   137,    29,    63,   138,    65,    75,    76,    77,
      66,    78,    47,   280,    79,    80,    81,    82,    83,    67,
      70,    84,    85,    99,    98,   100,   102,   101,   117,   112,
      45,    87,   119,   122,   125,   135,   130,   131,   132,   133,
     146,   158,   148,   159,   160,   161,   167,   170,   169,   172,
     173,   179,   183,   189,   186,   193,   198,   203,   204,   199,
     200,   191,   206,   202,   205,   141,   215,   216,   217,   233,
     224,   234,   236,   239,   221,   240,   251,   252,   232,   253,
     258,   235,   149,   241,   259,   223,   275,   262,   261,   225,
     228,   254,   181,   230,   231,   274,   279,    16,     0,   208,
     237,   238,   164,   147,     0,     0,   242,     0,   243,     0,
       0,     0,     0,     0,     0,     0,   214,     0,     0,   273,
       0,   278
};

static const yytype_int16 yycheck[] =
{
      56,    17,    80,     7,    65,   114,   215,   116,   217,    56,
       1,     5,    37,    69,     5,     7,    42,     1,     1,    11,
      12,     5,     5,    32,     8,    50,     1,     7,    37,    11,
       5,    11,    12,    17,    90,    76,    10,    78,    42,    13,
      96,    50,    36,   104,    85,   101,    30,    31,    42,    96,
      42,    42,   261,   262,    38,    39,    40,    41,    42,    42,
      42,    36,    42,     1,    31,   274,     1,    42,     4,     4,
       5,     9,    42,     8,     8,    42,     7,    12,   134,    10,
     189,    15,    16,    18,    19,    20,    24,    22,     0,    23,
      25,    26,    27,    28,    29,   156,     3,    32,    33,   177,
     178,     6,   180,     1,   182,     9,    41,    42,    36,     7,
       0,     1,    10,     3,    42,    36,     8,    11,     8,    13,
       1,    42,    14,     4,     5,    15,    16,     8,     4,     7,
     239,    12,    10,    23,    11,   191,    13,    18,    19,    20,
      19,    22,     5,    22,    25,    26,    27,    28,    29,   258,
     259,    32,    33,    34,    35,    36,     8,     9,     4,     4,
      41,    42,    42,     8,    34,    35,    36,    12,   246,   247,
     248,   249,   233,    18,    19,    20,   232,    22,    10,    11,
      25,    26,    27,    28,    29,    13,    42,    32,    33,    10,
      11,    19,    37,    24,    22,    40,    41,    42,   254,    10,
      11,     4,    13,     4,    32,    10,    11,     8,   224,    37,
       6,    12,     8,     9,     6,    11,     4,    18,    19,    20,
       4,    22,    50,   279,    25,    26,    27,    28,    29,    42,
       5,    32,    33,    42,     8,     6,    42,    24,    42,     9,
      41,    42,     6,     6,    42,    42,     8,     8,     8,     6,
       5,     5,     8,     5,    13,     8,     5,     8,     6,     4,
      42,    42,     8,     4,     8,    42,    42,   160,     8,    42,
      42,    21,     7,    42,    42,    42,    13,     7,    11,     8,
      14,     8,     7,    11,    42,     5,     8,     5,    24,     8,
      11,   207,   100,   213,    11,    42,     8,    11,    13,    42,
      42,    24,    11,    42,    42,    13,     7,     6,    -1,   171,
      42,    42,   111,    96,    -1,    -1,    42,    -1,    42,    -1,
      -1,    -1,    -1,    -1,    -1,    -1,    42,    -1,    -1,    42,
      -1,    42
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     8,    15,    16,    23,    44,    45,    46,    48,    49,
      50,    51,    42,     4,     0,     1,    46,     3,     6,     1,
      42,    52,    53,    36,    42,    47,    42,    57,    52,     9,
       5,    52,     4,     1,     7,    10,    54,     1,     8,    17,
      30,    31,    38,    39,    40,    41,    42,    53,    55,    58,
      59,    60,    61,    93,    94,    98,     4,    42,    42,    58,
      24,    31,    42,     6,     4,     4,     4,    42,     1,    24,
       5,    58,     4,     8,    12,    18,    19,    20,    22,    25,
      26,    27,    28,    29,    32,    33,    37,    42,    53,    55,
      67,    72,    73,    84,    86,    91,    92,    98,     8,    42,
       6,    24,    42,    69,     1,    42,    95,    95,     5,    47,
      96,    97,     9,    73,     1,    62,    73,    42,    82,     6,
      65,    65,     6,    65,    36,    42,    71,    79,    71,    81,
       8,     8,     8,     6,    65,    42,     6,     8,    11,    73,
       7,    42,    68,    88,     8,     9,     5,    91,     8,    69,
      73,     7,    11,    12,    42,    95,     8,    14,     5,     5,
      13,     8,     1,     5,    96,    56,    62,     5,    62,     6,
       8,    66,     4,    42,    63,    64,    70,    10,    13,    42,
      10,    11,    13,     8,    10,    80,     8,    74,    73,     4,
      85,    21,    78,    42,     7,    10,    77,     7,    42,    42,
      42,    95,    42,    64,     8,    42,     7,    83,    68,    34,
      35,    36,    89,    90,    42,    13,     7,    11,    71,    71,
      71,    42,    71,    42,    14,    42,    62,    73,    42,    87,
      42,    42,    24,     8,     8,    88,     7,    42,    42,    11,
       5,    89,    42,    42,    70,    70,    10,    13,    10,    13,
      47,     8,     5,     8,    24,    73,    95,     7,    11,    11,
      62,    13,    11,    71,    71,    71,    71,    75,    73,    62,
      62,    70,    70,    42,    13,     8,    70,    76,    42,     7,
      73
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
yy_symbol_value_print (FILE *yyoutput, int yytype, const YYSTYPE * const yyvaluep, const YYLTYPE * const yylocationp, struct parse_io *parseio)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp, parseio)
    FILE *yyoutput;
    int yytype;
    const YYSTYPE * const yyvaluep;
    const YYLTYPE * const yylocationp;
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
yy_symbol_print (FILE *yyoutput, int yytype, const YYSTYPE * const yyvaluep, const YYLTYPE * const yylocationp, struct parse_io *parseio)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep, yylocationp, parseio)
    FILE *yyoutput;
    int yytype;
    const YYSTYPE * const yyvaluep;
    const YYLTYPE * const yylocationp;
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
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
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
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       , &(yylsp[(yyi + 1) - (yynrhs)])		       , parseio);
      fprintf (stderr, "\n");
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
      size_t yyn = 0;
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

  if (! (YYPACT_NINF < yyn && yyn < YYLAST))
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
      int yychecklim = YYLAST - yyn;
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
      case 42: /* "word" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1450 "ael.tab.c"
	break;
      case 45: /* "objects" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1458 "ael.tab.c"
	break;
      case 46: /* "object" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1466 "ael.tab.c"
	break;
      case 47: /* "context_name" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1471 "ael.tab.c"
	break;
      case 48: /* "context" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1479 "ael.tab.c"
	break;
      case 50: /* "macro" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1487 "ael.tab.c"
	break;
      case 51: /* "globals" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1495 "ael.tab.c"
	break;
      case 52: /* "global_statements" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1503 "ael.tab.c"
	break;
      case 53: /* "assignment" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1511 "ael.tab.c"
	break;
      case 55: /* "local_assignment" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1519 "ael.tab.c"
	break;
      case 57: /* "arglist" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1527 "ael.tab.c"
	break;
      case 58: /* "elements" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1535 "ael.tab.c"
	break;
      case 59: /* "element" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1543 "ael.tab.c"
	break;
      case 60: /* "ignorepat" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1551 "ael.tab.c"
	break;
      case 61: /* "extension" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1559 "ael.tab.c"
	break;
      case 62: /* "statements" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1567 "ael.tab.c"
	break;
      case 63: /* "timerange" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1572 "ael.tab.c"
	break;
      case 64: /* "timespec" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1580 "ael.tab.c"
	break;
      case 65: /* "test_expr" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1585 "ael.tab.c"
	break;
      case 67: /* "if_like_head" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1593 "ael.tab.c"
	break;
      case 68: /* "word_list" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1598 "ael.tab.c"
	break;
      case 70: /* "word3_list" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1603 "ael.tab.c"
	break;
      case 71: /* "goto_word" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1608 "ael.tab.c"
	break;
      case 72: /* "switch_statement" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1616 "ael.tab.c"
	break;
      case 73: /* "statement" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1624 "ael.tab.c"
	break;
      case 78: /* "opt_else" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1632 "ael.tab.c"
	break;
      case 79: /* "target" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1640 "ael.tab.c"
	break;
      case 80: /* "opt_pri" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1645 "ael.tab.c"
	break;
      case 81: /* "jumptarget" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1653 "ael.tab.c"
	break;
      case 82: /* "macro_call" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1661 "ael.tab.c"
	break;
      case 84: /* "application_call_head" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1669 "ael.tab.c"
	break;
      case 86: /* "application_call" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1677 "ael.tab.c"
	break;
      case 87: /* "opt_word" */
#line 178 "ael.y"
	{ free((yyvaluep->str));};
#line 1682 "ael.tab.c"
	break;
      case 88: /* "eval_arglist" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1690 "ael.tab.c"
	break;
      case 89: /* "case_statements" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1698 "ael.tab.c"
	break;
      case 90: /* "case_statement" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1706 "ael.tab.c"
	break;
      case 91: /* "macro_statements" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1714 "ael.tab.c"
	break;
      case 92: /* "macro_statement" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1722 "ael.tab.c"
	break;
      case 93: /* "switches" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1730 "ael.tab.c"
	break;
      case 94: /* "eswitches" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1738 "ael.tab.c"
	break;
      case 95: /* "switchlist" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1746 "ael.tab.c"
	break;
      case 96: /* "included_entry" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1754 "ael.tab.c"
	break;
      case 97: /* "includeslist" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1762 "ael.tab.c"
	break;
      case 98: /* "includes" */
#line 165 "ael.y"
	{
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1770 "ael.tab.c"
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
  /* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;
/* Location data for the look-ahead symbol.  */
YYLTYPE yylloc;

  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;

  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[2];

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

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
  yylloc.first_column = yylloc.last_column = 0;
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
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);
	YYSTACK_RELOCATE (yyls);
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

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
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

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
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
#line 186 "ael.y"
    { (yyval.pval) = parseio->pval = (yyvsp[(1) - (1)].pval); ;}
    break;

  case 3:
#line 189 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 4:
#line 190 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); ;}
    break;

  case 5:
#line 191 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (2)].pval);;}
    break;

  case 6:
#line 194 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 7:
#line 195 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 8:
#line 196 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 9:
#line 197 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 10:
#line 200 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 11:
#line 201 "ael.y"
    { (yyval.str) = strdup("default"); ;}
    break;

  case 12:
#line 204 "ael.y"
    {
		if (!(yyvsp[(5) - (6)].pval)) {
                        ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Warning! The empty context %s will be IGNORED!\n", 
				my_file, (yylsp[(4) - (6)]).first_line, (yylsp[(4) - (6)]).first_column, (yylsp[(4) - (6)]).last_column, (yyvsp[(3) - (6)].str) );
			(yyval.pval) = 0;
			free((yyvsp[(3) - (6)].str));
		} else {
			(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[(1) - (6)]), &(yylsp[(6) - (6)]));
			(yyval.pval)->u1.str = (yyvsp[(3) - (6)].str);
			(yyval.pval)->u2.statements = (yyvsp[(5) - (6)].pval);
			set_dads((yyval.pval),(yyvsp[(5) - (6)].pval));
			(yyval.pval)->u3.abstract = (yyvsp[(1) - (6)].intval);} ;}
    break;

  case 13:
#line 219 "ael.y"
    { (yyval.intval) = 1; ;}
    break;

  case 14:
#line 220 "ael.y"
    { (yyval.intval) = 0; ;}
    break;

  case 15:
#line 223 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[(1) - (8)]), &(yylsp[(8) - (8)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (8)].str); (yyval.pval)->u2.arglist = (yyvsp[(4) - (8)].pval); (yyval.pval)->u3.macro_statements = (yyvsp[(7) - (8)].pval);
        set_dads((yyval.pval),(yyvsp[(7) - (8)].pval));;}
    break;

  case 16:
#line 229 "ael.y"
    {
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.statements = (yyvsp[(3) - (4)].pval);
        set_dads((yyval.pval),(yyvsp[(3) - (4)].pval));;}
    break;

  case 17:
#line 235 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 18:
#line 236 "ael.y"
    {(yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); ;}
    break;

  case 19:
#line 237 "ael.y"
    {(yyval.pval)=(yyvsp[(2) - (2)].pval);;}
    break;

  case 20:
#line 240 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 21:
#line 240 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (5)].str);
		(yyval.pval)->u2.val = (yyvsp[(4) - (5)].str); ;}
    break;

  case 22:
#line 246 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 23:
#line 246 "ael.y"
    {
		(yyval.pval) = npval2(PV_LOCALVARDEC, &(yylsp[(1) - (6)]), &(yylsp[(6) - (6)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (6)].str);
		(yyval.pval)->u2.val = (yyvsp[(5) - (6)].str); ;}
    break;

  case 24:
#line 253 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 25:
#line 254 "ael.y"
    { (yyval.pval) = nword((yyvsp[(1) - (1)].str), &(yylsp[(1) - (1)])); ;}
    break;

  case 26:
#line 255 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (3)].pval), nword((yyvsp[(3) - (3)].str), &(yylsp[(3) - (3)]))); ;}
    break;

  case 27:
#line 256 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (2)].pval);;}
    break;

  case 28:
#line 259 "ael.y"
    {(yyval.pval)=0;;}
    break;

  case 29:
#line 260 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); ;}
    break;

  case 30:
#line 261 "ael.y"
    { (yyval.pval)=(yyvsp[(2) - (2)].pval);;}
    break;

  case 31:
#line 264 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 32:
#line 265 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 33:
#line 266 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 34:
#line 267 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 35:
#line 268 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 36:
#line 269 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 37:
#line 270 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 38:
#line 271 "ael.y"
    {free((yyvsp[(1) - (2)].str)); (yyval.pval)=0;;}
    break;

  case 39:
#line 272 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 40:
#line 275 "ael.y"
    {
		(yyval.pval) = npval2(PV_IGNOREPAT, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.str = (yyvsp[(3) - (4)].str);;}
    break;

  case 41:
#line 280 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (3)].str);
		(yyval.pval)->u2.statements = (yyvsp[(3) - (3)].pval); set_dads((yyval.pval),(yyvsp[(3) - (3)].pval));;}
    break;

  case 42:
#line 284 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (4)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (4)].pval); set_dads((yyval.pval),(yyvsp[(4) - (4)].pval));
		(yyval.pval)->u4.regexten=1;;}
    break;

  case 43:
#line 289 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (7)]), &(yylsp[(7) - (7)]));
		(yyval.pval)->u1.str = (yyvsp[(5) - (7)].str);
		(yyval.pval)->u2.statements = (yyvsp[(7) - (7)].pval); set_dads((yyval.pval),(yyvsp[(7) - (7)].pval));
		(yyval.pval)->u3.hints = (yyvsp[(3) - (7)].str);;}
    break;

  case 44:
#line 294 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[(1) - (8)]), &(yylsp[(8) - (8)]));
		(yyval.pval)->u1.str = (yyvsp[(6) - (8)].str);
		(yyval.pval)->u2.statements = (yyvsp[(8) - (8)].pval); set_dads((yyval.pval),(yyvsp[(8) - (8)].pval));
		(yyval.pval)->u4.regexten=1;
		(yyval.pval)->u3.hints = (yyvsp[(4) - (8)].str);;}
    break;

  case 45:
#line 304 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 46:
#line 305 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); ;}
    break;

  case 47:
#line 306 "ael.y"
    {(yyval.pval)=(yyvsp[(2) - (2)].pval);;}
    break;

  case 48:
#line 312 "ael.y"
    {
		asprintf(&(yyval.str), "%s:%s:%s", (yyvsp[(1) - (5)].str), (yyvsp[(3) - (5)].str), (yyvsp[(5) - (5)].str));
		free((yyvsp[(1) - (5)].str));
		free((yyvsp[(3) - (5)].str));
		free((yyvsp[(5) - (5)].str)); ;}
    break;

  case 49:
#line 317 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 50:
#line 321 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (7)].str), &(yylsp[(1) - (7)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (7)].str), &(yylsp[(3) - (7)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (7)].str), &(yylsp[(5) - (7)]));
		(yyval.pval)->next->next->next = nword((yyvsp[(7) - (7)].str), &(yylsp[(7) - (7)])); ;}
    break;

  case 51:
#line 329 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 52:
#line 329 "ael.y"
    { (yyval.str) = (yyvsp[(3) - (4)].str); ;}
    break;

  case 53:
#line 333 "ael.y"
    {
		(yyval.pval)= npval2(PV_IF, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (2)].str); ;}
    break;

  case 54:
#line 336 "ael.y"
    {
		(yyval.pval) = npval2(PV_RANDOM, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)]));
		(yyval.pval)->u1.str=(yyvsp[(2) - (2)].str);;}
    break;

  case 55:
#line 339 "ael.y"
    {
		(yyval.pval) = npval2(PV_IFTIME, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.list = (yyvsp[(3) - (4)].pval);
		prev_word = 0; ;}
    break;

  case 56:
#line 350 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str);;}
    break;

  case 57:
#line 351 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[(1) - (2)].str), (yyvsp[(2) - (2)].str));
		free((yyvsp[(1) - (2)].str));
		free((yyvsp[(2) - (2)].str));
		prev_word = (yyval.str);;}
    break;

  case 58:
#line 358 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str); ;}
    break;

  case 59:
#line 359 "ael.y"
    {
		asprintf(&((yyval.str)), "%s %s", (yyvsp[(1) - (2)].str), (yyvsp[(2) - (2)].str));
		free((yyvsp[(1) - (2)].str));
		free((yyvsp[(2) - (2)].str)); ;}
    break;

  case 60:
#line 363 "ael.y"
    {
		asprintf(&((yyval.str)), "%s:%s", (yyvsp[(1) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(1) - (3)].str));
		free((yyvsp[(3) - (3)].str)); ;}
    break;

  case 61:
#line 367 "ael.y"
    {  /* there are often '&' in hints */
		asprintf(&((yyval.str)), "%s&%s", (yyvsp[(1) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(1) - (3)].str));
		free((yyvsp[(3) - (3)].str));;}
    break;

  case 62:
#line 373 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str);;}
    break;

  case 63:
#line 374 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[(1) - (2)].str), (yyvsp[(2) - (2)].str));
		free((yyvsp[(1) - (2)].str));
		free((yyvsp[(2) - (2)].str));
		prev_word = (yyval.str);;}
    break;

  case 64:
#line 379 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s%s", (yyvsp[(1) - (3)].str), (yyvsp[(2) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(1) - (3)].str));
		free((yyvsp[(2) - (3)].str));
		free((yyvsp[(3) - (3)].str));
		prev_word=(yyval.str);;}
    break;

  case 65:
#line 387 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str);;}
    break;

  case 66:
#line 388 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[(1) - (2)].str), (yyvsp[(2) - (2)].str));
		free((yyvsp[(1) - (2)].str));
		free((yyvsp[(2) - (2)].str));;}
    break;

  case 67:
#line 392 "ael.y"
    {
		asprintf(&((yyval.str)), "%s:%s", (yyvsp[(1) - (3)].str), (yyvsp[(3) - (3)].str));
		free((yyvsp[(1) - (3)].str));
		free((yyvsp[(3) - (3)].str));;}
    break;

  case 68:
#line 398 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCH, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (5)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (5)].pval); set_dads((yyval.pval),(yyvsp[(4) - (5)].pval));;}
    break;

  case 69:
#line 407 "ael.y"
    {
		(yyval.pval) = npval2(PV_STATEMENTBLOCK, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.list = (yyvsp[(2) - (3)].pval); set_dads((yyval.pval),(yyvsp[(2) - (3)].pval));;}
    break;

  case 70:
#line 410 "ael.y"
    { (yyval.pval) = (yyvsp[(1) - (1)].pval); ;}
    break;

  case 71:
#line 411 "ael.y"
    { (yyval.pval) = (yyvsp[(1) - (1)].pval); ;}
    break;

  case 72:
#line 412 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.list = (yyvsp[(2) - (3)].pval);;}
    break;

  case 73:
#line 415 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.list = (yyvsp[(2) - (3)].pval);;}
    break;

  case 74:
#line 418 "ael.y"
    {
		(yyval.pval) = npval2(PV_LABEL, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (2)].str); ;}
    break;

  case 75:
#line 421 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 76:
#line 422 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 77:
#line 423 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 78:
#line 423 "ael.y"
    { /* XXX word_list maybe ? */
		(yyval.pval) = npval2(PV_FOR, &(yylsp[(1) - (12)]), &(yylsp[(12) - (12)]));
		(yyval.pval)->u1.for_init = (yyvsp[(4) - (12)].str);
		(yyval.pval)->u2.for_test=(yyvsp[(7) - (12)].str);
		(yyval.pval)->u3.for_inc = (yyvsp[(10) - (12)].str);
		(yyval.pval)->u4.for_statements = (yyvsp[(12) - (12)].pval); set_dads((yyval.pval),(yyvsp[(12) - (12)].pval));;}
    break;

  case 79:
#line 429 "ael.y"
    {
		(yyval.pval) = npval2(PV_WHILE, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (3)].str);
		(yyval.pval)->u2.statements = (yyvsp[(3) - (3)].pval); set_dads((yyval.pval),(yyvsp[(3) - (3)].pval));;}
    break;

  case 80:
#line 433 "ael.y"
    { (yyval.pval) = (yyvsp[(1) - (1)].pval); ;}
    break;

  case 81:
#line 434 "ael.y"
    { (yyval.pval) = update_last((yyvsp[(2) - (3)].pval), &(yylsp[(2) - (3)])); ;}
    break;

  case 82:
#line 435 "ael.y"
    { (yyval.pval) = update_last((yyvsp[(1) - (2)].pval), &(yylsp[(2) - (2)])); ;}
    break;

  case 83:
#line 436 "ael.y"
    {
		(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (2)].str);;}
    break;

  case 84:
#line 439 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 85:
#line 439 "ael.y"
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
	;}
    break;

  case 86:
#line 472 "ael.y"
    { (yyval.pval) = npval2(PV_BREAK, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)])); ;}
    break;

  case 87:
#line 473 "ael.y"
    { (yyval.pval) = npval2(PV_RETURN, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)])); ;}
    break;

  case 88:
#line 474 "ael.y"
    { (yyval.pval) = npval2(PV_CONTINUE, &(yylsp[(1) - (2)]), &(yylsp[(2) - (2)])); ;}
    break;

  case 89:
#line 475 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[(1) - (3)].pval), &(yylsp[(2) - (3)]));
		(yyval.pval)->u2.statements = (yyvsp[(2) - (3)].pval); set_dads((yyval.pval),(yyvsp[(2) - (3)].pval));
		(yyval.pval)->u3.else_statements = (yyvsp[(3) - (3)].pval);set_dads((yyval.pval),(yyvsp[(3) - (3)].pval));;}
    break;

  case 90:
#line 479 "ael.y"
    { (yyval.pval)=0; ;}
    break;

  case 91:
#line 482 "ael.y"
    { (yyval.pval) = (yyvsp[(2) - (2)].pval); ;}
    break;

  case 92:
#line 483 "ael.y"
    { (yyval.pval) = NULL ; ;}
    break;

  case 93:
#line 486 "ael.y"
    { (yyval.pval) = nword((yyvsp[(1) - (1)].str), &(yylsp[(1) - (1)])); ;}
    break;

  case 94:
#line 487 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (3)].str), &(yylsp[(1) - (3)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (3)].str), &(yylsp[(3) - (3)])); ;}
    break;

  case 95:
#line 490 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (3)].str), &(yylsp[(1) - (3)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (3)].str), &(yylsp[(3) - (3)])); ;}
    break;

  case 96:
#line 493 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (5)].str), &(yylsp[(1) - (5)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (5)].str), &(yylsp[(3) - (5)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (5)].str), &(yylsp[(5) - (5)])); ;}
    break;

  case 97:
#line 497 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (5)].str), &(yylsp[(1) - (5)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (5)].str), &(yylsp[(3) - (5)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (5)].str), &(yylsp[(5) - (5)])); ;}
    break;

  case 98:
#line 501 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[(1) - (5)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (5)].str), &(yylsp[(3) - (5)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (5)].str), &(yylsp[(5) - (5)])); ;}
    break;

  case 99:
#line 505 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[(1) - (5)]));
		(yyval.pval)->next = nword((yyvsp[(3) - (5)].str), &(yylsp[(3) - (5)]));
		(yyval.pval)->next->next = nword((yyvsp[(5) - (5)].str), &(yylsp[(5) - (5)])); ;}
    break;

  case 100:
#line 511 "ael.y"
    { (yyval.str) = strdup("1"); ;}
    break;

  case 101:
#line 512 "ael.y"
    { (yyval.str) = (yyvsp[(2) - (2)].str); ;}
    break;

  case 102:
#line 516 "ael.y"
    {			/* ext[, pri] default 1 */
		(yyval.pval) = nword((yyvsp[(1) - (2)].str), &(yylsp[(1) - (2)]));
		(yyval.pval)->next = nword((yyvsp[(2) - (2)].str), &(yylsp[(2) - (2)])); ;}
    break;

  case 103:
#line 519 "ael.y"
    {	/* context, ext, pri */
		(yyval.pval) = nword((yyvsp[(4) - (4)].str), &(yylsp[(4) - (4)]));
		(yyval.pval)->next = nword((yyvsp[(1) - (4)].str), &(yylsp[(1) - (4)]));
		(yyval.pval)->next->next = nword((yyvsp[(2) - (4)].str), &(yylsp[(2) - (4)])); ;}
    break;

  case 104:
#line 525 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 105:
#line 525 "ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		(yyval.pval) = npval2(PV_MACRO_CALL, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (5)].str);
		(yyval.pval)->u2.arglist = (yyvsp[(4) - (5)].pval);;}
    break;

  case 106:
#line 530 "ael.y"
    {
		(yyval.pval)= npval2(PV_MACRO_CALL, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.str = (yyvsp[(1) - (3)].str); ;}
    break;

  case 107:
#line 538 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 108:
#line 538 "ael.y"
    {
		if (strcasecmp((yyvsp[(1) - (3)].str),"goto") == 0) {
			(yyval.pval) = npval2(PV_GOTO, &(yylsp[(1) - (3)]), &(yylsp[(2) - (3)]));
			free((yyvsp[(1) - (3)].str)); /* won't be using this */
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Suggestion: Use the goto statement instead of the Goto() application call in AEL.\n", my_file, (yylsp[(1) - (3)]).first_line, (yylsp[(1) - (3)]).first_column, (yylsp[(1) - (3)]).last_column );
		} else {
			(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[(1) - (3)]), &(yylsp[(2) - (3)]));
			(yyval.pval)->u1.str = (yyvsp[(1) - (3)].str);
		} ;}
    break;

  case 109:
#line 549 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[(1) - (3)].pval), &(yylsp[(3) - (3)]));
 		if( (yyval.pval)->type == PV_GOTO )
			(yyval.pval)->u1.list = (yyvsp[(2) - (3)].pval);
	 	else
			(yyval.pval)->u2.arglist = (yyvsp[(2) - (3)].pval);
	;}
    break;

  case 110:
#line 556 "ael.y"
    { (yyval.pval) = update_last((yyvsp[(1) - (2)].pval), &(yylsp[(2) - (2)])); ;}
    break;

  case 111:
#line 559 "ael.y"
    { (yyval.str) = (yyvsp[(1) - (1)].str) ;}
    break;

  case 112:
#line 560 "ael.y"
    { (yyval.str) = strdup(""); ;}
    break;

  case 113:
#line 563 "ael.y"
    { (yyval.pval) = nword((yyvsp[(1) - (1)].str), &(yylsp[(1) - (1)])); ;}
    break;

  case 114:
#line 564 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		(yyval.pval)->u1.str = strdup(""); ;}
    break;

  case 115:
#line 567 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (3)].pval), nword((yyvsp[(3) - (3)].str), &(yylsp[(3) - (3)]))); ;}
    break;

  case 116:
#line 570 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 117:
#line 571 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); ;}
    break;

  case 118:
#line 574 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[(1) - (4)]), &(yylsp[(3) - (4)])); /* XXX 3 or 4 ? */
		(yyval.pval)->u1.str = (yyvsp[(2) - (4)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (4)].pval); set_dads((yyval.pval),(yyvsp[(4) - (4)].pval));;}
    break;

  case 119:
#line 578 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));
		(yyval.pval)->u1.str = NULL;
		(yyval.pval)->u2.statements = (yyvsp[(3) - (3)].pval);set_dads((yyval.pval),(yyvsp[(3) - (3)].pval));;}
    break;

  case 120:
#line 582 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)])); /* XXX@3 or @4 ? */
		(yyval.pval)->u1.str = (yyvsp[(2) - (4)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (4)].pval);set_dads((yyval.pval),(yyvsp[(4) - (4)].pval));;}
    break;

  case 121:
#line 588 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 122:
#line 589 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (2)].pval), (yyvsp[(2) - (2)].pval)); ;}
    break;

  case 123:
#line 592 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 124:
#line 593 "ael.y"
    { (yyval.pval)=(yyvsp[(1) - (1)].pval);;}
    break;

  case 125:
#line 594 "ael.y"
    {
		(yyval.pval) = npval2(PV_CATCH, &(yylsp[(1) - (5)]), &(yylsp[(5) - (5)]));
		(yyval.pval)->u1.str = (yyvsp[(2) - (5)].str);
		(yyval.pval)->u2.statements = (yyvsp[(4) - (5)].pval); set_dads((yyval.pval),(yyvsp[(4) - (5)].pval));;}
    break;

  case 126:
#line 600 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[(1) - (4)]), &(yylsp[(2) - (4)]));
		(yyval.pval)->u1.list = (yyvsp[(3) - (4)].pval); set_dads((yyval.pval),(yyvsp[(3) - (4)].pval));;}
    break;

  case 127:
#line 605 "ael.y"
    {
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[(1) - (4)]), &(yylsp[(2) - (4)]));
		(yyval.pval)->u1.list = (yyvsp[(3) - (4)].pval); set_dads((yyval.pval),(yyvsp[(3) - (4)].pval));;}
    break;

  case 128:
#line 610 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 129:
#line 611 "ael.y"
    { (yyval.pval) = linku1(nword((yyvsp[(1) - (3)].str), &(yylsp[(1) - (3)])), (yyvsp[(3) - (3)].pval)); ;}
    break;

  case 130:
#line 612 "ael.y"
    { char *x; asprintf(&x,"%s@%s", (yyvsp[(1) - (5)].str),(yyvsp[(3) - (5)].str)); free((yyvsp[(1) - (5)].str)); free((yyvsp[(3) - (5)].str));
									  (yyval.pval) = linku1(nword(x, &(yylsp[(1) - (5)])), (yyvsp[(5) - (5)].pval));;}
    break;

  case 131:
#line 614 "ael.y"
    {(yyval.pval)=(yyvsp[(2) - (2)].pval);;}
    break;

  case 132:
#line 617 "ael.y"
    { (yyval.pval) = nword((yyvsp[(1) - (1)].str), &(yylsp[(1) - (1)])); ;}
    break;

  case 133:
#line 618 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[(1) - (3)].str), &(yylsp[(1) - (3)]));
		(yyval.pval)->u2.arglist = (yyvsp[(3) - (3)].pval);
		prev_word=0; /* XXX sure ? */ ;}
    break;

  case 134:
#line 625 "ael.y"
    { (yyval.pval) = (yyvsp[(1) - (2)].pval); ;}
    break;

  case 135:
#line 626 "ael.y"
    { (yyval.pval) = linku1((yyvsp[(1) - (3)].pval), (yyvsp[(2) - (3)].pval)); ;}
    break;

  case 136:
#line 627 "ael.y"
    {(yyval.pval)=(yyvsp[(1) - (2)].pval);;}
    break;

  case 137:
#line 630 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[(1) - (4)]), &(yylsp[(4) - (4)]));
		(yyval.pval)->u1.list = (yyvsp[(3) - (4)].pval);set_dads((yyval.pval),(yyvsp[(3) - (4)].pval));;}
    break;

  case 138:
#line 633 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[(1) - (3)]), &(yylsp[(3) - (3)]));;}
    break;


/* Line 1270 of yacc.c.  */
#line 2985 "ael.tab.c"
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
      yyerror (&yylloc, parseio, YY_("syntax error"));
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
	    yyerror (&yylloc, parseio, yymsg);
	  }
	else
	  {
	    yyerror (&yylloc, parseio, YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }

  yyerror_range[0] = yylloc;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
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

  /* Else will try to reuse look-ahead token after shifting the error
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
		  yystos[yystate], yyvsp, yylsp, parseio);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;

  yyerror_range[1] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the look-ahead.  YYLOC is available though.  */
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

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (&yylloc, parseio, YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval, &yylloc, parseio);
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
  return yyresult;
}


#line 638 "ael.y"


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

/* this routine adds a dad ptr to each element in the list */
static void set_dads(struct pval *dad, struct pval *child_list)
{
	struct pval *t;
	
	for(t=child_list;t;t=t->next)  /* simple stuff */
		t->dad = dad;
}


