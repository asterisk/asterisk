/* A Bison parser, made by GNU Bison 2.1.  */

/* Skeleton parser for Yacc-like parsing with Bison,
   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

/* Written by Richard Stallman by simplifying the original so called
   ``semantic'' parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.1"

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
     word = 296
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
#define word 296




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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asterisk/logger.h"
#include "asterisk/utils.h"		/* ast_calloc() */
#include "asterisk/ael_structs.h"

/* create a new object with start-end marker */
static pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column);

static void linku1(pval *head, pval *tail);

void reset_parencount(yyscan_t yyscanner);
void reset_semicount(yyscan_t yyscanner);
void reset_argcount(yyscan_t yyscanner );

#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner
#define YYERROR_VERBOSE 1

extern char *my_file;
#ifdef AAL_ARGCHECK
int ael_is_funcname(char *name);
#endif
static char *ael_token_subst(char *mess);



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

#if ! defined (YYSTYPE) && ! defined (YYSTYPE_IS_DECLARED)
#line 53 "ael.y"
typedef union YYSTYPE {
	char *str;
	struct pval *pval;
} YYSTYPE;
/* Line 196 of yacc.c.  */
#line 231 "ael.tab.c"
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined (YYLTYPE) && ! defined (YYLTYPE_IS_DECLARED)
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
#line 58 "ael.y"

	/* declaring these AFTER the union makes things a lot simpler! */
void yyerror(YYLTYPE *locp, struct parse_io *parseio, char const *s);
int ael_yylex (YYSTYPE * yylval_param, YYLTYPE * yylloc_param , void * yyscanner);

/* create a new object with start-end marker, simplified interface.
 * Must be declared here because YYLTYPE is not known before
 */
static pval *npval2(pvaltype type, YYLTYPE *first, YYLTYPE *last);


/* Line 219 of yacc.c.  */
#line 265 "ael.tab.c"

#if ! defined (YYSIZE_T) && defined (__SIZE_TYPE__)
# define YYSIZE_T __SIZE_TYPE__
#endif
#if ! defined (YYSIZE_T) && defined (size_t)
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T) && (defined (__STDC__) || defined (__cplusplus))
# include <stddef.h> /* INFRINGES ON USER NAME SPACE */
# define YYSIZE_T size_t
#endif
#if ! defined (YYSIZE_T)
# define YYSIZE_T unsigned int
#endif

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

#if ! defined (yyoverflow) || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if defined (__STDC__) || defined (__cplusplus)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     define YYINCLUDED_STDLIB_H
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning. */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2005 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM ((YYSIZE_T) -1)
#  endif
#  ifdef __cplusplus
extern "C" {
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if (! defined (malloc) && ! defined (YYINCLUDED_STDLIB_H) \
	&& (defined (__STDC__) || defined (__cplusplus)))
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if (! defined (free) && ! defined (YYINCLUDED_STDLIB_H) \
	&& (defined (__STDC__) || defined (__cplusplus)))
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifdef __cplusplus
}
#  endif
# endif
#endif /* ! defined (yyoverflow) || YYERROR_VERBOSE */


#if (! defined (yyoverflow) \
     && (! defined (__cplusplus) \
	 || (defined (YYLTYPE_IS_TRIVIAL) && YYLTYPE_IS_TRIVIAL \
             && defined (YYSTYPE_IS_TRIVIAL) && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  short int yyss;
  YYSTYPE yyvs;
    YYLTYPE yyls;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (short int) + sizeof (YYSTYPE) + sizeof (YYLTYPE))	\
      + 2 * YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined (__GNUC__) && 1 < __GNUC__
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
      while (0)
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
    while (0)

#endif

#if defined (__STDC__) || defined (__cplusplus)
   typedef signed char yysigned_char;
#else
   typedef short int yysigned_char;
#endif

/* YYFINAL -- State number of the termination state. */
#define YYFINAL  17
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   584

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  52
/* YYNRULES -- Number of rules. */
#define YYNRULES  151
/* YYNRULES -- Number of states. */
#define YYNSTATES  354

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   296

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const unsigned char yytranslate[] =
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
      35,    36,    37,    38,    39,    40,    41
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const unsigned short int yyprhs[] =
{
       0,     0,     3,     5,     7,    10,    13,    15,    17,    19,
      21,    27,    32,    38,    43,    50,    56,    63,    69,    78,
      86,    94,   101,   106,   110,   112,   115,   118,   119,   125,
     127,   131,   134,   136,   138,   141,   144,   146,   148,   150,
     152,   154,   155,   161,   164,   166,   171,   175,   180,   188,
     197,   199,   202,   205,   206,   212,   213,   219,   234,   245,
     247,   250,   252,   255,   259,   261,   264,   268,   269,   276,
     280,   281,   287,   291,   295,   298,   299,   300,   301,   314,
     315,   322,   325,   329,   333,   336,   339,   340,   346,   349,
     352,   355,   358,   363,   366,   371,   374,   379,   381,   383,
     387,   391,   397,   403,   409,   415,   417,   421,   427,   431,
     437,   441,   442,   448,   452,   453,   457,   461,   464,   466,
     467,   471,   474,   476,   479,   484,   488,   493,   497,   500,
     504,   506,   509,   511,   517,   522,   526,   531,   535,   538,
     542,   545,   548,   563,   574,   578,   594,   606,   609,   611,
     613,   618
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      43,     0,    -1,    44,    -1,    45,    -1,    44,    45,    -1,
      44,     1,    -1,    46,    -1,    47,    -1,    48,    -1,     8,
      -1,     3,    41,     4,    53,     5,    -1,     3,    41,     4,
       5,    -1,     3,    36,     4,    53,     5,    -1,     3,    36,
       4,     5,    -1,    23,     3,    41,     4,    53,     5,    -1,
      23,     3,    41,     4,     5,    -1,    23,     3,    36,     4,
      53,     5,    -1,    23,     3,    36,     4,     5,    -1,    15,
      41,     6,    52,     7,     4,    86,     5,    -1,    15,    41,
       6,    52,     7,     4,     5,    -1,    15,    41,     6,     7,
       4,    86,     5,    -1,    15,    41,     6,     7,     4,     5,
      -1,    16,     4,    49,     5,    -1,    16,     4,     5,    -1,
      50,    -1,    49,    50,    -1,    49,     1,    -1,    -1,    41,
       9,    51,    41,     8,    -1,    41,    -1,    52,    10,    41,
      -1,    52,     1,    -1,    54,    -1,     1,    -1,    53,    54,
      -1,    53,     1,    -1,    57,    -1,    93,    -1,    88,    -1,
      89,    -1,    56,    -1,    -1,    41,     9,    55,    41,     8,
      -1,    41,     1,    -1,     8,    -1,    17,    24,    41,     8,
      -1,    41,    24,    69,    -1,    30,    41,    24,    69,    -1,
      31,     6,    65,     7,    41,    24,    69,    -1,    30,    31,
       6,    65,     7,    41,    24,    69,    -1,    69,    -1,    58,
      69,    -1,    58,     1,    -1,    -1,    19,     6,    60,    64,
       7,    -1,    -1,    22,     6,    62,    64,     7,    -1,    20,
       6,    65,    11,    65,    11,    65,    13,    65,    13,    65,
      13,    65,     7,    -1,    20,     6,    41,    13,    65,    13,
      65,    13,    65,     7,    -1,    41,    -1,    41,    41,    -1,
      41,    -1,    41,    41,    -1,    41,    41,    41,    -1,    41,
      -1,    41,    41,    -1,    41,    11,    41,    -1,    -1,    18,
       6,    68,    41,     7,     4,    -1,     4,    58,     5,    -1,
      -1,    41,     9,    70,    41,     8,    -1,    25,    76,     8,
      -1,    26,    77,     8,    -1,    41,    11,    -1,    -1,    -1,
      -1,    32,     6,    71,    41,     8,    72,    41,     8,    73,
      41,     7,    69,    -1,    -1,    33,     6,    74,    41,     7,
      69,    -1,    67,     5,    -1,    67,    84,     5,    -1,    12,
      78,     8,    -1,    82,     8,    -1,    41,     8,    -1,    -1,
      82,     9,    75,    41,     8,    -1,    28,     8,    -1,    27,
       8,    -1,    29,     8,    -1,    61,    69,    -1,    61,    69,
      21,    69,    -1,    59,    69,    -1,    59,    69,    21,    69,
      -1,    63,    69,    -1,    63,    69,    21,    69,    -1,     8,
      -1,    66,    -1,    66,    13,    66,    -1,    66,    10,    66,
      -1,    66,    13,    66,    13,    66,    -1,    66,    10,    66,
      10,    66,    -1,    36,    13,    66,    13,    66,    -1,    36,
      10,    66,    10,    66,    -1,    66,    -1,    66,    10,    66,
      -1,    66,    10,    41,    14,    41,    -1,    66,    14,    66,
      -1,    66,    10,    41,    14,    36,    -1,    66,    14,    36,
      -1,    -1,    41,     6,    79,    83,     7,    -1,    41,     6,
       7,    -1,    -1,    41,    81,     6,    -1,    80,    83,     7,
      -1,    80,     7,    -1,    64,    -1,    -1,    83,    10,    41,
      -1,    83,    10,    -1,    85,    -1,    84,    85,    -1,    34,
      41,    11,    58,    -1,    36,    11,    58,    -1,    35,    41,
      11,    58,    -1,    34,    41,    11,    -1,    36,    11,    -1,
      35,    41,    11,    -1,    87,    -1,    86,    87,    -1,    69,
      -1,    37,    41,     4,    58,     5,    -1,    38,     4,    90,
       5,    -1,    38,     4,     5,    -1,    39,     4,    90,     5,
      -1,    39,     4,     5,    -1,    41,     8,    -1,    90,    41,
       8,    -1,    90,     1,    -1,    92,     8,    -1,    92,    13,
      65,    11,    65,    11,    65,    13,    65,    13,    65,    13,
      65,     8,    -1,    92,    13,    41,    13,    65,    13,    65,
      13,    65,     8,    -1,    91,    92,     8,    -1,    91,    92,
      13,    65,    11,    65,    11,    65,    13,    65,    13,    65,
      13,    65,     8,    -1,    91,    92,    13,    41,    13,    65,
      13,    65,    13,    65,     8,    -1,    91,     1,    -1,    41,
      -1,    36,    -1,    40,     4,    91,     5,    -1,    40,     4,
       5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   160,   160,   163,   164,   175,   178,   179,   180,   181,
     184,   188,   191,   195,   198,   203,   207,   212,   218,   221,
     224,   227,   232,   235,   240,   241,   242,   245,   245,   251,
     254,   259,   262,   263,   264,   267,   270,   271,   272,   273,
     274,   275,   275,   279,   280,   283,   288,   292,   297,   302,
     311,   312,   315,   318,   318,   323,   323,   328,   344,   364,
     365,   372,   373,   378,   386,   387,   391,   397,   397,   405,
     408,   408,   412,   415,   418,   421,   422,   423,   421,   429,
     429,   433,   437,   442,   446,   450,   453,   453,   486,   488,
     490,   492,   497,   503,   508,   514,   519,   525,   528,   529,
     534,   539,   546,   553,   560,   569,   574,   579,   586,   593,
     600,   609,   609,   614,   619,   619,   629,   635,   638,   641,
     644,   649,   656,   657,   662,   666,   670,   674,   677,   680,
     685,   686,   691,   692,   695,   696,   699,   700,   703,   704,
     705,   708,   709,   725,   738,   739,   754,   767,   770,   771,
     774,   777
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals. */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "KW_CONTEXT", "LC", "RC", "LP", "RP",
  "SEMI", "EQ", "COMMA", "COLON", "AMPER", "BAR", "AT", "KW_MACRO",
  "KW_GLOBALS", "KW_IGNOREPAT", "KW_SWITCH", "KW_IF", "KW_IFTIME",
  "KW_ELSE", "KW_RANDOM", "KW_ABSTRACT", "EXTENMARK", "KW_GOTO", "KW_JUMP",
  "KW_RETURN", "KW_BREAK", "KW_CONTINUE", "KW_REGEXTEN", "KW_HINT",
  "KW_FOR", "KW_WHILE", "KW_CASE", "KW_PATTERN", "KW_DEFAULT", "KW_CATCH",
  "KW_SWITCHES", "KW_ESWITCHES", "KW_INCLUDES", "word", "$accept", "file",
  "objects", "object", "context", "macro", "globals", "global_statements",
  "global_statement", "@1", "arglist", "elements", "element", "@2",
  "ignorepat", "extension", "statements", "if_head", "@3", "random_head",
  "@4", "iftime_head", "word_list", "word3_list", "goto_word",
  "switch_head", "@5", "statement", "@6", "@7", "@8", "@9", "@10", "@11",
  "target", "jumptarget", "macro_call", "@12", "application_call_head",
  "@13", "application_call", "eval_arglist", "case_statements",
  "case_statement", "macro_statements", "macro_statement", "switches",
  "eswitches", "switchlist", "includeslist", "includedname", "includes", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const unsigned short int yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,   281,   282,   283,   284,
     285,   286,   287,   288,   289,   290,   291,   292,   293,   294,
     295,   296
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const unsigned char yyr1[] =
{
       0,    42,    43,    44,    44,    44,    45,    45,    45,    45,
      46,    46,    46,    46,    46,    46,    46,    46,    47,    47,
      47,    47,    48,    48,    49,    49,    49,    51,    50,    52,
      52,    52,    53,    53,    53,    53,    54,    54,    54,    54,
      54,    55,    54,    54,    54,    56,    57,    57,    57,    57,
      58,    58,    58,    60,    59,    62,    61,    63,    63,    64,
      64,    65,    65,    65,    66,    66,    66,    68,    67,    69,
      70,    69,    69,    69,    69,    71,    72,    73,    69,    74,
      69,    69,    69,    69,    69,    69,    75,    69,    69,    69,
      69,    69,    69,    69,    69,    69,    69,    69,    76,    76,
      76,    76,    76,    76,    76,    77,    77,    77,    77,    77,
      77,    79,    78,    78,    81,    80,    82,    82,    83,    83,
      83,    83,    84,    84,    85,    85,    85,    85,    85,    85,
      86,    86,    87,    87,    88,    88,    89,    89,    90,    90,
      90,    91,    91,    91,    91,    91,    91,    91,    92,    92,
      93,    93
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       5,     4,     5,     4,     6,     5,     6,     5,     8,     7,
       7,     6,     4,     3,     1,     2,     2,     0,     5,     1,
       3,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     0,     5,     2,     1,     4,     3,     4,     7,     8,
       1,     2,     2,     0,     5,     0,     5,    14,    10,     1,
       2,     1,     2,     3,     1,     2,     3,     0,     6,     3,
       0,     5,     3,     3,     2,     0,     0,     0,    12,     0,
       6,     2,     3,     3,     2,     2,     0,     5,     2,     2,
       2,     2,     4,     2,     4,     2,     4,     1,     1,     3,
       3,     5,     5,     5,     5,     1,     3,     5,     3,     5,
       3,     0,     5,     3,     0,     3,     3,     2,     1,     0,
       3,     2,     1,     2,     4,     3,     4,     3,     2,     3,
       1,     2,     1,     5,     4,     3,     4,     3,     2,     3,
       2,     2,    14,    10,     3,    15,    11,     2,     1,     1,
       4,     3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       0,     0,     9,     0,     0,     0,     0,     0,     3,     6,
       7,     8,     0,     0,     0,     0,     0,     1,     5,     4,
       0,     0,     0,    23,     0,     0,    24,     0,     0,    33,
      13,    44,     0,     0,     0,     0,     0,     0,     0,     0,
      32,    40,    36,    38,    39,    37,    11,     0,     0,    29,
       0,    27,    26,    22,    25,     0,     0,     0,     0,     0,
       0,     0,     0,     0,    43,    41,     0,    35,    12,    34,
      10,     0,    31,     0,     0,     0,    17,     0,    15,     0,
       0,     0,     0,    61,     0,   135,     0,     0,   137,     0,
     151,   149,   148,     0,     0,     0,     0,    97,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     114,     0,     0,     0,     0,    46,   119,     0,    21,     0,
     132,     0,   130,     0,    30,     0,    16,    14,    45,     0,
      47,    62,     0,   138,   140,   134,     0,   136,   147,   150,
       0,   141,     0,     0,     0,    50,     0,     0,    67,    53,
       0,    55,     0,    64,    98,     0,   105,     0,    89,    88,
      90,    75,    79,    85,    70,    74,     0,    93,    91,    95,
      81,     0,     0,     0,     0,   122,   117,    59,   118,     0,
      84,    86,     0,    20,   131,    19,     0,    28,     0,    63,
       0,   139,   144,     0,    61,     0,    42,    52,    69,    51,
     111,    83,     0,     0,    61,     0,     0,     0,     0,     0,
      65,     0,     0,    72,     0,     0,    73,     0,     0,     0,
     115,     0,     0,     0,     0,     0,   128,    82,   123,    60,
     116,   121,     0,     0,    18,     0,     0,    61,     0,     0,
       0,   113,   119,     0,     0,     0,     0,     0,     0,     0,
      66,   100,    99,    64,   106,   110,   108,     0,     0,     0,
      94,    92,    96,   127,   129,     0,   120,     0,     0,     0,
      48,     0,     0,     0,     0,     0,     0,    54,     0,     0,
      56,     0,     0,     0,     0,     0,    76,     0,    71,     0,
       0,    87,   133,    49,     0,     0,     0,     0,   112,    68,
       0,     0,   104,   103,   102,   101,   109,   107,     0,    80,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    77,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   143,     0,    58,     0,     0,   146,     0,
       0,     0,     0,     0,     0,     0,    78,     0,     0,     0,
       0,   142,    57,   145
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     6,     7,     8,     9,    10,    11,    25,    26,    75,
      50,    39,    40,    95,    41,    42,   144,   111,   203,   112,
     206,   113,   178,    84,   154,   114,   202,   145,   219,   217,
     308,   330,   218,   232,   155,   157,   147,   242,   116,   166,
     117,   179,   174,   175,   121,   122,    43,    44,    87,    93,
      94,    45
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -215
static const short int yypact[] =
{
     212,   127,  -215,   -35,    55,    23,    85,   561,  -215,  -215,
    -215,  -215,    64,   118,   112,    13,   128,  -215,  -215,  -215,
     157,   208,     2,  -215,   123,    19,  -215,   130,   137,  -215,
    -215,  -215,   143,    43,   169,   181,   182,   189,   126,   284,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,   337,   196,  -215,
     141,  -215,  -215,  -215,  -215,   342,   357,   135,   188,   177,
     170,    22,    36,    29,  -215,  -215,   527,  -215,  -215,  -215,
    -215,   423,  -215,   206,   191,   195,  -215,   384,  -215,   399,
     210,   170,   527,   200,   215,  -215,   242,    51,  -215,    57,
    -215,  -215,  -215,    12,   158,   220,   527,  -215,   223,   245,
     250,   259,   262,   148,   229,   263,   265,   266,   269,   275,
     251,   527,   527,   527,   156,  -215,    26,   145,  -215,   243,
    -215,   449,  -215,   475,  -215,   279,  -215,  -215,  -215,   281,
    -215,   252,   253,  -215,  -215,  -215,   283,  -215,  -215,  -215,
     204,  -215,   254,   291,   278,  -215,   296,   300,  -215,  -215,
     276,  -215,    69,    40,    95,   310,   114,   313,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,   323,   311,   318,   325,
    -215,   290,   303,   340,   172,  -215,  -215,   307,  -215,   230,
    -215,  -215,   348,  -215,  -215,  -215,   501,  -215,   312,  -215,
     331,  -215,  -215,   315,    58,   346,  -215,  -215,  -215,  -215,
     353,  -215,   320,   322,    76,   355,   322,   229,   229,   328,
    -215,   229,   229,  -215,   329,   178,  -215,   330,   338,   343,
    -215,   527,   527,   527,   375,   379,   527,  -215,  -215,  -215,
    -215,   350,   352,   527,  -215,   370,   527,   108,   388,   170,
     170,  -215,   322,   395,   396,   170,   170,   398,   354,   393,
    -215,   400,   404,    50,  -215,  -215,  -215,   401,   405,   403,
    -215,  -215,  -215,   527,   527,     3,  -215,   410,   308,   527,
    -215,   170,   170,   406,   397,   235,   409,  -215,   407,   415,
    -215,   229,   229,   229,   229,   190,  -215,   527,  -215,    68,
     111,  -215,  -215,  -215,   408,   421,   170,   170,  -215,  -215,
     170,   170,  -215,  -215,  -215,  -215,  -215,  -215,   392,  -215,
     170,   170,   431,   433,   434,   445,   426,   446,   450,   170,
     170,   170,   170,  -215,   170,   170,   428,   452,   455,   453,
     429,   464,   460,  -215,   170,  -215,   170,   477,  -215,   170,
     472,   476,   527,   478,   170,   170,  -215,   170,   480,   485,
     488,  -215,  -215,  -215
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -215,  -215,  -215,   491,  -215,  -215,  -215,  -215,   474,  -215,
    -215,   104,   -37,  -215,  -215,  -215,  -214,  -215,  -215,  -215,
    -215,  -215,    60,   -67,  -101,  -215,  -215,   -66,  -215,  -215,
    -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,  -215,
    -215,   268,  -215,   341,   391,  -120,  -215,  -215,   456,  -215,
     418,  -215
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -127
static const short int yytable[] =
{
     115,   184,    69,   156,   197,   120,    14,    96,  -125,    48,
      69,    97,   265,   138,   129,    98,   130,   139,    23,   268,
      52,    99,   100,   101,    53,   102,    16,    85,   103,   104,
     105,   106,   107,   176,    90,   108,   109,  -125,  -125,  -125,
      69,    88,    69,    49,   110,   167,   168,   169,    91,   289,
     290,   209,   134,    92,    24,   120,   135,   120,   134,    15,
      24,   209,   137,    86,   285,    91,   184,   177,    20,   197,
      92,   239,    96,  -124,    58,   195,    97,    86,   199,   207,
      98,   210,   208,   205,    59,    17,    99,   100,   101,   245,
     102,   210,   136,   103,   104,   105,   106,   107,   136,   131,
     108,   109,  -124,  -124,  -124,   211,   248,   249,   212,   110,
     251,   252,   197,   254,   256,    96,  -126,   131,    22,    97,
     120,   271,    21,    98,   214,    47,   238,    64,   215,    99,
     100,   101,    51,   102,    55,    65,   103,   104,   105,   106,
     107,    56,    72,   108,   109,  -126,  -126,  -126,    73,   131,
      66,    74,   110,   180,   181,   260,   261,   262,    29,    77,
      79,   170,    30,    12,    27,    31,   141,    57,    13,    28,
     270,   142,   273,   274,    32,    60,    80,   227,   278,   279,
     302,   303,   304,   305,   152,    61,    62,    33,    34,   153,
     171,   172,   173,    63,    81,    35,    36,    37,    38,   199,
      71,    82,   199,   293,   294,   295,   171,   172,   173,    29,
     123,    83,   192,    46,   255,     1,    31,   193,   128,   153,
       2,   309,   132,   199,   199,    32,   306,     3,     4,   312,
     313,   307,   124,   314,   315,     5,   125,   230,    33,    34,
     231,   131,   298,   317,   318,   231,    35,    36,    37,    38,
     133,   148,   326,   327,   328,   329,   149,   331,   332,   163,
     164,   143,   165,   244,   146,   150,   247,   340,   151,   341,
     153,   158,   343,   159,   160,   161,   346,   348,   349,   197,
     350,   162,    96,   198,   182,    67,    97,   187,   188,    68,
      98,   191,    31,   189,   190,   194,    99,   100,   101,   196,
     102,    32,   200,   103,   104,   105,   106,   107,   201,   197,
     108,   109,    96,   292,    33,    34,    97,   204,   213,   110,
      98,   216,    35,    36,    37,    38,    99,   100,   101,   220,
     102,   224,   221,   103,   104,   105,   106,   107,    67,   222,
     108,   109,    70,    29,   225,    31,   223,    76,   229,   110,
      31,   226,   233,   235,    32,   236,   237,   240,    29,    32,
     241,   243,    78,   177,   281,    31,   246,    33,    34,   250,
     253,   257,    33,    34,    32,    35,    36,    37,    38,   258,
      35,    36,    37,    38,   259,    67,   263,    33,    34,   126,
     264,   266,    31,   267,   269,    35,    36,    37,    38,   272,
      67,    32,   276,   277,   127,   280,   282,    31,   297,   286,
     283,   288,   287,   299,    33,    34,    32,   284,   291,   296,
     300,   310,    35,    36,    37,    38,   301,    96,   118,    33,
      34,    97,   311,   316,   323,    98,   333,    35,    36,    37,
      38,    99,   100,   101,   319,   102,   320,   321,   103,   104,
     105,   106,   107,    96,   183,   108,   109,    97,   322,   324,
     119,    98,   335,   325,   110,   334,   336,    99,   100,   101,
     337,   102,   338,   339,   103,   104,   105,   106,   107,    96,
     185,   108,   109,    97,   342,   344,   119,    98,   351,   345,
     110,   347,   352,    99,   100,   101,   353,   102,    19,    54,
     103,   104,   105,   106,   107,    96,   234,   108,   109,    97,
     275,   140,   119,    98,   186,   228,   110,     0,    89,    99,
     100,   101,     0,   102,     0,     0,   103,   104,   105,   106,
     107,    96,     0,   108,   109,    97,     0,     0,   119,    98,
       0,     0,   110,     0,     0,    99,   100,   101,     0,   102,
       0,     0,   103,   104,   105,   106,   107,     0,     0,   108,
     109,    -2,    18,     0,     1,     0,     0,     0,   110,     2,
       0,     0,     0,     0,     0,     0,     3,     4,     0,     0,
       0,     0,     0,     0,     5
};

static const short int yycheck[] =
{
      66,   121,    39,   104,     1,    71,    41,     4,     5,     7,
      47,     8,   226,     1,    81,    12,    82,     5,     5,   233,
       1,    18,    19,    20,     5,    22,     3,     5,    25,    26,
      27,    28,    29,     7,     5,    32,    33,    34,    35,    36,
      77,     5,    79,    41,    41,   111,   112,   113,    36,   263,
     264,    11,     1,    41,    41,   121,     5,   123,     1,     4,
      41,    11,     5,    41,    14,    36,   186,    41,     4,     1,
      41,    13,     4,     5,    31,   142,     8,    41,   144,    10,
      12,    41,    13,   150,    41,     0,    18,    19,    20,    13,
      22,    41,    41,    25,    26,    27,    28,    29,    41,    41,
      32,    33,    34,    35,    36,    10,   207,   208,    13,    41,
     211,   212,     1,   214,   215,     4,     5,    41,     6,     8,
     186,    13,     4,    12,    10,    21,   193,     1,    14,    18,
      19,    20,     9,    22,     4,     9,    25,    26,    27,    28,
      29,     4,     1,    32,    33,    34,    35,    36,     7,    41,
      24,    10,    41,     8,     9,   221,   222,   223,     1,    55,
      56,     5,     5,    36,    36,     8,     8,    24,    41,    41,
     236,    13,   239,   240,    17,     6,    41,     5,   245,   246,
     281,   282,   283,   284,    36,     4,     4,    30,    31,    41,
      34,    35,    36,     4,     6,    38,    39,    40,    41,   265,
       4,    24,   268,   269,   271,   272,    34,    35,    36,     1,
       4,    41,     8,     5,    36,     3,     8,    13,     8,    41,
       8,   287,     7,   289,   290,    17,    36,    15,    16,   296,
     297,    41,    41,   300,   301,    23,    41,     7,    30,    31,
      10,    41,     7,   310,   311,    10,    38,    39,    40,    41,
       8,     6,   319,   320,   321,   322,     6,   324,   325,     8,
       9,    41,    11,   203,    41,     6,   206,   334,     6,   336,
      41,     8,   339,     8,     8,     6,   342,   344,   345,     1,
     347,     6,     4,     5,    41,     1,     8,     8,     7,     5,
      12,     8,     8,    41,    41,    41,    18,    19,    20,     8,
      22,    17,     6,    25,    26,    27,    28,    29,     8,     1,
      32,    33,     4,     5,    30,    31,     8,    41,     8,    41,
      12,     8,    38,    39,    40,    41,    18,    19,    20,     6,
      22,    41,    21,    25,    26,    27,    28,    29,     1,    21,
      32,    33,     5,     1,    41,     8,    21,     5,    41,    41,
       8,    11,     4,    41,    17,    24,    41,    11,     1,    17,
       7,    41,     5,    41,    10,     8,    11,    30,    31,    41,
      41,    41,    30,    31,    17,    38,    39,    40,    41,    41,
      38,    39,    40,    41,    41,     1,    11,    30,    31,     5,
      11,    41,     8,    41,    24,    38,    39,    40,    41,    11,
       1,    17,     7,     7,     5,     7,    13,     8,    11,     8,
      10,     8,     7,     4,    30,    31,    17,    13,     8,    13,
      13,    13,    38,    39,    40,    41,    11,     4,     5,    30,
      31,     8,    11,    41,     8,    12,     8,    38,    39,    40,
      41,    18,    19,    20,    13,    22,    13,    13,    25,    26,
      27,    28,    29,     4,     5,    32,    33,     8,    13,    13,
      37,    12,     7,    13,    41,    13,    13,    18,    19,    20,
      41,    22,     8,    13,    25,    26,    27,    28,    29,     4,
       5,    32,    33,     8,     7,    13,    37,    12,     8,    13,
      41,    13,     7,    18,    19,    20,     8,    22,     7,    25,
      25,    26,    27,    28,    29,     4,     5,    32,    33,     8,
     242,    93,    37,    12,   123,   174,    41,    -1,    62,    18,
      19,    20,    -1,    22,    -1,    -1,    25,    26,    27,    28,
      29,     4,    -1,    32,    33,     8,    -1,    -1,    37,    12,
      -1,    -1,    41,    -1,    -1,    18,    19,    20,    -1,    22,
      -1,    -1,    25,    26,    27,    28,    29,    -1,    -1,    32,
      33,     0,     1,    -1,     3,    -1,    -1,    -1,    41,     8,
      -1,    -1,    -1,    -1,    -1,    -1,    15,    16,    -1,    -1,
      -1,    -1,    -1,    -1,    23
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     3,     8,    15,    16,    23,    43,    44,    45,    46,
      47,    48,    36,    41,    41,     4,     3,     0,     1,    45,
       4,     4,     6,     5,    41,    49,    50,    36,    41,     1,
       5,     8,    17,    30,    31,    38,    39,    40,    41,    53,
      54,    56,    57,    88,    89,    93,     5,    53,     7,    41,
      52,     9,     1,     5,    50,     4,     4,    24,    31,    41,
       6,     4,     4,     4,     1,     9,    24,     1,     5,    54,
       5,     4,     1,     7,    10,    51,     5,    53,     5,    53,
      41,     6,    24,    41,    65,     5,    41,    90,     5,    90,
       5,    36,    41,    91,    92,    55,     4,     8,    12,    18,
      19,    20,    22,    25,    26,    27,    28,    29,    32,    33,
      41,    59,    61,    63,    67,    69,    80,    82,     5,    37,
      69,    86,    87,     4,    41,    41,     5,     5,     8,    65,
      69,    41,     7,     8,     1,     5,    41,     5,     1,     5,
      92,     8,    13,    41,    58,    69,    41,    78,     6,     6,
       6,     6,    36,    41,    66,    76,    66,    77,     8,     8,
       8,     6,     6,     8,     9,    11,    81,    69,    69,    69,
       5,    34,    35,    36,    84,    85,     7,    41,    64,    83,
       8,     9,    41,     5,    87,     5,    86,     8,     7,    41,
      41,     8,     8,    13,    41,    65,     8,     1,     5,    69,
       6,     8,    68,    60,    41,    65,    62,    10,    13,    11,
      41,    10,    13,     8,    10,    14,     8,    71,    74,    70,
       6,    21,    21,    21,    41,    41,    11,     5,    85,    41,
       7,    10,    75,     4,     5,    41,    24,    41,    65,    13,
      11,     7,    79,    41,    64,    13,    11,    64,    66,    66,
      41,    66,    66,    41,    66,    36,    66,    41,    41,    41,
      69,    69,    69,    11,    11,    58,    41,    41,    58,    24,
      69,    13,    11,    65,    65,    83,     7,     7,    65,    65,
       7,    10,    13,    10,    13,    14,     8,     7,     8,    58,
      58,     8,     5,    69,    65,    65,    13,    11,     7,     4,
      13,    11,    66,    66,    66,    66,    36,    41,    72,    69,
      13,    11,    65,    65,    65,    65,    41,    65,    65,    13,
      13,    13,    13,     8,    13,    13,    65,    65,    65,    65,
      73,    65,    65,     8,    13,     7,    13,    41,     8,    13,
      65,    65,     7,    65,    13,    13,    69,    13,    65,    65,
      65,     8,     7,     8
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
      YYPOPSTACK;						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (&yylloc, parseio, YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (0)


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (N)								\
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
    while (0)
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
} while (0)

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)		\
do {								\
  if (yydebug)							\
    {								\
      YYFPRINTF (stderr, "%s ", Title);				\
      yysymprint (stderr,					\
                  Type, Value, Location);	\
      YYFPRINTF (stderr, "\n");					\
    }								\
} while (0)

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_stack_print (short int *bottom, short int *top)
#else
static void
yy_stack_print (bottom, top)
    short int *bottom;
    short int *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (/* Nothing. */; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yy_reduce_print (int yyrule)
#else
static void
yy_reduce_print (yyrule)
    int yyrule;
#endif
{
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu), ",
             yyrule - 1, yylno);
  /* Print the symbols being reduced, and their result.  */
  for (yyi = yyprhs[yyrule]; 0 <= yyrhs[yyi]; yyi++)
    YYFPRINTF (stderr, "%s ", yytname[yyrhs[yyi]]);
  YYFPRINTF (stderr, "-> %s\n", yytname[yyr1[yyrule]]);
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (Rule);		\
} while (0)

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
#  if defined (__GLIBC__) && defined (_STRING_H)
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
static YYSIZE_T
#   if defined (__STDC__) || defined (__cplusplus)
yystrlen (const char *yystr)
#   else
yystrlen (yystr)
     const char *yystr;
#   endif
{
  const char *yys = yystr;

  while (*yys++ != '\0')
    continue;

  return yys - yystr - 1;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined (__GLIBC__) && defined (_STRING_H) && defined (_GNU_SOURCE)
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
static char *
#   if defined (__STDC__) || defined (__cplusplus)
yystpcpy (char *yydest, const char *yysrc)
#   else
yystpcpy (yydest, yysrc)
     char *yydest;
     const char *yysrc;
#   endif
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

#endif /* YYERROR_VERBOSE */



#if YYDEBUG
/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
static void
yysymprint (FILE *yyoutput, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yysymprint (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;
  (void) yylocationp;

  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  YY_LOCATION_PRINT (yyoutput, *yylocationp);
  YYFPRINTF (yyoutput, ": ");

# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# endif
  switch (yytype)
    {
      default:
        break;
    }
  YYFPRINTF (yyoutput, ")");
}

#endif /* ! YYDEBUG */
/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

#if defined (__STDC__) || defined (__cplusplus)
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
  /* Pacify ``unused variable'' warnings.  */
  (void) yyvaluep;
  (void) yylocationp;

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {
      case 41: /* "word" */
#line 155 "ael.y"
        { free((yyvaluep->str));};
#line 1319 "ael.tab.c"
        break;
      case 44: /* "objects" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1331 "ael.tab.c"
        break;
      case 45: /* "object" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1343 "ael.tab.c"
        break;
      case 46: /* "context" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1355 "ael.tab.c"
        break;
      case 47: /* "macro" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1367 "ael.tab.c"
        break;
      case 48: /* "globals" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1379 "ael.tab.c"
        break;
      case 49: /* "global_statements" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1391 "ael.tab.c"
        break;
      case 50: /* "global_statement" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1403 "ael.tab.c"
        break;
      case 52: /* "arglist" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1415 "ael.tab.c"
        break;
      case 53: /* "elements" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1427 "ael.tab.c"
        break;
      case 54: /* "element" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1439 "ael.tab.c"
        break;
      case 56: /* "ignorepat" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1451 "ael.tab.c"
        break;
      case 57: /* "extension" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1463 "ael.tab.c"
        break;
      case 58: /* "statements" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1475 "ael.tab.c"
        break;
      case 59: /* "if_head" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1487 "ael.tab.c"
        break;
      case 61: /* "random_head" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1499 "ael.tab.c"
        break;
      case 63: /* "iftime_head" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1511 "ael.tab.c"
        break;
      case 64: /* "word_list" */
#line 155 "ael.y"
        { free((yyvaluep->str));};
#line 1516 "ael.tab.c"
        break;
      case 65: /* "word3_list" */
#line 155 "ael.y"
        { free((yyvaluep->str));};
#line 1521 "ael.tab.c"
        break;
      case 66: /* "goto_word" */
#line 155 "ael.y"
        { free((yyvaluep->str));};
#line 1526 "ael.tab.c"
        break;
      case 67: /* "switch_head" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1538 "ael.tab.c"
        break;
      case 69: /* "statement" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1550 "ael.tab.c"
        break;
      case 76: /* "target" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1562 "ael.tab.c"
        break;
      case 77: /* "jumptarget" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1574 "ael.tab.c"
        break;
      case 78: /* "macro_call" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1586 "ael.tab.c"
        break;
      case 80: /* "application_call_head" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1598 "ael.tab.c"
        break;
      case 82: /* "application_call" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1610 "ael.tab.c"
        break;
      case 83: /* "eval_arglist" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1622 "ael.tab.c"
        break;
      case 84: /* "case_statements" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1634 "ael.tab.c"
        break;
      case 85: /* "case_statement" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1646 "ael.tab.c"
        break;
      case 86: /* "macro_statements" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1658 "ael.tab.c"
        break;
      case 87: /* "macro_statement" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1670 "ael.tab.c"
        break;
      case 88: /* "switches" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1682 "ael.tab.c"
        break;
      case 89: /* "eswitches" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1694 "ael.tab.c"
        break;
      case 90: /* "switchlist" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1706 "ael.tab.c"
        break;
      case 91: /* "includeslist" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1718 "ael.tab.c"
        break;
      case 92: /* "includedname" */
#line 155 "ael.y"
        { free((yyvaluep->str));};
#line 1723 "ael.tab.c"
        break;
      case 93: /* "includes" */
#line 140 "ael.y"
        {
		if (yymsg[0] != 'C') {
			destroy_pval((yyvaluep->pval));
			prev_word=0;
		} else {
			printf("Cleanup destructor called for pvals\n");
		}
	};
#line 1735 "ael.tab.c"
        break;

      default:
        break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM);
# else
int yyparse ();
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
int yyparse (struct parse_io *parseio);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */






/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
# if defined (__STDC__) || defined (__cplusplus)
int yyparse (void *YYPARSE_PARAM)
# else
int yyparse (YYPARSE_PARAM)
  void *YYPARSE_PARAM;
# endif
#else /* ! YYPARSE_PARAM */
#if defined (__STDC__) || defined (__cplusplus)
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

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  short int yyssa[YYINITDEPTH];
  short int *yyss = yyssa;
  short int *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;

  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
  /* The locations where the error started and ended. */
  YYLTYPE yyerror_range[2];

#define YYPOPSTACK   (yyvsp--, yyssp--, yylsp--)

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* When reducing, the number of symbols on the RHS of the reduced
     rule.  */
  int yylen;

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
     have just been pushed. so pushing a state here evens the stacks.
     */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack. Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	short int *yyss1 = yyss;
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
	short int *yyss1 = yyss;
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

/* Do appropriate processing given the current state.  */
/* Read a look-ahead token if we need one and don't already have one.  */
/* yyresume: */

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

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the token being shifted unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  *++yyvsp = yylval;
  *++yylsp = yylloc;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  yystate = yyn;
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

  /* Default location. */
  YYLLOC_DEFAULT (yyloc, yylsp - yylen, yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 160 "ael.y"
    { (yyval.pval) = parseio->pval = (yyvsp[0].pval); ;}
    break;

  case 3:
#line 163 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 4:
#line 165 "ael.y"
    {
			if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {
				(yyval.pval)=(yyvsp[-1].pval);
				linku1((yyval.pval),(yyvsp[0].pval));
			} else if ( (yyvsp[-1].pval) ) {
				(yyval.pval)=(yyvsp[-1].pval);
			} else if ( (yyvsp[0].pval) ) {
				(yyval.pval)=(yyvsp[0].pval);
			}
		;}
    break;

  case 5:
#line 175 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 6:
#line 178 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 7:
#line 179 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 8:
#line 180 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 9:
#line 181 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 10:
#line 184 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval); ;}
    break;

  case 11:
#line 188 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 12:
#line 191 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->u2.statements = (yyvsp[-1].pval); ;}
    break;

  case 13:
#line 195 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = strdup("default"); ;}
    break;

  case 14:
#line 198 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.abstract = 1; ;}
    break;

  case 15:
#line 203 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u3.abstract = 1; ;}
    break;

  case 16:
#line 207 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.abstract = 1; ;}
    break;

  case 17:
#line 212 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->u3.abstract = 1; ;}
    break;

  case 18:
#line 218 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-6].str); (yyval.pval)->u2.arglist = (yyvsp[-4].pval); (yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 19:
#line 221 "ael.y"
    {
		(yyval.pval)=npval(PV_MACRO,(yylsp[-6]).first_line,(yylsp[0]).last_line, (yylsp[-6]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-5].str); (yyval.pval)->u2.arglist = (yyvsp[-3].pval); ;}
    break;

  case 20:
#line 224 "ael.y"
    {
		(yyval.pval)=npval(PV_MACRO,(yylsp[-6]).first_line,(yylsp[0]).last_line, (yylsp[-6]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-5].str); (yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 21:
#line 227 "ael.y"
    {
		(yyval.pval)=npval(PV_MACRO,(yylsp[-5]).first_line,(yylsp[0]).last_line, (yylsp[-5]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-4].str); /* pretty empty! */ ;}
    break;

  case 22:
#line 232 "ael.y"
    {
		(yyval.pval)=npval(PV_GLOBALS,(yylsp[-3]).first_line,(yylsp[0]).last_line, (yylsp[-3]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.statements = (yyvsp[-1].pval);;}
    break;

  case 23:
#line 235 "ael.y"
    {
		(yyval.pval)=npval(PV_GLOBALS,(yylsp[-2]).first_line,(yylsp[0]).last_line, (yylsp[-2]).first_column, (yylsp[0]).last_column);
		/* and that's all */ ;}
    break;

  case 24:
#line 240 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 25:
#line 241 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));;}
    break;

  case 26:
#line 242 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 27:
#line 245 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 28:
#line 245 "ael.y"
    {
		(yyval.pval)=npval(PV_VARDEC,(yylsp[-4]).first_line,(yylsp[0]).last_line, (yylsp[-4]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 29:
#line 251 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[0].str); ;}
    break;

  case 30:
#line 254 "ael.y"
    {
		pval *z = npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[0]).last_line, (yylsp[-2]).first_column, (yylsp[0]).last_column);
		z->u1.str = (yyvsp[0].str);
		(yyval.pval)=(yyvsp[-2].pval);
		linku1((yyval.pval),z); ;}
    break;

  case 31:
#line 259 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 32:
#line 262 "ael.y"
    { (yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 33:
#line 263 "ael.y"
    {(yyval.pval)=0;;}
    break;

  case 34:
#line 264 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
				else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
				else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 35:
#line 267 "ael.y"
    { (yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 36:
#line 270 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 37:
#line 271 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 38:
#line 272 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 39:
#line 273 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 40:
#line 274 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 41:
#line 275 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 42:
#line 275 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 43:
#line 279 "ael.y"
    {free((yyvsp[-1].str)); (yyval.pval)=0;;}
    break;

  case 44:
#line 280 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 45:
#line 283 "ael.y"
    {
		(yyval.pval) = npval2(PV_IGNOREPAT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 46:
#line 288 "ael.y"
    {
		(yyval.pval) = npval(PV_EXTENSION,(yylsp[-2]).first_line,(yylsp[0]).last_line, (yylsp[-2]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 47:
#line 292 "ael.y"
    {
		(yyval.pval) = npval(PV_EXTENSION,(yylsp[-3]).first_line,(yylsp[0]).last_line, (yylsp[-3]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;;}
    break;

  case 48:
#line 297 "ael.y"
    {
		(yyval.pval) = npval(PV_EXTENSION,(yylsp[-6]).first_line,(yylsp[0]).last_line, (yylsp[-6]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 49:
#line 302 "ael.y"
    {
		(yyval.pval) = npval(PV_EXTENSION,(yylsp[-7]).first_line,(yylsp[0]).last_line, (yylsp[-7]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 50:
#line 311 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 51:
#line 312 "ael.y"
    {if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 52:
#line 315 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 53:
#line 318 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 54:
#line 318 "ael.y"
    {
		(yyval.pval)= npval(PV_IF,(yylsp[-4]).first_line,(yylsp[0]).last_line, (yylsp[-4]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 55:
#line 323 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 56:
#line 323 "ael.y"
    {
		(yyval.pval)= npval(PV_RANDOM,(yylsp[-4]).first_line,(yylsp[0]).last_line, (yylsp[-4]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str=(yyvsp[-1].str);;}
    break;

  case 57:
#line 329 "ael.y"
    {
		(yyval.pval) = npval2(PV_IFTIME, &(yylsp[-13]), &(yylsp[-9])); /* XXX really @5 or more ? */
		(yyval.pval)->u1.list = npval2(PV_WORD, &(yylsp[-11]), &(yylsp[-11]));
		asprintf(&((yyval.pval)->u1.list->u1.str), "%s:%s:%s", (yyvsp[-11].str), (yyvsp[-9].str), (yyvsp[-7].str));
		free((yyvsp[-11].str));
		free((yyvsp[-9].str));
		free((yyvsp[-7].str));
		(yyval.pval)->u1.list->next = npval2(PV_WORD, &(yylsp[-5]), &(yylsp[-5]));
		(yyval.pval)->u1.list->next->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u1.list->next->next = npval2(PV_WORD, &(yylsp[-3]), &(yylsp[-3]));
		(yyval.pval)->u1.list->next->next->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u1.list->next->next->next = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[-1]));
		(yyval.pval)->u1.list->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word = 0;
	;}
    break;

  case 58:
#line 344 "ael.y"
    {
		(yyval.pval) = npval2(PV_IFTIME, &(yylsp[-9]), &(yylsp[-5])); /* XXX @5 or greater ? */
		(yyval.pval)->u1.list = npval2(PV_WORD, &(yylsp[-7]), &(yylsp[-7]));
		(yyval.pval)->u1.list->u1.str = (yyvsp[-7].str);
		(yyval.pval)->u1.list->next = npval2(PV_WORD, &(yylsp[-5]), &(yylsp[-5]));
		(yyval.pval)->u1.list->next->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u1.list->next->next = npval2(PV_WORD, &(yylsp[-3]), &(yylsp[-3]));
		(yyval.pval)->u1.list->next->next->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u1.list->next->next->next = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[-1]));
		(yyval.pval)->u1.list->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word = 0;
	;}
    break;

  case 59:
#line 364 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 60:
#line 365 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 61:
#line 372 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 62:
#line 373 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 63:
#line 378 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s%s", (yyvsp[-2].str), (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word=(yyval.str);;}
    break;

  case 64:
#line 386 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 65:
#line 387 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));;}
    break;

  case 66:
#line 391 "ael.y"
    {
		asprintf(&((yyval.str)), "%s:%s", (yyvsp[-2].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[0].str));;}
    break;

  case 67:
#line 397 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 68:
#line 397 "ael.y"
    {
		(yyval.pval)=npval(PV_SWITCH,(yylsp[-5]).first_line,(yylsp[0]).last_line, (yylsp[-5]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 69:
#line 405 "ael.y"
    {
		(yyval.pval) = npval2(PV_STATEMENTBLOCK, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 70:
#line 408 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 71:
#line 408 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 72:
#line 412 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 73:
#line 415 "ael.y"
    {
		(yyval.pval)=npval(PV_GOTO,(yylsp[-2]).first_line,(yylsp[0]).last_line, (yylsp[-2]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 74:
#line 418 "ael.y"
    {
		(yyval.pval)=npval(PV_LABEL,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
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
    {
		(yyval.pval)=npval(PV_FOR,(yylsp[-11]).first_line,(yylsp[0]).last_line, (yylsp[-11]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.for_init = (yyvsp[-8].str);
		(yyval.pval)->u2.for_test=(yyvsp[-5].str);
		(yyval.pval)->u3.for_inc = (yyvsp[-2].str);
		(yyval.pval)->u4.for_statements = (yyvsp[0].pval);;}
    break;

  case 79:
#line 429 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 80:
#line 429 "ael.y"
    {
		(yyval.pval)=npval(PV_WHILE,(yylsp[-5]).first_line,(yylsp[0]).last_line, (yylsp[-5]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 81:
#line 433 "ael.y"
    {
		(yyval.pval)=(yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 82:
#line 437 "ael.y"
    {
		(yyval.pval)=(yyvsp[-2].pval);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 83:
#line 442 "ael.y"
    {
		(yyval.pval) = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[-1]).last_line;
		(yyval.pval)->endcol = (yylsp[-1]).last_column;;}
    break;

  case 84:
#line 446 "ael.y"
    {
		(yyval.pval) = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 85:
#line 450 "ael.y"
    {
		(yyval.pval)= npval(PV_APPLICATION_CALL,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 86:
#line 453 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 87:
#line 453 "ael.y"
    {
		char *bufx;
		int tot=0;
		pval *pptr;
		(yyval.pval) = npval(PV_VARDEC,(yylsp[-4]).first_line,(yylsp[0]).last_line, (yylsp[-4]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u2.val=(yyvsp[-1].str);
		/* rebuild the original string-- this is not an app call, it's an unwrapped vardec, with a func call on the LHS */
		/* string to big to fit in the buffer? */
		tot+=strlen((yyvsp[-4].pval)->u1.str);
		for(pptr=(yyvsp[-4].pval)->u2.arglist;pptr;pptr=pptr->next) {
			tot+=strlen(pptr->u1.str);
			tot++; /* for a sep like a comma */
		}
		tot+=4; /* for safety */
		bufx = ast_calloc(1, tot);
		strcpy(bufx,(yyvsp[-4].pval)->u1.str);
		strcat(bufx,"(");
		/* XXX need to advance the pointer or the loop is very inefficient */
		for (pptr=(yyvsp[-4].pval)->u2.arglist;pptr;pptr=pptr->next) {
			if ( pptr != (yyvsp[-4].pval)->u2.arglist )
				strcat(bufx,",");
			strcat(bufx,pptr->u1.str);
		}
		strcat(bufx,")");
#ifdef AAL_ARGCHECK
		if ( !ael_is_funcname((yyvsp[-4].pval)->u1.str) )
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Function call? The name %s is not in my internal list of function names\n",
				my_file, (yylsp[-4]).first_line, (yylsp[-4]).first_column, (yylsp[-4]).last_column, (yyvsp[-4].pval)->u1.str);
#endif
		(yyval.pval)->u1.str = bufx;
		destroy_pval((yyvsp[-4].pval)); /* the app call it is not, get rid of that chain */
		prev_word = 0;
	;}
    break;

  case 88:
#line 486 "ael.y"
    {
		(yyval.pval) = npval(PV_BREAK,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column);;}
    break;

  case 89:
#line 488 "ael.y"
    {
		(yyval.pval) = npval(PV_RETURN,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column);;}
    break;

  case 90:
#line 490 "ael.y"
    {
		(yyval.pval) = npval(PV_CONTINUE,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column);;}
    break;

  case 91:
#line 492 "ael.y"
    {
		(yyval.pval)=(yyvsp[-1].pval);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 92:
#line 497 "ael.y"
    {
		(yyval.pval)=(yyvsp[-3].pval);
		(yyval.pval)->u2.statements = (yyvsp[-2].pval);
		(yyval.pval)->endline = (yylsp[-2]).last_line;
		(yyval.pval)->endcol = (yylsp[-2]).last_column;
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 93:
#line 503 "ael.y"
    {
		(yyval.pval)=(yyvsp[-1].pval);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 94:
#line 508 "ael.y"
    {
		(yyval.pval)=(yyvsp[-3].pval);
		(yyval.pval)->u2.statements = (yyvsp[-2].pval);
		(yyval.pval)->endline = (yylsp[-2]).last_line;
		(yyval.pval)->endcol = (yylsp[-2]).last_column;
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 95:
#line 514 "ael.y"
    {
		(yyval.pval)=(yyvsp[-1].pval);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 96:
#line 519 "ael.y"
    {
		(yyval.pval)=(yyvsp[-3].pval);
		(yyval.pval)->u2.statements = (yyvsp[-2].pval);
		(yyval.pval)->endline = (yylsp[-2]).last_line;
		(yyval.pval)->endcol = (yylsp[-2]).last_column;
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 97:
#line 525 "ael.y"
    { (yyval.pval)=0; ;}
    break;

  case 98:
#line 528 "ael.y"
    { (yyval.pval) = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column); (yyval.pval)->u1.str = (yyvsp[0].str);;}
    break;

  case 99:
#line 529 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 100:
#line 534 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 101:
#line 539 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-4]).first_line,(yylsp[-4]).last_line, (yylsp[-4]).first_column, (yylsp[-4]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next = npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 102:
#line 546 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-4]).first_line,(yylsp[-4]).last_line, (yylsp[-4]).first_column, (yylsp[-4]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next = npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 103:
#line 553 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-4]).first_line,(yylsp[-4]).last_line, (yylsp[-4]).first_column, (yylsp[-4]).last_column);
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 104:
#line 560 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-4]).first_line,(yylsp[-4]).last_line, (yylsp[-4]).first_column, (yylsp[-4]).last_column);
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 105:
#line 569 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->u1.str = strdup("1");;}
    break;

  case 106:
#line 574 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 107:
#line 579 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-4]).first_line,(yylsp[-4]).last_line, (yylsp[-4]).first_column, (yylsp[-4]).last_column);
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->next->u1.str = (yyvsp[-2].str); ;}
    break;

  case 108:
#line 586 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->next->u1.str = strdup("1"); ;}
    break;

  case 109:
#line 593 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-4]).first_line,(yylsp[-4]).last_line, (yylsp[-4]).first_column, (yylsp[-4]).last_column);
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->next->u1.str = (yyvsp[-2].str); ;}
    break;

  case 110:
#line 600 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-2]).first_line,(yylsp[-2]).last_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column);
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->next->next->u1.str = strdup("1"); ;}
    break;

  case 111:
#line 609 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 112:
#line 609 "ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		(yyval.pval) = npval2(PV_MACRO_CALL, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.arglist = (yyvsp[-1].pval);;}
    break;

  case 113:
#line 614 "ael.y"
    {
		(yyval.pval)= npval2(PV_MACRO_CALL, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 114:
#line 619 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 115:
#line 619 "ael.y"
    {
		if (strcasecmp((yyvsp[-2].str),"goto") == 0) {
			(yyval.pval)= npval(PV_GOTO,(yylsp[-2]).first_line,(yylsp[0]).last_line, (yylsp[-2]).first_column, (yylsp[0]).last_column);
			free((yyvsp[-2].str)); /* won't be using this */
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Suggestion: Use the goto statement instead of the Goto() application call in AEL.\n", my_file, (yylsp[-2]).first_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column );
		} else
			(yyval.pval)= npval(PV_APPLICATION_CALL,(yylsp[-2]).first_line,(yylsp[0]).last_line, (yylsp[-2]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 116:
#line 629 "ael.y"
    {(yyval.pval) = (yyvsp[-2].pval);
 		if( (yyval.pval)->type == PV_GOTO )
			(yyval.pval)->u1.list = (yyvsp[-1].pval);
	 	else
			(yyval.pval)->u2.arglist = (yyvsp[-1].pval);
 		(yyval.pval)->endline = (yylsp[0]).last_line; (yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 117:
#line 635 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);(yyval.pval)->endline = (yylsp[0]).last_line; (yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 118:
#line 638 "ael.y"
    { 
		(yyval.pval)= npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval)->u1.str = (yyvsp[0].str);;}
    break;

  case 119:
#line 641 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		(yyval.pval)->u1.str = strdup(""); ;}
    break;

  case 120:
#line 644 "ael.y"
    {
		pval *z = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval) = (yyvsp[-2].pval);
		linku1((yyvsp[-2].pval),z);
		z->u1.str = (yyvsp[0].str);;}
    break;

  case 121:
#line 649 "ael.y"
    {
		pval *z = npval(PV_WORD,(yylsp[0]).first_line,(yylsp[0]).last_line, (yylsp[0]).first_column, (yylsp[0]).last_column);
		(yyval.pval) = (yyvsp[-1].pval);
		linku1((yyvsp[-1].pval),z);
		z->u1.str = strdup("");;}
    break;

  case 122:
#line 656 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 123:
#line 657 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 124:
#line 662 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-3]), &(yylsp[-1])); /* XXX 3 or 4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 125:
#line 666 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 126:
#line 670 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-3]), &(yylsp[0])); /* XXX@3 or @4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 127:
#line 674 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 128:
#line 677 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;;}
    break;

  case 129:
#line 680 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 130:
#line 685 "ael.y"
    {(yyval.pval) = (yyvsp[0].pval);;}
    break;

  case 131:
#line 686 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 132:
#line 691 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 133:
#line 692 "ael.y"
    {(yyval.pval)=npval(PV_CATCH,(yylsp[-4]).first_line,(yylsp[0]).last_line, (yylsp[-4]).first_column, (yylsp[0]).last_column); (yyval.pval)->u1.str = (yyvsp[-3].str); (yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 134:
#line 695 "ael.y"
    {(yyval.pval)= npval(PV_SWITCHES,(yylsp[-3]).first_line,(yylsp[0]).last_line, (yylsp[-3]).first_column, (yylsp[0]).last_column); (yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 135:
#line 696 "ael.y"
    {(yyval.pval)= npval(PV_SWITCHES,(yylsp[-2]).first_line,(yylsp[0]).last_line, (yylsp[-2]).first_column, (yylsp[0]).last_column);;}
    break;

  case 136:
#line 699 "ael.y"
    {(yyval.pval)= npval(PV_ESWITCHES,(yylsp[-3]).first_line,(yylsp[0]).last_line, (yylsp[-3]).first_column, (yylsp[0]).last_column); (yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 137:
#line 700 "ael.y"
    {(yyval.pval)= npval(PV_ESWITCHES,(yylsp[-2]).first_line,(yylsp[0]).last_line, (yylsp[-2]).first_column, (yylsp[0]).last_column); ;}
    break;

  case 138:
#line 703 "ael.y"
    {(yyval.pval)=npval(PV_WORD,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column); (yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 139:
#line 704 "ael.y"
    {pval *z = npval(PV_WORD,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column); (yyval.pval)=(yyvsp[-2].pval); z->u1.str = (yyvsp[-1].str); linku1((yyval.pval),z); ;}
    break;

  case 140:
#line 705 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 141:
#line 708 "ael.y"
    {(yyval.pval)=npval(PV_WORD,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column); (yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 142:
#line 709 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-13]).first_line,(yylsp[-12]).last_line, (yylsp[-13]).first_column, (yylsp[-12]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-13].str);
		(yyval.pval)->u2.arglist = npval(PV_WORD,(yylsp[-11]).first_line,(yylsp[-7]).last_line, (yylsp[-11]).first_column, (yylsp[-7]).last_column);
		asprintf( &((yyval.pval)->u2.arglist->u1.str), "%s:%s:%s", (yyvsp[-11].str), (yyvsp[-9].str), (yyvsp[-7].str));
		free((yyvsp[-11].str));
		free((yyvsp[-9].str));
		free((yyvsp[-7].str));
		(yyval.pval)->u2.arglist->next = npval(PV_WORD,(yylsp[-5]).first_line,(yylsp[-5]).last_line, (yylsp[-5]).first_column, (yylsp[-5]).last_column);
		(yyval.pval)->u2.arglist->next->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u2.arglist->next->next = npval(PV_WORD,(yylsp[-3]).first_line,(yylsp[-3]).last_line, (yylsp[-3]).first_column, (yylsp[-3]).last_column);
		(yyval.pval)->u2.arglist->next->next->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.arglist->next->next->next = npval(PV_WORD,(yylsp[-1]).first_line,(yylsp[-1]).last_line, (yylsp[-1]).first_column, (yylsp[-1]).last_column);
		(yyval.pval)->u2.arglist->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word=0;
	;}
    break;

  case 143:
#line 725 "ael.y"
    {
		(yyval.pval)=npval(PV_WORD,(yylsp[-9]).first_line,(yylsp[-8]).last_line, (yylsp[-9]).first_column, (yylsp[-8]).last_column);
		(yyval.pval)->u1.str = (yyvsp[-9].str);
		(yyval.pval)->u2.arglist = npval(PV_WORD,(yylsp[-7]).first_line,(yylsp[-7]).last_line, (yylsp[-7]).first_column, (yylsp[-7]).last_column);
		(yyval.pval)->u2.arglist->u1.str = (yyvsp[-7].str);
		(yyval.pval)->u2.arglist->next = npval(PV_WORD,(yylsp[-5]).first_line,(yylsp[-5]).last_line, (yylsp[-5]).first_column, (yylsp[-5]).last_column);
		(yyval.pval)->u2.arglist->next->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u2.arglist->next->next = npval(PV_WORD,(yylsp[-3]).first_line,(yylsp[-3]).last_line, (yylsp[-3]).first_column, (yylsp[-3]).last_column);
		(yyval.pval)->u2.arglist->next->next->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.arglist->next->next->next = npval(PV_WORD,(yylsp[-1]).first_line,(yylsp[-1]).last_line, (yylsp[-1]).first_column, (yylsp[-1]).last_column);
		(yyval.pval)->u2.arglist->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word=0;
	;}
    break;

  case 144:
#line 738 "ael.y"
    {pval *z = npval(PV_WORD,(yylsp[-1]).first_line,(yylsp[0]).last_line, (yylsp[-1]).first_column, (yylsp[0]).last_column); (yyval.pval)=(yyvsp[-2].pval); z->u1.str = (yyvsp[-1].str); linku1((yyval.pval),z); ;}
    break;

  case 145:
#line 739 "ael.y"
    {pval *z = npval(PV_WORD,(yylsp[-13]).first_line,(yylsp[-12]).last_line, (yylsp[-13]).first_column, (yylsp[-12]).last_column);
		(yyval.pval)=(yyvsp[-14].pval); z->u1.str = (yyvsp[-13].str); linku1((yyval.pval),z);
		z->u2.arglist = npval(PV_WORD,(yylsp[-11]).first_line,(yylsp[-11]).last_line, (yylsp[-11]).first_column, (yylsp[-11]).last_column);
		asprintf( &((yyval.pval)->u2.arglist->u1.str), "%s:%s:%s", (yyvsp[-11].str), (yyvsp[-9].str), (yyvsp[-7].str));
		free((yyvsp[-11].str));
		free((yyvsp[-9].str));
		free((yyvsp[-7].str));
		z->u2.arglist->next = npval(PV_WORD,(yylsp[-5]).first_line,(yylsp[-5]).last_line, (yylsp[-5]).first_column, (yylsp[-5]).last_column);
		z->u2.arglist->next->u1.str = (yyvsp[-5].str);
		z->u2.arglist->next->next = npval(PV_WORD,(yylsp[-3]).first_line,(yylsp[-3]).last_line, (yylsp[-3]).first_column, (yylsp[-3]).last_column);
		z->u2.arglist->next->next->u1.str = (yyvsp[-3].str);
		z->u2.arglist->next->next->next = npval(PV_WORD,(yylsp[-1]).first_line,(yylsp[-1]).last_line, (yylsp[-1]).first_column, (yylsp[-1]).last_column);
		z->u2.arglist->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word=0;
	;}
    break;

  case 146:
#line 755 "ael.y"
    {pval *z = npval(PV_WORD,(yylsp[-9]).first_line,(yylsp[-9]).last_line, (yylsp[-9]).first_column, (yylsp[-8]).last_column);
		(yyval.pval)=(yyvsp[-10].pval); z->u1.str = (yyvsp[-9].str); linku1((yyval.pval),z);
		z->u2.arglist = npval(PV_WORD,(yylsp[-7]).first_line,(yylsp[-7]).last_line, (yylsp[-7]).first_column, (yylsp[-7]).last_column);
		(yyval.pval)->u2.arglist->u1.str = (yyvsp[-7].str);
		z->u2.arglist->next = npval(PV_WORD,(yylsp[-5]).first_line,(yylsp[-5]).last_line, (yylsp[-5]).first_column, (yylsp[-5]).last_column);
		z->u2.arglist->next->u1.str = (yyvsp[-5].str);
		z->u2.arglist->next->next = npval(PV_WORD,(yylsp[-3]).first_line,(yylsp[-3]).last_line, (yylsp[-3]).first_column, (yylsp[-3]).last_column);
		z->u2.arglist->next->next->u1.str = (yyvsp[-3].str);
		z->u2.arglist->next->next->next = npval(PV_WORD,(yylsp[-1]).first_line,(yylsp[-1]).last_line, (yylsp[-1]).first_column, (yylsp[-1]).last_column);
		z->u2.arglist->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word=0;
	;}
    break;

  case 147:
#line 767 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 148:
#line 770 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 149:
#line 771 "ael.y"
    {(yyval.str)=strdup("default");;}
    break;

  case 150:
#line 774 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 151:
#line 777 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-2]), &(yylsp[0]));;}
    break;


      default: break;
    }

/* Line 1126 of yacc.c.  */
#line 3199 "ael.tab.c"

  yyvsp -= yylen;
  yyssp -= yylen;
  yylsp -= yylen;

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
#if YYERROR_VERBOSE
      yyn = yypact[yystate];

      if (YYPACT_NINF < yyn && yyn < YYLAST)
	{
	  int yytype = YYTRANSLATE (yychar);
	  YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
	  YYSIZE_T yysize = yysize0;
	  YYSIZE_T yysize1;
	  int yysize_overflow = 0;
	  char *yymsg = 0;
#	  define YYERROR_VERBOSE_ARGS_MAXIMUM 5
	  char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
	  int yyx;

#if 0
	  /* This is so xgettext sees the translatable formats that are
	     constructed on the fly.  */
	  YY_("syntax error, unexpected %s");
	  YY_("syntax error, unexpected %s, expecting %s");
	  YY_("syntax error, unexpected %s, expecting %s or %s");
	  YY_("syntax error, unexpected %s, expecting %s or %s or %s");
	  YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
#endif
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
		yysize_overflow |= yysize1 < yysize;
		yysize = yysize1;
		yyfmt = yystpcpy (yyfmt, yyprefix);
		yyprefix = yyor;
	      }

	  yyf = YY_(yyformat);
	  yysize1 = yysize + yystrlen (yyf);
	  yysize_overflow |= yysize1 < yysize;
	  yysize = yysize1;

	  if (!yysize_overflow && yysize <= YYSTACK_ALLOC_MAXIMUM)
	    yymsg = (char *) YYSTACK_ALLOC (yysize);
	  if (yymsg)
	    {
	      /* Avoid sprintf, as that infringes on the user's name space.
		 Don't have undefined behavior even if the translation
		 produced a string with the wrong number of "%s"s.  */
	      char *yyp = yymsg;
	      int yyi = 0;
	      while ((*yyp = *yyf))
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
	      yyerror (&yylloc, parseio, yymsg);
	      YYSTACK_FREE (yymsg);
	    }
	  else
	    {
	      yyerror (&yylloc, parseio, YY_("syntax error"));
	      goto yyexhaustedlab;
	    }
	}
      else
#endif /* YYERROR_VERBOSE */
	yyerror (&yylloc, parseio, YY_("syntax error"));
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
	  yydestruct ("Error: discarding", yytoken, &yylval, &yylloc);
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
  if (0)
     goto yyerrorlab;

  yyerror_range[0] = yylsp[1-yylen];
  yylsp -= yylen;
  yyvsp -= yylen;
  yyssp -= yylen;
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
      yydestruct ("Error: popping", yystos[yystate], yyvsp, yylsp);
      YYPOPSTACK;
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;

  yyerror_range[1] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the look-ahead.  YYLOC is available though. */
  YYLLOC_DEFAULT (yyloc, yyerror_range - 1, 2);
  *++yylsp = yyloc;

  /* Shift the error token. */
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
		 yytoken, &yylval, &yylloc);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp, yylsp);
      YYPOPSTACK;
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
  return yyresult;
}


#line 782 "ael.y"


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


static char *ael_token_subst(char *mess)
{
	/* calc a length, malloc, fill, and return; yyerror had better free it! */
	int len=0,i;
	char *p;
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
	res = ast_calloc(1, len+1);
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

static struct pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column)
{
	extern char *my_file;
	pval *z = ast_calloc(1, sizeof(struct pval));
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

/* append second element to the list in the first one */
static void linku1(pval *head, pval *tail)
{
	if (!head->next) {
		head->next = tail;
	} else {
		head->u1_last->next = tail;
	}
	head->u1_last = tail;
}


