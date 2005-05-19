/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * A simple program version comparison tool.
 * 
 * Copyright (C) 2005, Steven Michael Murphy (murf at e-tools dot com).
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

/* vercomp.c
   args: <program> <comparison> <version>

	where:

   program = path to program (bison or flex)
   comparison = ">", "<", "<=", ">=", "="  -- depending on shell, you may have to use backslash escapes
   version = a version compare against, say 1.875, or 2.5.4, or whatever.

*/

#include <stdio.h>
#include <string.h>

char *program_version[5];
char *arg_version[5];

void get_program_version_string(char *command, char *output)
{
	char cbuf[8000];
	char pbuf[8000];
	char zbuf[8000];
	char *res;
	FILE *p1;

	zbuf[0] = 0;
	
	sprintf( cbuf, "%s --version", command );
	p1 = popen(cbuf, "r");
	if( !p1 )
	{
		fprintf(stderr,"vercomp: Could not execute the command: %s\n", command);
		exit(125);
	}
	/* the first line is the magic one */
	res = fgets(zbuf, 8000, p1);
	/* clear the trailing blank */
	if( zbuf[strlen(zbuf)-1] == '\n' )
		zbuf[strlen(zbuf)-1] = 0;
	/* the rest is cruft, just empty the input stream */
	while( res )
	{
		res = fgets(pbuf, 8000, p1);
	}
	/* close the stream. Hopefully, we have what we need */
	pclose(p1);
	/* all we want is the last "word"-- so find the last blank, and grab everything after that */

	res = strrchr(zbuf,' ');
	if( !res )
	{
		fprintf(stderr,"Something is wrong with the version string: %s\n", zbuf);
		exit(124);
	}
	strcpy(output,res+1);
}


void extract_version(char *ver_string, char **where)
{
	int i=0;
	char *p=ver_string;
	
	while( p && *p )
	{
		where[i++] = p;
		p = strchr(p,'.');
		if( p )
		{
			*p= 0;
			p++;
		}
	}
}

void compare_versions(char *compare_func)
{
	int i;
	
	for(i=0;i<5;i++)
	{
		/* start out at the beginning, then go to the end */
		if( program_version[i] && arg_version[i] && *program_version[i] && *arg_version[i] )
		{
			
			if( strlen(program_version[i]) == strspn(program_version[i],"0123456789")
				&& strlen(arg_version[i]) == strspn(arg_version[i],"0123456789") )
			{
				/* just pure numbers -- do a numeric compare */
				int pv = atoi(program_version[i]);
				int av = atoi(arg_version[i]);
				
				if( pv < av )
				{
					if( !strcmp(compare_func,"=") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, ">") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, "<") )
					{
						printf("true\n");
						exit(0);
					}
					else if( !strcmp(compare_func, ">=") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, "<=") )
					{
						printf("true\n");
						exit(0);
					}
				}
				else if( pv > av )
				{
					if( !strcmp(compare_func,"=") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, ">") )
					{
						printf("true\n");
						exit(0);
					}
					else if( !strcmp(compare_func, "<") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, ">=") )
					{
						printf("true\n");
						exit(0);
					}
					else if( !strcmp(compare_func, "<=") )
					{
						printf("false\n");
						exit(0);
					}
				}
			}
			else
			{
				/* other junk thrown in -- do string compare */
				int res = strcmp(program_version[i], arg_version[i]);
				if( res < 0 ) /* prog is less than arg */
				{
					if( !strcmp(compare_func,"=") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, ">") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, "<") )
					{
						printf("true\n");
						exit(0);
					}
					else if( !strcmp(compare_func, ">=") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, "<=") )
					{
						printf("true\n");
						exit(0);
					}
				}
				else if( res > 0 ) /* prog is greater than arg */
				{
					if( !strcmp(compare_func,"=") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, ">") )
					{
						printf("true\n");
						exit(0);
					}
					else if( !strcmp(compare_func, "<") )
					{
						printf("false\n");
						exit(0);
					}
					else if( !strcmp(compare_func, ">=") )
					{
						printf("true\n");
						exit(0);
					}
					else if( !strcmp(compare_func, "<=") )
					{
						printf("false\n");
						exit(0);
					}
				}
			}
		}
		else if( program_version[i] && *program_version[i] )
		{
			if( !strcmp(compare_func,"=") )
			{
				printf("false\n");
				exit(0);
			}
			else if( !strcmp(compare_func, ">") )
			{
				printf("true\n");
				exit(0);
			}
			else if( !strcmp(compare_func, "<") )
			{
				printf("false\n");
				exit(0);
			}
			else if( !strcmp(compare_func, ">=") )
			{
				printf("true\n");
				exit(0);
			}
			else if( !strcmp(compare_func, "<=") )
			{
				printf("false\n");
				exit(0);
			}
			
		}
		else if( arg_version[i] && *arg_version[i] )
		{
			if( !strcmp(compare_func,"=") )
			{
				printf("false\n");
				exit(0);
			}
			else if( !strcmp(compare_func, ">") )
			{
				printf("false\n");
				exit(0);
			}
			else if( !strcmp(compare_func, "<") )
			{
				printf("true\n");
				exit(0);
			}
			else if( !strcmp(compare_func, ">=") )
			{
				printf("false\n");
				exit(0);
			}
			else if( !strcmp(compare_func, "<=") )
			{
				printf("true\n");
				exit(0);
			}
		}
		else
			break;
	}
	if( !strcmp(compare_func,"=") )
	{
		printf("true\n");
		exit(0);
	}
	else if( !strcmp(compare_func, ">") )
	{
		printf("false\n");
		exit(0);
	}
	else if( !strcmp(compare_func, "<") )
	{
		printf("false\n");
		exit(0);
	}
	else if( !strcmp(compare_func, ">=") )
	{
		printf("true\n");
		exit(0);
	}
	else if( !strcmp(compare_func, "<=") )
	{
		printf("true\n");
		exit(0);
	}
}

void usage(void)
{
	printf("Usage: <program-path> <comparison> <version>\n\
\n\
	where:\n\
\n\
   program-path = path to program (bison or flex)\n\
   comparison = '>', '<', '<=', '>=', '='  -- depending on shell, you may have to use backslash escapes\n\
   version = a version compare against, say 1.875, or 2.5.4, or whatever.\n\n");
}


int main(int argc, char **argv)
{
	char program_version_string[8000];
	
	/* before starting, check args and make sure all is OK */
	if( argc < 4 || argc > 4 )
	{
		usage();
		exit(-256);
	}
	if ( strcmp(argv[2],"=") && strcmp(argv[2],">") && strcmp(argv[2],"<") && strcmp(argv[2],">=") && strcmp(argv[2],"<=") )
	{
		fprintf(stderr,"vercomp: ILLEGAL input Comparison value: %s\n\n", argv[2]);
		usage();
		exit(-256);
	}
		 
	/* first, extract a version from the command line arg */
	extract_version(argv[3], arg_version);

	/* next, extract a version from the command line */
	get_program_version_string(argv[1], program_version_string);
	extract_version(program_version_string, program_version);
	
	/* next compare and return result */
	compare_versions(argv[2]);
	/* the above func shouldn't return */
}
