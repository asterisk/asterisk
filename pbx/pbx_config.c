/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <ctype.h>

#include "asterisk/paths.h"	/* ast_config_AST_CONFIG_DIR */
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/channel.h"	/* AST_MAX_EXTENSION */
#include "asterisk/callerid.h"

static const char config[] = "extensions.conf";
static const char registrar[] = "pbx_config";
static char userscontext[AST_MAX_EXTENSION] = "default";

static int static_config = 0;
static int write_protect_config = 1;
static int autofallthrough_config = 1;
static int clearglobalvars_config = 0;
static int extenpatternmatchnew_config = 0;
static char *overrideswitch_config = NULL;

AST_MUTEX_DEFINE_STATIC(save_dialplan_lock);

AST_MUTEX_DEFINE_STATIC(reload_lock);

static struct ast_context *local_contexts = NULL;
static struct ast_hashtab *local_table = NULL;
/*
 * Prototypes for our completion functions
 */
static char *complete_dialplan_remove_include(struct ast_cli_args *);
static char *complete_dialplan_add_include(struct ast_cli_args *);
static char *complete_dialplan_remove_ignorepat(struct ast_cli_args *);
static char *complete_dialplan_add_ignorepat(struct ast_cli_args *);
static char *complete_dialplan_remove_extension(struct ast_cli_args *);
static char *complete_dialplan_add_extension(struct ast_cli_args *);
static char *complete_dialplan_remove_context(struct ast_cli_args *);

/*
 * Implementation of functions provided by this module
 */

/*!
 * * REMOVE context command stuff
 */

static char *handle_cli_dialplan_remove_context(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_context *con;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan remove context";
		e->usage =
			"Usage: dialplan remove context <context>\n"
			"       Removes all extensions from a specified context.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_dialplan_remove_context(a);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	con = ast_context_find(a->argv[3]);

	if (!con) {
		ast_cli(a->fd, "There is no such context as '%s'\n",
                        a->argv[3]);
                return CLI_SUCCESS;
	} else {
		ast_context_destroy(con, registrar);
		ast_cli(a->fd, "Removing context '%s'\n",
			a->argv[3]);
		return CLI_SUCCESS;
	}
}
/*!
 * REMOVE INCLUDE command stuff
 */
static char *handle_cli_dialplan_remove_include(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan remove include";
		e->usage =
			"Usage: dialplan remove include <context> from <context>\n"
			"       Remove an included context from another context.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_dialplan_remove_include(a);
	}

	if (a->argc != 6 || strcmp(a->argv[4], "from"))
		return CLI_SHOWUSAGE;

	if (!ast_context_remove_include(a->argv[5], a->argv[3], registrar)) {
		ast_cli(a->fd, "We are not including '%s' into '%s' now\n",
			a->argv[3], a->argv[5]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Failed to remove '%s' include from '%s' context\n",
		a->argv[3], a->argv[5]);
	return CLI_FAILURE;
}

/*! \brief return true if 'name' is included by context c */
static int lookup_ci(struct ast_context *c, const char *name)
{
	struct ast_include *i = NULL;

	if (ast_rdlock_context(c))	/* error, skip */
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

	if (ast_rdlock_context(c))	/* error, skip */
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

/*! \brief split extension\@context in two parts, return -1 on error.
 * The return string is malloc'ed and pointed by *ext
 */
static int split_ec(const char *src, char **ext, char ** const ctx, char ** const cid)
{
	char *i, *c, *e = ast_strdup(src); /* now src is not used anymore */

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
	if (cid && (i = strchr(e, '/'))) {
		*i++ = '\0';
		*cid = i;
	} else if (cid) {
		/* Signal none detected */
		*cid = NULL;
	}
	return 0;
}

/* _X_ is the string we need to complete */
static char *complete_dialplan_remove_include(struct ast_cli_args *a)
{
	int which = 0;
	char *res = NULL;
	int len = strlen(a->word); /* how many bytes to match */
	struct ast_context *c = NULL;

	if (a->pos == 3) {		/* "dialplan remove include _X_" */
		if (ast_wrlock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}
		/* walk contexts and their includes, return the n-th match */
		while (!res && (c = ast_walk_contexts(c))) {
			struct ast_include *i = NULL;

			if (ast_rdlock_context(c))	/* error ? skip this one */
				continue;

			while ( !res && (i = ast_walk_context_includes(c, i)) ) {
				const char *i_name = ast_get_include_name(i);
				struct ast_context *nc = NULL;
				int already_served = 0;

				if (!partial_match(i_name, a->word, len))
					continue;	/* not matched */

				/* check if this include is already served or not */

				/* go through all contexts again till we reach actual
				 * context or already_served = 1
				 */
				while ( (nc = ast_walk_contexts(nc)) && nc != c && !already_served)
					already_served = lookup_ci(nc, i_name);

				if (!already_served && ++which > a->n)
					res = strdup(i_name);
			}
			ast_unlock_context(c);
		}

		ast_unlock_contexts();
		return res;
	} else if (a->pos == 4) { /* "dialplan remove include CTX _X_" */
		/*
		 * complete as 'from', but only if previous context is really
		 * included somewhere
		 */
		char *context, *dupline;
		const char *s = skip_words(a->line, 3); /* skip 'dialplan' 'remove' 'include' */

		if (a->n > 0)
			return NULL;
		context = dupline = strdup(s);
		if (!dupline) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}
		strsep(&dupline, " ");

		if (ast_rdlock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock contexts list\n");
			free(context);
			return NULL;
		}

		/* go through all contexts and check if is included ... */
		while (!res && (c = ast_walk_contexts(c)))
			if (lookup_ci(c, context)) /* context is really included, complete "from" command */
				res = strdup("from");
		ast_unlock_contexts();
		if (!res)
			ast_log(LOG_WARNING, "%s not included anywhere\n", context);
		free(context);
		return res;
	} else if (a->pos == 5) { /* "dialplan remove include CTX from _X_" */
		/*
		 * Context from which we removing include ... 
		 */
		char *context, *dupline, *from;
		const char *s = skip_words(a->line, 3); /* skip 'dialplan' 'remove' 'include' */
		context = dupline = strdup(s);
		if (!dupline) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		strsep(&dupline, " "); /* skip context */

		/* fourth word must be 'from' */
		from = strsep(&dupline, " ");
		if (!from || strcmp(from, "from")) {
			free(context);
			return NULL;
		}

		if (ast_rdlock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			free(context);
			return NULL;
		}

		/* walk through all contexts ... */
		c = NULL;
		while ( !res && (c = ast_walk_contexts(c))) {
			const char *c_name = ast_get_context_name(c);
			if (!partial_match(c_name, a->word, len))	/* not a good target */
				continue;
			/* walk through all includes and check if it is our context */	
			if (lookup_ci(c, context) && ++which > a->n)
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
static char *handle_cli_dialplan_remove_extension(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int removing_priority = 0;
	char *exten, *context, *cid;
	char *ret = CLI_FAILURE;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan remove extension";
		e->usage =
			"Usage: dialplan remove extension exten[/cid]@context [priority]\n"
			"       Remove an extension from a given context. If a priority\n"
			"       is given, only that specific priority from the given extension\n"
			"       will be removed.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_dialplan_remove_extension(a);
	}

	if (a->argc != 5 && a->argc != 4)
		return CLI_SHOWUSAGE;

	/*
	 * Priority input checking ...
	 */
	if (a->argc == 5) {
		const char *c = a->argv[4];

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
				ast_cli(a->fd, "Invalid priority '%s'\n", a->argv[4]);
				return CLI_FAILURE;
			}
			removing_priority = atoi(a->argv[4]);
		}

		if (removing_priority == 0) {
			ast_cli(a->fd, "If you want to remove whole extension, please " \
				"omit priority argument\n");
			return CLI_FAILURE;
		}
	}

	/* XXX original overwrote argv[3] */
	/*
	 * Format exten@context checking ...
	 */
	if (split_ec(a->argv[3], &exten, &context, &cid))
		return CLI_FAILURE; /* XXX malloc failure */
	if ((!strlen(exten)) || (!(strlen(context)))) {
		ast_cli(a->fd, "Missing extension or context name in third argument '%s'\n",
			a->argv[3]);
		free(exten);
		return CLI_FAILURE;
	}

	if (!ast_context_remove_extension_callerid(context, exten, removing_priority,
			/* Do NOT substitute S_OR; it is NOT the same thing */
			cid ? cid : (removing_priority ? "" : NULL), cid ? 1 : 0, registrar)) {
		if (!removing_priority)
			ast_cli(a->fd, "Whole extension %s@%s removed\n",
				exten, context);
		else
			ast_cli(a->fd, "Extension %s@%s with priority %d removed\n",
				exten, context, removing_priority);
			
		ret = CLI_SUCCESS;
	} else {
		if (cid) {
			ast_cli(a->fd, "Failed to remove extension %s/%s@%s\n", exten, cid, context);
		} else {
			ast_cli(a->fd, "Failed to remove extension %s@%s\n", exten, context);
		}
		ret = CLI_FAILURE;
	}
	free(exten);
	return ret;
}

static char *complete_dialplan_remove_extension(struct ast_cli_args *a)
{
	char *ret = NULL;
	int which = 0;

	if (a->pos == 3) { /* 'dialplan remove extension _X_' (exten@context ... */
		struct ast_context *c = NULL;
		char *context = NULL, *exten = NULL, *cid = NULL;
		int le = 0;	/* length of extension */
		int lc = 0;	/* length of context */
		int lcid = 0; /* length of cid */

		lc = split_ec(a->word, &exten, &context, &cid);
		if (lc)	{ /* error */
			return NULL;
		}
		le = strlen(exten);
		lc = strlen(context);
		lcid = cid ? strlen(cid) : -1;

		if (ast_rdlock_contexts()) {
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
				if ( !strchr(a->word, '/') ||
						(!strchr(a->word, '@') && partial_match(ast_get_extension_cidmatch(e), cid, lcid)) ||
						(strchr(a->word, '@') && !strcmp(ast_get_extension_cidmatch(e), cid))) {
					if ( ((strchr(a->word, '/') || strchr(a->word, '@')) && !strcmp(ast_get_extension_name(e), exten)) ||
						 (!strchr(a->word, '/') && !strchr(a->word, '@') && partial_match(ast_get_extension_name(e), exten, le))) { /* n-th match */
						if (++which > a->n) {
							/* If there is an extension then return exten@context. */
							if (ast_get_extension_matchcid(e) && (!strchr(a->word, '@') || strchr(a->word, '/'))) {
								if (ast_asprintf(&ret, "%s/%s@%s", ast_get_extension_name(e), ast_get_extension_cidmatch(e), ast_get_context_name(c)) < 0) {
									ret = NULL;
								}
								break;
							} else if (!ast_get_extension_matchcid(e) && !strchr(a->word, '/')) {
								if (ast_asprintf(&ret, "%s@%s", ast_get_extension_name(e), ast_get_context_name(c)) < 0) {
									ret = NULL;
								}
								break;
							}
						}
					}
				}
			}
			if (e)	/* got a match */
				break;
		}

		ast_unlock_contexts();
	error2:
		free(exten);
	} else if (a->pos == 4) { /* 'dialplan remove extension EXT _X_' (priority) */
		char *exten = NULL, *context, *cid, *p;
		struct ast_context *c;
		int le, lc, len;
		const char *s = skip_words(a->line, 3); /* skip 'dialplan' 'remove' 'extension' */
		int i = split_ec(s, &exten, &context, &cid);	/* parse ext@context */

		if (i)	/* error */
			goto error3;
		if ( (p = strchr(exten, ' ')) ) /* remove space after extension */
			*p = '\0';
		if ( (p = strchr(context, ' ')) ) /* remove space after context */
			*p = '\0';
		le = strlen(exten);
		lc = strlen(context);
		len = strlen(a->word);
		if (le == 0 || lc == 0)
			goto error3;

		if (ast_rdlock_contexts()) {
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

				if (cid && strcmp(ast_get_extension_cidmatch(e), cid) != 0) {
					continue;
				}
				if (strcmp(ast_get_extension_name(e), exten) != 0)
					continue;
				/* XXX lock e ? */
				priority = NULL;
				while ( !ret && (priority = ast_walk_extension_priorities(e, priority)) ) {
					snprintf(buffer, sizeof(buffer), "%d", ast_get_extension_priority(priority));
					if (partial_match(buffer, a->word, len) && ++which > a->n) /* n-th match */
						ret = strdup(buffer);
				}
				break;
			}
			break;
		}
		ast_unlock_contexts();
	error3:
		free(exten);
	}
	return ret; 
}

/*!
 * Include context ...
 */
static char *handle_cli_dialplan_add_include(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	const char *into_context;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan add include";
		e->usage =
			"Usage: dialplan add include <context> into <context>\n"
			"       Include a context in another context.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_dialplan_add_include(a);
	}

	if (a->argc != 6) /* dialplan add include CTX in CTX */
		return CLI_SHOWUSAGE;

	/* fifth arg must be 'into' ... */
	if (strcmp(a->argv[4], "into"))
		return CLI_SHOWUSAGE;

	into_context = a->argv[5];

	if (!ast_context_find(into_context)) {
		ast_cli(a->fd, "Context '%s' did not exist prior to add include - the context will be created.\n", into_context);
	}

	if (!ast_context_find_or_create(NULL, NULL, into_context, registrar)) {
		ast_cli(a->fd, "ast_context_find_or_create() failed\n");
		ast_cli(a->fd, "Failed to include '%s' in '%s' context\n",a->argv[3], a->argv[5]);
		return CLI_FAILURE;
	}

	if (ast_context_add_include(a->argv[5], a->argv[3], registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(a->fd, "Out of memory for context addition\n");
			break;

		case EBUSY:
			ast_cli(a->fd, "Failed to lock context(s) list, please try again later\n");
			break;

		case EEXIST:
			ast_cli(a->fd, "Context '%s' already included in '%s' context\n",
				a->argv[3], a->argv[5]);
			break;

		case ENOENT:
		case EINVAL:
			ast_cli(a->fd, "There is no existence of context '%s'\n",
				errno == ENOENT ? a->argv[5] : a->argv[3]);
			break;

		default:
			ast_cli(a->fd, "Failed to include '%s' in '%s' context\n",
				a->argv[3], a->argv[5]);
			break;
		}
		return CLI_FAILURE;
	}

	/* show some info ... */
	ast_cli(a->fd, "Context '%s' included in '%s' context\n",
		a->argv[3], a->argv[5]);

	return CLI_SUCCESS;
}

static char *complete_dialplan_add_include(struct ast_cli_args *a)
{
	struct ast_context *c;
	int which = 0;
	char *ret = NULL;
	int len = strlen(a->word);

	if (a->pos == 3) {		/* 'dialplan add include _X_' (context) ... */
		if (ast_rdlock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}
		for (c = NULL; !ret && (c = ast_walk_contexts(c)); )
			if (partial_match(ast_get_context_name(c), a->word, len) && ++which > a->n)
				ret = strdup(ast_get_context_name(c));
		ast_unlock_contexts();
		return ret;
	} else if (a->pos == 4) { /* dialplan add include CTX _X_ */
		/* always complete  as 'into' */
		return (a->n == 0) ? strdup("into") : NULL;
	} else if (a->pos == 5) { /* 'dialplan add include CTX into _X_' (dst context) */
		char *context, *dupline, *into;
		const char *s = skip_words(a->line, 3); /* should not fail */
		context = dupline = strdup(s);

		if (!dupline) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return NULL;
		}

		strsep(&dupline, " "); /* skip context */
		into = strsep(&dupline, " ");
		/* error if missing context or fifth word is not 'into' */
		if (!strlen(context) || strcmp(into, "into")) {
			ast_log(LOG_ERROR, "bad context %s or missing into %s\n",
				context, into);
			goto error3;
		}

		if (ast_rdlock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			goto error3;
		}

		for (c = NULL; !ret && (c = ast_walk_contexts(c)); ) {
			if (!strcmp(context, ast_get_context_name(c)))
				continue; /* skip ourselves */
			if (partial_match(ast_get_context_name(c), a->word, len) &&
					!lookup_ci(c, context) /* not included yet */ &&
					++which > a->n) {
				ret = strdup(ast_get_context_name(c));
			}
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
static char *handle_cli_dialplan_save(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char filename[256], overrideswitch[256] = "";
	struct ast_context *c;
	struct ast_config *cfg;
	struct ast_variable *v;
	int incomplete = 0; /* incomplete config write? */
	FILE *output;
	struct ast_flags config_flags = { 0 };
	const char *base, *slash;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan save";
		e->usage =
			"Usage: dialplan save [/path/to/extension/file]\n"
			"       Save dialplan created by pbx_config module.\n"
			"\n"
			"Example: dialplan save                 (/etc/asterisk/extensions.conf)\n"
			"         dialplan save /home/markster  (/home/markster/extensions.conf)\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (! (static_config && !write_protect_config)) {
		ast_cli(a->fd,
			"I can't save dialplan now, see '%s' example file.\n",
			config);
		return CLI_FAILURE;
	}

	if (a->argc != 2 && a->argc != 3)
		return CLI_SHOWUSAGE;

	if (ast_mutex_lock(&save_dialplan_lock)) {
		ast_cli(a->fd,
			"Failed to lock dialplan saving (another proccess saving?)\n");
		return CLI_FAILURE;
	}
	/* XXX the code here is quite loose, a pathname with .conf in it
	 * is assumed to be a complete pathname
	 */
	if (a->argc == 3) {	/* have config path. Look for *.conf */
		base = a->argv[2];
		if (!strstr(a->argv[2], ".conf")) { /*no, this is assumed to be a pathname */
			/* if filename ends with '/', do not add one */
			slash = (*(a->argv[2] + strlen(a->argv[2]) -1) == '/') ? "/" : "";
		} else {	/* yes, complete file name */
			slash = "";
		}
	} else {
		/* no config file, default one */
		base = ast_config_AST_CONFIG_DIR;
		slash = "/";
	}
	snprintf(filename, sizeof(filename), "%s%s%s", base, slash, config);

	cfg = ast_config_load("extensions.conf", config_flags);
	if (!cfg) {
		ast_cli(a->fd, "Failed to load extensions.conf\n");
		ast_mutex_unlock(&save_dialplan_lock);
		return CLI_FAILURE;
	}

	/* try to lock contexts list */
	if (ast_rdlock_contexts()) {
		ast_cli(a->fd, "Failed to lock contexts list\n");
		ast_mutex_unlock(&save_dialplan_lock);
		ast_config_destroy(cfg);
		return CLI_FAILURE;
	}

	/* create new file ... */
	if (!(output = fopen(filename, "wt"))) {
		ast_cli(a->fd, "Failed to create file '%s'\n",
			filename);
		ast_unlock_contexts();
		ast_mutex_unlock(&save_dialplan_lock);
		ast_config_destroy(cfg);
		return CLI_FAILURE;
	}

	/* fireout general info */
	if (overrideswitch_config) {
		snprintf(overrideswitch, sizeof(overrideswitch), "overrideswitch=%s\n", overrideswitch_config);
	}
	fprintf(output, "[general]\nstatic=%s\nwriteprotect=%s\nautofallthrough=%s\nclearglobalvars=%s\n%sextenpatternmatchnew=%s\n\n",
		static_config ? "yes" : "no",
		write_protect_config ? "yes" : "no",
                autofallthrough_config ? "yes" : "no",
				clearglobalvars_config ? "yes" : "no",
				overrideswitch_config ? overrideswitch : "",
				extenpatternmatchnew_config ? "yes" : "no");

	if ((v = ast_variable_browse(cfg, "globals"))) {
		fprintf(output, "[globals]\n");
		while(v) {
			int escaped_len = 2 * strlen(v->value) + 1;
			char escaped[escaped_len];

			ast_escape_semicolons(v->value, escaped, escaped_len);
			fprintf(output, "%s => %s\n", v->name, escaped);
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
		struct ast_exten *ext, *last_written_e = NULL;
		struct ast_include *i;
		struct ast_ignorepat *ip;
		struct ast_sw *sw;

		/* try to lock context and fireout all info */	
		if (ast_rdlock_context(c)) { /* lock failure */
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
		for (ext = NULL; (ext = ast_walk_context_extensions(c, ext)); ) {
			struct ast_exten *p = NULL;

			/* fireout priorities */
			while ( (p = ast_walk_extension_priorities(ext, p)) ) {
				if (strcmp(ast_get_extension_registrar(p), registrar) != 0) /* not this source */
					continue;
		
				/* make empty line between different extensions */	
				if (last_written_e != NULL &&
					    strcmp(ast_get_extension_name(last_written_e),
						    ast_get_extension_name(p)))
					fprintf(output, "\n");
				last_written_e = p;
			
				PUT_CTX_HDR;

				if (ast_get_extension_priority(p) == PRIORITY_HINT) { /* easy */
					fprintf(output, "exten => %s,hint,%s\n",
						    ast_get_extension_name(p),
						    ast_get_extension_app(p));
				} else {
					const char *sep, *cid;
					const char *el = ast_get_extension_label(p);
					char label[128] = "";
					char *appdata = ast_get_extension_app_data(p);
					char *escaped;

					if (ast_get_extension_matchcid(p)) {
						sep = "/";
						cid = ast_get_extension_cidmatch(p);
					} else {
						sep = cid = "";
					}

					if (el && (snprintf(label, sizeof(label), "(%s)", el) != (strlen(el) + 2))) {
						incomplete = 1;	/* error encountered or label > 125 chars */
					}

					if (!ast_strlen_zero(appdata)) {
						int escaped_len = 2 * strlen(appdata) + 1;
						char escaped[escaped_len];

						ast_escape_semicolons(appdata, escaped, escaped_len);
					} else {
						escaped = "";
					}

					fprintf(output, "exten => %s%s%s,%d%s,%s(%s)\n",
					    ast_get_extension_name(p), (ast_strlen_zero(sep) ? "" : sep), (ast_strlen_zero(cid) ? "" : cid),
					    ast_get_extension_priority(p), label,
					    ast_get_extension_app(p), escaped);
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
		ast_cli(a->fd, "Saved dialplan is incomplete\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Dialplan successfully saved into '%s'\n",
		filename);
	return CLI_SUCCESS;
}

/*!
 * \brief ADD EXTENSION command stuff
 */
static char *handle_cli_dialplan_add_extension(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *whole_exten;
	char *exten, *prior;
	int iprior = -2;
	char *cidmatch, *app, *app_data;
	char *start, *end;
	const char *into_context;

	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan add extension";
		e->usage =
			"Usage: dialplan add extension <exten>,<priority>,<app> into <context> [replace]\n"
			"\n"
			"       app can be either:\n"
			"         app-name\n"
			"         app-name(app-data)\n"
			"         app-name,<app-data>\n"
			"\n"
			"       This command will add the new extension into <context>.  If\n"
			"       an extension with the same priority already exists and the\n"
			"       'replace' option is given we will replace the extension.\n"
			"\n"
			"Example: dialplan add extension 6123,1,Dial,IAX/216.207.245.56/6123 into local\n"
			"         Now, you can dial 6123 and talk to Markster :)\n";
		return NULL;
	case CLI_GENERATE:
		return complete_dialplan_add_extension(a);
	}

	/* check for arguments at first */
	if (a->argc != 6 && a->argc != 7)
		return CLI_SHOWUSAGE;
	if (strcmp(a->argv[4], "into"))
		return CLI_SHOWUSAGE;
	if (a->argc == 7)
		if (strcmp(a->argv[6], "replace"))
			return CLI_SHOWUSAGE;

	whole_exten = ast_strdupa(a->argv[3]);
	exten = strsep(&whole_exten,",");
	if (strchr(exten, '/')) {
		cidmatch = exten;
		strsep(&cidmatch,"/");
	} else {
		cidmatch = NULL;
	}
	prior = strsep(&whole_exten,",");
	if (prior) {
		if (!strcmp(prior, "hint")) {
			iprior = PRIORITY_HINT;
		} else {
			if (sscanf(prior, "%30d", &iprior) != 1) {
				ast_cli(a->fd, "'%s' is not a valid priority\n", prior);
				prior = NULL;
			}
		}
	}
	app = whole_exten;
	if (app) {
		if ((start = strchr(app, '(')) && (end = strrchr(app, ')'))) {
			*start = *end = '\0';
			app_data = start + 1;
		} else {
			app_data = strchr(app, ',');
			if (app_data) {
				*app_data++ = '\0';
			}
		}
	} else {
		app_data = NULL;
	}

	if (!exten || !prior || !app) {
		return CLI_SHOWUSAGE;
	}

	if (!app_data) {
		app_data = "";
	}
	into_context = a->argv[5];

	if (!ast_context_find(into_context)) {
		ast_cli(a->fd, "Context '%s' did not exist prior to add extension - the context will be created.\n", into_context);
	}

	if (!ast_context_find_or_create(NULL, NULL, into_context, registrar)) {
		ast_cli(a->fd, "Failed to add '%s,%s,%s(%s)' extension into '%s' context\n",
			exten, prior, app, app_data, into_context);
		return CLI_FAILURE;
	}

	if (ast_add_extension(into_context, a->argc == 7 ? 1 : 0, exten, iprior, NULL, cidmatch, app,
		ast_strdup(app_data), ast_free_ptr, registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(a->fd, "Out of free memory\n");
			break;

		case EBUSY:
			ast_cli(a->fd, "Failed to lock context(s) list, please try again later\n");
			break;

		case ENOENT:
			ast_cli(a->fd, "No existence of '%s' context\n", into_context);
			break;

		case EEXIST:
			ast_cli(a->fd, "Extension %s@%s with priority %s already exists\n",
				exten, into_context, prior);
			break;

		default:
			ast_cli(a->fd, "Failed to add '%s,%s,%s(%s)' extension into '%s' context\n",
					exten, prior, app, app_data, into_context);
			break;
		}
		return CLI_FAILURE;
	}

	if (a->argc == 7) {
		ast_cli(a->fd, "Extension %s@%s (%s) replace by '%s,%s,%s(%s)'\n",
			exten, into_context, prior, exten, prior, app, app_data);
	} else {
		ast_cli(a->fd, "Extension '%s,%s,%s(%s)' added into '%s' context\n",
			exten, prior, app, app_data, into_context);
	}

	return CLI_SUCCESS;
}

static char *complete_dialplan_remove_context(struct ast_cli_args *a)
{
	struct ast_context *c = NULL;
	int len = strlen(a->word);
	char *res = NULL;
	int which = 0;

	if (a->pos != 3) {
		return NULL;
	}


	/* try to lock contexts list ... */
	if (ast_rdlock_contexts()) {
		ast_log(LOG_WARNING, "Failed to lock contexts list\n");
		return NULL;
	}

	/* walk through all contexts */
	while ( !res && (c = ast_walk_contexts(c)) ) {
		if (partial_match(ast_get_context_name(c), a->word, len) && ++which > a->n) {
			res = strdup(ast_get_context_name(c));
		}
	}
	ast_unlock_contexts();
	return res;
}

/*! dialplan add extension 6123,1,Dial,IAX/212.71.138.13/6123 into local */
static char *complete_dialplan_add_extension(struct ast_cli_args *a)
{
	int which = 0;

	if (a->pos == 4) {		/* complete 'into' word ... */
		return (a->n == 0) ? strdup("into") : NULL;
	} else if (a->pos == 5) { /* complete context */
		struct ast_context *c = NULL;
		int len = strlen(a->word);
		char *res = NULL;

		/* try to lock contexts list ... */
		if (ast_rdlock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		/* walk through all contexts */
		while ( !res && (c = ast_walk_contexts(c)) )
			if (partial_match(ast_get_context_name(c), a->word, len) && ++which > a->n)
				res = strdup(ast_get_context_name(c));
		ast_unlock_contexts();
		return res;
	} else if (a->pos == 6) {
		return a->n == 0 ? strdup("replace") : NULL;
	}
	return NULL;
}

/*!
 * IGNOREPAT CLI stuff
 */
static char *handle_cli_dialplan_add_ignorepat(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan add ignorepat";
		e->usage =
			"Usage: dialplan add ignorepat <pattern> into <context>\n"
			"       This command adds a new ignore pattern into context <context>\n"
			"\n"
			"Example: dialplan add ignorepat _3XX into local\n";
		return NULL;
	case CLI_GENERATE:
		return complete_dialplan_add_ignorepat(a);
	}

	if (a->argc != 6)
		return CLI_SHOWUSAGE;

	if (strcmp(a->argv[4], "into"))
		return CLI_SHOWUSAGE;

	if (ast_context_add_ignorepat(a->argv[5], a->argv[3], registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(a->fd, "Out of free memory\n");
			break;

		case ENOENT:
			ast_cli(a->fd, "There is no existence of '%s' context\n", a->argv[5]);
			break;

		case EEXIST:
			ast_cli(a->fd, "Ignore pattern '%s' already included in '%s' context\n",
				a->argv[3], a->argv[5]);
			break;

		case EBUSY:
			ast_cli(a->fd, "Failed to lock context(s) list, please, try again later\n");
			break;

		default:
			ast_cli(a->fd, "Failed to add ingore pattern '%s' into '%s' context\n",
				a->argv[3], a->argv[5]);
			break;
		}
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Ignore pattern '%s' added into '%s' context\n",
		a->argv[3], a->argv[5]);

	return CLI_SUCCESS;
}

static char *complete_dialplan_add_ignorepat(struct ast_cli_args *a)
{
	if (a->pos == 4)
		return a->n == 0 ? strdup("into") : NULL;
	else if (a->pos == 5) {
		struct ast_context *c;
		int which = 0;
		char *dupline, *ignorepat = NULL;
		const char *s;
		char *ret = NULL;
		int len = strlen(a->word);

		/* XXX skip first three words 'dialplan' 'add' 'ignorepat' */
		s = skip_words(a->line, 3);
		if (s == NULL)
			return NULL;
		dupline = strdup(s);
		if (!dupline) {
			ast_log(LOG_ERROR, "Malloc failure\n");
			return NULL;
		}
		ignorepat = strsep(&dupline, " ");

		if (ast_rdlock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock contexts list\n");
			return NULL;
		}

		for (c = NULL; !ret && (c = ast_walk_contexts(c));) {
			int found = 0;

			if (!partial_match(ast_get_context_name(c), a->word, len))
				continue; /* not mine */
			if (ignorepat) /* there must be one, right ? */
				found = lookup_c_ip(c, ignorepat);
			if (!found && ++which > a->n)
				ret = strdup(ast_get_context_name(c));
		}

		free(ignorepat);
		ast_unlock_contexts();
		return ret;
	}

	return NULL;
}

static char *handle_cli_dialplan_remove_ignorepat(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan remove ignorepat";
		e->usage =
			"Usage: dialplan remove ignorepat <pattern> from <context>\n"
			"       This command removes an ignore pattern from context <context>\n"
			"\n"
			"Example: dialplan remove ignorepat _3XX from local\n";
		return NULL;
	case CLI_GENERATE:
		return complete_dialplan_remove_ignorepat(a);
	}

	if (a->argc != 6)
		return CLI_SHOWUSAGE;

	if (strcmp(a->argv[4], "from"))
		return CLI_SHOWUSAGE;

	if (ast_context_remove_ignorepat(a->argv[5], a->argv[3], registrar)) {
		switch (errno) {
		case EBUSY:
			ast_cli(a->fd, "Failed to lock context(s) list, please try again later\n");
			break;

		case ENOENT:
			ast_cli(a->fd, "There is no existence of '%s' context\n", a->argv[5]);
			break;

		case EINVAL:
			ast_cli(a->fd, "There is no existence of '%s' ignore pattern in '%s' context\n",
					a->argv[3], a->argv[5]);
			break;

		default:
			ast_cli(a->fd, "Failed to remove ignore pattern '%s' from '%s' context\n",
					a->argv[3], a->argv[5]);
			break;
		}
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "Ignore pattern '%s' removed from '%s' context\n",
		a->argv[3], a->argv[5]);
	return CLI_SUCCESS;
}

static char *complete_dialplan_remove_ignorepat(struct ast_cli_args *a)
{
	struct ast_context *c;
	int which = 0;
	char *ret = NULL;

	if (a->pos == 3) {
		int len = strlen(a->word);
		if (ast_rdlock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		for (c = NULL; !ret && (c = ast_walk_contexts(c));) {
			struct ast_ignorepat *ip;

			if (ast_rdlock_context(c))	/* error, skip it */
				continue;
			
			for (ip = NULL; !ret && (ip = ast_walk_context_ignorepats(c, ip));) {
				if (partial_match(ast_get_ignorepat_name(ip), a->word, len) && ++which > a->n) {
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
	} else if (a->pos == 4) {
		 return a->n == 0 ? strdup("from") : NULL;
	} else if (a->pos == 5) { /* XXX check this */
		char *dupline, *duplinet, *ignorepat;
		int len = strlen(a->word);

		dupline = strdup(a->line);
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

		if (ast_rdlock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			free(dupline);
			return NULL;
		}

		for (c = NULL; !ret && (c = ast_walk_contexts(c)); ) {
			if (ast_rdlock_context(c))	/* fail, skip it */
				continue;
			if (!partial_match(ast_get_context_name(c), a->word, len))
				continue;
			if (lookup_c_ip(c, ignorepat) && ++which > a->n)
				ret = strdup(ast_get_context_name(c));
			ast_unlock_context(c);
		}
		ast_unlock_contexts();
		free(dupline);
		return NULL;
	}

	return NULL;
}

static int pbx_load_module(void);

static char *handle_cli_dialplan_reload(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "dialplan reload";
		e->usage =
			"Usage: dialplan reload\n"
			"       Reload extensions.conf without reloading any other\n"
			"       modules.  This command does not delete global variables\n"
			"       unless clearglobalvars is set to yes in extensions.conf\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 2)
		return CLI_SHOWUSAGE;

	if (clearglobalvars_config)
		pbx_builtin_clear_globals();

	pbx_load_module();
	ast_cli(a->fd, "Dialplan reloaded.\n");
	return CLI_SUCCESS;
}

/*!
 * CLI entries for commands provided by this module
 */
static struct ast_cli_entry cli_pbx_config[] = {
	AST_CLI_DEFINE(handle_cli_dialplan_add_extension,    "Add new extension into context"),
	AST_CLI_DEFINE(handle_cli_dialplan_remove_extension, "Remove a specified extension"),
	AST_CLI_DEFINE(handle_cli_dialplan_remove_context,   "Remove a specified context"),
	AST_CLI_DEFINE(handle_cli_dialplan_add_ignorepat,    "Add new ignore pattern"),
	AST_CLI_DEFINE(handle_cli_dialplan_remove_ignorepat, "Remove ignore pattern from context"),
	AST_CLI_DEFINE(handle_cli_dialplan_add_include,      "Include context in other context"),
	AST_CLI_DEFINE(handle_cli_dialplan_remove_include,   "Remove a specified include from context"),
	AST_CLI_DEFINE(handle_cli_dialplan_reload,           "Reload extensions and *only* extensions"),
	AST_CLI_DEFINE(handle_cli_dialplan_save,             "Save current dialplan into a file")
};

static struct ast_cli_entry cli_dialplan_save =
	AST_CLI_DEFINE(handle_cli_dialplan_save, "Save dialplan");

/*!
 * Standard module functions ...
 */
static int unload_module(void)
{
	if (static_config && !write_protect_config)
		ast_cli_unregister(&cli_dialplan_save);
	if (overrideswitch_config) {
		ast_free(overrideswitch_config);
	}
	ast_cli_unregister_multiple(cli_pbx_config, ARRAY_LEN(cli_pbx_config));
	ast_context_destroy(NULL, registrar);
	return 0;
}

/*!\note Protect against misparsing based upon commas in the middle of fields
 * like character classes.  We've taken steps to permit pretty much every other
 * printable character in a character class, so properly handling a comma at
 * this level is a natural extension.  This is almost like the standard
 * application parser in app.c, except that it handles square brackets. */
static char *pbx_strsep(char **destructible, const char *delim)
{
	int square = 0;
	char *res;

	if (!destructible || !*destructible) {
		return NULL;
	}
	res = *destructible;
	for (; **destructible; (*destructible)++) {
		if (**destructible == '[' && !strchr(delim, '[')) {
			square++;
		} else if (**destructible == ']' && !strchr(delim, ']')) {
			if (square) {
				square--;
			}
		} else if (**destructible == '\\' && !strchr(delim, '\\')) {
			(*destructible)++;
		} else if (strchr(delim, **destructible) && !square) {
			**destructible = '\0';
			(*destructible)++;
			break;
		}
	}
	if (**destructible == '\0') {
		*destructible = NULL;
	}
	return res;
}

static int pbx_load_config(const char *config_file)
{
	struct ast_config *cfg;
	char *end;
	char *label;
#ifdef LOW_MEMORY
	char realvalue[256];
#else
	char realvalue[8192];
#endif
	int lastpri = -2;
	struct ast_context *con;
	struct ast_variable *v;
	const char *cxt;
	const char *aft;
	const char *newpm, *ovsw;
	struct ast_flags config_flags = { 0 };
	char lastextension[256];
	cfg = ast_config_load(config_file, config_flags);
	if (!cfg || cfg == CONFIG_STATUS_FILEINVALID)
		return 0;

	/* Use existing config to populate the PBX table */
	static_config = ast_true(ast_variable_retrieve(cfg, "general", "static"));
	write_protect_config = ast_true(ast_variable_retrieve(cfg, "general", "writeprotect"));
	if ((aft = ast_variable_retrieve(cfg, "general", "autofallthrough")))
		autofallthrough_config = ast_true(aft);
	if ((newpm = ast_variable_retrieve(cfg, "general", "extenpatternmatchnew")))
		extenpatternmatchnew_config = ast_true(newpm);
	clearglobalvars_config = ast_true(ast_variable_retrieve(cfg, "general", "clearglobalvars"));
	if ((ovsw = ast_variable_retrieve(cfg, "general", "overrideswitch"))) {
		if (overrideswitch_config) {
			ast_free(overrideswitch_config);
		}
		if (!ast_strlen_zero(ovsw)) {
			overrideswitch_config = ast_strdup(ovsw);
		} else {
			overrideswitch_config = NULL;
		}
	}

	ast_copy_string(userscontext, ast_variable_retrieve(cfg, "general", "userscontext") ?: "default", sizeof(userscontext));
								    
	for (v = ast_variable_browse(cfg, "globals"); v; v = v->next) {
		pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
		pbx_builtin_setvar_helper(NULL, v->name, realvalue);
	}
	for (cxt = ast_category_browse(cfg, NULL);
	     cxt;
	     cxt = ast_category_browse(cfg, cxt)) {
		/* All categories but "general" or "globals" are considered contexts */
		if (!strcasecmp(cxt, "general") || !strcasecmp(cxt, "globals")) {
			continue;
		}
		if (!(con = ast_context_find_or_create(&local_contexts, local_table, cxt, registrar))) {
			continue;
		}

		/* Reset continuation items at the beginning of each context */
		lastextension[0] = '\0';
		lastpri = -2;

		for (v = ast_variable_browse(cfg, cxt); v; v = v->next) {
			char *tc = NULL;
			char realext[256] = "";
			char *stringp, *ext;
			const char *vfile;

			/* get filename for error reporting from top level or an #include */
			vfile = !*v->file ? config_file : v->file;

			if (!strncasecmp(v->name, "same", 4)) {
				if (ast_strlen_zero(lastextension)) {
					ast_log(LOG_ERROR,
						"No previous pattern in the first entry of context '%s' to match '%s' at line %d of %s!\n",
						cxt, v->name, v->lineno, vfile);
					continue;
				}
				if ((stringp = tc = ast_strdup(v->value))) {
					ast_copy_string(realext, lastextension, sizeof(realext));
					goto process_extension;
				}
			} else if (!strcasecmp(v->name, "exten")) {
				int ipri;
				char *plus;
				char *pri, *appl, *data, *cidmatch;

				if (!(stringp = tc = ast_strdup(v->value))) {
					continue;
				}

				ext = S_OR(pbx_strsep(&stringp, ","), "");
				pbx_substitute_variables_helper(NULL, ext, realext, sizeof(realext) - 1);
				ast_copy_string(lastextension, realext, sizeof(lastextension));
process_extension:
				ipri = -2;
				if ((cidmatch = strchr(realext, '/'))) {
					*cidmatch++ = '\0';
					ast_shrink_phone_number(cidmatch);
				}
				pri = ast_strip(S_OR(strsep(&stringp, ","), ""));
				if ((label = strchr(pri, '('))) {
					*label++ = '\0';
					if ((end = strchr(label, ')'))) {
						*end = '\0';
					} else {
						ast_log(LOG_WARNING,
							"Label missing trailing ')' at line %d of %s\n",
							v->lineno, vfile);
						ast_free(tc);
						continue;
					}
				}
				if ((plus = strchr(pri, '+'))) {
					*plus++ = '\0';
				}
				if (!strcmp(pri,"hint")) {
					ipri = PRIORITY_HINT;
				} else if (!strcmp(pri, "next") || !strcmp(pri, "n")) {
					if (lastpri > -2) {
						ipri = lastpri + 1;
					} else {
						ast_log(LOG_WARNING,
							"Can't use 'next' priority on the first entry at line %d of %s!\n",
							v->lineno, vfile);
						ast_free(tc);
						continue;
					}
				} else if (!strcmp(pri, "same") || !strcmp(pri, "s")) {
					if (lastpri > -2) {
						ipri = lastpri;
					} else {
						ast_log(LOG_WARNING,
							"Can't use 'same' priority on the first entry at line %d of %s!\n",
							v->lineno, vfile);
						ast_free(tc);
						continue;
					}
				} else if (sscanf(pri, "%30d", &ipri) != 1 &&
					   (ipri = ast_findlabel_extension2(NULL, con, realext, pri, cidmatch)) < 1) {
					ast_log(LOG_WARNING,
						"Invalid priority/label '%s' at line %d of %s\n",
						pri, v->lineno, vfile);
					ipri = 0;
					ast_free(tc);
					continue;
				} else if (ipri < 1) {
					ast_log(LOG_WARNING, "Invalid priority '%s' at line %d of %s\n",
						pri, v->lineno, vfile);
					ast_free(tc);
					continue;
				}
				appl = S_OR(stringp, "");
				/* Find the first occurrence of '(' */
				if (!strchr(appl, '(')) {
					/* No arguments */
					data = "";
				} else {
					char *orig_appl = ast_strdup(appl);

					if (!orig_appl) {
						ast_free(tc);
						continue;
					}

					appl = strsep(&stringp, "(");

					/* check if there are variables or expressions without an application, like: exten => 100,hint,DAHDI/g0/${GLOBAL(var)}  */
					if (strstr(appl, "${") || strstr(appl, "$[")){
						/* set appl to original one */
						strcpy(appl, orig_appl);
						/* set no data */
						data = "";
					/* no variable before application found -> go ahead */
					} else {
						data = S_OR(stringp, "");
						if ((end = strrchr(data, ')'))) {
							*end = '\0';
						} else {
							ast_log(LOG_WARNING,
								"No closing parenthesis found? '%s(%s' at line %d of %s\n",
								appl, data, v->lineno, vfile);
						}
					}
					ast_free(orig_appl);
				}

				appl = ast_skip_blanks(appl);
				if (ipri) {
					if (plus) {
						ipri += atoi(plus);
					}
					lastpri = ipri;
					if (!ast_opt_dont_warn && (!strcmp(realext, "_.") || !strcmp(realext, "_!"))) {
						ast_log(LOG_WARNING,
							"The use of '%s' for an extension is strongly discouraged and can have unexpected behavior.  Please use '_X%c' instead at line %d of %s\n",
							realext, realext[1], v->lineno, vfile);
					}
					if (ast_add_extension2(con, 0, realext, ipri, label, cidmatch, appl, ast_strdup(data), ast_free_ptr, registrar)) {
						ast_log(LOG_WARNING,
							"Unable to register extension at line %d of %s\n",
							v->lineno, vfile);
					}
				}
				ast_free(tc);
			} else if (!strcasecmp(v->name, "include")) {
				pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
				if (ast_context_add_include2(con, realvalue, registrar)) {
					switch (errno) {
					case ENOMEM:
						ast_log(LOG_WARNING, "Out of memory for context addition\n");
						break;

					case EBUSY:
						ast_log(LOG_WARNING, "Failed to lock context(s) list, please try again later\n");
						break;

					case EEXIST:
						ast_log(LOG_WARNING,
							"Context '%s' already included in '%s' context on include at line %d of %s\n",
							v->value, cxt, v->lineno, vfile);
						break;

					case ENOENT:
					case EINVAL:
						ast_log(LOG_WARNING,
							"There is no existence of context '%s' included at line %d of %s\n",
							errno == ENOENT ? v->value : cxt, v->lineno, vfile);
						break;

					default:
						ast_log(LOG_WARNING,
							"Failed to include '%s' in '%s' context at line %d of %s\n",
							v->value, cxt, v->lineno, vfile);
						break;
					}
				}
			} else if (!strcasecmp(v->name, "ignorepat")) {
				pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
				if (ast_context_add_ignorepat2(con, realvalue, registrar)) {
					ast_log(LOG_WARNING,
						"Unable to include ignorepat '%s' in context '%s' at line %d of %s\n",
						v->value, cxt, v->lineno, vfile);
				}
			} else if (!strcasecmp(v->name, "switch") || !strcasecmp(v->name, "lswitch") || !strcasecmp(v->name, "eswitch")) {
				char *appl, *data;
				stringp = realvalue;
				
				if (!strcasecmp(v->name, "switch")) {
					pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
				} else {
					ast_copy_string(realvalue, v->value, sizeof(realvalue));
				}
				appl = strsep(&stringp, "/");
				data = S_OR(stringp, "");
				if (ast_context_add_switch2(con, appl, data, !strcasecmp(v->name, "eswitch"), registrar)) {
					ast_log(LOG_WARNING,
						"Unable to include switch '%s' in context '%s' at line %d of %s\n",
						v->value, cxt, v->lineno, vfile);
				}
			} else {
				ast_log(LOG_WARNING,
					"==!!== Unknown directive: %s at line %d of %s -- IGNORING!!!\n",
					v->name, v->lineno, vfile);
			}
		}
	}
	ast_config_destroy(cfg);
	return 1;
}

static void append_interface(char *iface, int maxlen, char *add)
{
	int len = strlen(iface);
	if (strlen(add) + len < maxlen - 2) {
		if (strlen(iface)) {
			iface[len] = '&';
			strcpy(iface + len + 1, add);
		} else
			strcpy(iface, add);
	}
}

static void pbx_load_users(void)
{
	struct ast_config *cfg;
	char *cat, *chan;
	const char *dahdichan;
	const char *hasexten, *altexts;
	char tmp[256];
	char iface[256];
	char dahdicopy[256];
	char *ext, altcopy[256];
	char *c;
	int hasvoicemail;
	int start, finish, x;
	struct ast_context *con = NULL;
	struct ast_flags config_flags = { 0 };
	
	cfg = ast_config_load("users.conf", config_flags);
	if (!cfg)
		return;

	for (cat = ast_category_browse(cfg, NULL); cat ; cat = ast_category_browse(cfg, cat)) {
		if (!strcasecmp(cat, "general"))
			continue;
		iface[0] = '\0';
		if (ast_true(ast_config_option(cfg, cat, "hassip"))) {
			snprintf(tmp, sizeof(tmp), "SIP/%s", cat);
			append_interface(iface, sizeof(iface), tmp);
		}
		if (ast_true(ast_config_option(cfg, cat, "hasiax"))) {
			snprintf(tmp, sizeof(tmp), "IAX2/%s", cat);
			append_interface(iface, sizeof(iface), tmp);
		}
		if (ast_true(ast_config_option(cfg, cat, "hash323"))) {
			snprintf(tmp, sizeof(tmp), "H323/%s", cat);
			append_interface(iface, sizeof(iface), tmp);
		}
		hasexten = ast_config_option(cfg, cat, "hasexten");
		if (hasexten && !ast_true(hasexten))
			continue;
		hasvoicemail = ast_true(ast_config_option(cfg, cat, "hasvoicemail"));
		dahdichan = ast_variable_retrieve(cfg, cat, "dahdichan");
		if (!dahdichan)
			dahdichan = ast_variable_retrieve(cfg, "general", "dahdichan");
		if (!ast_strlen_zero(dahdichan)) {
			ast_copy_string(dahdicopy, dahdichan, sizeof(dahdicopy));
			c = dahdicopy;
			chan = strsep(&c, ",");
			while (chan) {
				if (sscanf(chan, "%30d-%30d", &start, &finish) == 2) {
					/* Range */
				} else if (sscanf(chan, "%30d", &start)) {
					/* Just one */
					finish = start;
				} else {
					start = 0; finish = 0;
				}
				if (finish < start) {
					x = finish;
					finish = start;
					start = x;
				}
				for (x = start; x <= finish; x++) {
					snprintf(tmp, sizeof(tmp), "DAHDI/%d", x);
					append_interface(iface, sizeof(iface), tmp);
				}
				chan = strsep(&c, ",");
			}
		}
		if (!ast_strlen_zero(iface)) {
			/* Only create a context here when it is really needed. Otherwise default empty context
			created by pbx_config may conflict with the one explicitly created by pbx_ael */
			if (!con)
				con = ast_context_find_or_create(&local_contexts, local_table, userscontext, registrar);

			if (!con) {
				ast_log(LOG_ERROR, "Can't find/create user context '%s'\n", userscontext);
				return;
			}

			/* Add hint */
			ast_add_extension2(con, 0, cat, -1, NULL, NULL, iface, NULL, NULL, registrar);
			/* If voicemail, use "stdexten" else use plain old dial */
			if (hasvoicemail) {
				if (ast_opt_stdexten_macro) {
					/* Use legacy stdexten macro method. */
					snprintf(tmp, sizeof(tmp), "stdexten,%s,${HINT}", cat);
					ast_add_extension2(con, 0, cat, 1, NULL, NULL, "Macro", ast_strdup(tmp), ast_free_ptr, registrar);
				} else {
					snprintf(tmp, sizeof(tmp), "%s,stdexten(${HINT})", cat);
					ast_add_extension2(con, 0, cat, 1, NULL, NULL, "Gosub", ast_strdup(tmp), ast_free_ptr, registrar);
				}
			} else {
				ast_add_extension2(con, 0, cat, 1, NULL, NULL, "Dial", ast_strdup("${HINT}"), ast_free_ptr, registrar);
			}
			altexts = ast_variable_retrieve(cfg, cat, "alternateexts");
			if (!ast_strlen_zero(altexts)) {
				snprintf(tmp, sizeof(tmp), "%s,1", cat);
				ast_copy_string(altcopy, altexts, sizeof(altcopy));
				c = altcopy;
				ext = strsep(&c, ",");
				while (ext) {
					ast_add_extension2(con, 0, ext, 1, NULL, NULL, "Goto", ast_strdup(tmp), ast_free_ptr, registrar);
					ext = strsep(&c, ",");
				}
			}
		}
	}
	ast_config_destroy(cfg);
}

static int pbx_load_module(void)
{
	struct ast_context *con;

	ast_mutex_lock(&reload_lock);

	if (!local_table)
		local_table = ast_hashtab_create(17, ast_hashtab_compare_contexts, ast_hashtab_resize_java, ast_hashtab_newsize_java, ast_hashtab_hash_contexts, 0);

	if (!pbx_load_config(config)) {
		ast_mutex_unlock(&reload_lock);
		return AST_MODULE_LOAD_DECLINE;
	}
	
	pbx_load_users();

	ast_merge_contexts_and_delete(&local_contexts, local_table, registrar);
	local_table = NULL; /* the local table has been moved into the global one. */
	local_contexts = NULL;

	ast_mutex_unlock(&reload_lock);

	for (con = NULL; (con = ast_walk_contexts(con));)
		ast_context_verify_includes(con);

	pbx_set_overrideswitch(overrideswitch_config);
	pbx_set_autofallthrough(autofallthrough_config);
	pbx_set_extenpatternmatchnew(extenpatternmatchnew_config);

	return AST_MODULE_LOAD_SUCCESS;
}

static int load_module(void)
{
	if (static_config && !write_protect_config)
		ast_cli_register(&cli_dialplan_save);
	ast_cli_register_multiple(cli_pbx_config, ARRAY_LEN(cli_pbx_config));

	if (pbx_load_module())
		return AST_MODULE_LOAD_DECLINE;

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload(void)
{
	if (clearglobalvars_config)
		pbx_builtin_clear_globals();
	return pbx_load_module();
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Text Extension Configuration",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
