/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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

#ifndef _DNS_CACHE_H_
#define _DNS_CACHE_H_

/*!
 * \page Asterisk DNS Cache
 *
 * This DNS cache currently implements an extremely simplified negative cache.
 * Meaning it keeps track of instances when the domain name failed to resolve.
 *
 * Domains attempting to resolve should first look in the cache to see if a given
 * domain is contained within. If a matching, non-expired entry is found then
 * domain name resolution should be skipped with a failure assumed. If the domain
 * is not found, or the entry has expired then an attempt should made to resolve
 * it. If it fails to resolve the domain name should then be added to the cache.
 *
 * Subsequent failures when resolving the domain name should update the matching
 * entry, thus doubling its expiration timeout.
 *
 * A background task also executes every so often to check for stale records.
 * A stale record is a cache entry that has expired, but has not been updated
 * for a given interval. Any stale records found are removed from the cache.
 */

/*!
 * \brief Check to see if a domain name exists in the cache and whether
 *        or not it has already expired.
 *
 * \param name A domain name
 *
 * \retval 0 if an entry exists and has expired, no zero otherwise
 */
int ast_dns_cache_check(const char *name);

/*!
 * \brief Update a cached item or add a new one.
 *
 * Adds a 'failed to resolve' domain name to the cache. It will remain in the
 * cache until manually removed via a CLI command, the domain successfully
 * resolves, or its expiration is reached and the background task checking for
 * stale entries removes it.
 *
 * - OR -
 *
 * Updates a cached entry increasing its expiration time. Note, the expiration
 * doubles for every update. Thus given a starting expiration of 60 seconds,
 * the first update increases it to 120 seconds, the second update increases it
 * to 240 seconds, and so forth.
 *
 * \param name A domain name
 */
void ast_dns_cache_add_or_update(const char *name);

/*!
 * \brief Delete an item out of the cache
 *
 * \param name A domain name
 */
void ast_dns_cache_delete(const char *name);

#endif /* _DNS_CACHE_H_ */
