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
 * \brief Channel monitoring
 */

#ifndef _ASTERISK_MONITOR_H
#define _ASTERISK_MONITOR_H

#include "asterisk/channel.h"
#include "asterisk/optional_api.h"

/* Streams recording control */
#define X_REC_IN	1
#define X_REC_OUT	2
#define X_JOIN		4

/* Start monitoring a channel */
AST_OPTIONAL_API(int, ast_monitor_start,
		 (struct ast_channel *chan, const char *format_spec,
		  const char *fname_base, int need_lock, int stream_action,
		  const char *beep_id),
		 { return -1; });

/* Stop monitoring a channel */
AST_OPTIONAL_API(int, ast_monitor_stop,
		 (struct ast_channel *chan, int need_lock),
		 { return -1; });

/* Change monitoring filename of a channel */
AST_OPTIONAL_API(int, ast_monitor_change_fname,
		 (struct ast_channel *chan, const char *fname_base,
		  int need_lock),
		 { return -1; });

AST_OPTIONAL_API(void, ast_monitor_setjoinfiles,
		 (struct ast_channel *chan, int turnon),
		 { return; });

/* Pause monitoring of a channel */
AST_OPTIONAL_API(int, ast_monitor_pause,
		 (struct ast_channel *chan),
		 { return -1; });

/* Unpause monitoring of a channel */
AST_OPTIONAL_API(int, ast_monitor_unpause,
		 (struct ast_channel *chan),
		 { return -1; });

#endif /* _ASTERISK_MONITOR_H */
