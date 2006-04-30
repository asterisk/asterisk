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
#define YYFINAL  18
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   505

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  55
/* YYNRULES -- Number of rules. */
#define YYNRULES  149
/* YYNRULES -- Number of states. */
#define YYNSTATES  346

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
      21,    23,    25,    31,    36,    43,    49,    58,    66,    74,
      81,    86,    90,    92,    95,    98,    99,   105,   107,   111,
     114,   116,   118,   121,   124,   126,   128,   130,   132,   134,
     135,   141,   144,   146,   151,   155,   160,   168,   177,   179,
     182,   185,   186,   192,   193,   199,   214,   225,   227,   230,
     232,   235,   239,   241,   244,   248,   249,   256,   260,   261,
     267,   271,   275,   278,   279,   280,   281,   294,   295,   302,
     305,   309,   313,   316,   319,   320,   326,   329,   332,   335,
     339,   343,   347,   349,   352,   353,   355,   359,   363,   369,
     375,   381,   387,   389,   393,   399,   403,   409,   413,   414,
     420,   424,   425,   429,   433,   436,   438,   439,   441,   442,
     446,   448,   451,   456,   460,   465,   469,   472,   476,   478,
     481,   483,   489,   494,   498,   503,   507,   510,   514,   517,
     520,   535,   546,   550,   566,   578,   581,   583,   585,   590
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      43,     0,    -1,    44,    -1,    45,    -1,    44,    45,    -1,
      44,     1,    -1,    47,    -1,    48,    -1,    49,    -1,     8,
      -1,    41,    -1,    36,    -1,     3,    46,     4,    54,     5,
      -1,     3,    46,     4,     5,    -1,    23,     3,    46,     4,
      54,     5,    -1,    23,     3,    46,     4,     5,    -1,    15,
      41,     6,    53,     7,     4,    89,     5,    -1,    15,    41,
       6,    53,     7,     4,     5,    -1,    15,    41,     6,     7,
       4,    89,     5,    -1,    15,    41,     6,     7,     4,     5,
      -1,    16,     4,    50,     5,    -1,    16,     4,     5,    -1,
      51,    -1,    50,    51,    -1,    50,     1,    -1,    -1,    41,
       9,    52,    41,     8,    -1,    41,    -1,    53,    10,    41,
      -1,    53,     1,    -1,    55,    -1,     1,    -1,    54,    55,
      -1,    54,     1,    -1,    58,    -1,    96,    -1,    91,    -1,
      92,    -1,    57,    -1,    -1,    41,     9,    56,    41,     8,
      -1,    41,     1,    -1,     8,    -1,    17,    24,    41,     8,
      -1,    41,    24,    70,    -1,    30,    41,    24,    70,    -1,
      31,     6,    66,     7,    41,    24,    70,    -1,    30,    31,
       6,    66,     7,    41,    24,    70,    -1,    70,    -1,    59,
      70,    -1,    59,     1,    -1,    -1,    19,     6,    61,    65,
       7,    -1,    -1,    22,     6,    63,    65,     7,    -1,    20,
       6,    66,    11,    66,    11,    66,    13,    66,    13,    66,
      13,    66,     7,    -1,    20,     6,    41,    13,    66,    13,
      66,    13,    66,     7,    -1,    41,    -1,    41,    41,    -1,
      41,    -1,    41,    41,    -1,    41,    41,    41,    -1,    41,
      -1,    41,    41,    -1,    41,    11,    41,    -1,    -1,    18,
       6,    69,    41,     7,     4,    -1,     4,    59,     5,    -1,
      -1,    41,     9,    71,    41,     8,    -1,    25,    78,     8,
      -1,    26,    79,     8,    -1,    41,    11,    -1,    -1,    -1,
      -1,    32,     6,    72,    41,     8,    73,    41,     8,    74,
      41,     7,    70,    -1,    -1,    33,     6,    75,    41,     7,
      70,    -1,    68,     5,    -1,    68,    87,     5,    -1,    12,
      80,     8,    -1,    84,     8,    -1,    41,     8,    -1,    -1,
      84,     9,    76,    41,     8,    -1,    28,     8,    -1,    27,
       8,    -1,    29,     8,    -1,    62,    70,    77,    -1,    60,
      70,    77,    -1,    64,    70,    77,    -1,     8,    -1,    21,
      70,    -1,    -1,    67,    -1,    67,    13,    67,    -1,    67,
      10,    67,    -1,    67,    13,    67,    13,    67,    -1,    67,
      10,    67,    10,    67,    -1,    36,    13,    67,    13,    67,
      -1,    36,    10,    67,    10,    67,    -1,    67,    -1,    67,
      10,    67,    -1,    67,    10,    41,    14,    41,    -1,    67,
      14,    67,    -1,    67,    10,    41,    14,    36,    -1,    67,
      14,    36,    -1,    -1,    41,     6,    81,    86,     7,    -1,
      41,     6,     7,    -1,    -1,    41,    83,     6,    -1,    82,
      86,     7,    -1,    82,     7,    -1,    41,    -1,    -1,    65,
      -1,    -1,    86,    10,    85,    -1,    88,    -1,    87,    88,
      -1,    34,    41,    11,    59,    -1,    36,    11,    59,    -1,
      35,    41,    11,    59,    -1,    34,    41,    11,    -1,    36,
      11,    -1,    35,    41,    11,    -1,    90,    -1,    89,    90,
      -1,    70,    -1,    37,    41,     4,    59,     5,    -1,    38,
       4,    93,     5,    -1,    38,     4,     5,    -1,    39,     4,
      93,     5,    -1,    39,     4,     5,    -1,    41,     8,    -1,
      93,    41,     8,    -1,    93,     1,    -1,    95,     8,    -1,
      95,    13,    66,    11,    66,    11,    66,    13,    66,    13,
      66,    13,    66,     8,    -1,    95,    13,    41,    13,    66,
      13,    66,    13,    66,     8,    -1,    94,    95,     8,    -1,
      94,    95,    13,    66,    11,    66,    11,    66,    13,    66,
      13,    66,    13,    66,     8,    -1,    94,    95,    13,    41,
      13,    66,    13,    66,    13,    66,     8,    -1,    94,     1,
      -1,    41,    -1,    36,    -1,    40,     4,    94,     5,    -1,
      40,     4,     5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   165,   165,   168,   169,   180,   183,   184,   185,   186,
     189,   190,   193,   197,   200,   205,   229,   232,   235,   239,
     244,   247,   251,   252,   253,   256,   256,   262,   265,   269,
     272,   273,   274,   277,   280,   281,   282,   283,   284,   285,
     285,   289,   290,   293,   298,   302,   307,   312,   321,   322,
     325,   328,   328,   333,   333,   338,   354,   374,   375,   382,
     383,   388,   396,   397,   401,   407,   407,   415,   418,   418,
     422,   425,   428,   431,   432,   433,   431,   439,   439,   443,
     447,   452,   456,   460,   463,   463,   496,   497,   498,   499,
     513,   527,   541,   544,   545,   550,   552,   557,   562,   569,
     576,   583,   592,   597,   602,   609,   616,   623,   632,   632,
     637,   642,   642,   652,   658,   661,   662,   665,   668,   671,
     685,   686,   691,   695,   699,   703,   706,   709,   714,   715,
     720,   721,   727,   730,   734,   737,   741,   744,   749,   752,
     755,   772,   785,   790,   808,   823,   826,   827,   830,   833
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
  "global_statements", "global_statement", "@1", "arglist", "elements",
  "element", "@2", "ignorepat", "extension", "statements", "if_head", "@3",
  "random_head", "@4", "iftime_head", "word_list", "word3_list",
  "goto_word", "switch_head", "@5", "statement", "@6", "@7", "@8", "@9",
  "@10", "@11", "opt_else", "target", "jumptarget", "macro_call", "@12",
  "application_call_head", "@13", "application_call", "opt_word",
  "eval_arglist", "case_statements", "case_statement", "macro_statements",
  "macro_statement", "switches", "eswitches", "switchlist", "includeslist",
  "includedname", "includes", 0
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
      46,    46,    47,    47,    47,    47,    48,    48,    48,    48,
      49,    49,    50,    50,    50,    52,    51,    53,    53,    53,
      54,    54,    54,    54,    55,    55,    55,    55,    55,    56,
      55,    55,    55,    57,    58,    58,    58,    58,    59,    59,
      59,    61,    60,    63,    62,    64,    64,    65,    65,    66,
      66,    66,    67,    67,    67,    69,    68,    70,    71,    70,
      70,    70,    70,    72,    73,    74,    70,    75,    70,    70,
      70,    70,    70,    70,    76,    70,    70,    70,    70,    70,
      70,    70,    70,    77,    77,    78,    78,    78,    78,    78,
      78,    78,    79,    79,    79,    79,    79,    79,    81,    80,
      80,    83,    82,    84,    84,    85,    85,    86,    86,    86,
      87,    87,    88,    88,    88,    88,    88,    88,    89,    89,
      90,    90,    91,    91,    92,    92,    93,    93,    93,    94,
      94,    94,    94,    94,    94,    94,    95,    95,    96,    96
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     1,     5,     4,     6,     5,     8,     7,     7,     6,
       4,     3,     1,     2,     2,     0,     5,     1,     3,     2,
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
       4,     0,     0,    21,     0,     0,    22,     0,    31,    13,
      42,     0,     0,     0,     0,     0,     0,     0,     0,    30,
      38,    34,    36,    37,    35,     0,    27,     0,    25,    24,
      20,    23,     0,     0,     0,     0,     0,     0,     0,     0,
      41,    39,     0,    33,    12,    32,     0,    29,     0,     0,
       0,    15,     0,     0,     0,     0,    59,     0,   133,     0,
       0,   135,     0,   149,   147,   146,     0,     0,     0,     0,
      92,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,   111,     0,     0,     0,     0,    44,   118,
       0,    19,     0,   130,     0,   128,     0,    28,     0,    14,
      43,     0,    45,    60,     0,   136,   138,   132,     0,   134,
     145,   148,     0,   139,     0,     0,     0,    48,     0,     0,
      65,    51,     0,    53,     0,    62,    95,     0,   102,     0,
      87,    86,    88,    73,    77,    83,    68,    72,     0,    94,
      94,    94,    79,     0,     0,     0,     0,   120,   114,    57,
     117,     0,    82,    84,     0,    18,   129,    17,     0,    26,
       0,    61,     0,   137,   142,     0,    59,     0,    40,    50,
      67,    49,   108,    81,     0,     0,    59,     0,     0,     0,
       0,     0,    63,     0,     0,    70,     0,     0,    71,     0,
       0,     0,   112,     0,    90,    89,    91,     0,     0,   126,
      80,   121,    58,   113,   116,     0,     0,    16,     0,     0,
      59,     0,     0,     0,   110,   118,     0,     0,     0,     0,
       0,     0,     0,    64,    97,    96,    62,   103,   107,   105,
       0,     0,     0,    93,   125,   127,     0,   115,   119,     0,
       0,     0,    46,     0,     0,     0,     0,     0,     0,    52,
       0,     0,    54,     0,     0,     0,     0,     0,    74,     0,
      69,     0,     0,    85,   131,    47,     0,     0,     0,     0,
     109,    66,     0,     0,   101,   100,    99,    98,   106,   104,
       0,    78,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    75,     0,     0,     0,     0,
       0,     0,     0,     0,     0,   141,     0,    56,     0,     0,
     144,     0,     0,     0,     0,     0,     0,     0,    76,     0,
       0,     0,     0,   140,    55,   143
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     6,     7,     8,    14,     9,    10,    11,    25,    26,
      70,    47,    38,    39,    88,    40,    41,   136,   104,   195,
     105,   198,   106,   170,    77,   146,   107,   194,   137,   211,
     209,   300,   322,   210,   225,   214,   147,   149,   139,   235,
     109,   158,   110,   258,   171,   166,   167,   114,   115,    42,
      43,    80,    86,    87,    44
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -193
static const short int yypact[] =
{
     121,   117,  -193,   -38,    84,    17,    95,   343,  -193,  -193,
    -193,  -193,  -193,  -193,    98,   122,     9,   117,  -193,  -193,
    -193,   151,    53,  -193,   110,    14,  -193,   167,  -193,  -193,
    -193,   102,    39,   182,   200,   214,   218,   184,   204,  -193,
    -193,  -193,  -193,  -193,  -193,   219,  -193,   196,  -193,  -193,
    -193,  -193,   280,   183,   221,   207,   187,    23,    44,    20,
    -193,  -193,   461,  -193,  -193,  -193,   357,  -193,   228,   192,
     195,  -193,   333,   229,   187,   461,   197,   234,  -193,   238,
      16,  -193,    46,  -193,  -193,  -193,     5,   147,   206,   461,
    -193,   211,   249,   250,   251,   252,   125,   220,   254,   256,
     258,   253,   261,   104,   461,   461,   461,   129,  -193,    60,
      50,  -193,   230,  -193,   383,  -193,   409,  -193,   262,  -193,
    -193,   270,  -193,   239,   242,  -193,  -193,  -193,   276,  -193,
    -193,  -193,   149,  -193,   246,   281,   274,  -193,   263,   282,
    -193,  -193,   257,  -193,   173,     7,   201,   283,    63,   287,
    -193,  -193,  -193,  -193,  -193,  -193,  -193,  -193,   298,   292,
     292,   292,  -193,   273,   284,   306,   138,  -193,  -193,   286,
    -193,   120,  -193,  -193,   324,  -193,  -193,  -193,   435,  -193,
     294,  -193,   236,  -193,  -193,   299,    -6,   328,  -193,  -193,
    -193,  -193,   335,  -193,   307,   308,    40,   336,   308,   220,
     220,   311,  -193,   220,   220,  -193,   312,   166,  -193,   313,
     314,   315,  -193,   461,  -193,  -193,  -193,   346,   349,   461,
    -193,  -193,  -193,  -193,   326,   327,   461,  -193,   354,   461,
      65,   359,   187,   187,  -193,   308,   373,   374,   187,   187,
     385,   386,   380,  -193,   387,   391,   109,  -193,  -193,  -193,
     392,   399,   410,  -193,   461,   461,     4,  -193,  -193,   411,
     304,   461,  -193,   187,   187,   394,   388,   177,   418,  -193,
     412,   415,  -193,   220,   220,   220,   220,   174,  -193,   461,
    -193,    64,   113,  -193,  -193,  -193,   417,   421,   187,   187,
    -193,  -193,   187,   187,  -193,  -193,  -193,  -193,  -193,  -193,
     382,  -193,   187,   187,   420,   431,   432,   436,   440,   438,
     439,   187,   187,   187,   187,  -193,   187,   187,   448,   445,
     452,   453,   429,   463,   462,  -193,   187,  -193,   187,   467,
    -193,   187,   464,   465,   461,   469,   187,   187,  -193,   187,
     476,   478,   483,  -193,  -193,  -193
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -193,  -193,  -193,   485,   479,  -193,  -193,  -193,  -193,   470,
    -193,  -193,   446,    37,  -193,  -193,  -193,  -192,  -193,  -193,
    -193,  -193,  -193,    18,   -63,   -96,  -193,  -193,   -62,  -193,
    -193,  -193,  -193,  -193,  -193,    35,  -193,  -193,  -193,  -193,
    -193,  -193,  -193,  -193,   264,  -193,   331,   384,  -112,  -193,
    -193,   443,  -193,   419,  -193
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -125
static const short int yytable[] =
{
     108,   148,   176,    15,   113,   189,   130,   232,    89,  -123,
     131,   121,    90,   122,    23,    49,    91,   126,   201,    50,
      17,   127,    92,    93,    94,    83,    95,   256,    78,    96,
      97,    98,    99,   100,   260,   123,   101,   102,  -123,  -123,
    -123,    84,   159,   160,   161,   103,    85,   126,   202,    81,
      24,   129,   113,   238,   113,    24,    84,   128,   172,   173,
      45,    85,   281,   282,    79,   189,   176,   168,    89,  -122,
      54,   187,    90,   206,   191,    65,    91,   207,   263,   197,
      55,   123,    92,    93,    94,    79,    95,   128,    16,    96,
      97,    98,    99,   100,    46,    18,   101,   102,  -122,  -122,
    -122,   169,    21,   241,   242,   103,   123,   244,   245,    65,
     247,   249,   155,   156,   189,   157,   113,    89,  -124,    48,
     201,    90,   231,   277,     1,    91,    53,   223,    22,     2,
     224,    92,    93,    94,   162,    95,     3,     4,    96,    97,
      98,    99,   100,   220,     5,   101,   102,  -124,  -124,  -124,
     202,   253,    28,    12,   103,   133,    29,   184,    13,    30,
     134,   144,   185,   163,   164,   165,   145,   262,    31,   265,
     266,    52,   163,   164,   165,   270,   271,   294,   295,   296,
     297,    32,    33,   199,   290,    60,   200,   224,    56,    34,
      35,    36,    37,    61,   191,   215,   216,    67,   191,   285,
     286,   287,   248,    68,    57,    63,    69,   145,    62,    64,
     298,   203,    30,   237,   204,   299,   240,   301,    58,   191,
     191,    31,    59,    66,    73,   304,   305,    74,    76,   306,
     307,    75,   116,   117,    32,    33,   118,   120,   123,   309,
     310,   124,    34,    35,    36,    37,   125,   135,   318,   319,
     320,   321,   138,   323,   324,   140,   141,   142,   143,   153,
     229,   145,   150,   332,   151,   333,   152,   154,   335,   192,
     179,   174,   338,   340,   341,   189,   342,   180,    89,   190,
     181,    28,    90,   182,   183,    71,    91,   186,    30,   188,
     193,   205,    92,    93,    94,   208,    95,    31,   196,    96,
      97,    98,    99,   100,   212,   189,   101,   102,    89,   284,
      32,    33,    90,   213,   217,   103,    91,   219,    34,    35,
      36,    37,    92,    93,    94,   218,    95,   222,   226,    96,
      97,    98,    99,   100,    63,   228,   101,   102,   119,   233,
     230,    30,   234,    -2,    19,   103,     1,   239,   236,   169,
      31,     2,   243,   246,   250,   251,   252,   254,     3,     4,
     255,    89,   111,    32,    33,    90,     5,   257,   259,    91,
     264,    34,    35,    36,    37,    92,    93,    94,   261,    95,
     268,   269,    96,    97,    98,    99,   100,    89,   175,   101,
     102,    90,   272,   274,   112,    91,   273,   275,   103,   289,
     278,    92,    93,    94,   276,    95,   279,   288,    96,    97,
      98,    99,   100,    89,   177,   101,   102,    90,   280,   283,
     112,    91,   291,   308,   103,   292,   293,    92,    93,    94,
     302,    95,   303,   311,    96,    97,    98,    99,   100,    89,
     227,   101,   102,    90,   312,   313,   112,    91,   315,   314,
     103,   316,   317,    92,    93,    94,   325,    95,   326,   327,
      96,    97,    98,    99,   100,    89,   328,   101,   102,    90,
     329,   330,   112,    91,   334,   331,   103,   336,   337,    92,
      93,    94,   339,    95,   343,   344,    96,    97,    98,    99,
     100,   345,    20,   101,   102,    51,    27,   221,    72,   267,
     178,    82,   103,     0,     0,   132
};

static const short int yycheck[] =
{
      62,    97,   114,    41,    66,     1,     1,    13,     4,     5,
       5,    74,     8,    75,     5,     1,    12,     1,    11,     5,
       3,     5,    18,    19,    20,     5,    22,   219,     5,    25,
      26,    27,    28,    29,   226,    41,    32,    33,    34,    35,
      36,    36,   104,   105,   106,    41,    41,     1,    41,     5,
      41,     5,   114,    13,   116,    41,    36,    41,     8,     9,
       7,    41,   254,   255,    41,     1,   178,     7,     4,     5,
      31,   134,     8,    10,   136,    38,    12,    14,    13,   142,
      41,    41,    18,    19,    20,    41,    22,    41,     4,    25,
      26,    27,    28,    29,    41,     0,    32,    33,    34,    35,
      36,    41,     4,   199,   200,    41,    41,   203,   204,    72,
     206,   207,     8,     9,     1,    11,   178,     4,     5,     9,
      11,     8,   185,    14,     3,    12,    24,     7,     6,     8,
      10,    18,    19,    20,     5,    22,    15,    16,    25,    26,
      27,    28,    29,     5,    23,    32,    33,    34,    35,    36,
      41,   213,     1,    36,    41,     8,     5,     8,    41,     8,
      13,    36,    13,    34,    35,    36,    41,   229,    17,   232,
     233,     4,    34,    35,    36,   238,   239,   273,   274,   275,
     276,    30,    31,    10,     7,     1,    13,    10,     6,    38,
      39,    40,    41,     9,   256,   160,   161,     1,   260,   261,
     263,   264,    36,     7,     4,     1,    10,    41,    24,     5,
      36,    10,     8,   195,    13,    41,   198,   279,     4,   281,
     282,    17,     4,     4,    41,   288,   289,     6,    41,   292,
     293,    24,     4,    41,    30,    31,    41,     8,    41,   302,
     303,     7,    38,    39,    40,    41,     8,    41,   311,   312,
     313,   314,    41,   316,   317,     6,     6,     6,     6,     6,
      24,    41,     8,   326,     8,   328,     8,     6,   331,     6,
       8,    41,   334,   336,   337,     1,   339,     7,     4,     5,
      41,     1,     8,    41,     8,     5,    12,    41,     8,     8,
       8,     8,    18,    19,    20,     8,    22,    17,    41,    25,
      26,    27,    28,    29,     6,     1,    32,    33,     4,     5,
      30,    31,     8,    21,    41,    41,    12,    11,    38,    39,
      40,    41,    18,    19,    20,    41,    22,    41,     4,    25,
      26,    27,    28,    29,     1,    41,    32,    33,     5,    11,
      41,     8,     7,     0,     1,    41,     3,    11,    41,    41,
      17,     8,    41,    41,    41,    41,    41,    11,    15,    16,
      11,     4,     5,    30,    31,     8,    23,    41,    41,    12,
      11,    38,    39,    40,    41,    18,    19,    20,    24,    22,
       7,     7,    25,    26,    27,    28,    29,     4,     5,    32,
      33,     8,     7,    13,    37,    12,    10,    10,    41,    11,
       8,    18,    19,    20,    13,    22,     7,    13,    25,    26,
      27,    28,    29,     4,     5,    32,    33,     8,     8,     8,
      37,    12,     4,    41,    41,    13,    11,    18,    19,    20,
      13,    22,    11,    13,    25,    26,    27,    28,    29,     4,
       5,    32,    33,     8,    13,    13,    37,    12,     8,    13,
      41,    13,    13,    18,    19,    20,     8,    22,    13,     7,
      25,    26,    27,    28,    29,     4,    13,    32,    33,     8,
      41,     8,    37,    12,     7,    13,    41,    13,    13,    18,
      19,    20,    13,    22,     8,     7,    25,    26,    27,    28,
      29,     8,     7,    32,    33,    25,    17,   166,    52,   235,
     116,    58,    41,    -1,    -1,    86
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     3,     8,    15,    16,    23,    43,    44,    45,    47,
      48,    49,    36,    41,    46,    41,     4,     3,     0,     1,
      45,     4,     6,     5,    41,    50,    51,    46,     1,     5,
       8,    17,    30,    31,    38,    39,    40,    41,    54,    55,
      57,    58,    91,    92,    96,     7,    41,    53,     9,     1,
       5,    51,     4,    24,    31,    41,     6,     4,     4,     4,
       1,     9,    24,     1,     5,    55,     4,     1,     7,    10,
      52,     5,    54,    41,     6,    24,    41,    66,     5,    41,
      93,     5,    93,     5,    36,    41,    94,    95,    56,     4,
       8,    12,    18,    19,    20,    22,    25,    26,    27,    28,
      29,    32,    33,    41,    60,    62,    64,    68,    70,    82,
      84,     5,    37,    70,    89,    90,     4,    41,    41,     5,
       8,    66,    70,    41,     7,     8,     1,     5,    41,     5,
       1,     5,    95,     8,    13,    41,    59,    70,    41,    80,
       6,     6,     6,     6,    36,    41,    67,    78,    67,    79,
       8,     8,     8,     6,     6,     8,     9,    11,    83,    70,
      70,    70,     5,    34,    35,    36,    87,    88,     7,    41,
      65,    86,     8,     9,    41,     5,    90,     5,    89,     8,
       7,    41,    41,     8,     8,    13,    41,    66,     8,     1,
       5,    70,     6,     8,    69,    61,    41,    66,    63,    10,
      13,    11,    41,    10,    13,     8,    10,    14,     8,    72,
      75,    71,     6,    21,    77,    77,    77,    41,    41,    11,
       5,    88,    41,     7,    10,    76,     4,     5,    41,    24,
      41,    66,    13,    11,     7,    81,    41,    65,    13,    11,
      65,    67,    67,    41,    67,    67,    41,    67,    36,    67,
      41,    41,    41,    70,    11,    11,    59,    41,    85,    41,
      59,    24,    70,    13,    11,    66,    66,    86,     7,     7,
      66,    66,     7,    10,    13,    10,    13,    14,     8,     7,
       8,    59,    59,     8,     5,    70,    66,    66,    13,    11,
       7,     4,    13,    11,    67,    67,    67,    67,    36,    41,
      73,    70,    13,    11,    66,    66,    66,    66,    41,    66,
      66,    13,    13,    13,    13,     8,    13,    13,    66,    66,
      66,    66,    74,    66,    66,     8,    13,     7,    13,    41,
       8,    13,    66,    66,     7,    66,    13,    13,    70,    13,
      66,    66,    66,     8,     7,     8
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
#line 160 "ael.y"
        { free((yyvaluep->str));};
#line 1294 "ael.tab.c"
        break;
      case 44: /* "objects" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1302 "ael.tab.c"
        break;
      case 45: /* "object" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1310 "ael.tab.c"
        break;
      case 46: /* "word_or_default" */
#line 160 "ael.y"
        { free((yyvaluep->str));};
#line 1315 "ael.tab.c"
        break;
      case 47: /* "context" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1323 "ael.tab.c"
        break;
      case 48: /* "macro" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1331 "ael.tab.c"
        break;
      case 49: /* "globals" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1339 "ael.tab.c"
        break;
      case 50: /* "global_statements" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1347 "ael.tab.c"
        break;
      case 51: /* "global_statement" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1355 "ael.tab.c"
        break;
      case 53: /* "arglist" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1363 "ael.tab.c"
        break;
      case 54: /* "elements" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1371 "ael.tab.c"
        break;
      case 55: /* "element" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1379 "ael.tab.c"
        break;
      case 57: /* "ignorepat" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1387 "ael.tab.c"
        break;
      case 58: /* "extension" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1395 "ael.tab.c"
        break;
      case 59: /* "statements" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1403 "ael.tab.c"
        break;
      case 60: /* "if_head" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1411 "ael.tab.c"
        break;
      case 62: /* "random_head" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1419 "ael.tab.c"
        break;
      case 64: /* "iftime_head" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1427 "ael.tab.c"
        break;
      case 65: /* "word_list" */
#line 160 "ael.y"
        { free((yyvaluep->str));};
#line 1432 "ael.tab.c"
        break;
      case 66: /* "word3_list" */
#line 160 "ael.y"
        { free((yyvaluep->str));};
#line 1437 "ael.tab.c"
        break;
      case 67: /* "goto_word" */
#line 160 "ael.y"
        { free((yyvaluep->str));};
#line 1442 "ael.tab.c"
        break;
      case 68: /* "switch_head" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1450 "ael.tab.c"
        break;
      case 70: /* "statement" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1458 "ael.tab.c"
        break;
      case 77: /* "opt_else" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1466 "ael.tab.c"
        break;
      case 78: /* "target" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1474 "ael.tab.c"
        break;
      case 79: /* "jumptarget" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1482 "ael.tab.c"
        break;
      case 80: /* "macro_call" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1490 "ael.tab.c"
        break;
      case 82: /* "application_call_head" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1498 "ael.tab.c"
        break;
      case 84: /* "application_call" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1506 "ael.tab.c"
        break;
      case 85: /* "opt_word" */
#line 160 "ael.y"
        { free((yyvaluep->str));};
#line 1511 "ael.tab.c"
        break;
      case 86: /* "eval_arglist" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1519 "ael.tab.c"
        break;
      case 87: /* "case_statements" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1527 "ael.tab.c"
        break;
      case 88: /* "case_statement" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1535 "ael.tab.c"
        break;
      case 89: /* "macro_statements" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1543 "ael.tab.c"
        break;
      case 90: /* "macro_statement" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1551 "ael.tab.c"
        break;
      case 91: /* "switches" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1559 "ael.tab.c"
        break;
      case 92: /* "eswitches" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1567 "ael.tab.c"
        break;
      case 93: /* "switchlist" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1575 "ael.tab.c"
        break;
      case 94: /* "includeslist" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1583 "ael.tab.c"
        break;
      case 95: /* "includedname" */
#line 160 "ael.y"
        { free((yyvaluep->str));};
#line 1588 "ael.tab.c"
        break;
      case 96: /* "includes" */
#line 148 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1596 "ael.tab.c"
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
#line 165 "ael.y"
    { (yyval.pval) = parseio->pval = (yyvsp[0].pval); ;}
    break;

  case 3:
#line 168 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 4:
#line 170 "ael.y"
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
#line 180 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 6:
#line 183 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 7:
#line 184 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 8:
#line 185 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 9:
#line 186 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 10:
#line 189 "ael.y"
    { (yyval.str) = (yyvsp[0].str); ;}
    break;

  case 11:
#line 190 "ael.y"
    { (yyval.str) = strdup("default"); ;}
    break;

  case 12:
#line 193 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval); ;}
    break;

  case 13:
#line 197 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 14:
#line 200 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.abstract = 1; ;}
    break;

  case 15:
#line 205 "ael.y"
    {
		(yyval.pval) = npval2(PV_CONTEXT, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u3.abstract = 1; ;}
    break;

  case 16:
#line 229 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-6].str); (yyval.pval)->u2.arglist = (yyvsp[-4].pval); (yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 17:
#line 232 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-5].str); (yyval.pval)->u2.arglist = (yyvsp[-3].pval); ;}
    break;

  case 18:
#line 235 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-5].str);
		(yyval.pval)->u3.macro_statements = (yyvsp[-1].pval); ;}
    break;

  case 19:
#line 239 "ael.y"
    {
		(yyval.pval) = npval2(PV_MACRO, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str); ;}
    break;

  case 20:
#line 244 "ael.y"
    {
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.statements = (yyvsp[-1].pval);;}
    break;

  case 21:
#line 247 "ael.y"
    { /* empty globals is OK */
		(yyval.pval) = npval2(PV_GLOBALS, &(yylsp[-2]), &(yylsp[0])); ;}
    break;

  case 22:
#line 251 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 23:
#line 252 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));;}
    break;

  case 24:
#line 253 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 25:
#line 256 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 26:
#line 256 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 27:
#line 262 "ael.y"
    {
		(yyval.pval)= npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str); ;}
    break;

  case 28:
#line 265 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[0]));
		z->u1.str = (yyvsp[0].str);
		(yyval.pval) = linku1((yyvsp[-2].pval),z); ;}
    break;

  case 29:
#line 269 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 30:
#line 272 "ael.y"
    { (yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 31:
#line 273 "ael.y"
    {(yyval.pval)=0;;}
    break;

  case 32:
#line 274 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
				else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
				else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 33:
#line 277 "ael.y"
    { (yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 34:
#line 280 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 35:
#line 281 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 36:
#line 282 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 37:
#line 283 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 38:
#line 284 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 39:
#line 285 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 40:
#line 285 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 41:
#line 289 "ael.y"
    {free((yyvsp[-1].str)); (yyval.pval)=0;;}
    break;

  case 42:
#line 290 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 43:
#line 293 "ael.y"
    {
		(yyval.pval) = npval2(PV_IGNOREPAT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 44:
#line 298 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 45:
#line 302 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;;}
    break;

  case 46:
#line 307 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 47:
#line 312 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 48:
#line 321 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 49:
#line 322 "ael.y"
    {if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 50:
#line 325 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 51:
#line 328 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 52:
#line 328 "ael.y"
    {
		(yyval.pval)= npval2(PV_IF, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 53:
#line 333 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 54:
#line 333 "ael.y"
    {
		(yyval.pval) = npval2(PV_RANDOM, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str=(yyvsp[-1].str);;}
    break;

  case 55:
#line 339 "ael.y"
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
#line 354 "ael.y"
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
#line 374 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 58:
#line 375 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 59:
#line 382 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 60:
#line 383 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 61:
#line 388 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s%s", (yyvsp[-2].str), (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word=(yyval.str);;}
    break;

  case 62:
#line 396 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 63:
#line 397 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));;}
    break;

  case 64:
#line 401 "ael.y"
    {
		asprintf(&((yyval.str)), "%s:%s", (yyvsp[-2].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[0].str));;}
    break;

  case 65:
#line 407 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 66:
#line 407 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCH, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 67:
#line 415 "ael.y"
    {
		(yyval.pval) = npval2(PV_STATEMENTBLOCK, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 68:
#line 418 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 69:
#line 418 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 70:
#line 422 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 71:
#line 425 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 72:
#line 428 "ael.y"
    {
		(yyval.pval) = npval2(PV_LABEL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 73:
#line 431 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 74:
#line 432 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 75:
#line 433 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 76:
#line 433 "ael.y"
    {
		(yyval.pval) = npval2(PV_FOR, &(yylsp[-11]), &(yylsp[0]));
		(yyval.pval)->u1.for_init = (yyvsp[-8].str);
		(yyval.pval)->u2.for_test=(yyvsp[-5].str);
		(yyval.pval)->u3.for_inc = (yyvsp[-2].str);
		(yyval.pval)->u4.for_statements = (yyvsp[0].pval);;}
    break;

  case 77:
#line 439 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 78:
#line 439 "ael.y"
    {
		(yyval.pval) = npval2(PV_WHILE, &(yylsp[-5]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 79:
#line 443 "ael.y"
    {
		(yyval.pval)=(yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 80:
#line 447 "ael.y"
    {
		(yyval.pval)=(yyvsp[-2].pval);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 81:
#line 452 "ael.y"
    {
		(yyval.pval) = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[-1]).last_line;
		(yyval.pval)->endcol = (yylsp[-1]).last_column;;}
    break;

  case 82:
#line 456 "ael.y"
    {
		(yyval.pval) = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[0]).last_line;
		(yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 83:
#line 460 "ael.y"
    {
		(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 84:
#line 463 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 85:
#line 463 "ael.y"
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
#line 496 "ael.y"
    { (yyval.pval) = npval2(PV_BREAK, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 87:
#line 497 "ael.y"
    { (yyval.pval) = npval2(PV_RETURN, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 88:
#line 498 "ael.y"
    { (yyval.pval) = npval2(PV_CONTINUE, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 89:
#line 499 "ael.y"
    {
		(yyval.pval)=(yyvsp[-2].pval);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[-1]).last_line;
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);
		(yyval.pval)->endcol = (yylsp[-1]).last_column;;}
    break;

  case 90:
#line 513 "ael.y"
    {
		(yyval.pval)=(yyvsp[-2].pval);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[-1]).last_line;
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);
		(yyval.pval)->endcol = (yylsp[-1]).last_column;;}
    break;

  case 91:
#line 527 "ael.y"
    {
		(yyval.pval)=(yyvsp[-2].pval);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->endline = (yylsp[-1]).last_line;
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);
		(yyval.pval)->endcol = (yylsp[-1]).last_column;;}
    break;

  case 92:
#line 541 "ael.y"
    { (yyval.pval)=0; ;}
    break;

  case 93:
#line 544 "ael.y"
    { (yyval.pval) = (yyvsp[0].pval); ;}
    break;

  case 94:
#line 545 "ael.y"
    { (yyval.pval) = NULL ; ;}
    break;

  case 95:
#line 550 "ael.y"
    { (yyval.pval) = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str);;}
    break;

  case 96:
#line 552 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 97:
#line 557 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 98:
#line 562 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 99:
#line 569 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 100:
#line 576 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 101:
#line 583 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[0].str); ;}
    break;

  case 102:
#line 592 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0])); /* XXX not really @1 */
		(yyval.pval)->next->u1.str = strdup("1");;}
    break;

  case 103:
#line 597 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[0].str);;}
    break;

  case 104:
#line 602 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[-2].str); ;}
    break;

  case 105:
#line 609 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = (yyvsp[0].str);
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = strdup("1"); ;}
    break;

  case 106:
#line 616 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-4]), &(yylsp[-4]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->next->u1.str = (yyvsp[-4].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = (yyvsp[-2].str); ;}
    break;

  case 107:
#line 623 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-2]), &(yylsp[-2]));
		(yyval.pval)->u1.str = strdup("default");
		(yyval.pval)->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->u1.str = (yyvsp[-2].str);
		(yyval.pval)->next->next = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->next->next->u1.str = strdup("1"); ;}
    break;

  case 108:
#line 632 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 109:
#line 632 "ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		(yyval.pval) = npval2(PV_MACRO_CALL, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.arglist = (yyvsp[-1].pval);;}
    break;

  case 110:
#line 637 "ael.y"
    {
		(yyval.pval)= npval2(PV_MACRO_CALL, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 111:
#line 642 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 112:
#line 642 "ael.y"
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
#line 652 "ael.y"
    {(yyval.pval) = (yyvsp[-2].pval);
 		if( (yyval.pval)->type == PV_GOTO )
			(yyval.pval)->u1.list = (yyvsp[-1].pval);
	 	else
			(yyval.pval)->u2.arglist = (yyvsp[-1].pval);
 		(yyval.pval)->endline = (yylsp[0]).last_line; (yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 114:
#line 658 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);(yyval.pval)->endline = (yylsp[0]).last_line; (yyval.pval)->endcol = (yylsp[0]).last_column;;}
    break;

  case 115:
#line 661 "ael.y"
    { (yyval.str) = (yyvsp[0].str) ;}
    break;

  case 116:
#line 662 "ael.y"
    { (yyval.str) = strdup(""); ;}
    break;

  case 117:
#line 665 "ael.y"
    { 
		(yyval.pval)= npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str);;}
    break;

  case 118:
#line 668 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		(yyval.pval)->u1.str = strdup(""); ;}
    break;

  case 119:
#line 671 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[0]), &(yylsp[0]));
		(yyval.pval) = (yyvsp[-2].pval);
		linku1((yyvsp[-2].pval),z);
		z->u1.str = (yyvsp[0].str);;}
    break;

  case 120:
#line 685 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 121:
#line 686 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 122:
#line 691 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-3]), &(yylsp[-1])); /* XXX 3 or 4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 123:
#line 695 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 124:
#line 699 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-3]), &(yylsp[0])); /* XXX@3 or @4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 125:
#line 703 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 126:
#line 706 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;;}
    break;

  case 127:
#line 709 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 128:
#line 714 "ael.y"
    {(yyval.pval) = (yyvsp[0].pval);;}
    break;

  case 129:
#line 715 "ael.y"
    { if ( (yyvsp[-1].pval) && (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[-1].pval); linku1((yyval.pval),(yyvsp[0].pval));}
						 else if ( (yyvsp[-1].pval) ) {(yyval.pval)=(yyvsp[-1].pval);}
						 else if ( (yyvsp[0].pval) ) {(yyval.pval)=(yyvsp[0].pval);} ;}
    break;

  case 130:
#line 720 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 131:
#line 721 "ael.y"
    {
		(yyval.pval) = npval2(PV_CATCH, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 132:
#line 727 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 133:
#line 730 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[-2]), &(yylsp[0])); ;}
    break;

  case 134:
#line 734 "ael.y"
    {
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 135:
#line 737 "ael.y"
    { /* empty switch list OK */
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[-2]), &(yylsp[0])); ;}
    break;

  case 136:
#line 741 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 137:
#line 744 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[0]));
		z->u1.str = (yyvsp[-1].str);
		(yyval.pval)=(yyvsp[-2].pval);
		linku1((yyval.pval),z); ;}
    break;

  case 138:
#line 749 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 139:
#line 752 "ael.y"
    {
		(yyval.pval) = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 140:
#line 756 "ael.y"
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
#line 772 "ael.y"
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
#line 785 "ael.y"
    {
		pval *z = npval2(PV_WORD, &(yylsp[-1]), &(yylsp[0])); /* XXX don't we need @1-@3 ?*/
		(yyval.pval)=(yyvsp[-2].pval);
		z->u1.str = (yyvsp[-1].str);
		linku1((yyval.pval),z); ;}
    break;

  case 143:
#line 791 "ael.y"
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
#line 808 "ael.y"
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
#line 823 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 146:
#line 826 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 147:
#line 827 "ael.y"
    {(yyval.str)=strdup("default");;}
    break;

  case 148:
#line 830 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 149:
#line 833 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-2]), &(yylsp[0]));;}
    break;


      default: break;
    }

/* Line 1126 of yacc.c.  */
#line 3045 "ael.tab.c"

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


#line 838 "ael.y"


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


