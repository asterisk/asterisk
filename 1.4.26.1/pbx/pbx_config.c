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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/cli.h"
#include "asterisk/callerid.h"

static char *config = "extensions.conf";
static char *registrar = "pbx_config";
static char userscontext[AST_MAX_EXTENSION] = "default";

static int static_config = 0;
static int write_protect_config = 1;
static int autofallthrough_config = 1;
static int clearglobalvars_config = 0;

AST_MUTEX_DEFINE_STATIC(save_dialplan_lock);

static struct ast_context *local_contexts = NULL;

/*
 * Help for commands provided by this module ...
 */
static char context_add_extension_help[] =
"Usage: dialplan add extension <exten>,<priority>,<app>,<app-data>\n"
"       into <context> [replace]\n\n"
"       This command will add new extension into <context>. If there is an\n"
"       existence of extension with the same priority and last 'replace'\n"
"       arguments is given here we simply replace this extension.\n"
"\n"
"Example: dialplan add extension 6123,1,Dial,IAX/216.207.245.56/6123 into local\n"
"         Now, you can dial 6123 and talk to Markster :)\n";

static char context_remove_extension_help[] =
"Usage: dialplan remove extension exten[/cid]@context [priority]\n"
"       Remove an extension from a given context. If a priority\n"
"       is given, only that specific priority from the given extension\n"
"       will be removed.\n";

static char context_add_ignorepat_help[] =
"Usage: dialplan add ignorepat <pattern> into <context>\n"
"       This command adds a new ignore pattern into context <context>\n"
"\n"
"Example: dialplan add ignorepat _3XX into local\n";

static char context_remove_ignorepat_help[] =
"Usage: dialplan remove ignorepat <pattern> from <context>\n"
"       This command removes an ignore pattern from context <context>\n"
"\n"
"Example: dialplan remove ignorepat _3XX from local\n";

static char context_add_include_help[] =
"Usage: dialplan add include <context> into <context>\n"
"       Include a context in another context.\n";

static char context_remove_include_help[] =
"Usage: dialplan remove include <context> from <context>\n"
"       Remove an included context from another context.\n";

static char save_dialplan_help[] =
"Usage: dialplan save [/path/to/extension/file]\n"
"       Save dialplan created by pbx_config module.\n"
"\n"
"Example: dialplan save                 (/etc/asterisk/extensions.conf)\n"
"         dialplan save /home/markster  (/home/markster/extensions.conf)\n";

static char reload_extensions_help[] =
"Usage: dialplan reload\n"
"       reload extensions.conf without reloading any other modules\n"
"       This command does not delete global variables unless\n"
"       clearglobalvars is set to yes in extensions.conf\n";

/*
 * Implementation of functions provided by this module
 */

/*!
 * REMOVE INCLUDE command stuff
 */
static int handle_context_dont_include_deprecated(int fd, int argc, char *argv[])
{
	if (argc != 5)
		return RESULT_SHOWUSAGE;

	if (strcmp(argv[3], "into"))
		return RESULT_SHOWUSAGE;

	if (!ast_context_remove_include(argv[4], argv[2], registrar)) {
		ast_cli(fd, "We are not including '%s' into '%s' now\n",
			argv[2], argv[4]);
		return RESULT_SUCCESS;
	}

	ast_cli(fd, "Failed to remove '%s' include from '%s' context\n",
		argv[2], argv[4]);
	return RESULT_FAILURE;
}

static int handle_context_remove_include(int fd, int argc, char *argv[])
{
	if (argc != 6) {
		return RESULT_SHOWUSAGE;
	}

	if (strcmp(argv[4], "from")) {
		return RESULT_SHOWUSAGE;
	}

	if (!ast_context_remove_include(argv[5], argv[3], registrar)) {
		ast_cli(fd, "The dialplan no longer includes '%s' into '%s'\n",
			argv[3], argv[5]);
		return RESULT_SUCCESS;
	}

	ast_cli(fd, "Failed to remove '%s' include from '%s' context\n",
		argv[3], argv[5]);

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
static char *complete_context_dont_include_deprecated(const char *line, const char *word,
	int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int len = strlen(word); /* how many bytes to match */
	struct ast_context *c = NULL;

	if (pos == 2) {		/* "dont include _X_" */
		if (ast_wrlock_contexts()) {
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

		if (ast_rdlock_contexts()) {
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

		if (ast_rdlock_contexts()) {
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

static char *complete_context_remove_include(const char *line, const char *word,
	int pos, int state)
{
	int which = 0;
	char *res = NULL;
	int len = strlen(word); /* how many bytes to match */
	struct ast_context *c = NULL;

	if (pos == 3) {		/* "dialplan remove include _X_" */
		if (ast_rdlock_contexts()) {
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
	} else if (pos == 4) { /* "dialplan remove include CTX _X_" */
		/*
		 * complete as 'from', but only if previous context is really
		 * included somewhere
		 */
		char *context, *dupline;
		const char *s = skip_words(line, 3); /* skip 'dialplan' 'remove' 'include' */

		if (state > 0)
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
	} else if (pos == 5) { /* "dialplan remove include CTX from _X_" */
		/*
		 * Context from which we removing include ... 
		 */
		char *context, *dupline, *from;
		const char *s = skip_words(line, 3); /* skip 'dialplan' 'remove' 'include' */
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
static int handle_context_remove_extension_deprecated(int fd, int argc, char *argv[])
{
	int removing_priority = 0;
	char *exten, *context, *cid;
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
	if (split_ec(argv[2], &exten, &context, &cid))
		return RESULT_FAILURE; /* XXX malloc failure */
	if ((!strlen(exten)) || (!(strlen(context)))) {
		ast_cli(fd, "Missing extension or context name in second argument '%s'\n",
			argv[2]);
		free(exten);
		return RESULT_FAILURE;
	}

	if (!ast_context_remove_extension_callerid(context, exten, removing_priority,
			/* Do NOT substitute S_OR; it is NOT the same thing */
			cid ? cid : (removing_priority ? "" : NULL), cid ? 1 : 0, registrar)) {
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

static int handle_context_remove_extension(int fd, int argc, char *argv[])
{
	int removing_priority = 0;
	char *exten, *context, *cid;
	int ret = RESULT_FAILURE;

	if (argc != 5 && argc != 4) return RESULT_SHOWUSAGE;

	/*
	 * Priority input checking ...
	 */
	if (argc == 5) {
		char *c = argv[4];

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
				ast_cli(fd, "Invalid priority '%s'\n", argv[4]);
				return RESULT_FAILURE;
			}
			removing_priority = atoi(argv[4]);
		}

		if (removing_priority == 0) {
			ast_cli(fd, "If you want to remove whole extension, please " \
				"omit priority argument\n");
			return RESULT_FAILURE;
		}
	}

	/* XXX original overwrote argv[3] */
	/*
	 * Format exten@context checking ...
	 */
	if (split_ec(argv[3], &exten, &context, &cid))
		return RESULT_FAILURE; /* XXX malloc failure */
	if ((!strlen(exten)) || (!(strlen(context)))) {
		ast_cli(fd, "Missing extension or context name in third argument '%s'\n",
			argv[3]);
		free(exten);
		return RESULT_FAILURE;
	}

	if (!ast_context_remove_extension_callerid(context, exten, removing_priority,
			/* Do NOT substitute S_OR; it is NOT the same thing */
			cid ? cid : (removing_priority ? "" : NULL), cid ? 1 : 0, registrar)) {
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

static char *complete_context_remove_extension_deprecated(const char *line, const char *word, int pos,
	int state)
{
	char *ret = NULL;
	int which = 0;

	if (pos == 2) { /* 'remove extension _X_' (exten/cid@context ... */
		struct ast_context *c = NULL;
		char *context = NULL, *exten = NULL, *cid = NULL;
		int le = 0;	/* length of extension */
		int lc = 0;	/* length of context */
		int lcid = 0; /* length of cid */

		lc = split_ec(word, &exten, &context, &cid);
		if (lc)	/* error */
			return NULL;
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
				if ( !strchr(word, '/') ||
						(!strchr(word, '@') && partial_match(ast_get_extension_cidmatch(e), cid, lcid)) ||
						(strchr(word, '@') && !strcmp(ast_get_extension_cidmatch(e), cid))) {
					if ( ((strchr(word, '/') || strchr(word, '@')) && !strcmp(ast_get_extension_name(e), exten)) ||
						 (!strchr(word, '/') && !strchr(word, '@') && partial_match(ast_get_extension_name(e), exten, le))) { /* n-th match */
						if (++which > state) {
							/* If there is an extension then return exten@context. */
							if (ast_get_extension_matchcid(e) && (!strchr(word, '@') || strchr(word, '/'))) {
								if (asprintf(&ret, "%s/%s@%s", ast_get_extension_name(e), ast_get_extension_cidmatch(e), ast_get_context_name(c)) < 0) {
									ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
									ret = NULL;
								}
								break;
							} else if (!ast_get_extension_matchcid(e) && !strchr(word, '/')) {
								if (asprintf(&ret, "%s@%s", ast_get_extension_name(e), ast_get_context_name(c)) < 0) {
									ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
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
		if (exten)
			free(exten);
	} else if (pos == 3) { /* 'remove extension EXT _X_' (priority) */
		char *exten = NULL, *context, *cid, *p;
		struct ast_context *c;
		int le, lc, lcid, len;
		const char *s = skip_words(line, 2); /* skip 'remove' 'extension' */
		int i = split_ec(s, &exten, &context, &cid);	/* parse ext@context */

		if (i)	/* error */
			goto error3;
		if ( (p = strchr(exten, ' ')) ) /* remove space after extension */
			*p = '\0';
		if ( (p = strchr(context, ' ')) ) /* remove space after context */
			*p = '\0';
		le = strlen(exten);
		lc = strlen(context);
		if (cid == NULL) {
			lcid = 0;
		} else {
			lcid = strlen(cid);
		}
		len = strlen(word);
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
	}
	return ret; 
}

static char *complete_context_remove_extension(const char *line, const char *word, int pos,
	int state)
{
	char *ret = NULL;
	int which = 0;

	if (pos == 3) { /* 'dialplan remove extension _X_' (exten@context ... */
		struct ast_context *c = NULL;
		char *context = NULL, *exten = NULL, *cid = NULL;
		int le = 0;	/* length of extension */
		int lc = 0;	/* length of context */
		int lcid = 0; /* length of cid */

		lc = split_ec(word, &exten, &context, &cid);
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
				if ( !strchr(word, '/') ||
						(!strchr(word, '@') && partial_match(ast_get_extension_cidmatch(e), cid, lcid)) ||
						(strchr(word, '@') && !strcmp(ast_get_extension_cidmatch(e), cid))) {
					if ( ((strchr(word, '/') || strchr(word, '@')) && !strcmp(ast_get_extension_name(e), exten)) ||
						 (!strchr(word, '/') && !strchr(word, '@') && partial_match(ast_get_extension_name(e), exten, le))) { /* n-th match */
						if (++which > state) {
							/* If there is an extension then return exten@context. */
							if (ast_get_extension_matchcid(e) && (!strchr(word, '@') || strchr(word, '/'))) {
								if (asprintf(&ret, "%s/%s@%s", ast_get_extension_name(e), ast_get_extension_cidmatch(e), ast_get_context_name(c)) < 0) {
									ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
									ret = NULL;
								}
								break;
							} else if (!ast_get_extension_matchcid(e) && !strchr(word, '/')) {
								if (asprintf(&ret, "%s@%s", ast_get_extension_name(e), ast_get_context_name(c)) < 0) {
									ast_log(LOG_WARNING, "asprintf() failed: %s\n", strerror(errno));
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
		if (exten)
			free(exten);
	} else if (pos == 4) { /* 'dialplan remove extension EXT _X_' (priority) */
		char *exten = NULL, *context, *cid, *p;
		struct ast_context *c;
		int le, lc, lcid, len;
		const char *s = skip_words(line, 3); /* skip 'dialplan' 'remove' 'extension' */
		int i = split_ec(s, &exten, &context, &cid);	/* parse ext@context */

		if (i)	/* error */
			goto error3;
		if ( (p = strchr(exten, ' ')) ) /* remove space after extension */
			*p = '\0';
		if ( (p = strchr(context, ' ')) ) /* remove space after context */
			*p = '\0';
		le = strlen(exten);
		lc = strlen(context);
		lcid = cid ? strlen(cid) : -1;
		len = strlen(word);
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
	}
	return ret; 
}

/*!
 * Include context ...
 */
static int handle_context_add_include_deprecated(int fd, int argc, char *argv[])
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

static int handle_context_add_include(int fd, int argc, char *argv[])
{
	if (argc != 6) /* dialplan add include CTX in CTX */
		return RESULT_SHOWUSAGE;

	/* fifth arg must be 'into' ... */
	if (strcmp(argv[4], "into"))
		return RESULT_SHOWUSAGE;

	if (ast_context_add_include(argv[5], argv[3], registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(fd, "Out of memory for context addition\n");
			break;

		case EBUSY:
			ast_cli(fd, "Failed to lock context(s) list, please try again later\n");
			break;

		case EEXIST:
			ast_cli(fd, "Context '%s' already included in '%s' context\n",
				argv[3], argv[5]);
			break;

		case ENOENT:
		case EINVAL:
			ast_cli(fd, "There is no existence of context '%s'\n",
				errno == ENOENT ? argv[5] : argv[3]);
			break;

		default:
			ast_cli(fd, "Failed to include '%s' in '%s' context\n",
				argv[3], argv[5]);
			break;
		}
		return RESULT_FAILURE;
	}

	/* show some info ... */
	ast_cli(fd, "Context '%s' included in '%s' context\n",
		argv[3], argv[5]);

	return RESULT_SUCCESS;
}

static char *complete_context_add_include_deprecated(const char *line, const char *word, int pos,
    int state)
{
	struct ast_context *c;
	int which = 0;
	char *ret = NULL;
	int len = strlen(word);

	if (pos == 2) {		/* 'include context _X_' (context) ... */
		if (ast_rdlock_contexts()) {
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
		if (ast_rdlock_contexts()) {
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

		if (ast_rdlock_contexts()) {
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

static char *complete_context_add_include(const char *line, const char *word, int pos,
    int state)
{
	struct ast_context *c;
	int which = 0;
	char *ret = NULL;
	int len = strlen(word);

	if (pos == 3) {		/* 'dialplan add include _X_' (context) ... */
		if (ast_rdlock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			return NULL;
		}
		for (c = NULL; !ret && (c = ast_walk_contexts(c)); )
			if (partial_match(ast_get_context_name(c), word, len) && ++which > state)
				ret = strdup(ast_get_context_name(c));
		ast_unlock_contexts();
		return ret;
	} else if (pos == 4) { /* dialplan add include CTX _X_ */
		/* complete  as 'into' if context exists or we are unable to check */
		char *context, *dupline;
		struct ast_context *c;
		const char *s = skip_words(line, 3); /* should not fail */

		if (state != 0)	/* only once */
			return NULL;

		/* parse context from line ... */
		context = dupline = strdup(s);
		if (!context) {
			ast_log(LOG_ERROR, "Out of free memory\n");
			return strdup("into");
		}
		strsep(&dupline, " ");

		/* check for context existence ... */
		if (ast_rdlock_contexts()) {
			ast_log(LOG_ERROR, "Failed to lock context list\n");
			/* our fault, we can't check, so complete 'into' ... */
			ret = strdup("into");
		} else {
			for (c = NULL; !ret && (c = ast_walk_contexts(c)); )
				if (!strcmp(context, ast_get_context_name(c)))
					ret = strdup("into"); /* found */
			ast_unlock_contexts();
		}
		free(context);
		return ret;
	} else if (pos == 5) { /* 'dialplan add include CTX into _X_' (dst context) */
		char *context, *dupline, *into;
		const char *s = skip_words(line, 3); /* should not fail */
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
	if (ast_rdlock_contexts()) {
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
	fprintf(output, "[general]\nstatic=%s\nwriteprotect=%s\nautofallthrough=%s\nclearglobalvars=%s\npriorityjumping=%s\n\n",
		static_config ? "yes" : "no",
		write_protect_config ? "yes" : "no",
                autofallthrough_config ? "yes" : "no",
                clearglobalvars_config ? "yes" : "no",
		ast_true(ast_variable_retrieve(cfg, "general", "priorityjumping")) ? "yes" : "no");

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
					char *tempdata = "";
					char *s;
					const char *el = ast_get_extension_label(p);
					char label[128] = "";
 
 					s = ast_get_extension_app_data(p);
					if (s) {
						char *t;
						tempdata = alloca(strlen(tempdata) * 2 + 1);

						for (t = tempdata; *s; s++, t++) {
							if (*s == '|')
								*t = ',';
							else {
								if (*s == ',' || *s == ';')
									*t++ = '\\';
								*t = *s;
							}
						}
						/* Terminating NULL */
						*t = *s;
					}

					if (ast_get_extension_matchcid(p)) {
						sep = "/";
						cid = ast_get_extension_cidmatch(p);
					} else
						sep = cid = "";
				
					if (el && (snprintf(label, sizeof(label), "(%s)", el) != (strlen(el) + 2)))
						incomplete = 1;	/* error encountered or label > 125 chars */
					
					fprintf(output, "exten => %s%s%s,%d%s,%s(%s)\n",
					    ast_get_extension_name(p), (ast_strlen_zero(sep) ? "" : sep), (ast_strlen_zero(cid) ? "" : cid),
					    ast_get_extension_priority(p), label,
					    ast_get_extension_app(p), (ast_strlen_zero(tempdata) ? "" : tempdata));
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
static int handle_context_add_extension_deprecated(int fd, int argc, char *argv[])
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
			if (sscanf(prior, "%30d", &iprior) != 1) {
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
		(void *)strdup(app_data), ast_free_ptr, registrar)) {
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
static int handle_context_add_extension(int fd, int argc, char *argv[])
{
	char *whole_exten;
	char *exten, *prior;
	int iprior = -2;
	char *cidmatch, *app, *app_data;
	char *start, *end;

	/* check for arguments at first */
	if (argc != 6 && argc != 7)
		return RESULT_SHOWUSAGE;
	if (strcmp(argv[4], "into"))
		return RESULT_SHOWUSAGE;
	if (argc == 7) if (strcmp(argv[6], "replace")) return RESULT_SHOWUSAGE;

	/* XXX overwrite argv[3] */
	whole_exten = argv[3];
	exten 	= strsep(&whole_exten,",");
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
	if (ast_add_extension(argv[5], argc == 7 ? 1 : 0, exten, iprior, NULL, cidmatch, app,
		(void *)strdup(app_data), ast_free_ptr, registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(fd, "Out of free memory\n");
			break;

		case EBUSY:
			ast_cli(fd, "Failed to lock context(s) list, please try again later\n");
			break;

		case ENOENT:
			ast_cli(fd, "No existence of '%s' context\n", argv[5]);
			break;

		case EEXIST:
			ast_cli(fd, "Extension %s@%s with priority %s already exists\n",
				exten, argv[5], prior);
			break;

		default:
			ast_cli(fd, "Failed to add '%s,%s,%s,%s' extension into '%s' context\n",
					exten, prior, app, app_data, argv[5]);
			break;
		}
		return RESULT_FAILURE;
	}

	if (argc == 7)
		ast_cli(fd, "Extension %s@%s (%s) replace by '%s,%s,%s,%s'\n",
			exten, argv[5], prior, exten, prior, app, app_data);
	else
		ast_cli(fd, "Extension '%s,%s,%s,%s' added into '%s' context\n",
			exten, prior, app, app_data, argv[5]);

	return RESULT_SUCCESS;
}

/*! dialplan add extension 6123,1,Dial,IAX/212.71.138.13/6123 into local */
static char *complete_context_add_extension_deprecated(const char *line, const char *word, int pos, int state)
{
	int which = 0;

	if (pos == 3) {		/* complete 'into' word ... */
		return (state == 0) ? strdup("into") : NULL;
	} else if (pos == 4) { /* complete context */
		struct ast_context *c = NULL;
		int len = strlen(word);
		char *res = NULL;

		/* try to lock contexts list ... */
		if (ast_rdlock_contexts()) {
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

static char *complete_context_add_extension(const char *line, const char *word, int pos, int state)
{
	int which = 0;

	if (pos == 4) {		/* complete 'into' word ... */
		return (state == 0) ? strdup("into") : NULL;
	} else if (pos == 5) { /* complete context */
		struct ast_context *c = NULL;
		int len = strlen(word);
		char *res = NULL;

		/* try to lock contexts list ... */
		if (ast_rdlock_contexts()) {
			ast_log(LOG_WARNING, "Failed to lock contexts list\n");
			return NULL;
		}

		/* walk through all contexts */
		while ( !res && (c = ast_walk_contexts(c)) )
			if (partial_match(ast_get_context_name(c), word, len) && ++which > state)
				res = strdup(ast_get_context_name(c));
		ast_unlock_contexts();
		return res;
	} else if (pos == 6) {
		return state == 0 ? strdup("replace") : NULL;
	}
	return NULL;
}

/*!
 * IGNOREPAT CLI stuff
 */
static int handle_context_add_ignorepat_deprecated(int fd, int argc, char *argv[])
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

static int handle_context_add_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 6)
		return RESULT_SHOWUSAGE;
	if (strcmp(argv[4], "into"))
		return RESULT_SHOWUSAGE;

	if (ast_context_add_ignorepat(argv[5], argv[3], registrar)) {
		switch (errno) {
		case ENOMEM:
			ast_cli(fd, "Out of free memory\n");
			break;

		case ENOENT:
			ast_cli(fd, "There is no existence of '%s' context\n", argv[5]);
			break;

		case EEXIST:
			ast_cli(fd, "Ignore pattern '%s' already included in '%s' context\n",
				argv[3], argv[5]);
			break;

		case EBUSY:
			ast_cli(fd, "Failed to lock context(s) list, please, try again later\n");
			break;

		default:
			ast_cli(fd, "Failed to add ingore pattern '%s' into '%s' context\n",
				argv[3], argv[5]);
			break;
		}
		return RESULT_FAILURE;
	}

	ast_cli(fd, "Ignore pattern '%s' added into '%s' context\n",
		argv[3], argv[5]);
	return RESULT_SUCCESS;
}

static char *complete_context_add_ignorepat_deprecated(const char *line, const char *word,
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

		if (ast_rdlock_contexts()) {
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

static char *complete_context_add_ignorepat(const char *line, const char *word,
	int pos, int state)
{
	if (pos == 4)
		return state == 0 ? strdup("into") : NULL;
	else if (pos == 5) {
		struct ast_context *c;
		int which = 0;
		char *dupline, *ignorepat = NULL;
		const char *s;
		char *ret = NULL;
		int len = strlen(word);

		/* XXX skip first three words 'dialplan' 'add' 'ignorepat' */
		s = skip_words(line, 3);
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

static int handle_context_remove_ignorepat_deprecated(int fd, int argc, char *argv[])
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

static int handle_context_remove_ignorepat(int fd, int argc, char *argv[])
{
	if (argc != 6)
		return RESULT_SHOWUSAGE;
	if (strcmp(argv[4], "from"))
		return RESULT_SHOWUSAGE;

	if (ast_context_remove_ignorepat(argv[5], argv[3], registrar)) {
		switch (errno) {
		case EBUSY:
			ast_cli(fd, "Failed to lock context(s) list, please try again later\n");
			break;

		case ENOENT:
			ast_cli(fd, "There is no existence of '%s' context\n", argv[5]);
			break;

		case EINVAL:
			ast_cli(fd, "There is no existence of '%s' ignore pattern in '%s' context\n",
					argv[3], argv[5]);
			break;

		default:
			ast_cli(fd, "Failed to remove ignore pattern '%s' from '%s' context\n", argv[3], argv[5]);
			break;
		}
		return RESULT_FAILURE;
	}

	ast_cli(fd, "Ignore pattern '%s' removed from '%s' context\n",
		argv[3], argv[5]);
	return RESULT_SUCCESS;
}

static char *complete_context_remove_ignorepat_deprecated(const char *line, const char *word,
	int pos, int state)
{
	struct ast_context *c;
	int which = 0;
	char *ret = NULL;

	if (pos == 2) {
		int len = strlen(word);
		if (ast_rdlock_contexts()) {
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

		if (ast_rdlock_contexts()) {
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

static char *complete_context_remove_ignorepat(const char *line, const char *word,
	int pos, int state)
{
	struct ast_context *c;
	int which = 0;
	char *ret = NULL;

	if (pos == 3) {
		int len = strlen(word);
		if (ast_rdlock_contexts()) {
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
	} else if (pos == 4) {
		 return state == 0 ? strdup("from") : NULL;
	} else if (pos == 5) { /* XXX check this */
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

		if (ast_rdlock_contexts()) {
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

static int pbx_load_module(void);

static int handle_reload_extensions(int fd, int argc, char *argv[])
{
	if (argc != 2)
		return RESULT_SHOWUSAGE;
	if (clearglobalvars_config)
		pbx_builtin_clear_globals();
	pbx_load_module();
	ast_cli(fd, "Dialplan reloaded.\n");
	return RESULT_SUCCESS;
}

/*!
 * CLI entries for commands provided by this module
 */
static struct ast_cli_entry cli_dont_include_deprecated = {
	{ "dont", "include", NULL },
	handle_context_dont_include_deprecated, NULL,
	NULL, complete_context_dont_include_deprecated };

static struct ast_cli_entry cli_remove_extension_deprecated = {
	{ "remove", "extension", NULL },
	handle_context_remove_extension_deprecated, NULL,
	NULL, complete_context_remove_extension_deprecated };

static struct ast_cli_entry cli_include_context_deprecated = {
	{ "include", "context", NULL },
	handle_context_add_include_deprecated, NULL,
	NULL, complete_context_add_include_deprecated };

static struct ast_cli_entry cli_add_extension_deprecated = {
	{ "add", "extension", NULL },
	handle_context_add_extension_deprecated, NULL,
	NULL, complete_context_add_extension_deprecated };

static struct ast_cli_entry cli_add_ignorepat_deprecated = {
	{ "add", "ignorepat", NULL },
	handle_context_add_ignorepat_deprecated, NULL,
	NULL, complete_context_add_ignorepat_deprecated };

static struct ast_cli_entry cli_remove_ignorepat_deprecated = {
	{ "remove", "ignorepat", NULL },
	handle_context_remove_ignorepat_deprecated, NULL,
	NULL, complete_context_remove_ignorepat_deprecated };

static struct ast_cli_entry cli_extensions_reload_deprecated = {
	{ "extensions", "reload", NULL },
	handle_reload_extensions, NULL,
	NULL };

static struct ast_cli_entry cli_save_dialplan_deprecated = {
	{ "save", "dialplan", NULL },
	handle_save_dialplan, NULL,
	NULL };

static struct ast_cli_entry cli_pbx_config[] = {
	{ { "dialplan", "add", "extension", NULL },
	handle_context_add_extension, "Add new extension into context",
	context_add_extension_help, complete_context_add_extension, &cli_add_extension_deprecated },

	{ { "dialplan", "remove", "extension", NULL },
	handle_context_remove_extension, "Remove a specified extension",
	context_remove_extension_help, complete_context_remove_extension, &cli_remove_extension_deprecated },

	{ { "dialplan", "add", "ignorepat", NULL },
	handle_context_add_ignorepat, "Add new ignore pattern",
	context_add_ignorepat_help, complete_context_add_ignorepat, &cli_add_ignorepat_deprecated },

	{ { "dialplan", "remove", "ignorepat", NULL },
	handle_context_remove_ignorepat, "Remove ignore pattern from context",
	context_remove_ignorepat_help, complete_context_remove_ignorepat, &cli_remove_ignorepat_deprecated },

	{ { "dialplan", "add", "include", NULL },
	handle_context_add_include, "Include context in other context",
	context_add_include_help, complete_context_add_include, &cli_include_context_deprecated },

	{ { "dialplan", "remove", "include", NULL },
	handle_context_remove_include, "Remove a specified include from context",
	context_remove_include_help, complete_context_remove_include, &cli_dont_include_deprecated },

	{ { "dialplan", "reload", NULL },
	handle_reload_extensions, "Reload extensions and *only* extensions",
	reload_extensions_help, NULL, &cli_extensions_reload_deprecated },
};


static struct ast_cli_entry cli_dialplan_save = {
	{ "dialplan", "save", NULL },
	handle_save_dialplan, "Save dialplan",
	save_dialplan_help, NULL, &cli_save_dialplan_deprecated };

/*!
 * Standard module functions ...
 */
static int unload_module(void)
{
	if (static_config && !write_protect_config)
		ast_cli_unregister(&cli_dialplan_save);
	ast_cli_unregister_multiple(cli_pbx_config, sizeof(cli_pbx_config) / sizeof(struct ast_cli_entry));
	ast_context_destroy(NULL, registrar);
	return 0;
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

	cfg = ast_config_load(config_file);
	if (!cfg)
		return 0;

	/* Use existing config to populate the PBX table */
	static_config = ast_true(ast_variable_retrieve(cfg, "general", "static"));
	write_protect_config = ast_true(ast_variable_retrieve(cfg, "general", "writeprotect"));
	if ((aft = ast_variable_retrieve(cfg, "general", "autofallthrough")))
		autofallthrough_config = ast_true(aft);
	clearglobalvars_config = ast_true(ast_variable_retrieve(cfg, "general", "clearglobalvars"));
	ast_set2_flag(&ast_options, ast_true(ast_variable_retrieve(cfg, "general", "priorityjumping")), AST_OPT_FLAG_PRIORITY_JUMPING);

	if ((cxt = ast_variable_retrieve(cfg, "general", "userscontext"))) 
		ast_copy_string(userscontext, cxt, sizeof(userscontext));
	else
		ast_copy_string(userscontext, "default", sizeof(userscontext));
								    
	for (v = ast_variable_browse(cfg, "globals"); v; v = v->next) {
		memset(realvalue, 0, sizeof(realvalue));
		pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
		pbx_builtin_setvar_helper(NULL, v->name, realvalue);
	}
	for (cxt = NULL; (cxt = ast_category_browse(cfg, cxt)); ) {
		/* All categories but "general" or "globals" are considered contexts */
		if (!strcasecmp(cxt, "general") || !strcasecmp(cxt, "globals"))
			continue;
		con=ast_context_find_or_create(&local_contexts,cxt, registrar);
		if (con == NULL)
			continue;

		for (v = ast_variable_browse(cfg, cxt); v; v = v->next) {
			if (!strcasecmp(v->name, "exten")) {
				char *tc = ast_strdup(v->value);
				if (tc) {
					int ipri = -2;
					char realext[256]="";
					char *plus, *firstp, *firstc;
					char *pri, *appl, *data, *cidmatch;
					char *stringp = tc;
					char *ext = strsep(&stringp, ",");
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
					pri = ast_skip_blanks(pri);
					pri = ast_trim_blanks(pri);
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
					} else if (sscanf(pri, "%30d", &ipri) != 1 &&
					    (ipri = ast_findlabel_extension2(NULL, con, realext, pri, cidmatch)) < 1) {
						ast_log(LOG_WARNING, "Invalid priority/label '%s' at line %d\n", pri, v->lineno);
						ipri = 0;
					}
					appl = S_OR(stringp, "");
					/* Find the first occurrence of either '(' or ',' */
					firstc = strchr(appl, ',');
					firstp = strchr(appl, '(');
					if (firstc && (!firstp || firstc < firstp)) {
						/* comma found, no parenthesis */
						/* or both found, but comma found first */
						appl = strsep(&stringp, ",");
						data = stringp;
					} else if (!firstc && !firstp) {
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
						if (!ast_opt_dont_warn && !strcmp(realext, "_."))
							ast_log(LOG_WARNING, "The use of '_.' for an extension is strongly discouraged and can have unexpected behavior.  Please use '_X.' instead at line %d\n", v->lineno);
						if (ast_add_extension2(con, 0, realext, ipri, label, cidmatch, appl, strdup(data), ast_free_ptr, registrar)) {
							ast_log(LOG_WARNING, "Unable to register extension at line %d\n", v->lineno);
						}
					}
					free(tc);
				}
			} else if (!strcasecmp(v->name, "include")) {
				memset(realvalue, 0, sizeof(realvalue));
				pbx_substitute_variables_helper(NULL, v->value, realvalue, sizeof(realvalue) - 1);
				if (ast_context_add_include2(con, realvalue, registrar))
					ast_log(LOG_WARNING, "Unable to include context '%s' in context '%s'\n", v->value, cxt);
			} else if (!strcasecmp(v->name, "ignorepat")) {
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
	const char *hasexten;
	char tmp[256];
	char iface[256];
	char zapcopy[256];
	char *c;
	int len;
	int hasvoicemail;
	int start, finish, x;
	struct ast_context *con = NULL;
	
	cfg = ast_config_load("users.conf");
	if (!cfg)
		return;

	for (cat = ast_category_browse(cfg, NULL); cat ; cat = ast_category_browse(cfg, cat)) {
		if (!strcasecmp(cat, "general"))
			continue;
		iface[0] = '\0';
		len = sizeof(iface);
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
		if (!dahdichan) {
		/* no dahdichan, but look for zapchan too */
			dahdichan = ast_variable_retrieve(cfg, cat, "zapchan");
			if (!dahdichan) {
				dahdichan = ast_variable_retrieve(cfg, "general", "zapchan");
			}
			if (!ast_strlen_zero(dahdichan)) {
				ast_log(LOG_WARNING, "Use of zapchan in users.conf is deprecated. Please update configuration to use dahdichan instead.\n");
			}
		}
		if (!ast_strlen_zero(dahdichan)) {
			ast_copy_string(zapcopy, dahdichan, sizeof(zapcopy));
			c = zapcopy;
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
					snprintf(tmp, sizeof(tmp), "%s/%d", dahdi_chan_name, x);
					append_interface(iface, sizeof(iface), tmp);
				}
				chan = strsep(&c, ",");
			}
		}
		if (!ast_strlen_zero(iface)) {
			/* Only create a context here when it is really needed. Otherwise default empty context
			created by pbx_config may conflict with the one explicitly created by pbx_ael */
			if (!con)
				con = ast_context_find_or_create(&local_contexts, userscontext, registrar);

			if (!con) {
				ast_log(LOG_ERROR, "Can't find/create user context '%s'\n", userscontext);
				return;
			}

			/* Add hint */
			ast_add_extension2(con, 0, cat, -1, NULL, NULL, iface, NULL, NULL, registrar);
			/* If voicemail, use "stdexten" else use plain old dial */
			if (hasvoicemail) {
				snprintf(tmp, sizeof(tmp), "stdexten|%s|${HINT}", cat);
				ast_add_extension2(con, 0, cat, 1, NULL, NULL, "Macro", strdup(tmp), ast_free_ptr, registrar);
			} else {
				ast_add_extension2(con, 0, cat, 1, NULL, NULL, "Dial", strdup("${HINT}"), ast_free_ptr, registrar);
			}
		}
	}
	ast_config_destroy(cfg);
}

static int pbx_load_module(void)
{
	struct ast_context *con;

	if(!pbx_load_config(config))
		return AST_MODULE_LOAD_DECLINE;
	
	pbx_load_users();

	ast_merge_contexts_and_delete(&local_contexts, registrar);

	for (con = NULL; (con = ast_walk_contexts(con));)
		ast_context_verify_includes(con);

	pbx_set_autofallthrough(autofallthrough_config);

	return 0;
}

static int load_module(void)
{
	if (pbx_load_module())
		return AST_MODULE_LOAD_DECLINE;
 
	if (static_config && !write_protect_config)
		ast_cli_register(&cli_dialplan_save);
	ast_cli_register_multiple(cli_pbx_config, sizeof(cli_pbx_config) / sizeof(struct ast_cli_entry));

	return 0;
}

static int reload(void)
{
	if (clearglobalvars_config)
		pbx_builtin_clear_globals();
	pbx_load_module();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Text Extension Configuration",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
