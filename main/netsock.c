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
 * \author Kevin P. Fleming <kpfleming@digium.com>
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__Darwin__)
#include <net/if_dl.h>
#endif

#if defined (SOLARIS)
#include <sys/sockio.h>
#elif defined(HAVE_GETIFADDRS)
#include <ifaddrs.h>
#endif

#include "asterisk/netsock.h"
#include "asterisk/utils.h"
#include "asterisk/astobj.h"

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
	ast_free(netsock);
}

struct ast_netsock_list *ast_netsock_list_alloc(void)
{
	return ast_calloc(1, sizeof(struct ast_netsock_list));
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

struct ast_netsock *ast_netsock_bindaddr(struct ast_netsock_list *list, struct io_context *ioc, struct sockaddr_in *bindaddr, int tos, int cos, ast_io_cb callback, void *data)
{
	int netsocket = -1;
	int *ioref;
	
	struct ast_netsock *ns;
	const int reuseFlag = 1;
	
	/* Make a UDP socket */
	netsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	
	if (netsocket < 0) {
		ast_log(LOG_ERROR, "Unable to create network socket: %s\n", strerror(errno));
		return NULL;
	}
	if (setsockopt(netsocket, SOL_SOCKET, SO_REUSEADDR, (char *)&reuseFlag, sizeof reuseFlag) < 0) {
			ast_log(LOG_WARNING, "Error setting SO_REUSEADDR on sockfd '%d'\n", netsocket);
	}
	if (bind(netsocket,(struct sockaddr *)bindaddr, sizeof(struct sockaddr_in))) {
		ast_log(LOG_ERROR, "Unable to bind to %s port %d: %s\n", ast_inet_ntoa(bindaddr->sin_addr), ntohs(bindaddr->sin_port), strerror(errno));
		close(netsocket);
		return NULL;
	}

	ast_netsock_set_qos(netsocket, tos, cos, "IAX2");
		
	ast_enable_packet_fragmentation(netsocket);

	if (!(ns = ast_calloc(1, sizeof(*ns)))) {
		close(netsocket);
		return NULL;
	}
	
	/* Establish I/O callback for socket read */
	if (!(ioref = ast_io_add(ioc, netsocket, callback, AST_IO_IN, ns))) {
		close(netsocket);
		ast_free(ns);
		return NULL;
	}	
	ASTOBJ_INIT(ns);
	ns->ioref = ioref;
	ns->ioc = ioc;
	ns->sockfd = netsocket;
	ns->data = data;
	memcpy(&ns->bindaddr, bindaddr, sizeof(ns->bindaddr));
	ASTOBJ_CONTAINER_LINK(list, ns);

	return ns;
}

int ast_netsock_set_qos(int netsocket, int tos, int cos, const char *desc)
{
	int res;
	
	if ((res = setsockopt(netsocket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos))))
		ast_log(LOG_WARNING, "Unable to set %s TOS to %d, may be you have no root privileges\n", desc, tos);
	else if (tos)
		ast_verb(2, "Using %s TOS bits %d\n", desc, tos);

#if defined(linux)								
	if (setsockopt(netsocket, SOL_SOCKET, SO_PRIORITY, &cos, sizeof(cos)))
		ast_log(LOG_WARNING, "Unable to set %s CoS to %d\n", desc, cos);
	else if (cos)
		ast_verb(2, "Using %s CoS mark %d\n", desc, cos);
#endif
							
	return res;
}
													

struct ast_netsock *ast_netsock_bind(struct ast_netsock_list *list, struct io_context *ioc, const char *bindinfo, int defaultport, int tos, int cos, ast_io_cb callback, void *data)
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

	host = strsep(&tmp, ":");
	port = tmp;

	if (port && ((portno = atoi(port)) > 0))
		sin.sin_port = htons(portno);

	inet_aton(host, &sin.sin_addr);

	return ast_netsock_bindaddr(list, ioc, &sin, tos, cos, callback, data);
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

void ast_netsock_unref(struct ast_netsock *ns)
{
	ASTOBJ_UNREF(ns, ast_netsock_destroy);
}

char *ast_eid_to_str(char *s, int maxlen, struct ast_eid *eid)
{
	int x;
	char *os = s;
	if (maxlen < 18) {
		if (s && (maxlen > 0))
			*s = '\0';
	} else {
		for (x = 0; x < 5; x++) {
			sprintf(s, "%02x:", eid->eid[x]);
			s += 3;
		}
		sprintf(s, "%02x", eid->eid[5]);
	}
	return os;
}

void ast_set_default_eid(struct ast_eid *eid)
{
#if defined(SIOCGIFHWADDR)
	int s, x = 0;
	char eid_str[20];
	struct ifreq ifr;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return;
	for (x = 0; x < 10; x++) {
		memset(&ifr, 0, sizeof(ifr));
		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "eth%d", x);
		if (ioctl(s, SIOCGIFHWADDR, &ifr))
			continue;
		memcpy(eid, ((unsigned char *)&ifr.ifr_hwaddr) + 2, sizeof(*eid));
		ast_debug(1, "Seeding global EID '%s' from '%s' using 'siocgifhwaddr'\n", ast_eid_to_str(eid_str, sizeof(eid_str), eid), ifr.ifr_name);
		close(s);
		return;
	}
	close(s);
#else
#if defined(ifa_broadaddr) && !defined(SOLARIS)
	char eid_str[20];
	struct ifaddrs *ifap;
	
	if (getifaddrs(&ifap) == 0) {
		struct ifaddrs *p;
		for (p = ifap; p; p = p->ifa_next) {
			if ((p->ifa_addr->sa_family == AF_LINK) && !(p->ifa_flags & IFF_LOOPBACK) && (p->ifa_flags & IFF_RUNNING)) {
				struct sockaddr_dl* sdp = (struct sockaddr_dl*) p->ifa_addr;
				memcpy(&(eid->eid), sdp->sdl_data + sdp->sdl_nlen, 6);
				ast_debug(1, "Seeding global EID '%s' from '%s' using 'getifaddrs'\n", ast_eid_to_str(eid_str, sizeof(eid_str), eid), p->ifa_name);
				freeifaddrs(ifap);
				return;
			}
		}
		freeifaddrs(ifap);
	}
#endif
#endif
	ast_debug(1, "No ethernet interface found for seeding global EID. You will have to set it manually.\n");
}

int ast_str_to_eid(struct ast_eid *eid, const char *s)
{
	unsigned int eid_int[6];
	int x;

	if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &eid_int[0], &eid_int[1], &eid_int[2],
		 &eid_int[3], &eid_int[4], &eid_int[5]) != 6)
		 	return -1;
	
	for (x = 0; x < 6; x++)
		eid->eid[x] = eid_int[x];

	return 0;
}

int ast_eid_cmp(const struct ast_eid *eid1, const struct ast_eid *eid2)
{
	return memcmp(eid1, eid2, sizeof(*eid1));
}
