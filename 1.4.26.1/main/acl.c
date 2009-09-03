/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
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
 * \brief Various sorts of access control
 *
 * \author Mark Spencer <markster@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

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

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__Darwin__)
#include <fcntl.h>
#include <net/route.h>
#endif

#if defined(SOLARIS)
#include <sys/sockio.h>
#include <net/if.h>
#elif defined(HAVE_GETIFADDRS)
#include <ifaddrs.h>
#endif

/* netinet/ip.h may not define the following (See RFCs 791 and 1349) */
#if !defined(IPTOS_LOWCOST)
#define       IPTOS_LOWCOST           0x02
#endif

#if !defined(IPTOS_MINCOST)
#define       IPTOS_MINCOST           IPTOS_LOWCOST
#endif

#include "asterisk/acl.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/srv.h"

struct ast_ha {
	/* Host access rule */
	struct in_addr netaddr;
	struct in_addr netmask;
	int sense;
	struct ast_ha *next;
};

/* Default IP - if not otherwise set, don't breathe garbage */
static struct in_addr __ourip = { .s_addr = 0x00000000, };

struct my_ifreq {
	char ifrn_name[IFNAMSIZ];	/* Interface name, e.g. "eth0", "ppp0", etc.  */
	struct sockaddr_in ifru_addr;
};

#if (!defined(SOLARIS) && !defined(HAVE_GETIFADDRS))
static int get_local_address(struct in_addr *ourip)
{
	return -1;
}
#else
static void score_address(const struct sockaddr_in *sin, struct in_addr *best_addr, int *best_score)
{
	const char *address;
	int score;

	address = ast_inet_ntoa(sin->sin_addr);

	/* RFC 1700 alias for the local network */
	if (address[0] == '0')
		score = -25;
	/* RFC 1700 localnet */
	else if (strncmp(address, "127", 3) == 0)
		score = -20;
	/* RFC 1918 non-public address space */
	else if (strncmp(address, "10.", 3) == 0)
		score = -5;
	/* RFC 1918 non-public address space */
	else if (strncmp(address, "172", 3) == 0) {
		/* 172.16.0.0 - 172.19.255.255, but not 172.160.0.0 - 172.169.255.255 */
		if (address[4] == '1' && address[5] >= '6' && address[6] == '.')
			score = -5;
		/* 172.20.0.0 - 172.29.255.255, but not 172.200.0.0 - 172.255.255.255 nor 172.2.0.0 - 172.2.255.255 */
		else if (address[4] == '2' && address[6] == '.')
			score = -5;
		/* 172.30.0.0 - 172.31.255.255 */
		else if (address[4] == '3' && address[5] <= '1')
			score = -5;
		/* All other 172 addresses are public */
		else
			score = 0;
	/* RFC 2544 Benchmark test range */
	} else if (strncmp(address, "198.1", 5) == 0 && address[5] >= '8' && address[6] == '.')
		score = -10;
	/* RFC 1918 non-public address space */
	else if (strncmp(address, "192.168", 7) == 0)
		score = -5;
	/* RFC 3330 Zeroconf network */
	else if (strncmp(address, "169.254", 7) == 0)
		/*!\note Better score than a test network, but not quite as good as RFC 1918
		 * address space.  The reason is that some Linux distributions automatically
		 * configure a Zeroconf address before trying DHCP, so we want to prefer a
		 * DHCP lease to a Zeroconf address.
		 */
		score = -10;
	/* RFC 3330 Test network */
	else if (strncmp(address, "192.0.2.", 8) == 0)
		score = -15;
	/* Every other address should be publically routable */
	else
		score = 0;

	if (score > *best_score) {
		*best_score = score;
		memcpy(best_addr, &sin->sin_addr, sizeof(*best_addr));
	}
}

static int get_local_address(struct in_addr *ourip)
{
	int s, res = -1;
#ifdef SOLARIS
	struct lifreq *ifr = NULL;
	struct lifnum ifn;
	struct lifconf ifc;
	struct sockaddr_in *sa;
	char *buf = NULL;
	int bufsz, x;
#endif /* SOLARIS */
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__linux__) || defined(__Darwin__)
	struct ifaddrs *ifap, *ifaphead;
	int rtnerr;
	const struct sockaddr_in *sin;
#endif /* BSD_OR_LINUX */
	struct in_addr best_addr;
	int best_score = -100;
	memset(&best_addr, 0, sizeof(best_addr));

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__linux__) || defined(__Darwin__)
	rtnerr = getifaddrs(&ifaphead);
	if (rtnerr) {
		perror(NULL);
		return -1;
	}
#endif /* BSD_OR_LINUX */

	s = socket(AF_INET, SOCK_STREAM, 0);

	if (s > 0) {
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__linux__) || defined(__Darwin__)
		for (ifap = ifaphead; ifap; ifap = ifap->ifa_next) {

			if (ifap->ifa_addr && ifap->ifa_addr->sa_family == AF_INET) {
				sin = (const struct sockaddr_in *) ifap->ifa_addr;
				score_address(sin, &best_addr, &best_score);
				res = 0;

				if (best_score == 0)
					break;
			}
		}
#endif /* BSD_OR_LINUX */

		/* There is no reason whatsoever that this shouldn't work on Linux or BSD also. */
#ifdef SOLARIS
		/* Get a count of interfaces on the machine */
		ifn.lifn_family = AF_INET;
		ifn.lifn_flags = 0;
		ifn.lifn_count = 0;
		if (ioctl(s, SIOCGLIFNUM, &ifn) < 0) {
			close(s);
			return -1;
		}

		bufsz = ifn.lifn_count * sizeof(struct lifreq);
		if (!(buf = malloc(bufsz))) {
			close(s);
			return -1;
		}
		memset(buf, 0, bufsz);

		/* Get a list of interfaces on the machine */
		ifc.lifc_len = bufsz;
		ifc.lifc_buf = buf;
		ifc.lifc_family = AF_INET;
		ifc.lifc_flags = 0;
		if (ioctl(s, SIOCGLIFCONF, &ifc) < 0) {
			close(s);
			free(buf);
			return -1;
		}

		for (ifr = ifc.lifc_req, x = 0; x < ifn.lifn_count; ifr++, x++) {
			sa = (struct sockaddr_in *)&(ifr->lifr_addr);
			score_address(sa, &best_addr, &best_score);
			res = 0;

			if (best_score == 0)
				break;
		}

		free(buf);
#endif /* SOLARIS */
		
		close(s);
	}
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__linux__) || defined(__Darwin__)
	freeifaddrs(ifaphead);
#endif /* BSD_OR_LINUX */

	if (res == 0 && ourip)
		memcpy(ourip, &best_addr, sizeof(*ourip));
	return res;
}
#endif /* HAVE_GETIFADDRS */

/* Free HA structure */
void ast_free_ha(struct ast_ha *ha)
{
	struct ast_ha *hal;
	while (ha) {
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
	struct ast_ha *new_ha;

	if ((new_ha = ast_malloc(sizeof(*new_ha)))) {
		/* Copy from original to new object */
		ast_copy_ha(original, new_ha);
	}

	return new_ha;
}

/* Create duplicate HA link list */
/*  Used in chan_sip2 templates */
struct ast_ha *ast_duplicate_ha_list(struct ast_ha *original)
{
	struct ast_ha *start = original;
	struct ast_ha *ret = NULL;
	struct ast_ha *link, *prev = NULL;

	while (start) {
		link = ast_duplicate_ha(start);  /* Create copy of this object */
		if (prev)
			prev->next = link;		/* Link previous to this object */

		if (!ret)
			ret = link;		/* Save starting point */

		start = start->next;		/* Go to next object */
		prev = link;			/* Save pointer to this object */
	}
	return ret;    			/* Return start of list */
}

struct ast_ha *ast_append_ha(char *sense, char *stuff, struct ast_ha *path)
{
	struct ast_ha *ha;
	char *nm = "255.255.255.255";
	char tmp[256];
	struct ast_ha *prev = NULL;
	struct ast_ha *ret;
	int x, z;
	unsigned int y;

	ret = path;
	while (path) {
		prev = path;
		path = path->next;
	}
	if ((ha = ast_malloc(sizeof(*ha)))) {
		ast_copy_string(tmp, stuff, sizeof(tmp));
		nm = strchr(tmp, '/');
		if (!nm) {
			nm = "255.255.255.255";
		} else {
			*nm = '\0';
			nm++;
		}
		if (!strchr(nm, '.')) {
			if ((sscanf(nm, "%30d", &x) == 1) && (x >= 0) && (x <= 32)) {
				y = 0;
				for (z = 0; z < x; z++) {
					y >>= 1;
					y |= 0x80000000;
				}
				ha->netmask.s_addr = htonl(y);
			}
		} else if (!inet_aton(nm, &ha->netmask)) {
			ast_log(LOG_WARNING, "%s is not a valid netmask\n", nm);
			free(ha);
			return ret;
		}
		if (!inet_aton(tmp, &ha->netaddr)) {
			ast_log(LOG_WARNING, "%s is not a valid IP\n", tmp);
			free(ha);
			return ret;
		}
		ha->netaddr.s_addr &= ha->netmask.s_addr;
		if (!strncasecmp(sense, "p", 1)) {
			ha->sense = AST_SENSE_ALLOW;
		} else {
			ha->sense = AST_SENSE_DENY;
		}
		ha->next = NULL;
		if (prev) {
			prev->next = ha;
		} else {
			ret = ha;
		}
	}
	if (option_debug)
		ast_log(LOG_DEBUG, "%s/%s appended to acl for peer\n", stuff, nm);
	return ret;
}

int ast_apply_ha(struct ast_ha *ha, struct sockaddr_in *sin)
{
	/* Start optimistic */
	int res = AST_SENSE_ALLOW;
	while (ha) {
		char iabuf[INET_ADDRSTRLEN];
		char iabuf2[INET_ADDRSTRLEN];
		/* DEBUG */
		ast_copy_string(iabuf, ast_inet_ntoa(sin->sin_addr), sizeof(iabuf));
		ast_copy_string(iabuf2, ast_inet_ntoa(ha->netaddr), sizeof(iabuf2));
		if (option_debug)
			ast_log(LOG_DEBUG, "##### Testing %s with %s\n", iabuf, iabuf2);
		/* For each rule, if this address and the netmask = the net address
		   apply the current rule */
		if ((sin->sin_addr.s_addr & ha->netmask.s_addr) == ha->netaddr.s_addr)
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

struct dscp_codepoint {
	char *name;
	unsigned int space;
};

/* IANA registered DSCP codepoints */

static const struct dscp_codepoint dscp_pool1[] = {
	{ "CS0", 0x00 },
	{ "CS1", 0x08 },
	{ "CS2", 0x10 },
	{ "CS3", 0x18 },
	{ "CS4", 0x20 },
	{ "CS5", 0x28 },
	{ "CS6", 0x30 },
	{ "CS7", 0x38 },
	{ "AF11", 0x0A },
	{ "AF12", 0x0C },
	{ "AF13", 0x0E },
	{ "AF21", 0x12 },
	{ "AF22", 0x14 },
	{ "AF23", 0x16 },
	{ "AF31", 0x1A },
	{ "AF32", 0x1C },
	{ "AF33", 0x1E },
	{ "AF41", 0x22 },
	{ "AF42", 0x24 },
	{ "AF43", 0x26 },
	{ "EF", 0x2E },
};

int ast_str2tos(const char *value, unsigned int *tos)
{
	int fval;
	unsigned int x;

	if (sscanf(value, "%30i", &fval) == 1) {
		*tos = fval & 0xFF;
		return 0;
	}

	for (x = 0; x < sizeof(dscp_pool1) / sizeof(dscp_pool1[0]); x++) {
		if (!strcasecmp(value, dscp_pool1[x].name)) {
			*tos = dscp_pool1[x].space << 2;
			return 0;
		}
	}

	if (!strcasecmp(value, "lowdelay"))
		*tos = IPTOS_LOWDELAY;
	else if (!strcasecmp(value, "throughput"))
		*tos = IPTOS_THROUGHPUT;
	else if (!strcasecmp(value, "reliability"))
		*tos = IPTOS_RELIABILITY;
	else if (!strcasecmp(value, "mincost"))
		*tos = IPTOS_MINCOST;
	else if (!strcasecmp(value, "none"))
		*tos = 0;
	else
		return -1;

	ast_log(LOG_WARNING, "TOS value %s is deprecated. Please see doc/ip-tos.txt for more information.\n", value);

	return 0;
}

const char *ast_tos2str(unsigned int tos)
{
	unsigned int x;

	switch (tos) {
	case 0:
		return "none";
	case IPTOS_LOWDELAY:
		return "lowdelay";
	case IPTOS_THROUGHPUT:
		return "throughput";
	case IPTOS_RELIABILITY:
		return "reliability";
	case IPTOS_MINCOST:
		return "mincost";
	default:
		for (x = 0; x < sizeof(dscp_pool1) / sizeof(dscp_pool1[0]); x++) {
			if (dscp_pool1[x].space == (tos >> 2))
				return dscp_pool1[x].name;
		}
	}

	return "unknown";
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
	ast_copy_string(ifreq.ifrn_name, iface, sizeof(ifreq.ifrn_name));

	mysock = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
	res = ioctl(mysock, SIOCGIFADDR, &ifreq);

	close(mysock);
	if (res < 0) {
		ast_log(LOG_WARNING, "Unable to get IP of %s: %s\n", iface, strerror(errno));
		memcpy((char *)address, (char *)&__ourip, sizeof(__ourip));
		return -1;
	} else {
		memcpy((char *)address, (char *)&ifreq.ifru_addr.sin_addr, sizeof(ifreq.ifru_addr.sin_addr));
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

int ast_find_ourip(struct in_addr *ourip, struct sockaddr_in bindaddr)
{
	char ourhost[MAXHOSTNAMELEN] = "";
	struct ast_hostent ahp;
	struct hostent *hp;
	struct in_addr saddr;

	/* just use the bind address if it is nonzero */
	if (ntohl(bindaddr.sin_addr.s_addr)) {
		memcpy(ourip, &bindaddr.sin_addr, sizeof(*ourip));
		return 0;
	}
	/* try to use our hostname */
	if (gethostname(ourhost, sizeof(ourhost) - 1)) {
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
	return get_local_address(ourip);
}

