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
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <asterisk/acl.h>
#include <asterisk/logger.h>
#include <asterisk/channel.h>
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

static ast_mutex_t routeseq_lock = AST_MUTEX_INITIALIZER;
#endif

#define AST_SENSE_DENY			0
#define AST_SENSE_ALLOW			1

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

void ast_free_ha(struct ast_ha *ha)
{
	struct ast_ha *hal;
	while(ha) {
		hal = ha;
		ha = ha->next;
		free(hal);
	}
}

struct ast_ha *ast_append_ha(char *sense, char *stuff, struct ast_ha *path)
{
	struct ast_ha *ha = malloc(sizeof(struct ast_ha));
	char *nm;
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
			if ((sscanf(nm, "%i", &x) == 1) && (x >= 0) && (x <= 32)) {
				y = 0;
				for (z=0;z<x;z++) {
					y >>= 1;
					y |= 0x80000000;
				}
				ha->netmask.s_addr = htonl(y);
			}
		} else if (!inet_aton(nm, &ha->netmask)) {
			ast_log(LOG_WARNING, "%s not a valid netmask\n", nm);
			free(ha);
			return path;
		}
		if (!inet_aton(tmp, &ha->netaddr)) {
			ast_log(LOG_WARNING, "%s not a valid IP\n", tmp);
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
	return ret;
}

int ast_apply_ha(struct ast_ha *ha, struct sockaddr_in *sin)
{
	/* Start optimistic */
	int res = AST_SENSE_ALLOW;
	while(ha) {
		/* For each rule, if this address and the netmask = the net address
		   apply the current rule */
		if ((sin->sin_addr.s_addr & ha->netmask.s_addr) == (ha->netaddr.s_addr))
			res = ha->sense;
		ha = ha->next;
	}
	return res;
}

int ast_get_ip(struct sockaddr_in *sin, char *value)
{
	struct hostent *hp;
	struct ast_hostent ahp;
	hp = ast_gethostbyname(value, &ahp);
	if (hp) {
		memcpy(&sin->sin_addr, hp->h_addr, sizeof(sin->sin_addr));
	} else {
		ast_log(LOG_WARNING, "Unable to lookup '%s'\n", value);
		return -1;
	}
	return 0;
}

/* iface is the interface (e.g. eth0); address is the return value */
int ast_lookup_iface(char *iface, struct in_addr *address) {
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
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__)
	struct sockaddr_in *sin;
	struct sockaddr *sa;
	struct {
		struct	rt_msghdr m_rtm;
		char	m_space[512];
	} m_rtmsg;
	char *cp, *p = ast_strdupa(inet_ntoa(*them));
	int i, l, s, seq, flags;
	pid_t pid = getpid();
	static int routeseq;	/* Protected by "routeseq_lock" mutex */

	memset(us, 0, sizeof(struct in_addr));

	memset(&m_rtmsg, 0, sizeof(m_rtmsg));
	m_rtmsg.m_rtm.rtm_type = RTM_GET;
	m_rtmsg.m_rtm.rtm_flags = RTF_UP | RTF_HOST;
	m_rtmsg.m_rtm.rtm_version = RTM_VERSION;
	ast_mutex_lock(&routeseq_lock);
	seq = ++routeseq;
	ast_mutex_unlock(&routeseq_lock);
	m_rtmsg.m_rtm.rtm_seq = seq;
	m_rtmsg.m_rtm.rtm_addrs = RTA_IFA | RTA_DST;
	m_rtmsg.m_rtm.rtm_msglen = sizeof(struct rt_msghdr) + sizeof(struct sockaddr_in);
	sin = (struct sockaddr_in *)m_rtmsg.m_space;
	sin->sin_family = AF_INET;
	sin->sin_len = sizeof(struct sockaddr_in);
	sin->sin_addr = *them;

	if ((s = socket(PF_ROUTE, SOCK_RAW, 0)) < 0) {
		ast_log(LOG_ERROR, "Error opening routing socket\n");
		return -1;
	}
	flags = fcntl(s, F_GETFL);
	fcntl(s, F_SETFL, flags | O_NONBLOCK);
	if (write(s, (char *)&m_rtmsg, m_rtmsg.m_rtm.rtm_msglen) < 0) {
		ast_log(LOG_ERROR, "Error writing to routing socket: %s\n", strerror(errno));
		close(s);
		return -1;
	}
	do {
		l = read(s, (char *)&m_rtmsg, sizeof(m_rtmsg));
	} while (l > 0 && (m_rtmsg.m_rtm.rtm_seq != 1 || m_rtmsg.m_rtm.rtm_pid != pid));
	if (l < 0) {
		if (errno != EAGAIN)
			ast_log(LOG_ERROR, "Error reading from routing socket\n");
		close(s);
		return -1;
	}
	close(s);

	if (m_rtmsg.m_rtm.rtm_version != RTM_VERSION) {
		ast_log(LOG_ERROR, "Unsupported route socket protocol version\n");
		return -1;
	}

	if (m_rtmsg.m_rtm.rtm_msglen != l)
		ast_log(LOG_WARNING, "Message length mismatch, in packet %d, returned %d\n",
				m_rtmsg.m_rtm.rtm_msglen, l);

	if (m_rtmsg.m_rtm.rtm_errno) {
		ast_log(LOG_ERROR, "RTM_GET got %s (%d)\n",
				strerror(m_rtmsg.m_rtm.rtm_errno), m_rtmsg.m_rtm.rtm_errno);
		return -1;
	}

	cp = (char *)m_rtmsg.m_space;
	if (m_rtmsg.m_rtm.rtm_addrs)
		for (i = 1; i; i <<= 1)
			if (m_rtmsg.m_rtm.rtm_addrs & i) {
				sa = (struct sockaddr *)cp;
				if (i == RTA_IFA && sa->sa_family == AF_INET) {
					sin = (struct sockaddr_in *)sa;
					*us = sin->sin_addr;
					ast_log(LOG_DEBUG, "Found route to %s, output from our address %s.\n", p, inet_ntoa(*us));
					return 0;
				}
				cp += sa->sa_len > 0 ?
					  (1 + ((sa->sa_len - 1) | (sizeof(long) - 1))) :
					  sizeof(long);
			}

	ast_log(LOG_DEBUG, "No route found for address %s!\n", p);
	return -1;
#else
	FILE *PROC;
	unsigned int remote_ip;
	int res = 1;
	char line[256];
	remote_ip = them->s_addr;
	
	PROC = fopen("/proc/net/route","r");
	if (!PROC) {
		bzero(us,sizeof(struct in_addr));
		return -1;
	}
	/* First line contains headers */
	fgets(line,sizeof(line),PROC);

	while (!feof(PROC)) {
		char iface[256];
		unsigned int dest, gateway, mask;
		int i,fieldnum;
		char *fields[40];

		fgets(line,sizeof(line),PROC);

		fieldnum = 0;
		for (i=0;i<sizeof(line);i++) {
			char *offset;

			fields[fieldnum++] = line + i;
			offset = strchr(line + i,'\t');
			if (offset == NULL) {
				/* Exit loop */
				break;
			} else if (fieldnum >= 9) {
				/* Short-circuit: can't break at 8, since the end of field 7 is figured when fieldnum=8 */
				break;
			} else {
				*offset = '\0';
				i = offset - line;
			}
		}
		if (fieldnum >= 8) {

			sscanf(fields[0],"%s",iface);
			sscanf(fields[1],"%x",&dest);
			sscanf(fields[2],"%x",&gateway);
			sscanf(fields[7],"%x",&mask);
#if 0
			printf("Addr: %s %08x Dest: %08x Mask: %08x\n", inet_ntoa(*them), remote_ip, dest, mask);
#endif		
			/* Looks simple, but here is the magic */
			if (((remote_ip & mask) ^ dest) == 0) {
				res = ast_lookup_iface(iface,us);
				break;
			}
		}
	}
	fclose(PROC);
	if (res == 1) {
		ast_log(LOG_WARNING, "Yikes!  No default route?!!\n");
		bzero(us,sizeof(struct in_addr));
		return -2;
	} else if (res) {
		/* We've already warned in subroutine */
		return -1;
 	}
	return 0;
#endif
}
