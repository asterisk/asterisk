/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 * \brief loadable MixMonitor functionality
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/lock.h"
#include "asterisk/logger.h"
#include "asterisk/mixmonitor.h"
#include "asterisk/utils.h"
#include "asterisk/channel.h"

AST_RWLOCK_DEFINE_STATIC(mixmonitor_lock);

static struct ast_mixmonitor_methods mixmonitor_methods;
static int table_loaded = 0;

int ast_set_mixmonitor_methods(struct ast_mixmonitor_methods *method_table)
{
	SCOPED_WRLOCK(lock, &mixmonitor_lock);

	if (table_loaded) {
		/* If mixmonitor methods have already been provided, reject the new set */
		ast_log(LOG_ERROR, "Tried to set mixmonitor methods, but something else has already provided them.\n");
		return -1;
	}

	mixmonitor_methods = *method_table;

	table_loaded = 1;
	return 0;
}

void ast_clear_mixmonitor_methods(void)
{
	SCOPED_WRLOCK(lock, &mixmonitor_lock);

	if (table_loaded) {
		memset(&mixmonitor_methods, 0, sizeof(mixmonitor_methods));
		table_loaded = 0;
	}
}

int ast_start_mixmonitor(struct ast_channel *chan, const char *filename, const char *options)
{
	SCOPED_RDLOCK(lock, &mixmonitor_lock);

	if (!mixmonitor_methods.start) {
		ast_log(LOG_ERROR, "No loaded module currently provides MixMonitor starting functionality.\n");
		return -1;
	}

	return mixmonitor_methods.start(chan, filename, options);
}

int ast_stop_mixmonitor(struct ast_channel *chan, const char *mixmon_id)
{
	SCOPED_RDLOCK(lock, &mixmonitor_lock);

	if (!mixmonitor_methods.stop) {
		ast_log(LOG_ERROR, "No loaded module currently provides MixMonitor stopping functionality.\n");
		return -1;
	}

	return mixmonitor_methods.stop(chan, mixmon_id);
}
