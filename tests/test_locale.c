/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2009, Digium, Inc.
 *
 * Tilghman Lesher <tlesher AT digium DOT com>
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
 * \brief Locale Test
 *
 * \author\verbatim Tilghman Lesher <tlesher AT digium DOT com> \endverbatim
 * 
 * \ingroup tests
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <sys/types.h>
#include <dirent.h>
#ifndef __USE_GNU
#define __USE_GNU 1
#endif
#include <locale.h>

#include "asterisk/cli.h"
#include "asterisk/linkedlists.h"
#include "asterisk/localtime.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"


static char *handle_cli_test_locales(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	DIR *localedir;
	struct dirent *dent;
	struct ast_tm atm;
	struct timeval tv;
	const char *orig_locale;
	char origlocalformat[200] = "", localformat[200] = "";
	struct test_locales {
		AST_LIST_ENTRY(test_locales) list;
		char *localformat;
		char name[0];
	} *tl = NULL;
	AST_LIST_HEAD_NOLOCK(locales, test_locales) locales;
	int varies = 0, all_successful = 1, count = 0, count_fail = 0;

	switch (cmd) {
	case CLI_INIT:
		e->command = "test locale";
		e->usage = ""
			"Usage: test locale\n"
			"   Test thread safety of locale functions.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != e->args) {
		return CLI_SHOWUSAGE;
	}

	/* First we run a set of tests with the global locale, which isn't thread-safe. */
	if (!(localedir = opendir(
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined( __NetBSD__ ) || defined(__APPLE__)
		"/usr/share/locale"
#else /* Linux */
		"/usr/lib/locale"
#endif
		))) {
		ast_cli(a->fd, "No locales seem to exist on this platform.\n");
		return CLI_SUCCESS;
	}

	tv = ast_tvnow();
	ast_localtime(&tv, &atm, NULL);
	orig_locale = setlocale(LC_ALL, NULL);
	AST_LIST_HEAD_SET_NOLOCK(&locales, NULL);

	/* Get something different, to compare against. */
	ast_strftime(origlocalformat, sizeof(origlocalformat), "%c", &atm);

	while ((dent = readdir(localedir))) {
		size_t namelen;

		if (dent->d_name[0] == '.') {
			continue;
		}

		setlocale(LC_ALL, dent->d_name);
		ast_strftime(localformat, sizeof(localformat), "%c", &atm);

		/* Store values */
		if (!(tl = ast_calloc(1, sizeof(*tl) + strlen(localformat) + (namelen = strlen(dent->d_name)) + 2))) {
			continue;
		}

		strcpy(tl->name, dent->d_name); /* SAFE */
		tl->localformat = tl->name + namelen + 1;
		strcpy(tl->localformat, localformat); /* SAFE */

		AST_LIST_INSERT_TAIL(&locales, tl, list);

		/* Ensure that at least two entries differ, otherwise this test doesn't mean much. */
		if (!varies && strcmp(AST_LIST_FIRST(&locales)->localformat, localformat)) {
			varies = 1;
		}
	}

	setlocale(LC_ALL, orig_locale);

	closedir(localedir);

	if (!varies) {
		if (!strcmp(origlocalformat, localformat)) {
			ast_cli(a->fd, "WARNING: the locales on your system don't differ.  Install more locales if you want this test to mean something.\n");
		}
	}

	orig_locale = ast_setlocale(AST_LIST_FIRST(&locales)->name);

	while ((tl = AST_LIST_REMOVE_HEAD(&locales, list))) {
		ast_setlocale(tl->name);
		ast_strftime(localformat, sizeof(localformat), "%c", &atm);
		if (strcmp(localformat, tl->localformat)) {
			ast_cli(a->fd, "WARNING: locale test fails for locale %s\n", tl->name);
			all_successful = 0;
			count_fail++;
		}
		ast_free(tl);
		count++;
	}

	ast_setlocale(orig_locale);

	if (all_successful) {
		ast_cli(a->fd, "All %d locale tests successful\n", count);
	} else if (count_fail == count && count > 0) {
		ast_cli(a->fd, "No locale tests successful out of %d tries\n", count);
	} else if (count > 0) {
		ast_cli(a->fd, "Partial failure (%d/%d) for a %.0f%% failure rate\n", count_fail, count, count_fail * 100.0 / count);
	} else {
		ast_cli(a->fd, "No locales tested.  Install more locales.\n");
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_locales[] = {
	AST_CLI_DEFINE(handle_cli_test_locales, "Test locales for thread-safety"),
};

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_locales, ARRAY_LEN(cli_locales));
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_locales, ARRAY_LEN(cli_locales));
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Locale tests");
