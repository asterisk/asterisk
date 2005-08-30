/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
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

/*
 * Network socket handling
 */

#ifndef _ASTERISK_NETSOCK_H
#define _ASTERISK_NETSOCK_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <netinet/in.h>
#include "asterisk/io.h"
#include "asterisk/astobj.h"

struct ast_netsock;

struct ast_netsock_list;

struct ast_netsock_list *ast_netsock_list_alloc(void);

int ast_netsock_init(struct ast_netsock_list *list);

struct ast_netsock *ast_netsock_bind(struct ast_netsock_list *list, struct io_context *ioc,
				     const char *bindinfo, int defaultport, int tos, ast_io_cb callback, void *data);

struct ast_netsock *ast_netsock_bindaddr(struct ast_netsock_list *list, struct io_context *ioc,
					 struct sockaddr_in *bindaddr, int tos, ast_io_cb callback, void *data);

int ast_netsock_free(struct ast_netsock_list *list, struct ast_netsock *netsock);

int ast_netsock_release(struct ast_netsock_list *list);

struct ast_netsock *ast_netsock_find(struct ast_netsock_list *list,
				     struct sockaddr_in *sa);

int ast_netsock_sockfd(const struct ast_netsock *ns);

const struct sockaddr_in *ast_netsock_boundaddr(const struct ast_netsock *ns);

void *ast_netsock_data(const struct ast_netsock *ns);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_NETSOCK_H */
