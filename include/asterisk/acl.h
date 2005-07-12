/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Access Control of various sorts
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_ACL_H
#define _ASTERISK_ACL_H


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <netinet/in.h>
#include "asterisk/io.h"
#include "asterisk/astobj.h"

#define AST_SENSE_DENY                  0
#define AST_SENSE_ALLOW                 1

/* Host based access control */

struct ast_ha;
struct ast_netsock;

struct ast_netsock_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct ast_netsock);
	struct io_context *ioc;
};

extern void ast_free_ha(struct ast_ha *ha);
extern struct ast_ha *ast_append_ha(char *sense, char *stuff, struct ast_ha *path);
extern int ast_apply_ha(struct ast_ha *ha, struct sockaddr_in *sin);
extern int ast_get_ip(struct sockaddr_in *sin, const char *value);
extern int ast_get_ip_or_srv(struct sockaddr_in *sin, const char *value, const char *service);
extern int ast_ouraddrfor(struct in_addr *them, struct in_addr *us);
extern int ast_lookup_iface(char *iface, struct in_addr *address);
extern struct ast_ha *ast_duplicate_ha_list(struct ast_ha *original);
extern int ast_netsock_init(struct ast_netsock_list *list);
extern struct ast_netsock *ast_netsock_bind(struct ast_netsock_list *list, struct io_context *ioc, const char *bindinfo, int defaultport, int tos, ast_io_cb callback, void *data);
extern struct ast_netsock *ast_netsock_bindaddr(struct ast_netsock_list *list, struct io_context *ioc, struct sockaddr_in *bindaddr, int tos, ast_io_cb callback, void *data);
extern int ast_netsock_free(struct ast_netsock_list *list, struct ast_netsock *netsock);
extern int ast_netsock_release(struct ast_netsock_list *list);
extern struct ast_netsock *ast_netsock_find(struct ast_netsock_list *list,
					    struct sockaddr_in *sa);
extern int ast_netsock_sockfd(struct ast_netsock *ns);
extern const struct sockaddr_in *ast_netsock_boundaddr(struct ast_netsock *ns);
extern void *ast_netsock_data(struct ast_netsock *ns);
extern int ast_find_ourip(struct in_addr *ourip, struct sockaddr_in bindaddr);

/*! Compares the source address and port of two sockaddr_in */
static inline int inaddrcmp(struct sockaddr_in *sin1, struct sockaddr_in *sin2)
{
	return ((sin1->sin_addr.s_addr != sin2->sin_addr.s_addr ) 
		|| (sin1->sin_port != sin2->sin_port));
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
