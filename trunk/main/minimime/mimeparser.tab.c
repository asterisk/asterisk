/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Using locations.  */
#define YYLSP_NEEDED 0

/* Substitute the variable and function names.  */
#define yyparse mimeparser_yyparse
#define yylex   mimeparser_yylex
#define yyerror mimeparser_yyerror
#define yylval  mimeparser_yylval
#define yychar  mimeparser_yychar
#define yydebug mimeparser_yydebug
#define yynerrs mimeparser_yynerrs


/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     ANY = 258,
     COLON = 259,
     DASH = 260,
     DQUOTE = 261,
     ENDOFHEADERS = 262,
     EOL = 263,
     EOM = 264,
     EQUAL = 265,
     MIMEVERSION_HEADER = 266,
     SEMICOLON = 267,
     CONTENTDISPOSITION_HEADER = 268,
     CONTENTENCODING_HEADER = 269,
     CONTENTTYPE_HEADER = 270,
     MAIL_HEADER = 271,
     HEADERVALUE = 272,
     BOUNDARY = 273,
     ENDBOUNDARY = 274,
     CONTENTTYPE_VALUE = 275,
     TSPECIAL = 276,
     WORD = 277,
     BODY = 278,
     PREAMBLE = 279,
     POSTAMBLE = 280
   };
#endif
/* Tokens.  */
#define ANY 258
#define COLON 259
#define DASH 260
#define DQUOTE 261
#define ENDOFHEADERS 262
#define EOL 263
#define EOM 264
#define EQUAL 265
#define MIMEVERSION_HEADER 266
#define SEMICOLON 267
#define CONTENTDISPOSITION_HEADER 268
#define CONTENTENCODING_HEADER 269
#define CONTENTTYPE_HEADER 270
#define MAIL_HEADER 271
#define HEADERVALUE 272
#define BOUNDARY 273
#define ENDBOUNDARY 274
#define CONTENTTYPE_VALUE 275
#define TSPECIAL 276
#define WORD 277
#define BODY 278
#define PREAMBLE 279
#define POSTAMBLE 280




/* Copy the first part of user declarations.  */
#line 1 "mimeparser.y"

/*
 * Copyright (c) 2004 Jann Fischer. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * These are the grammatic definitions in yacc syntax to parse MIME conform
 * messages.
 *
 * TODO:
 *	- honour parse flags passed to us (partly done)
 *	- parse Content-Disposition header (partly done)
 *	- parse Content-Encoding header
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "mimeparser.h"
#include "mm.h"
#include "mm_internal.h"

int set_boundary(char *,struct parser_state *);
int mimeparser_yywrap(void);
void reset_environ(struct parser_state *pstate);
int PARSER_initialize(struct parser_state *pstate, void *yyscanner);

static char *PARSE_readmessagepart(size_t, size_t, size_t, size_t *,yyscan_t, struct parser_state *);
FILE *mimeparser_yyget_in (yyscan_t yyscanner );



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

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 67 "mimeparser.y"
{
	int number;
	char *string;
	struct s_position position;
}
/* Line 187 of yacc.c.  */
#line 220 "mimeparser.tab.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



/* Copy the second part of user declarations.  */


/* Line 216 of yacc.c.  */
#line 233 "mimeparser.tab.c"

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
# if defined(YYENABLE_NLS)
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
	 || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
  };

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

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
#define YYFINAL  26
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   61

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  28
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  29
/* YYNRULES -- Number of rules.  */
#define YYNRULES  50
/* YYNRULES -- Number of states.  */
#define YYNSTATES  83

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
       2,     2,     2,     2,    27,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,    26,     2,     2,
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
       0,     0,     3,     5,     7,     8,    15,    18,    21,    23,
      25,    27,    28,    30,    31,    34,    36,    40,    42,    44,
      46,    48,    50,    52,    57,    61,    66,    72,    77,    83,
      85,    90,    95,    98,   101,   103,   107,   111,   114,   116,
     120,   123,   125,   129,   133,   135,   137,   141,   143,   146,
     148
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      29,     0,    -1,    30,    -1,    32,    -1,    -1,    33,    34,
      31,    36,    55,    35,    -1,    33,    56,    -1,    38,    33,
      -1,    53,    -1,    38,    -1,    24,    -1,    -1,    25,    -1,
      -1,    36,    37,    -1,    37,    -1,    54,    33,    56,    -1,
      39,    -1,    40,    -1,    41,    -1,    43,    -1,    44,    -1,
      45,    -1,    16,     4,    22,     8,    -1,    16,     4,     8,
      -1,    15,     4,    47,     8,    -1,    15,     4,    47,    48,
       8,    -1,    13,     4,    42,     8,    -1,    13,     4,    42,
      49,     8,    -1,    22,    -1,    14,     4,    22,     8,    -1,
      11,     4,    22,     8,    -1,    46,     8,    -1,    46,     3,
      -1,     3,    -1,    22,    26,    22,    -1,    12,    50,    48,
      -1,    12,    50,    -1,    12,    -1,    12,    51,    49,    -1,
      12,    51,    -1,    12,    -1,    22,    10,    52,    -1,    22,
      10,    52,    -1,    22,    -1,    21,    -1,    27,    21,    27,
      -1,     7,    -1,    18,     8,    -1,    19,    -1,    23,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   112,   112,   114,   119,   118,   131,   139,   141,   165,
     169,   184,   188,   191,   195,   197,   201,   216,   218,   228,
     230,   232,   234,   248,   255,   274,   282,   292,   298,   306,
     329,   336,   343,   347,   349,   353,   362,   364,   366,   380,
     382,   384,   398,   429,   443,   449,   464,   472,   479,   498,
     517
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "ANY", "COLON", "DASH", "DQUOTE",
  "ENDOFHEADERS", "EOL", "EOM", "EQUAL", "MIMEVERSION_HEADER", "SEMICOLON",
  "CONTENTDISPOSITION_HEADER", "CONTENTENCODING_HEADER",
  "CONTENTTYPE_HEADER", "MAIL_HEADER", "HEADERVALUE", "BOUNDARY",
  "ENDBOUNDARY", "CONTENTTYPE_VALUE", "TSPECIAL", "WORD", "BODY",
  "PREAMBLE", "POSTAMBLE", "'/'", "'\"'", "$accept", "message",
  "multipart_message", "@1", "singlepart_message", "headers", "preamble",
  "postamble", "mimeparts", "mimepart", "header", "mail_header",
  "contenttype_header", "contentdisposition_header", "content_disposition",
  "contentencoding_header", "mimeversion_header", "invalid_header", "any",
  "mimetype", "contenttype_parameters", "content_disposition_parameters",
  "contenttype_parameter", "content_disposition_parameter",
  "contenttype_parameter_value", "end_headers", "boundary", "endboundary",
  "body", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279,   280,    47,    34
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    28,    29,    29,    31,    30,    32,    33,    33,    33,
      34,    34,    35,    35,    36,    36,    37,    38,    38,    38,
      38,    38,    38,    39,    39,    40,    40,    41,    41,    42,
      43,    44,    45,    46,    46,    47,    48,    48,    48,    49,
      49,    49,    50,    51,    52,    52,    52,    53,    54,    55,
      56
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     1,     0,     6,     2,     2,     1,     1,
       1,     0,     1,     0,     2,     1,     3,     1,     1,     1,
       1,     1,     1,     4,     3,     4,     5,     4,     5,     1,
       4,     4,     2,     2,     1,     3,     3,     2,     1,     3,
       2,     1,     3,     3,     1,     1,     3,     1,     2,     1,
       1
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       0,    34,    47,     0,     0,     0,     0,     0,     0,     2,
       3,    11,     9,    17,    18,    19,    20,    21,    22,     0,
       8,     0,     0,     0,     0,     0,     1,    50,    10,     4,
       6,     7,    33,    32,     0,    29,     0,     0,     0,     0,
      24,     0,     0,    31,    27,    41,     0,    30,     0,    25,
      38,     0,    23,     0,     0,    15,     0,     0,    40,    28,
      35,     0,    37,    26,    48,    49,    14,    13,     0,     0,
      39,     0,    36,    12,     5,    16,    45,    44,     0,    43,
      42,     0,    46
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,     8,     9,    42,    10,    11,    29,    74,    54,    55,
      12,    13,    14,    15,    36,    16,    17,    18,    19,    39,
      51,    46,    62,    58,    79,    20,    56,    67,    30
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -20
static const yytype_int8 yypact[] =
{
       3,   -20,   -20,    17,    21,    22,    23,    24,     5,   -20,
     -20,   -11,     3,   -20,   -20,   -20,   -20,   -20,   -20,     1,
     -20,     7,     8,     9,    10,    -7,   -20,   -20,   -20,   -20,
     -20,   -20,   -20,   -20,    25,   -20,    -1,    26,    11,    12,
     -20,    27,    18,   -20,   -20,    16,    31,   -20,    19,   -20,
      20,    32,   -20,    35,     4,   -20,     3,    36,    33,   -20,
     -20,    37,    38,   -20,   -20,   -20,   -20,    28,    29,   -19,
     -20,   -19,   -20,   -20,   -20,   -20,   -20,   -20,    30,   -20,
     -20,    34,   -20
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -20,   -20,   -20,   -20,   -20,   -12,   -20,   -20,   -20,    -6,
     -20,   -20,   -20,   -20,   -20,   -20,   -20,   -20,   -20,   -20,
     -13,    -4,   -20,   -20,   -16,   -20,   -20,   -20,   -10
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] =
{
      31,    40,    76,    77,    32,    26,     1,    44,    78,    33,
       2,    45,    27,    28,     3,    41,     4,     5,     6,     7,
      49,    21,    53,    65,    50,    22,    23,    24,    25,    34,
      35,    37,    38,    43,    47,    52,    53,    48,    57,    59,
      63,    60,    61,    64,    68,    45,    69,    71,    66,    72,
      50,    81,    27,    73,    70,    80,     0,     0,    75,     0,
       0,    82
};

static const yytype_int8 yycheck[] =
{
      12,     8,    21,    22,     3,     0,     3,     8,    27,     8,
       7,    12,    23,    24,    11,    22,    13,    14,    15,    16,
       8,     4,    18,    19,    12,     4,     4,     4,     4,    22,
      22,    22,    22,     8,     8,     8,    18,    26,    22,     8,
       8,    22,    22,     8,    56,    12,    10,    10,    54,    62,
      12,    21,    23,    25,    58,    71,    -1,    -1,    68,    -1,
      -1,    27
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,     3,     7,    11,    13,    14,    15,    16,    29,    30,
      32,    33,    38,    39,    40,    41,    43,    44,    45,    46,
      53,     4,     4,     4,     4,     4,     0,    23,    24,    34,
      56,    33,     3,     8,    22,    22,    42,    22,    22,    47,
       8,    22,    31,     8,     8,    12,    49,     8,    26,     8,
      12,    48,     8,    18,    36,    37,    54,    22,    51,     8,
      22,    22,    50,     8,     8,    19,    37,    55,    33,    10,
      49,    10,    48,    25,    35,    56,    21,    22,    27,    52,
      52,    21,    27
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
      yyerror (pstate, yyscanner, YY_("syntax error: cannot back up")); \
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
# if defined(YYLTYPE_IS_TRIVIAL)
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
# define YYLEX yylex (&yylval, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval, yyscanner)
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
		  Type, Value, pstate, yyscanner); \
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
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, struct parser_state *pstate, void *yyscanner)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep, pstate, yyscanner)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    struct parser_state *pstate;
    void *yyscanner;
#endif
{
  if (!yyvaluep)
    return;
  YYUSE (pstate);
  YYUSE (yyscanner);
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
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, struct parser_state *pstate, void *yyscanner)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep, pstate, yyscanner)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    struct parser_state *pstate;
    void *yyscanner;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  yy_symbol_value_print (yyoutput, yytype, yyvaluep, pstate, yyscanner);
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
yy_reduce_print (YYSTYPE *yyvsp, int yyrule, struct parser_state *pstate, void *yyscanner)
#else
static void
yy_reduce_print (yyvsp, yyrule, pstate, yyscanner)
    YYSTYPE *yyvsp;
    int yyrule;
    struct parser_state *pstate;
    void *yyscanner;
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
		       		       , pstate, yyscanner);
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, Rule, pstate, yyscanner); \
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
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, struct parser_state *pstate, void *yyscanner)
#else
static void
yydestruct (yymsg, yytype, yyvaluep, pstate, yyscanner)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
    struct parser_state *pstate;
    void *yyscanner;
#endif
{
  YYUSE (yyvaluep);
  YYUSE (pstate);
  YYUSE (yyscanner);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {

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
int yyparse (struct parser_state *pstate, void *yyscanner);
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
yyparse (struct parser_state *pstate, void *yyscanner)
#else
int
yyparse (pstate, yyscanner)
    struct parser_state *pstate;
    void *yyscanner;
#endif
#endif
{
  /* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;

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



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;


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


	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),

		    &yystacksize);

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

#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;


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


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 4:
#line 119 "mimeparser.y"
    { 
		mm_context_attachpart(pstate->ctx, pstate->current_mimepart);
		pstate->current_mimepart = mm_mimepart_new();
		pstate->have_contenttype = 0;
	;}
    break;

  case 5:
#line 125 "mimeparser.y"
    {
		dprintf2(pstate,"This was a multipart message\n");
	;}
    break;

  case 6:
#line 132 "mimeparser.y"
    {
		dprintf2(pstate,"This was a single part message\n");
		mm_context_attachpart(pstate->ctx, pstate->current_mimepart);
	;}
    break;

  case 8:
#line 142 "mimeparser.y"
    {
		/* If we did not find a Content-Type header for the current
		 * MIME part (or envelope), we create one and attach it.
		 * According to the RFC, a type of "text/plain" and a
		 * charset of "us-ascii" can be assumed.
		 */
		struct mm_content *ct;
		struct mm_param *param;

		if (!pstate->have_contenttype) {
			ct = mm_content_new();
			mm_content_settype(ct, "text/plain");
			
			param = mm_param_new();
			param->name = xstrdup("charset");
			param->value = xstrdup("us-ascii");

			mm_content_attachtypeparam(ct, param);
			mm_mimepart_attachcontenttype(pstate->current_mimepart, ct);
		}	
		pstate->have_contenttype = 0;
	;}
    break;

  case 10:
#line 170 "mimeparser.y"
    {
		char *preamble;
		size_t offset;
		
		if ((yyvsp[(1) - (1)].position).start != (yyvsp[(1) - (1)].position).end) {
			preamble = PARSE_readmessagepart(0, (yyvsp[(1) - (1)].position).start, (yyvsp[(1) - (1)].position).end,
			    &offset,yyscanner,pstate);
			if (preamble == NULL) {
				return(-1);
			}
			pstate->ctx->preamble = preamble;
			dprintf2(pstate,"PREAMBLE:\n%s\n", preamble);
		}
	;}
    break;

  case 12:
#line 189 "mimeparser.y"
    {
	;}
    break;

  case 16:
#line 202 "mimeparser.y"
    {

		if (mm_context_attachpart(pstate->ctx, pstate->current_mimepart) == -1) {
			mm_errno = MM_ERROR_ERRNO;
			return(-1);
		}	

		pstate->temppart = mm_mimepart_new();
		pstate->current_mimepart = pstate->temppart;
		pstate->mime_parts++;
	;}
    break;

  case 18:
#line 219 "mimeparser.y"
    {
		pstate->have_contenttype = 1;
		if (mm_content_iscomposite(pstate->envelope->type)) {
			pstate->ctx->messagetype = MM_MSGTYPE_MULTIPART;
		} else {
			pstate->ctx->messagetype = MM_MSGTYPE_FLAT;
		}	
	;}
    break;

  case 22:
#line 235 "mimeparser.y"
    {
		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("invalid header encountered");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}
	;}
    break;

  case 23:
#line 249 "mimeparser.y"
    {
		struct mm_mimeheader *hdr;
		hdr = mm_mimeheader_generate((yyvsp[(1) - (4)].string), (yyvsp[(3) - (4)].string));
		mm_mimepart_attachheader(pstate->current_mimepart, hdr);
	;}
    break;

  case 24:
#line 256 "mimeparser.y"
    {
		struct mm_mimeheader *hdr;

		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_MIME;
			mm_error_setmsg("invalid header encountered");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}	
		
		hdr = mm_mimeheader_generate((yyvsp[(1) - (3)].string), xstrdup(""));
		mm_mimepart_attachheader(pstate->current_mimepart, hdr);
	;}
    break;

  case 25:
#line 275 "mimeparser.y"
    {
		mm_content_settype(pstate->ctype, "%s", (yyvsp[(3) - (4)].string));
		mm_mimepart_attachcontenttype(pstate->current_mimepart, pstate->ctype);
		dprintf2(pstate,"Content-Type -> %s\n", (yyvsp[(3) - (4)].string));
		pstate->ctype = mm_content_new();
	;}
    break;

  case 26:
#line 283 "mimeparser.y"
    {
		mm_content_settype(pstate->ctype, "%s", (yyvsp[(3) - (5)].string));
		mm_mimepart_attachcontenttype(pstate->current_mimepart, pstate->ctype);
		dprintf2(pstate,"Content-Type (P) -> %s\n", (yyvsp[(3) - (5)].string));
		pstate->ctype = mm_content_new();
	;}
    break;

  case 27:
#line 293 "mimeparser.y"
    {
		dprintf2(pstate,"Content-Disposition -> %s\n", (yyvsp[(3) - (4)].string));
		pstate->ctype->disposition_type = xstrdup((yyvsp[(3) - (4)].string));
	;}
    break;

  case 28:
#line 299 "mimeparser.y"
    {
		dprintf2(pstate,"Content-Disposition (P) -> %s; params\n", (yyvsp[(3) - (5)].string));
		pstate->ctype->disposition_type = xstrdup((yyvsp[(3) - (5)].string));
	;}
    break;

  case 29:
#line 307 "mimeparser.y"
    {
		/*
		 * According to RFC 2183, the content disposition value may
		 * only be "inline", "attachment" or an extension token. We
		 * catch invalid values here if we are not in loose parsing
		 * mode.
		 */
		if (strcasecmp((yyvsp[(1) - (1)].string), "inline") && strcasecmp((yyvsp[(1) - (1)].string), "attachment")
		    && strncasecmp((yyvsp[(1) - (1)].string), "X-", 2)) {
			if (pstate->parsemode != MM_PARSE_LOOSE) {
				mm_errno = MM_ERROR_MIME;
				mm_error_setmsg("invalid content-disposition");
				return(-1);
			}	
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}	
		(yyval.string) = (yyvsp[(1) - (1)].string);
	;}
    break;

  case 30:
#line 330 "mimeparser.y"
    {
		dprintf2(pstate,"Content-Transfer-Encoding -> %s\n", (yyvsp[(3) - (4)].string));
	;}
    break;

  case 31:
#line 337 "mimeparser.y"
    {
		dprintf2(pstate,"MIME-Version -> '%s'\n", (yyvsp[(3) - (4)].string));
	;}
    break;

  case 35:
#line 354 "mimeparser.y"
    {
		char type[255];
		snprintf(type, sizeof(type), "%s/%s", (yyvsp[(1) - (3)].string), (yyvsp[(3) - (3)].string));
		(yyval.string) = type;
	;}
    break;

  case 38:
#line 367 "mimeparser.y"
    {
		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_MIME;
			mm_error_setmsg("invalid Content-Type header");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}	
	;}
    break;

  case 41:
#line 385 "mimeparser.y"
    {	
		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_MIME;
			mm_error_setmsg("invalid Content-Disposition header");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVHDR */
		}
	;}
    break;

  case 42:
#line 399 "mimeparser.y"
    {
		struct mm_param *param;
		param = mm_param_new();
		
		dprintf2(pstate,"Param: '%s', Value: '%s'\n", (yyvsp[(1) - (3)].string), (yyvsp[(3) - (3)].string));
		
		/* Catch an eventual boundary identifier */
		if (!strcasecmp((yyvsp[(1) - (3)].string), "boundary")) {
			if (pstate->lstate.boundary_string == NULL) {
				set_boundary((yyvsp[(3) - (3)].string),pstate);
			} else {
				if (pstate->parsemode != MM_PARSE_LOOSE) {
					mm_errno = MM_ERROR_MIME;
					mm_error_setmsg("duplicate boundary "
					    "found");
					return -1;
				} else {
					/* TODO: attach MM_WARNING_DUPPARAM */
				}
			}
		}

		param->name = xstrdup((yyvsp[(1) - (3)].string));
		param->value = xstrdup((yyvsp[(3) - (3)].string));

		mm_content_attachtypeparam(pstate->ctype, param);
	;}
    break;

  case 43:
#line 430 "mimeparser.y"
    {
		struct mm_param *param;
		param = mm_param_new();
		
		param->name = xstrdup((yyvsp[(1) - (3)].string));
		param->value = xstrdup((yyvsp[(3) - (3)].string));

		mm_content_attachdispositionparam(pstate->ctype, param);

	;}
    break;

  case 44:
#line 444 "mimeparser.y"
    {
		dprintf2(pstate,"contenttype_param_val: WORD=%s\n", (yyvsp[(1) - (1)].string));
		(yyval.string) = (yyvsp[(1) - (1)].string);
	;}
    break;

  case 45:
#line 450 "mimeparser.y"
    {
		dprintf2(pstate,"contenttype_param_val: TSPECIAL\n");
		/* For broken MIME implementation */
		if (pstate->parsemode != MM_PARSE_LOOSE) {
			mm_errno = MM_ERROR_MIME;
			mm_error_setmsg("tspecial without quotes");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		} else {
			/* TODO: attach MM_WARNING_INVAL */
		}	
		(yyval.string) = (yyvsp[(1) - (1)].string);
	;}
    break;

  case 46:
#line 465 "mimeparser.y"
    {
		dprintf2(pstate,"contenttype_param_val: \"TSPECIAL\"\n" );
		(yyval.string) = (yyvsp[(2) - (3)].string);
	;}
    break;

  case 47:
#line 473 "mimeparser.y"
    {
		dprintf2(pstate,"End of headers at line %d\n", pstate->lstate.lineno);
	;}
    break;

  case 48:
#line 480 "mimeparser.y"
    {
		if (pstate->lstate.boundary_string == NULL) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("internal incosistency");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		}
		if (strcmp(pstate->lstate.boundary_string, (yyvsp[(1) - (2)].string))) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("invalid boundary: '%s' (%d)", (yyvsp[(1) - (2)].string), strlen((yyvsp[(1) - (2)].string)));
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		}
		dprintf2(pstate,"New MIME part... (%s)\n", (yyvsp[(1) - (2)].string));
	;}
    break;

  case 49:
#line 499 "mimeparser.y"
    {
		if (pstate->lstate.endboundary_string == NULL) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("internal incosistency");
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		}
		if (strcmp(pstate->lstate.endboundary_string, (yyvsp[(1) - (1)].string))) {
			mm_errno = MM_ERROR_PARSE;
			mm_error_setmsg("invalid end boundary: %s", (yyvsp[(1) - (1)].string));
			mm_error_setlineno(pstate->lstate.lineno);
			return(-1);
		}
		dprintf2(pstate,"End of MIME message\n");
	;}
    break;

  case 50:
#line 518 "mimeparser.y"
    {
		char *body;
		size_t offset;

		dprintf2(pstate,"BODY (%d/%d), SIZE %d\n", (yyvsp[(1) - (1)].position).start, (yyvsp[(1) - (1)].position).end, (yyvsp[(1) - (1)].position).end - (yyvsp[(1) - (1)].position).start);

		body = PARSE_readmessagepart((yyvsp[(1) - (1)].position).opaque_start, (yyvsp[(1) - (1)].position).start, (yyvsp[(1) - (1)].position).end,
		    &offset,yyscanner,pstate);

		if (body == NULL) {
			return(-1);
		}
		pstate->current_mimepart->opaque_body = body;
		pstate->current_mimepart->body = body + offset;
		pstate->current_mimepart->opaque_length = (yyvsp[(1) - (1)].position).end - (yyvsp[(1) - (1)].position).start - 2 + offset;
		pstate->current_mimepart->length = pstate->current_mimepart->opaque_length - offset;
	;}
    break;


/* Line 1267 of yacc.c.  */
#line 1913 "mimeparser.tab.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;


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
      yyerror (pstate, yyscanner, YY_("syntax error"));
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
	    yyerror (pstate, yyscanner, yymsg);
	  }
	else
	  {
	    yyerror (pstate, yyscanner, YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }



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
		      yytoken, &yylval, pstate, yyscanner);
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


      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp, pstate, yyscanner);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;


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
  yyerror (pstate, yyscanner, YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval, pstate, yyscanner);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp, pstate, yyscanner);
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


#line 537 "mimeparser.y"


/*
 * This function gets the specified part from the currently parsed message.
 */
static char *
PARSE_readmessagepart(size_t opaque_start, size_t real_start, size_t end, 
    size_t *offset, yyscan_t yyscanner, struct parser_state *pstate)
{
	size_t body_size;
	size_t current;
	size_t start;
	char *body;

	/* calculate start and offset markers for the opaque and
	 * header stripped body message.
	 */
	if (opaque_start > 0) {
		/* Multipart message */
		if (real_start) {
			if (real_start < opaque_start) {
				mm_errno = MM_ERROR_PARSE;
				mm_error_setmsg("internal incosistency (S:%d/O:%d)",
				    real_start,
				    opaque_start);
				return(NULL);
			}
			start = opaque_start;
			*offset = real_start - start;
		/* Flat message */	
		} else {	
			start = opaque_start;
			*offset = 0;
		}	
	} else {
		start = real_start;
		*offset = 0;
	}

	/* The next three cases should NOT happen anytime */
	if (end <= start) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("internal incosistency,2");
		mm_error_setlineno(pstate->lstate.lineno);
		return(NULL);
	}
	if (start < *offset) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("internal incosistency, S:%d,O:%d,L:%d", start, offset, pstate->lstate.lineno);
		mm_error_setlineno(pstate->lstate.lineno);
		return(NULL);
	}	
	if (start < 0 || end < 0) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("internal incosistency,4");
		mm_error_setlineno(pstate->lstate.lineno);
		return(NULL);
	}	

	/* XXX: do we want to enforce a maximum body size? make it a
	 * parser option? */

	/* Read in the body message */
	body_size = end - start;

	if (body_size < 1) {
		mm_errno = MM_ERROR_PARSE;
		mm_error_setmsg("size of body cannot be < 1");
		mm_error_setlineno(pstate->lstate.lineno);
		return(NULL);
	}	
	
	body = (char *)malloc(body_size + 1);
	if (body == NULL) {
		mm_errno = MM_ERROR_ERRNO;
		return(NULL);
	}	
		
	/* Get the message body either from a stream or a memory
	 * buffer.
	 */
	if (mimeparser_yyget_in(yyscanner) != NULL) {
		FILE *x = mimeparser_yyget_in(yyscanner);
		current = ftell(x);
		fseek(x, start - 1, SEEK_SET);
		fread(body, body_size - 1, 1, x);
		fseek(x, current, SEEK_SET);
	} else if (pstate->lstate.message_buffer != NULL) {
		strlcpy(body, pstate->lstate.message_buffer + start - 1, body_size);
	} 
	
	return(body);

}

int
yyerror(struct parser_state *pstate, void *yyscanner, const char *str)
{
	mm_errno = MM_ERROR_PARSE;
	mm_error_setmsg("%s", str);
	mm_error_setlineno(pstate->lstate.lineno);
	return -1;
}

int 
mimeparser_yywrap(void)
{
	return 1;
}

/**
 * Sets the boundary value for the current message
 */
int 
set_boundary(char *str, struct parser_state *pstate)
{
	size_t blen;

	blen = strlen(str);

	pstate->lstate.boundary_string = (char *)malloc(blen + 3);
	pstate->lstate.endboundary_string = (char *)malloc(blen + 5);

	if (pstate->lstate.boundary_string == NULL || pstate->lstate.endboundary_string == NULL) {
		if (pstate->lstate.boundary_string != NULL) {
			free(pstate->lstate.boundary_string);
		}
		if (pstate->lstate.endboundary_string != NULL) {
			free(pstate->lstate.endboundary_string);
		}	
		return -1;
	}
	
	pstate->ctx->boundary = xstrdup(str);

	snprintf(pstate->lstate.boundary_string, blen + 3, "--%s", str);
	snprintf(pstate->lstate.endboundary_string, blen + 5, "--%s--", str);

	return 0;
}

/**
 * Debug printf()
 */
int
dprintf2(struct parser_state *pstate, const char *fmt, ...)
{
	va_list ap;
	char *msg;
	if (pstate->debug == 0) return 1;

	va_start(ap, fmt);
	vasprintf(&msg, fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s", msg);
	free(msg);

	return 0;
	
}

void reset_environ(struct parser_state *pstate)
{
	pstate->lstate.lineno = 0;
	pstate->lstate.boundary_string = NULL;
	pstate->lstate.endboundary_string = NULL;
	pstate->lstate.message_buffer = NULL;
	pstate->mime_parts = 0;
	pstate->debug = 0;
	pstate->envelope = NULL;
	pstate->temppart = NULL;
	pstate->ctype = NULL;
	pstate->current_mimepart = NULL;

	pstate->have_contenttype = 0;
}
/**
 * Initializes the parser engine.
 */
int
PARSER_initialize(struct parser_state *pstate, void *yyscanner)
{
	void reset_lexer_state(void *yyscanner, struct parser_state *);
#if 0
	if (pstate->ctx != NULL) {
		xfree(pstate->ctx);
		pstate->ctx = NULL;
	}
	if (pstate->envelope != NULL) {
		xfree(pstate->envelope);
		pstate->envelope = NULL;
	}	
	if (pstate->ctype != NULL) {
		xfree(pstate->ctype);
		pstate->ctype = NULL;
	}	
#endif
	/* yydebug = 1; */
	reset_environ(pstate);
	reset_lexer_state(yyscanner,pstate);

	pstate->envelope = mm_mimepart_new();
	pstate->current_mimepart = pstate->envelope;
	pstate->ctype = mm_content_new();

	pstate->have_contenttype = 0;

	return 1;
}



