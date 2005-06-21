#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

int global_lineno = 1;
int global_expr_count = 0;
int global_expr_max_size = 0;
int global_expr_tot_size = 0;
int global_warn_count = 0;
int global_OK_count = 0;

struct varz
{
	char varname[100]; /* a really ultra-simple, space-wasting linked list of var=val data */
	char varval[1000]; /* if any varname is bigger than 100 chars, or val greater than 1000, then **CRASH** */
	struct varz *next;
};

struct varz *global_varlist;

/* Our own version of ast_log, since the expr parser uses it. */

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...) __attribute__ ((format (printf,5,6)));

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

void set_var(const char *varname, const char *varval)
{
	struct varz *t = calloc(1,sizeof(struct varz));
	strcpy(t->varname, varname);
	strcpy(t->varval, varval);
	t->next = global_varlist;
	global_varlist = t;
}

int check_expr(char *buffer, char *error_report)
{
	char *cp;
	int oplen = 0;
	int warn_found = 0;

	error_report[0] = 0;
	
	for (cp=buffer;*cp;cp++) {
		
		if (*cp == '|' 
			|| *cp == '&'
			|| *cp == '='
			|| *cp == '>'
			|| *cp == '<'
			|| *cp == '+'
			|| *cp == '-'
			|| *cp == '*'
			|| *cp == '/'
			|| *cp == '%'
			|| *cp == '?'
			|| *cp == ':'
			/*	|| *cp == '('
				|| *cp == ')' These are pretty hard to track, as they are in funcalls, etc. */
			|| *cp == '"') {
			if (*cp == '"') {
				/* skip to the other end */
				cp++;
				while (*cp && *cp != '"')
					cp++;
				if (*cp == 0) {
					fprintf(stderr,"Trouble? Unterminated double quote found at line %d\n",
							global_lineno);
				}
			}
			else {
				if ((*cp == '>'||*cp == '<' ||*cp=='!') && (*(cp+1) == '=')) {
					oplen = 2;
				}
				else {
					oplen = 1;
				}
				
				if ((cp > buffer && *(cp-1) != ' ') || *(cp+oplen) != ' ') {
					char tbuf[1000];
					if (oplen == 1)
						sprintf(tbuf,"WARNING: line %d,  '%c' operator not separated by spaces. This may lead to confusion. You may wish to use double quotes to quote the grouping it is in. Please check!\n",
								global_lineno, *cp);
					else
						sprintf(tbuf,"WARNING: line %d,  '%c%c' operator not separated by spaces. This may lead to confusion. You may wish to use double quotes to quote the grouping it is in. Please check!\n",
								global_lineno, *cp, *(cp+1));
					strcat(error_report,tbuf);

					global_warn_count++;
					warn_found++;
				}
			}
		}
	}
	return warn_found;
}

int check_eval(char *buffer, char *error_report)
{
	char *cp, *ep, *xp, *s;
	char evalbuf[80000];
	extern char *ast_expr(char *);
	int oplen = 0;
	int warn_found = 0;

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
	s = ast_expr(evalbuf);
	if (s) {
		sprintf(error_report,"line %d, evaluation of $[ %s ] result: %s\n", global_lineno, evalbuf, s);
		return 1;
	}
	else {
		sprintf(error_report,"line %d, evaluation of $[ %s ] result: ****SYNTAX ERROR****\n", global_lineno, evalbuf);
		return 1;
	}
}


void parse_file(const char *fname)
{
	FILE *f = fopen(fname,"r");
	FILE *l = fopen("expr2_log","w");
	int c1;
	char last_char= 0;
	char buffer[30000]; /* I sure hope no expr gets this big! */
	
	if (!f) {
		fprintf(stderr,"Couldn't open %s for reading... need an extensions.conf file to parse!\n");
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


main(int argc,char **argv)
{
	int argc1;
	char *eq;
	
	if (argc < 2) {
		printf("Hey-- give me a path to an extensions.conf file!\n");
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
}
