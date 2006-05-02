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
#include "asterisk/ael_structs.h"

static pval * linku1(pval *head, pval *tail);

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
#line 48 "ael.y"
typedef union YYSTYPE {
	int	intval;		/* integer value, typically flags */
	char	*str;		/* strings */
	struct pval *pval;	/* full objects */
} YYSTYPE;
/* Line 196 of yacc.c.  */
#line 227 "ael.tab.c"
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
#line 54 "ael.y"

	/* declaring these AFTER the union makes things a lot simpler! */
void yyerror(YYLTYPE *locp, struct parse_io *parseio, char const *s);
int ael_yylex (YYSTYPE * yylval_param, YYLTYPE * yylloc_param , void * yyscanner);

/* create a new object with start-end marker */
static pval *npval(pvaltype type, int first_line, int last_line,
	int first_column, int last_column);

/* create a new object with start-end marker, simplified interface.
 * Must be declared here because YYLTYPE is not known before
 */
static pval *npval2(pvaltype type, YYLTYPE *first, YYLTYPE *last);

/* another frontend for npval, this time for a string */
static pval *nword(char *string, YYLTYPE *pos);

/* update end position of an object, return the object */
static pval *update_last(pval *, YYLTYPE *);


/* Line 219 of yacc.c.  */
#line 271 "ael.tab.c"

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
#define YYFINAL  14
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   390

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  56
/* YYNRULES -- Number of rules. */
#define YYNRULES  143
/* YYNRULES -- Number of states. */
#define YYNSTATES  274

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
      21,    23,    25,    30,    32,    33,    42,    47,    51,    53,
      56,    59,    60,    66,    67,    69,    73,    76,    79,    83,
      85,    87,    90,    93,    95,    97,    99,   101,   103,   104,
     110,   113,   115,   120,   124,   129,   137,   146,   148,   151,
     154,   160,   162,   170,   171,   176,   179,   182,   187,   189,
     192,   194,   197,   201,   203,   206,   210,   214,   218,   219,
     225,   229,   233,   236,   237,   238,   239,   252,   256,   259,
     263,   267,   270,   273,   274,   280,   283,   286,   289,   293,
     295,   298,   299,   301,   305,   309,   315,   321,   327,   333,
     335,   339,   345,   349,   355,   359,   360,   366,   370,   371,
     375,   379,   382,   384,   385,   387,   388,   392,   394,   397,
     402,   406,   411,   415,   418,   422,   423,   425,   428,   430,
     436,   439,   442,   446,   449,   452,   456,   459,   462,   467,
     469,   472,   475,   480
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      43,     0,    -1,    44,    -1,    45,    -1,    44,    45,    -1,
      44,     1,    -1,    47,    -1,    49,    -1,    50,    -1,     8,
      -1,    41,    -1,    36,    -1,    48,     3,    46,    55,    -1,
      23,    -1,    -1,    15,    41,     6,    54,     7,     4,    89,
       5,    -1,    16,     4,    51,     5,    -1,    16,     4,     5,
      -1,    52,    -1,    51,    52,    -1,    51,     1,    -1,    -1,
      41,     9,    53,    41,     8,    -1,    -1,    41,    -1,    54,
      10,    41,    -1,    54,     1,    -1,     4,     5,    -1,     4,
      56,     5,    -1,    57,    -1,     1,    -1,    56,    57,    -1,
      56,     1,    -1,    60,    -1,    97,    -1,    91,    -1,    92,
      -1,    59,    -1,    -1,    41,     9,    58,    41,     8,    -1,
      41,     1,    -1,     8,    -1,    17,    24,    41,     8,    -1,
      41,    24,    71,    -1,    30,    41,    24,    71,    -1,    31,
       6,    68,     7,    41,    24,    71,    -1,    30,    31,     6,
      68,     7,    41,    24,    71,    -1,    71,    -1,    61,    71,
      -1,    61,     1,    -1,    68,    11,    68,    11,    68,    -1,
      41,    -1,    62,    13,    68,    13,    68,    13,    68,    -1,
      -1,     6,    65,    67,     7,    -1,    19,    64,    -1,    22,
      64,    -1,    20,     6,    63,     7,    -1,    41,    -1,    41,
      41,    -1,    41,    -1,    41,    41,    -1,    41,    41,    41,
      -1,    41,    -1,    41,    41,    -1,    41,    11,    41,    -1,
      18,    64,     4,    -1,     4,    61,     5,    -1,    -1,    41,
       9,    72,    41,     8,    -1,    25,    78,     8,    -1,    26,
      79,     8,    -1,    41,    11,    -1,    -1,    -1,    -1,    32,
       6,    73,    41,     8,    74,    41,     8,    75,    41,     7,
      71,    -1,    33,    64,    71,    -1,    70,     5,    -1,    70,
      87,     5,    -1,    12,    80,     8,    -1,    84,     8,    -1,
      41,     8,    -1,    -1,    84,     9,    76,    41,     8,    -1,
      28,     8,    -1,    27,     8,    -1,    29,     8,    -1,    66,
      71,    77,    -1,     8,    -1,    21,    71,    -1,    -1,    69,
      -1,    69,    13,    69,    -1,    69,    10,    69,    -1,    69,
      13,    69,    13,    69,    -1,    69,    10,    69,    10,    69,
      -1,    36,    13,    69,    13,    69,    -1,    36,    10,    69,
      10,    69,    -1,    69,    -1,    69,    10,    69,    -1,    69,
      10,    41,    14,    41,    -1,    69,    14,    69,    -1,    69,
      10,    41,    14,    36,    -1,    69,    14,    36,    -1,    -1,
      41,     6,    81,    86,     7,    -1,    41,     6,     7,    -1,
      -1,    41,     6,    83,    -1,    82,    86,     7,    -1,    82,
       7,    -1,    41,    -1,    -1,    67,    -1,    -1,    86,    10,
      85,    -1,    88,    -1,    87,    88,    -1,    34,    41,    11,
      61,    -1,    36,    11,    61,    -1,    35,    41,    11,    61,
      -1,    34,    41,    11,    -1,    36,    11,    -1,    35,    41,
      11,    -1,    -1,    90,    -1,    89,    90,    -1,    71,    -1,
      37,    41,     4,    61,     5,    -1,    38,    93,    -1,    39,
      93,    -1,     4,    94,     5,    -1,     4,     5,    -1,    41,
       8,    -1,    94,    41,     8,    -1,    94,     1,    -1,    46,
       8,    -1,    46,    13,    63,     8,    -1,    95,    -1,    96,
      95,    -1,    96,     1,    -1,    40,     4,    96,     5,    -1,
      40,     4,     5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   180,   180,   183,   184,   185,   188,   189,   190,   191,
     194,   195,   198,   206,   207,   210,   215,   218,   222,   223,
     224,   227,   227,   233,   234,   235,   236,   239,   240,   243,
     244,   245,   246,   249,   250,   251,   252,   253,   254,   254,
     258,   259,   262,   267,   271,   276,   281,   290,   291,   292,
     298,   303,   307,   315,   315,   320,   323,   326,   337,   338,
     345,   346,   351,   359,   360,   364,   370,   378,   381,   381,
     385,   388,   391,   394,   395,   396,   394,   402,   406,   408,
     411,   413,   415,   418,   418,   451,   452,   453,   454,   458,
     461,   462,   467,   468,   471,   474,   478,   482,   486,   493,
     496,   499,   503,   507,   511,   517,   517,   522,   530,   530,
     541,   548,   551,   552,   555,   556,   559,   562,   563,   566,
     570,   574,   578,   581,   584,   589,   590,   591,   594,   595,
     601,   606,   611,   612,   615,   616,   617,   620,   621,   628,
     629,   630,   633,   636
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
  "objects", "object", "context_name", "context", "opt_abstract", "macro",
  "globals", "global_statements", "global_statement", "@1", "arglist",
  "elements_block", "elements", "element", "@2", "ignorepat", "extension",
  "statements", "timerange", "timespec", "test_expr", "@3", "if_like_head",
  "word_list", "word3_list", "goto_word", "switch_head", "statement", "@4",
  "@5", "@6", "@7", "@8", "opt_else", "target", "jumptarget", "macro_call",
  "@9", "application_call_head", "@10", "application_call", "opt_word",
  "eval_arglist", "case_statements", "case_statement", "macro_statements",
  "macro_statement", "switches", "eswitches", "switchlist_block",
  "switchlist", "included_entry", "includeslist", "includes", 0
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
      46,    46,    47,    48,    48,    49,    50,    50,    51,    51,
      51,    53,    52,    54,    54,    54,    54,    55,    55,    56,
      56,    56,    56,    57,    57,    57,    57,    57,    58,    57,
      57,    57,    59,    60,    60,    60,    60,    61,    61,    61,
      62,    62,    63,    65,    64,    66,    66,    66,    67,    67,
      68,    68,    68,    69,    69,    69,    70,    71,    72,    71,
      71,    71,    71,    73,    74,    75,    71,    71,    71,    71,
      71,    71,    71,    76,    71,    71,    71,    71,    71,    71,
      77,    77,    78,    78,    78,    78,    78,    78,    78,    79,
      79,    79,    79,    79,    79,    81,    80,    80,    83,    82,
      84,    84,    85,    85,    86,    86,    86,    87,    87,    88,
      88,    88,    88,    88,    88,    89,    89,    89,    90,    90,
      91,    92,    93,    93,    94,    94,    94,    95,    95,    96,
      96,    96,    97,    97
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     1,     4,     1,     0,     8,     4,     3,     1,     2,
       2,     0,     5,     0,     1,     3,     2,     2,     3,     1,
       1,     2,     2,     1,     1,     1,     1,     1,     0,     5,
       2,     1,     4,     3,     4,     7,     8,     1,     2,     2,
       5,     1,     7,     0,     4,     2,     2,     4,     1,     2,
       1,     2,     3,     1,     2,     3,     3,     3,     0,     5,
       3,     3,     2,     0,     0,     0,    12,     3,     2,     3,
       3,     2,     2,     0,     5,     2,     2,     2,     3,     1,
       2,     0,     1,     3,     3,     5,     5,     5,     5,     1,
       3,     5,     3,     5,     3,     0,     5,     3,     0,     3,
       3,     2,     1,     0,     1,     0,     3,     1,     2,     4,
       3,     4,     3,     2,     3,     0,     1,     2,     1,     5,
       2,     2,     3,     2,     2,     3,     2,     2,     4,     1,
       2,     2,     4,     3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
      14,     9,     0,     0,    13,     0,     0,     3,     6,     0,
       7,     8,     0,     0,     1,     5,     4,     0,    23,    17,
       0,     0,    18,    11,    10,     0,    24,     0,    21,    20,
      16,    19,     0,    12,    26,     0,     0,     0,    30,    27,
      41,     0,     0,     0,     0,     0,     0,     0,     0,    29,
      37,    33,    35,    36,    34,   125,    25,     0,     0,     0,
       0,     0,     0,   130,   131,     0,    40,    38,     0,    32,
      28,    31,     0,    89,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
     128,   115,     0,     0,   126,    22,     0,     0,     0,    60,
       0,   133,     0,     0,   143,     0,   139,     0,     0,    43,
       0,    47,     0,     0,    53,     0,    55,     0,    56,     0,
      63,    92,     0,    99,     0,    86,    85,    87,    73,     0,
       0,   108,    82,    68,    72,    91,    78,     0,     0,     0,
       0,   117,   111,    58,   114,     0,    81,    83,    15,   127,
      42,     0,    44,    61,     0,   134,   136,   132,     0,   137,
       0,   141,   142,   140,     0,    49,    67,    48,   105,    80,
       0,    66,    51,     0,     0,     0,     0,     0,     0,    64,
       0,     0,    70,     0,     0,    71,     0,    77,     0,   109,
       0,     0,    88,     0,     0,   123,    79,   118,    59,   110,
     113,     0,     0,    62,     0,   135,     0,    39,   107,   115,
       0,     0,    57,     0,     0,     0,    65,    94,    93,    63,
     100,   104,   102,     0,     0,     0,    90,   122,   124,     0,
     112,   116,     0,     0,     0,   138,     0,    54,     0,     0,
       0,     0,     0,     0,     0,    74,   129,    69,     0,     0,
      84,     0,    45,   106,     0,     0,    98,    97,    96,    95,
     103,   101,     0,    46,     0,    50,     0,     0,    75,    52,
       0,     0,     0,    76
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     5,     6,     7,   105,     8,     9,    10,    11,    21,
      22,    37,    27,    33,    48,    49,   108,    50,    51,   110,
     173,   174,   115,   170,    88,   144,   175,   121,    89,   111,
     190,   186,   262,   270,   201,   192,   122,   124,   113,   209,
      91,   189,    92,   231,   145,   140,   141,    93,    94,    52,
      53,    63,   103,   106,   107,    54
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -142
static const short int yypact[] =
{
     111,  -142,   -36,    28,  -142,     7,   367,  -142,  -142,    27,
    -142,  -142,    10,     3,  -142,  -142,  -142,   -12,    -7,  -142,
      48,    26,  -142,  -142,  -142,    64,  -142,    13,  -142,  -142,
    -142,  -142,    20,  -142,  -142,    86,    58,    62,  -142,  -142,
    -142,    94,   -22,   125,   129,   129,   133,     2,   127,  -142,
    -142,  -142,  -142,  -142,  -142,   307,  -142,   145,   118,   165,
     148,   143,    12,  -142,  -142,    89,  -142,  -142,   333,  -142,
    -142,  -142,   333,  -142,   144,   180,   180,   181,   180,    47,
     149,   192,   193,   194,   183,   180,   162,   139,   333,   104,
    -142,    -5,   174,   281,  -142,  -142,   196,   143,   333,   170,
     201,  -142,   210,    34,  -142,   138,  -142,     5,   184,  -142,
     225,  -142,   218,   219,  -142,   227,  -142,   191,  -142,   163,
      54,   167,   226,     8,   228,  -142,  -142,  -142,  -142,   333,
     231,  -142,  -142,  -142,  -142,   217,  -142,   198,   199,   230,
     107,  -142,  -142,   205,  -142,   142,  -142,  -142,  -142,  -142,
    -142,   235,  -142,   207,   208,  -142,  -142,  -142,   247,  -142,
     191,  -142,  -142,  -142,   253,  -142,  -142,  -142,   257,  -142,
     221,  -142,    70,   252,   261,   258,   149,   149,   229,  -142,
     149,   149,  -142,   237,   119,  -142,   238,  -142,   333,  -142,
     249,   333,  -142,   260,   265,   333,  -142,  -142,  -142,  -142,
     250,   251,   254,  -142,   248,  -142,   286,  -142,  -142,   221,
     290,   143,  -142,   143,   288,   289,  -142,   294,   292,     1,
    -142,  -142,  -142,   304,   255,   308,  -142,   333,   333,    44,
    -142,  -142,   309,   296,   333,  -142,   171,  -142,   310,   313,
     149,   149,   149,   149,   134,  -142,  -142,  -142,    88,   187,
    -142,   333,  -142,  -142,   143,   143,  -142,  -142,  -142,  -142,
    -142,  -142,   280,  -142,   315,  -142,   322,   143,  -142,  -142,
     297,   324,   333,  -142
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -142,  -142,  -142,   336,   326,  -142,  -142,  -142,  -142,  -142,
     325,  -142,  -142,  -142,  -142,   299,  -142,  -142,  -142,  -141,
    -142,   189,     6,  -142,  -142,   186,   -57,   -79,  -142,   -55,
    -142,  -142,  -142,  -142,  -142,  -142,  -142,  -142,  -142,  -142,
    -142,  -142,  -142,  -142,   141,  -142,   214,  -142,   264,  -142,
    -142,   318,  -142,   262,  -142,  -142
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -122
static const short int yytable[] =
{
      90,   123,   142,    66,   100,    12,   161,    14,    19,    59,
     162,    67,   178,   109,    34,   244,    18,   101,   183,    60,
      35,    38,   184,    36,    23,    39,    68,    29,    40,    24,
      17,    30,    13,   135,    26,   156,   143,    41,    90,   157,
     151,    23,   179,   152,    20,   165,    24,   224,    72,  -120,
      42,    43,    73,   102,   229,   167,    74,    28,    44,    45,
      46,    47,    75,    76,    77,   178,    78,    20,    32,    79,
      80,    81,    82,    83,   187,   158,    84,    85,  -120,  -120,
    -120,   -60,   116,   119,   118,    87,   248,   249,   120,   165,
      55,   129,    72,  -119,   104,   179,    73,   214,   215,    56,
      74,   217,   218,    57,   220,   222,    75,    76,    77,   136,
      78,   153,   196,    79,    80,    81,    82,    83,    58,     1,
      84,    85,  -119,  -119,  -119,    23,     2,     3,    69,    87,
      24,    61,    70,    62,     4,    40,   226,    65,   137,   138,
     139,   137,   138,   139,    41,   131,   159,   132,   133,   199,
     134,   160,   200,    95,   238,   221,   239,    42,    43,    96,
     120,   256,   257,   258,   259,    44,    45,    46,    47,   167,
     260,    97,    98,   176,   167,   261,   177,   180,   253,   252,
     181,   200,   146,   147,    99,   112,   114,   117,   165,   128,
     120,    72,  -121,   167,   167,    73,   263,   264,   265,    74,
     125,   126,   127,   130,   150,    75,    76,    77,   154,    78,
     269,   153,    79,    80,    81,    82,    83,   273,   155,    84,
      85,  -121,  -121,  -121,   168,   164,   165,   169,    87,    72,
     166,   171,   172,    73,   182,   188,   185,    74,   191,   193,
     194,   195,   202,    75,    76,    77,   198,    78,   203,   204,
      79,    80,    81,    82,    83,   205,   165,    84,    85,    72,
     246,   207,   143,    73,   208,   211,    87,    74,   212,   213,
     216,   227,   234,    75,    76,    77,   228,    78,   219,   223,
      79,    80,    81,    82,    83,    72,   148,    84,    85,    73,
     225,   230,   232,    74,   235,   233,    87,   237,   240,    75,
      76,    77,   241,    78,   242,   243,    79,    80,    81,    82,
      83,    72,   245,    84,    85,    73,   247,   250,    86,    74,
     251,   266,    87,   254,   255,    75,    76,    77,   267,    78,
     268,   272,    79,    80,    81,    82,    83,    72,   271,    84,
      85,    73,    16,    25,    86,    74,    31,    71,    87,   206,
     236,    75,    76,    77,   197,    78,   210,   149,    79,    80,
      81,    82,    83,    64,     0,    84,    85,    -2,    15,   163,
     -14,     0,     0,     0,    87,     1,     0,     0,     0,     0,
       0,     0,     2,     3,     0,     0,     0,     0,     0,     0,
       4
};

static const short int yycheck[] =
{
      55,    80,     7,     1,    61,    41,     1,     0,     5,    31,
       5,     9,    11,    68,     1,    14,     6,     5,    10,    41,
       7,     1,    14,    10,    36,     5,    24,     1,     8,    41,
       3,     5,     4,    88,    41,     1,    41,    17,    93,     5,
      97,    36,    41,    98,    41,     1,    41,   188,     4,     5,
      30,    31,     8,    41,   195,   110,    12,     9,    38,    39,
      40,    41,    18,    19,    20,    11,    22,    41,     4,    25,
      26,    27,    28,    29,   129,    41,    32,    33,    34,    35,
      36,    11,    76,    36,    78,    41,   227,   228,    41,     1,
       4,    85,     4,     5,     5,    41,     8,   176,   177,    41,
      12,   180,   181,    41,   183,   184,    18,    19,    20,     5,
      22,    41,     5,    25,    26,    27,    28,    29,    24,     8,
      32,    33,    34,    35,    36,    36,    15,    16,     1,    41,
      41,     6,     5,     4,    23,     8,   191,     4,    34,    35,
      36,    34,    35,    36,    17,     6,     8,     8,     9,     7,
      11,    13,    10,     8,   211,    36,   213,    30,    31,    41,
      41,   240,   241,   242,   243,    38,    39,    40,    41,   224,
      36,     6,    24,    10,   229,    41,    13,    10,     7,   234,
      13,    10,     8,     9,    41,    41,     6,     6,     1,     6,
      41,     4,     5,   248,   249,     8,   251,   254,   255,    12,
       8,     8,     8,    41,     8,    18,    19,    20,     7,    22,
     267,    41,    25,    26,    27,    28,    29,   272,     8,    32,
      33,    34,    35,    36,     6,    41,     1,     8,    41,     4,
       5,     4,    41,     8,     8,     4,     8,    12,    21,    41,
      41,    11,     7,    18,    19,    20,    41,    22,    41,    41,
      25,    26,    27,    28,    29,     8,     1,    32,    33,     4,
       5,     8,    41,     8,     7,    13,    41,    12,     7,    11,
      41,    11,    24,    18,    19,    20,    11,    22,    41,    41,
      25,    26,    27,    28,    29,     4,     5,    32,    33,     8,
      41,    41,    41,    12,     8,    41,    41,     7,    10,    18,
      19,    20,    13,    22,    10,    13,    25,    26,    27,    28,
      29,     4,     8,    32,    33,     8,     8,     8,    37,    12,
      24,    41,    41,    13,    11,    18,    19,    20,    13,    22,
       8,     7,    25,    26,    27,    28,    29,     4,    41,    32,
      33,     8,     6,    17,    37,    12,    21,    48,    41,   160,
     209,    18,    19,    20,   140,    22,   170,    93,    25,    26,
      27,    28,    29,    45,    -1,    32,    33,     0,     1,   107,
       3,    -1,    -1,    -1,    41,     8,    -1,    -1,    -1,    -1,
      -1,    -1,    15,    16,    -1,    -1,    -1,    -1,    -1,    -1,
      23
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     8,    15,    16,    23,    43,    44,    45,    47,    48,
      49,    50,    41,     4,     0,     1,    45,     3,     6,     5,
      41,    51,    52,    36,    41,    46,    41,    54,     9,     1,
       5,    52,     4,    55,     1,     7,    10,    53,     1,     5,
       8,    17,    30,    31,    38,    39,    40,    41,    56,    57,
      59,    60,    91,    92,    97,     4,    41,    41,    24,    31,
      41,     6,     4,    93,    93,     4,     1,     9,    24,     1,
       5,    57,     4,     8,    12,    18,    19,    20,    22,    25,
      26,    27,    28,    29,    32,    33,    37,    41,    66,    70,
      71,    82,    84,    89,    90,     8,    41,     6,    24,    41,
      68,     5,    41,    94,     5,    46,    95,    96,    58,    71,
      61,    71,    41,    80,     6,    64,    64,     6,    64,    36,
      41,    69,    78,    69,    79,     8,     8,     8,     6,    64,
      41,     6,     8,     9,    11,    71,     5,    34,    35,    36,
      87,    88,     7,    41,    67,    86,     8,     9,     5,    90,
       8,    68,    71,    41,     7,     8,     1,     5,    41,     8,
      13,     1,     5,    95,    41,     1,     5,    71,     6,     8,
      65,     4,    41,    62,    63,    68,    10,    13,    11,    41,
      10,    13,     8,    10,    14,     8,    73,    71,     4,    83,
      72,    21,    77,    41,    41,    11,     5,    88,    41,     7,
      10,    76,     7,    41,    41,     8,    63,     8,     7,    81,
      67,    13,     7,    11,    69,    69,    41,    69,    69,    41,
      69,    36,    69,    41,    61,    41,    71,    11,    11,    61,
      41,    85,    41,    41,    24,     8,    86,     7,    68,    68,
      10,    13,    10,    13,    14,     8,     5,     8,    61,    61,
       8,    24,    71,     7,    13,    11,    69,    69,    69,    69,
      36,    41,    74,    71,    68,    68,    41,    13,     8,    68,
      75,    41,     7,    71
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
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1246 "ael.tab.c"
        break;
      case 44: /* "objects" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1254 "ael.tab.c"
        break;
      case 45: /* "object" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1262 "ael.tab.c"
        break;
      case 46: /* "context_name" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1267 "ael.tab.c"
        break;
      case 47: /* "context" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1275 "ael.tab.c"
        break;
      case 49: /* "macro" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1283 "ael.tab.c"
        break;
      case 50: /* "globals" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1291 "ael.tab.c"
        break;
      case 51: /* "global_statements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1299 "ael.tab.c"
        break;
      case 52: /* "global_statement" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1307 "ael.tab.c"
        break;
      case 54: /* "arglist" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1315 "ael.tab.c"
        break;
      case 55: /* "elements_block" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1323 "ael.tab.c"
        break;
      case 56: /* "elements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1331 "ael.tab.c"
        break;
      case 57: /* "element" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1339 "ael.tab.c"
        break;
      case 59: /* "ignorepat" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1347 "ael.tab.c"
        break;
      case 60: /* "extension" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1355 "ael.tab.c"
        break;
      case 61: /* "statements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1363 "ael.tab.c"
        break;
      case 62: /* "timerange" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1368 "ael.tab.c"
        break;
      case 63: /* "timespec" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1376 "ael.tab.c"
        break;
      case 64: /* "test_expr" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1381 "ael.tab.c"
        break;
      case 66: /* "if_like_head" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1389 "ael.tab.c"
        break;
      case 67: /* "word_list" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1394 "ael.tab.c"
        break;
      case 68: /* "word3_list" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1399 "ael.tab.c"
        break;
      case 69: /* "goto_word" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1404 "ael.tab.c"
        break;
      case 70: /* "switch_head" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1412 "ael.tab.c"
        break;
      case 71: /* "statement" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1420 "ael.tab.c"
        break;
      case 77: /* "opt_else" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1428 "ael.tab.c"
        break;
      case 78: /* "target" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1436 "ael.tab.c"
        break;
      case 79: /* "jumptarget" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1444 "ael.tab.c"
        break;
      case 80: /* "macro_call" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1452 "ael.tab.c"
        break;
      case 82: /* "application_call_head" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1460 "ael.tab.c"
        break;
      case 84: /* "application_call" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1468 "ael.tab.c"
        break;
      case 85: /* "opt_word" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1473 "ael.tab.c"
        break;
      case 86: /* "eval_arglist" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1481 "ael.tab.c"
        break;
      case 87: /* "case_statements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1489 "ael.tab.c"
        break;
      case 88: /* "case_statement" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1497 "ael.tab.c"
        break;
      case 89: /* "macro_statements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1505 "ael.tab.c"
        break;
      case 90: /* "macro_statement" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1513 "ael.tab.c"
        break;
      case 91: /* "switches" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1521 "ael.tab.c"
        break;
      case 92: /* "eswitches" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1529 "ael.tab.c"
        break;
      case 93: /* "switchlist_block" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1537 "ael.tab.c"
        break;
      case 94: /* "switchlist" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1545 "ael.tab.c"
        break;
      case 95: /* "included_entry" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1553 "ael.tab.c"
        break;
      case 96: /* "includeslist" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1561 "ael.tab.c"
        break;
      case 97: /* "includes" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1569 "ael.tab.c"
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
#line 180 "ael.y"
    { (yyval.pval) = parseio->pval = (yyvsp[0].pval); ;}
    break;

  case 3:
#line 183 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 4:
#line 184 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 5:
#line 185 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 6:
#line 188 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 7:
#line 189 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 8:
#line 190 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 9:
#line 191 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 10:
#line 194 "ael.y"
    { (yyval.str) = (yyvsp[0].str); ;}
    break;

  case 11:
#line 195 "ael.y"
    { (yyval.str) = strdup("default"); ;}
    break;

  case 12:
#line 198 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.abstract = (yyvsp[-3].intval); ;}
    break;

  case 13:
#line 206 "ael.y"
    { (yyval.intval) = 1; ;}
    break;

  case 14:
#line 207 "ael.y"
    { (yyval.intval) = 0; ;}
    break;

  case 15:
#line 210 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-6].str); (yyval.pval)->u2.arglist = (yyvsp[-4].pval); (yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 16:
#line 215 "ael.y"
    {
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.statements = (yyvsp[-1].pval);;}
    break;

  case 17:
#line 218 "ael.y"
    { /* empty globals is OK */
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[-2]), &(yylsp[0])); ;}
    break;

  case 18:
#line 222 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 19:
#line 223 "ael.y"
    {(yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 20:
#line 224 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 21:
#line 227 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 22:
#line 227 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 23:
#line 233 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 24:
#line 234 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 25:
#line 235 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[0].str), &(yylsp[0]))); ;}
    break;

  case 26:
#line 236 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 27:
#line 239 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 28:
#line 240 "ael.y"
    { (yyval.pval) = (yyvsp[-1].pval); ;}
    break;

  case 29:
#line 243 "ael.y"
    { (yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 30:
#line 244 "ael.y"
    {(yyval.pval)=0;;}
    break;

  case 31:
#line 245 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 32:
#line 246 "ael.y"
    { (yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 33:
#line 249 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 34:
#line 250 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 35:
#line 251 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 36:
#line 252 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 37:
#line 253 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 38:
#line 254 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 39:
#line 254 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 40:
#line 258 "ael.y"
    {free((yyvsp[-1].str)); (yyval.pval)=0;;}
    break;

  case 41:
#line 259 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 42:
#line 262 "ael.y"
    {
		(yyval.pval) = npval2(PV_IGNOREPAT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 43:
#line 267 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 44:
#line 271 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;;}
    break;

  case 45:
#line 276 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 46:
#line 281 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 47:
#line 290 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 48:
#line 291 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 49:
#line 292 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 50:
#line 298 "ael.y"
    {
		asprintf(&(yyval.str), "%s:%s:%s", (yyvsp[-4].str), (yyvsp[-2].str), (yyvsp[0].str));
		free((yyvsp[-4].str));
		free((yyvsp[-2].str));
		free((yyvsp[0].str)); ;}
    break;

  case 51:
#line 303 "ael.y"
    { (yyval.str) = (yyvsp[0].str); ;}
    break;

  case 52:
#line 307 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-6].str), &(yylsp[-6]));
		(yyval.pval)->u1.list = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->u1.list->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->u1.list->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 53:
#line 315 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 54:
#line 315 "ael.y"
    {
		(yyval.str) = (yyvsp[-1].str); ;}
    break;

  case 55:
#line 320 "ael.y"
    {
		(yyval.pval)= npval2(PV_IF, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str); ;}
    break;

  case 56:
#line 323 "ael.y"
    {
		(yyval.pval) = npval2(PV_RANDOM, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str=(yyvsp[0].str);;}
    break;

  case 57:
#line 326 "ael.y"
    {
		(yyval.pval) = npval2(PV_IFTIME, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);
		prev_word = 0; ;}
    break;

  case 58:
#line 337 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 59:
#line 338 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 60:
#line 345 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 61:
#line 346 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 62:
#line 351 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s%s", (yyvsp[-2].str), (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word=(yyval.str);;}
    break;

  case 63:
#line 359 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 64:
#line 360 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));;}
    break;

  case 65:
#line 364 "ael.y"
    {
		asprintf(&((yyval.str)), "%s:%s", (yyvsp[-2].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[0].str));;}
    break;

  case 66:
#line 370 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCH, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 67:
#line 378 "ael.y"
    {
		(yyval.pval) = npval2(PV_STATEMENTBLOCK, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 68:
#line 381 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 69:
#line 381 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 70:
#line 385 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 71:
#line 388 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 72:
#line 391 "ael.y"
    {
		(yyval.pval) = npval2(PV_LABEL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 73:
#line 394 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 74:
#line 395 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 75:
#line 396 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 76:
#line 396 "ael.y"
    { /* XXX word_list maybe ? */
		(yyval.pval) = npval2(PV_FOR, &(yylsp[-11]), &(yylsp[0]));
		(yyval.pval)->u1.for_init = (yyvsp[-8].str);
		(yyval.pval)->u2.for_test=(yyvsp[-5].str);
		(yyval.pval)->u3.for_inc = (yyvsp[-2].str);
		(yyval.pval)->u4.for_statements = (yyvsp[0].pval);;}
    break;

  case 77:
#line 402 "ael.y"
    {
		(yyval.pval) = npval2(PV_WHILE, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 78:
#line 406 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 79:
#line 408 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[0]));
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 80:
#line 411 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[-1])); ;}
    break;

  case 81:
#line 413 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 82:
#line 415 "ael.y"
    {
		(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 83:
#line 418 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 84:
#line 418 "ael.y"
    {
		char *bufx;
		int tot=0;
		pval *pptr;
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u2.val=(yyvsp[-1].str);
		/* rebuild the original string-- this is not an app call, it's an unwrapped vardec, with a func call on the LHS */
		/* string to big to fit in the buffer? */
		tot+=strlen((yyvsp[-4].pval)->u1.str);
		for(pptr=(yyvsp[-4].pval)->u2.arglist;pptr;pptr=pptr->next) {
			tot+=strlen(pptr->u1.str);
			tot++; /* for a sep like a comma */
		}
		tot+=4; /* for safety */
		bufx = calloc(1, tot);
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

  case 85:
#line 451 "ael.y"
    { (yyval.pval) = npval2(PV_BREAK, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 86:
#line 452 "ael.y"
    { (yyval.pval) = npval2(PV_RETURN, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 87:
#line 453 "ael.y"
    { (yyval.pval) = npval2(PV_CONTINUE, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 88:
#line 454 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[-1]));
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 89:
#line 458 "ael.y"
    { (yyval.pval)=0; ;}
    break;

  case 90:
#line 461 "ael.y"
    { (yyval.pval) = (yyvsp[0].pval); ;}
    break;

  case 91:
#line 462 "ael.y"
    { (yyval.pval) = NULL ; ;}
    break;

  case 92:
#line 467 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 93:
#line 468 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 94:
#line 471 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 95:
#line 474 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 96:
#line 478 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 97:
#line 482 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 98:
#line 486 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 99:
#line 493 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword(strdup("1"), &(yylsp[0])); ;}
    break;

  case 100:
#line 496 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 101:
#line 499 "ael.y"
    {	/* XXX they are stored in a different order */
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next->next = nword((yyvsp[-2].str), &(yylsp[-2])); ;}
    break;

  case 102:
#line 503 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword(strdup("1"), &(yylsp[0])); ;}
    break;

  case 103:
#line 507 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next->next = nword((yyvsp[-2].str), &(yylsp[-2])); ;}
    break;

  case 104:
#line 511 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[0]));
		(yyval.pval)->next->next = nword( strdup("1"), &(yylsp[0])); ;}
    break;

  case 105:
#line 517 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 106:
#line 517 "ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		(yyval.pval) = npval2(PV_MACRO_CALL, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.arglist = (yyvsp[-1].pval);;}
    break;

  case 107:
#line 522 "ael.y"
    {
		(yyval.pval)= npval2(PV_MACRO_CALL, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 108:
#line 530 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 109:
#line 530 "ael.y"
    {
		if (strcasecmp((yyvsp[-2].str),"goto") == 0) {
			(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[-1]));
			free((yyvsp[-2].str)); /* won't be using this */
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Suggestion: Use the goto statement instead of the Goto() application call in AEL.\n", my_file, (yylsp[-2]).first_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column );
		} else {
			(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[-2]), &(yylsp[-1]));
			(yyval.pval)->u1.str = (yyvsp[-2].str);
		} ;}
    break;

  case 110:
#line 541 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[0]));
 		if( (yyval.pval)->type == PV_GOTO )
			(yyval.pval)->u1.list = (yyvsp[-1].pval);
	 	else
			(yyval.pval)->u2.arglist = (yyvsp[-1].pval);
	;}
    break;

  case 111:
#line 548 "ael.y"
    { (yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 112:
#line 551 "ael.y"
    { (yyval.str) = (yyvsp[0].str) ;}
    break;

  case 113:
#line 552 "ael.y"
    { (yyval.str) = strdup(""); ;}
    break;

  case 114:
#line 555 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 115:
#line 556 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		(yyval.pval)->u1.str = strdup(""); ;}
    break;

  case 116:
#line 559 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[0].str), &(yylsp[0]))); ;}
    break;

  case 117:
#line 562 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 118:
#line 563 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 119:
#line 566 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-3]), &(yylsp[-1])); /* XXX 3 or 4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 120:
#line 570 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 121:
#line 574 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-3]), &(yylsp[0])); /* XXX@3 or @4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 122:
#line 578 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 123:
#line 581 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;;}
    break;

  case 124:
#line 584 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 125:
#line 589 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 126:
#line 590 "ael.y"
    {(yyval.pval) = (yyvsp[0].pval);;}
    break;

  case 127:
#line 591 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 128:
#line 594 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 129:
#line 595 "ael.y"
    {
		(yyval.pval) = npval2(PV_CATCH, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 130:
#line 601 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[0].pval); ;}
    break;

  case 131:
#line 606 "ael.y"
    {
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[0].pval); ;}
    break;

  case 132:
#line 611 "ael.y"
    { (yyval.pval) = (yyvsp[-1].pval); ;}
    break;

  case 133:
#line 612 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 134:
#line 615 "ael.y"
    { (yyval.pval) = nword((yyvsp[-1].str), &(yylsp[-1])); ;}
    break;

  case 135:
#line 616 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[-1].str), &(yylsp[-1]))); ;}
    break;

  case 136:
#line 617 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 137:
#line 620 "ael.y"
    { (yyval.pval) = nword((yyvsp[-1].str), &(yylsp[-1])); ;}
    break;

  case 138:
#line 621 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-3].str), &(yylsp[-3]));
		(yyval.pval)->u2.arglist = (yyvsp[-1].pval);
		prev_word=0; /* XXX sure ? */ ;}
    break;

  case 139:
#line 628 "ael.y"
    { (yyval.pval) = (yyvsp[0].pval); ;}
    break;

  case 140:
#line 629 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 141:
#line 630 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 142:
#line 633 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 143:
#line 636 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-2]), &(yylsp[0]));;}
    break;


      default: break;
    }

/* Line 1126 of yacc.c.  */
#line 2813 "ael.tab.c"

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


#line 641 "ael.y"


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

static struct pval *npval(pvaltype type, int first_line, int last_line,
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

/* append second element to the list in the first one */
static pval * linku1(pval *head, pval *tail)
{
	if (!head)
		return tail;
	if (tail) {
		if (!head->next) {
			head->next = tail;
		} else {
			head->u1_last->next = tail;
		}
		head->u1_last = tail;
	}
	return head;
}

