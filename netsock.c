/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 *
 * \brief Network socket handling
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__)
#include <fcntl.h>
#include <net/route.h>
#endif

#if defined (SOLARIS)
#include <sys/sockio.h>
#endif

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/netsock.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/srv.h"

struct ast_netsock {
	ASTOBJ_COMPONENTS(struct ast_netsock);
	struct sockaddr_in bindaddr;
	int sockfd;
	int *ioref;
	struct io_context *ioc;
	void *data;
};

struct ast_netsock_list {
	ASTOBJ_CONTAINER_COMPONENTS(struct ast_netsock);
	struct io_context *ioc;
};

static void ast_netsock_destroy(struct ast_netsock *netsock)
{
	ast_io_remove(netsock->ioc, netsock->ioref);
	close(netsock->sockfd);
	free(netsock);
}

struct ast_netsock_list *ast_netsock_list_alloc(void)
{
	struct ast_netsock_list *res;

	res = calloc(1, sizeof(*res));

	return res;
}

int ast_netsock_init(struct ast_netsock_list *list)
{
	memset(list, 0, sizeof(*list));
	ASTOBJ_CONTAINER_INIT(list);

	return 0;
}

int ast_netsock_release(struct ast_netsock_list *list)
{
	ASTOBJ_CONTAINER_DESTROYALL(list, ast_netsock_destroy);
	ASTOBJ_CONTAINER_DESTROY(list);

	return 0;
}

struct ast_netsock *ast_netsock_find(struct ast_netsock_list *list,
				     struct sockaddr_in *sa)
{
	struct ast_netsock *sock = NULL;

	ASTOBJ_CONTAINER_TRAVERSE(list, !sock, {
		ASTOBJ_RDLOCK(iterator);
		if (!inaddrcmp(&iterator->bindaddr, sa))
			sock = iterator;
		ASTOBJ_UNLOCK(iterator);
	});

	return sock;
}

struct ast_netsock *ast_netsock_bindaddr(struct ast_netsock_list *list, struct io_context *ioc, struct sockaddr_in *bindaddr, int tos, ast_io_cb callback, void *data)
{
	int netsocket = -1;
	int *ioref;
	char iabuf[INET_ADDRSTRLEN];
	
	struct ast_netsock *ns;
	
	/* Make a UDP socket */
	netsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	
	if (netsocket < 0) {
		ast_log(LOG_ERROR, "Unable to create network socket: %s\n", strerror(errno));
		return NULL;
	}
	if (bind(netsocket,(struct sockaddr *)bindaddr, sizeof(struct sockaddr_in))) {
		ast_log(LOG_ERROR, "Unable to bind to %s port %d: %s\n", ast_inet_ntoa(iabuf, sizeof(iabuf), bindaddr->sin_addr), ntohs(bindaddr->sin_port), strerror(errno));
		close(netsocket);
		return NULL;
	}
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "Using TOS bits %d\n", tos);

	if (setsockopt(netsocket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))) 
		ast_log(LOG_WARNING, "Unable to set TOS to %d\n", tos);

	ns = malloc(sizeof(struct ast_netsock));
	if (ns) {
		/* Establish I/O callback for socket read */
		ioref = ast_io_add(ioc, netsocket, callback, AST_IO_IN, ns);
		if (!ioref) {
			ast_log(LOG_WARNING, "Out of memory!\n");
			close(netsocket);
			free(ns);
			return NULL;
		}	
		ASTOBJ_INIT(ns);
		ns->ioref = ioref;
		ns->ioc = ioc;
		ns->sockfd = netsocket;
		ns->data = data;
		memcpy(&ns->bindaddr, bindaddr, sizeof(ns->bindaddr));
		ASTOBJ_CONTAINER_LINK(list, ns);
	} else {
		ast_log(LOG_WARNING, "Out of memory!\n");
		close(netsocket);
	}

	return ns;
}

struct ast_netsock *ast_netsock_bind(struct ast_netsock_list *list, struct io_context *ioc, const char *bindinfo, int defaultport, int tos, ast_io_cb callback, void *data)
{
	struct sockaddr_in sin;
	char *tmp;
	char *host;
	char *port;
	int portno;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(defaultport);
	tmp = ast_strdupa(bindinfo);
	if (!tmp) {
		ast_log(LOG_WARNING, "Out of memory!\n");
		return NULL;
	}

	host = strsep(&tmp, ":");
	port = tmp;

	if (port && ((portno = atoi(port)) > 0))
		sin.sin_port = htons(portno);

	inet_aton(host, &sin.sin_addr);

	return ast_netsock_bindaddr(list, ioc, &sin, tos, callback, data);
}

int ast_netsock_sockfd(const struct ast_netsock *ns)
{
	return ns ? ns-> sockfd : -1;
}

const struct sockaddr_in *ast_netsock_boundaddr(const struct ast_netsock *ns)
{
	return &(ns->bindaddr);
}

void *ast_netsock_data(const struct ast_netsock *ns)
{
	return ns->data;
}
