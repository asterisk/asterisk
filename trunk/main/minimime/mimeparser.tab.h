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




#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 67 "mimeparser.y"
{
	int number;
	char *string;
	struct s_position position;
}
/* Line 1489 of yacc.c.  */
#line 105 "mimeparser.tab.h"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif



