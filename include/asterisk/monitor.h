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

enum AST_MONITORING_STATE {
	AST_MONITOR_RUNNING,
	AST_MONITOR_PAUSED
};

/* Streams recording control */
#define X_REC_IN	1
#define X_REC_OUT	2
#define X_JOIN		4

/*! Responsible for channel monitoring data */
struct ast_channel_monitor {
	struct ast_filestream *read_stream;
	struct ast_filestream *write_stream;
	char read_filename[FILENAME_MAX];
	char write_filename[FILENAME_MAX];
	char filename_base[FILENAME_MAX];
	int filename_changed;
	char *format;
	int joinfiles;
	enum AST_MONITORING_STATE state;
	int (*stop)(struct ast_channel *chan, int need_lock);
};

/* Start monitoring a channel */
int ast_monitor_start(struct ast_channel *chan, const char
	*format_spec, const char *fname_base, int need_lock, int stream_action)
	attribute_weak;

/* Stop monitoring a channel */
int ast_monitor_stop(struct ast_channel *chan, int need_lock)
	attribute_weak;

/* Change monitoring filename of a channel */
int ast_monitor_change_fname(struct ast_channel *chan, const char *fname_base,
	int need_lock) attribute_weak;

void ast_monitor_setjoinfiles(struct ast_channel *chan, int turnon)
	attribute_weak;

/* Pause monitoring of a channel */
int ast_monitor_pause(struct ast_channel *chan) attribute_weak;

/* Unpause monitoring of a channel */
int ast_monitor_unpause(struct ast_channel *chan) attribute_weak;

#endif /* _ASTERISK_MONITOR_H */
