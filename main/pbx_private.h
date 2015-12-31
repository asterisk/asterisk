/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015 Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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
 * \brief Private include file for pbx
 */

#ifndef _PBX_PRIVATE_H
#define _PBX_PRIVATE_H

/*! pbx.c functions needed by pbx_builtinns */
int raise_exception(struct ast_channel *chan, const char *reason, int priority);
void wait_for_hangup(struct ast_channel *chan, const void *data);
void set_ext_pri(struct ast_channel *c, const char *exten, int pri);

/*! pbx_builtins.c functions needed by pbx.c */
int indicate_congestion(struct ast_channel *, const char *);
int indicate_busy(struct ast_channel *, const char *);

#define VAR_BUF_SIZE 4096

#endif /* _PBX_PRIVATE_H */
