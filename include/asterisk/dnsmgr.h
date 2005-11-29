/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Background DNS update manager
 * 
 * Copyright (C) 2005, Kevin P. Fleming
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_DNSMGR_H
#define _ASTERISK_DNSMGR_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <netinet/in.h>

struct ast_dnsmgr_entry;

struct ast_dnsmgr_entry *ast_dnsmgr_get(const char *name, struct in_addr *result);

void ast_dnsmgr_release(struct ast_dnsmgr_entry *entry);

int ast_dnsmgr_lookup(const char *name, struct in_addr *result, struct ast_dnsmgr_entry **dnsmgr);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif /* c_plusplus */

#endif /* ASTERISK_DNSMGR_H */
