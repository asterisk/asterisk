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
#define YYLAST   510

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  55
/* YYNRULES -- Number of rules. */
#define YYNRULES  146
/* YYNRULES -- Number of states. */
#define YYNSTATES  332

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
      21,    23,    25,    30,    32,    33,    42,    50,    58,    65,
      70,    74,    76,    79,    82,    83,    89,    91,    95,    98,
     101,   105,   107,   109,   112,   115,   117,   119,   121,   123,
     125,   126,   132,   135,   137,   142,   146,   151,   159,   168,
     170,   173,   176,   177,   183,   184,   190,   205,   216,   218,
     221,   223,   226,   230,   232,   235,   239,   240,   247,   251,
     252,   258,   262,   266,   269,   270,   271,   272,   285,   286,
     293,   296,   300,   304,   307,   310,   311,   317,   320,   323,
     326,   330,   332,   335,   336,   338,   342,   346,   352,   358,
     364,   370,   372,   376,   382,   386,   392,   396,   397,   403,
     407,   408,   412,   416,   419,   421,   422,   424,   425,   429,
     431,   434,   439,   443,   448,   452,   455,   459,   461,   464,
     466,   472,   475,   478,   482,   485,   488,   492,   495,   498,
     513,   524,   528,   544,   556,   559,   564
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      43,     0,    -1,    44,    -1,    45,    -1,    44,    45,    -1,
      44,     1,    -1,    47,    -1,    49,    -1,    50,    -1,     8,
      -1,    41,    -1,    36,    -1,    48,     3,    46,    55,    -1,
      23,    -1,    -1,    15,    41,     6,    54,     7,     4,    89,
       5,    -1,    15,    41,     6,    54,     7,     4,     5,    -1,
      15,    41,     6,     7,     4,    89,     5,    -1,    15,    41,
       6,     7,     4,     5,    -1,    16,     4,    51,     5,    -1,
      16,     4,     5,    -1,    52,    -1,    51,    52,    -1,    51,
       1,    -1,    -1,    41,     9,    53,    41,     8,    -1,    41,
      -1,    54,    10,    41,    -1,    54,     1,    -1,     4,     5,
      -1,     4,    56,     5,    -1,    57,    -1,     1,    -1,    56,
      57,    -1,    56,     1,    -1,    60,    -1,    96,    -1,    91,
      -1,    92,    -1,    59,    -1,    -1,    41,     9,    58,    41,
       8,    -1,    41,     1,    -1,     8,    -1,    17,    24,    41,
       8,    -1,    41,    24,    70,    -1,    30,    41,    24,    70,
      -1,    31,     6,    66,     7,    41,    24,    70,    -1,    30,
      31,     6,    66,     7,    41,    24,    70,    -1,    70,    -1,
      61,    70,    -1,    61,     1,    -1,    -1,    19,     6,    63,
      65,     7,    -1,    -1,    22,     6,    64,    65,     7,    -1,
      20,     6,    66,    11,    66,    11,    66,    13,    66,    13,
      66,    13,    66,     7,    -1,    20,     6,    41,    13,    66,
      13,    66,    13,    66,     7,    -1,    41,    -1,    41,    41,
      -1,    41,    -1,    41,    41,    -1,    41,    41,    41,    -1,
      41,    -1,    41,    41,    -1,    41,    11,    41,    -1,    -1,
      18,     6,    69,    41,     7,     4,    -1,     4,    61,     5,
      -1,    -1,    41,     9,    71,    41,     8,    -1,    25,    78,
       8,    -1,    26,    79,     8,    -1,    41,    11,    -1,    -1,
      -1,    -1,    32,     6,    72,    41,     8,    73,    41,     8,
      74,    41,     7,    70,    -1,    -1,    33,     6,    75,    41,
       7,    70,    -1,    68,     5,    -1,    68,    87,     5,    -1,
      12,    80,     8,    -1,    84,     8,    -1,    41,     8,    -1,
      -1,    84,     9,    76,    41,     8,    -1,    28,     8,    -1,
      27,     8,    -1,    29,     8,    -1,    62,    70,    77,    -1,
       8,    -1,    21,    70,    -1,    -1,    67,    -1,    67,    13,
      67,    -1,    67,    10,    67,    -1,    67,    13,    67,    13,
      67,    -1,    67,    10,    67,    10,    67,    -1,    36,    13,
      67,    13,    67,    -1,    36,    10,    67,    10,    67,    -1,
      67,    -1,    67,    10,    67,    -1,    67,    10,    41,    14,
      41,    -1,    67,    14,    67,    -1,    67,    10,    41,    14,
      36,    -1,    67,    14,    36,    -1,    -1,    41,     6,    81,
      86,     7,    -1,    41,     6,     7,    -1,    -1,    41,     6,
      83,    -1,    82,    86,     7,    -1,    82,     7,    -1,    41,
      -1,    -1,    65,    -1,    -1,    86,    10,    85,    -1,    88,
      -1,    87,    88,    -1,    34,    41,    11,    61,    -1,    36,
      11,    61,    -1,    35,    41,    11,    61,    -1,    34,    41,
      11,    -1,    36,    11,    -1,    35,    41,    11,    -1,    90,
      -1,    89,    90,    -1,    70,    -1,    37,    41,     4,    61,
       5,    -1,    38,    93,    -1,    39,    93,    -1,     4,    94,
       5,    -1,     4,     5,    -1,    41,     8,    -1,    94,    41,
       8,    -1,    94,     1,    -1,    46,     8,    -1,    46,    13,
      66,    11,    66,    11,    66,    13,    66,    13,    66,    13,
      66,     8,    -1,    46,    13,    41,    13,    66,    13,    66,
      13,    66,     8,    -1,    95,    46,     8,    -1,    95,    46,
      13,    66,    11,    66,    11,    66,    13,    66,    13,    66,
      13,    66,     8,    -1,    95,    46,    13,    41,    13,    66,
      13,    66,    13,    66,     8,    -1,    95,     1,    -1,    40,
       4,    95,     5,    -1,    40,     4,     5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   173,   173,   176,   177,   178,   181,   182,   183,   184,
     187,   188,   191,   199,   200,   203,   206,   209,   213,   218,
     221,   225,   226,   227,   230,   230,   236,   237,   238,   241,
     242,   245,   246,   247,   248,   251,   252,   253,   254,   255,
     256,   256,   260,   261,   264,   269,   273,   278,   283,   292,
     293,   294,   298,   298,   301,   301,   304,   317,   333,   334,
     341,   342,   347,   355,   356,   360,   366,   366,   374,   377,
     377,   381,   384,   387,   390,   391,   392,   390,   398,   398,
     402,   404,   407,   409,   411,   414,   414,   447,   448,   449,
     450,   454,   457,   458,   463,   464,   467,   470,   474,   478,
     482,   489,   492,   495,   499,   503,   507,   513,   513,   518,
     526,   526,   537,   544,   547,   548,   551,   552,   555,   558,
     559,   562,   566,   570,   574,   577,   580,   585,   586,   589,
     590,   596,   601,   606,   607,   610,   611,   612,   615,   616,
     629,   637,   638,   652,   663,   666,   669
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
  "objects", "object", "word_or_default", "context", "opt_abstract",
  "macro", "globals", "global_statements", "global_statement", "@1",
  "arglist", "elements_block", "elements", "element", "@2", "ignorepat",
  "extension", "statements", "if_like_head", "@3", "@4", "word_list",
  "word3_list", "goto_word", "switch_head", "@5", "statement", "@6", "@7",
  "@8", "@9", "@10", "@11", "opt_else", "target", "jumptarget",
  "macro_call", "@12", "application_call_head", "@13", "application_call",
  "opt_word", "eval_arglist", "case_statements", "case_statement",
  "macro_statements", "macro_statement", "switches", "eswitches",
  "switchlist_block", "switchlist", "includeslist", "includes", 0
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
      46,    46,    47,    48,    48,    49,    49,    49,    49,    50,
      50,    51,    51,    51,    53,    52,    54,    54,    54,    55,
      55,    56,    56,    56,    56,    57,    57,    57,    57,    57,
      58,    57,    57,    57,    59,    60,    60,    60,    60,    61,
      61,    61,    63,    62,    64,    62,    62,    62,    65,    65,
      66,    66,    66,    67,    67,    67,    69,    68,    70,    71,
      70,    70,    70,    70,    72,    73,    74,    70,    75,    70,
      70,    70,    70,    70,    70,    76,    70,    70,    70,    70,
      70,    70,    77,    77,    78,    78,    78,    78,    78,    78,
      78,    79,    79,    79,    79,    79,    79,    81,    80,    80,
      83,    82,    84,    84,    85,    85,    86,    86,    86,    87,
      87,    88,    88,    88,    88,    88,    88,    89,    89,    90,
      90,    91,    92,    93,    93,    94,    94,    94,    95,    95,
      95,    95,    95,    95,    95,    96,    96
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     1,     4,     1,     0,     8,     7,     7,     6,     4,
       3,     1,     2,     2,     0,     5,     1,     3,     2,     2,
       3,     1,     1,     2,     2,     1,     1,     1,     1,     1,
       0,     5,     2,     1,     4,     3,     4,     7,     8,     1,
       2,     2,     0,     5,     0,     5,    14,    10,     1,     2,
       1,     2,     3,     1,     2,     3,     0,     6,     3,     0,
       5,     3,     3,     2,     0,     0,     0,    12,     0,     6,
       2,     3,     3,     2,     2,     0,     5,     2,     2,     2,
       3,     1,     2,     0,     1,     3,     3,     5,     5,     5,
       5,     1,     3,     5,     3,     5,     3,     0,     5,     3,
       0,     3,     3,     2,     1,     0,     1,     0,     3,     1,
       2,     4,     3,     4,     3,     2,     3,     1,     2,     1,
       5,     2,     2,     3,     2,     2,     3,     2,     2,    14,
      10,     3,    15,    11,     2,     4,     3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
      14,     9,     0,     0,    13,     0,     0,     3,     6,     0,
       7,     8,     0,     0,     1,     5,     4,     0,     0,    20,
       0,     0,    21,    11,    10,     0,     0,    26,     0,    24,
      23,    19,    22,     0,    12,     0,    28,     0,     0,     0,
      32,    29,    43,     0,     0,     0,     0,     0,     0,     0,
       0,    31,    39,    35,    37,    38,    36,     0,    18,    91,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,   129,   117,     0,     0,
     127,     0,    27,     0,     0,     0,     0,     0,     0,   131,
     132,     0,    42,    40,     0,    34,    30,    33,     0,    49,
       0,     0,    66,    52,     0,    54,     0,    63,    94,     0,
     101,     0,    88,    87,    89,    74,    78,     0,   110,    84,
      69,    73,    93,    80,     0,     0,     0,     0,   119,   113,
      58,   116,     0,    83,    85,    17,   128,    16,     0,    25,
       0,     0,     0,    60,     0,   134,     0,     0,   146,     0,
       0,     0,    45,    51,    68,    50,   107,    82,     0,     0,
      60,     0,     0,     0,     0,     0,    64,     0,     0,    71,
       0,     0,    72,     0,     0,     0,   111,     0,     0,    90,
       0,     0,   125,    81,   120,    59,   112,   115,     0,    15,
      44,     0,    46,    61,     0,   135,   137,   133,     0,   138,
       0,   144,   145,     0,     0,   109,   117,     0,     0,     0,
       0,     0,     0,     0,    65,    96,    95,    63,   102,   106,
     104,     0,     0,     0,     0,    92,   124,   126,     0,   114,
     118,     0,     0,    62,     0,   136,    60,     0,   141,     0,
      41,     0,     0,    53,     0,     0,    55,     0,     0,     0,
       0,     0,    75,     0,   130,    70,     0,     0,    86,     0,
       0,     0,     0,    60,     0,   108,    67,     0,     0,   100,
      99,    98,    97,   105,   103,     0,    79,     0,    47,     0,
       0,     0,     0,     0,     0,     0,    48,     0,     0,     0,
       0,     0,     0,    76,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    57,     0,     0,     0,     0,
       0,     0,     0,     0,   140,     0,     0,     0,     0,    77,
       0,   143,     0,     0,     0,     0,    56,     0,     0,   139,
       0,   142
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     5,     6,     7,    25,     8,     9,    10,    11,    21,
      22,    39,    28,    34,    50,    51,   151,    52,    53,    98,
      74,   159,   162,   131,   144,   108,    75,   158,    99,   177,
     173,   275,   300,   174,   188,   179,   109,   111,   101,   206,
      77,   176,    78,   230,   132,   127,   128,    79,    80,    54,
      55,    89,   147,   150,    56
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -173
static const short int yypact[] =
{
     117,  -173,   -23,     2,  -173,    27,   164,  -173,  -173,    55,
    -173,  -173,    72,    10,  -173,  -173,  -173,    70,     7,  -173,
      76,    33,  -173,  -173,  -173,    87,   104,  -173,   146,  -173,
    -173,  -173,  -173,   322,  -173,   365,  -173,   112,    79,    81,
    -173,  -173,  -173,   100,    41,   124,   135,   135,   145,    19,
     327,  -173,  -173,  -173,  -173,  -173,  -173,   469,  -173,  -173,
     119,   160,   169,   201,   202,   116,   157,   143,   204,   205,
     203,   208,   176,   175,   469,    65,  -173,    34,   195,   391,
    -173,   417,  -173,   215,   187,   210,   206,   188,    12,  -173,
    -173,    -1,  -173,  -173,   469,  -173,  -173,  -173,     4,  -173,
     220,   224,  -173,  -173,   193,  -173,    54,     8,   113,   227,
      11,   228,  -173,  -173,  -173,  -173,  -173,   239,  -173,  -173,
    -173,  -173,   223,  -173,   209,   212,   234,   110,  -173,  -173,
     213,  -173,   185,  -173,  -173,  -173,  -173,  -173,   443,  -173,
     238,   188,   469,   216,   240,  -173,   241,    51,  -173,   155,
       6,   217,  -173,  -173,  -173,  -173,   249,  -173,   219,   221,
      63,   250,   221,   157,   157,   222,  -173,   157,   157,  -173,
     229,   133,  -173,   230,   236,   469,  -173,   243,   469,  -173,
     253,   257,   469,  -173,  -173,  -173,  -173,   244,   246,  -173,
    -173,   284,  -173,   251,   254,  -173,  -173,  -173,   286,  -173,
     255,  -173,  -173,   165,   290,  -173,   221,   292,   293,   188,
     188,   294,   296,   289,  -173,   298,   302,    57,  -173,  -173,
    -173,   301,   309,   109,   314,  -173,   469,   469,    61,  -173,
    -173,   316,   288,  -173,   307,  -173,    64,   323,  -173,   295,
    -173,   192,   321,  -173,   320,   326,  -173,   157,   157,   157,
     157,   153,  -173,   469,  -173,  -173,   247,   285,  -173,   317,
     469,   188,   188,    71,   329,  -173,  -173,   188,   188,  -173,
    -173,  -173,  -173,  -173,  -173,   297,  -173,   469,  -173,   330,
     331,   188,   188,   332,   333,   339,  -173,   188,   188,   335,
     338,   188,   188,  -173,   337,   341,   188,   188,   344,   342,
     315,   188,   188,   346,   351,  -173,   188,   364,   366,   359,
     188,   188,   362,   469,  -173,   188,   368,   367,   188,  -173,
     369,  -173,   188,   371,   188,   373,  -173,   380,   188,  -173,
     381,  -173
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -173,  -173,  -173,   375,   -90,  -173,  -173,  -173,  -173,  -173,
     358,  -173,  -173,  -173,  -173,   350,  -173,  -173,  -173,  -172,
    -173,  -173,  -173,    23,   -91,    -9,  -173,  -173,   -35,  -173,
    -173,  -173,  -173,  -173,  -173,  -173,  -173,  -173,  -173,  -173,
    -173,  -173,  -173,  -173,   198,  -173,   274,   324,   -77,  -173,
    -173,   360,  -173,  -173,  -173
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -124
static const short int yytable[] =
{
      76,   149,   136,   223,   148,   153,    13,   201,    57,   154,
     228,   202,    59,   161,    26,    19,    60,   145,    12,   165,
      92,   170,    61,    62,    63,   171,    64,    14,    93,    65,
      66,    67,    68,    69,    30,    23,    70,    71,    31,   122,
      24,   129,    23,    94,    76,    73,    76,    24,    27,   166,
     191,    20,   196,   146,   256,   257,   197,   110,    17,   152,
     203,   136,   153,   155,   163,    57,  -122,   164,   165,    59,
     123,   251,    85,    60,    20,   130,   209,   261,    18,    61,
      62,    63,    86,    64,   281,    29,    65,    66,    67,    68,
      69,    33,   198,    70,    71,  -122,  -122,  -122,   166,   124,
     125,   126,    73,    76,   193,   193,    23,   192,    35,   237,
     153,    24,   193,    57,   254,   183,    81,    59,   244,   245,
      82,    60,    83,   167,    84,     1,   168,    61,    62,    63,
      87,    64,     2,     3,    65,    66,    67,    68,    69,    88,
       4,    70,    71,   225,   124,   125,   126,    36,   264,    91,
      73,   112,   106,    37,   212,   213,    38,   107,   215,   216,
     100,   218,   220,   199,    -2,    15,   102,   -14,   200,   219,
     279,   280,     1,   238,   107,   103,   283,   284,   239,     2,
       3,   118,   208,   119,   120,   211,   121,     4,   155,   273,
     289,   290,   186,   155,   274,   187,   294,   295,   107,   265,
     298,   299,   187,   133,   134,   303,   304,   104,   105,   115,
     308,   309,   113,   114,   116,   312,   141,   117,   276,   316,
     317,   155,   155,   139,   320,   278,   156,   323,   140,   143,
     142,   325,   157,   327,   160,   169,   172,   330,   269,   270,
     271,   272,   286,   175,   178,   182,   190,   194,   153,   195,
     180,    57,  -121,   181,   185,    59,   205,   193,   204,    60,
     207,   210,   130,   214,   226,    61,    62,    63,   227,    64,
     217,   221,    65,    66,    67,    68,    69,   222,   319,    70,
      71,  -121,  -121,  -121,   224,   229,   153,   231,    73,    57,
    -123,   232,   233,    59,   235,   234,   236,    60,   240,   242,
     243,   246,   248,    61,    62,    63,   247,    64,   249,   252,
      65,    66,    67,    68,    69,   250,   253,    70,    71,  -123,
    -123,  -123,   255,    40,   258,   266,    73,    41,    95,   259,
      42,   260,    96,   267,   262,    42,   263,   268,   285,    43,
     282,   277,   288,   287,    43,   291,   292,   293,   296,   297,
     301,   305,    44,    45,   302,   306,   307,    44,    45,   310,
      46,    47,    48,    49,   311,    46,    47,    48,    49,    57,
      58,   313,   315,    59,   314,   318,   321,    60,   326,    32,
     322,    16,   324,    61,    62,    63,   328,    64,   329,   331,
      65,    66,    67,    68,    69,    57,   135,    70,    71,    59,
      97,   184,    72,    60,   241,   138,    73,    90,     0,    61,
      62,    63,     0,    64,     0,     0,    65,    66,    67,    68,
      69,    57,   137,    70,    71,    59,     0,     0,    72,    60,
       0,     0,    73,     0,     0,    61,    62,    63,     0,    64,
       0,     0,    65,    66,    67,    68,    69,    57,   189,    70,
      71,    59,     0,     0,    72,    60,     0,     0,    73,     0,
       0,    61,    62,    63,     0,    64,     0,     0,    65,    66,
      67,    68,    69,    57,     0,    70,    71,    59,     0,     0,
      72,    60,     0,     0,    73,     0,     0,    61,    62,    63,
       0,    64,     0,     0,    65,    66,    67,    68,    69,     0,
       0,    70,    71,     0,     0,     0,     0,     0,     0,     0,
      73
};

static const short int yycheck[] =
{
      35,    91,    79,   175,     5,     1,     4,     1,     4,     5,
     182,     5,     8,   104,     7,     5,    12,     5,    41,    11,
       1,    10,    18,    19,    20,    14,    22,     0,     9,    25,
      26,    27,    28,    29,     1,    36,    32,    33,     5,    74,
      41,     7,    36,    24,    79,    41,    81,    41,    41,    41,
     141,    41,     1,    41,   226,   227,     5,    66,     3,    94,
     150,   138,     1,    98,    10,     4,     5,    13,    11,     8,
       5,    14,    31,    12,    41,    41,    13,    13,     6,    18,
      19,    20,    41,    22,    13,     9,    25,    26,    27,    28,
      29,     4,    41,    32,    33,    34,    35,    36,    41,    34,
      35,    36,    41,   138,    41,    41,    36,   142,     4,   200,
       1,    41,    41,     4,     5,     5,     4,     8,   209,   210,
      41,    12,    41,    10,    24,     8,    13,    18,    19,    20,
       6,    22,    15,    16,    25,    26,    27,    28,    29,     4,
      23,    32,    33,   178,    34,    35,    36,     1,   239,     4,
      41,     8,    36,     7,   163,   164,    10,    41,   167,   168,
      41,   170,   171,     8,     0,     1,     6,     3,    13,    36,
     261,   262,     8,     8,    41,     6,   267,   268,    13,    15,
      16,     6,   159,     8,     9,   162,    11,    23,   223,    36,
     281,   282,     7,   228,    41,    10,   287,   288,    41,     7,
     291,   292,    10,     8,     9,   296,   297,     6,     6,     6,
     301,   302,     8,     8,     6,   306,     6,    41,   253,   310,
     311,   256,   257,     8,   315,   260,     6,   318,    41,    41,
      24,   322,     8,   324,    41,     8,     8,   328,   247,   248,
     249,   250,   277,     4,    21,    11,     8,     7,     1,     8,
      41,     4,     5,    41,    41,     8,     7,    41,    41,    12,
      41,    11,    41,    41,    11,    18,    19,    20,    11,    22,
      41,    41,    25,    26,    27,    28,    29,    41,   313,    32,
      33,    34,    35,    36,    41,    41,     1,    41,    41,     4,
       5,     7,    41,     8,     8,    41,    41,    12,     8,     7,
       7,     7,    13,    18,    19,    20,    10,    22,    10,     8,
      25,    26,    27,    28,    29,    13,     7,    32,    33,    34,
      35,    36,     8,     1,     8,     4,    41,     5,     1,    41,
       8,    24,     5,    13,    11,     8,    41,    11,    41,    17,
      11,    24,    11,    13,    17,    13,    13,     8,    13,    11,
      13,     7,    30,    31,    13,    13,    41,    30,    31,    13,
      38,    39,    40,    41,    13,    38,    39,    40,    41,     4,
       5,     7,    13,     8,     8,    13,     8,    12,     7,    21,
      13,     6,    13,    18,    19,    20,    13,    22,     8,     8,
      25,    26,    27,    28,    29,     4,     5,    32,    33,     8,
      50,   127,    37,    12,   206,    81,    41,    47,    -1,    18,
      19,    20,    -1,    22,    -1,    -1,    25,    26,    27,    28,
      29,     4,     5,    32,    33,     8,    -1,    -1,    37,    12,
      -1,    -1,    41,    -1,    -1,    18,    19,    20,    -1,    22,
      -1,    -1,    25,    26,    27,    28,    29,     4,     5,    32,
      33,     8,    -1,    -1,    37,    12,    -1,    -1,    41,    -1,
      -1,    18,    19,    20,    -1,    22,    -1,    -1,    25,    26,
      27,    28,    29,     4,    -1,    32,    33,     8,    -1,    -1,
      37,    12,    -1,    -1,    41,    -1,    -1,    18,    19,    20,
      -1,    22,    -1,    -1,    25,    26,    27,    28,    29,    -1,
      -1,    32,    33,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      41
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     8,    15,    16,    23,    43,    44,    45,    47,    48,
      49,    50,    41,     4,     0,     1,    45,     3,     6,     5,
      41,    51,    52,    36,    41,    46,     7,    41,    54,     9,
       1,     5,    52,     4,    55,     4,     1,     7,    10,    53,
       1,     5,     8,    17,    30,    31,    38,    39,    40,    41,
      56,    57,    59,    60,    91,    92,    96,     4,     5,     8,
      12,    18,    19,    20,    22,    25,    26,    27,    28,    29,
      32,    33,    37,    41,    62,    68,    70,    82,    84,    89,
      90,     4,    41,    41,    24,    31,    41,     6,     4,    93,
      93,     4,     1,     9,    24,     1,     5,    57,    61,    70,
      41,    80,     6,     6,     6,     6,    36,    41,    67,    78,
      67,    79,     8,     8,     8,     6,     6,    41,     6,     8,
       9,    11,    70,     5,    34,    35,    36,    87,    88,     7,
      41,    65,    86,     8,     9,     5,    90,     5,    89,     8,
      41,     6,    24,    41,    66,     5,    41,    94,     5,    46,
      95,    58,    70,     1,     5,    70,     6,     8,    69,    63,
      41,    66,    64,    10,    13,    11,    41,    10,    13,     8,
      10,    14,     8,    72,    75,     4,    83,    71,    21,    77,
      41,    41,    11,     5,    88,    41,     7,    10,    76,     5,
       8,    66,    70,    41,     7,     8,     1,     5,    41,     8,
      13,     1,     5,    46,    41,     7,    81,    41,    65,    13,
      11,    65,    67,    67,    41,    67,    67,    41,    67,    36,
      67,    41,    41,    61,    41,    70,    11,    11,    61,    41,
      85,    41,     7,    41,    41,     8,    41,    66,     8,    13,
       8,    86,     7,     7,    66,    66,     7,    10,    13,    10,
      13,    14,     8,     7,     5,     8,    61,    61,     8,    41,
      24,    13,    11,    41,    66,     7,     4,    13,    11,    67,
      67,    67,    67,    36,    41,    73,    70,    24,    70,    66,
      66,    13,    11,    66,    66,    41,    70,    13,    11,    66,
      66,    13,    13,     8,    66,    66,    13,    11,    66,    66,
      74,    13,    13,    66,    66,     7,    13,    41,    66,    66,
      13,    13,    66,     7,     8,    13,    66,    66,    13,    70,
      66,     8,    13,    66,    13,    66,     7,    66,    13,     8,
      66,     8
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
#line 168 "ael.y"
        { free((yyvaluep->str));};
#line 1296 "ael.tab.c"
        break;
      case 44: /* "objects" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1304 "ael.tab.c"
        break;
      case 45: /* "object" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1312 "ael.tab.c"
        break;
      case 46: /* "word_or_default" */
#line 168 "ael.y"
        { free((yyvaluep->str));};
#line 1317 "ael.tab.c"
        break;
      case 47: /* "context" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1325 "ael.tab.c"
        break;
      case 49: /* "macro" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1333 "ael.tab.c"
        break;
      case 50: /* "globals" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1341 "ael.tab.c"
        break;
      case 51: /* "global_statements" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1349 "ael.tab.c"
        break;
      case 52: /* "global_statement" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1357 "ael.tab.c"
        break;
      case 54: /* "arglist" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1365 "ael.tab.c"
        break;
      case 55: /* "elements_block" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1373 "ael.tab.c"
        break;
      case 56: /* "elements" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1381 "ael.tab.c"
        break;
      case 57: /* "element" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1389 "ael.tab.c"
        break;
      case 59: /* "ignorepat" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1397 "ael.tab.c"
        break;
      case 60: /* "extension" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1405 "ael.tab.c"
        break;
      case 61: /* "statements" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1413 "ael.tab.c"
        break;
      case 62: /* "if_like_head" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1421 "ael.tab.c"
        break;
      case 65: /* "word_list" */
#line 168 "ael.y"
        { free((yyvaluep->str));};
#line 1426 "ael.tab.c"
        break;
      case 66: /* "word3_list" */
#line 168 "ael.y"
        { free((yyvaluep->str));};
#line 1431 "ael.tab.c"
        break;
      case 67: /* "goto_word" */
#line 168 "ael.y"
        { free((yyvaluep->str));};
#line 1436 "ael.tab.c"
        break;
      case 68: /* "switch_head" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1444 "ael.tab.c"
        break;
      case 70: /* "statement" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1452 "ael.tab.c"
        break;
      case 77: /* "opt_else" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1460 "ael.tab.c"
        break;
      case 78: /* "target" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1468 "ael.tab.c"
        break;
      case 79: /* "jumptarget" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1476 "ael.tab.c"
        break;
      case 80: /* "macro_call" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1484 "ael.tab.c"
        break;
      case 82: /* "application_call_head" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1492 "ael.tab.c"
        break;
      case 84: /* "application_call" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1500 "ael.tab.c"
        break;
      case 85: /* "opt_word" */
#line 168 "ael.y"
        { free((yyvaluep->str));};
#line 1505 "ael.tab.c"
        break;
      case 86: /* "eval_arglist" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1513 "ael.tab.c"
        break;
      case 87: /* "case_statements" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1521 "ael.tab.c"
        break;
      case 88: /* "case_statement" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1529 "ael.tab.c"
        break;
      case 89: /* "macro_statements" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1537 "ael.tab.c"
        break;
      case 90: /* "macro_statement" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1545 "ael.tab.c"
        break;
      case 91: /* "switches" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1553 "ael.tab.c"
        break;
      case 92: /* "eswitches" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1561 "ael.tab.c"
        break;
      case 93: /* "switchlist_block" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1569 "ael.tab.c"
        break;
      case 94: /* "switchlist" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1577 "ael.tab.c"
        break;
      case 95: /* "includeslist" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1585 "ael.tab.c"
        break;
      case 96: /* "includes" */
#line 155 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1593 "ael.tab.c"
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
#line 173 "ael.y"
    { (yyval.pval) = parseio->pval = (yyvsp[0].pval); ;}
    break;

  case 3:
#line 176 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 4:
#line 177 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 5:
#line 178 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 6:
#line 181 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 7:
#line 182 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 8:
#line 183 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 9:
#line 184 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 10:
#line 187 "ael.y"
    { (yyval.str) = (yyvsp[0].str); ;}
    break;

  case 11:
#line 188 "ael.y"
    { (yyval.str) = strdup("default"); ;}
    break;

  case 12:
#line 191 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.abstract = (yyvsp[-3].intval); ;}
    break;

  case 13:
#line 199 "ael.y"
    { (yyval.intval) = 1; ;}
    break;

  case 14:
#line 200 "ael.y"
    { (yyval.intval) = 0; ;}
    break;

  case 15:
#line 203 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-6].str); (yyval.pval)->u2.arglist = (yyvsp[-4].pval); (yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 16:
#line 206 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-5].str); (yyval.pval)->u2.arglist = (yyvsp[-3].pval); ;}
    break;

  case 17:
#line 209 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 18:
#line 213 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str); ;}
    break;

  case 19:
#line 218 "ael.y"
    {
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.statements = (yyvsp[-1].pval);;}
    break;

  case 20:
#line 221 "ael.y"
    { /* empty globals is OK */
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[-2]), &(yylsp[0])); ;}
    break;

  case 21:
#line 225 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 22:
#line 226 "ael.y"
    {(yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 23:
#line 227 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 24:
#line 230 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 25:
#line 230 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 26:
#line 236 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 27:
#line 237 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[0].str), &(yylsp[0]))); ;}
    break;

  case 28:
#line 238 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 29:
#line 241 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 30:
#line 242 "ael.y"
    { (yyval.pval) = (yyvsp[-1].pval); ;}
    break;

  case 31:
#line 245 "ael.y"
    { (yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 32:
#line 246 "ael.y"
    {(yyval.pval)=0;;}
    break;

  case 33:
#line 247 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 34:
#line 248 "ael.y"
    { (yyval.pval)=(yyvsp[-1].pval);;}
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
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 39:
#line 255 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 40:
#line 256 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 41:
#line 256 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 42:
#line 260 "ael.y"
    {free((yyvsp[-1].str)); (yyval.pval)=0;;}
    break;

  case 43:
#line 261 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 44:
#line 264 "ael.y"
    {
		(yyval.pval) = npval2(PV_IGNOREPAT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 45:
#line 269 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 46:
#line 273 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;;}
    break;

  case 47:
#line 278 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 48:
#line 283 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 49:
#line 292 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 50:
#line 293 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 51:
#line 294 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 52:
#line 298 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 53:
#line 298 "ael.y"
    {
		(yyval.pval)= npval2(PV_IF, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 54:
#line 301 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 55:
#line 301 "ael.y"
    {
		(yyval.pval) = npval2(PV_RANDOM, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str=(yyvsp[-1].str);;}
    break;

  case 56:
#line 305 "ael.y"
    {
		(yyval.pval) = npval2(PV_IFTIME, &(yylsp[-13]), &(yylsp[-13]));
		(yyval.pval)->u1.list = npval2(PV_WORD, &(yylsp[-11]), &(yylsp[-7]));
		asprintf(&((yyval.pval)->u1.list->u1.str), "%s:%s:%s", (yyvsp[-11].str), (yyvsp[-9].str), (yyvsp[-7].str));
		free((yyvsp[-11].str));
		free((yyvsp[-9].str));
		free((yyvsp[-7].str));
		(yyval.pval)->u1.list->next = nword((yyvsp[-5].str), &(yylsp[-5]));
		(yyval.pval)->u1.list->next->next = nword((yyvsp[-3].str), &(yylsp[-3]));
		(yyval.pval)->u1.list->next->next->next = nword((yyvsp[-1].str), &(yylsp[-1]));
		prev_word = 0;
	;}
    break;

  case 57:
#line 317 "ael.y"
    {
		(yyval.pval) = npval2(PV_IFTIME, &(yylsp[-9]), &(yylsp[-5])); /* XXX @5 or greater ? */
		(yyval.pval)->u1.list = nword((yyvsp[-7].str), &(yylsp[-7]));
		(yyval.pval)->u1.list->next = nword((yyvsp[-5].str), &(yylsp[-5]));
		(yyval.pval)->u1.list->next->next = nword((yyvsp[-3].str), &(yylsp[-3]));
		(yyval.pval)->u1.list->next->next->next = nword((yyvsp[-1].str), &(yylsp[-1]));
		prev_word = 0;
	;}
    break;

  case 58:
#line 333 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 59:
#line 334 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 60:
#line 341 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 61:
#line 342 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 62:
#line 347 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s%s", (yyvsp[-2].str), (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word=(yyval.str);;}
    break;

  case 63:
#line 355 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 64:
#line 356 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));;}
    break;

  case 65:
#line 360 "ael.y"
    {
		asprintf(&((yyval.str)), "%s:%s", (yyvsp[-2].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[0].str));;}
    break;

  case 66:
#line 366 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 67:
#line 366 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCH, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 68:
#line 374 "ael.y"
    {
		(yyval.pval) = npval2(PV_STATEMENTBLOCK, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 69:
#line 377 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 70:
#line 377 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 71:
#line 381 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 72:
#line 384 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 73:
#line 387 "ael.y"
    {
		(yyval.pval) = npval2(PV_LABEL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 74:
#line 390 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 75:
#line 391 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 76:
#line 392 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 77:
#line 392 "ael.y"
    {
		(yyval.pval) = npval2(PV_FOR, &(yylsp[-11]), &(yylsp[0]));
		(yyval.pval)->u1.for_init = (yyvsp[-8].str);
		(yyval.pval)->u2.for_test=(yyvsp[-5].str);
		(yyval.pval)->u3.for_inc = (yyvsp[-2].str);
		(yyval.pval)->u4.for_statements = (yyvsp[0].pval);;}
    break;

  case 78:
#line 398 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 79:
#line 398 "ael.y"
    {
		(yyval.pval) = npval2(PV_WHILE, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 80:
#line 402 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 81:
#line 404 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[0]));
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 82:
#line 407 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[-1])); ;}
    break;

  case 83:
#line 409 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 84:
#line 411 "ael.y"
    {
		(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 85:
#line 414 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 86:
#line 414 "ael.y"
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

  case 87:
#line 447 "ael.y"
    { (yyval.pval) = npval2(PV_BREAK, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 88:
#line 448 "ael.y"
    { (yyval.pval) = npval2(PV_RETURN, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 89:
#line 449 "ael.y"
    { (yyval.pval) = npval2(PV_CONTINUE, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 90:
#line 450 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[-1]));
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 91:
#line 454 "ael.y"
    { (yyval.pval)=0; ;}
    break;

  case 92:
#line 457 "ael.y"
    { (yyval.pval) = (yyvsp[0].pval); ;}
    break;

  case 93:
#line 458 "ael.y"
    { (yyval.pval) = NULL ; ;}
    break;

  case 94:
#line 463 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 95:
#line 464 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 96:
#line 467 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 97:
#line 470 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 98:
#line 474 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 99:
#line 478 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 100:
#line 482 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 101:
#line 489 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword(strdup("1"), &(yylsp[0])); ;}
    break;

  case 102:
#line 492 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 103:
#line 495 "ael.y"
    {	/* XXX they are stored in a different order */
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next->next = nword((yyvsp[-2].str), &(yylsp[-2])); ;}
    break;

  case 104:
#line 499 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword(strdup("1"), &(yylsp[0])); ;}
    break;

  case 105:
#line 503 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next->next = nword((yyvsp[-2].str), &(yylsp[-2])); ;}
    break;

  case 106:
#line 507 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[0]));
		(yyval.pval)->next->next = nword( strdup("1"), &(yylsp[0])); ;}
    break;

  case 107:
#line 513 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 108:
#line 513 "ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		(yyval.pval) = npval2(PV_MACRO_CALL, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.arglist = (yyvsp[-1].pval);;}
    break;

  case 109:
#line 518 "ael.y"
    {
		(yyval.pval)= npval2(PV_MACRO_CALL, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 110:
#line 526 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 111:
#line 526 "ael.y"
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

  case 112:
#line 537 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[0]));
 		if( (yyval.pval)->type == PV_GOTO )
			(yyval.pval)->u1.list = (yyvsp[-1].pval);
	 	else
			(yyval.pval)->u2.arglist = (yyvsp[-1].pval);
	;}
    break;

  case 113:
#line 544 "ael.y"
    { (yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 114:
#line 547 "ael.y"
    { (yyval.str) = (yyvsp[0].str) ;}
    break;

  case 115:
#line 548 "ael.y"
    { (yyval.str) = strdup(""); ;}
    break;

  case 116:
#line 551 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 117:
#line 552 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		(yyval.pval)->u1.str = strdup(""); ;}
    break;

  case 118:
#line 555 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[0].str), &(yylsp[0]))); ;}
    break;

  case 119:
#line 558 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 120:
#line 559 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 121:
#line 562 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-3]), &(yylsp[-1])); /* XXX 3 or 4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 122:
#line 566 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 123:
#line 570 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-3]), &(yylsp[0])); /* XXX@3 or @4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 124:
#line 574 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 125:
#line 577 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;;}
    break;

  case 126:
#line 580 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 127:
#line 585 "ael.y"
    {(yyval.pval) = (yyvsp[0].pval);;}
    break;

  case 128:
#line 586 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 129:
#line 589 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 130:
#line 590 "ael.y"
    {
		(yyval.pval) = npval2(PV_CATCH, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 131:
#line 596 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[0].pval); ;}
    break;

  case 132:
#line 601 "ael.y"
    {
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[0].pval); ;}
    break;

  case 133:
#line 606 "ael.y"
    { (yyval.pval) = (yyvsp[-1].pval); ;}
    break;

  case 134:
#line 607 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 135:
#line 610 "ael.y"
    { (yyval.pval) = nword((yyvsp[-1].str), &(yylsp[-1])); ;}
    break;

  case 136:
#line 611 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[-1].str), &(yylsp[-1]))); ;}
    break;

  case 137:
#line 612 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 138:
#line 615 "ael.y"
    { (yyval.pval) = nword((yyvsp[-1].str), &(yylsp[-1])); ;}
    break;

  case 139:
#line 617 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-13].str), &(yylsp[-13]));
		(yyval.pval)->u2.arglist = npval2(PV_WORD, &(yylsp[-11]), &(yylsp[-7]));
		asprintf( &((yyval.pval)->u2.arglist->u1.str), "%s:%s:%s", (yyvsp[-11].str), (yyvsp[-9].str), (yyvsp[-7].str));
		free((yyvsp[-11].str));
		free((yyvsp[-9].str));
		free((yyvsp[-7].str));
		(yyval.pval)->u2.arglist->next = nword((yyvsp[-5].str), &(yylsp[-5]));
		(yyval.pval)->u2.arglist->next->next = nword((yyvsp[-3].str), &(yylsp[-3]));
		(yyval.pval)->u2.arglist->next->next->next = nword((yyvsp[-1].str), &(yylsp[-1]));
		prev_word=0;
	;}
    break;

  case 140:
#line 629 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-9].str), &(yylsp[-9]));
		(yyval.pval)->u2.arglist = nword((yyvsp[-7].str), &(yylsp[-7]));
		(yyval.pval)->u2.arglist->next = nword((yyvsp[-5].str), &(yylsp[-5]));
		(yyval.pval)->u2.arglist->next->next = nword((yyvsp[-3].str), &(yylsp[-3]));
		(yyval.pval)->u2.arglist->next->next->next = nword((yyvsp[-1].str), &(yylsp[-1]));
		prev_word=0;
	;}
    break;

  case 141:
#line 637 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[-1].str), &(yylsp[-1]))); ;}
    break;

  case 142:
#line 639 "ael.y"
    {
		pval *z = nword((yyvsp[-13].str), &(yylsp[-13]));
		(yyval.pval) = linku1((yyvsp[-14].pval), z);
		z->u2.arglist = npval2(PV_WORD, &(yylsp[-11]), &(yylsp[-7]));
		asprintf( &((yyval.pval)->u2.arglist->u1.str), "%s:%s:%s", (yyvsp[-11].str), (yyvsp[-9].str), (yyvsp[-7].str));
		free((yyvsp[-11].str));
		free((yyvsp[-9].str));
		free((yyvsp[-7].str));
		z->u2.arglist->next = nword((yyvsp[-5].str), &(yylsp[-5]));
		z->u2.arglist->next->next = nword((yyvsp[-3].str), &(yylsp[-3]));
		z->u2.arglist->next->next->next = nword((yyvsp[-1].str), &(yylsp[-1]));
		prev_word=0;
	;}
    break;

  case 143:
#line 652 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-9]), &(yylsp[-8]));
		(yyval.pval) = linku1((yyvsp[-10].pval), z);
		(yyval.pval)->u2.arglist->u1.str = (yyvsp[-7].str);			/* XXX maybe too early ? */
		z->u1.str = (yyvsp[-9].str);
		z->u2.arglist = npval2(PV_WORD, &(yylsp[-7]), &(yylsp[-7]));	/* XXX is this correct ? */
		z->u2.arglist->next = nword((yyvsp[-5].str), &(yylsp[-5]));
		z->u2.arglist->next->next = nword((yyvsp[-3].str), &(yylsp[-3]));
		z->u2.arglist->next->next->next = nword((yyvsp[-1].str), &(yylsp[-1]));
		prev_word=0;
	;}
    break;

  case 144:
#line 663 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 145:
#line 666 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 146:
#line 669 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-2]), &(yylsp[0]));;}
    break;


      default: break;
    }

/* Line 1126 of yacc.c.  */
#line 2902 "ael.tab.c"

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


#line 674 "ael.y"


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

