/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

/*** MODULEINFO
	<support_level>extended</support_level>
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"
ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/ast_expr.h"

#define AST_API_MODULE 1
#include "asterisk/inline_api.h"

#define AST_API_MODULE 1
#include "asterisk/lock.h"

#ifndef DEBUG_THREADS
enum ast_lock_type {
	        AST_MUTEX,
	        AST_RDLOCK,
	        AST_WRLOCK,
};
#endif
#ifdef DEBUG_THREADLOCALS
#define MALLOC_FAILURE_MSG \
	ast_log(LOG_ERROR, "Memory Allocation Failure in function %s at line %d of %s\n", func, lineno, file);

void * attribute_malloc _ast_calloc(size_t num, size_t len, const char *file, int lineno, const char *func);

void * attribute_malloc _ast_calloc(size_t num, size_t len, const char *file, int lineno, const char *func)
{
	void *p;

	if (!(p = calloc(num, len)))
		MALLOC_FAILURE_MSG;

	return p;
}
#endif

#ifdef DEBUG_THREADS
#if !defined(LOW_MEMORY)
#ifdef HAVE_BKTR
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
		        int line_num, const char *func, const char *lock_name, void *lock_addr, struct ast_bt *bt);
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
		        int line_num, const char *func, const char *lock_name, void *lock_addr, struct ast_bt *bt)
{
    /* not a lot to do in a standalone w/o threading! */
}

void ast_remove_lock_info(void *lock_addr, struct ast_bt *bt);
void ast_remove_lock_info(void *lock_addr, struct ast_bt *bt)
{
    /* not a lot to do in a standalone w/o threading! */
}

int ast_bt_get_addresses(struct ast_bt *bt);
int ast_bt_get_addresses(struct ast_bt *bt)
{
	/* Suck it, you stupid utils directory! */
	return 0;
}
char **ast_bt_get_symbols(void **addresses, size_t num_frames);
char **ast_bt_get_symbols(void **addresses, size_t num_frames)
{
	char **foo = calloc(num_frames, sizeof(char *) + 1);
	if (foo) {
		int i;
		for (i = 0; i < num_frames; i++) {
			foo[i] = (char *) foo + sizeof(char *) * num_frames;
		}
	}
	return foo;
}
#else
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
		        int line_num, const char *func, const char *lock_name, void *lock_addr);
void ast_store_lock_info(enum ast_lock_type type, const char *filename,
		        int line_num, const char *func, const char *lock_name, void *lock_addr)
{
    /* not a lot to do in a standalone w/o threading! */
}

void ast_remove_lock_info(void *lock_addr);
void ast_remove_lock_info(void *lock_addr)
{
    /* not a lot to do in a standalone w/o threading! */
}
#endif /* HAVE_BKTR */

void ast_suspend_lock_info(void *lock_addr)
{
}
void ast_restore_lock_info(void *lock_addr)
{
}
void ast_mark_lock_acquired(void *);
void ast_mark_lock_acquired(void *foo)
{
    /* not a lot to do in a standalone w/o threading! */
}
#endif
#endif /* DEBUG_THREADS */


static int global_lineno = 1;
static int global_expr_count=0;
static int global_expr_max_size=0;
static int global_expr_tot_size=0;
static int global_warn_count=0;
static int global_OK_count=0;

struct varz
{
	char varname[100]; /* a really ultra-simple, space-wasting linked list of var=val data */
	char varval[1000]; /* if any varname is bigger than 100 chars, or val greater than 1000, then **CRASH** */
	struct varz *next;
};

struct varz *global_varlist;

/* Our own version of ast_log, since the expr parser uses it. */

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...) __attribute__((format(printf,5,6)));

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
	va_list vars;
	va_start(vars,fmt);
	
	printf("LOG: lev:%d file:%s  line:%d func: %s  ",
		   level, file, line, function);
	vprintf(fmt, vars);
	fflush(stdout);
	va_end(vars);
}
//void ast_register_file_version(const char *file, const char *version);
//void ast_unregister_file_version(const char *file);

char *find_var(const char *varname);
void set_var(const char *varname, const char *varval);
unsigned int check_expr(char* buffer, char* error_report);
int check_eval(char *buffer, char *error_report);
void parse_file(const char *fname);

void ast_register_file_version(const char *file, const char *version);  
void ast_register_file_version(const char *file, const char *version) { }
#if !defined(LOW_MEMORY)
int ast_add_profile(const char *x, uint64_t scale) { return 0;} 
#endif
int ast_atomic_fetchadd_int_slow(volatile int *p, int v)
{
        int ret;
        ret = *p;
        *p += v;
        return ret;
}

void ast_unregister_file_version(const char *file);
void ast_unregister_file_version(const char *file)
{
}

char *find_var(const char *varname) /* the list should be pretty short, if there's any list at all */
{
	struct varz *t;
	for (t= global_varlist; t; t = t->next) {
		if (!strcmp(t->varname, varname)) {
			return t->varval;
		}
	}
	return 0;
}

void set_var(const char *varname, const char *varval);

void set_var(const char *varname, const char *varval)
{
	struct varz *t = (struct varz*)calloc(1,sizeof(struct varz));
	if (!t)
		return;
	strcpy(t->varname, varname);
	strcpy(t->varval, varval);
	t->next = global_varlist;
	global_varlist = t;
}

unsigned int check_expr(char* buffer, char* error_report)
{
	char* cp;
	unsigned int warn_found = 0;

	error_report[0] = 0;
	
	for (cp = buffer; *cp; ++cp)
	{
		switch (*cp)
		{
			case '"':
				/* skip to the other end */
				while (*(++cp) && *cp != '"') ;

				if (*cp == 0)
				{
					fprintf(stderr,
						"Trouble? Unterminated double quote found at line %d\n",
						global_lineno);
				}
				break;
				
			case '>':
			case '<':
			case '!':
				if (   (*(cp + 1) == '=')
					&& ( ( (cp > buffer) && (*(cp - 1) != ' ') ) || (*(cp + 2) != ' ') ) )
				{
					char msg[200];
					snprintf(msg,
						sizeof(msg),
						"WARNING: line %d: '%c%c' operator not separated by spaces. This may lead to confusion. You may wish to use double quotes to quote the grouping it is in. Please check!\n",
						global_lineno, *cp, *(cp + 1));
					strcat(error_report, msg);
					++global_warn_count;
					++warn_found;
				}
				break;
				
			case '|':
			case '&':
			case '=':
			case '+':
			case '-':
			case '*':
			case '/':
			case '%':
			case '?':
			case ':':
				if ( ( (cp > buffer) && (*(cp - 1) != ' ') ) || (*(cp + 1) != ' ') )
				{
					char msg[200];
					snprintf(msg,
						sizeof(msg),
						"WARNING: line %d: '%c' operator not separated by spaces. This may lead to confusion. You may wish to use double quotes to quote the grouping it is in. Please check!\n",
						global_lineno, *cp );
					strcat(error_report, msg);
					++global_warn_count;
					++warn_found;
				}
				break;
		}
	}

	return warn_found;
}

int check_eval(char *buffer, char *error_report);

struct ast_custom_function *ast_custom_function_find(const char *name);

struct ast_custom_function *ast_custom_function_find(const char *name)
{
	return 0;
}

int check_eval(char *buffer, char *error_report)
{
	char *cp, *ep;
	char s[4096];
	char evalbuf[80000];
	int result;

	error_report[0] = 0;
	ep = evalbuf;

	for (cp=buffer;*cp;cp++) {
		if (*cp == '$' && *(cp+1) == '{') {
			int brack_lev = 1;
			char *xp= cp+2;
			
			while (*xp) {
				if (*xp == '{')
					brack_lev++;
				else if (*xp == '}')
					brack_lev--;
				
				if (brack_lev == 0)
					break;
				xp++;
			}
			if (*xp == '}') {
				char varname[200];
				char *val;
				
				strncpy(varname,cp+2, xp-cp-2);
				varname[xp-cp-2] = 0;
				cp = xp;
				val = find_var(varname);
				if (val) {
					char *z = val;
					while (*z)
						*ep++ = *z++;
				}
				else {
					*ep++ = '5';  /* why not */
					*ep++ = '5';
					*ep++ = '5';
				}
			}
			else {
				printf("Unterminated variable reference at line %d\n", global_lineno);
				*ep++ = *cp;
			}
		}
		else if (*cp == '\\') {
			/* braindead simple elim of backslash */
			cp++;
			*ep++ = *cp;
		}
		else
			*ep++ = *cp;
	}
	*ep++ = 0;

	/* now, run the test */
	result = ast_expr(evalbuf, s, sizeof(s),NULL);
	if (result) {
		sprintf(error_report,"line %d, evaluation of $[ %s ] result: %s\n", global_lineno, evalbuf, s);
		return 1;
	} else {
		sprintf(error_report,"line %d, evaluation of $[ %s ] result: ****SYNTAX ERROR****\n", global_lineno, evalbuf);
		return 1;
	}
}


void parse_file(const char *fname);

void parse_file(const char *fname)
{
	FILE *f = fopen(fname,"r");
	FILE *l = fopen("expr2_log","w");
	int c1;
	char last_char= 0;
	char buffer[30000]; /* I sure hope no expr gets this big! */
	
	if (!f) {
		fprintf(stderr,"Couldn't open %s for reading... need an extensions.conf file to parse!\n",fname);
		exit(20);
	}
	if (!l) {
		fprintf(stderr,"Couldn't open 'expr2_log' file for writing... please fix and re-run!\n");
		exit(21);
	}
	
	global_lineno = 1;
	
	while ((c1 = fgetc(f)) != EOF) {
		if (c1 == '\n')
			global_lineno++;
		else if (c1 == '[') {
			if (last_char == '$') {
				/* bingo, an expr */
				int bracklev = 1;
				int bufcount = 0;
				int retval;
				char error_report[30000];
				
				while ((c1 = fgetc(f)) != EOF) {
					if (c1 == '[')
						bracklev++;
					else if (c1 == ']')
						bracklev--;
					if (c1 == '\n') {
						fprintf(l, "ERROR-- A newline in an expression? Weird! ...at line %d\n", global_lineno);
						fclose(f);
						fclose(l);
						printf("--- ERROR --- A newline in the middle of an expression at line %d!\n", global_lineno);
					}
					
					if (bracklev == 0)
						break;
					buffer[bufcount++] = c1;
				}
				if (c1 == EOF) {
					fprintf(l, "ERROR-- End of File Reached in the middle of an Expr at line %d\n", global_lineno);
					fclose(f);
					fclose(l);
					printf("--- ERROR --- EOF reached in middle of an expression at line %d!\n", global_lineno);
					exit(22);
				}
				
				buffer[bufcount] = 0;
				/* update stats */
				global_expr_tot_size += bufcount;
				global_expr_count++;
				if (bufcount > global_expr_max_size)
					global_expr_max_size = bufcount;
				
				retval = check_expr(buffer, error_report); /* check_expr should bump the warning counter */
				if (retval != 0) {
					/* print error report */
					printf("Warning(s) at line %d, expression: $[%s]; see expr2_log file for details\n", 
						   global_lineno, buffer);
					fprintf(l, "%s", error_report);
				}
				else {
					printf("OK -- $[%s] at line %d\n", buffer, global_lineno);
					global_OK_count++;
				}
				error_report[0] = 0;
				retval = check_eval(buffer, error_report);
				fprintf(l, "%s", error_report);
			}
		}
		last_char = c1;
	}
	printf("Summary:\n  Expressions detected: %d\n  Expressions OK:  %d\n  Total # Warnings:   %d\n  Longest Expr:   %d chars\n  Ave expr len:  %d chars\n",
		   global_expr_count,
		   global_OK_count,
		   global_warn_count,
		   global_expr_max_size,
		   (global_expr_count) ? global_expr_tot_size/global_expr_count : 0);
	
	fclose(f);
	fclose(l);
}


int main(int argc,char **argv)
{
	int argc1;
	char *eq;
	
	if (argc < 2) {
		printf("check_expr -- a program to look thru extensions.conf files for $[...] expressions,\n");
		printf("              and run them thru the parser, looking for problems\n");
		printf("Hey-- give me a path to an extensions.conf file!\n");
		printf(" You can also follow the file path with a series of variable decls,\n");
		printf("     of the form, varname=value, each separated from the next by spaces.\n");
		printf("     (this might allow you to avoid division by zero messages, check that math\n");
		printf("      is being done correctly, etc.)\n");
		printf(" Note that messages about operators not being surrounded by spaces is merely to alert\n");
		printf("  you to possible problems where you might be expecting those operators as part of a string.\n");
        printf("  (to include operators in a string, wrap with double quotes!)\n");
		
		exit(19);
	}
	global_varlist = 0;
	for (argc1=2;argc1 < argc; argc1++) {
		if ((eq = strchr(argv[argc1],'='))) {
			*eq = 0;
			set_var(argv[argc1],eq+1);
		}
	}

	/* parse command args for x=y and set varz */
	
	parse_file(argv[1]);
	return 0;
}
