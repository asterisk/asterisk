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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>

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
	struct ast_ha *prev = NULL;
	struct ast_ha *ret;
	ret = path;
	while(path) {
		prev = path;
		path = path->next;
	}
	if (ha) {
		char *stringp=NULL;
		stringp=stuff;
		strsep(&stringp, "/");
		nm = strsep(&stringp, "/");
		if (!nm)
			nm = "255.255.255.255";
		if (!inet_aton(stuff, &ha->netaddr)) {
			ast_log(LOG_WARNING, "%s not a valid IP\n", stuff);
			free(ha);
			return NULL;
		}
		if (!inet_aton(nm, &ha->netmask)) {
			ast_log(LOG_WARNING, "%s not a valid netmask\n", nm);
			free(ha);
			return NULL;
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
	hp = gethostbyname(value);
	if (hp) {
		memcpy(&sin->sin_addr, hp->h_addr, sizeof(sin->sin_addr));
	} else {
		ast_log(LOG_WARNING, "Unable to lookup '%s'\n", value);
		return -1;
	}
	return 0;
}

int inaddrcmp(struct sockaddr_in *sin1, struct sockaddr_in *sin2)
{
	return ((sin1->sin_addr.s_addr != sin2->sin_addr.s_addr )
			|| (sin1->sin_port != sin2->sin_port));
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
		char iface[8];
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
}


