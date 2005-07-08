/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Various sorts of access control
 * 
 * Copyright (C) 1999, Mark Spencer
 *
 * Mark Spencer <markster@linux-support.net>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
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

#include "asterisk/acl.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/srv.h"

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__)
AST_MUTEX_DEFINE_STATIC(routeseq_lock);
#endif

struct ast_netsock {
	ASTOBJ_COMPONENTS(struct ast_netsock);
	struct sockaddr_in bindaddr;
	int sockfd;
	int *ioref;
	struct io_context *ioc;
	void *data;
};


struct ast_ha {
	/* Host access rule */
	struct in_addr netaddr;
	struct in_addr netmask;
	int sense;
	struct ast_ha *next;
};

/* Default IP - if not otherwise set, don't breathe garbage */
static struct in_addr __ourip = { 0x00000000 };

struct my_ifreq {
	char ifrn_name[IFNAMSIZ];	/* Interface name, e.g. "eth0", "ppp0", etc.  */
	struct sockaddr_in ifru_addr;
};

/* Free HA structure */
void ast_free_ha(struct ast_ha *ha)
{
	struct ast_ha *hal;
	while(ha) {
		hal = ha;
		ha = ha->next;
		free(hal);
	}
}

/* Copy HA structure */
static void ast_copy_ha(struct ast_ha *from, struct ast_ha *to)
{
	memcpy(&to->netaddr, &from->netaddr, sizeof(from->netaddr));
	memcpy(&to->netmask, &from->netmask, sizeof(from->netmask));
	to->sense = from->sense;
}

/* Create duplicate of ha structure */
static struct ast_ha *ast_duplicate_ha(struct ast_ha *original)
{
	struct ast_ha *new_ha = malloc(sizeof(struct ast_ha));

	/* Copy from original to new object */
	ast_copy_ha(original, new_ha); 

	return(new_ha);

}

/* Create duplicate HA link list */
/*  Used in chan_sip2 templates */
struct ast_ha *ast_duplicate_ha_list(struct ast_ha *original)
{
	struct ast_ha *start=original;
	struct ast_ha *ret = NULL;
	struct ast_ha *link,*prev=NULL;

	while(start) {
		link = ast_duplicate_ha(start);  /* Create copy of this object */
		if (prev)
			prev->next = link;		/* Link previous to this object */

		if (!ret) 
			ret = link;		/* Save starting point */

		start = start->next;		/* Go to next object */
		prev = link;			/* Save pointer to this object */
	}
	return (ret);    			/* Return start of list */
}

struct ast_ha *ast_append_ha(char *sense, char *stuff, struct ast_ha *path)
{
	struct ast_ha *ha = malloc(sizeof(struct ast_ha));
	char *nm="255.255.255.255";
	char tmp[256] = "";
	struct ast_ha *prev = NULL;
	struct ast_ha *ret;
	int x,z;
	unsigned int y;
	ret = path;
	while(path) {
		prev = path;
		path = path->next;
	}
	if (ha) {
		strncpy(tmp, stuff, sizeof(tmp) - 1);
		nm = strchr(tmp, '/');
		if (!nm)
			nm = "255.255.255.255";
		else {
			*nm = '\0';
			nm++;
		}
		if (!strchr(nm, '.')) {
			if ((sscanf(nm, "%d", &x) == 1) && (x >= 0) && (x <= 32)) {
				y = 0;
				for (z=0;z<x;z++) {
					y >>= 1;
					y |= 0x80000000;
				}
				ha->netmask.s_addr = htonl(y);
			}
		} else if (!inet_aton(nm, &ha->netmask)) {
			ast_log(LOG_WARNING, "%s is not a valid netmask\n", nm);
			free(ha);
			return path;
		}
		if (!inet_aton(tmp, &ha->netaddr)) {
			ast_log(LOG_WARNING, "%s is not a valid IP\n", tmp);
			free(ha);
			return path;
		}
		ha->netaddr.s_addr &= ha->netmask.s_addr;
		if (!strncasecmp(sense, "p", 1)) {
			ha->sense = AST_SENSE_ALLOW;
		} else {
			ha->sense = AST_SENSE_DENY;
		}
		ha->next = NULL;
		if (prev)
			prev->next = ha;
		else
			ret = ha;
	}
	ast_log(LOG_DEBUG, "%s/%s appended to acl for peer\n",stuff, nm);
	return ret;
}

int ast_apply_ha(struct ast_ha *ha, struct sockaddr_in *sin)
{
	/* Start optimistic */
	int res = AST_SENSE_ALLOW;
	while(ha) {
		char iabuf[INET_ADDRSTRLEN];
		char iabuf2[INET_ADDRSTRLEN];
		/* DEBUG */
		ast_log(LOG_DEBUG,
			"##### Testing %s with %s\n",
			ast_inet_ntoa(iabuf, sizeof(iabuf), sin->sin_addr),
			ast_inet_ntoa(iabuf2, sizeof(iabuf2), ha->netaddr));
		/* For each rule, if this address and the netmask = the net address
		   apply the current rule */
		if ((sin->sin_addr.s_addr & ha->netmask.s_addr) == (ha->netaddr.s_addr))
			res = ha->sense;
		ha = ha->next;
	}
	return res;
}

int ast_get_ip_or_srv(struct sockaddr_in *sin, const char *value, const char *service)
{
	struct hostent *hp;
	struct ast_hostent ahp;
	char srv[256];
	char host[256];
	int tportno = ntohs(sin->sin_port);
	if (inet_aton(value, &sin->sin_addr))
		return 0;
	if (service) {
		snprintf(srv, sizeof(srv), "%s.%s", service, value);
		if (ast_get_srv(NULL, host, sizeof(host), &tportno, srv) > 0) {
			sin->sin_port = htons(tportno);
			value = host;
		}
	}
	hp = ast_gethostbyname(value, &ahp);
	if (hp) {
		memcpy(&sin->sin_addr, hp->h_addr, sizeof(sin->sin_addr));
	} else {
		ast_log(LOG_WARNING, "Unable to lookup '%s'\n", value);
		return -1;
	}
	return 0;
}

int ast_get_ip(struct sockaddr_in *sin, const char *value)
{
	return ast_get_ip_or_srv(sin, value, NULL);
}

/* iface is the interface (e.g. eth0); address is the return value */
int ast_lookup_iface(char *iface, struct in_addr *address) 
{
	int mysock, res = 0;
	struct my_ifreq ifreq;

	memset(&ifreq, 0, sizeof(ifreq));
	strncpy(ifreq.ifrn_name,iface,sizeof(ifreq.ifrn_name) - 1);

	mysock = socket(PF_INET,SOCK_DGRAM,IPPROTO_IP);
	res = ioctl(mysock,SIOCGIFADDR,&ifreq);

	close(mysock);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to get IP of %s: %s\n", iface, strerror(errno));
		memcpy((char *)address,(char *)&__ourip,sizeof(__ourip));
		return -1;
	} else {
		memcpy((char *)address,(char *)&ifreq.ifru_addr.sin_addr,sizeof(ifreq.ifru_addr.sin_addr));
		return 0;
	}
}

int ast_ouraddrfor(struct in_addr *them, struct in_addr *us)
{
	int s;
	struct sockaddr_in sin;
	socklen_t slen;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		ast_log(LOG_WARNING, "Cannot create socket\n");
		return -1;
	}
	sin.sin_family = AF_INET;
	sin.sin_port = 5060;
	sin.sin_addr = *them;
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin))) {
		ast_log(LOG_WARNING, "Cannot connect\n");
		close(s);
		return -1;
	}
	slen = sizeof(sin);
	if (getsockname(s, (struct sockaddr *)&sin, &slen)) {
		ast_log(LOG_WARNING, "Cannot get socket name\n");
		close(s);
		return -1;
	}
	close(s);
	*us = sin.sin_addr;
	return 0;
}

int ast_netsock_sockfd(struct ast_netsock *ns)
{
	if (ns)
		return ns->sockfd;
	return -1;
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

static void ast_netsock_destroy(struct ast_netsock *netsock)
{
	ast_io_remove(netsock->ioc, netsock->ioref);
	close(netsock->sockfd);
	free(netsock);
}

int ast_netsock_init(struct ast_netsock_list *list)
{
	memset(list, 0, sizeof(struct ast_netsock_list));
	ASTOBJ_CONTAINER_INIT(list);
	return 0;
}

int ast_netsock_release(struct ast_netsock_list *list)
{
	ASTOBJ_CONTAINER_DESTROYALL(list, ast_netsock_destroy);
	ASTOBJ_CONTAINER_DESTROY(list);
	return 0;
}

const struct sockaddr_in *ast_netsock_boundaddr(struct ast_netsock *ns)
{
	return &(ns->bindaddr);
}

void *ast_netsock_data(struct ast_netsock *ns)
{
	return ns->data;
}

struct ast_netsock *ast_netsock_bind(struct ast_netsock_list *list, struct io_context *ioc, const char *bindinfo, int defaultport, int tos, ast_io_cb callback, void *data)
{
	struct sockaddr_in sin;
	char *tmp;
	char *port;
	int portno;
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(defaultport);
	tmp = ast_strdupa(bindinfo);
	if (tmp) {
		port = strchr(tmp, ':');
		if (port) {
			*port = '\0';
			port++;
			if ((portno = atoi(port)) > 0) 
				sin.sin_port = htons(portno);
		}
		inet_aton(tmp, &sin.sin_addr);
		return ast_netsock_bindaddr(list, ioc, &sin, tos, callback, data);
	} else
		ast_log(LOG_WARNING, "Out of memory!\n");
	return NULL;
}

int ast_find_ourip(struct in_addr *ourip, struct sockaddr_in bindaddr)
{
	char ourhost[MAXHOSTNAMELEN]="";
	struct ast_hostent ahp;
	struct hostent *hp;
	struct in_addr saddr;

	/* just use the bind address if it is nonzero */
	if (ntohl(bindaddr.sin_addr.s_addr)) {
		memcpy(ourip, &bindaddr.sin_addr, sizeof(*ourip));
		return 0;
	}
	/* try to use our hostname */
	if (gethostname(ourhost, sizeof(ourhost)-1)) {
		ast_log(LOG_WARNING, "Unable to get hostname\n");
	} else {
		hp = ast_gethostbyname(ourhost, &ahp);
		if (hp) {
			memcpy(ourip, hp->h_addr, sizeof(*ourip));
			return 0;
		}
	}
	/* A.ROOT-SERVERS.NET. */
	if (inet_aton("198.41.0.4", &saddr) && !ast_ouraddrfor(&saddr, ourip))
		return 0;
	return -1;
}

