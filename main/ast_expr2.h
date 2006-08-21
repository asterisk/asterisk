/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton interface for Bison's Yacc-like parsers in C

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

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TOK_COLONCOLON = 258,
     TOK_COND = 259,
     TOK_OR = 260,
     TOK_AND = 261,
     TOK_NE = 262,
     TOK_LE = 263,
     TOK_GE = 264,
     TOK_LT = 265,
     TOK_GT = 266,
     TOK_EQ = 267,
     TOK_MINUS = 268,
     TOK_PLUS = 269,
     TOK_MOD = 270,
     TOK_DIV = 271,
     TOK_MULT = 272,
     TOK_COMPL = 273,
     TOK_EQTILDE = 274,
     TOK_COLON = 275,
     TOK_LP = 276,
     TOK_RP = 277,
     TOKEN = 278
   };
#endif
/* Tokens.  */
#define TOK_COLONCOLON 258
#define TOK_COND 259
#define TOK_OR 260
#define TOK_AND 261
#define TOK_NE 262
#define TOK_LE 263
#define TOK_GE 264
#define TOK_LT 265
#define TOK_GT 266
#define TOK_EQ 267
#define TOK_MINUS 268
#define TOK_PLUS 269
#define TOK_MOD 270
#define TOK_DIV 271
#define TOK_MULT 272
#define TOK_COMPL 273
#define TOK_EQTILDE 274
#define TOK_COLON 275
#define TOK_LP 276
#define TOK_RP 277
#define TOKEN 278




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 147 "ast_expr2.y"
{
	struct val *val;
}
/* Line 1529 of yacc.c.  */
#line 99 "ast_expr2.h"
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


