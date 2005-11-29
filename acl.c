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

#define AST_SENSE_DENY			0
#define AST_SENSE_ALLOW			1

struct ast_ha {
	/* Host access rule */
	struct in_addr netaddr;
	struct in_addr netmask;
	int sense;
	struct ast_ha *next;
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
		strtok(stuff, "/");
		nm = strtok(NULL, "/");
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
	return NULL;
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

