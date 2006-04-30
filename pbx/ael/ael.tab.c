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

/* update end position of an object, return the object */
static pval *update_last(pval *, YYLTYPE *);


/* Line 219 of yacc.c.  */
#line 268 "ael.tab.c"

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
#define YYFINAL  18
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   488

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  56
/* YYNRULES -- Number of rules. */
#define YYNRULES  149
/* YYNRULES -- Number of states. */
#define YYNSTATES  344

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
      21,    23,    25,    29,    34,    43,    51,    59,    66,    71,
      75,    77,    80,    83,    84,    90,    92,    96,    99,   102,
     106,   108,   110,   113,   116,   118,   120,   122,   124,   126,
     127,   133,   136,   138,   143,   147,   152,   160,   169,   171,
     174,   177,   178,   184,   185,   191,   206,   217,   219,   222,
     224,   227,   231,   233,   236,   240,   241,   248,   252,   253,
     259,   263,   267,   270,   271,   272,   273,   286,   287,   294,
     297,   301,   305,   308,   311,   312,   318,   321,   324,   327,
     331,   335,   339,   341,   344,   345,   347,   351,   355,   361,
     367,   373,   379,   381,   385,   391,   395,   401,   405,   406,
     412,   416,   417,   421,   425,   428,   430,   431,   433,   434,
     438,   440,   443,   448,   452,   457,   461,   464,   468,   470,
     473,   475,   481,   486,   490,   495,   499,   502,   506,   509,
     512,   527,   538,   542,   558,   570,   573,   575,   577,   582
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      43,     0,    -1,    44,    -1,    45,    -1,    44,    45,    -1,
      44,     1,    -1,    47,    -1,    48,    -1,    49,    -1,     8,
      -1,    41,    -1,    36,    -1,     3,    46,    54,    -1,    23,
       3,    46,    54,    -1,    15,    41,     6,    53,     7,     4,
      90,     5,    -1,    15,    41,     6,    53,     7,     4,     5,
      -1,    15,    41,     6,     7,     4,    90,     5,    -1,    15,
      41,     6,     7,     4,     5,    -1,    16,     4,    50,     5,
      -1,    16,     4,     5,    -1,    51,    -1,    50,    51,    -1,
      50,     1,    -1,    -1,    41,     9,    52,    41,     8,    -1,
      41,    -1,    53,    10,    41,    -1,    53,     1,    -1,     4,
       5,    -1,     4,    55,     5,    -1,    56,    -1,     1,    -1,
      55,    56,    -1,    55,     1,    -1,    59,    -1,    97,    -1,
      92,    -1,    93,    -1,    58,    -1,    -1,    41,     9,    57,
      41,     8,    -1,    41,     1,    -1,     8,    -1,    17,    24,
      41,     8,    -1,    41,    24,    71,    -1,    30,    41,    24,
      71,    -1,    31,     6,    67,     7,    41,    24,    71,    -1,
      30,    31,     6,    67,     7,    41,    24,    71,    -1,    71,
      -1,    60,    71,    -1,    60,     1,    -1,    -1,    19,     6,
      62,    66,     7,    -1,    -1,    22,     6,    64,    66,     7,
      -1,    20,     6,    67,    11,    67,    11,    67,    13,    67,
      13,    67,    13,    67,     7,    -1,    20,     6,    41,    13,
      67,    13,    67,    13,    67,     7,    -1,    41,    -1,    41,
      41,    -1,    41,    -1,    41,    41,    -1,    41,    41,    41,
      -1,    41,    -1,    41,    41,    -1,    41,    11,    41,    -1,
      -1,    18,     6,    70,    41,     7,     4,    -1,     4,    60,
       5,    -1,    -1,    41,     9,    72,    41,     8,    -1,    25,
      79,     8,    -1,    26,    80,     8,    -1,    41,    11,    -1,
      -1,    -1,    -1,    32,     6,    73,    41,     8,    74,    41,
       8,    75,    41,     7,    71,    -1,    -1,    33,     6,    76,
      41,     7,    71,    -1,    69,     5,    -1,    69,    88,     5,
      -1,    12,    81,     8,    -1,    85,     8,    -1,    41,     8,
      -1,    -1,    85,     9,    77,    41,     8,    -1,    28,     8,
      -1,    27,     8,    -1,    29,     8,    -1,    63,    71,    78,
      -1,    61,    71,    78,    -1,    65,    71,    78,    -1,     8,
      -1,    21,    71,    -1,    -1,    68,    -1,    68,    13,    68,
      -1,    68,    10,    68,    -1,    68,    13,    68,    13,    68,
      -1,    68,    10,    68,    10,    68,    -1,    36,    13,    68,
      13,    68,    -1,    36,    10,    68,    10,    68,    -1,    68,
      -1,    68,    10,    68,    -1,    68,    10,    41,    14,    41,
      -1,    68,    14,    68,    -1,    68,    10,    41,    14,    36,
      -1,    68,    14,    36,    -1,    -1,    41,     6,    82,    87,
       7,    -1,    41,     6,     7,    -1,    -1,    41,    84,     6,
      -1,    83,    87,     7,    -1,    83,     7,    -1,    41,    -1,
      -1,    66,    -1,    -1,    87,    10,    86,    -1,    89,    -1,
      88,    89,    -1,    34,    41,    11,    60,    -1,    36,    11,
      60,    -1,    35,    41,    11,    60,    -1,    34,    41,    11,
      -1,    36,    11,    -1,    35,    41,    11,    -1,    91,    -1,
      90,    91,    -1,    71,    -1,    37,    41,     4,    60,     5,
      -1,    38,     4,    94,     5,    -1,    38,     4,     5,    -1,
      39,     4,    94,     5,    -1,    39,     4,     5,    -1,    41,
       8,    -1,    94,    41,     8,    -1,    94,     1,    -1,    96,
       8,    -1,    96,    13,    67,    11,    67,    11,    67,    13,
      67,    13,    67,    13,    67,     8,    -1,    96,    13,    41,
      13,    67,    13,    67,    13,    67,     8,    -1,    95,    96,
       8,    -1,    95,    96,    13,    67,    11,    67,    11,    67,
      13,    67,    13,    67,    13,    67,     8,    -1,    95,    96,
      13,    41,    13,    67,    13,    67,    13,    67,     8,    -1,
      95,     1,    -1,    41,    -1,    36,    -1,    40,     4,    95,
       5,    -1,    40,     4,     5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   170,   170,   173,   174,   185,   188,   189,   190,   191,
     194,   195,   198,   202,   209,   212,   215,   219,   224,   227,
     231,   232,   233,   236,   236,   242,   245,   249,   252,   253,
     256,   257,   258,   261,   264,   265,   266,   267,   268,   269,
     269,   273,   274,   277,   282,   286,   291,   296,   305,   306,
     309,   312,   312,   317,   317,   322,   338,   358,   359,   366,
     367,   372,   380,   381,   385,   391,   391,   399,   402,   402,
     406,   409,   412,   415,   416,   417,   415,   423,   423,   427,
     429,   432,   434,   436,   439,   439,   472,   473,   474,   475,
     479,   483,   487,   490,   491,   496,   498,   503,   508,   515,
     522,   529,   538,   543,   548,   555,   562,   569,   578,   578,
     583,   588,   588,   598,   605,   608,   609,   612,   615,   618,
     625,   626,   631,   635,   639,   643,   646,   649,   654,   655,
     660,   661,   667,   670,   674,   677,   681,   684,   689,   692,
     695,   712,   725,   730,   748,   763,   766,   767,   770,   773
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
  "objects", "object", "word_or_default", "context", "macro", "globals",
  "global_statements", "global_statement", "@1", "arglist",
  "elements_block", "elements", "element", "@2", "ignorepat", "extension",
  "statements", "if_head", "@3", "random_head", "@4", "iftime_head",
  "word_list", "word3_list", "goto_word", "switch_head", "@5", "statement",
  "@6", "@7", "@8", "@9", "@10", "@11", "opt_else", "target", "jumptarget",
  "macro_call", "@12", "application_call_head", "@13", "application_call",
  "opt_word", "eval_arglist", "case_statements", "case_statement",
  "macro_statements", "macro_statement", "switches", "eswitches",
  "switchlist", "includeslist", "includedname", "includes", 0
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
      46,    46,    47,    47,    48,    48,    48,    48,    49,    49,
      50,    50,    50,    52,    51,    53,    53,    53,    54,    54,
      55,    55,    55,    55,    56,    56,    56,    56,    56,    57,
      56,    56,    56,    58,    59,    59,    59,    59,    60,    60,
      60,    62,    61,    64,    63,    65,    65,    66,    66,    67,
      67,    67,    68,    68,    68,    70,    69,    71,    72,    71,
      71,    71,    71,    73,    74,    75,    71,    76,    71,    71,
      71,    71,    71,    71,    77,    71,    71,    71,    71,    71,
      71,    71,    71,    78,    78,    79,    79,    79,    79,    79,
      79,    79,    80,    80,    80,    80,    80,    80,    82,    81,
      81,    84,    83,    85,    85,    86,    86,    87,    87,    87,
      88,    88,    89,    89,    89,    89,    89,    89,    90,    90,
      91,    91,    92,    92,    93,    93,    94,    94,    94,    95,
      95,    95,    95,    95,    95,    95,    96,    96,    97,    97
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     1,     3,     4,     8,     7,     7,     6,     4,     3,
       1,     2,     2,     0,     5,     1,     3,     2,     2,     3,
       1,     1,     2,     2,     1,     1,     1,     1,     1,     0,
       5,     2,     1,     4,     3,     4,     7,     8,     1,     2,
       2,     0,     5,     0,     5,    14,    10,     1,     2,     1,
       2,     3,     1,     2,     3,     0,     6,     3,     0,     5,
       3,     3,     2,     0,     0,     0,    12,     0,     6,     2,
       3,     3,     2,     2,     0,     5,     2,     2,     2,     3,
       3,     3,     1,     2,     0,     1,     3,     3,     5,     5,
       5,     5,     1,     3,     5,     3,     5,     3,     0,     5,
       3,     0,     3,     3,     2,     1,     0,     1,     0,     3,
       1,     2,     4,     3,     4,     3,     2,     3,     1,     2,
       1,     5,     4,     3,     4,     3,     2,     3,     2,     2,
      14,    10,     3,    15,    11,     2,     1,     1,     4,     3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
       0,     0,     9,     0,     0,     0,     0,     0,     3,     6,
       7,     8,    11,    10,     0,     0,     0,     0,     1,     5,
       4,     0,    12,     0,    19,     0,     0,    20,     0,    31,
      28,    42,     0,     0,     0,     0,     0,     0,     0,     0,
      30,    38,    34,    36,    37,    35,     0,    25,     0,    23,
      22,    18,    21,    13,     0,     0,     0,     0,     0,     0,
       0,    41,    39,     0,    33,    29,    32,     0,    27,     0,
       0,     0,     0,     0,     0,    59,     0,   133,     0,     0,
     135,     0,   149,   147,   146,     0,     0,     0,     0,    92,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,   111,     0,     0,     0,     0,    44,   118,     0,
      17,     0,   130,     0,   128,     0,    26,     0,    43,     0,
      45,    60,     0,   136,   138,   132,     0,   134,   145,   148,
       0,   139,     0,     0,     0,    48,     0,     0,    65,    51,
       0,    53,     0,    62,    95,     0,   102,     0,    87,    86,
      88,    73,    77,    83,    68,    72,     0,    94,    94,    94,
      79,     0,     0,     0,     0,   120,   114,    57,   117,     0,
      82,    84,     0,    16,   129,    15,     0,    24,     0,    61,
       0,   137,   142,     0,    59,     0,    40,    50,    67,    49,
     108,    81,     0,     0,    59,     0,     0,     0,     0,     0,
      63,     0,     0,    70,     0,     0,    71,     0,     0,     0,
     112,     0,    90,    89,    91,     0,     0,   126,    80,   121,
      58,   113,   116,     0,     0,    14,     0,     0,    59,     0,
       0,     0,   110,   118,     0,     0,     0,     0,     0,     0,
       0,    64,    97,    96,    62,   103,   107,   105,     0,     0,
       0,    93,   125,   127,     0,   115,   119,     0,     0,     0,
      46,     0,     0,     0,     0,     0,     0,    52,     0,     0,
      54,     0,     0,     0,     0,     0,    74,     0,    69,     0,
       0,    85,   131,    47,     0,     0,     0,     0,   109,    66,
       0,     0,   101,   100,    99,    98,   106,   104,     0,    78,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,    75,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   141,     0,    56,     0,     0,   144,     0,
       0,     0,     0,     0,     0,     0,    76,     0,     0,     0,
       0,   140,    55,   143
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     6,     7,     8,    14,     9,    10,    11,    26,    27,
      71,    48,    22,    39,    40,    87,    41,    42,   134,   103,
     193,   104,   196,   105,   168,    76,   144,   106,   192,   135,
     209,   207,   298,   320,   208,   223,   212,   145,   147,   137,
     233,   108,   156,   109,   256,   169,   164,   165,   113,   114,
      43,    44,    79,    85,    86,    45
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -192
static const short int yypact[] =
{
     178,    76,  -192,   -36,    21,    13,    18,   465,  -192,  -192,
    -192,  -192,  -192,  -192,    43,    66,    52,    76,  -192,  -192,
    -192,   144,  -192,     5,  -192,    61,    14,  -192,    43,  -192,
    -192,  -192,    62,   113,    89,   143,   166,   176,   134,   201,
    -192,  -192,  -192,  -192,  -192,  -192,   184,  -192,   152,  -192,
    -192,  -192,  -192,  -192,   148,   213,   196,   149,    64,    93,
       3,  -192,  -192,   431,  -192,  -192,  -192,   327,  -192,   225,
     189,   192,   226,   149,   431,   194,   231,  -192,   235,    44,
    -192,    53,  -192,  -192,  -192,    12,   138,   203,   431,  -192,
     208,   246,   247,   248,   249,   124,   215,   250,   251,   253,
     257,   258,   160,   431,   431,   431,   106,  -192,    25,    70,
    -192,   216,  -192,   353,  -192,   379,  -192,   259,  -192,   261,
    -192,   233,   236,  -192,  -192,  -192,   270,  -192,  -192,  -192,
     179,  -192,   239,   273,   271,  -192,   260,   274,  -192,  -192,
     243,  -192,   198,   109,   200,   277,   193,   278,  -192,  -192,
    -192,  -192,  -192,  -192,  -192,  -192,   281,   267,   267,   267,
    -192,   254,   266,   283,   121,  -192,  -192,   269,  -192,   205,
    -192,  -192,   288,  -192,  -192,  -192,   405,  -192,   275,  -192,
     284,  -192,  -192,   276,    10,   290,  -192,  -192,  -192,  -192,
     304,  -192,   295,   296,    51,   303,   296,   215,   215,   297,
    -192,   215,   215,  -192,   299,   163,  -192,   300,   302,   307,
    -192,   431,  -192,  -192,  -192,   311,   313,   431,  -192,  -192,
    -192,  -192,   309,   310,   431,  -192,   291,   431,    86,   314,
     149,   149,  -192,   296,   337,   355,   149,   149,   356,   308,
     354,  -192,   359,   357,   122,  -192,  -192,  -192,   358,   367,
     368,  -192,   431,   431,     2,  -192,  -192,   369,   301,   431,
    -192,   149,   149,   375,   378,   214,   388,  -192,   380,   384,
    -192,   215,   215,   215,   215,   164,  -192,   431,  -192,    55,
      96,  -192,  -192,  -192,   383,   389,   149,   149,  -192,  -192,
     149,   149,  -192,  -192,  -192,  -192,  -192,  -192,   361,  -192,
     149,   149,   390,   401,   402,   406,   410,   408,   409,   149,
     149,   149,   149,  -192,   149,   149,   418,   415,   422,   423,
     399,   433,   432,  -192,   149,  -192,   149,   437,  -192,   149,
     434,   435,   431,   439,   149,   149,  -192,   149,   446,   448,
     453,  -192,  -192,  -192
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -192,  -192,  -192,   455,   450,  -192,  -192,  -192,  -192,   443,
    -192,  -192,   442,  -192,   436,  -192,  -192,  -192,  -191,  -192,
    -192,  -192,  -192,  -192,    32,   -64,   -95,  -192,  -192,   -63,
    -192,  -192,  -192,  -192,  -192,  -192,   -20,  -192,  -192,  -192,
    -192,  -192,  -192,  -192,  -192,   238,  -192,   312,   362,  -111,
    -192,  -192,   419,  -192,   394,  -192
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -125
static const short int yytable[] =
{
     107,   146,   174,   187,   112,    15,    88,  -123,    82,   119,
      89,   120,    46,   128,    90,    50,    17,   129,    18,    51,
      91,    92,    93,   230,    94,    16,   254,    95,    96,    97,
      98,    99,   166,   258,   100,   101,  -123,  -123,  -123,    83,
     157,   158,   159,   102,    84,   124,    47,    21,    83,   125,
     112,   121,   112,    84,   124,    25,   187,    24,   127,    88,
    -122,   279,   280,    89,   236,   174,   167,    90,   185,    77,
      49,   189,    23,    91,    92,    93,   195,    94,   170,   171,
      95,    96,    97,    98,    99,   126,    54,   100,   101,  -122,
    -122,  -122,   121,    25,   126,    57,   102,   187,    80,   261,
      88,  -124,   239,   240,    89,    78,   242,   243,    90,   245,
     247,   160,    12,   112,    91,    92,    93,    13,    94,   229,
     199,    95,    96,    97,    98,    99,   218,   121,   100,   101,
    -124,  -124,  -124,   199,    78,    61,   275,   102,   213,   214,
     161,   162,   163,    62,    55,    29,   131,    58,   251,    30,
     200,   132,    31,    68,    56,   161,   162,   163,    63,    69,
     142,    32,    70,   200,   260,   143,   263,   264,   153,   154,
      59,   155,   268,   269,    33,    34,   292,   293,   294,   295,
      60,     1,    35,    36,    37,    38,     2,   182,    67,    72,
      75,   189,   183,     3,     4,   189,   283,   284,   285,   246,
     296,     5,    64,   204,   143,   297,    65,   205,   197,    31,
     201,   198,   221,   202,   299,   222,   189,   189,    32,    73,
      74,   288,   302,   303,   222,   235,   304,   305,   238,   115,
     116,    33,    34,   117,   118,   121,   307,   308,   122,    35,
      36,    37,    38,   123,   133,   316,   317,   318,   319,   136,
     321,   322,   138,   139,   140,   141,   143,   172,   148,   149,
     330,   150,   331,   151,   152,   333,   190,   177,   178,   336,
     338,   339,   187,   340,   179,    88,   188,   180,   181,    89,
     184,   186,   191,    90,   194,   203,   206,   210,   211,    91,
      92,    93,   224,    94,   217,   215,    95,    96,    97,    98,
      99,   231,   187,   100,   101,    88,   282,   216,   227,    89,
     220,   232,   102,    90,   237,   259,   226,   228,   271,    91,
      92,    93,   252,    94,   253,   262,    95,    96,    97,    98,
      99,    88,   110,   100,   101,    89,   234,   167,   241,    90,
     244,   248,   102,   249,   266,    91,    92,    93,   250,    94,
     255,   257,    95,    96,    97,    98,    99,    88,   173,   100,
     101,    89,   267,   270,   111,    90,   276,   272,   102,   273,
     274,    91,    92,    93,   277,    94,   278,   281,    95,    96,
      97,    98,    99,    88,   175,   100,   101,    89,   286,   287,
     111,    90,   289,   290,   102,   291,   300,    91,    92,    93,
     301,    94,   306,   309,    95,    96,    97,    98,    99,    88,
     225,   100,   101,    89,   310,   311,   111,    90,   313,   312,
     102,   314,   315,    91,    92,    93,   323,    94,   324,   325,
      95,    96,    97,    98,    99,    88,   326,   100,   101,    89,
     327,   328,   111,    90,   332,   329,   102,   334,   335,    91,
      92,    93,   337,    94,   341,   342,    95,    96,    97,    98,
      99,   343,    20,   100,   101,    -2,    19,    28,     1,    52,
      53,   265,   102,     2,     0,    66,   219,   176,    81,   130,
       3,     4,     0,     0,     0,     0,     0,     0,     5
};

static const short int yycheck[] =
{
      63,    96,   113,     1,    67,    41,     4,     5,     5,    73,
       8,    74,     7,     1,    12,     1,     3,     5,     0,     5,
      18,    19,    20,    13,    22,     4,   217,    25,    26,    27,
      28,    29,     7,   224,    32,    33,    34,    35,    36,    36,
     103,   104,   105,    41,    41,     1,    41,     4,    36,     5,
     113,    41,   115,    41,     1,    41,     1,     5,     5,     4,
       5,   252,   253,     8,    13,   176,    41,    12,   132,     5,
       9,   134,     6,    18,    19,    20,   140,    22,     8,     9,
      25,    26,    27,    28,    29,    41,    24,    32,    33,    34,
      35,    36,    41,    41,    41,     6,    41,     1,     5,    13,
       4,     5,   197,   198,     8,    41,   201,   202,    12,   204,
     205,     5,    36,   176,    18,    19,    20,    41,    22,   183,
      11,    25,    26,    27,    28,    29,     5,    41,    32,    33,
      34,    35,    36,    11,    41,     1,    14,    41,   158,   159,
      34,    35,    36,     9,    31,     1,     8,     4,   211,     5,
      41,    13,     8,     1,    41,    34,    35,    36,    24,     7,
      36,    17,    10,    41,   227,    41,   230,   231,     8,     9,
       4,    11,   236,   237,    30,    31,   271,   272,   273,   274,
       4,     3,    38,    39,    40,    41,     8,     8,     4,    41,
      41,   254,    13,    15,    16,   258,   259,   261,   262,    36,
      36,    23,     1,    10,    41,    41,     5,    14,    10,     8,
      10,    13,     7,    13,   277,    10,   279,   280,    17,     6,
      24,     7,   286,   287,    10,   193,   290,   291,   196,     4,
      41,    30,    31,    41,     8,    41,   300,   301,     7,    38,
      39,    40,    41,     8,    41,   309,   310,   311,   312,    41,
     314,   315,     6,     6,     6,     6,    41,    41,     8,     8,
     324,     8,   326,     6,     6,   329,     6,     8,     7,   332,
     334,   335,     1,   337,    41,     4,     5,    41,     8,     8,
      41,     8,     8,    12,    41,     8,     8,     6,    21,    18,
      19,    20,     4,    22,    11,    41,    25,    26,    27,    28,
      29,    11,     1,    32,    33,     4,     5,    41,    24,     8,
      41,     7,    41,    12,    11,    24,    41,    41,    10,    18,
      19,    20,    11,    22,    11,    11,    25,    26,    27,    28,
      29,     4,     5,    32,    33,     8,    41,    41,    41,    12,
      41,    41,    41,    41,     7,    18,    19,    20,    41,    22,
      41,    41,    25,    26,    27,    28,    29,     4,     5,    32,
      33,     8,     7,     7,    37,    12,     8,    13,    41,    10,
      13,    18,    19,    20,     7,    22,     8,     8,    25,    26,
      27,    28,    29,     4,     5,    32,    33,     8,    13,    11,
      37,    12,     4,    13,    41,    11,    13,    18,    19,    20,
      11,    22,    41,    13,    25,    26,    27,    28,    29,     4,
       5,    32,    33,     8,    13,    13,    37,    12,     8,    13,
      41,    13,    13,    18,    19,    20,     8,    22,    13,     7,
      25,    26,    27,    28,    29,     4,    13,    32,    33,     8,
      41,     8,    37,    12,     7,    13,    41,    13,    13,    18,
      19,    20,    13,    22,     8,     7,    25,    26,    27,    28,
      29,     8,     7,    32,    33,     0,     1,    17,     3,    26,
      28,   233,    41,     8,    -1,    39,   164,   115,    59,    85,
      15,    16,    -1,    -1,    -1,    -1,    -1,    -1,    23
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     3,     8,    15,    16,    23,    43,    44,    45,    47,
      48,    49,    36,    41,    46,    41,     4,     3,     0,     1,
      45,     4,    54,     6,     5,    41,    50,    51,    46,     1,
       5,     8,    17,    30,    31,    38,    39,    40,    41,    55,
      56,    58,    59,    92,    93,    97,     7,    41,    53,     9,
       1,     5,    51,    54,    24,    31,    41,     6,     4,     4,
       4,     1,     9,    24,     1,     5,    56,     4,     1,     7,
      10,    52,    41,     6,    24,    41,    67,     5,    41,    94,
       5,    94,     5,    36,    41,    95,    96,    57,     4,     8,
      12,    18,    19,    20,    22,    25,    26,    27,    28,    29,
      32,    33,    41,    61,    63,    65,    69,    71,    83,    85,
       5,    37,    71,    90,    91,     4,    41,    41,     8,    67,
      71,    41,     7,     8,     1,     5,    41,     5,     1,     5,
      96,     8,    13,    41,    60,    71,    41,    81,     6,     6,
       6,     6,    36,    41,    68,    79,    68,    80,     8,     8,
       8,     6,     6,     8,     9,    11,    84,    71,    71,    71,
       5,    34,    35,    36,    88,    89,     7,    41,    66,    87,
       8,     9,    41,     5,    91,     5,    90,     8,     7,    41,
      41,     8,     8,    13,    41,    67,     8,     1,     5,    71,
       6,     8,    70,    62,    41,    67,    64,    10,    13,    11,
      41,    10,    13,     8,    10,    14,     8,    73,    76,    72,
       6,    21,    78,    78,    78,    41,    41,    11,     5,    89,
      41,     7,    10,    77,     4,     5,    41,    24,    41,    67,
      13,    11,     7,    82,    41,    66,    13,    11,    66,    68,
      68,    41,    68,    68,    41,    68,    36,    68,    41,    41,
      41,    71,    11,    11,    60,    41,    86,    41,    60,    24,
      71,    13,    11,    67,    67,    87,     7,     7,    67,    67,
       7,    10,    13,    10,    13,    14,     8,     7,     8,    60,
      60,     8,     5,    71,    67,    67,    13,    11,     7,     4,
      13,    11,    68,    68,    68,    68,    36,    41,    74,    71,
      13,    11,    67,    67,    67,    67,    41,    67,    67,    13,
      13,    13,    13,     8,    13,    13,    67,    67,    67,    67,
      75,    67,    67,     8,    13,     7,    13,    41,     8,    13,
      67,    67,     7,    67,    13,    13,    71,    13,    67,    67,
      67,     8,     7,     8
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
#line 165 "ael.y"
        { free((yyvaluep->str));};
#line 1292 "ael.tab.c"
        break;
      case 44: /* "objects" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1300 "ael.tab.c"
        break;
      case 45: /* "object" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1308 "ael.tab.c"
        break;
      case 46: /* "word_or_default" */
#line 165 "ael.y"
        { free((yyvaluep->str));};
#line 1313 "ael.tab.c"
        break;
      case 47: /* "context" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1321 "ael.tab.c"
        break;
      case 48: /* "macro" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1329 "ael.tab.c"
        break;
      case 49: /* "globals" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1337 "ael.tab.c"
        break;
      case 50: /* "global_statements" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1345 "ael.tab.c"
        break;
      case 51: /* "global_statement" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1353 "ael.tab.c"
        break;
      case 53: /* "arglist" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1361 "ael.tab.c"
        break;
      case 54: /* "elements_block" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1369 "ael.tab.c"
        break;
      case 55: /* "elements" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1377 "ael.tab.c"
        break;
      case 56: /* "element" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1385 "ael.tab.c"
        break;
      case 58: /* "ignorepat" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1393 "ael.tab.c"
        break;
      case 59: /* "extension" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1401 "ael.tab.c"
        break;
      case 60: /* "statements" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1409 "ael.tab.c"
        break;
      case 61: /* "if_head" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1417 "ael.tab.c"
        break;
      case 63: /* "random_head" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1425 "ael.tab.c"
        break;
      case 65: /* "iftime_head" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1433 "ael.tab.c"
        break;
      case 66: /* "word_list" */
#line 165 "ael.y"
        { free((yyvaluep->str));};
#line 1438 "ael.tab.c"
        break;
      case 67: /* "word3_list" */
#line 165 "ael.y"
        { free((yyvaluep->str));};
#line 1443 "ael.tab.c"
        break;
      case 68: /* "goto_word" */
#line 165 "ael.y"
        { free((yyvaluep->str));};
#line 1448 "ael.tab.c"
        break;
      case 69: /* "switch_head" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1456 "ael.tab.c"
        break;
      case 71: /* "statement" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1464 "ael.tab.c"
        break;
      case 78: /* "opt_else" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1472 "ael.tab.c"
        break;
      case 79: /* "target" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1480 "ael.tab.c"
        break;
      case 80: /* "jumptarget" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1488 "ael.tab.c"
        break;
      case 81: /* "macro_call" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1496 "ael.tab.c"
        break;
      case 83: /* "application_call_head" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1504 "ael.tab.c"
        break;
      case 85: /* "application_call" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1512 "ael.tab.c"
        break;
      case 86: /* "opt_word" */
#line 165 "ael.y"
        { free((yyvaluep->str));};
#line 1517 "ael.tab.c"
        break;
      case 87: /* "eval_arglist" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1525 "ael.tab.c"
        break;
      case 88: /* "case_statements" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1533 "ael.tab.c"
        break;
      case 89: /* "case_statement" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1541 "ael.tab.c"
        break;
      case 90: /* "macro_statements" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1549 "ael.tab.c"
        break;
      case 91: /* "macro_statement" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1557 "ael.tab.c"
        break;
      case 92: /* "switches" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1565 "ael.tab.c"
        break;
      case 93: /* "eswitches" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1573 "ael.tab.c"
        break;
      case 94: /* "switchlist" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1581 "ael.tab.c"
        break;
      case 95: /* "includeslist" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1589 "ael.tab.c"
        break;
      case 96: /* "includedname" */
#line 165 "ael.y"
        { free((yyvaluep->str));};
#line 1594 "ael.tab.c"
        break;
      case 97: /* "includes" */
#line 152 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1602 "ael.tab.c"
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
#line 170 "ael.y"
    { (yyval.pval) = parseio->pval = (yyvsp[0].pval); ;}
    break;

  case 3:
#line 173 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 4:
#line 175 "ael.y"
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
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 13:
#line 202 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.abstract = 1; ;}
    break;

  case 14:
#line 209 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-6].str); (yyval.pval)->u2.arglist = (yyvsp[-4].pval); (yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 15:
#line 212 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-5].str); (yyval.pval)->u2.arglist = (yyvsp[-3].pval); ;}
    break;

  case 16:
#line 215 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 17:
#line 219 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str); ;}
    break;

  case 18:
#line 224 "ael.y"
    {
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.statements = (yyvsp[-1].pval);;}
    break;

  case 19:
#line 227 "ael.y"
    { /* empty globals is OK */
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[-2]), &(yylsp[0])); ;}
    break;

  case 20:
#line 231 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 21:
#line 232 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));;}
    break;

  case 22:
#line 233 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 23:
#line 236 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 24:
#line 236 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 25:
#line 242 "ael.y"
    {
		(yyval.pval)= npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str); ;}
    break;

  case 26:
#line 245 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[0]));
		z->u1.str = (yyvsp[0].str);
		(yyval.pval) = linku1((yyvsp[-2].pval),z); ;}
    break;

  case 27:
#line 249 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 28:
#line 252 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 29:
#line 253 "ael.y"
    { (yyval.pval) = (yyvsp[-1].pval); ;}
    break;

  case 30:
#line 256 "ael.y"
    { (yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 31:
#line 257 "ael.y"
    {(yyval.pval)=0;;}
    break;

  case 32:
#line 258 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
				else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
				else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 33:
#line 261 "ael.y"
    { (yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 34:
#line 264 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 35:
#line 265 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 36:
#line 266 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 37:
#line 267 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 38:
#line 268 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 39:
#line 269 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 40:
#line 269 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 41:
#line 273 "ael.y"
    {free((yyvsp[-1].str)); (yyval.pval)=0;;}
    break;

  case 42:
#line 274 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 43:
#line 277 "ael.y"
    {
		(yyval.pval) = npval2(PV_IGNOREPAT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 44:
#line 282 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 45:
#line 286 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;;}
    break;

  case 46:
#line 291 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 47:
#line 296 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 48:
#line 305 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 49:
#line 306 "ael.y"
    {if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 50:
#line 309 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 51:
#line 312 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 52:
#line 312 "ael.y"
    {
		(yyval.pval)= npval2(PV_IF, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 53:
#line 317 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 54:
#line 317 "ael.y"
    {
		(yyval.pval) = npval2(PV_RANDOM, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str=(yyvsp[-1].str);;}
    break;

  case 55:
#line 323 "ael.y"
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

  case 56:
#line 338 "ael.y"
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

  case 57:
#line 358 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 58:
#line 359 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 59:
#line 366 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 60:
#line 367 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 61:
#line 372 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s%s", (yyvsp[-2].str), (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word=(yyval.str);;}
    break;

  case 62:
#line 380 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 63:
#line 381 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));;}
    break;

  case 64:
#line 385 "ael.y"
    {
		asprintf(&((yyval.str)), "%s:%s", (yyvsp[-2].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[0].str));;}
    break;

  case 65:
#line 391 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 66:
#line 391 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCH, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 67:
#line 399 "ael.y"
    {
		(yyval.pval) = npval2(PV_STATEMENTBLOCK, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 68:
#line 402 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 69:
#line 402 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 70:
#line 406 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 71:
#line 409 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 72:
#line 412 "ael.y"
    {
		(yyval.pval) = npval2(PV_LABEL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 73:
#line 415 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 74:
#line 416 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 75:
#line 417 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 76:
#line 417 "ael.y"
    {
		(yyval.pval) = npval2(PV_FOR, &(yylsp[-11]), &(yylsp[0]));
		(yyval.pval)->u1.for_init = (yyvsp[-8].str);
		(yyval.pval)->u2.for_test=(yyvsp[-5].str);
		(yyval.pval)->u3.for_inc = (yyvsp[-2].str);
		(yyval.pval)->u4.for_statements = (yyvsp[0].pval);;}
    break;

  case 77:
#line 423 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 78:
#line 423 "ael.y"
    {
		(yyval.pval) = npval2(PV_WHILE, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 79:
#line 427 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 80:
#line 429 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[0]));
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 81:
#line 432 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[-1])); ;}
    break;

  case 82:
#line 434 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 83:
#line 436 "ael.y"
    {
		(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
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

  case 86:
#line 472 "ael.y"
    { (yyval.pval) = npval2(PV_BREAK, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 87:
#line 473 "ael.y"
    { (yyval.pval) = npval2(PV_RETURN, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 88:
#line 474 "ael.y"
    { (yyval.pval) = npval2(PV_CONTINUE, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 89:
#line 475 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[-1])); /* XXX probably @3... */
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 90:
#line 479 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[-1])); /* XXX probably @3... */
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 91:
#line 483 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[-1])); /* XXX probably @3... */
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 92:
#line 487 "ael.y"
    { (yyval.pval)=0; ;}
    break;

  case 93:
#line 490 "ael.y"
    { (yyval.pval) = (yyvsp[0].pval); ;}
    break;

  case 94:
#line 491 "ael.y"
    { (yyval.pval) = NULL ; ;}
    break;

  case 95:
#line 496 "ael.y"
    { (yyval.pval) = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str);;}
    break;

  case 96:
#line 498 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 97:
#line 503 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 98:
#line 508 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 99:
#line 515 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 100:
#line 522 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 101:
#line 529 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 102:
#line 538 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0])); /* XXX not really @1 */
		(yyval.pval)->next->u1.str = strdup("1");;}
    break;

  case 103:
#line 543 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 104:
#line 548 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[-2].str); ;}
    break;

  case 105:
#line 555 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = strdup("1"); ;}
    break;

  case 106:
#line 562 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[-2].str); ;}
    break;

  case 107:
#line 569 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = strdup("1"); ;}
    break;

  case 108:
#line 578 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 109:
#line 578 "ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		(yyval.pval) = npval2(PV_MACRO_CALL, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.arglist = (yyvsp[-1].pval);;}
    break;

  case 110:
#line 583 "ael.y"
    {
		(yyval.pval)= npval2(PV_MACRO_CALL, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 111:
#line 588 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 112:
#line 588 "ael.y"
    {
		if (strcasecmp((yyvsp[-2].str),"goto") == 0) {
			(yyval.pval)= npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
			free((yyvsp[-2].str)); /* won't be using this */
			ast_log(LOG_WARNING, "==== File: %s, Line %d, Cols: %d-%d: Suggestion: Use the goto statement instead of the Goto() application call in AEL.\n", my_file, (yylsp[-2]).first_line, (yylsp[-2]).first_column, (yylsp[-2]).last_column );
		} else
			(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 113:
#line 598 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[0]));
 		if( (yyval.pval)->type == PV_GOTO )
			(yyval.pval)->u1.list = (yyvsp[-1].pval);
	 	else
			(yyval.pval)->u2.arglist = (yyvsp[-1].pval);
	;}
    break;

  case 114:
#line 605 "ael.y"
    { (yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 115:
#line 608 "ael.y"
    { (yyval.str) = (yyvsp[0].str) ;}
    break;

  case 116:
#line 609 "ael.y"
    { (yyval.str) = strdup(""); ;}
    break;

  case 117:
#line 612 "ael.y"
    { 
		(yyval.pval)= npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str);;}
    break;

  case 118:
#line 615 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		(yyval.pval)->u1.str = strdup(""); ;}
    break;

  case 119:
#line 618 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval) = (yyvsp[-2].pval);
		linku1((yyvsp[-2].pval),z);
		z->u1.str = (yyvsp[0].str);;}
    break;

  case 120:
#line 625 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 121:
#line 626 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 122:
#line 631 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-3]), &(yylsp[-1])); /* XXX 3 or 4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 123:
#line 635 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 124:
#line 639 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-3]), &(yylsp[0])); /* XXX@3 or @4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 125:
#line 643 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 126:
#line 646 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;;}
    break;

  case 127:
#line 649 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 128:
#line 654 "ael.y"
    {(yyval.pval) = (yyvsp[0].pval);;}
    break;

  case 129:
#line 655 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 130:
#line 660 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 131:
#line 661 "ael.y"
    {
		(yyval.pval) = npval2(PV_CATCH, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 132:
#line 667 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 133:
#line 670 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[-2]), &(yylsp[0])); ;}
    break;

  case 134:
#line 674 "ael.y"
    {
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 135:
#line 677 "ael.y"
    { /* empty switch list OK */
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[-2]), &(yylsp[0])); ;}
    break;

  case 136:
#line 681 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 137:
#line 684 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[0]));
		z->u1.str = (yyvsp[-1].str);
		(yyval.pval)=(yyvsp[-2].pval);
		linku1((yyval.pval),z); ;}
    break;

  case 138:
#line 689 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 139:
#line 692 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 140:
#line 696 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-13]), &(yylsp[-12]));
		(yyval.pval)->u1.str = (yyvsp[-13].str);
		(yyval.pval)->u2.arglist = npval2(PV_WORD, &(yylsp[-11]), &(yylsp[-7]));
		asprintf( &((yyval.pval)->u2.arglist->u1.str), "%s:%s:%s", (yyvsp[-11].str), (yyvsp[-9].str), (yyvsp[-7].str));
		free((yyvsp[-11].str));
		free((yyvsp[-9].str));
		free((yyvsp[-7].str));
		(yyval.pval)->u2.arglist->next = npval2(PV_WORD, &(yylsp[-5]), &(yylsp[-5]));
		(yyval.pval)->u2.arglist->next->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u2.arglist->next->next = npval2(PV_WORD, &(yylsp[-3]), &(yylsp[-3]));
		(yyval.pval)->u2.arglist->next->next->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.arglist->next->next->next = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[-1]));
		(yyval.pval)->u2.arglist->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word=0;
	;}
    break;

  case 141:
#line 712 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-9]), &(yylsp[-8]));
		(yyval.pval)->u1.str = (yyvsp[-9].str);
		(yyval.pval)->u2.arglist = npval2(PV_WORD, &(yylsp[-7]), &(yylsp[-7]));
		(yyval.pval)->u2.arglist->u1.str = (yyvsp[-7].str);
		(yyval.pval)->u2.arglist->next = npval2(PV_WORD, &(yylsp[-5]), &(yylsp[-5]));
		(yyval.pval)->u2.arglist->next->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u2.arglist->next->next = npval2(PV_WORD, &(yylsp[-3]), &(yylsp[-3]));
		(yyval.pval)->u2.arglist->next->next->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.arglist->next->next->next = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[-1]));
		(yyval.pval)->u2.arglist->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word=0;
	;}
    break;

  case 142:
#line 725 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[0])); /* XXX don't we need @1-@3 ?*/
		(yyval.pval)=(yyvsp[-2].pval);
		z->u1.str = (yyvsp[-1].str);
		linku1((yyval.pval),z); ;}
    break;

  case 143:
#line 731 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-13]), &(yylsp[-12]));
		(yyval.pval)=(yyvsp[-14].pval); z->u1.str = (yyvsp[-13].str);
		linku1((yyval.pval),z);
		z->u2.arglist = npval2(PV_WORD, &(yylsp[-11]), &(yylsp[-11]));
		asprintf( &((yyval.pval)->u2.arglist->u1.str), "%s:%s:%s", (yyvsp[-11].str), (yyvsp[-9].str), (yyvsp[-7].str));
		free((yyvsp[-11].str));
		free((yyvsp[-9].str));
		free((yyvsp[-7].str));
		z->u2.arglist->next = npval2(PV_WORD, &(yylsp[-5]), &(yylsp[-5]));
		z->u2.arglist->next->u1.str = (yyvsp[-5].str);
		z->u2.arglist->next->next = npval2(PV_WORD, &(yylsp[-3]), &(yylsp[-3]));
		z->u2.arglist->next->next->u1.str = (yyvsp[-3].str);
		z->u2.arglist->next->next->next = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[-1]));
		z->u2.arglist->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word=0;
	;}
    break;

  case 144:
#line 748 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-9]), &(yylsp[-8]));
		(yyval.pval)=(yyvsp[-10].pval);
		linku1((yyval.pval),z);
		(yyval.pval)->u2.arglist->u1.str = (yyvsp[-7].str);
		z->u1.str = (yyvsp[-9].str);
		z->u2.arglist = npval2(PV_WORD, &(yylsp[-7]), &(yylsp[-7]));	/* XXX is this correct ? */
		z->u2.arglist->next = npval2(PV_WORD, &(yylsp[-5]), &(yylsp[-5]));
		z->u2.arglist->next->u1.str = (yyvsp[-5].str);
		z->u2.arglist->next->next = npval2(PV_WORD, &(yylsp[-3]), &(yylsp[-3]));
		z->u2.arglist->next->next->u1.str = (yyvsp[-3].str);
		z->u2.arglist->next->next->next = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[-1]));
		z->u2.arglist->next->next->next->u1.str = (yyvsp[-1].str);
		prev_word=0;
	;}
    break;

  case 145:
#line 763 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 146:
#line 766 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 147:
#line 767 "ael.y"
    {(yyval.str)=strdup("default");;}
    break;

  case 148:
#line 770 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 149:
#line 773 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-2]), &(yylsp[0]));;}
    break;


      default: break;
    }

/* Line 1126 of yacc.c.  */
#line 3033 "ael.tab.c"

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


#line 778 "ael.y"


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

static struct pval *update_last(pval *obj, YYLTYPE *last)
{
	obj->endline = last->last_line;
	obj->endcol = last->last_column;
	return obj;
}

/* append second element to the list in the first one */
static pval * linku1(pval *head, pval *tail)
{
	if (!head)
		return tail;
	if (!head->next) {
		head->next = tail;
	} else {
		head->u1_last->next = tail;
	}
	head->u1_last = tail;
	return head;
}


