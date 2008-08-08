#ifndef _ASTERISK_AEL_STRUCTS_H
#define _ASTERISK_AEL_STRUCTS_H

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


typedef enum 
{
	PV_WORD, /* an ident, string, name, label, etc. A user-supplied string. */ /* 0 */
	PV_MACRO,             /* 1 */
	PV_CONTEXT,           /* 2 */
	PV_MACRO_CALL,        /* 3 */
	PV_APPLICATION_CALL,  /* 4 */
	PV_CASE,              /* 5 */
	PV_PATTERN,           /* 6 */
	PV_DEFAULT,           /* 7 */
	PV_CATCH,             /* 8 */
	PV_SWITCHES,          /* 9 */
	PV_ESWITCHES,         /* 10 */
	PV_INCLUDES,          /* 11 */
	PV_STATEMENTBLOCK,    /* 12 */
	PV_VARDEC, /* you know, var=val; */  /* 13 */
	PV_GOTO,              /* 14 */
	PV_LABEL,             /* 15 */
	PV_FOR,               /* 16 */
	PV_WHILE,             /* 17 */
	PV_BREAK,             /* 18 */
	PV_RETURN,            /* 19 */
	PV_CONTINUE,          /* 20 */
	PV_IF,                /* 21 */
	PV_IFTIME,            /* 22 */
	PV_RANDOM,            /* 23 */
	PV_SWITCH,            /* 24 */
	PV_EXTENSION,         /* 25 */
	PV_IGNOREPAT,         /* 26 */
	PV_GLOBALS,           /* 27 */

} pvaltype;

/* why this horrible mess? It's always been a tradeoff-- tons of structs,
   each storing it's specific lists of goodies, or a 'simple' single struct,
   with lots of fields, that catches all uses at once. Either you have a long
   list of struct names and subnames, or you have a long list of field names,
   and where/how they are used. I'm going with a single struct, using unions
   to reduce storage. Some simple generalizations, and a long list of types,
   and a book about what is used with what types.... Sorry!
*/

struct pval
{
	pvaltype type;
	int startline;
	int endline;
	int startcol;
	int endcol;
	char *filename;
	
	union
	{
		char *str; /* wow, used almost everywhere! */
		struct pval *list; /* used in SWITCHES, ESWITCHES, INCLUDES, STATEMENTBLOCK, GOTO */
		struct pval *statements;/*  used in EXTENSION */
		char *for_init;  /* used in FOR */
	} u1;
	struct pval *u1_last; /* to build in-order lists -- looks like we only need one */
	
	union
	{
		struct pval *arglist; /* used in macro_call, application_call, MACRO def, also attached to PWORD, the 4 timevals for includes  */
		struct pval *statements; /* used in case, default, catch, while's statement, CONTEXT elements, GLOBALS */
		char *val;  /* used in VARDEC */
		char *for_test; /* used in FOR */
		int label_in_case; /* a boolean for LABELs */
		struct pval *goto_target;  /* used in GOTO */
	} u2;
	
	union
	{
		char *for_inc; /* used in FOR */
		struct pval *else_statements; /* used in IF */
		struct pval *macro_statements; /* used in MACRO */
		int abstract;  /* used for context 1=abstract; 2=extend; 3=both */
		char *hints; /* used in EXTENSION */
		int goto_target_in_case; /* used in GOTO */
		struct ael_extension *compiled_label;
		struct pval *extend; /* to link extended contexts to the 'original' */
	} u3;
	
	union
	{
		struct pval *for_statements; /* used in PV_FOR */
		int regexten;                /* used in EXTENSION */
	} u4;
	
	struct pval *next; /* the pval at the end of this ptr will ALWAYS be of the same type as this one! 
						  EXCEPT for objects of the different types, that are in the same list, like contexts & macros, etc */
	
	struct pval *dad; /* the 'container' of this struct instance */
	struct pval *prev; /* the opposite of the 'next' pointer */
} ;


typedef struct pval pval;

#if 0
pval *npval(pvaltype type, int first_line, int last_line, int first_column, int last_column);
void linku1(pval *head, pval *tail);
void print_pval_list(FILE *f, pval *item, int depth);
void print_pval(FILE *f, pval *item, int depth);
void ael2_semantic_check(pval *item, int *errs, int *warns, int *notes);
struct pval *find_label_in_current_context(char *exten, char *label);
struct pval *find_label_in_current_extension(char *label);
int count_labels_in_current_context(char *label);
struct pval *find_label_in_current_db(char *context, char *exten, char *label);
void ael2_print(char *fname, pval *tree);
#endif
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

	struct ael_priority *loop_break;  /* set by latest loop for breaks */
	struct ael_priority *loop_continue; /* set by lastest loop for continuing */
	struct ael_priority *return_target;
	int return_needed;
};

#endif /* _ASTERISK_AEL_STRUCTS_H */
