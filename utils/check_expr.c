#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

int lineno = 1;
int expr_count = 0;
int expr_max_size = 0;
int expr_tot_size = 0;
int warn_count = 0;
int OK_count = 0;

int check_expr(char *buffer, char *error_report)
{
	char *cp;
	int oplen = 0;
	int warn_found = 0;

	error_report[0] = 0;
	
	for(cp=buffer;*cp;cp++)
	{
		
		if( *cp == '|' 
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
			|| *cp == '"' )
		{
			if( *cp == '"' )
			{
				/* skip to the other end */
				cp++;
				while( *cp && *cp != '"' )
					cp++;
				if( *cp == 0 )
				{
					fprintf(stderr,"Trouble? Unterminated double quote found at line %d\n",
							lineno);
				}
			}
			else
			{
				if( ((*cp == '>'||*cp == '<' ||*cp=='!') && *(cp+1) == '=' ) )
				{
					oplen = 2;
				}
				else
				{
					oplen = 1;
				}
				
				if( (cp > buffer && *(cp-1) != ' ') || *(cp+oplen) != ' ' )
				{
					char tbuf[1000];
					if( oplen == 1 )
						sprintf(tbuf,"WARNING: line %d,  '%c' operator not separated by spaces. This may lead to confusion. You may wish to use double quotes to quote the grouping it is in. Please check!\n",
								lineno, *cp);
					else
						sprintf(tbuf,"WARNING: line %d,  '%c%c' operator not separated by spaces. This may lead to confusion. You may wish to use double quotes to quote the grouping it is in. Please check!\n",
								lineno, *cp, *(cp+1));
					strcat(error_report,tbuf);

					warn_count++;
					warn_found++;
				}
			}
		}
	}
	return warn_found;
}


void parse_file(char *fname)
{
	FILE *f = fopen(fname,"r");
	FILE *l = fopen("expr2_log","w");
	int c1;
	char last_char= 0;
	char buffer[30000]; /* I sure hope no expr gets this big! */
	
	if( !f )
	{
		fprintf(stderr,"Couldn't open %s for reading... need an extensions.conf file to parse!\n");
		exit(20);
	}
	if( !l )
	{
		fprintf(stderr,"Couldn't open 'expr2_log' file for writing... please fix and re-run!\n");
		exit(21);
	}
	
	lineno = 1;
	
	while( (c1 = fgetc(f)) != EOF )
	{
		if( c1 == '\n' )
			lineno++;
		else if( c1 == '[' )
		{
			if( last_char == '$' )
			{
				/* bingo, an expr */
				int bracklev = 1;
				int bufcount = 0;
				int retval;
				char error_report[30000];
				
				while( (c1 = fgetc(f)) != EOF )
				{
					if( c1 == '[' )
						bracklev++;
					else if( c1 == ']' )
						bracklev--;
					if( c1 == '\n' )
					{
						fprintf(l, "ERROR-- A newline in an expression? Weird! ...at line %d\n", lineno);
						fclose(f);
						fclose(l);
						printf("--- ERROR --- A newline in the middle of an expression at line %d!\n", lineno);
					}
					
					if( bracklev == 0 )
						break;
					buffer[bufcount++] = c1;
				}
				if( c1 == EOF )
				{
					fprintf(l, "ERROR-- End of File Reached in the middle of an Expr at line %d\n", lineno);
					fclose(f);
					fclose(l);
					printf("--- ERROR --- EOF reached in middle of an expression at line %d!\n", lineno);
					exit(22);
				}
				
				buffer[bufcount] = 0;
				/* update stats */
				expr_tot_size += bufcount;
				expr_count++;
				if( bufcount > expr_max_size )
					expr_max_size = bufcount;
				
				retval = check_expr(buffer, error_report); /* check_expr should bump the warning counter */
				if( retval != 0 )
				{
					/* print error report */
					printf("Warning(s) at line %d, expression: $[%s]; see expr2_log file for details\n", 
						   lineno, buffer);
					fprintf(l, "%s", error_report);
				}
				else
				{
					printf("OK -- $[%s] at line %d\n", buffer, lineno);
					OK_count++;
				}
			}
		}
		last_char = c1;
	}
	printf("Summary:\n  Expressions detected: %d\n  Expressions OK:  %d\n  Total # Warnings:   %d\n  Longest Expr:   %d chars\n  Ave expr len:  %d chars\n",
		   expr_count,
		   OK_count,
		   warn_count,
		   expr_max_size,
		   (expr_count) ? expr_tot_size/expr_count : 0);
	
	fclose(f);
	fclose(l);
}


main(int argc,char **argv)
{
	if( argc < 2 )
	{
		printf("Hey-- give me a path to an extensions.conf file!\n");
		exit(19);
	}
	
	parse_file(argv[1]);
}
