/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2012, Digium, Inc.
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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/network.h"

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

#include "asterisk/acl.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/srv.h"

#if (!defined(SOLARIS) && !defined(HAVE_GETIFADDRS))
static int get_local_address(struct ast_sockaddr *ourip)
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
	if (address[0] == '0') {
		score = -25;
	/* RFC 1700 localnet */
	} else if (strncmp(address, "127", 3) == 0) {
		score = -20;
	/* RFC 1918 non-public address space */
	} else if (strncmp(address, "10.", 3) == 0) {
		score = -5;
	/* RFC 1918 non-public address space */
	} else if (strncmp(address, "172", 3) == 0) {
		/* 172.16.0.0 - 172.19.255.255, but not 172.160.0.0 - 172.169.255.255 */
		if (address[4] == '1' && address[5] >= '6' && address[6] == '.') {
			score = -5;
		/* 172.20.0.0 - 172.29.255.255, but not 172.200.0.0 - 172.255.255.255 nor 172.2.0.0 - 172.2.255.255 */
		} else if (address[4] == '2' && address[6] == '.') {
			score = -5;
		/* 172.30.0.0 - 172.31.255.255, but not 172.3.0.0 - 172.3.255.255 */
		} else if (address[4] == '3' && (address[5] == '0' || address[5] == '1')) {
			score = -5;
		/* All other 172 addresses are public */
		} else {
			score = 0;
		}
	/* RFC 2544 Benchmark test range (198.18.0.0 - 198.19.255.255, but not 198.180.0.0 - 198.199.255.255) */
	} else if (strncmp(address, "198.1", 5) == 0 && address[5] >= '8' && address[6] == '.') {
		score = -10;
	/* RFC 1918 non-public address space */
	} else if (strncmp(address, "192.168", 7) == 0) {
		score = -5;
	/* RFC 3330 Zeroconf network */
	} else if (strncmp(address, "169.254", 7) == 0) {
		/*!\note Better score than a test network, but not quite as good as RFC 1918
		 * address space.  The reason is that some Linux distributions automatically
		 * configure a Zeroconf address before trying DHCP, so we want to prefer a
		 * DHCP lease to a Zeroconf address.
		 */
		score = -10;
	/* RFC 3330 Test network */
	} else if (strncmp(address, "192.0.2.", 8) == 0) {
		score = -15;
	/* Every other address should be publically routable */
	} else {
		score = 0;
	}

	if (score > *best_score) {
		*best_score = score;
		memcpy(best_addr, &sin->sin_addr, sizeof(*best_addr));
	}
}

static int get_local_address(struct ast_sockaddr *ourip)
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
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__linux__) || defined(__Darwin__) || defined(__GLIBC__)
	struct ifaddrs *ifap, *ifaphead;
	int rtnerr;
	const struct sockaddr_in *sin;
#endif /* BSD_OR_LINUX */
	struct in_addr best_addr;
	int best_score = -100;
	memset(&best_addr, 0, sizeof(best_addr));

#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__linux__) || defined(__Darwin__) || defined(__GLIBC__)
	rtnerr = getifaddrs(&ifaphead);
	if (rtnerr) {
		perror(NULL);
		return -1;
	}
#endif /* BSD_OR_LINUX */

	s = socket(AF_INET, SOCK_STREAM, 0);

	if (s > 0) {
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__linux__) || defined(__Darwin__) || defined(__GLIBC__)
		for (ifap = ifaphead; ifap; ifap = ifap->ifa_next) {

			if (ifap->ifa_addr && ifap->ifa_addr->sa_family == AF_INET) {
				sin = (const struct sockaddr_in *) ifap->ifa_addr;
				score_address(sin, &best_addr, &best_score);
				res = 0;

				if (best_score == 0) {
					break;
				}
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
		if (!(buf = ast_malloc(bufsz))) {
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
			ast_free(buf);
			return -1;
		}

		for (ifr = ifc.lifc_req, x = 0; x < ifn.lifn_count; ifr++, x++) {
			sa = (struct sockaddr_in *)&(ifr->lifr_addr);
			score_address(sa, &best_addr, &best_score);
			res = 0;

			if (best_score == 0) {
				break;
			}
		}

		ast_free(buf);
#endif /* SOLARIS */

		close(s);
	}
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__) || defined(__linux__) || defined(__Darwin__)
	freeifaddrs(ifaphead);
#endif /* BSD_OR_LINUX */

	if (res == 0 && ourip) {
		ast_sockaddr_setnull(ourip);
		ourip->ss.ss_family = AF_INET;
		((struct sockaddr_in *)&ourip->ss)->sin_addr = best_addr;
	}
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
		ast_free(hal);
	}
}

/* Free ACL list structure */
struct ast_acl_list *ast_free_acl_list(struct ast_acl_list *acl_list)
{
	struct ast_acl *current;

	if (!acl_list) {
		return NULL;
	}

	AST_LIST_LOCK(acl_list);
	while ((current = AST_LIST_REMOVE_HEAD(acl_list, list))) {
		ast_free_ha(current->acl);
		ast_free(current);
	}
	AST_LIST_UNLOCK(acl_list);

	AST_LIST_HEAD_DESTROY(acl_list);
	ast_free(acl_list);

	return NULL;
}

/* Copy HA structure */
void ast_copy_ha(const struct ast_ha *from, struct ast_ha *to)
{
	ast_sockaddr_copy(&to->addr, &from->addr);
	ast_sockaddr_copy(&to->netmask, &from->netmask);
	to->sense = from->sense;
}

/* Create duplicate of ha structure */
static struct ast_ha *ast_duplicate_ha(struct ast_ha *original)
{
	struct ast_ha *new_ha;

	if ((new_ha = ast_calloc(1, sizeof(*new_ha)))) {
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
	struct ast_ha *current, *prev = NULL;

	while (start) {
		current = ast_duplicate_ha(start);  /* Create copy of this object */
		if (prev) {
			prev->next = current;           /* Link previous to this object */
		}

		if (!ret) {
			ret = current;                  /* Save starting point */
		}

		start = start->next;                /* Go to next object */
		prev = current;                     /* Save pointer to this object */
	}
	return ret;                             /* Return start of list */
}

static int acl_new(struct ast_acl **pointer, const char *name) {
	struct ast_acl *acl;
	if (!(acl = ast_calloc(1, sizeof(*acl)))) {
		return 1;
	}

	*pointer = acl;
	ast_copy_string(acl->name, name, ACL_NAME_LENGTH);
	return 0;
}

struct ast_acl_list *ast_duplicate_acl_list(struct ast_acl_list *original)
{
	struct ast_acl_list *clone;
	struct ast_acl *current_cursor;
	struct ast_acl *current_clone;

	/* Early return if we receive a duplication request for a NULL original. */
	if (!original) {
		return NULL;
	}

	if (!(clone = ast_calloc(1, sizeof(*clone)))) {
		ast_log(LOG_WARNING, "Failed to allocate ast_acl_list struct while cloning an ACL\n");
		return NULL;
	}
	AST_LIST_HEAD_INIT(clone);

	AST_LIST_LOCK(original);

	AST_LIST_TRAVERSE(original, current_cursor, list) {
		if ((acl_new(&current_clone, current_cursor->name))) {
			ast_log(LOG_WARNING, "Failed to allocate ast_acl struct while cloning an ACL.");
			continue;
		}

		/* Copy data from original ACL to clone ACL */
		current_clone->acl = ast_duplicate_ha_list(current_cursor->acl);

		current_clone->is_invalid = current_cursor->is_invalid;
		current_clone->is_realtime = current_cursor->is_realtime;

		AST_LIST_INSERT_TAIL(clone, current_clone, list);
	}

	AST_LIST_UNLOCK(original);

	return clone;
}

/*!
 * \brief
 * Parse a netmask in CIDR notation
 *
 * \details
 * For a mask of an IPv4 address, this should be a number between 0 and 32. For
 * a mask of an IPv6 address, this should be a number between 0 and 128. This
 * function creates an IPv6 ast_sockaddr from the given netmask. For masks of
 * IPv4 addresses, this is accomplished by adding 96 to the original netmask.
 *
 * \param[out] addr The ast_sockaddr produced from the CIDR netmask
 * \param is_v4 Tells if the address we are masking is IPv4.
 * \param mask_str The CIDR mask to convert
 * \retval -1 Failure
 * \retval 0 Success
 */
static int parse_cidr_mask(struct ast_sockaddr *addr, int is_v4, const char *mask_str)
{
	int mask;

	if (sscanf(mask_str, "%30d", &mask) != 1) {
		return -1;
	}

	if (is_v4) {
		struct sockaddr_in sin;
		if (mask < 0 || mask > 32) {
			return -1;
		}
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		/* If mask is 0, then we already have the
		 * appropriate all 0s address in sin from
		 * the above memset.
		 */
		if (mask != 0) {
			sin.sin_addr.s_addr = htonl(0xFFFFFFFF << (32 - mask));
		}
		ast_sockaddr_from_sin(addr, &sin);
	} else {
		struct sockaddr_in6 sin6;
		int i;
		if (mask < 0 || mask > 128) {
			return -1;
		}
		memset(&sin6, 0, sizeof(sin6));
		sin6.sin6_family = AF_INET6;
		for (i = 0; i < 4; ++i) {
			/* Once mask reaches 0, we don't have
			 * to explicitly set anything anymore
			 * since sin6 was zeroed out already
			 */
			if (mask > 0) {
				V6_WORD(&sin6, i) = htonl(0xFFFFFFFF << (mask < 32 ? (32 - mask) : 0));
				mask -= mask < 32 ? mask : 32;
			}
		}
		memcpy(&addr->ss, &sin6, sizeof(sin6));
		addr->len = sizeof(sin6);
	}

	return 0;
}

void ast_append_acl(const char *sense, const char *stuff, struct ast_acl_list **path, int *error, int *named_acl_flag)
{
	struct ast_acl *acl = NULL;
	struct ast_acl *current;
	struct ast_acl_list *working_list;

	char *tmp, *list;

	/* If the ACL list is currently uninitialized, it must be initialized. */
	if (*path == NULL) {
		struct ast_acl_list *list;
		list = ast_calloc(1, sizeof(*list));
		if (!list) {
			/* Allocation Error */
			if (error) {
				*error = 1;
			}
			return;
		}

		AST_LIST_HEAD_INIT(list);
		*path = list;
	}

	working_list = *path;

	AST_LIST_LOCK(working_list);

	/* First we need to determine if we will need to add a new ACL node or if we can use an existing one. */
	if (strncasecmp(sense, "a", 1)) {
		/* The first element in the path should be the unnamed, base ACL. If that's the case, we use it. If not,
		 * we have to make one and link it up appropriately. */
		current = AST_LIST_FIRST(working_list);

		if (!current || !ast_strlen_zero(current->name)) {
			if (acl_new(&acl, "")) {
				if (error) {
					*error = 1;
				}
			}
			// Need to INSERT the ACL at the head here.
			AST_LIST_INSERT_HEAD(working_list, acl, list);
		} else {
			/* If the first element was already the unnamed base ACL, we just use that one. */
			acl = current;
		}

		/* With the proper ACL set for modification, we can just pass this off to the ast_ha append function. */
		acl->acl = ast_append_ha(sense, stuff, acl->acl, error);

		AST_LIST_UNLOCK(working_list);
		return;
	}

	/* We are in ACL append mode, so we know we'll be adding one or more named ACLs. */
	list = ast_strdupa(stuff);

	while ((tmp = strsep(&list, ","))) {
		struct ast_ha *named_ha;
		int already_included = 0;

		/* Remove leading whitespace from the string in case the user put spaces between items */
		tmp = ast_skip_blanks(tmp);

		/* The first step is to check for a duplicate */
		AST_LIST_TRAVERSE(working_list, current, list) {
			if (!strcasecmp(current->name, tmp)) { /* ACL= */
				/* Inclusion of the same ACL multiple times isn't a catastrophic error, but it will raise the error flag and skip the entry. */
				ast_log(LOG_ERROR, "Named ACL '%s' occurs multiple times in ACL definition. Please update your ACL configuration.", tmp);
				if (error) {
					*error = 1;
				}
				already_included = 1;
				break;
			}
		}

		if (already_included) {
			continue;
		}

		if (acl_new(&acl, tmp)) {
			/* This is a catastrophic allocation error and we'll return immediately if this happens. */
			if (error) {
				*error = 1;
			}
			AST_LIST_UNLOCK(working_list);
			return;
		}

		/* Attempt to grab the Named ACL we are looking for. */
		named_ha = ast_named_acl_find(tmp, &acl->is_realtime, &acl->is_invalid);

		/* Set the ACL's ast_ha to the duplicated named ACL retrieved above. */
		acl->acl = named_ha;

		/* Raise the named_acl_flag since we are adding a named ACL to the ACL container. */
		if (named_acl_flag) {
			*named_acl_flag = 1;
		}

		/* Now insert the new ACL at the end of the list. */
		AST_LIST_INSERT_TAIL(working_list, acl, list);
	}

	AST_LIST_UNLOCK(working_list);
}

int ast_acl_list_is_empty(struct ast_acl_list *acl_list)
{
	struct ast_acl *head;

	if (!acl_list) {
		return 1;
	}

	AST_LIST_LOCK(acl_list);
	head = AST_LIST_FIRST(acl_list);
	AST_LIST_UNLOCK(acl_list);

	if (head) {
		return 0;
	}

	return 1;
}

struct ast_ha *ast_append_ha(const char *sense, const char *stuff, struct ast_ha *path, int *error)
{
	struct ast_ha *ha;
	struct ast_ha *prev = NULL;
	struct ast_ha *ret;
	char *tmp, *list = ast_strdupa(stuff);
	char *address = NULL, *mask = NULL;
	int addr_is_v4;
	int allowing = strncasecmp(sense, "p", 1) ? AST_SENSE_DENY : AST_SENSE_ALLOW;
	const char *parsed_addr, *parsed_mask;

	ret = path;
	while (path) {
		prev = path;
		path = path->next;
	}

	while ((tmp = strsep(&list, ","))) {
		if (!(ha = ast_calloc(1, sizeof(*ha)))) {
			if (error) {
				*error = 1;
			}
			return ret;
		}

		address = strsep(&tmp, "/");
		if (!address) {
			address = tmp;
		} else {
			mask = tmp;
		}

		if (*address == '!') {
			ha->sense = (allowing == AST_SENSE_DENY) ? AST_SENSE_ALLOW : AST_SENSE_DENY;
			address++;
		} else {
			ha->sense = allowing;
		}

		if (!ast_sockaddr_parse(&ha->addr, address, PARSE_PORT_FORBID)) {
			ast_log(LOG_WARNING, "Invalid IP address: %s\n", address);
			ast_free_ha(ha);
			if (error) {
				*error = 1;
			}
			return ret;
		}

		/* If someone specifies an IPv4-mapped IPv6 address,
		 * we just convert this to an IPv4 ACL
		 */
		if (ast_sockaddr_ipv4_mapped(&ha->addr, &ha->addr)) {
			ast_log(LOG_NOTICE, "IPv4-mapped ACL network address specified. "
				"Converting to an IPv4 ACL network address.\n");
		}

		addr_is_v4 = ast_sockaddr_is_ipv4(&ha->addr);

		if (!mask) {
			parse_cidr_mask(&ha->netmask, addr_is_v4, addr_is_v4 ? "32" : "128");
		} else if (strchr(mask, ':') || strchr(mask, '.')) {
			int mask_is_v4;
			/* Mask is of x.x.x.x or x:x:x:x:x:x:x:x variety */
			if (!ast_sockaddr_parse(&ha->netmask, mask, PARSE_PORT_FORBID)) {
				ast_log(LOG_WARNING, "Invalid netmask: %s\n", mask);
				ast_free_ha(ha);
				if (error) {
					*error = 1;
				}
				return ret;
			}
			/* If someone specifies an IPv4-mapped IPv6 netmask,
			 * we just convert this to an IPv4 ACL
			 */
			if (ast_sockaddr_ipv4_mapped(&ha->netmask, &ha->netmask)) {
				ast_log(LOG_NOTICE, "IPv4-mapped ACL netmask specified. "
					"Converting to an IPv4 ACL netmask.\n");
			}
			mask_is_v4 = ast_sockaddr_is_ipv4(&ha->netmask);
			if (addr_is_v4 ^ mask_is_v4) {
				ast_log(LOG_WARNING, "Address and mask are not using same address scheme.\n");
				ast_free_ha(ha);
				if (error) {
					*error = 1;
				}
				return ret;
			}
		} else if (parse_cidr_mask(&ha->netmask, addr_is_v4, mask)) {
			ast_log(LOG_WARNING, "Invalid CIDR netmask: %s\n", mask);
			ast_free_ha(ha);
			if (error) {
				*error = 1;
			}
			return ret;
		}

		if (ast_sockaddr_apply_netmask(&ha->addr, &ha->netmask, &ha->addr)) {
			/* This shouldn't happen because ast_sockaddr_parse would
			 * have failed much earlier on an unsupported address scheme
			 */
			char *failmask = ast_strdupa(ast_sockaddr_stringify(&ha->netmask));
			char *failaddr = ast_strdupa(ast_sockaddr_stringify(&ha->addr));
			ast_log(LOG_WARNING, "Unable to apply netmask %s to address %s\n", failmask, failaddr);
			ast_free_ha(ha);
			if (error) {
				*error = 1;
			}
			return ret;
		}

		if (prev) {
			prev->next = ha;
		} else {
			ret = ha;
		}
		prev = ha;

		parsed_addr = ast_strdupa(ast_sockaddr_stringify(&ha->addr));
		parsed_mask = ast_strdupa(ast_sockaddr_stringify(&ha->netmask));

		ast_debug(3, "%s/%s sense %u appended to ACL\n", parsed_addr, parsed_mask, ha->sense);
	}

	return ret;
}

void ast_ha_join(const struct ast_ha *ha, struct ast_str **buf)
{
	for (; ha; ha = ha->next) {
		const char *addr = ast_strdupa(ast_sockaddr_stringify_addr(&ha->addr));
		ast_str_append(buf, 0, "%s%s/%s",
			       ha->sense == AST_SENSE_ALLOW ? "!" : "",
			       addr, ast_sockaddr_stringify_addr(&ha->netmask));
		if (ha->next) {
			ast_str_append(buf, 0, ",");
		}
	}
}

void ast_ha_join_cidr(const struct ast_ha *ha, struct ast_str **buf)
{
	for (; ha; ha = ha->next) {
		const char *addr = ast_sockaddr_stringify_addr(&ha->addr);
		ast_str_append(buf, 0, "%s%s/%d",
			       ha->sense == AST_SENSE_ALLOW ? "!" : "",
			       addr, ast_sockaddr_cidr_bits(&ha->netmask));
		if (ha->next) {
			ast_str_append(buf, 0, ",");
		}
	}
}

enum ast_acl_sense ast_apply_acl(struct ast_acl_list *acl_list, const struct ast_sockaddr *addr, const char *purpose)
{
	struct ast_acl *acl;

	/* If the list is NULL, there are no rules, so we'll allow automatically. */
	if (!acl_list) {
		return AST_SENSE_ALLOW;
	}

	AST_LIST_LOCK(acl_list);

	AST_LIST_TRAVERSE(acl_list, acl, list) {
		if (acl->is_invalid) {
			/* In this case, the baseline ACL shouldn't ever trigger this, but if that somehow happens, it'll still be shown. */
			ast_log(LOG_WARNING, "%sRejecting '%s' due to use of an invalid ACL '%s'.\n", purpose ? purpose : "", ast_sockaddr_stringify_addr(addr),
					ast_strlen_zero(acl->name) ? "(BASELINE)" : acl->name);
			AST_LIST_UNLOCK(acl_list);
			return AST_SENSE_DENY;
		}

		if (acl->acl) {
			if (ast_apply_ha(acl->acl, addr) == AST_SENSE_DENY) {
				ast_log(LOG_NOTICE, "%sRejecting '%s' due to a failure to pass ACL '%s'\n", purpose ? purpose : "", ast_sockaddr_stringify_addr(addr),
						ast_strlen_zero(acl->name) ? "(BASELINE)" : acl->name);
				AST_LIST_UNLOCK(acl_list);
				return AST_SENSE_DENY;
			}
		}
	}

	AST_LIST_UNLOCK(acl_list);

	return AST_SENSE_ALLOW;
}

enum ast_acl_sense ast_apply_ha(const struct ast_ha *ha, const struct ast_sockaddr *addr)
{
	/* Start optimistic */
	enum ast_acl_sense res = AST_SENSE_ALLOW;
	const struct ast_ha *current_ha;

	for (current_ha = ha; current_ha; current_ha = current_ha->next) {
		struct ast_sockaddr result;
		struct ast_sockaddr mapped_addr;
		const struct ast_sockaddr *addr_to_use;
#if 0	/* debugging code */
		char iabuf[INET_ADDRSTRLEN];
		char iabuf2[INET_ADDRSTRLEN];
		/* DEBUG */
		ast_copy_string(iabuf, ast_inet_ntoa(sin->sin_addr), sizeof(iabuf));
		ast_copy_string(iabuf2, ast_inet_ntoa(ha->netaddr), sizeof(iabuf2));
		ast_debug(1, "##### Testing %s with %s\n", iabuf, iabuf2);
#endif
		if (ast_sockaddr_is_ipv4(&current_ha->addr)) {
			if (ast_sockaddr_is_ipv6(addr)) {
				if (ast_sockaddr_is_ipv4_mapped(addr)) {
					/* IPv4 ACLs apply to IPv4-mapped addresses */
					if (!ast_sockaddr_ipv4_mapped(addr, &mapped_addr)) {
						ast_log(LOG_ERROR, "%s provided to ast_sockaddr_ipv4_mapped could not be converted. That shouldn't be possible.\n",
							ast_sockaddr_stringify(addr));
						continue;
					}
					addr_to_use = &mapped_addr;
				} else {
					/* An IPv4 ACL does not apply to an IPv6 address */
					continue;
				}
			} else {
				/* Address is IPv4 and ACL is IPv4. No biggie */
				addr_to_use = addr;
			}
		} else {
			if (ast_sockaddr_is_ipv6(addr) && !ast_sockaddr_is_ipv4_mapped(addr)) {
				addr_to_use = addr;
			} else {
				/* Address is IPv4 or IPv4 mapped but ACL is IPv6. Skip */
				continue;
			}
		}

		/* For each rule, if this address and the netmask = the net address
		   apply the current rule */
		if (ast_sockaddr_apply_netmask(addr_to_use, &current_ha->netmask, &result)) {
			/* Unlikely to happen since we know the address to be IPv4 or IPv6 */
			continue;
		}
		if (!ast_sockaddr_cmp_addr(&result, &current_ha->addr)) {
			res = current_ha->sense;
		}
	}
	return res;
}

static int resolve_first(struct ast_sockaddr *addr, const char *name, int flag,
			 int family)
{
	struct ast_sockaddr *addrs;
	int addrs_cnt;

	addrs_cnt = ast_sockaddr_resolve(&addrs, name, flag, family);
	if (addrs_cnt > 0) {
		if (addrs_cnt > 1) {
			ast_debug(1, "Multiple addresses. Using the first only\n");
		}
		ast_sockaddr_copy(addr, &addrs[0]);
		ast_free(addrs);
	} else {
		ast_log(LOG_WARNING, "Unable to lookup '%s'\n", name);
		return -1;
	}

	return 0;
}

int ast_get_ip_or_srv(struct ast_sockaddr *addr, const char *hostname, const char *service)
{
	char srv[256];
	char host[256];
	int srv_ret = 0;
	int tportno;

	if (service) {
		snprintf(srv, sizeof(srv), "%s.%s", service, hostname);
		if ((srv_ret = ast_get_srv(NULL, host, sizeof(host), &tportno, srv)) > 0) {
			hostname = host;
		}
	}

	if (resolve_first(addr, hostname, PARSE_PORT_FORBID, addr->ss.ss_family) != 0) {
		return -1;
	}

	if (srv_ret > 0) {
		ast_sockaddr_set_port(addr, tportno);
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

int ast_str2cos(const char *value, unsigned int *cos)
{
	int fval;

	if (sscanf(value, "%30d", &fval) == 1) {
		if (fval < 8) {
		    *cos = fval;
		    return 0;
		}
	}

	return -1;
}

int ast_str2tos(const char *value, unsigned int *tos)
{
	int fval;
	unsigned int x;

	if (sscanf(value, "%30i", &fval) == 1) {
		*tos = fval & 0xFF;
		return 0;
	}

	for (x = 0; x < ARRAY_LEN(dscp_pool1); x++) {
		if (!strcasecmp(value, dscp_pool1[x].name)) {
			*tos = dscp_pool1[x].space << 2;
			return 0;
		}
	}

	return -1;
}

const char *ast_tos2str(unsigned int tos)
{
	unsigned int x;

	for (x = 0; x < ARRAY_LEN(dscp_pool1); x++) {
		if (dscp_pool1[x].space == (tos >> 2)) {
			return dscp_pool1[x].name;
		}
	}

	return "unknown";
}

int ast_get_ip(struct ast_sockaddr *addr, const char *hostname)
{
	return ast_get_ip_or_srv(addr, hostname, NULL);
}

int ast_ouraddrfor(const struct ast_sockaddr *them, struct ast_sockaddr *us)
{
	int port;
	int s;

	port = ast_sockaddr_port(us);

	if ((s = socket(ast_sockaddr_is_ipv6(them) ? AF_INET6 : AF_INET,
			SOCK_DGRAM, 0)) < 0) {
		ast_log(LOG_ERROR, "Cannot create socket\n");
		return -1;
	}

	if (ast_connect(s, them)) {
		ast_log(LOG_WARNING, "Cannot connect\n");
		close(s);
		return -1;
	}
	if (ast_getsockname(s, us)) {

		ast_log(LOG_WARNING, "Cannot get socket name\n");
		close(s);
		return -1;
	}
	close(s);

	{
		const char *them_addr = ast_strdupa(ast_sockaddr_stringify_addr(them));
		const char *us_addr = ast_strdupa(ast_sockaddr_stringify_addr(us));

		ast_debug(3, "For destination '%s', our source address is '%s'.\n",
				them_addr, us_addr);
	}

	ast_sockaddr_set_port(us, port);

	return 0;
}

int ast_find_ourip(struct ast_sockaddr *ourip, const struct ast_sockaddr *bindaddr, int family)
{
	char ourhost[MAXHOSTNAMELEN] = "";
	struct ast_sockaddr root;
	int res, port = ast_sockaddr_port(ourip);

	/* just use the bind address if it is nonzero */
	if (!ast_sockaddr_is_any(bindaddr)) {
		ast_sockaddr_copy(ourip, bindaddr);
		ast_debug(3, "Attached to given IP address\n");
		return 0;
	}
	/* try to use our hostname */
	if (gethostname(ourhost, sizeof(ourhost) - 1)) {
		ast_log(LOG_WARNING, "Unable to get hostname\n");
	} else {
		if (resolve_first(ourip, ourhost, PARSE_PORT_FORBID, family) == 0) {
			/* reset port since resolve_first wipes this out */
			ast_sockaddr_set_port(ourip, port);
			return 0;
		}
	}
	ast_debug(3, "Trying to check A.ROOT-SERVERS.NET and get our IP address for that connection\n");
	/* A.ROOT-SERVERS.NET. */
	if (!resolve_first(&root, "A.ROOT-SERVERS.NET", PARSE_PORT_FORBID, 0) &&
	    !ast_ouraddrfor(&root, ourip)) {
		/* reset port since resolve_first wipes this out */
		ast_sockaddr_set_port(ourip, port);
		return 0;
	}
	res = get_local_address(ourip);
	ast_sockaddr_set_port(ourip, port);
	return res;
}

