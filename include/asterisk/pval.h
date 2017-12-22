#ifndef _ASTERISK_PVAL_H
#define _ASTERISK_PVAL_H

/* whatever includes this, better include asterisk/lock.h and asterisk/hashtab.h */

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
	PV_LOCALVARDEC,       /* 28 */
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

#ifndef AAL_ARGCHECK
/* for the time being, short circuit all the AAL related structures
   without permanently removing the code; after/during the AAL
   development, this code can be properly re-instated
*/

/* null definitions for structs passed down the infrastructure */
struct argapp
{
	struct argapp *next;
};

#endif

struct ast_context;

#ifdef AAL_ARGCHECK
int option_matches_j( struct argdesc *should, pval *is, struct argapp *app);
int option_matches( struct argdesc *should, pval *is, struct argapp *app);
int ael_is_funcname(char *name);
#endif

int do_pbx_load_module(void);
int count_labels_in_current_context(char *label);
int check_app_args(pval *appcall, pval *arglist, struct argapp *app);
void check_pval(pval *item, struct argapp *apps, int in_globals);
void check_pval_item(pval *item, struct argapp *apps, int in_globals);
void check_switch_expr(pval *item, struct argapp *apps);
void ast_expr_register_extra_error_info(char *errmsg);
void ast_expr_clear_extra_error_info(void);
int  ast_expr(char *expr, char *buf, int length, struct ast_channel *chan);
struct pval *find_macro(char *name);
struct pval *find_context(char *name);
struct pval *find_context(char *name);
struct pval *find_macro(char *name);
struct ael_priority *new_prio(void);
struct ael_extension *new_exten(void);
void linkprio(struct ael_extension *exten, struct ael_priority *prio, struct ael_extension *mother_exten);
void destroy_extensions(struct ael_extension *exten);
/* static void linkexten(struct ael_extension *exten, struct ael_extension *add);
   static void gen_prios(struct ael_extension *exten, char *label, pval *statement, struct ael_extension *mother_exten, struct ast_context *context ); */
void set_priorities(struct ael_extension *exten);
void add_extensions(struct ael_extension *exten);
int ast_compile_ael2(struct ast_context **local_contexts, struct ast_hashtab *local_table, struct pval *root);
void destroy_pval(pval *item);
void destroy_pval_item(pval *item);
int is_float(char *arg );
int is_int(char *arg );
int is_empty(char *arg);

/* PVAL PI */


pval *pvalCreateNode( pvaltype type );
pvaltype pvalObjectGetType( pval *p );

void pvalWordSetString( pval *p, char *str);
char *pvalWordGetString( pval *p );

void pvalMacroSetName( pval *p, char *name);
char *pvalMacroGetName( pval *p );
void pvalMacroSetArglist( pval *p, pval *arglist );
void pvalMacroAddArg( pval *p, pval *arg );
pval *pvalMacroWalkArgs( pval *p, pval **arg );
void pvalMacroAddStatement( pval *p, pval *statement );
pval *pvalMacroWalkStatements( pval *p, pval **next_statement );

void pvalContextSetName( pval *p, char *name);
char *pvalContextGetName( pval *p );
void pvalContextSetAbstract( pval *p );
void pvalContextUnsetAbstract( pval *p );
int  pvalContextGetAbstract( pval *p );
void pvalContextAddStatement( pval *p, pval *statement);
pval *pvalContextWalkStatements( pval *p, pval **statements );

void pvalMacroCallSetMacroName( pval *p, char *name );
char* pvalMacroCallGetMacroName( pval *p );
void pvalMacroCallSetArglist( pval *p, pval *arglist );
void pvalMacroCallAddArg( pval *p, pval *arg );
pval *pvalMacroCallWalkArgs( pval *p, pval **args );

void pvalAppCallSetAppName( pval *p, char *name );
char* pvalAppCallGetAppName( pval *p );
void pvalAppCallSetArglist( pval *p, pval *arglist );
void pvalAppCallAddArg( pval *p, pval *arg );
pval *pvalAppCallWalkArgs( pval *p, pval **args );

void pvalCasePatSetVal( pval *p, char *val );
char* pvalCasePatGetVal( pval *p );
void pvalCasePatDefAddStatement( pval *p, pval *statement );
pval *pvalCasePatDefWalkStatements( pval *p, pval **statement );

void pvalCatchSetExtName( pval *p, char *name );
char* pvalCatchGetExtName( pval *p );
void pvalCatchSetStatement( pval *p, pval *statement );
pval *pvalCatchGetStatement( pval *p );

void pvalSwitchesAddSwitch( pval *p, char *name );
char* pvalSwitchesWalkNames( pval *p, pval **next_item );
void pvalESwitchesAddSwitch( pval *p, char *name );
char* pvalESwitchesWalkNames( pval *p, pval **next_item );

void pvalIncludesAddInclude( pval *p, const char *include );

void pvalIncludesAddIncludeWithTimeConstraints( pval *p, const char *include, char *hour_range, char *dom_range, char *dow_range, char *month_range );
void pvalIncludeGetTimeConstraints( pval *p, char **hour_range, char **dom_range, char **dow_range, char **month_range );
char* pvalIncludesWalk( pval *p, pval **next_item );

void pvalStatementBlockAddStatement( pval *p, pval *statement);
pval *pvalStatementBlockWalkStatements( pval *p, pval **next_statement);

void pvalVarDecSetVarname( pval *p, char *name );
void pvalVarDecSetValue( pval *p, char *value );
char* pvalVarDecGetVarname( pval *p );
char* pvalVarDecGetValue( pval *p );

void pvalGotoSetTarget( pval *p, char *context, char *exten, char *label );
void pvalGotoGetTarget( pval *p, char **context, char **exten, char **label );

void pvalLabelSetName( pval *p, char *name );
char* pvalLabelGetName( pval *p );

void pvalForSetInit( pval *p, char *init );
void pvalForSetTest( pval *p, char *test );
void pvalForSetInc( pval *p, char *inc );
void pvalForSetStatement( pval *p, pval *statement );
char* pvalForGetInit( pval *p );
char* pvalForGetTest( pval *p );
char* pvalForGetInc( pval *p );
pval* pvalForGetStatement( pval *p );


void pvalIfSetCondition( pval *p, char *expr );
char* pvalIfGetCondition( pval *p );
void pvalIfTimeSetCondition( pval *p, char *hour_range, char *dow_range, char *dom_range, char *mon_range );  /* time range format: 24-hour format begin-end|dow range|dom range|month range */
void pvalIfTimeGetCondition( pval *p, char **hour_range, char **dow_range, char **dom_range, char **month_range );
void pvalRandomSetCondition( pval *p, char *percent );
char* pvalRandomGetCondition( pval *p );
void pvalConditionalSetThenStatement( pval *p, pval *statement );
void pvalConditionalSetElseStatement( pval *p, pval *statement );
pval* pvalConditionalGetThenStatement( pval *p );
pval* pvalConditionalGetElseStatement( pval *p );

void pvalSwitchSetTestexpr( pval *p, char *expr );
char* pvalSwitchGetTestexpr( pval *p );
void pvalSwitchAddCase( pval *p, pval *Case );
pval* pvalSwitchWalkCases( pval *p, pval **next_case );

void pvalExtenSetName( pval *p, char *name );
char *pvalExtenGetName( pval *p );
void pvalExtenSetRegexten( pval *p );
void pvalExtenUnSetRegexten( pval *p );
int pvalExtenGetRegexten( pval *p );
void pvalExtenSetHints( pval *p, char *hints );
char* pvalExtenGetHints( pval *p );
void pvalExtenSetStatement( pval *p, pval *statement );
pval* pvalExtenGetStatement( pval *p );

void pvalIgnorePatSetPattern( pval *p, char *pat );
char* pvalIgnorePatGetPattern( pval *p );

void pvalGlobalsAddStatement( pval *p, pval *statement );
pval* pvalGlobalsWalkStatements( pval *p, pval **next_statement );

void pvalTopLevAddObject( pval *p, pval *contextOrObj );
pval* pvalTopLevWalkObjects( pval *p, pval **next_obj );

int  pvalCheckType( pval *p, char *funcname, pvaltype type );


#endif
