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

/*! pbx.c functions needed by pbx_builtins.c */
int raise_exception(struct ast_channel *chan, const char *reason, int priority);
void wait_for_hangup(struct ast_channel *chan, const void *data);
void set_ext_pri(struct ast_channel *c, const char *exten, int pri);

/*! pbx.c function needed by pbx_app.c */
void unreference_cached_app(struct ast_app *app);

/*! pbx_ignorepat.c */
struct ast_ignorepat;
AST_VECTOR(ast_ignorepats, struct ast_ignorepat *);

struct ast_ignorepat *ignorepat_alloc(const char *value, const char *registrar);
void ignorepat_free(struct ast_ignorepat *ip);

/*! pbx_includes.c */
struct ast_include;
AST_VECTOR(ast_includes, struct ast_include *);

/*! Allocate and initialize an ast_include. */
struct ast_include *include_alloc(const char *value, const char *registrar);
/*! Free an ast_include and associated data. */
void include_free(struct ast_include *inc);
int include_valid(const struct ast_include *inc);
const char *include_rname(const struct ast_include *inc);

/*! pbx_sw.c */
struct ast_sw;
AST_VECTOR(ast_sws, struct ast_sw *);
struct ast_sw *sw_alloc(const char *value, const char *data, int eval, const char *registrar);
void sw_free(struct ast_sw *sw);

/*! pbx_builtins.c functions needed by pbx.c */
int indicate_congestion(struct ast_channel *, const char *);
int indicate_busy(struct ast_channel *, const char *);

/*! pbx_switch.c functions needed by pbx.c */
struct ast_switch *pbx_findswitch(const char *sw);

/*! pbx_app.c functions needed by pbx.c */
const char *app_name(struct ast_app *app);

#define VAR_BUF_SIZE 4096

#endif /* _PBX_PRIVATE_H */
