/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, CFWare, LLC
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
#include "asterisk/stasis_channels.h"
#include "asterisk/strings.h"
#include "asterisk/term.h"
#include "asterisk/utils.h"
#include "asterisk/xmldoc.h"
#include "pbx_private.h"

/*! \brief ast_app: A registered application */
struct ast_app {
	int (*execute)(struct ast_channel *chan, const char *data);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(synopsis);     /*!< Synopsis text for 'show applications' */
		AST_STRING_FIELD(description);  /*!< Description (help text) for 'show application &lt;name&gt;' */
		AST_STRING_FIELD(syntax);       /*!< Syntax text for 'core show applications' */
		AST_STRING_FIELD(arguments);    /*!< Arguments description */
		AST_STRING_FIELD(seealso);      /*!< See also */
	);
#ifdef AST_XML_DOCS
	enum ast_doc_src docsrc;		/*!< Where the documentation come from. */
#endif
	AST_RWLIST_ENTRY(ast_app) list;		/*!< Next app in list */
	struct ast_module *module;		/*!< Module this app belongs to */
	char name[0];				/*!< Name of the application */
};

/*!
 * \brief Registered applications container.
 *
 * It is sorted by application name.
 */
static AST_RWLIST_HEAD_STATIC(apps, ast_app);

static struct ast_app *pbx_findapp_nolock(const char *name)
{
	struct ast_app *cur;
	int cmp;

	AST_RWLIST_TRAVERSE(&apps, cur, list) {
		cmp = strcasecmp(name, cur->name);
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

struct ast_app *pbx_findapp(const char *app)
{
	struct ast_app *ret;

	AST_RWLIST_RDLOCK(&apps);
	ret = pbx_findapp_nolock(app);
	AST_RWLIST_UNLOCK(&apps);

	return ret;
}

/*! \brief Dynamically register a new dial plan application */
int ast_register_application2(const char *app, int (*execute)(struct ast_channel *, const char *), const char *synopsis, const char *description, void *mod)
{
	struct ast_app *tmp;
	struct ast_app *cur;
	int length;
#ifdef AST_XML_DOCS
	char *tmpxml;
#endif

	AST_RWLIST_WRLOCK(&apps);
	cur = pbx_findapp_nolock(app);
	if (cur) {
		ast_log(LOG_WARNING, "Already have an application '%s'\n", app);
		AST_RWLIST_UNLOCK(&apps);
		return -1;
	}

	length = sizeof(*tmp) + strlen(app) + 1;

	if (!(tmp = ast_calloc(1, length))) {
		AST_RWLIST_UNLOCK(&apps);
		return -1;
	}

	if (ast_string_field_init(tmp, 128)) {
		AST_RWLIST_UNLOCK(&apps);
		ast_free(tmp);
		return -1;
	}

	strcpy(tmp->name, app);
	tmp->execute = execute;
	tmp->module = mod;

#ifdef AST_XML_DOCS
	/* Try to lookup the docs in our XML documentation database */
	if (ast_strlen_zero(synopsis) && ast_strlen_zero(description)) {
		/* load synopsis */
		tmpxml = ast_xmldoc_build_synopsis("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, synopsis, tmpxml);
		ast_free(tmpxml);

		/* load description */
		tmpxml = ast_xmldoc_build_description("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, description, tmpxml);
		ast_free(tmpxml);

		/* load syntax */
		tmpxml = ast_xmldoc_build_syntax("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, syntax, tmpxml);
		ast_free(tmpxml);

		/* load arguments */
		tmpxml = ast_xmldoc_build_arguments("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, arguments, tmpxml);
		ast_free(tmpxml);

		/* load seealso */
		tmpxml = ast_xmldoc_build_seealso("application", app, ast_module_name(tmp->module));
		ast_string_field_set(tmp, seealso, tmpxml);
		ast_free(tmpxml);
		tmp->docsrc = AST_XML_DOC;
	} else {
#endif
		ast_string_field_set(tmp, synopsis, synopsis);
		ast_string_field_set(tmp, description, description);
#ifdef AST_XML_DOCS
		tmp->docsrc = AST_STATIC_DOC;
	}
#endif

	/* Store in alphabetical order */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&apps, cur, list) {
		if (strcasecmp(tmp->name, cur->name) < 0) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(tmp, list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	if (!cur)
		AST_RWLIST_INSERT_TAIL(&apps, tmp, list);

	ast_verb(2, "Registered application '" COLORIZE_FMT "'\n", COLORIZE(COLOR_BRCYAN, 0, tmp->name));

	AST_RWLIST_UNLOCK(&apps);

	return 0;
}

static void print_app_docs(struct ast_app *aa, int fd)
{
#ifdef AST_XML_DOCS
	char *synopsis = NULL, *description = NULL, *arguments = NULL, *seealso = NULL;
	if (aa->docsrc == AST_XML_DOC) {
		synopsis = ast_xmldoc_printable(S_OR(aa->synopsis, "Not available"), 1);
		description = ast_xmldoc_printable(S_OR(aa->description, "Not available"), 1);
		arguments = ast_xmldoc_printable(S_OR(aa->arguments, "Not available"), 1);
		seealso = ast_xmldoc_printable(S_OR(aa->seealso, "Not available"), 1);
		if (!synopsis || !description || !arguments || !seealso) {
			goto free_docs;
		}
		ast_cli(fd, "\n"
			"%s  -= Info about application '%s' =- %s\n\n"
			COLORIZE_FMT "\n"
			"%s\n\n"
			COLORIZE_FMT "\n"
			"%s\n\n"
			COLORIZE_FMT "\n"
			"%s%s%s\n\n"
			COLORIZE_FMT "\n"
			"%s\n\n"
			COLORIZE_FMT "\n"
			"%s\n",
			ast_term_color(COLOR_MAGENTA, 0), aa->name, ast_term_reset(),
			COLORIZE(COLOR_MAGENTA, 0, "[Synopsis]"), synopsis,
			COLORIZE(COLOR_MAGENTA, 0, "[Description]"), description,
			COLORIZE(COLOR_MAGENTA, 0, "[Syntax]"),
				ast_term_color(COLOR_CYAN, 0), S_OR(aa->syntax, "Not available"), ast_term_reset(),
			COLORIZE(COLOR_MAGENTA, 0, "[Arguments]"), arguments,
			COLORIZE(COLOR_MAGENTA, 0, "[See Also]"), seealso);
free_docs:
		ast_free(synopsis);
		ast_free(description);
		ast_free(arguments);
		ast_free(seealso);
	} else
#endif
	{
		ast_cli(fd, "\n"
			"%s  -= Info about application '%s' =- %s\n\n"
			COLORIZE_FMT "\n"
			COLORIZE_FMT "\n\n"
			COLORIZE_FMT "\n"
			COLORIZE_FMT "\n\n"
			COLORIZE_FMT "\n"
			COLORIZE_FMT "\n\n"
			COLORIZE_FMT "\n"
			COLORIZE_FMT "\n\n"
			COLORIZE_FMT "\n"
			COLORIZE_FMT "\n",
			ast_term_color(COLOR_MAGENTA, 0), aa->name, ast_term_reset(),
			COLORIZE(COLOR_MAGENTA, 0, "[Synopsis]"),
			COLORIZE(COLOR_CYAN, 0, S_OR(aa->synopsis, "Not available")),
			COLORIZE(COLOR_MAGENTA, 0, "[Description]"),
			COLORIZE(COLOR_CYAN, 0, S_OR(aa->description, "Not available")),
			COLORIZE(COLOR_MAGENTA, 0, "[Syntax]"),
			COLORIZE(COLOR_CYAN, 0, S_OR(aa->syntax, "Not available")),
			COLORIZE(COLOR_MAGENTA, 0, "[Arguments]"),
			COLORIZE(COLOR_CYAN, 0, S_OR(aa->arguments, "Not available")),
			COLORIZE(COLOR_MAGENTA, 0, "[See Also]"),
			COLORIZE(COLOR_CYAN, 0, S_OR(aa->seealso, "Not available")));
	}
}

/*
 * \brief 'show application' CLI command implementation function...
 */
static char *handle_show_application(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_app *aa;
	int app, no_registered_app = 1;

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show application";
		e->usage =
			"Usage: core show application <application> [<application> [<application> [...]]]\n"
			"       Describes a particular application.\n";
		return NULL;
	case CLI_GENERATE:
		/*
		 * There is a possibility to show informations about more than one
		 * application at one time. You can type 'show application Dial Echo' and
		 * you will see informations about these two applications ...
		 */
		return ast_complete_applications(a->line, a->word, -1);
	}

	if (a->argc < 4) {
		return CLI_SHOWUSAGE;
	}

	AST_RWLIST_RDLOCK(&apps);
	AST_RWLIST_TRAVERSE(&apps, aa, list) {
		/* Check for each app that was supplied as an argument */
		for (app = 3; app < a->argc; app++) {
			if (strcasecmp(aa->name, a->argv[app])) {
				continue;
			}

			/* We found it! */
			no_registered_app = 0;

			print_app_docs(aa, a->fd);
		}
	}
	AST_RWLIST_UNLOCK(&apps);

	/* we found at least one app? no? */
	if (no_registered_app) {
		ast_cli(a->fd, "Your application(s) is (are) not registered\n");
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

static char *handle_show_applications(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_app *aa;
	int like = 0, describing = 0;
	int total_match = 0;    /* Number of matches in like clause */
	int total_apps = 0;     /* Number of apps registered */

	switch (cmd) {
	case CLI_INIT:
		e->command = "core show applications [like|describing]";
		e->usage =
			"Usage: core show applications [{like|describing} <text>]\n"
			"       List applications which are currently available.\n"
			"       If 'like', <text> will be a substring of the app name\n"
			"       If 'describing', <text> will be a substring of the description\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	AST_RWLIST_RDLOCK(&apps);

	if (AST_RWLIST_EMPTY(&apps)) {
		ast_cli(a->fd, "There are no registered applications\n");
		AST_RWLIST_UNLOCK(&apps);
		return CLI_SUCCESS;
	}

	/* core list applications like <keyword> */
	if ((a->argc == 5) && (!strcmp(a->argv[3], "like"))) {
		like = 1;
	} else if ((a->argc > 4) && (!strcmp(a->argv[3], "describing"))) {
		describing = 1;
	}

	/* core list applications describing <keyword1> [<keyword2>] [...] */
	if ((!like) && (!describing)) {
		ast_cli(a->fd, "    -= Registered Asterisk Applications =-\n");
	} else {
		ast_cli(a->fd, "    -= Matching Asterisk Applications =-\n");
	}

	AST_RWLIST_TRAVERSE(&apps, aa, list) {
		int printapp = 0;
		total_apps++;
		if (like) {
			if (strcasestr(aa->name, a->argv[4])) {
				printapp = 1;
				total_match++;
			}
		} else if (describing) {
			if (aa->description) {
				/* Match all words on command line */
				int i;
				printapp = 1;
				for (i = 4; i < a->argc; i++) {
					if (!strcasestr(aa->description, a->argv[i])) {
						printapp = 0;
					} else {
						total_match++;
					}
				}
			}
		} else {
			printapp = 1;
		}

		if (printapp) {
			ast_cli(a->fd,"  %20s: %s\n", aa->name, aa->synopsis ? aa->synopsis : "<Synopsis not available>");
		}
	}
	if ((!like) && (!describing)) {
		ast_cli(a->fd, "    -= %d Applications Registered =-\n",total_apps);
	} else {
		ast_cli(a->fd, "    -= %d Applications Matching =-\n",total_match);
	}

	AST_RWLIST_UNLOCK(&apps);

	return CLI_SUCCESS;
}

int ast_unregister_application(const char *app)
{
	struct ast_app *cur;
	int cmp;

	/* Anticipate need for conlock in unreference_cached_app(), in order to avoid
	 * possible deadlock with pbx_extension_helper()/pbx_findapp()
	 */
	ast_rdlock_contexts();

	AST_RWLIST_WRLOCK(&apps);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&apps, cur, list) {
		cmp = strcasecmp(app, cur->name);
		if (cmp > 0) {
			continue;
		}
		if (!cmp) {
			/* Found it. */
			unreference_cached_app(cur);
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_verb(2, "Unregistered application '%s'\n", cur->name);
			ast_string_field_free_memory(cur);
			ast_free(cur);
			break;
		}
		/* Not in container. */
		cur = NULL;
		break;
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&apps);

	ast_unlock_contexts();

	return cur ? 0 : -1;
}

char *ast_complete_applications(const char *line, const char *word, int state)
{
	struct ast_app *app;
	int which = 0;
	int cmp;
	char *ret = NULL;
	size_t wordlen = strlen(word);

	AST_RWLIST_RDLOCK(&apps);
	AST_RWLIST_TRAVERSE(&apps, app, list) {
		cmp = strncasecmp(word, app->name, wordlen);
		if (cmp < 0) {
			/* No more matches. */
			break;
		} else if (!cmp) {
			/* Found match. */
			if (state != -1) {
				if (++which <= state) {
					/* Not enough matches. */
					continue;
				}
				ret = ast_strdup(app->name);
				break;
			}
			if (ast_cli_completion_add(ast_strdup(app->name))) {
				break;
			}
		}
	}
	AST_RWLIST_UNLOCK(&apps);

	return ret;
}

const char *app_name(struct ast_app *app)
{
	return app->name;
}

/*
   \note This function is special. It saves the stack so that no matter
   how many times it is called, it returns to the same place */
int pbx_exec(struct ast_channel *c,	/*!< Channel */
	     struct ast_app *app,	/*!< Application */
	     const char *data)		/*!< Data for execution */
{
	int res;
	struct ast_module_user *u = NULL;
	const char *saved_c_appl;
	const char *saved_c_data;

	/* save channel values */
	saved_c_appl= ast_channel_appl(c);
	saved_c_data= ast_channel_data(c);

	ast_channel_lock(c);
	ast_channel_appl_set(c, app->name);
	ast_channel_data_set(c, data);
	ast_channel_publish_snapshot(c);
	ast_channel_unlock(c);

	if (app->module)
		u = __ast_module_user_add(app->module, c);
	res = app->execute(c, S_OR(data, ""));
	if (app->module && u)
		__ast_module_user_remove(app->module, u);
	/* restore channel values */
	ast_channel_appl_set(c, saved_c_appl);
	ast_channel_data_set(c, saved_c_data);
	return res;
}

static struct ast_cli_entry app_cli[] = {
	AST_CLI_DEFINE(handle_show_applications, "Shows registered dialplan applications"),
	AST_CLI_DEFINE(handle_show_application, "Describe a specific dialplan application"),
};

static void unload_pbx_app(void)
{
	ast_cli_unregister_multiple(app_cli, ARRAY_LEN(app_cli));
}

int load_pbx_app(void)
{
	ast_cli_register_multiple(app_cli, ARRAY_LEN(app_cli));
	ast_register_cleanup(unload_pbx_app);

	return 0;
}
