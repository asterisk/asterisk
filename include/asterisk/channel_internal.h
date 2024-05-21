/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
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
 * \brief Internal channel functions for channel.c to use
 */

#ifndef ASTERISK_CHANNEL_INTERNAL_H
#define ASTERISK_CHANNEL_INTERNAL_H

#define ast_channel_internal_alloc(destructor, assignedid, requestor) __ast_channel_internal_alloc(destructor, assignedid, requestor, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_channel *__ast_channel_internal_alloc(void (*destructor)(void *obj), const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *file, int line, const char *function);
struct ast_channel *__ast_channel_internal_alloc_with_initializers(void (*destructor)(void *obj), const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const struct ast_channel_initializers *initializers, const char *file, int line, const char *function);
void ast_channel_internal_finalize(struct ast_channel *chan);
int ast_channel_internal_is_finalized(struct ast_channel *chan);
void ast_channel_internal_cleanup(struct ast_channel *chan);
int ast_channel_internal_setup_topics(struct ast_channel *chan);

void ast_channel_internal_errno_set(enum ast_channel_error error);
enum ast_channel_error ast_channel_internal_errno(void);
void ast_channel_internal_set_stream_topology(struct ast_channel *chan,
	struct ast_stream_topology *topology);
void ast_channel_internal_set_stream_topology_change_source(
	struct ast_channel *chan, void *change_source);
void ast_channel_internal_swap_stream_topology(struct ast_channel *chan1,
	struct ast_channel *chan2);

#endif /* ASTERISK_CHANNEL_INTERNAL_H */
