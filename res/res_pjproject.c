/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief Bridge PJPROJECT logging to Asterisk logging.
 * \author David M. Lee, II <dlee@digium.com>
 *
 * PJPROJECT logging doesn't exactly match Asterisk logging, but mapping the two is
 * not too bad. PJPROJECT log levels are identified by a single int. Limits are
 * not specified by PJPROJECT, but their implementation used 1 through 6.
 *
 * The mapping is as follows:
 *  - 0: LOG_ERROR
 *  - 1: LOG_ERROR
 *  - 2: LOG_WARNING
 *  - 3 and above: equivalent to ast_debug(level, ...) for res_pjproject.so
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <stdarg.h>
#include <pjlib.h>
#include <pjsip.h>
#include <pj/log.h>

#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/res_pjproject.h"
#include "asterisk/vector.h"

static pj_log_func *log_cb_orig;
static unsigned decor_orig;

static AST_VECTOR(buildopts, char *) buildopts;

/*! Protection from other log intercept instances.  There can be only one at a time. */
AST_MUTEX_DEFINE_STATIC(pjproject_log_intercept_lock);

struct pjproject_log_intercept_data {
	pthread_t thread;
	int fd;
};

static struct pjproject_log_intercept_data pjproject_log_intercept = {
	.thread = AST_PTHREADT_NULL,
	.fd = -1,
};

static void log_forwarder(int level, const char *data, int len)
{
	int ast_level;
	/* PJPROJECT doesn't provide much in the way of source info */
	const char * log_source = "pjproject";
	int log_line = 0;
	const char *log_func = "<?>";
	int mod_level;

	if (pjproject_log_intercept.fd != -1
		&& pjproject_log_intercept.thread == pthread_self()) {
		/*
		 * We are handling a CLI command intercepting PJPROJECT
		 * log output.
		 */
		ast_cli(pjproject_log_intercept.fd, "%s\n", data);
		return;
	}

	/* Lower number indicates higher importance */
	switch (level) {
	case 0: /* level zero indicates fatal error, according to docs */
	case 1: /* 1 seems to be used for errors */
		ast_level = __LOG_ERROR;
		break;
	case 2: /* 2 seems to be used for warnings and errors */
		ast_level = __LOG_WARNING;
		break;
	default:
		ast_level = __LOG_DEBUG;

		/* For levels 3 and up, obey the debug level for res_pjproject */
		mod_level = ast_opt_dbg_module ?
			ast_debug_get_by_module("res_pjproject") : 0;
		if (option_debug < level && mod_level < level) {
			return;
		}
		break;
	}

	/* PJPROJECT uses indention to indicate function call depth. We'll prepend
	 * log statements with a tab so they'll have a better shot at lining
	 * up */
	ast_log(ast_level, log_source, log_line, log_func, "\t%s\n", data);
}

static void capture_buildopts_cb(int level, const char *data, int len)
{
	if (strstr(data, "Teluu") || strstr(data, "Dumping")) {
		return;
	}

	AST_VECTOR_ADD_SORTED(&buildopts, ast_strdup(ast_skip_blanks(data)), strcmp);
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"
int ast_pjproject_get_buildopt(char *option, char *format_string, ...)
{
	int res = 0;
	char *format_temp;
	int i;

	format_temp = ast_alloca(strlen(option) + strlen(" : ") + strlen(format_string) + 1);
	sprintf(format_temp, "%s : %s", option, format_string);

	for (i = 0; i < AST_VECTOR_SIZE(&buildopts); i++) {
		va_list arg_ptr;
		va_start(arg_ptr, format_string);
		res = vsscanf(AST_VECTOR_GET(&buildopts, i), format_temp, arg_ptr);
		va_end(arg_ptr);
		if (res) {
			break;
		}
	}

	return res;
}
#pragma GCC diagnostic warning "-Wformat-nonliteral"

void ast_pjproject_log_intercept_begin(int fd)
{
	/* Protect from other CLI instances trying to do this at the same time. */
	ast_mutex_lock(&pjproject_log_intercept_lock);

	pjproject_log_intercept.thread = pthread_self();
	pjproject_log_intercept.fd = fd;
}

void ast_pjproject_log_intercept_end(void)
{
	pjproject_log_intercept.fd = -1;
	pjproject_log_intercept.thread = AST_PTHREADT_NULL;

	ast_mutex_unlock(&pjproject_log_intercept_lock);
}

void ast_pjproject_ref(void)
{
	ast_module_ref(ast_module_info->self);
}

void ast_pjproject_unref(void)
{
	ast_module_unref(ast_module_info->self);
}

static char *handle_pjproject_show_buildopts(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjproject show buildopts";
		e->usage =
			"Usage: pjproject show buildopts\n"
			"       Show the compile time config of the pjproject that Asterisk is\n"
			"       running against.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "PJPROJECT compile time config currently running against:\n");

	for (i = 0; i < AST_VECTOR_SIZE(&buildopts); i++) {
		ast_cli(a->fd, "%s\n", AST_VECTOR_GET(&buildopts, i));
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry pjproject_cli[] = {
	AST_CLI_DEFINE(handle_pjproject_show_buildopts, "Show the compiled config of the pjproject in use"),
};

static int load_module(void)
{
	ast_debug(3, "Starting PJPROJECT logging to Asterisk logger\n");

	pj_init();

	decor_orig = pj_log_get_decor();
	log_cb_orig = pj_log_get_log_func();

	if (AST_VECTOR_INIT(&buildopts, 64)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	/*
	 * On startup, we want to capture the dump once and store it.
	 */
	pj_log_set_log_func(capture_buildopts_cb);
	pj_log_set_decor(0);
	pj_dump_config();
	pj_log_set_decor(PJ_LOG_HAS_SENDER | PJ_LOG_HAS_INDENT);
	pj_log_set_log_func(log_forwarder);

	ast_cli_register_multiple(pjproject_cli, ARRAY_LEN(pjproject_cli));

	return AST_MODULE_LOAD_SUCCESS;
}

#define NOT_EQUALS(a, b) (a != b)

static int unload_module(void)
{
	ast_cli_unregister_multiple(pjproject_cli, ARRAY_LEN(pjproject_cli));
	pj_log_set_log_func(log_cb_orig);
	pj_log_set_decor(decor_orig);

	AST_VECTOR_REMOVE_CMP_UNORDERED(&buildopts, NULL, NOT_EQUALS, ast_free);
	AST_VECTOR_FREE(&buildopts);

	ast_debug(3, "Stopped PJPROJECT logging to Asterisk logger\n");

	pj_shutdown();

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJPROJECT Log and Utility Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 6,
);
