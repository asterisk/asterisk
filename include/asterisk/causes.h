#define AST_CAUSE_NOTDEFINED	0
#define AST_CAUSE_NORMAL	1
#define AST_CAUSE_BUSY		2
#define AST_CAUSE_FAILURE	3

/* Translate the pri's cause number to asterisk's */
int hangup_pri2cause(int cause);


