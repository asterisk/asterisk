/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief Structures for AEL - the Asterisk extension language
 *
 * \ref pbx_ael.c
 * \todo document this file (ael.h)
 */

#ifndef _ASTERISK_AEL_STRUCTS_H
#define _ASTERISK_AEL_STRUCTS_H

/*
 * We include asterisk/paths.h here because it is a convenient place
 * that doesn't require us to rebuild ael files from .fl/.y
 */
#include "asterisk/paths.h"

#include "pval.h"

#if !defined(SOLARIS) && !defined(__CYGWIN__)
/* #include <err.h> */
#else
#define quad_t int64_t
#endif

#if defined(LONG_LONG_MIN) && !defined(QUAD_MIN)
#define QUAD_MIN LONG_LONG_MIN
#endif
#if defined(LONG_LONG_MAX) && !defined(QUAD_MAX)
#define QUAD_MAX LONG_LONG_MAX
#endif

#  if ! defined(QUAD_MIN)
#   define QUAD_MIN     (-0x7fffffffffffffffLL-1)
#  endif
#  if ! defined(QUAD_MAX)
#   define QUAD_MAX     (0x7fffffffffffffffLL)
#  endif


#if 0
#endif
void ael2_semantic_check(pval *item, int *errs, int *warns, int *notes);
pval *npval(pvaltype type, int first_line, int last_line, int first_column, int last_column);
pval *linku1(pval *head, pval *tail);
void ael2_print(char *fname, pval *tree);
struct pval *ael2_parse(char *fname, int *errs);	/* in ael.flex */
void destroy_pval(pval *item);

extern char *prev_word;	/* in ael.flex */

#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void* yyscan_t;
#endif

/* for passing info into and out of yyparse */
struct parse_io
{
	struct pval *pval; /* yyparse will set this to point to the parse tree */
	yyscan_t scanner;       /* yylex needs a scanner. Set it up, and pass it in */
	int syntax_error_count;  /* the count of syntax errors encountered */
};

/* for CODE GENERATION */

typedef enum { AEL_APPCALL, AEL_CONTROL1, AEL_FOR_CONTROL, AEL_IF_CONTROL, AEL_IFTIME_CONTROL, AEL_RAND_CONTROL, AEL_LABEL, AEL_RETURN } ael_priority_type;


struct ael_priority
{
	int priority_num;
	ael_priority_type type;

	char *app;
	char *appargs;

	struct pval *origin;
	struct ael_extension *exten;

	struct ael_priority *goto_true;
	struct ael_priority *goto_false;
	struct ael_priority *next;
};

struct ael_extension
{
	char *name;
	char *cidmatch;
	char *hints;
	int regexten;
	int is_switch;
	int has_switch; /* set if a switch exists in the extension */
	int checked_switch; /* set if we checked for a switch in the extension -- so we don't have to do it again */

	struct ast_context *context;

	struct ael_priority *plist;
	struct ael_priority *plist_last;
	struct ael_extension *next_exten;

	struct ael_priority *loop_break;  /*!< set by latest loop for breaks */
	struct ael_priority *loop_continue; /*!< set by lastest loop for continuing */
	struct ael_priority *return_target;
	int return_needed;
};

#endif /* _ASTERISK_AEL_STRUCTS_H */
