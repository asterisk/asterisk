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

#define ast_channel_internal_alloc(destructor) __ast_channel_internal_alloc(destructor, __FILE__, __LINE__, __PRETTY_FUNCTION__)
struct ast_channel *__ast_channel_internal_alloc(void (*destructor)(void *obj), const char *file, int line, const char *function);
void ast_channel_internal_finalize(struct ast_channel *chan);
int ast_channel_internal_is_finalized(struct ast_channel *chan);
void ast_channel_internal_cleanup(struct ast_channel *chan);

