/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Populate and remember extensions from static config file
 * 
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#include <sys/types.h>
#include <asterisk/pbx.h>
#include <asterisk/config.h>
#include <asterisk/module.h>
#include <asterisk/logger.h>
#include <asterisk/cli.h>
#include <asterisk/callerid.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
/* For where to put dynamic tables */
#include "../asterisk.h"
#include "../astconf.h"

#ifdef __AST_DEBUG_MALLOC
static void FREE(void *ptr)
{
	free(ptr);
}
#else
#define FREE free
#endif

static char *dtext = "Text Extension Configuration";
static char *config = "extensions.conf";
static char *registrar = "pbx_config";

static int static_config = 0;
static int write_protect_config = 1;
static int autofallthrough_config = 0;

AST_MUTEX_DEFINE_STATIC(save_dialplan_lock);

static struct ast_context *local_contexts = NULL;

/*
 * Help for commands provided by this module ...
 */
static char context_dont_include_help[] =
"Usage: dont include context in include\n"
"       Remove include from context.\n";

static char context_remove_extension_help[] =
"Usage: remove extension exten@context [priority]\n"
"       Remove whole extension from context. If priority is set, we are only\n"
"       removing extension with given priority.\n";

static char context_add_include_help[] =
"Usage: include context in context\n"
"       Include context in other context.\n";

static char save_dialplan_help[] =
"Usage: save dialplan [/path/to/extension/file]\n"
"       Save dialplan created by pbx_config module.\n"
"\n"
"Example: save dialplan                 (/etc/asterisk/extensions.conf)\n"
"         save dialplan /home/markster  (/home/markster/extensions.conf)\n";

static char context_add_extension_help[] =
"Usage: add extension <exten>,<priority>,<app>,<app-data> into <context>\n"
"       [replace]\n\n"
"       This command will add new extension into <context>. If there is an\n"
"       existence of extension with the same priority and last 'replace'\n"
"       arguments is given here we simply replace this extension.\n"
"\n"
"Example: add extension 6123,1,Dial,IAX/216.207.245.56/6123 into local\n"
"         Now, you can dial 6123 and talk to Markster :)\n";

static char context_add_ignorepat_help[] =
"Usage: add ignorepat <pattern> into <context>\n"
"       This command add new ignore pattern into context <context>\n"
"\n"
"Example: add ignorepat _3XX into local\n";

static char context_remove_ignorepat_help[] =
"Usage: remove ignorepat <pattern> from <context>\n"
"       This command remove ignore pattern from context <context>\n"
"\n"
"Example: remove ignorepat _3XX from local\n";

static char reload_extensions_help[] =
"Usage: reload extensions.conf without reloading any other modules\n"
"       This command does not delete global variables\n"
"\n"
"Example: extensions reload\n";

/*
 * Static code
 */
static char *process_quotes_and_slashes(char *start, char find, char replace_with)
{
 	char *dataPut = start;
	int inEscape = 0;
	int inQuotes = 0;

	for (; *start; start++) {
		if (inEscape) {
			*dataPut++ = *start;       /* Always goes verbatim */
			inEscape = 0;
    		} else {
			if (*start == '\\') {
				inEscape = 1;      /* Do not copy \ into the data */
			} else if (*start == '\'') {
				inQuotes = 1-inQuotes;   /* Do not copy ' into the data */
			} else {
				/* Replace , with |, unless in quotes */
				*dataPut++ = inQuotes ? *start : ((*start==find) ? replace_with : *start);
			}
		}
	}
	*dataPut = 0;
	return dataPut;
}

/*
 * Implementation of functions provided by this module
 */

/*
 * REMOVE INCLUDE command stuff
 */
static int handle_context_dont_include(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;

	if (strcmp(argv[3], "in")) return RESULT_SHOWUSAGE;

	if (!ast_context_remove_include(argv[4], argv[2], registrar)) {
		ast_cli(fd, "We are not including '%s' in '%s' now\n",
			argv[2], argv[4]);
		return RESULT_SUCCESS;
	}

	ast_cli(fd, "Failed to remove '%s' include from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_FAILURE;
}

static char *complete_context_dont_include(char *line, char *word,
	int pos, int state)
{
	int which = 0;

	/*
	 * Context completion ...
	 */
	if (pos == 2) {
		struct ast_context *c;

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}

		/* walk pbx_get_contexts ... */
		c = ast_walk_contexts(NULL); 
		while (c) {
			struct ast_include *i;

			if (ast_lock_context(c)) {
				c = ast_walk_contexts(c);
				continue;
			}

			i = ast_walk_context_includes(c, NULL);
			while (i) {
				if (!strlen(word) ||
					!strncmp(ast_get_include_name(i), word, strlen(word))) {
					struct ast_context *nc;
					int already_served = 0;

					/* check if this include is already served or not */

					/* go through all contexts again till we reach actuall
					 * context or already_served = 1
					 */
					nc = ast_walk_contexts(NULL);
					while (nc && nc != c && !already_served) {
						if (!ast_lock_context(nc)) {
							struct ast_include *ni;

							ni = ast_walk_context_includes(nc, NULL);
							while (ni && !already_served) {
								if (!strcmp(ast_get_include_name(i),
									ast_get_include_name(ni)))
									already_served = 1;
								ni = ast_walk_context_includes(nc, ni);
							}	
							
							ast_unlock_context(nc);
						}
						nc = ast_walk_contexts(nc);
					}

					if (!already_served) {
						if (++which > state) {
							char *res =
								strdup(ast_get_include_name(i));
							ast_unlock_context(c);
							ast_unlock_contexts();
							return res;
						}
					}
				}
				i = ast_walk_context_includes(c, i);
			}

			ast_unlock_context(c);
			c = ast_walk_contexts(c);
		}

		ast_unlock_contexts();
		return NULL;
	}

	/*
	 * 'in' completion ... (complete only if previous context is really
	 * included somewhere)
	 */
	if (pos == 3) {
		struct ast_context *c;
		char *context, *dupline, *duplinet;

		if (state > 0) return NULL;

		/* take 'context' from line ... */
		if (!(dupline = strdup(line))) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		duplinet = dupline;
		strsep(&duplinet, " "); /* skip 'dont' */
		strsep(&duplinet, " "); /* skip 'include' */
		context = strsep(&duplinet, " ");

		if (!context) {
			free(dupline);
			return NULL;
		}

		if (ast_lock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			free(dupline);
			return NULL;
		}

		/* go through all contexts and check if is included ... */
		c = ast_walk_contexts(NULL);
		while (c) {
			struct ast_include *i;
			if (ast_lock_context(c)) {
				free(dupline);
				ast_unlock_contexts();
				return NULL;
			}

			i = ast_walk_context_includes(c, NULL);
			while (i) {
				/* is it our context? */
				if (!strcmp(ast_get_include_name(i), context)) {
					/* yes, it is, context is really included, so
					 * complete "in" command
					 */
					free(dupline);
					ast_unlock_context(c);
					ast_unlock_contexts();
					return strdup("in");
				}
				i = ast_walk_context_includes(c, i);
			}
			ast_unlock_context(c);
			c = ast_walk_contexts(c);
		}
		free(dupline);
		ast_unlock_contexts();
		return NULL;
	}

	/*
	 * Context from which we removing include ... 
	 */
	if (pos == 4) {
		struct ast_context *c;
		char *context, *dupline, *duplinet, *in;

		if (!(dupline = strdup(line))) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'dont' */
		strsep(&duplinet, " "); /* skip 'include' */

		if (!(context = strsep(&duplinet, " "))) {
			free(dupline);
			return NULL;
		}

		/* third word must be in */
		in = strsep(&duplinet, " ");
		if (!in ||
			strcmp(in, "in")) {
			free(dupline);
			return NULL;
		}

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			free(dupline);
			return NULL;
		}

		/* walk through all contexts ... */
		c = ast_walk_contexts(NULL);
		while (c) {
			struct ast_include *i;
			if (ast_lock_context(c)) {
				free(dupline);
				return NULL;
			}
	
			/* walk through all includes and check if it is our context */	
			i = ast_walk_context_includes(c, NULL);
			while (i) {
				/* is in this context included another on which we want to
				 * remove?
				 */
				if (!strcmp(context, ast_get_include_name(i))) {
					/* yes, it's included, is matching our word too? */
					if (!strncmp(ast_get_context_name(c),
							word, strlen(word))) {
						/* check state for completion */
						if (++which > state) {
							char *res = strdup(ast_get_context_name(c));
							free(dupline);
							ast_unlock_context(c);
							ast_unlock_contexts();
							return res;
						}
					}
					break;
				}
				i = ast_walk_context_includes(c, i);
			}	
			ast_unlock_context(c);
			c = ast_walk_contexts(c);
		}

		free(dupline);
		ast_unlock_contexts();
		return NULL;
	}

	return NULL;
}

/*
 * REMOVE EXTENSION command stuff
 */
static int handle_context_remove_extension(int fd, int argc, char *argv[])
{
	int removing_priority = 0;
	char *exten, *context;

	if (argc != 4 && argc != 3) return RESULT_SHOWUSAGE;

	/*
	 * Priority input checking ...
	 */
	if (argc == 4) {
		char *c = argv[3];

		/* check for digits in whole parameter for right priority ...
		 * why? because atoi (strtol) returns 0 if any characters in
		 * string and whole extension will be removed, it's not good
		 */
		if (strcmp("hint", c)) {
    		    while (*c != '\0') {
			if (!isdigit(*c++)) {
				ast_cli(fd, "Invalid priority '%s'\n", argv[3]);
				return RESULT_FAILURE;
			}
		    }
		    removing_priority = atoi(argv[3]);
		} else
		    removing_priority = PRIORITY_HINT;

		if (removing_priority == 0) {
			ast_cli(fd, "If you want to remove whole extension, please " \
				"omit priority argument\n");
			return RESULT_FAILURE;
		}
	}

	/*
	 * Format exten@context checking ...
	 */
	if (!(context = strchr(argv[2], (int)'@'))) {
		ast_cli(fd, "First argument must be in exten@context format\n");
		return RESULT_FAILURE;
	}

	*context++ = '\0';
	exten = argv[2];
	if ((!strlen(exten)) || (!(strlen(context)))) {
		ast_cli(fd, "Missing extension or context name in second argument '%s@%s'\n",
			exten == NULL ? "?" : exten, context == NULL ? "?" : context);
		return RESULT_FAILURE;
	}

	if (!ast_context_remove_extension(context, exten, removing_priority, registrar)) {
		if (!removing_priority)
			ast_cli(fd, "Whole extension %s@%s removed\n",
				exten, context);
		else
			ast_cli(fd, "Extension %s@%s with priority %d removed\n",
				exten, context, removing_priority);
			
		return RESULT_SUCCESS;
	}

	ast_cli(fd, "Failed to remove extension %s@%s\n", exten, context);

	return RESULT_FAILURE;
}

#define BROKEN_READLINE 1

#ifdef BROKEN_READLINE
/*
 * There is one funny thing, when you have word like 300@ and you hit
 * <tab>, you arguments will like as your word is '300 ', so it '@'
 * characters acts sometimes as word delimiter and sometimes as a part
 * of word
 *
 * This fix function, allocates new word variable and store here every
 * time xxx@yyy always as one word and correct pos is set too
 *
 * It's ugly, I know, but I'm waiting for Mark suggestion if upper is
 * bug or feature ...
 */
static int fix_complete_args(char *line, char **word, int *pos)
{
	char *_line, *_strsep_line, *_previous_word = NULL, *_word = NULL;
	int words = 0;

	_line = strdup(line);

	_strsep_line = _line;
	while (_strsep_line) {
		_previous_word = _word;
		_word = strsep(&_strsep_line, " ");

		if (_word && strlen(_word)) words++;
	}


	if (_word || _previous_word) {
		if (_word) {
			if (!strlen(_word)) words++;
			*word = strdup(_word);
		} else
			*word = strdup(_previous_word);
		*pos = words - 1;
		free(_line);
		return 0;
	}

	free(_line);
	return -1;
}
#endif /* BROKEN_READLINE */

static char *complete_context_remove_extension(char *line, char *word, int pos,
	int state)
{
	char *ret = NULL;
	int which = 0;

#ifdef BROKEN_READLINE
	/*
	 * Fix arguments, *word is a new allocated structure, REMEMBER to
	 * free *word when you want to return from this function ...
	 */
	if (fix_complete_args(line, &word, &pos)) {
		ast_log(LOG_ERROR, "Out of free memory\n");
		return NULL;
	}
#endif

	/*
	 * exten@context completion ... 
	 */
	if (pos == 2) {
		struct ast_context *c;
		struct ast_exten *e;
		char *context = NULL, *exten = NULL, *delim = NULL;

		/* now, parse values from word = exten@context */
		if ((delim = strchr(word, (int)'@'))) {
			/* check for duplicity ... */
			if (delim != strrchr(word, (int)'@')) {
#ifdef BROKEN_READLINE
				free(word);
#endif
				return NULL;
			}

			*delim = '\0';
			exten = strdup(word);
			context = strdup(delim + 1);
			*delim = '@';
		} else {
			exten = strdup(word);
		}
#ifdef BROKEN_READLINE
		free(word);
#endif

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			free(context); free(exten);
			return NULL;
		}

		/* find our context ... */
		c = ast_walk_contexts(NULL); 
		while (c) {
			/* our context? */
			if ( (!context || !strlen(context)) ||                            /* if no input, all contexts ... */
				 (context && !strncmp(ast_get_context_name(c),
				              context, strlen(context))) ) {                  /* if input, compare ... */
				/* try to complete extensions ... */
				e = ast_walk_context_extensions(c, NULL);
				while (e) {
					/* our extension? */
					if ( (!exten || !strlen(exten)) ||                           /* if not input, all extensions ... */
						 (exten && !strncmp(ast_get_extension_name(e), exten,
						                    strlen(exten))) ) { /* if input, compare ... */
						if (++which > state) {
							/* If there is an extension then return
							 * exten@context.
							 */
							if (exten) {
								ret = malloc(strlen(ast_get_extension_name(e)) +
									strlen(ast_get_context_name(c)) + 2);
								if (ret)
									sprintf(ret, "%s@%s", ast_get_extension_name(e),
										ast_get_context_name(c));
							}
							free(exten); free(context);

							ast_unlock_contexts();
	
							return ret;
						}
					}
					e = ast_walk_context_extensions(c, e);
				}
			}
			c = ast_walk_contexts(c);
		}

		ast_unlock_contexts();

		free(exten); free(context);

		return NULL;
	}

	/*
	 * Complete priority ...
	 */
	if (pos == 3) {
		char *delim, *exten, *context, *dupline, *duplinet, *ec;
		struct ast_context *c;

		dupline = strdup(line);
		if (!dupline) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			return NULL;
		}
		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'remove' */
		strsep(&duplinet, " "); /* skip 'extension */

		if (!(ec = strsep(&duplinet, " "))) {
			free(dupline);
#ifdef BROKEN_READLINE
			free(word);
#endif
			return NULL;
		}

		/* wrong exten@context format? */
		if (!(delim = strchr(ec, (int)'@')) ||
			(strchr(ec, (int)'@') != strrchr(ec, (int)'@'))) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(dupline);
			return NULL;
		}

		/* check if there is exten and context too ... */
		*delim = '\0';
		if ((!strlen(ec)) || (!strlen(delim + 1))) {
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(dupline);
			return NULL;
		}

		exten = strdup(ec);
		context = strdup(delim + 1);
		free(dupline);

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
#ifdef BROKEN_READLINE
			free(word);
#endif
			free(exten); free(context);
			return NULL;
		}

		/* walk contexts */
		c = ast_walk_contexts(NULL); 
		while (c) {
			if (!strcmp(ast_get_context_name(c), context)) {
				struct ast_exten *e;

				/* walk extensions */
				free(context);
				e = ast_walk_context_extensions(c, NULL); 
				while (e) {
					if (!strcmp(ast_get_extension_name(e), exten)) {
						struct ast_exten *priority;
						char buffer[10];
					
						free(exten);
						priority = ast_walk_extension_priorities(e, NULL);
						/* serve priorities */
						do {
							snprintf(buffer, 10, "%u",
								ast_get_extension_priority(priority));
							if (!strncmp(word, buffer, strlen(word))) {
								if (++which > state) {
#ifdef BROKEN_READLINE
									free(word);
#endif
									ast_unlock_contexts();
									return strdup(buffer);
								}
							}
							priority = ast_walk_extension_priorities(e,
								priority);
						} while (priority);

#ifdef BROKEN_READLINE
						free(word);
#endif
						ast_unlock_contexts();
						return NULL;			
					}
					e = ast_walk_context_extensions(c, e);
				}
#ifdef BROKEN_READLINE
				free(word);
#endif
				free(exten);
				ast_unlock_contexts();
				return NULL;
			}
			c = ast_walk_contexts(c);
		}

#ifdef BROKEN_READLINE
		free(word);
#endif
		free(exten); free(context);

		ast_unlock_contexts();
		return NULL;
	}

#ifdef BROKEN_READLINE
	free(word);
#endif
	return NULL; 
}

/*
 * Include context ...
 */
static int handle_context_add_include(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;

	/* third arg must be 'in' ... */
	if (strcmp(argv[3], "in")) return RESULT_SHOWUSAGE;

	if (ast_context_add_include(argv[4], argv[2], registrar)) {
		switch (errno) {
			case ENOMEM:
				ast_cli(fd, "Out of memory for context addition\n"); break;

			case EBUSY:
				ast_cli(fd, "Failed to lock context(s) list, please try again later\n"); break;

			case EEXIST:
				ast_cli(fd, "Context '%s' already included in '%s' context\n",
					argv[1], argv[3]); break;

			case ENOENT:
			case EINVAL:
				ast_cli(fd, "There is no existence of context '%s'\n",
					errno == ENOENT ? argv[4] : argv[2]); break;

			default:
				ast_cli(fd, "Failed to include '%s' in '%s' context\n",
					argv[1], argv[3]); break;
		}
		return RESULT_FAILURE;
	}

	/* show some info ... */
	ast_cli(fd, "Context '%s' included in '%s' context\n",
		argv[2], argv[3]);

	return RESULT_SUCCESS;
}

static char *complete_context_add_include(char *line, char *word, int pos,
    int state)
{
	struct ast_context *c;
	int which = 0;

	/* server context for inclusion ... */
	if (pos == 1)
	{
		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}

		/* server all contexts */ 
		c = ast_walk_contexts(NULL); 
		while (c) {
			if ((!strlen(word) || 
				 !strncmp(ast_get_context_name(c), word, strlen(word))) &&
				++which > state)
			{
				char *context = strdup(ast_get_context_name(c));
				ast_unlock_contexts();
				return context;
			}
			c = ast_walk_contexts(c);
		}

		ast_unlock_contexts();
	}

	/* complete 'in' only if context exist ... */
	if (pos == 2)
	{
		char *context, *dupline, *duplinet;

		if (state != 0) return NULL;

		/* parse context from line ... */
		if (!(dupline = strdup(line))) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			if (state == 0) return strdup("in");
			return NULL;
		}

		duplinet = dupline;

		strsep(&duplinet, " ");
		context = strsep(&duplinet, " ");
		if (context) {
			struct ast_context *c;
			int context_existence = 0;

			/* check for context existence ... */
			if (ast_lock_contexts()) {
				ast_log(LOG_ERROR, "Failed to lock context list\n");
				free(dupline);
				/* our fault, we can't check, so complete 'in' ... */
				return strdup("in");
			}

			c = ast_walk_contexts(NULL);
			while (c && !context_existence) {
				if (!strcmp(context, ast_get_context_name(c))) {
					context_existence = 1;
					continue;
				}
				c = ast_walk_contexts(c);
			}

			/* if context exists, return 'into' ... */
			if (context_existence) {
				free(dupline);
				ast_unlock_contexts();
				return strdup("into");
			}

			ast_unlock_contexts();
		}	

		free(dupline);
		return NULL;
	}

	/* serve context into which we include another context */
	if (pos == 3)
	{
		char *context, *dupline, *duplinet, *in;
		int context_existence = 0;

		if (!(dupline = strdup(line))) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		duplinet = dupline;

		strsep(&duplinet, " "); /* skip 'include' */
		context = strsep(&duplinet, " ");
		in = strsep(&duplinet, " ");

		/* given some context and third word is in? */
		if (!strlen(context) || strcmp(in, "in")) {
			free(dupline);
			return NULL;
		}

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			free(dupline);
			return NULL;
		}

		/* check for context existence ... */
		c = ast_walk_contexts(NULL);
		while (c && !context_existence) {
			if (!strcmp(context, ast_get_context_name(c))) {
				context_existence = 1;
				continue;
			}
			c = ast_walk_contexts(c);
		}

		if (!context_existence) {
			free(dupline);
			ast_unlock_contexts();
			return NULL;
		}

		/* go through all contexts ... */
		c = ast_walk_contexts(NULL);
		while (c) {
			/* must be different contexts ... */
			if (strcmp(context, ast_get_context_name(c))) {
				if (!ast_lock_context(c)) {
					struct ast_include *i;
					int included = 0;

					/* check for duplicity inclusion ... */
					i = ast_walk_context_includes(c, NULL);
					while (i && !included) {
						if (!strcmp(ast_get_include_name(i), context))
							included = 1;
						i = ast_walk_context_includes(c, i);
					}
					ast_unlock_context(c);

					/* not included yet, so show possibility ... */
					if (!included &&
						!strncmp(ast_get_context_name(c), word, strlen(word))){
						
						if (++which > state) {
							char *res = strdup(ast_get_context_name(c));
							free(dupline);
							ast_unlock_contexts();
							return res;
						}
					}	
				}
			}
			c = ast_walk_contexts(c);
		}

		ast_unlock_contexts();
		free(dupline);
		return NULL;
	}

	return NULL;
}

/*
 * 'save dialplan' CLI command implementation functions ...
 */
static int handle_save_dialplan(int fd, int argc, char *argv[])
{
	char filename[256];
	struct ast_context *c;
	struct ast_config *cfg;
	struct ast_variable *v;
	int context_header_written;
	int incomplete = 0; /* incomplete config write? */
	FILE *output;

	if (! (static_config && !write_protect_config)) {
		ast_cli(fd,
			"I can't save dialplan now, see '%s' example file.\n",
			config);
		return RESULT_FAILURE;
	}

	if (argc != 2 && argc != 3) return RESULT_SHOWUSAGE;

	if (ast_mutex_lock(&save_dialplan_lock)) {
		ast_cli(fd,
			"Failed to lock dialplan saving (another proccess saving?)\n");
		return RESULT_FAILURE;
	}

	/* have config path? */
	if (argc == 3) {
		/* is there extension.conf too? */
		if (!strstr(argv[2], ".conf")) {
			/* no, only directory path, check for last '/' occurence */
			if (*(argv[2] + strlen(argv[2]) -1) == '/')
				snprintf(filename, sizeof(filename), "%s%s",
					argv[2], config);
			else
				/* without config extensions.conf, add it */
				snprintf(filename, sizeof(filename), "%s/%s",
					argv[2], config);
		} else
			/* there is an .conf */
			snprintf(filename, sizeof(filename), argv[2]);
	} else
		/* no config file, default one */
		snprintf(filename, sizeof(filename), "%s/%s",
			(char *)ast_config_AST_CONFIG_DIR, config);

	cfg = ast_load("extensions.conf");

	/* try to lock contexts list */
	if (ast_lock_contexts()) {
		ast_cli(fd, "Failed to lock contexts list\n");
		ast_mutex_unlock(&save_dialplan_lock);
		ast_destroy(cfg);
		return RESULT_FAILURE;
	}

	/* create new file ... */
	if (!(output = fopen(filename, "wt"))) {
		ast_cli(fd, "Failed to create file '%s'\n",
			filename);
		ast_unlock_contexts();
		ast_mutex_unlock(&save_dialplan_lock);
		ast_destroy(cfg);
		return RESULT_FAILURE;
	}

	/* fireout general info */
	fprintf(output, "[general]\nstatic=%s\nwriteprotect=%s\n\n",
		static_config ? "yes" : "no",
		write_protect_config ? "yes" : "no");

	if ((v = ast_variable_browse(cfg, "globals"))) {
		fprintf(output, "[globals]\n");
		while(v) {
			fprintf(output, "%s => %s\n", v->name, v->value);
			v = v->next;
		}
		fprintf(output, "\n");
	}

	ast_destroy(cfg);
	
	/* walk all contexts */
	c = ast_walk_contexts(NULL);
	while (c) {
		context_header_written = 0;
	
		/* try to lock context and fireout all info */	
		if (!ast_lock_context(c)) {
			struct ast_exten *e, *last_written_e = NULL;
			struct ast_include *i;
			struct ast_ignorepat *ip;
			struct ast_sw *sw;

			/* registered by this module? */
			if (!strcmp(ast_get_context_registrar(c), registrar)) {
				fprintf(output, "[%s]\n", ast_get_context_name(c));
				context_header_written = 1;
			}

			/* walk extensions ... */
			e = ast_walk_context_extensions(c, NULL);
			while (e) {
				struct ast_exten *p;

				/* fireout priorities */
				p = ast_walk_extension_priorities(e, NULL);
				while (p) {
					if (!strcmp(ast_get_extension_registrar(p),
						registrar)) {
			
						/* make empty line between different extensions */	
						if (last_written_e != NULL &&
							strcmp(ast_get_extension_name(last_written_e),
								ast_get_extension_name(p)))
							fprintf(output, "\n");
						last_written_e = p;
				
						if (!context_header_written) {
							fprintf(output, "[%s]\n", ast_get_context_name(c));
							context_header_written = 1;
						}

						if (ast_get_extension_priority(p)!=PRIORITY_HINT) {
							char *tempdata = NULL, *startdata;
							tempdata = strdup((char *)ast_get_extension_app_data(p));
							if (tempdata) {
								startdata = tempdata;
								while (*tempdata) {
									if (*tempdata == '|')
										*tempdata = ',';
									tempdata++;
								}
								tempdata = startdata;
							}
							if (ast_get_extension_matchcid(p))
								fprintf(output, "exten => %s/%s,%d,%s(%s)\n",
								    ast_get_extension_name(p),
								    ast_get_extension_cidmatch(p),
								    ast_get_extension_priority(p),
								    ast_get_extension_app(p),
								    tempdata);
							else
								fprintf(output, "exten => %s,%d,%s(%s)\n",
								    ast_get_extension_name(p),
								    ast_get_extension_priority(p),
								    ast_get_extension_app(p),
								    tempdata);
							if (tempdata)
								free(tempdata);
						} else
							fprintf(output, "exten => %s,hint,%s\n",
							    ast_get_extension_name(p),
							    ast_get_extension_app(p));
						
					}
					p = ast_walk_extension_priorities(e, p);
				}

				e = ast_walk_context_extensions(c, e);
			}

			/* written any extensions? ok, write space between exten & inc */
			if (last_written_e) fprintf(output, "\n");

			/* walk through includes */
			i = ast_walk_context_includes(c, NULL);
			while (i) {
				if (!strcmp(ast_get_include_registrar(i), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", ast_get_context_name(c));
						context_header_written = 1;
					}
					fprintf(output, "include => %s\n",
						ast_get_include_name(i));
				}
				i = ast_walk_context_includes(c, i);
			}

			if (ast_walk_context_includes(c, NULL))
				fprintf(output, "\n");

			/* walk through switches */
			sw = ast_walk_context_switches(c, NULL);
			while (sw) {
				if (!strcmp(ast_get_switch_registrar(sw), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", ast_get_context_name(c));
						context_header_written = 1;
					}
					fprintf(output, "switch => %s/%s\n",
						ast_get_switch_name(sw),
						ast_get_switch_data(sw));
				}
				sw = ast_walk_context_switches(c, sw);
			}

			if (ast_walk_context_switches(c, NULL))
				fprintf(output, "\n");

			/* fireout ignorepats ... */
			ip = ast_walk_context_ignorepats(c, NULL);
			while (ip) {
				if (!strcmp(ast_get_ignorepat_registrar(ip), registrar)) {
					if (!context_header_written) {
						fprintf(output, "[%s]\n", ast_get_context_name(c));
						context_header_written = 1;
					}

					fprintf(output, "ignorepat => %s\n",
						ast_get_ignorepat_name(ip));
				}
				ip = ast_walk_context_ignorepats(c, ip);
			}

			ast_unlock_context(c);
		} else
			incomplete = 1;

		c = ast_walk_contexts(c);
	}	

	ast_unlock_contexts();
	ast_mutex_unlock(&save_dialplan_lock);
	fclose(output);

	if (incomplete) {
		ast_cli(fd, "Saved dialplan is incomplete\n");
		return RESULT_FAILURE;
	}

	ast_cli(fd, "Dialplane successfully saved into '%s'\n",
		filename);
	return RESULT_SUCCESS;
}

/*
 * ADD EXTENSION command stuff
 */
static int handle_context_add_extension(int fd, int argc, char *argv[])
{
	char *whole_exten;
	char *exten, *prior;
	int iprior = -2;
	char *cidmatch, *app, *app_data;
	char *start, *end;

	/* check for arguments at first */
	if (argc != 5 && argc != 6) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into")) return RESULT_SHOWUSAGE;
	if (argc == 6) if (strcmp(argv[5], "replace")) return RESULT_SHOWUSAGE;

	whole_exten = argv[2];
	exten 		= strsep(&whole_exten,",");
	if (strchr(exten, '/')) {
		cidmatch = exten;
		strsep(&cidmatch,"/");
	} else {
		cidmatch = NULL;
	}
	prior       = strsep(&whole_exten,",");
	if (prior) {
    	if (!strcmp(prior, "hint")) {
			iprior = PRIORITY_HINT;
		} else {
			if (sscanf(prior, "%i", &iprior) != 1) {
				ast_cli(fd, "'%s' is not a valid priority\n", prior);
				prior = NULL;
			}
		}
	}
	app = whole_exten;
	if (app && (start = strchr(app, '(')) && (end = strrchr(app, ')'))) {
		*start = *end = '\0';
		app_data = start + 1;
		process_quotes_and_slashes(app_data, ',', '|');
	} else {
		if (app) {
			app_data = strchr(app, ',');
			if (app_data) {
				*app_data = '\0';
				app_data++;
			}
		} else	
			app_data = NULL;
	}

	if (!exten || !prior || !app || (!app_data && iprior != PRIORITY_HINT)) return RESULT_SHOWUSAGE;

	if (!app_data)
		app_data="";
	if (ast_add_extension(argv[4], argc == 6 ? 1 : 0, exten, iprior, NULL, cidmatch, app,
		(void *)strdup(app_data), free, registrar)) {
		switch (errno) {
			case ENOMEM:
				ast_cli(fd, "Out of free memory\n"); break;

			case EBUSY:
				ast_cli(fd, "Failed to lock context(s) list, please try again later\n"); break;

			case ENOENT:
				ast_cli(fd, "No existence of '%s' context\n", argv[4]); break;

			case EEXIST:
				ast_cli(fd, "Extension %s@%s with priority %s already exists\n",
					exten, argv[4], prior); break;

			default:
				ast_cli(fd, "Failed to add '%s,%s,%s,%s' extension into '%s' context\n",
					exten, prior, app, app_data, argv[4]); break;
		}
		return RESULT_FAILURE;
	}

	if (argc == 6) 
		ast_cli(fd, "Extension %s@%s (%s) replace by '%s,%s,%s,%s'\n",
			exten, argv[4], prior, exten, prior, app, app_data);
	else
		ast_cli(fd, "Extension '%s,%s,%s,%s' added into '%s' context\n",
			exten, prior, app, app_data, argv[4]);

	return RESULT_SUCCESS;
}

/* add extension 6123,1,Dial,IAX/212.71.138.13/6123 into local */
static char *complete_context_add_extension(char *line, char *word,
	int pos, int state)
{
	int which = 0;

	/* complete 'into' word ... */
	if (pos == 3) {
		if (state == 0) return strdup("into");
		return NULL;
	}

	/* complete context */
	if (pos == 4) {
		struct ast_context *c;

		/* try to lock contexts list ... */
		if (ast_lock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		/* walk through all contexts */
		c = ast_walk_contexts(NULL);
		while (c) {
			/* matching context? */
			if (!strncmp(ast_get_context_name(c), word, strlen(word))) {
				if (++which > state) {
					char *res = strdup(ast_get_context_name(c));
					ast_unlock_contexts();
					return res;
				}
			}
			c = ast_walk_contexts(c);
		}

		ast_unlock_contexts();
		return NULL;
	}

	if (pos == 5) return state == 0 ? strdup("replace") : NULL;

	return NULL;
}

/*
 * IGNOREPAT CLI stuff
 */
static int handle_context_add_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into")) return RESULT_SHOWUSAGE;

	if (ast_context_add_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
			case ENOMEM:
				ast_cli(fd, "Out of free memory\n"); break;

			case ENOENT:
				ast_cli(fd, "There is no existence of '%s' context\n", argv[4]);
				break;

			case EEXIST:
				ast_cli(fd, "Ignore pattern '%s' already included in '%s' context\n",
					argv[2], argv[4]);
				break;

			case EBUSY:
				ast_cli(fd, "Failed to lock context(s) list, please, try again later\n");
				break;

			default:
				ast_cli(fd, "Failed to add ingore pattern '%s' into '%s' context\n",
					argv[2], argv[4]);
				break;
		}
		return RESULT_FAILURE;
	}

	ast_cli(fd, "Ignore pattern '%s' added into '%s' context\n",
		argv[2], argv[4]);
	return RESULT_SUCCESS;
}

static char *complete_context_add_ignorepat(char *line, char *word,
	int pos, int state)
{
	if (pos == 3) return state == 0 ? strdup("into") : NULL;

	if (pos == 4) {
		struct ast_context *c;
		int which = 0;
		char *dupline, *duplinet, *ignorepat = NULL;

		dupline = strdup(line);
		duplinet = dupline;

		if (duplinet) {
			strsep(&duplinet, " "); /* skip 'add' */
			strsep(&duplinet, " "); /* skip 'ignorepat' */
			ignorepat = strsep(&duplinet, " ");
		}

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock contexts list\n");
			return NULL;
		}

		c = ast_walk_contexts(NULL);
		while (c) {
			if (!strncmp(ast_get_context_name(c), word, strlen(word))) {
				int serve_context = 1;
				if (ignorepat) {
					if (!ast_lock_context(c)) {
						struct ast_ignorepat *ip;
						ip = ast_walk_context_ignorepats(c, NULL);
						while (ip && serve_context) {
							if (!strcmp(ast_get_ignorepat_name(ip), ignorepat))
								serve_context = 0;
							ip = ast_walk_context_ignorepats(c, ip);
						}
						ast_unlock_context(c);
					}
				}
				if (serve_context) {
					if (++which > state) {
						char *context = strdup(ast_get_context_name(c));
						if (dupline) free(dupline);
						ast_unlock_contexts();
						return context;
					}
				}
			}
			c = ast_walk_contexts(c);
		}

		if (dupline) free(dupline);
		ast_unlock_contexts();
		return NULL;
	}

	return NULL;
}

static int handle_context_remove_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 5) return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "from")) return RESULT_SHOWUSAGE;

	if (ast_context_remove_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
			case EBUSY:
				ast_cli(fd, "Failed to lock context(s) list, please try again later\n");
				break;

			case ENOENT:
				ast_cli(fd, "There is no existence of '%s' context\n", argv[4]);
				break;

			case EINVAL:
				ast_cli(fd, "There is no existence of '%s' ignore pattern in '%s' context\n",
					argv[2], argv[4]);
				break;

			default:
				ast_cli(fd, "Failed to remove ignore pattern '%s' from '%s' context\n", argv[2], argv[4]);
				break;
		}
		return RESULT_FAILURE;
	}

	ast_cli(fd, "Ignore pattern '%s' removed from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_SUCCESS;
}

static int pbx_load_module(void);

static int handle_reload_extensions(int fd, int argc, char *argv[])
{
	if (argc!=2) return RESULT_SHOWUSAGE;
	pbx_load_module();
	return RESULT_SUCCESS;
}

static char *complete_context_remove_ignorepat(char *line, char *word,
	int pos, int state)
{
	struct ast_context *c;
	int which = 0;

	if (pos == 2) {
		if (ast_lock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		c = ast_walk_contexts(NULL);
		while (c) {
			if (!ast_lock_context(c)) {
				struct ast_ignorepat *ip;
			
				ip = ast_walk_context_ignorepats(c, NULL);
				while (ip) {
					if (!strncmp(ast_get_ignorepat_name(ip), word, strlen(word))) {
						if (which + 1 > state) {
							struct ast_context *cw;
							int already_served = 0;
							cw = ast_walk_contexts(NULL);
							while (cw && cw != c && !already_served) {
								if (!ast_lock_context(cw)) {
									struct ast_ignorepat *ipw;
									ipw = ast_walk_context_ignorepats(cw, NULL);
									while (ipw) {
										if (!strcmp(ast_get_ignorepat_name(ipw),
											ast_get_ignorepat_name(ip))) already_served = 1;
										ipw = ast_walk_context_ignorepats(cw, ipw);
									}
									ast_unlock_context(cw);
								}
								cw = ast_walk_contexts(cw);
							}
							if (!already_served) {
								char *ret = strdup(ast_get_ignorepat_name(ip));
								ast_unlock_context(c);
								ast_unlock_contexts();
								return ret;
							}
						} else
							which++;
					}
					ip = ast_walk_context_ignorepats(c, ip);
				}

				ast_unlock_context(c);
			}
			c = ast_walk_contexts(c);
		}

		ast_unlock_contexts();
		return NULL;
	}
 
	if (pos == 3) return state == 0 ? strdup("from") : NULL;

	if (pos == 4) {
		char *dupline, *duplinet, *ignorepat;

		dupline = strdup(line);
		if (!dupline) {
			ast_log(LOG_WARNING, "Out of free memory\n");
			return NULL;
		}

		duplinet = dupline;
		strsep(&duplinet, " ");
		strsep(&duplinet, " ");
		ignorepat = strsep(&duplinet, " ");

		if (!ignorepat) {
			free(dupline);
			return NULL;
		}

		if (ast_lock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			free(dupline);
			return NULL;
		}

		c = ast_walk_contexts(NULL);
		while (c) {
			if (!ast_lock_context(c)) {
				struct ast_ignorepat *ip;
				ip = ast_walk_context_ignorepats(c, NULL);
				while (ip) {
					if (!strcmp(ast_get_ignorepat_name(ip), ignorepat)) {
						if (!strncmp(ast_get_context_name(c), word, strlen(word))) {
							if (++which > state) {
								char *ret = strdup(ast_get_context_name(c));
								free(dupline);
								ast_unlock_context(c);
								ast_unlock_contexts();
								return ret;
							}
						}
					}
					ip = ast_walk_context_ignorepats(c, ip);
				}

				ast_unlock_context(c);
			}
			c = ast_walk_contexts(c);
		}

		free(dupline);
		ast_unlock_contexts();
		return NULL;
	}

	return NULL;
}

/*
 * CLI entries for commands provided by this module
 */
static struct ast_cli_entry context_dont_include_cli =
	{ { "dont", "include", NULL }, handle_context_dont_include,
		"Remove a specified include from context", context_dont_include_help,
		complete_context_dont_include };

static struct ast_cli_entry context_remove_extension_cli =
	{ { "remove", "extension", NULL }, handle_context_remove_extension,
		"Remove a specified extension", context_remove_extension_help,
		complete_context_remove_extension };

static struct ast_cli_entry context_add_include_cli =
	{ { "include", "context", NULL }, handle_context_add_include,
		"Include context in other context", context_add_include_help,
		complete_context_add_include };

static struct ast_cli_entry save_dialplan_cli =
	{ { "save", "dialplan", NULL }, handle_save_dialplan,
		"Save dialplan", save_dialplan_help };

static struct ast_cli_entry context_add_extension_cli =
	{ { "add", "extension", NULL }, handle_context_add_extension,
		"Add new extension into context", context_add_extension_help,
		complete_context_add_extension };

static struct ast_cli_entry context_add_ignorepat_cli =
	{ { "add", "ignorepat", NULL }, handle_context_add_ignorepat,
		"Add new ignore pattern", context_add_ignorepat_help,
		complete_context_add_ignorepat };

static struct ast_cli_entry context_remove_ignorepat_cli =
	{ { "remove", "ignorepat", NULL }, handle_context_remove_ignorepat,
		"Remove ignore pattern from context", context_remove_ignorepat_help,
		complete_context_remove_ignorepat };

static struct ast_cli_entry reload_extensions_cli = 
	{ { "extensions", "reload", NULL}, handle_reload_extensions,
		"Reload extensions and *only* extensions", reload_extensions_help };

/*
 * Standard module functions ...
 */
int unload_module(void)
{
	ast_cli_unregister(&context_add_extension_cli);
	if (static_config && !write_protect_config)
		ast_cli_unregister(&save_dialplan_cli);
	ast_cli_unregister(&context_add_include_cli);
	ast_cli_unregister(&context_dont_include_cli);
	ast_cli_unregister(&context_remove_extension_cli);
	ast_cli_unregister(&context_remove_ignorepat_cli);
	ast_cli_unregister(&context_add_ignorepat_cli);
	ast_cli_unregister(&reload_extensions_cli);
	ast_context_destroy(NULL, registrar);
	return 0;
}

static int pbx_load_module(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	char *cxt, *ext, *pri, *appl, *data, *tc, *cidmatch;
	struct ast_context *con;
	char *start, *end;
	char *label;
	char realvalue[256];
	int lastpri = -2;

	cfg = ast_load(config);
	if (cfg) {
		/* Use existing config to populate the PBX table */
		static_config = ast_true(ast_variable_retrieve(cfg, "general",
			"static"));
		write_protect_config = ast_true(ast_variable_retrieve(cfg, "general",
			"writeprotect"));
		
		autofallthrough_config = ast_true(ast_variable_retrieve(cfg, "general",
			"autofallthrough"));

		v = ast_variable_browse(cfg, "globals");
		while(v) {
			memset(realvalue, 0, sizeof(realvalue));
			pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
			pbx_builtin_setvar_helper(NULL, v->name, realvalue);
			v = v->next;
		}
		cxt = ast_category_browse(cfg, NULL);
		while(cxt) {
			/* All categories but "general" or "globals" are considered contexts */
			if (!strcasecmp(cxt, "general") || !strcasecmp(cxt, "globals")) {
				cxt = ast_category_browse(cfg, cxt);
				continue;
			}
			if ((con=ast_context_create(&local_contexts,cxt, registrar))) {
				v = ast_variable_browse(cfg, cxt);
				while(v) {
					if (!strcasecmp(v->name, "exten")) {
						char *stringp=NULL;
						int ipri = -2;
						char realext[256]="";
						char *plus;
						tc = strdup(v->value);
						if(tc!=NULL){
							stringp=tc;
							ext = strsep(&stringp, ",");
							if (!ext)
								ext="";
							cidmatch = strchr(ext, '/');
							if (cidmatch) {
								*cidmatch = '\0';
								cidmatch++;
								ast_shrink_phone_number(cidmatch);
							}
							pri = strsep(&stringp, ",");
							if (!pri)
								pri="";
							label = strchr(pri, '(');
							if (label) {
								*label = '\0';
								label++;
								end = strchr(label, ')');
								if (end)
									*end = '\0';
								else
									ast_log(LOG_WARNING, "Label missing trailing ')' at line %d\n", v->lineno);
							}
							plus = strchr(pri, '+');
							if (plus) {
								*plus = '\0';
								plus++;
							}
							if (!strcmp(pri,"hint"))
								ipri=PRIORITY_HINT;
							else if (!strcmp(pri, "next") || !strcmp(pri, "n")) {
								if (lastpri > -2)
									ipri = lastpri + 1;
								else
									ast_log(LOG_WARNING, "Can't use 'next' priority on the first entry!\n");
							} else if (!strcmp(pri, "same") || !strcmp(pri, "s")) {
								if (lastpri > -2)
									ipri = lastpri;
								else
									ast_log(LOG_WARNING, "Can't use 'same' priority on the first entry!\n");
							} else  {
								if (sscanf(pri, "%i", &ipri) != 1) {
									if ((ipri = ast_findlabel_extension2(NULL, con, ext, pri, cidmatch)) < 1) {
										ast_log(LOG_WARNING, "Invalid priority/label '%s' at line %d\n", pri, v->lineno);
										ipri = 0;
									}
								}
							}
							appl = stringp;
							if (!appl)
								appl="";
							if (!(start = strchr(appl, '('))) {
								if (stringp)
									appl = strsep(&stringp, ",");
								else
									appl = "";
							}
							if (start && (end = strrchr(appl, ')'))) {
								*start = *end = '\0';
								data = start + 1;
								process_quotes_and_slashes(data, ',', '|');
							} else if (stringp!=NULL && *stringp=='"') {
								stringp++;
								data = strsep(&stringp, "\"");
								stringp++;
							} else {
								if (stringp)
									data = strsep(&stringp, ",");
								else
									data = "";
							}

							if (!data)
								data="";
							while(*appl && (*appl < 33)) appl++;
							pbx_substitute_variables_helper(NULL, ext, realext, sizeof(realext) - 1);
							if (ipri) {
								if (plus)
									ipri += atoi(plus);
								lastpri = ipri;
								if (ast_add_extension2(con, 0, realext, ipri, label, cidmatch, appl, strdup(data), FREE, registrar)) {
									ast_log(LOG_WARNING, "Unable to register extension at line %d\n", v->lineno);
								}
							}
							free(tc);
						} else fprintf(stderr,"Error strdup returned NULL in %s\n",__PRETTY_FUNCTION__);
					} else if(!strcasecmp(v->name, "include")) {
						memset(realvalue, 0, sizeof(realvalue));
						pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
						if (ast_context_add_include2(con, realvalue, registrar))
							ast_log(LOG_WARNING, "Unable to include context '%s' in context '%s'\n", v->value, cxt);
					} else if(!strcasecmp(v->name, "ignorepat")) {
						memset(realvalue, 0, sizeof(realvalue));
						pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
						if (ast_context_add_ignorepat2(con, realvalue, registrar))
							ast_log(LOG_WARNING, "Unable to include ignorepat '%s' in context '%s'\n", v->value, cxt);
					} else if (!strcasecmp(v->name, "switch") || !strcasecmp(v->name, "lswitch") || !strcasecmp(v->name, "eswitch")) {
						char *stringp=NULL;
						memset(realvalue, 0, sizeof(realvalue));
						if (!strcasecmp(v->name, "switch"))
							pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
						else
							strncpy(realvalue, v->value, sizeof(realvalue) - 1);
						tc = realvalue;
						stringp=tc;
						appl = strsep(&stringp, "/");
						data = strsep(&stringp, "");
						if (!data)
							data = "";
						if (ast_context_add_switch2(con, appl, data, !strcasecmp(v->name, "eswitch"), registrar))
							ast_log(LOG_WARNING, "Unable to include switch '%s' in context '%s'\n", v->value, cxt);
					}
					v = v->next;
				}
			}
			cxt = ast_category_browse(cfg, cxt);
		}
		ast_destroy(cfg);
	}
	ast_merge_contexts_and_delete(&local_contexts,registrar);

	for (con = ast_walk_contexts(NULL); con; con = ast_walk_contexts(con))
		ast_context_verify_includes(con);

	pbx_set_autofallthrough(autofallthrough_config);

	return 0;
}

int load_module(void)
{
	if (pbx_load_module()) return -1;
 
	ast_cli_register(&context_remove_extension_cli);
	ast_cli_register(&context_dont_include_cli);
	ast_cli_register(&context_add_include_cli);
	if (static_config && !write_protect_config)
		ast_cli_register(&save_dialplan_cli);
	ast_cli_register(&context_add_extension_cli);
	ast_cli_register(&context_add_ignorepat_cli);
	ast_cli_register(&context_remove_ignorepat_cli);
	ast_cli_register(&reload_extensions_cli);

	return 0;
}

int reload(void)
{
	ast_context_destroy(NULL, registrar);
	/* For martin's global variables, don't clear them on reload */
#if 0
	pbx_builtin_clear_globals();
#endif	
	pbx_load_module();
	return 0;
}

int usecount(void)
{
	return 0;
}

char *description(void)
{
	return dtext;
}

char *key(void)
{
	return ASTERISK_GPL_KEY;
}
