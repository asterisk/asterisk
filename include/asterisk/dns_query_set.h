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
 * \brief DNS Query Set API
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _ASTERISK_DNS_QUERY_SET_H
#define _ASTERISK_DNS_QUERY_SET_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief Opaque structure for a set of DNS queries */
struct ast_dns_query_set;

/*!
 * \brief Callback invoked when a query set completes
 *
 * \param query_set The DNS query set that was invoked
 */
typedef void (*ast_dns_query_set_callback)(const struct ast_dns_query_set *query_set);

/*!
 * \brief Create a query set to hold queries
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The query set must be released upon cancellation or completion using ao2_ref
 */
struct ast_dns_query_set *ast_dns_query_set_create(void);

/*!
 * \brief Add a query to a query set
 *
 * \param query_set A DNS query set
 * \param name The name of what to resolve
 * \param rr_type Resource record type
 * \param rr_class Resource record class
 *
 * \retval 0 success
 * \retval -1 failure
 */
int ast_dns_query_set_add(struct ast_dns_query_set *query_set, const char *name, int rr_type, int rr_class);

/*!
 * \brief Retrieve the number of queries in a query set
 *
 * \param query_set A DNS query set
 *
 * \return the number of queries
 */
size_t ast_dns_query_set_num_queries(const struct ast_dns_query_set *query_set);

/*!
 * \brief Retrieve a query from a query set
 *
 * \param query_set A DNS query set
 * \param index The index of the query to retrieve
 *
 * \retval non-NULL success
 * \retval NULL failure
 *
 * \note The returned query is only valid for the lifetime of the query set itself
 */
struct ast_dns_query *ast_dns_query_set_get(const struct ast_dns_query_set *query_set, unsigned int index);

/*!
 * \brief Retrieve user specific data from a query set
 *
 * \param query_set A DNS query set
 *
 * \return user specific data
 */
void *ast_dns_query_set_get_data(const struct ast_dns_query_set *query_set);

/*!
 * \brief Asynchronously resolve queries in a query set
 *
 * \param query_set The query set
 * \param callback The callback to invoke upon completion
 * \param data User data to make available on the query set
 *
 * \note The callback will be invoked when all queries have completed
 *
 * \note The user data passed in to this function must be ao2 allocated
 */
void ast_dns_query_set_resolve_async(struct ast_dns_query_set *query_set, ast_dns_query_set_callback callback, void *data);

/*!
 * \brief Synchronously resolve queries in a query set
 *
 * \param query_set The query set
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note This function will return when all queries have been completed
 */
int ast_query_set_resolve(struct ast_dns_query_set *query_set);

/*!
 * \brief Cancel an asynchronous DNS query set resolution
 *
 * \param query_set The DNS query set
 *
 * \retval 0 success (all queries have been cancelled)
 * \retval -1 failure (some queries could not be cancelled)
 *
 * \note If successfully cancelled the callback will not be invoked
 */
int ast_dns_query_set_resolve_cancel(struct ast_dns_query_set *query_set);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DNS_QUERY_SET_H */
