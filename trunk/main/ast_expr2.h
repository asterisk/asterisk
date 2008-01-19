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
     TOK_EQTILDE = 275,
     TOK_COLON = 276,
     TOK_LP = 277,
     TOK_RP = 278,
     TOKEN = 279
   };
#endif
/* Tokens.  */
#define TOK_COMMA 258
#define TOK_COLONCOLON 259
#define TOK_COND 260
#define TOK_OR 261
#define TOK_AND 262
#define TOK_NE 263
#define TOK_LE 264
#define TOK_GE 265
#define TOK_LT 266
#define TOK_GT 267
#define TOK_EQ 268
#define TOK_MINUS 269
#define TOK_PLUS 270
#define TOK_MOD 271
#define TOK_DIV 272
#define TOK_MULT 273
#define TOK_COMPL 274
#define TOK_EQTILDE 275
#define TOK_COLON 276
#define TOK_LP 277
#define TOK_RP 278
#define TOKEN 279




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 341 "ast_expr2.y"
{
	struct val *val;
	struct expr_node *arglist;
}
/* Line 1536 of yacc.c.  */
#line 92 "ast_expr2.h"
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




