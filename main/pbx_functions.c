/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, CFWare, LLC
 *
 * Corey Farrell <git@cfware.com>
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
 * \brief Custom function management routines.
 *
 * \author Corey Farrell <git@cfware.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/term.h"
#include "asterisk/threadstorage.h"
#include "asterisk/xmldoc.h"
#include "pbx_private.h"

/*!
 * \brief A thread local indicating whether the current thread can run
 * 'dangerous' dialplan functions.
 */
AST_THREADSTORAGE(thread_inhibit_escalations_tl);

/*!
 * \brief Set to true (non-zero) to globally allow all dangerous dialplan
 * functions to run.
 */
static int live_dangerously;

/*!
 * \brief Registered functions container.
 *
 * It is sorted by function name.
 */
static AST_RWLIST_HEAD_STATIC(acf_root, ast_custom_function);

static char *handle_show_functions(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_custom_function *acf;
	int count_acf = 0;
	int like = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show functions [like]";
		e->usage =
			"Usage: core show functions [like <text>]\n"
			"       List builtin functions, optionally only those matching a given string\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc == 5 && (!strcmp(a->argv[3], "like")) ) {
		like = 1;
	} else if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	ast_cli(a->fd, "%s Custom Functions:\n"
		"--------------------------------------------------------------------------------\n",
		like ? "Matching" : "Installed");

	AST_RWLIST_RDLOCK(&acf_root);
	AST_RWLIST_TRAVERSE(&acf_root, acf, acflist) {
		if (!like || strstr(acf->name, a->argv[4])) {
			count_acf++;
			ast_cli(a->fd, "%-20.20s  %-35.35s  %s\n",
				S_OR(acf->name, ""),
				S_OR(acf->syntax, ""),
				S_OR(acf->synopsis, ""));
		}
	}
	AST_RWLIST_UNLOCK(&acf_root);

	ast_cli(a->fd, "%d %scustom functions installed.\n", count_acf, like ? "matching " : "");

	return CLI_SUCCESS;
}

static char *complete_functions(const char *word, int pos, int state)
{
	struct ast_custom_function *cur;
	char *ret = NULL;
	int which = 0;
	int wordlen;
	int cmp;

	if (pos != 3) {
		return NULL;
	}

	wordlen = strlen(word);
	AST_RWLIST_RDLOCK(&acf_root);
	AST_RWLIST_TRAVERSE(&acf_root, cur, acflist) {
		/*
		 * Do a case-insensitive search for convenience in this
		 * 'complete' function.
		 *
		 * We must search the entire container because the functions are
		 * sorted and normally found case sensitively.
		 */
		cmp = strncasecmp(word, cur->name, wordlen);
		if (!cmp) {
			/* Found match. */
			if (++which <= state) {
				/* Not enough matches. */
				continue;
			}
			ret = ast_strdup(cur->name);
			break;
		}
	}
	AST_RWLIST_UNLOCK(&acf_root);

	return ret;
}

static char *handle_show_function(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_custom_function *acf;
	/* Maximum number of characters added by terminal coloring is 22 */
	char infotitle[64 + AST_MAX_APP + 22], syntitle[40], desctitle[40], argtitle[40], seealsotitle[40];
	char info[64 + AST_MAX_APP], *synopsis = NULL, *description = NULL, *seealso = NULL;
	char stxtitle[40], *syntax = NULL, *arguments = NULL;
	int syntax_size, description_size, synopsis_size, arguments_size, seealso_size;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show function";
		e->usage =
			"Usage: core show function <function>\n"
			"       Describe a particular dialplan function.\n";
		return NULL;
	case CLI_GENERATE:
		return complete_functions(a->word, a->pos, a->n);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (!(acf = ast_custom_function_find(a->argv[3]))) {
		ast_cli(a->fd, "No function by that name registered.\n");

		return CLI_FAILURE;
	}

	syntax_size = strlen(S_OR(acf->syntax, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
	syntax = ast_malloc(syntax_size);
	if (!syntax) {
		ast_cli(a->fd, "Memory allocation failure!\n");

		return CLI_FAILURE;
	}

	snprintf(info, sizeof(info), "\n  -= Info about function '%s' =- \n\n", acf->name);
	term_color(infotitle, info, COLOR_MAGENTA, 0, sizeof(infotitle));
	term_color(syntitle, "[Synopsis]\n", COLOR_MAGENTA, 0, 40);
	term_color(desctitle, "[Description]\n", COLOR_MAGENTA, 0, 40);
	term_color(stxtitle, "[Syntax]\n", COLOR_MAGENTA, 0, 40);
	term_color(argtitle, "[Arguments]\n", COLOR_MAGENTA, 0, 40);
	term_color(seealsotitle, "[See Also]\n", COLOR_MAGENTA, 0, 40);
	term_color(syntax, S_OR(acf->syntax, "Not available"), COLOR_CYAN, 0, syntax_size);
#ifdef AST_XML_DOCS
	if (acf->docsrc == AST_XML_DOC) {
		arguments = ast_xmldoc_printable(S_OR(acf->arguments, "Not available"), 1);
		synopsis = ast_xmldoc_printable(S_OR(acf->synopsis, "Not available"), 1);
		description = ast_xmldoc_printable(S_OR(acf->desc, "Not available"), 1);
		seealso = ast_xmldoc_printable(S_OR(acf->seealso, "Not available"), 1);
	} else
#endif
	{
		synopsis_size = strlen(S_OR(acf->synopsis, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		synopsis = ast_malloc(synopsis_size);

		description_size = strlen(S_OR(acf->desc, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		description = ast_malloc(description_size);

		arguments_size = strlen(S_OR(acf->arguments, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		arguments = ast_malloc(arguments_size);

		seealso_size = strlen(S_OR(acf->seealso, "Not Available")) + AST_TERM_MAX_ESCAPE_CHARS;
		seealso = ast_malloc(seealso_size);

		/* check allocated memory. */
		if (!synopsis || !description || !arguments || !seealso) {
			ast_free(synopsis);
			ast_free(description);
			ast_free(arguments);
			ast_free(seealso);
			ast_free(syntax);

			return CLI_FAILURE;
		}

		term_color(arguments, S_OR(acf->arguments, "Not available"), COLOR_CYAN, 0, arguments_size);
		term_color(synopsis, S_OR(acf->synopsis, "Not available"), COLOR_CYAN, 0, synopsis_size);
		term_color(description, S_OR(acf->desc, "Not available"), COLOR_CYAN, 0, description_size);
		term_color(seealso, S_OR(acf->seealso, "Not available"), COLOR_CYAN, 0, seealso_size);
	}

	ast_cli(a->fd, "%s%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n\n%s%s\n",
			infotitle, syntitle, synopsis, desctitle, description,
			stxtitle, syntax, argtitle, arguments, seealsotitle, seealso);

	ast_free(arguments);
	ast_free(synopsis);
	ast_free(description);
	ast_free(seealso);
	ast_free(syntax);

	return CLI_SUCCESS;
}

static struct ast_custom_function *ast_custom_function_find_nolock(const char *name)
{
	struct ast_custom_function *cur;
	int cmp;

	AST_RWLIST_TRAVERSE(&acf_root, cur, acflist) {
		cmp = strcmp(name, cur->name);
		if (cmp > 0) {
			continue;
		}
		if (!cmp) {
			/* Found it. */
			break;
		}
		/* Not in container. */
		cur = NULL;
		break;
	}

	return cur;
}

struct ast_custom_function *ast_custom_function_find(const char *name)
{
	struct ast_custom_function *acf;

	AST_RWLIST_RDLOCK(&acf_root);
	acf = ast_custom_function_find_nolock(name);
	AST_RWLIST_UNLOCK(&acf_root);

	return acf;
}

int ast_custom_function_unregister(struct ast_custom_function *acf)
{
	struct ast_custom_function *cur;

	if (!acf) {
		return -1;
	}

	AST_RWLIST_WRLOCK(&acf_root);
	cur = AST_RWLIST_REMOVE(&acf_root, acf, acflist);
	if (cur) {
#ifdef AST_XML_DOCS
		if (cur->docsrc == AST_XML_DOC) {
			ast_string_field_free_memory(acf);
		}
#endif
		ast_verb(2, "Unregistered custom function %s\n", cur->name);
	}
	AST_RWLIST_UNLOCK(&acf_root);

	return cur ? 0 : -1;
}

/*!
 * \brief Returns true if given custom function escalates privileges on read.
 *
 * \param acf Custom function to query.
 * \return True (non-zero) if reads escalate privileges.
 * \return False (zero) if reads just read.
 */
static int read_escalates(const struct ast_custom_function *acf)
{
	return acf->read_escalates;
}

/*!
 * \brief Returns true if given custom function escalates privileges on write.
 *
 * \param acf Custom function to query.
 * \return True (non-zero) if writes escalate privileges.
 * \return False (zero) if writes just write.
 */
static int write_escalates(const struct ast_custom_function *acf)
{
	return acf->write_escalates;
}

/*! \internal
 *  \brief Retrieve the XML documentation of a specified ast_custom_function,
 *         and populate ast_custom_function string fields.
 *  \param acf ast_custom_function structure with empty 'desc' and 'synopsis'
 *             but with a function 'name'.
 *  \retval -1 On error.
 *  \retval 0 On succes.
 */
static int acf_retrieve_docs(struct ast_custom_function *acf)
{
#ifdef AST_XML_DOCS
	char *tmpxml;

	/* Let's try to find it in the Documentation XML */
	if (!ast_strlen_zero(acf->desc) || !ast_strlen_zero(acf->synopsis)) {
		return 0;
	}

	if (ast_string_field_init(acf, 128)) {
		return -1;
	}

	/* load synopsis */
	tmpxml = ast_xmldoc_build_synopsis("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, synopsis, tmpxml);
	ast_free(tmpxml);

	/* load description */
	tmpxml = ast_xmldoc_build_description("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, desc, tmpxml);
	ast_free(tmpxml);

	/* load syntax */
	tmpxml = ast_xmldoc_build_syntax("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, syntax, tmpxml);
	ast_free(tmpxml);

	/* load arguments */
	tmpxml = ast_xmldoc_build_arguments("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, arguments, tmpxml);
	ast_free(tmpxml);

	/* load seealso */
	tmpxml = ast_xmldoc_build_seealso("function", acf->name, ast_module_name(acf->mod));
	ast_string_field_set(acf, seealso, tmpxml);
	ast_free(tmpxml);

	acf->docsrc = AST_XML_DOC;
#endif

	return 0;
}

int __ast_custom_function_register(struct ast_custom_function *acf, struct ast_module *mod)
{
	struct ast_custom_function *cur;

	if (!acf) {
		return -1;
	}

	acf->mod = mod;
#ifdef AST_XML_DOCS
	acf->docsrc = AST_STATIC_DOC;
#endif

	if (acf_retrieve_docs(acf)) {
		return -1;
	}

	AST_RWLIST_WRLOCK(&acf_root);

	cur = ast_custom_function_find_nolock(acf->name);
	if (cur) {
		ast_log(LOG_ERROR, "Function %s already registered.\n", acf->name);
		AST_RWLIST_UNLOCK(&acf_root);
		return -1;
	}

	/* Store in alphabetical order */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&acf_root, cur, acflist) {
		if (strcmp(acf->name, cur->name) < 0) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(acf, acflist);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	if (!cur) {
		AST_RWLIST_INSERT_TAIL(&acf_root, acf, acflist);
	}

	AST_RWLIST_UNLOCK(&acf_root);

	ast_verb(2, "Registered custom function '" COLORIZE_FMT "'\n", COLORIZE(COLOR_BRCYAN, 0, acf->name));

	return 0;
}

int __ast_custom_function_register_escalating(struct ast_custom_function *acf, enum ast_custom_function_escalation escalation, struct ast_module *mod)
{
	int res;

	res = __ast_custom_function_register(acf, mod);
	if (res != 0) {
		return -1;
	}

	switch (escalation) {
	case AST_CFE_NONE:
		break;
	case AST_CFE_READ:
		acf->read_escalates = 1;
		break;
	case AST_CFE_WRITE:
		acf->write_escalates = 1;
		break;
	case AST_CFE_BOTH:
		acf->read_escalates = 1;
		acf->write_escalates = 1;
		break;
	}

	return 0;
}

/*! \brief return a pointer to the arguments of the function,
 * and terminates the function name with '\\0'
 */
static char *func_args(char *function)
{
	char *args = strchr(function, '(');

	if (!args) {
		ast_log(LOG_WARNING, "Function '%s' doesn't contain parentheses.  Assuming null argument.\n", function);
	} else {
		char *p;
		*args++ = '\0';
		if ((p = strrchr(args, ')'))) {
			*p = '\0';
		} else {
			ast_log(LOG_WARNING, "Can't find trailing parenthesis for function '%s(%s'?\n", function, args);
		}
	}
	return args;
}

void pbx_live_dangerously(int new_live_dangerously)
{
	if (new_live_dangerously && !live_dangerously) {
		ast_log(LOG_WARNING, "Privilege escalation protection disabled!\n"
			"See https://wiki.asterisk.org/wiki/x/1gKfAQ for more details.\n");
	}

	if (!new_live_dangerously && live_dangerously) {
		ast_log(LOG_NOTICE, "Privilege escalation protection enabled.\n");
	}
	live_dangerously = new_live_dangerously;
}

int ast_thread_inhibit_escalations(void)
{
	int *thread_inhibit_escalations;

	thread_inhibit_escalations = ast_threadstorage_get(
		&thread_inhibit_escalations_tl, sizeof(*thread_inhibit_escalations));
	if (thread_inhibit_escalations == NULL) {
		ast_log(LOG_ERROR, "Error inhibiting privilege escalations for current thread\n");
		return -1;
	}

	*thread_inhibit_escalations = 1;
	return 0;
}

int ast_thread_inhibit_escalations_swap(int inhibit)
{
	int *thread_inhibit_escalations;
	int orig;

	thread_inhibit_escalations = ast_threadstorage_get(
		&thread_inhibit_escalations_tl, sizeof(*thread_inhibit_escalations));
	if (thread_inhibit_escalations == NULL) {
		ast_log(LOG_ERROR, "Error swapping privilege escalations inhibit for current thread\n");
		return -1;
	}

	orig = *thread_inhibit_escalations;
	*thread_inhibit_escalations = !!inhibit;
	return orig;
}

/*!
 * \brief Indicates whether the current thread inhibits the execution of
 * dangerous functions.
 *
 * \return True (non-zero) if dangerous function execution is inhibited.
 * \return False (zero) if dangerous function execution is allowed.
 */
static int thread_inhibits_escalations(void)
{
	int *thread_inhibit_escalations;

	thread_inhibit_escalations = ast_threadstorage_get(
		&thread_inhibit_escalations_tl, sizeof(*thread_inhibit_escalations));
	if (thread_inhibit_escalations == NULL) {
		ast_log(LOG_ERROR, "Error checking thread's ability to run dangerous functions\n");
		/* On error, assume that we are inhibiting */
		return 1;
	}

	return *thread_inhibit_escalations;
}

/*!
 * \brief Determines whether execution of a custom function's read function
 * is allowed.
 *
 * \param acfptr Custom function to check
 * \return True (non-zero) if reading is allowed.
 * \return False (zero) if reading is not allowed.
 */
static int is_read_allowed(struct ast_custom_function *acfptr)
{
	if (!acfptr) {
		return 1;
	}

	if (!read_escalates(acfptr)) {
		return 1;
	}

	if (!thread_inhibits_escalations()) {
		return 1;
	}

	if (live_dangerously) {
		/* Global setting overrides the thread's preference */
		ast_debug(2, "Reading %s from a dangerous context\n",
			acfptr->name);
		return 1;
	}

	/* We have no reason to allow this function to execute */
	return 0;
}

/*!
 * \brief Determines whether execution of a custom function's write function
 * is allowed.
 *
 * \param acfptr Custom function to check
 * \return True (non-zero) if writing is allowed.
 * \return False (zero) if writing is not allowed.
 */
static int is_write_allowed(struct ast_custom_function *acfptr)
{
	if (!acfptr) {
		return 1;
	}

	if (!write_escalates(acfptr)) {
		return 1;
	}

	if (!thread_inhibits_escalations()) {
		return 1;
	}

	if (live_dangerously) {
		/* Global setting overrides the thread's preference */
		ast_debug(2, "Writing %s from a dangerous context\n",
			acfptr->name);
		return 1;
	}

	/* We have no reason to allow this function to execute */
	return 0;
}

int ast_func_read(struct ast_channel *chan, const char *function, char *workspace, size_t len)
{
	char *copy = ast_strdupa(function);
	char *args = func_args(copy);
	struct ast_custom_function *acfptr = ast_custom_function_find(copy);
	int res;
	struct ast_module_user *u = NULL;

	if (acfptr == NULL) {
		ast_log(LOG_ERROR, "Function %s not registered\n", copy);
	} else if (!acfptr->read && !acfptr->read2) {
		ast_log(LOG_ERROR, "Function %s cannot be read\n", copy);
	} else if (!is_read_allowed(acfptr)) {
		ast_log(LOG_ERROR, "Dangerous function %s read blocked\n", copy);
	} else if (acfptr->read) {
		if (acfptr->mod) {
			u = __ast_module_user_add(acfptr->mod, chan);
		}
		res = acfptr->read(chan, copy, args, workspace, len);
		if (acfptr->mod && u) {
			__ast_module_user_remove(acfptr->mod, u);
		}

		return res;
	} else {
		struct ast_str *str = ast_str_create(16);

		if (acfptr->mod) {
			u = __ast_module_user_add(acfptr->mod, chan);
		}
		res = acfptr->read2(chan, copy, args, &str, 0);
		if (acfptr->mod && u) {
			__ast_module_user_remove(acfptr->mod, u);
		}
		ast_copy_string(workspace, ast_str_buffer(str), len > ast_str_size(str) ? ast_str_size(str) : len);
		ast_free(str);

		return res;
	}

	return -1;
}

int ast_func_read2(struct ast_channel *chan, const char *function, struct ast_str **str, ssize_t maxlen)
{
	char *copy = ast_strdupa(function);
	char *args = func_args(copy);
	struct ast_custom_function *acfptr = ast_custom_function_find(copy);
	int res;
	struct ast_module_user *u = NULL;

	if (acfptr == NULL) {
		ast_log(LOG_ERROR, "Function %s not registered\n", copy);
	} else if (!acfptr->read && !acfptr->read2) {
		ast_log(LOG_ERROR, "Function %s cannot be read\n", copy);
	} else if (!is_read_allowed(acfptr)) {
		ast_log(LOG_ERROR, "Dangerous function %s read blocked\n", copy);
	} else {
		if (acfptr->mod) {
			u = __ast_module_user_add(acfptr->mod, chan);
		}
		ast_str_reset(*str);
		if (acfptr->read2) {
			/* ast_str enabled */
			res = acfptr->read2(chan, copy, args, str, maxlen);
		} else {
			/* Legacy function pointer, allocate buffer for result */
			int maxsize = ast_str_size(*str);

			if (maxlen > -1) {
				if (maxlen == 0) {
					if (acfptr->read_max) {
						maxsize = acfptr->read_max;
					} else {
						maxsize = VAR_BUF_SIZE;
					}
				} else {
					maxsize = maxlen;
				}
				ast_str_make_space(str, maxsize);
			}
			res = acfptr->read(chan, copy, args, ast_str_buffer(*str), maxsize);
		}
		if (acfptr->mod && u) {
			__ast_module_user_remove(acfptr->mod, u);
		}

		return res;
	}

	return -1;
}

int ast_func_write(struct ast_channel *chan, const char *function, const char *value)
{
	char *copy = ast_strdupa(function);
	char *args = func_args(copy);
	struct ast_custom_function *acfptr = ast_custom_function_find(copy);

	if (acfptr == NULL) {
		ast_log(LOG_ERROR, "Function %s not registered\n", copy);
	} else if (!acfptr->write) {
		ast_log(LOG_ERROR, "Function %s cannot be written to\n", copy);
	} else if (!is_write_allowed(acfptr)) {
		ast_log(LOG_ERROR, "Dangerous function %s write blocked\n", copy);
	} else {
		int res;
		struct ast_module_user *u = NULL;

		if (acfptr->mod) {
			u = __ast_module_user_add(acfptr->mod, chan);
		}
		res = acfptr->write(chan, copy, args, value);
		if (acfptr->mod && u) {
			__ast_module_user_remove(acfptr->mod, u);
		}

		return res;
	}

	return -1;
}

static struct ast_cli_entry acf_cli[] = {
	AST_CLI_DEFINE(handle_show_functions, "Shows registered dialplan functions"),
	AST_CLI_DEFINE(handle_show_function, "Describe a specific dialplan function"),
};

static void unload_pbx_functions_cli(void)
{
	ast_cli_unregister_multiple(acf_cli, ARRAY_LEN(acf_cli));
}

int load_pbx_functions_cli(void)
{
	ast_cli_register_multiple(acf_cli, ARRAY_LEN(acf_cli));
	ast_register_cleanup(unload_pbx_functions_cli);

	return 0;
}
