/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief DNS Recurring Resolution API
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _ASTERISK_DNS_RECURRING_H
#define _ASTERISK_DNS_RECURRING_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief Opaque structure for a recurring DNS query */
struct ast_dns_query_recurring;

/*!
 * \brief Asynchronously resolve a DNS query, and continue resolving it according to the lowest TTL available
 *
 * \param name The name of what to resolve
 * \param rr_type Resource record type
 * \param rr_class Resource record class
 * \param callback The callback to invoke upon completion
 * \param data User data to make available on the query
 *
 * \retval non-NULL success - query has been sent for resolution
 * \retval NULL failure
 *
 * \note The user data passed in to this function must be ao2 allocated
 *
 * \note This query will continue to happen according to the lowest TTL unless cancelled using ast_dns_resolve_recurring_cancel
 *
 * \note It is NOT possible for the callback to be invoked concurrently for the query multiple times
 *
 * \note The query will occur when the TTL expires, not before. This means that there is a period of time where the previous
 *       information can be considered stale.
 *
 * \note If the TTL is determined to be 0 (the record specifies 0, or no records exist) this will cease doing a recurring query.
 *       It is the responsibility of the caller to resume querying at an interval they determine.
 */
struct ast_dns_query_recurring *ast_dns_resolve_recurring(const char *name, int rr_type, int rr_class, ast_dns_resolve_callback callback, void *data);

/*!
 * \brief Cancel an asynchronous recurring DNS resolution
 *
 * \param query The DNS query returned from ast_dns_resolve_recurring
 *
 * \retval 0 success - any active query has been cancelled and the query will no longer occur
 * \retval -1 failure - an active query was in progress and could not be cancelled
 *
 * \note If successfully cancelled the callback will not be invoked
 *
 * \note This function does NOT drop your reference to the recurring query, this should be dropped using ao2_ref
 */
int ast_dns_resolve_recurring_cancel(struct ast_dns_query_recurring *recurring);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DNS_RECURRING_H */
