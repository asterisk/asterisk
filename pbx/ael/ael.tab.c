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
#define YYLAST   302

/* YYNTOKENS -- Number of terminals. */
#define YYNTOKENS  42
/* YYNNTS -- Number of nonterminals. */
#define YYNNTS  54
/* YYNRULES -- Number of rules. */
#define YYNRULES  132
/* YYNRULES -- Number of states. */
#define YYNSTATES  262

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
      21,    23,    25,    30,    32,    33,    42,    47,    48,    51,
      54,    55,    61,    62,    64,    68,    71,    75,    76,    79,
      82,    84,    86,    88,    90,    92,    94,    97,    99,   104,
     108,   113,   121,   130,   131,   134,   137,   143,   145,   153,
     154,   159,   162,   165,   170,   172,   175,   177,   180,   184,
     186,   189,   193,   199,   203,   205,   209,   213,   216,   217,
     218,   219,   232,   236,   238,   242,   245,   248,   249,   255,
     258,   261,   264,   268,   270,   273,   274,   276,   280,   284,
     290,   296,   302,   308,   310,   314,   320,   324,   325,   331,
     335,   336,   340,   344,   347,   349,   350,   352,   353,   357,
     358,   361,   366,   370,   375,   376,   378,   381,   383,   389,
     392,   395,   399,   402,   405,   409,   412,   414,   418,   421,
     425,   428,   433
};

/* YYRHS -- A `-1'-separated list of the rules' RHS. */
static const yysigned_char yyrhs[] =
{
      43,     0,    -1,    44,    -1,    45,    -1,    44,    45,    -1,
      44,     1,    -1,    47,    -1,    49,    -1,    50,    -1,     8,
      -1,    41,    -1,    36,    -1,    48,     3,    46,    55,    -1,
      23,    -1,    -1,    15,    41,     6,    54,     7,     4,    87,
       5,    -1,    16,     4,    51,     5,    -1,    -1,    52,    51,
      -1,    51,     1,    -1,    -1,    41,     9,    53,    41,     8,
      -1,    -1,    41,    -1,    54,    10,    41,    -1,    54,     1,
      -1,     4,    56,     5,    -1,    -1,    57,    56,    -1,    56,
       1,    -1,    59,    -1,    95,    -1,    89,    -1,    90,    -1,
      58,    -1,    52,    -1,    41,     1,    -1,     8,    -1,    17,
      24,    41,     8,    -1,    41,    24,    70,    -1,    30,    41,
      24,    70,    -1,    31,     6,    67,     7,    41,    24,    70,
      -1,    30,    31,     6,    67,     7,    41,    24,    70,    -1,
      -1,    70,    60,    -1,    60,     1,    -1,    67,    11,    67,
      11,    67,    -1,    41,    -1,    61,    13,    67,    13,    67,
      13,    67,    -1,    -1,     6,    64,    66,     7,    -1,    19,
      63,    -1,    22,    63,    -1,    20,     6,    62,     7,    -1,
      41,    -1,    41,    41,    -1,    41,    -1,    41,    41,    -1,
      41,    41,    41,    -1,    41,    -1,    41,    41,    -1,    41,
      11,    41,    -1,    18,    63,     4,    85,     5,    -1,     4,
      60,     5,    -1,    52,    -1,    25,    76,     8,    -1,    26,
      77,     8,    -1,    41,    11,    -1,    -1,    -1,    -1,    32,
       6,    71,    41,     8,    72,    41,     8,    73,    41,     7,
      70,    -1,    33,    63,    70,    -1,    69,    -1,    12,    78,
       8,    -1,    82,     8,    -1,    41,     8,    -1,    -1,    82,
       9,    74,    41,     8,    -1,    28,     8,    -1,    27,     8,
      -1,    29,     8,    -1,    65,    70,    75,    -1,     8,    -1,
      21,    70,    -1,    -1,    68,    -1,    68,    13,    68,    -1,
      68,    10,    68,    -1,    68,    13,    68,    13,    68,    -1,
      68,    10,    68,    10,    68,    -1,    36,    13,    68,    13,
      68,    -1,    36,    10,    68,    10,    68,    -1,    68,    -1,
      68,    10,    68,    -1,    68,    10,    41,    14,    46,    -1,
      68,    14,    46,    -1,    -1,    41,     6,    79,    84,     7,
      -1,    41,     6,     7,    -1,    -1,    41,     6,    81,    -1,
      80,    84,     7,    -1,    80,     7,    -1,    41,    -1,    -1,
      66,    -1,    -1,    84,    10,    83,    -1,    -1,    86,    85,
      -1,    34,    41,    11,    60,    -1,    36,    11,    60,    -1,
      35,    41,    11,    60,    -1,    -1,    88,    -1,    87,    88,
      -1,    70,    -1,    37,    41,     4,    60,     5,    -1,    38,
      91,    -1,    39,    91,    -1,     4,    92,     5,    -1,     4,
       5,    -1,    41,     8,    -1,    92,    41,     8,    -1,    92,
       1,    -1,    46,    -1,    46,    13,    62,    -1,    93,     8,
      -1,    94,    93,     8,    -1,    94,     1,    -1,    40,     4,
      94,     5,    -1,    40,     4,     5,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const unsigned short int yyrline[] =
{
       0,   180,   180,   183,   184,   185,   188,   189,   190,   191,
     194,   195,   198,   206,   207,   210,   215,   220,   221,   222,
     225,   225,   231,   232,   233,   234,   237,   240,   241,   242,
     245,   246,   247,   248,   249,   250,   251,   252,   255,   260,
     264,   269,   274,   284,   285,   286,   292,   297,   301,   309,
     309,   314,   317,   320,   331,   332,   339,   340,   345,   353,
     354,   358,   364,   373,   376,   377,   380,   383,   386,   387,
     388,   386,   394,   398,   399,   401,   403,   406,   406,   439,
     440,   441,   442,   446,   449,   450,   455,   456,   459,   462,
     466,   470,   474,   481,   484,   487,   491,   497,   497,   502,
     510,   510,   521,   528,   531,   532,   535,   536,   539,   542,
     543,   546,   550,   554,   560,   561,   562,   565,   566,   572,
     577,   582,   583,   586,   587,   588,   591,   592,   599,   600,
     601,   604,   607
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
  "globals", "global_statements", "assignment", "@1", "arglist",
  "elements_block", "elements", "element", "ignorepat", "extension",
  "statements", "timerange", "timespec", "test_expr", "@2", "if_like_head",
  "word_list", "word3_list", "goto_word", "switch_statement", "statement",
  "@3", "@4", "@5", "@6", "opt_else", "target", "jumptarget", "macro_call",
  "@7", "application_call_head", "@8", "application_call", "opt_word",
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
      46,    46,    47,    48,    48,    49,    50,    51,    51,    51,
      53,    52,    54,    54,    54,    54,    55,    56,    56,    56,
      57,    57,    57,    57,    57,    57,    57,    57,    58,    59,
      59,    59,    59,    60,    60,    60,    61,    61,    62,    64,
      63,    65,    65,    65,    66,    66,    67,    67,    67,    68,
      68,    68,    69,    70,    70,    70,    70,    70,    71,    72,
      73,    70,    70,    70,    70,    70,    70,    74,    70,    70,
      70,    70,    70,    70,    75,    75,    76,    76,    76,    76,
      76,    76,    76,    77,    77,    77,    77,    79,    78,    78,
      81,    80,    82,    82,    83,    83,    84,    84,    84,    85,
      85,    86,    86,    86,    87,    87,    87,    88,    88,    89,
      90,    91,    91,    92,    92,    92,    93,    93,    94,    94,
      94,    95,    95
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const unsigned char yyr2[] =
{
       0,     2,     1,     1,     2,     2,     1,     1,     1,     1,
       1,     1,     4,     1,     0,     8,     4,     0,     2,     2,
       0,     5,     0,     1,     3,     2,     3,     0,     2,     2,
       1,     1,     1,     1,     1,     1,     2,     1,     4,     3,
       4,     7,     8,     0,     2,     2,     5,     1,     7,     0,
       4,     2,     2,     4,     1,     2,     1,     2,     3,     1,
       2,     3,     5,     3,     1,     3,     3,     2,     0,     0,
       0,    12,     3,     1,     3,     2,     2,     0,     5,     2,
       2,     2,     3,     1,     2,     0,     1,     3,     3,     5,
       5,     5,     5,     1,     3,     5,     3,     0,     5,     3,
       0,     3,     3,     2,     1,     0,     1,     0,     3,     0,
       2,     4,     3,     4,     0,     1,     2,     1,     5,     2,
       2,     3,     2,     2,     3,     2,     1,     3,     2,     3,
       2,     4,     3
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const unsigned char yydefact[] =
{
      14,     9,     0,     0,    13,     0,     0,     3,     6,     0,
       7,     8,     0,    17,     1,     5,     4,     0,    22,     0,
       0,    17,    11,    10,     0,    23,     0,    20,    19,    16,
       0,    27,    12,    25,     0,     0,     0,    37,     0,     0,
       0,     0,     0,     0,     0,    35,     0,    27,    34,    30,
      32,    33,    31,   114,    24,     0,     0,     0,     0,     0,
       0,   119,   120,     0,    36,     0,    29,    26,     0,    43,
      83,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,    64,     0,    73,   117,   107,
       0,     0,   115,    21,     0,     0,     0,    56,     0,   122,
       0,     0,   132,   126,     0,     0,    39,     0,    43,     0,
       0,    49,     0,    51,     0,    52,     0,    59,    86,     0,
      93,     0,    80,    79,    81,    68,     0,     0,   100,    76,
      67,    85,   103,    54,   106,     0,    75,    77,    15,   116,
      38,     0,    40,    57,     0,   123,   125,   121,     0,     0,
     128,   130,   131,     0,    45,    63,     0,    97,    74,     0,
     109,    47,     0,     0,     0,     0,     0,     0,    60,     0,
       0,    65,     0,     0,    66,     0,    72,    43,   101,     0,
      82,    55,   102,   105,     0,     0,    58,     0,   124,   127,
     129,    99,   107,     0,     0,     0,     0,     0,   109,     0,
      53,     0,     0,     0,    61,    88,    87,    59,    94,    96,
       0,     0,    84,   104,   108,     0,     0,     0,     0,    50,
       0,     0,    43,    62,   110,     0,     0,     0,     0,     0,
       0,     0,    69,   118,    78,     0,    41,    98,    43,    43,
       0,     0,     0,    92,    91,    90,    89,    95,     0,    42,
       0,     0,     0,    46,     0,     0,    70,    48,     0,     0,
       0,    71
};

/* YYDEFGOTO[NTERM-NUM]. */
static const short int yydefgoto[] =
{
      -1,     5,     6,     7,   103,     8,     9,    10,    11,    20,
      85,    36,    26,    32,    46,    47,    48,    49,   107,   162,
     163,   112,   159,    86,   134,   164,   118,    87,   108,   175,
     248,   258,   184,   180,   119,   121,   110,   192,    89,   178,
      90,   214,   135,   197,   198,    91,    92,    50,    51,    61,
     101,   104,   105,    52
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -106
static const short int yypact[] =
{
     122,  -106,   -25,    13,  -106,    36,   166,  -106,  -106,    40,
    -106,  -106,    69,    50,  -106,  -106,  -106,    21,    60,   102,
      93,    50,  -106,  -106,   128,  -106,    96,  -106,  -106,  -106,
     103,   123,  -106,  -106,   137,   107,   108,  -106,   151,    17,
     185,   188,   188,   200,   104,  -106,   124,   123,  -106,  -106,
    -106,  -106,  -106,     0,  -106,   197,   169,   201,   187,   171,
       9,  -106,  -106,     6,  -106,   168,  -106,  -106,   134,   168,
    -106,   172,   209,   209,   210,   209,    46,   176,   211,   212,
     213,   216,   209,   177,   101,  -106,   168,  -106,  -106,     8,
      59,    51,  -106,  -106,   215,   171,   168,   183,   218,  -106,
     219,     5,  -106,   217,   220,     4,  -106,   142,   168,   223,
     224,  -106,   222,  -106,   190,  -106,   158,    12,   160,   225,
     136,   226,  -106,  -106,  -106,  -106,   168,   231,  -106,  -106,
    -106,   221,  -106,   195,  -106,   192,  -106,  -106,  -106,  -106,
    -106,   230,  -106,   198,   199,  -106,  -106,  -106,   233,   190,
    -106,  -106,  -106,   235,  -106,  -106,    30,   237,  -106,   204,
     143,    19,   234,   239,   227,   176,   176,   207,  -106,   176,
     176,  -106,   208,    21,  -106,   214,  -106,   168,  -106,   168,
    -106,  -106,  -106,   228,   229,   232,  -106,   236,  -106,  -106,
    -106,  -106,   204,   243,   238,   240,   241,   246,   143,   171,
    -106,   171,   244,   245,  -106,   247,   248,    10,  -106,  -106,
     251,   150,  -106,  -106,  -106,   254,   242,   168,   196,  -106,
     252,   253,   168,  -106,  -106,   255,   256,   176,   176,   176,
     176,    21,  -106,  -106,  -106,   168,  -106,  -106,   168,   168,
      80,   171,   171,  -106,  -106,  -106,  -106,  -106,   249,  -106,
      85,    88,   258,  -106,   257,   171,  -106,  -106,   250,   265,
     168,  -106
};

/* YYPGOTO[NTERM-NUM].  */
static const short int yypgoto[] =
{
    -106,  -106,  -106,   268,   -17,  -106,  -106,  -106,  -106,   259,
     105,  -106,  -106,  -106,   206,  -106,  -106,  -106,  -105,  -106,
     126,   -21,  -106,  -106,    97,   -57,   -70,  -106,   -52,  -106,
    -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,
    -106,  -106,    84,    79,  -106,  -106,   191,  -106,  -106,   260,
    -106,   173,  -106,  -106
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -114
static const short int yytable[] =
{
      24,    88,    98,   156,    69,   151,   146,   120,    70,   152,
     147,   102,    71,   106,    99,   132,    12,    13,    72,    73,
      74,   167,    75,   167,   231,    76,    77,    78,    79,    80,
     -56,   154,    81,    82,   131,   -44,    14,    83,   141,    88,
      22,    84,    22,    17,   142,    23,   148,    23,    57,   133,
     100,   168,   113,   168,   115,    69,   138,    22,    58,    70,
     143,   126,    23,    71,   -44,   -44,   -44,   136,   137,    72,
      73,    74,   211,    75,   176,    18,    76,    77,    78,    79,
      80,   154,   116,    81,    82,  -112,   154,   117,    83,   154,
    -111,    19,    84,  -113,    28,   202,   203,    33,    29,   205,
     206,    25,   208,    34,    28,    64,    35,   128,   -18,   129,
      27,    27,   130,    27,  -112,  -112,  -112,   240,    21,  -111,
    -111,  -111,  -113,  -113,  -113,    66,    21,   212,    65,    67,
       1,    37,    31,   250,   251,    66,    45,     2,     3,   -28,
      38,    53,   225,   154,   226,     4,   172,   155,    54,    55,
     173,   154,    45,    39,    40,   233,   209,   243,   244,   245,
     246,    41,    42,    43,    44,   236,    -2,    15,   165,   -14,
     169,   166,    69,   170,     1,    56,    70,   194,   195,   196,
      71,     2,     3,   249,   252,   253,    72,    73,    74,     4,
      75,    59,    60,    76,    77,    78,    79,    80,   257,   182,
      81,    82,   183,   237,    63,    93,   183,    95,   261,    84,
      94,    96,    97,   109,   247,   111,   114,   117,   127,   122,
     123,   124,   125,   140,   143,   144,   160,   145,   150,   157,
     149,   161,   158,   171,   174,   177,   181,   185,   201,   186,
     187,   188,   179,   190,   191,   133,   200,   199,   204,   207,
     219,   223,   222,    68,   227,   210,   193,   229,   228,   232,
     217,   230,   234,   238,   239,   256,   235,   242,   241,   213,
     215,   255,   260,   216,    16,   189,   218,   224,   153,   220,
      30,   221,   139,     0,     0,     0,     0,     0,     0,     0,
     254,   259,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,    62
};

static const short int yycheck[] =
{
      17,    53,    59,   108,     4,     1,     1,    77,     8,     5,
       5,     5,    12,    65,     5,     7,    41,     4,    18,    19,
      20,    11,    22,    11,    14,    25,    26,    27,    28,    29,
      11,     1,    32,    33,    86,     5,     0,    37,    95,    91,
      36,    41,    36,     3,    96,    41,    41,    41,    31,    41,
      41,    41,    73,    41,    75,     4,     5,    36,    41,     8,
      41,    82,    41,    12,    34,    35,    36,     8,     9,    18,
      19,    20,   177,    22,   126,     6,    25,    26,    27,    28,
      29,     1,    36,    32,    33,     5,     1,    41,    37,     1,
       5,    41,    41,     5,     1,   165,   166,     1,     5,   169,
     170,    41,   172,     7,     1,     1,    10,     6,     5,     8,
       9,     9,    11,     9,    34,    35,    36,   222,    13,    34,
      35,    36,    34,    35,    36,     1,    21,   179,    24,     5,
       8,     8,     4,   238,   239,     1,    31,    15,    16,     5,
      17,     4,   199,     1,   201,    23,    10,     5,    41,    41,
      14,     1,    47,    30,    31,     5,   173,   227,   228,   229,
     230,    38,    39,    40,    41,   217,     0,     1,    10,     3,
      10,    13,     4,    13,     8,    24,     8,    34,    35,    36,
      12,    15,    16,   235,   241,   242,    18,    19,    20,    23,
      22,     6,     4,    25,    26,    27,    28,    29,   255,     7,
      32,    33,    10,     7,     4,     8,    10,     6,   260,    41,
      41,    24,    41,    41,   231,     6,     6,    41,    41,     8,
       8,     8,     6,     8,    41,     7,     4,     8,     8,     6,
      13,    41,     8,     8,     8,     4,    41,     7,    11,    41,
      41,     8,    21,     8,     7,    41,     7,    13,    41,    41,
       7,     5,    11,    47,    10,    41,   159,    10,    13,     8,
      24,    13,     8,    11,    11,     8,    24,    11,    13,    41,
      41,    13,     7,    41,     6,   149,   192,   198,   105,    41,
      21,    41,    91,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      41,    41,    -1,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    -1,    42
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const unsigned char yystos[] =
{
       0,     8,    15,    16,    23,    43,    44,    45,    47,    48,
      49,    50,    41,     4,     0,     1,    45,     3,     6,    41,
      51,    52,    36,    41,    46,    41,    54,     9,     1,     5,
      51,     4,    55,     1,     7,    10,    53,     8,    17,    30,
      31,    38,    39,    40,    41,    52,    56,    57,    58,    59,
      89,    90,    95,     4,    41,    41,    24,    31,    41,     6,
       4,    91,    91,     4,     1,    24,     1,     5,    56,     4,
       8,    12,    18,    19,    20,    22,    25,    26,    27,    28,
      29,    32,    33,    37,    41,    52,    65,    69,    70,    80,
      82,    87,    88,     8,    41,     6,    24,    41,    67,     5,
      41,    92,     5,    46,    93,    94,    70,    60,    70,    41,
      78,     6,    63,    63,     6,    63,    36,    41,    68,    76,
      68,    77,     8,     8,     8,     6,    63,    41,     6,     8,
      11,    70,     7,    41,    66,    84,     8,     9,     5,    88,
       8,    67,    70,    41,     7,     8,     1,     5,    41,    13,
       8,     1,     5,    93,     1,     5,    60,     6,     8,    64,
       4,    41,    61,    62,    67,    10,    13,    11,    41,    10,
      13,     8,    10,    14,     8,    71,    70,     4,    81,    21,
      75,    41,     7,    10,    74,     7,    41,    41,     8,    62,
       8,     7,    79,    66,    34,    35,    36,    85,    86,    13,
       7,    11,    68,    68,    41,    68,    68,    41,    68,    46,
      41,    60,    70,    41,    83,    41,    41,    24,    84,     7,
      41,    41,    11,     5,    85,    67,    67,    10,    13,    10,
      13,    14,     8,     5,     8,    24,    70,     7,    11,    11,
      60,    13,    11,    68,    68,    68,    68,    46,    72,    70,
      60,    60,    67,    67,    41,    13,     8,    67,    73,    41,
       7,    70
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
#line 1216 "ael.tab.c"
        break;
      case 44: /* "objects" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1224 "ael.tab.c"
        break;
      case 45: /* "object" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1232 "ael.tab.c"
        break;
      case 46: /* "context_name" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1237 "ael.tab.c"
        break;
      case 47: /* "context" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1245 "ael.tab.c"
        break;
      case 49: /* "macro" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1253 "ael.tab.c"
        break;
      case 50: /* "globals" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1261 "ael.tab.c"
        break;
      case 51: /* "global_statements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1269 "ael.tab.c"
        break;
      case 52: /* "assignment" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1277 "ael.tab.c"
        break;
      case 54: /* "arglist" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1285 "ael.tab.c"
        break;
      case 55: /* "elements_block" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1293 "ael.tab.c"
        break;
      case 56: /* "elements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1301 "ael.tab.c"
        break;
      case 57: /* "element" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1309 "ael.tab.c"
        break;
      case 58: /* "ignorepat" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1317 "ael.tab.c"
        break;
      case 59: /* "extension" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1325 "ael.tab.c"
        break;
      case 60: /* "statements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1333 "ael.tab.c"
        break;
      case 61: /* "timerange" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1338 "ael.tab.c"
        break;
      case 62: /* "timespec" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1346 "ael.tab.c"
        break;
      case 63: /* "test_expr" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1351 "ael.tab.c"
        break;
      case 65: /* "if_like_head" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1359 "ael.tab.c"
        break;
      case 66: /* "word_list" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1364 "ael.tab.c"
        break;
      case 67: /* "word3_list" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1369 "ael.tab.c"
        break;
      case 68: /* "goto_word" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1374 "ael.tab.c"
        break;
      case 69: /* "switch_statement" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1382 "ael.tab.c"
        break;
      case 70: /* "statement" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1390 "ael.tab.c"
        break;
      case 75: /* "opt_else" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1398 "ael.tab.c"
        break;
      case 76: /* "target" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1406 "ael.tab.c"
        break;
      case 77: /* "jumptarget" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1414 "ael.tab.c"
        break;
      case 78: /* "macro_call" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1422 "ael.tab.c"
        break;
      case 80: /* "application_call_head" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1430 "ael.tab.c"
        break;
      case 82: /* "application_call" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1438 "ael.tab.c"
        break;
      case 83: /* "opt_word" */
#line 173 "ael.y"
        { free((yyvaluep->str));};
#line 1443 "ael.tab.c"
        break;
      case 84: /* "eval_arglist" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1451 "ael.tab.c"
        break;
      case 85: /* "case_statements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1459 "ael.tab.c"
        break;
      case 86: /* "case_statement" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1467 "ael.tab.c"
        break;
      case 87: /* "macro_statements" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1475 "ael.tab.c"
        break;
      case 88: /* "macro_statement" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1483 "ael.tab.c"
        break;
      case 89: /* "switches" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1491 "ael.tab.c"
        break;
      case 90: /* "eswitches" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1499 "ael.tab.c"
        break;
      case 91: /* "switchlist_block" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1507 "ael.tab.c"
        break;
      case 92: /* "switchlist" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1515 "ael.tab.c"
        break;
      case 93: /* "included_entry" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1523 "ael.tab.c"
        break;
      case 94: /* "includeslist" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1531 "ael.tab.c"
        break;
      case 95: /* "includes" */
#line 159 "ael.y"
        {
		destroy_pval((yyvaluep->pval));
		prev_word=0;
	};
#line 1539 "ael.tab.c"
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
#line 220 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 18:
#line 221 "ael.y"
    {(yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 19:
#line 222 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 20:
#line 225 "ael.y"
    { reset_semicount(parseio->scanner); ;}
    break;

  case 21:
#line 225 "ael.y"
    {
		(yyval.pval) = npval2(PV_VARDEC, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.val = (yyvsp[-1].str); ;}
    break;

  case 22:
#line 231 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 23:
#line 232 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 24:
#line 233 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[0].str), &(yylsp[0]))); ;}
    break;

  case 25:
#line 234 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 26:
#line 237 "ael.y"
    { (yyval.pval) = (yyvsp[-1].pval); ;}
    break;

  case 27:
#line 240 "ael.y"
    {(yyval.pval)=0;;}
    break;

  case 28:
#line 241 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 29:
#line 242 "ael.y"
    { (yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 30:
#line 245 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 31:
#line 246 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 32:
#line 247 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 33:
#line 248 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 34:
#line 249 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 35:
#line 250 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 36:
#line 251 "ael.y"
    {free((yyvsp[-1].str)); (yyval.pval)=0;;}
    break;

  case 37:
#line 252 "ael.y"
    {(yyval.pval)=0;/* allow older docs to be read */;}
    break;

  case 38:
#line 255 "ael.y"
    {
		(yyval.pval) = npval2(PV_IGNOREPAT, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 39:
#line 260 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 40:
#line 264 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;;}
    break;

  case 41:
#line 269 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-6]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 42:
#line 274 "ael.y"
    {
		(yyval.pval) = npval2(PV_EXTENSION, &(yylsp[-7]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);
		(yyval.pval)->u4.regexten=1;
		(yyval.pval)->u3.hints = (yyvsp[-4].str);;}
    break;

  case 43:
#line 284 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 44:
#line 285 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 45:
#line 286 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 46:
#line 292 "ael.y"
    {
		asprintf(&(yyval.str), "%s:%s:%s", (yyvsp[-4].str), (yyvsp[-2].str), (yyvsp[0].str));
		free((yyvsp[-4].str));
		free((yyvsp[-2].str));
		free((yyvsp[0].str)); ;}
    break;

  case 47:
#line 297 "ael.y"
    { (yyval.str) = (yyvsp[0].str); ;}
    break;

  case 48:
#line 301 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-6].str), &(yylsp[-6]));
		(yyval.pval)->next = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 49:
#line 309 "ael.y"
    { reset_parencount(parseio->scanner); ;}
    break;

  case 50:
#line 309 "ael.y"
    {
		(yyval.str) = (yyvsp[-1].str); ;}
    break;

  case 51:
#line 314 "ael.y"
    {
		(yyval.pval)= npval2(PV_IF, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[0].str); ;}
    break;

  case 52:
#line 317 "ael.y"
    {
		(yyval.pval) = npval2(PV_RANDOM, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str=(yyvsp[0].str);;}
    break;

  case 53:
#line 320 "ael.y"
    {
		(yyval.pval) = npval2(PV_IFTIME, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);
		prev_word = 0; ;}
    break;

  case 54:
#line 331 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 55:
#line 332 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 56:
#line 339 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 57:
#line 340 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word = (yyval.str);;}
    break;

  case 58:
#line 345 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s%s", (yyvsp[-2].str), (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));
		prev_word=(yyval.str);;}
    break;

  case 59:
#line 353 "ael.y"
    { (yyval.str) = (yyvsp[0].str);;}
    break;

  case 60:
#line 354 "ael.y"
    {
		asprintf(&((yyval.str)), "%s%s", (yyvsp[-1].str), (yyvsp[0].str));
		free((yyvsp[-1].str));
		free((yyvsp[0].str));;}
    break;

  case 61:
#line 358 "ael.y"
    {
		asprintf(&((yyval.str)), "%s:%s", (yyvsp[-2].str), (yyvsp[0].str));
		free((yyvsp[-2].str));
		free((yyvsp[0].str));;}
    break;

  case 62:
#line 364 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCH, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 63:
#line 373 "ael.y"
    {
		(yyval.pval) = npval2(PV_STATEMENTBLOCK, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval); ;}
    break;

  case 64:
#line 376 "ael.y"
    { (yyval.pval) = (yyvsp[0].pval); ;}
    break;

  case 65:
#line 377 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 66:
#line 380 "ael.y"
    {
		(yyval.pval) = npval2(PV_GOTO, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 67:
#line 383 "ael.y"
    {
		(yyval.pval) = npval2(PV_LABEL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str); ;}
    break;

  case 68:
#line 386 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 69:
#line 387 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 70:
#line 388 "ael.y"
    {reset_parencount(parseio->scanner);;}
    break;

  case 71:
#line 388 "ael.y"
    { /* XXX word_list maybe ? */
		(yyval.pval) = npval2(PV_FOR, &(yylsp[-11]), &(yylsp[0]));
		(yyval.pval)->u1.for_init = (yyvsp[-8].str);
		(yyval.pval)->u2.for_test=(yyvsp[-5].str);
		(yyval.pval)->u3.for_inc = (yyvsp[-2].str);
		(yyval.pval)->u4.for_statements = (yyvsp[0].pval);;}
    break;

  case 72:
#line 394 "ael.y"
    {
		(yyval.pval) = npval2(PV_WHILE, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval); ;}
    break;

  case 73:
#line 398 "ael.y"
    { (yyval.pval) = (yyvsp[0].pval); ;}
    break;

  case 74:
#line 399 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[-1])); ;}
    break;

  case 75:
#line 401 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 76:
#line 403 "ael.y"
    {
		(yyval.pval)= npval2(PV_APPLICATION_CALL, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-1].str);;}
    break;

  case 77:
#line 406 "ael.y"
    {reset_semicount(parseio->scanner);;}
    break;

  case 78:
#line 406 "ael.y"
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

  case 79:
#line 439 "ael.y"
    { (yyval.pval) = npval2(PV_BREAK, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 80:
#line 440 "ael.y"
    { (yyval.pval) = npval2(PV_RETURN, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 81:
#line 441 "ael.y"
    { (yyval.pval) = npval2(PV_CONTINUE, &(yylsp[-1]), &(yylsp[0])); ;}
    break;

  case 82:
#line 442 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[-1]));
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);
		(yyval.pval)->u3.else_statements = (yyvsp[0].pval);;}
    break;

  case 83:
#line 446 "ael.y"
    { (yyval.pval)=0; ;}
    break;

  case 84:
#line 449 "ael.y"
    { (yyval.pval) = (yyvsp[0].pval); ;}
    break;

  case 85:
#line 450 "ael.y"
    { (yyval.pval) = NULL ; ;}
    break;

  case 86:
#line 455 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 87:
#line 456 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 88:
#line 459 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 89:
#line 462 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 90:
#line 466 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 91:
#line 470 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 92:
#line 474 "ael.y"
    {
		(yyval.pval) = nword(strdup("default"), &(yylsp[-4]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 93:
#line 481 "ael.y"
    {			/* ext, 1 */
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword(strdup("1"), &(yylsp[0])); ;}
    break;

  case 94:
#line 484 "ael.y"
    {		/* ext, pri */
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 95:
#line 487 "ael.y"
    {	/* context, ext, pri */
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword((yyvsp[-4].str), &(yylsp[-4]));
		(yyval.pval)->next->next = nword((yyvsp[-2].str), &(yylsp[-2])); ;}
    break;

  case 96:
#line 491 "ael.y"
    {		/* context, ext, 1 */
		(yyval.pval) = nword((yyvsp[0].str), &(yylsp[0]));
		(yyval.pval)->next = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->next->next = nword(strdup("1"), &(yylsp[0])); ;}
    break;

  case 97:
#line 497 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 98:
#line 497 "ael.y"
    {
		/* XXX original code had @2 but i think we need @5 */
		(yyval.pval) = npval2(PV_MACRO_CALL, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-4].str);
		(yyval.pval)->u2.arglist = (yyvsp[-1].pval);;}
    break;

  case 99:
#line 502 "ael.y"
    {
		(yyval.pval)= npval2(PV_MACRO_CALL, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-2].str); ;}
    break;

  case 100:
#line 510 "ael.y"
    {reset_argcount(parseio->scanner);;}
    break;

  case 101:
#line 510 "ael.y"
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

  case 102:
#line 521 "ael.y"
    {
		(yyval.pval) = update_last((yyvsp[-2].pval), &(yylsp[0]));
 		if( (yyval.pval)->type == PV_GOTO )
			(yyval.pval)->u1.list = (yyvsp[-1].pval);
	 	else
			(yyval.pval)->u2.arglist = (yyvsp[-1].pval);
	;}
    break;

  case 103:
#line 528 "ael.y"
    { (yyval.pval) = update_last((yyvsp[-1].pval), &(yylsp[0])); ;}
    break;

  case 104:
#line 531 "ael.y"
    { (yyval.str) = (yyvsp[0].str) ;}
    break;

  case 105:
#line 532 "ael.y"
    { (yyval.str) = strdup(""); ;}
    break;

  case 106:
#line 535 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 107:
#line 536 "ael.y"
    {
		(yyval.pval)= npval(PV_WORD,0/*@1.first_line*/,0/*@1.last_line*/,0/* @1.first_column*/, 0/*@1.last_column*/);
		(yyval.pval)->u1.str = strdup(""); ;}
    break;

  case 108:
#line 539 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[0].str), &(yylsp[0]))); ;}
    break;

  case 109:
#line 542 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 110:
#line 543 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 111:
#line 546 "ael.y"
    {
		(yyval.pval) = npval2(PV_CASE, &(yylsp[-3]), &(yylsp[-1])); /* XXX 3 or 4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 112:
#line 550 "ael.y"
    {
		(yyval.pval) = npval2(PV_DEFAULT, &(yylsp[-2]), &(yylsp[0]));
		(yyval.pval)->u1.str = NULL;
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 113:
#line 554 "ael.y"
    {
		(yyval.pval) = npval2(PV_PATTERN, &(yylsp[-3]), &(yylsp[0])); /* XXX@3 or @4 ? */
		(yyval.pval)->u1.str = (yyvsp[-2].str);
		(yyval.pval)->u2.statements = (yyvsp[0].pval);;}
    break;

  case 114:
#line 560 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 115:
#line 561 "ael.y"
    {(yyval.pval) = (yyvsp[0].pval);;}
    break;

  case 116:
#line 562 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-1].pval), (yyvsp[0].pval)); ;}
    break;

  case 117:
#line 565 "ael.y"
    {(yyval.pval)=(yyvsp[0].pval);;}
    break;

  case 118:
#line 566 "ael.y"
    {
		(yyval.pval) = npval2(PV_CATCH, &(yylsp[-4]), &(yylsp[0]));
		(yyval.pval)->u1.str = (yyvsp[-3].str);
		(yyval.pval)->u2.statements = (yyvsp[-1].pval);;}
    break;

  case 119:
#line 572 "ael.y"
    {
		(yyval.pval) = npval2(PV_SWITCHES, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[0].pval); ;}
    break;

  case 120:
#line 577 "ael.y"
    {
		(yyval.pval) = npval2(PV_ESWITCHES, &(yylsp[-1]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[0].pval); ;}
    break;

  case 121:
#line 582 "ael.y"
    { (yyval.pval) = (yyvsp[-1].pval); ;}
    break;

  case 122:
#line 583 "ael.y"
    { (yyval.pval) = NULL; ;}
    break;

  case 123:
#line 586 "ael.y"
    { (yyval.pval) = nword((yyvsp[-1].str), &(yylsp[-1])); ;}
    break;

  case 124:
#line 587 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), nword((yyvsp[-1].str), &(yylsp[-1]))); ;}
    break;

  case 125:
#line 588 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 126:
#line 591 "ael.y"
    { (yyval.pval) = nword((yyvsp[0].str), &(yylsp[0])); ;}
    break;

  case 127:
#line 592 "ael.y"
    {
		(yyval.pval) = nword((yyvsp[-2].str), &(yylsp[-2]));
		(yyval.pval)->u2.arglist = (yyvsp[0].pval);
		prev_word=0; /* XXX sure ? */ ;}
    break;

  case 128:
#line 599 "ael.y"
    { (yyval.pval) = (yyvsp[-1].pval); ;}
    break;

  case 129:
#line 600 "ael.y"
    { (yyval.pval) = linku1((yyvsp[-2].pval), (yyvsp[-1].pval)); ;}
    break;

  case 130:
#line 601 "ael.y"
    {(yyval.pval)=(yyvsp[-1].pval);;}
    break;

  case 131:
#line 604 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-3]), &(yylsp[0]));
		(yyval.pval)->u1.list = (yyvsp[-1].pval);;}
    break;

  case 132:
#line 607 "ael.y"
    {
		(yyval.pval) = npval2(PV_INCLUDES, &(yylsp[-2]), &(yylsp[0]));;}
    break;


      default: break;
    }

/* Line 1126 of yacc.c.  */
#line 2707 "ael.tab.c"

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


#line 612 "ael.y"


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

