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
 * \brief Bridge PJSIP logging to Asterisk logging.
 * \author David M. Lee, II <dlee@digium.com>
 *
 * PJSIP logging doesn't exactly match Asterisk logging, but mapping the two is
 * not too bad. PJSIP log levels are identified by a single int. Limits are
 * not specified by PJSIP, but their implementation used 1 through 6.
 *
 * The mapping is as follows:
 *  - 0: LOG_ERROR
 *  - 1: LOG_ERROR
 *  - 2: LOG_WARNING
 *  - 3 and above: equivalent to ast_debug(level, ...) for res_pjsip.so
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <pjsip.h>
#include <pj/log.h>

#include "asterisk/logger.h"
#include "asterisk/module.h"

static pj_log_func *log_cb_orig;
static unsigned decor_orig;

static void log_cb(int level, const char *data, int len)
{
	int ast_level;
	/* PJSIP doesn't provide much in the way of source info */
	const char * log_source = "pjsip";
	int log_line = 0;
	const char *log_func = "<?>";
	int mod_level;

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

		/* For levels 3 and up, obey the debug level for res_pjsip */
		mod_level = ast_opt_dbg_module ?
			ast_debug_get_by_module("res_pjsip") : 0;
		if (option_debug < level && mod_level < level) {
			return;
		}
		break;
	}

	/* PJSIP uses indention to indicate function call depth. We'll prepend
	 * log statements with a tab so they'll have a better shot at lining
	 * up */
	ast_log(ast_level, log_source, log_line, log_func, "\t%s\n", data);
}

static int load_module(void)
{
	pj_init();

	decor_orig = pj_log_get_decor();
	log_cb_orig = pj_log_get_log_func();

	ast_debug(3, "Forwarding PJSIP logger to Asterisk logger\n");
	/* SENDER prepends the source to the log message. This could be a
	 * filename, object reference, or simply a string
	 *
	 * INDENT is assumed to be on by most log statements in PJSIP itself.
	 */
	pj_log_set_decor(PJ_LOG_HAS_SENDER | PJ_LOG_HAS_INDENT);
	pj_log_set_log_func(log_cb);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	pj_log_set_log_func(log_cb_orig);
	pj_log_set_decor(decor_orig);

	pj_shutdown();

	return 0;
}

/* While we don't really export global symbols, we want to load before other
 * modules that do */
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP Log Forwarder",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 6,
);
