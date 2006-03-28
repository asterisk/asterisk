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

/*! \file
 *
 * \brief Populate and remember extensions from static config file
 *
 * 
 */

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/callerid.h"

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
static int clearglobalvars_config = 0;

AST_MUTEX_DEFINE_STATIC(save_dialplan_lock);

static struct ast_context *local_contexts = NULL;

/*
 * Help for commands provided by this module ...
 */
static char context_dont_include_help[] =
"Usage: dont include <context> in <context>\n"
"       Remove an included context from another context.\n";

static char context_remove_extension_help[] =
"Usage: remove extension exten@context [priority]\n"
"       Remove an extension from a given context. If a priority\n"
"       is given, only that specific priority from the given extension\n"
"       will be removed.\n";

static char context_add_include_help[] =
"Usage: include <context> in <context>\n"
"       Include a context in another context.\n";

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
"       This command adds a new ignore pattern into context <context>\n"
"\n"
"Example: add ignorepat _3XX into local\n";

static char context_remove_ignorepat_help[] =
"Usage: remove ignorepat <pattern> from <context>\n"
"       This command removes an ignore pattern from context <context>\n"
"\n"
"Example: remove ignorepat _3XX from local\n";

static char reload_extensions_help[] =
"Usage: reload extensions.conf without reloading any other modules\n"
"       This command does not delete global variables unless\n"
"       clearglobalvars is set to yes in extensions.conf\n"
"\n"
"Example: extensions reload\n";

/*
 * Implementation of functions provided by this module
 */

/*!
 * REMOVE INCLUDE command stuff
 */
static int handle_context_dont_include(int fd, int argc, char *argv[])
{
	if (argc != 5)
		return RESULT_SHOWUSAGE;

	if (strcmp(argv[3], "in"))
		return RESULT_SHOWUSAGE;

	if (!ast_context_remove_include(argv[4], argv[2], registrar)) {
		ast_cli(fd, "We are not including '%s' in '%s' now\n",
			argv[2], argv[4]);
		return RESULT_SUCCESS;
	}

	ast_cli(fd, "Failed to remove '%s' include from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_FAILURE;
}

/*! \brief return true if 'name' is included by context c */
static int lookup_ci(struct ast_context *c, const char *name)
{
	struct ast_include *i = NULL;

	if (ast_lock_context(c))	/* error, skip */
		return 0;
	while ( (i = ast_walk_context_includes(c, i)) )
		if (!strcmp(name, ast_get_include_name(i)))
			break;
	ast_unlock_context(c);
	return i ? -1 /* success */ : 0;
}

/*! \brief return true if 'name' is in the ignorepats for context c */
static int lookup_c_ip(struct ast_context *c, const char *name)
{
	struct ast_ignorepat *ip = NULL;

	if (ast_lock_context(c))	/* error, skip */
		return 0;
	while ( (ip = ast_walk_context_ignorepats(c, ip)) )
		if (!strcmp(name, ast_get_ignorepat_name(ip)))
			break;
	ast_unlock_context(c);
	return ip ? -1 /* success */ : 0;
}

/*! \brief moves to the n-th word in the string, or empty string if none */
static const char *skip_words(const char *p, int n)
{
	int in_blank = 0;
	for (;n && *p; p++) {
		if (isblank(*p) /* XXX order is important */ && !in_blank) {
			n--;	/* one word is gone */
			in_blank = 1;
		} else if (/* !is_blank(*p), we know already, && */ in_blank) {
			in_blank = 0;
		}
	}
	return p;
}

/*! \brief match the first 'len' chars of word. len==0 always succeeds */
static int partial_match(const char *s, const char *word, int len)
{
	return (len == 0 || !strncmp(s, word, len));
}

/*! \brief split extension@context in two parts, return -1 on error.
 * The return string is malloc'ed and pointed by *ext
 */
static int split_ec(const char *src, char **ext, char ** const ctx)
{
	char *c, *e = ast_strdup(src); /* now src is not used anymore */

	if (e == NULL)
		return -1;	/* malloc error */
	/* now, parse values from 'exten@context' */
	*ext = e;
	c = strchr(e, '@');
	if (c == NULL)	/* no context part */
		*ctx = "";	/* it is not overwritten, anyways */
	else {	/* found context, check for duplicity ... */
		*c++ = '\0';
		*ctx = c;
		if (strchr(c, '@')) { /* two @, not allowed */
			free(e);
			return -1;
		}
	} 
	return 0;
}

/* _X_ is the string we need to complete */
static char *complete_context_dont_include(const char *line, const char *word,
	int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int len = strlen(word); /* how many bytes to match */
	struct ast_context *c = NULL;

	if (pos == 2) {		/* "dont include _X_" */
		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}
		/* walk contexts and their includes, return the n-th match */
		while (!res && (c = ast_walk_contexts(c))) {
			struct ast_include *i = NULL;

			if (ast_lock_context(c))	/* error ? skip this one */
				continue;

			while ( !res && (i = ast_walk_context_includes(c, i)) ) {
				const char *i_name = ast_get_include_name(i);
				struct ast_context *nc = NULL;
				int already_served = 0;

				if (!partial_match(i_name, word, len))
					continue;	/* not matched */

				/* check if this include is already served or not */

				/* go through all contexts again till we reach actual
				 * context or already_served = 1
				 */
				while ( (nc = ast_walk_contexts(nc)) && nc != c && !already_served)
					already_served = lookup_ci(nc, i_name);

				if (!already_served && ++which > state)
					res = strdup(i_name);
			}
			ast_unlock_context(c);
		}

		ast_unlock_contexts();
		return res;
	} else if (pos == 3) { /* "dont include CTX _X_" */
		/*
		 * complete as 'in', but only if previous context is really
		 * included somewhere
		 */
		char *context, *dupline;
		const char *s = skip_words(line, 2); /* skip 'dont' 'include' */

		if (state > 0)
			return NULL;
		context = dupline = strdup(s);
		if (!dupline) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}
		strsep(&dupline, " ");

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock contexts list\n");
			free(context);
			return NULL;
		}

		/* go through all contexts and check if is included ... */
		while (!res && (c = ast_walk_contexts(c)))
			if (lookup_ci(c, context)) /* context is really included, complete "in" command */
				res = strdup("in");
		ast_unlock_contexts();
		if (!res)
			ast_log(LOG_WARNING, "%s not included anywhere\n", context);
		free(context);
		return res;
	} else if (pos == 4) { /* "dont include CTX in _X_" */
		/*
		 * Context from which we removing include ... 
		 */
		char *context, *dupline, *in;
		const char *s = skip_words(line, 2); /* skip 'dont' 'include' */
		context = dupline = strdup(s);
		if (!dupline) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		strsep(&dupline, " "); /* skip context */

		/* third word must be 'in' */
		in = strsep(&dupline, " ");
		if (!in || strcmp(in, "in")) {
			free(context);
			return NULL;
		}

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			free(context);
			return NULL;
		}

		/* walk through all contexts ... */
		c = NULL;
		while ( !res && (c = ast_walk_contexts(c))) {
			const char *c_name = ast_get_context_name(c);
			if (!partial_match(c_name, word, len))	/* not a good target */
				continue;
			/* walk through all includes and check if it is our context */	
			if (lookup_ci(c, context) && ++which > state)
				res = strdup(c_name);
		}
		ast_unlock_contexts();
		free(context);
		return res;
	}

	return NULL;
}

/*!
 * REMOVE EXTENSION command stuff
 */
static int handle_context_remove_extension(int fd, int argc, char *argv[])
{
	int removing_priority = 0;
	char *exten, *context;
	int ret = RESULT_FAILURE;

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
		if (!strcmp("hint", c))
			removing_priority = PRIORITY_HINT;
		else {
			while (*c && isdigit(*c))
				c++;
			if (*c) { /* non-digit in string */
				ast_cli(fd, "Invalid priority '%s'\n", argv[3]);
				return RESULT_FAILURE;
			}
			removing_priority = atoi(argv[3]);
		}

		if (removing_priority == 0) {
			ast_cli(fd, "If you want to remove whole extension, please " \
				"omit priority argument\n");
			return RESULT_FAILURE;
		}
	}

	/* XXX original overwrote argv[2] */
	/*
	 * Format exten@context checking ...
	 */
	if (split_ec(argv[2], &exten, &context))
		return RESULT_FAILURE; /* XXX malloc failure */
	if ((!strlen(exten)) || (!(strlen(context)))) {
		ast_cli(fd, "Missing extension or context name in second argument '%s'\n",
			argv[2]);
		free(exten);
		return RESULT_FAILURE;
	}

	if (!ast_context_remove_extension(context, exten, removing_priority, registrar)) {
		if (!removing_priority)
			ast_cli(fd, "Whole extension %s@%s removed\n",
				exten, context);
		else
			ast_cli(fd, "Extension %s@%s with priority %d removed\n",
				exten, context, removing_priority);
			
		ret = RESULT_SUCCESS;
	} else {
		ast_cli(fd, "Failed to remove extension %s@%s\n", exten, context);
		ret = RESULT_FAILURE;
	}
	free(exten);
	return ret;
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
static int fix_complete_args(const char *line, char **word, int *pos)
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

static char *complete_context_remove_extension(const char *line, const char *word, int pos,
	int state)
{
	char *ret = NULL;
	int which = 0;

#ifdef BROKEN_READLINE
	char *word2;
	/*
	 * Fix arguments, *word is a new allocated structure, REMEMBER to
	 * free *word when you want to return from this function ...
	 */
	if (fix_complete_args(line, &word2, &pos)) {
		ast_log(LOG_ERROR, "Out of free memory\n");
		return NULL;
	}
	word = word2;
#endif

	if (pos == 2) { /* 'remove extension _X_' (exten@context ... */
		struct ast_context *c = NULL;
		char *context = NULL, *exten = NULL;
		int le = 0;	/* length of extension */
		int lc = 0;	/* length of context */

		lc = split_ec(word, &exten, &context);
#ifdef BROKEN_READLINE
		free(word2);
#endif
		if (lc)	/* error */
			return NULL;
		le = strlen(exten);
		lc = strlen(context);

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			goto error2;
		}

		/* find our context ... */
		while ( (c = ast_walk_contexts(c)) ) {	/* match our context if any */
			struct ast_exten *e = NULL;
			/* XXX locking ? */
			if (!partial_match(ast_get_context_name(c), context, lc))
				continue;	/* context not matched */
			while ( (e = ast_walk_context_extensions(c, e)) ) { /* try to complete extensions ... */
				if ( partial_match(ast_get_extension_name(e), exten, le) && ++which > state) { /* n-th match */
					/* If there is an extension then return exten@context. XXX otherwise ? */
					if (exten)
						asprintf(&ret, "%s@%s", ast_get_extension_name(e), ast_get_context_name(c));
					break;
				}
			}
			if (e)	/* got a match */
				break;
		}

		ast_unlock_contexts();
	error2:
		if (exten)
			free(exten);
	} else if (pos == 3) { /* 'remove extension EXT _X_' (priority) */
		char *exten = NULL, *context, *p;
		struct ast_context *c;
		int le, lc, len;
		const char *s = skip_words(line, 2); /* skip 'remove' 'extension' */
		int i = split_ec(s, &exten, &context);	/* parse ext@context */

		if (i)	/* error */
			goto error3;
		if ( (p = strchr(exten, ' ')) ) /* remove space after extension */
			*p = '\0';
		if ( (p = strchr(context, ' ')) ) /* remove space after context */
			*p = '\0';
		le = strlen(exten);
		lc = strlen(context);
		len = strlen(word);
		if (le == 0 || lc == 0)
			goto error3;

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			goto error3;
		}

		/* walk contexts */
		c = NULL;
		while ( (c = ast_walk_contexts(c)) ) {
			/* XXX locking on c ? */
			struct ast_exten *e;
			if (strcmp(ast_get_context_name(c), context) != 0)
				continue;
			/* got it, we must match here */
			e = NULL;
			while ( (e = ast_walk_context_extensions(c, e)) ) {
				struct ast_exten *priority;
				char buffer[10];

				if (strcmp(ast_get_extension_name(e), exten) != 0)
					continue;
				/* XXX lock e ? */
				priority = NULL;
				while ( !ret && (priority = ast_walk_extension_priorities(e, priority)) ) {
					snprintf(buffer, sizeof(buffer), "%u", ast_get_extension_priority(priority));
					if (partial_match(buffer, word, len) && ++which > state) /* n-th match */
						ret = strdup(buffer);
				}
				break;
			}
			break;
		}
		ast_unlock_contexts();
	error3:
		if (exten)
			free(exten);
#ifdef BROKEN_READLINE
		free(word2);
#endif
	}
	return ret; 
}

/*!
 * Include context ...
 */
static int handle_context_add_include(int fd, int argc, char *argv[])
{
	if (argc != 5) /* include context CTX in CTX */
		return RESULT_SHOWUSAGE;

	/* third arg must be 'in' ... */
	if (strcmp(argv[3], "in") && strcmp(argv[3], "into")) /* XXX why both ? */
		return RESULT_SHOWUSAGE;

	if (ast_context_add_include(argv[4], argv[2], registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(fd, "Out of memory for context addition\n");
			break;

		case EBUSY:
			ast_cli(fd, "Failed to lock context(s) list, please try again later\n");
			break;

		case EEXIST:
			ast_cli(fd, "Context '%s' already included in '%s' context\n",
				argv[2], argv[4]);
			break;

		case ENOENT:
		case EINVAL:
			ast_cli(fd, "There is no existence of context '%s'\n",
				errno == ENOENT ? argv[4] : argv[2]);
			break;

		default:
			ast_cli(fd, "Failed to include '%s' in '%s' context\n",
				argv[2], argv[4]);
			break;
		}
		return RESULT_FAILURE;
	}

	/* show some info ... */
	ast_cli(fd, "Context '%s' included in '%s' context\n",
		argv[2], argv[4]);

	return RESULT_SUCCESS;
}

static char *complete_context_add_include(const char *line, const char *word, int pos,
    int state)
{
	struct ast_context *c;
	int which = 0;
	char *ret = NULL;
	int len = strlen(word);

	if (pos == 2) {		/* 'include context _X_' (context) ... */
		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}
		for (c = NULL; !ret && (c = ast_walk_contexts(c)); )
			if (partial_match(ast_get_context_name(c), word, len) && ++which > state)
				ret = strdup(ast_get_context_name(c));
		ast_unlock_contexts();
		return ret;
	} else if (pos == 3) { /* include context CTX _X_ */
		/* complete  as 'in' if context exists or we are unable to check */
		char *context, *dupline;
		struct ast_context *c;
		const char *s = skip_words(line, 2);	/* should not fail */

		if (state != 0)	/* only once */
			return NULL;

		/* parse context from line ... */
		context = dupline = strdup(s);
		if (!context) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return strdup("in");
		}
		strsep(&dupline, " ");

		/* check for context existence ... */
		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			/* our fault, we can't check, so complete 'in' ... */
			ret = strdup("in");
		} else {
			for (c = NULL; !ret && (c = ast_walk_contexts(c)); )
				if (!strcmp(context, ast_get_context_name(c)))
					ret = strdup("in"); /* found */
			ast_unlock_contexts();
		}
		free(context);
		return ret;
	} else if (pos == 4) { /* 'include context CTX in _X_' (dst context) */
		char *context, *dupline, *in;
		const char *s = skip_words(line, 2); /* should not fail */
		context = dupline = strdup(s);
		if (!dupline) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}
		strsep(&dupline, " "); /* skip context */
		in = strsep(&dupline, " ");
		/* error if missing context or third word is not 'in' */
		if (!strlen(context) || strcmp(in, "in")) {
			ast_log(LOG_ERROR, "bad context %s or missing in %s\n",
				context, in);
			goto error3;
		}

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			goto error3;
		}

		for (c = NULL; (c = ast_walk_contexts(c)); )
			if (!strcmp(context, ast_get_context_name(c)))
				break;
		if (c) { /* first context exists, go on... */
			/* go through all contexts ... */
			for (c = NULL; !ret && (c = ast_walk_contexts(c)); ) {
				if (!strcmp(context, ast_get_context_name(c)))
					continue; /* skip ourselves */
				if (partial_match(ast_get_context_name(c), word, len) &&
						!lookup_ci(c, context) /* not included yet */ &&
						++which > state)
					ret = strdup(ast_get_context_name(c));
			}
		} else {
			ast_log(LOG_ERROR, "context %s not found\n", context);
		}
		ast_unlock_contexts();
	error3:
		free(context);
		return ret;
	}

	return NULL;
}

/*!
 * \brief 'save dialplan' CLI command implementation functions ...
 */
static int handle_save_dialplan(int fd, int argc, char *argv[])
{
	char filename[256];
	struct ast_context *c;
	struct ast_config *cfg;
	struct ast_variable *v;
	int incomplete = 0; /* incomplete config write? */
	FILE *output;

	const char *base, *slash, *file;

	if (! (static_config && !write_protect_config)) {
		ast_cli(fd,
			"I can't save dialplan now, see '%s' example file.\n",
			config);
		return RESULT_FAILURE;
	}

	if (argc != 2 && argc != 3)
		return RESULT_SHOWUSAGE;

	if (ast_mutex_lock(&save_dialplan_lock)) {
		ast_cli(fd,
			"Failed to lock dialplan saving (another proccess saving?)\n");
		return RESULT_FAILURE;
	}
	/* XXX the code here is quite loose, a pathname with .conf in it
	 * is assumed to be a complete pathname
	 */
	if (argc == 3) {	/* have config path. Look for *.conf */
		base = argv[2];
		if (!strstr(argv[2], ".conf")) { /*no, this is assumed to be a pathname */
			/* if filename ends with '/', do not add one */
			slash = (*(argv[2] + strlen(argv[2]) -1) == '/') ? "/" : "";
			file = config;	/* default: 'extensions.conf' */
		} else {	/* yes, complete file name */
			slash = "";
			file = "";
		}
	} else {
		/* no config file, default one */
		base = ast_config_AST_CONFIG_DIR;
		slash = "/";
		file = config;
	}
	snprintf(filename, sizeof(filename), "%s%s%s", base, slash, config);

	cfg = ast_config_load("extensions.conf");

	/* try to lock contexts list */
	if (ast_lock_contexts()) {
		ast_cli(fd, "Failed to lock contexts list\n");
		ast_mutex_unlock(&save_dialplan_lock);
		ast_config_destroy(cfg);
		return RESULT_FAILURE;
	}

	/* create new file ... */
	if (!(output = fopen(filename, "wt"))) {
		ast_cli(fd, "Failed to create file '%s'\n",
			filename);
		ast_unlock_contexts();
		ast_mutex_unlock(&save_dialplan_lock);
		ast_config_destroy(cfg);
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

	ast_config_destroy(cfg);
	
#define PUT_CTX_HDR	do { \
	if (!context_header_written) {	\
		fprintf(output, "[%s]\n", ast_get_context_name(c));	\
		context_header_written = 1;	\
	}	\
	} while (0)

	/* walk all contexts */
	for (c = NULL; (c = ast_walk_contexts(c)); ) {
		int context_header_written = 0;
		struct ast_exten *e, *last_written_e = NULL;
		struct ast_include *i;
		struct ast_ignorepat *ip;
		struct ast_sw *sw;

		/* try to lock context and fireout all info */	
		if (ast_lock_context(c)) { /* lock failure */
			incomplete = 1;
			continue;
		}
		/* registered by this module? */
		/* XXX do we need this ? */
		if (!strcmp(ast_get_context_registrar(c), registrar)) {
			fprintf(output, "[%s]\n", ast_get_context_name(c));
			context_header_written = 1;
		}

		/* walk extensions ... */
		for (e = NULL; (e = ast_walk_context_extensions(c, e)); ) {
			struct ast_exten *p = NULL;

			/* fireout priorities */
			while ( (p = ast_walk_extension_priorities(e, p)) ) {
				if (strcmp(ast_get_extension_registrar(p), registrar) != 0) /* not this source */
					continue;
		
				/* make empty line between different extensions */	
				if (last_written_e != NULL &&
					    strcmp(ast_get_extension_name(last_written_e),
						    ast_get_extension_name(p)))
					fprintf(output, "\n");
				last_written_e = p;
			
				PUT_CTX_HDR;

				if (ast_get_extension_priority(p)==PRIORITY_HINT) { /* easy */
					fprintf(output, "exten => %s,hint,%s\n",
						    ast_get_extension_name(p),
						    ast_get_extension_app(p));
				} else { /* copy and replace '|' with ',' */
					const char *sep, *cid;
					char *tempdata = strdup(ast_get_extension_app_data(p));
					char *s;

					if (!tempdata) { /* XXX error duplicating string ? */
						incomplete = 1;
						continue;
					}
					for (s = tempdata; *s; s++)
						if (*s == '|')
							*s = ',';
					if (ast_get_extension_matchcid(p)) {
						sep = "/";
						cid = ast_get_extension_cidmatch(p);
					} else {
						sep = cid = "";
					}
					fprintf(output, "exten => %s%s%s,%d,%s(%s)\n",
					    ast_get_extension_name(p), sep, cid,
					    ast_get_extension_priority(p),
					    ast_get_extension_app(p), tempdata);
					free(tempdata);
				}
			}
		}

		/* written any extensions? ok, write space between exten & inc */
		if (last_written_e)
			fprintf(output, "\n");

		/* walk through includes */
		for (i = NULL; (i = ast_walk_context_includes(c, i)) ; ) {
			if (strcmp(ast_get_include_registrar(i), registrar) != 0)
				continue; /* not mine */
			PUT_CTX_HDR;
			fprintf(output, "include => %s\n", ast_get_include_name(i));
		}
		if (ast_walk_context_includes(c, NULL))
			fprintf(output, "\n");

		/* walk through switches */
		for (sw = NULL; (sw = ast_walk_context_switches(c, sw)) ; ) {
			if (strcmp(ast_get_switch_registrar(sw), registrar) != 0)
				continue; /* not mine */
			PUT_CTX_HDR;
			fprintf(output, "switch => %s/%s\n",
				    ast_get_switch_name(sw), ast_get_switch_data(sw));
		}

		if (ast_walk_context_switches(c, NULL))
			fprintf(output, "\n");

		/* fireout ignorepats ... */
		for (ip = NULL; (ip = ast_walk_context_ignorepats(c, ip)); ) {
			if (strcmp(ast_get_ignorepat_registrar(ip), registrar) != 0)
				continue; /* not mine */
			PUT_CTX_HDR;
			fprintf(output, "ignorepat => %s\n",
						ast_get_ignorepat_name(ip));
		}

		ast_unlock_context(c);
	}	

	ast_unlock_contexts();
	ast_mutex_unlock(&save_dialplan_lock);
	fclose(output);

	if (incomplete) {
		ast_cli(fd, "Saved dialplan is incomplete\n");
		return RESULT_FAILURE;
	}

	ast_cli(fd, "Dialplan successfully saved into '%s'\n",
		filename);
	return RESULT_SUCCESS;
}

/*!
 * \brief ADD EXTENSION command stuff
 */
static int handle_context_add_extension(int fd, int argc, char *argv[])
{
	char *whole_exten;
	char *exten, *prior;
	int iprior = -2;
	char *cidmatch, *app, *app_data;
	char *start, *end;

	/* check for arguments at first */
	if (argc != 5 && argc != 6)
		return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into"))
		return RESULT_SHOWUSAGE;
	if (argc == 6) if (strcmp(argv[5], "replace")) return RESULT_SHOWUSAGE;

	/* XXX overwrite argv[2] */
	whole_exten = argv[2];
	exten 	= strsep(&whole_exten,",");
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
			if (sscanf(prior, "%d", &iprior) != 1) {
				ast_cli(fd, "'%s' is not a valid priority\n", prior);
				prior = NULL;
			}
		}
	}
	app = whole_exten;
	if (app && (start = strchr(app, '(')) && (end = strrchr(app, ')'))) {
		*start = *end = '\0';
		app_data = start + 1;
		ast_process_quotes_and_slashes(app_data, ',', '|');
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

	if (!exten || !prior || !app || (!app_data && iprior != PRIORITY_HINT))
		return RESULT_SHOWUSAGE;

	if (!app_data)
		app_data="";
	if (ast_add_extension(argv[4], argc == 6 ? 1 : 0, exten, iprior, NULL, cidmatch, app,
		(void *)strdup(app_data), free, registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(fd, "Out of free memory\n");
			break;

		case EBUSY:
			ast_cli(fd, "Failed to lock context(s) list, please try again later\n");
			break;

		case ENOENT:
			ast_cli(fd, "No existence of '%s' context\n", argv[4]);
			break;

		case EEXIST:
			ast_cli(fd, "Extension %s@%s with priority %s already exists\n",
				exten, argv[4], prior);
			break;

		default:
			ast_cli(fd, "Failed to add '%s,%s,%s,%s' extension into '%s' context\n",
					exten, prior, app, app_data, argv[4]);
			break;
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

/*! add extension 6123,1,Dial,IAX/212.71.138.13/6123 into local */
static char *complete_context_add_extension(const char *line, const char *word,
	int pos, int state)
{
	int which = 0;

	if (pos == 3) {		/* complete 'into' word ... */
		return (state == 0) ? strdup("into") : NULL;
	} else if (pos == 4) { /* complete context */
		struct ast_context *c = NULL;
		int len = strlen(word);
		char *res = NULL;

		/* try to lock contexts list ... */
		if (ast_lock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		/* walk through all contexts */
		while ( !res && (c = ast_walk_contexts(c)) )
			if (partial_match(ast_get_context_name(c), word, len) && ++which > state)
				res = strdup(ast_get_context_name(c));
		ast_unlock_contexts();
		return res;
	} else if (pos == 5) {
		return state == 0 ? strdup("replace") : NULL;
	}
	return NULL;
}

/*!
 * IGNOREPAT CLI stuff
 */
static int handle_context_add_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 5)
		return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "into"))
		return RESULT_SHOWUSAGE;

	if (ast_context_add_ignorepat(argv[4], argv[2], registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(fd, "Out of free memory\n");
			break;

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

static char *complete_context_add_ignorepat(const char *line, const char *word,
	int pos, int state)
{
	if (pos == 3)
		return state == 0 ? strdup("into") : NULL;
	else if (pos == 4) {
		struct ast_context *c;
		int which = 0;
		char *dupline, *ignorepat = NULL;
		const char *s;
		char *ret = NULL;
		int len = strlen(word);

		/* XXX skip first two words 'add' 'ignorepat' */
		s = skip_words(line, 2);
		if (s == NULL)
			return NULL;
		dupline = strdup(s);
		if (!dupline) {
			ast_log(LOG_ERROR, "Malloc failure\n");
			return NULL;
		}
		ignorepat = strsep(&dupline, " ");

		if (ast_lock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock contexts list\n");
			return NULL;
		}

		for (c = NULL; !ret && (c = ast_walk_contexts(c));) {
			int found = 0;

			if (!partial_match(ast_get_context_name(c), word, len))
				continue; /* not mine */
			if (ignorepat) /* there must be one, right ? */
				found = lookup_c_ip(c, ignorepat);
			if (!found && ++which > state)
				ret = strdup(ast_get_context_name(c));
		}

		if (ignorepat)
			free(ignorepat);
		ast_unlock_contexts();
		return ret;
	}

	return NULL;
}

static int handle_context_remove_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 5)
		return RESULT_SHOWUSAGE;
	if (strcmp(argv[3], "from"))
		return RESULT_SHOWUSAGE;

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
	if (argc!=2)
		return RESULT_SHOWUSAGE;
	pbx_load_module();
	return RESULT_SUCCESS;
}

static char *complete_context_remove_ignorepat(const char *line, const char *word,
	int pos, int state)
{
	struct ast_context *c;
	int which = 0;
	char *ret = NULL;

	if (pos == 2) {
		int len = strlen(word);
		if (ast_lock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		for (c = NULL; !ret && (c = ast_walk_contexts(c));) {
			struct ast_ignorepat *ip;

			if (ast_lock_context(c))	/* error, skip it */
				continue;
			
			for (ip = NULL; !ret && (ip = ast_walk_context_ignorepats(c, ip));) {
				if (partial_match(ast_get_ignorepat_name(ip), word, len) && ++which > state) {
					/* n-th match */
					struct ast_context *cw = NULL;
					int found = 0;
					while ( (cw = ast_walk_contexts(cw)) && cw != c && !found) {
						/* XXX do i stop on c, or skip it ? */
						found = lookup_c_ip(cw, ast_get_ignorepat_name(ip));
					}
					if (!found)
						ret = strdup(ast_get_ignorepat_name(ip));
				}
			}
			ast_unlock_context(c);
		}
		ast_unlock_contexts();
		return ret;
	} else if (pos == 3) {
		 return state == 0 ? strdup("from") : NULL;
	} else if (pos == 4) { /* XXX check this */
		char *dupline, *duplinet, *ignorepat;
		int len = strlen(word);

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

		for (c = NULL; !ret && (c = ast_walk_contexts(c)); ) {
			if (ast_lock_context(c))	/* fail, skip it */
				continue;
			if (!partial_match(ast_get_context_name(c), word, len))
				continue;
			if (lookup_c_ip(c, ignorepat) && ++which > state)
				ret = strdup(ast_get_context_name(c));
			ast_unlock_context(c);
		}
		ast_unlock_contexts();
		free(dupline);
		return NULL;
	}

	return NULL;
}

/*!
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

/*!
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
	char *end;
	char *label;
	char realvalue[256];
	int lastpri = -2;
	struct ast_context *con;

	cfg = ast_config_load(config);
	if (cfg) {
		struct ast_variable *v;
		char *cxt;

		/* Use existing config to populate the PBX table */
		static_config = ast_true(ast_variable_retrieve(cfg, "general", "static"));
		write_protect_config = ast_true(ast_variable_retrieve(cfg, "general", "writeprotect"));
		autofallthrough_config = ast_true(ast_variable_retrieve(cfg, "general", "autofallthrough"));
		clearglobalvars_config = ast_true(ast_variable_retrieve(cfg, "general", "clearglobalvars"));
		ast_set2_flag(&ast_options, !ast_false(ast_variable_retrieve(cfg, "general", "priorityjumping")), AST_OPT_FLAG_PRIORITY_JUMPING);
									    
		for (v = ast_variable_browse(cfg, "globals"); v; v = v->next) {
			memset(realvalue, 0, sizeof(realvalue));
			pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
			pbx_builtin_setvar_helper(NULL, v->name, realvalue);
		}
		for (cxt = NULL; (cxt = ast_category_browse(cfg, cxt)); ) {

			/* All categories but "general" or "globals" are considered contexts */
			if (!strcasecmp(cxt, "general") || !strcasecmp(cxt, "globals"))
				continue;
			con=ast_context_create(&local_contexts,cxt, registrar);
			if (con == NULL)
				continue;

				/* XXX indentation should be fixed for this block */
				for (v = ast_variable_browse(cfg, cxt); v; v = v->next) {
					if (!strcasecmp(v->name, "exten")) {
						char *ext, *pri, *appl, *data, *cidmatch;
						char *stringp=NULL;
						int ipri = -2;
						char realext[256]="";
						char *plus, *firstp, *firstc;
						char *tc = strdup(v->value);
						if (tc == NULL)
							fprintf(stderr,"Error strdup returned NULL in %s\n",__PRETTY_FUNCTION__);
						else {
							stringp=tc;
							ext = strsep(&stringp, ",");
							if (!ext)
								ext="";
							pbx_substitute_variables_helper(NULL, ext, realext, sizeof(realext) - 1);
							cidmatch = strchr(realext, '/');
							if (cidmatch) {
								*cidmatch++ = '\0';
								ast_shrink_phone_number(cidmatch);
							}
							pri = strsep(&stringp, ",");
							if (!pri)
								pri="";
							label = strchr(pri, '(');
							if (label) {
								*label++ = '\0';
								end = strchr(label, ')');
								if (end)
									*end = '\0';
								else
									ast_log(LOG_WARNING, "Label missing trailing ')' at line %d\n", v->lineno);
							}
							plus = strchr(pri, '+');
							if (plus)
								*plus++ = '\0';
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
								if (sscanf(pri, "%d", &ipri) != 1) {
									if ((ipri = ast_findlabel_extension2(NULL, con, realext, pri, cidmatch)) < 1) {
										ast_log(LOG_WARNING, "Invalid priority/label '%s' at line %d\n", pri, v->lineno);
										ipri = 0;
									}
								}
							}
							appl = stringp;
							if (!appl)
								appl="";
							/* Find the first occurrence of either '(' or ',' */
							firstc = strchr(appl, ',');
							firstp = strchr(appl, '(');
							if (firstc && ((!firstp) || (firstc < firstp))) {
								/* comma found, no parenthesis */
								/* or both found, but comma found first */
								appl = strsep(&stringp, ",");
								data = stringp;
							} else if ((!firstc) && (!firstp)) {
								/* Neither found */
								data = "";
							} else {
								/* Final remaining case is parenthesis found first */
								appl = strsep(&stringp, "(");
								data = stringp;
								end = strrchr(data, ')');
								if ((end = strrchr(data, ')'))) {
									*end = '\0';
								} else {
									ast_log(LOG_WARNING, "No closing parenthesis found? '%s(%s'\n", appl, data);
								}
								ast_process_quotes_and_slashes(data, ',', '|');
							}

							if (!data)
								data="";
							appl = ast_skip_blanks(appl);
							if (ipri) {
								if (plus)
									ipri += atoi(plus);
								lastpri = ipri;
								if (!ast_opt_dont_warn) {
									if (!strcmp(realext, "_."))
										ast_log(LOG_WARNING, "The use of '_.' for an extension is strongly discouraged and can have unexpected behavior.  Please use '_X.' instead at line %d\n", v->lineno);
								}
								if (ast_add_extension2(con, 0, realext, ipri, label, cidmatch, appl, strdup(data), free, registrar)) {
									ast_log(LOG_WARNING, "Unable to register extension at line %d\n", v->lineno);
								}
							}
							free(tc);
						}
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
						char *stringp= realvalue;
						char *appl, *data;

						memset(realvalue, 0, sizeof(realvalue));
						if (!strcasecmp(v->name, "switch"))
							pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
						else
							ast_copy_string(realvalue, v->value, sizeof(realvalue));
						appl = strsep(&stringp, "/");
						data = strsep(&stringp, ""); /* XXX what for ? */
						if (!data)
							data = "";
						if (ast_context_add_switch2(con, appl, data, !strcasecmp(v->name, "eswitch"), registrar))
							ast_log(LOG_WARNING, "Unable to include switch '%s' in context '%s'\n", v->value, cxt);
					}
				}
		}
		ast_config_destroy(cfg);
	}
	ast_merge_contexts_and_delete(&local_contexts,registrar);

	for (con = NULL; (con = ast_walk_contexts(con));)
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
	if (clearglobalvars_config)
		pbx_builtin_clear_globals();
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
