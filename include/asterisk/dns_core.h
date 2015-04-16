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
 * \brief Core DNS API
 * \author Joshua Colp <jcolp@digium.com>
 */

#ifndef _ASTERISK_DNS_CORE_H
#define _ASTERISK_DNS_CORE_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*! \brief Opaque structure for an active DNS query */
struct ast_dns_query_active;

/*! \brief Opaque structure for a DNS query */
struct ast_dns_query;

/*!
 * \brief Get the name queried in a DNS query
 *
 * \param query The DNS query
 *
 * \return the name queried
 */
const char *ast_dns_query_get_name(const struct ast_dns_query *query);

/*!
 * \brief Get the record resource type of a DNS query
 *
 * \param query The DNS query
 *
 * \return the record resource type
 */
int ast_dns_query_get_rr_type(const struct ast_dns_query *query);

/*!
 * \brief Get the record resource class of a DNS query
 *
 * \param query The DNS query
 *
 * \return the record resource class
 */
int ast_dns_query_get_rr_class(const struct ast_dns_query *query);

/*!
 * \brief Get the user specific data of a DNS query
 *
 * \param query The DNS query
 *
 * \return the user specific data
 *
 * \note The reference count of the data is NOT incremented on return
 */
void *ast_dns_query_get_data(const struct ast_dns_query *query);

/*! \brief Opaque structure for a DNS query result, guaranteed to be immutable */
struct ast_dns_result;

/*!
 * \brief Get the result information for a DNS query
 *
 * \param query The DNS query
 *
 * \return the DNS result information
 *
 * \note The result is NOT ao2 allocated
 */
struct ast_dns_result *ast_dns_query_get_result(const struct ast_dns_query *query);

/*!
 * \brief Get whether the result is secure or not
 *
 * \param result The DNS result
 *
 * \return whether the result is secure or not
 */
unsigned int ast_dns_result_get_secure(const struct ast_dns_result *result);

/*!
 * \brief Get whether the result is bogus or not
 *
 * \param result The DNS result
 *
 * \return whether the result is bogus or not
 */
unsigned int ast_dns_result_get_bogus(const struct ast_dns_result *result);

/*!
 * \brief Get the error rcode of a DN result
 *
 * \param query The DNS result
 *
 * \return the DNS rcode
 */
unsigned int ast_dns_result_get_rcode(const struct ast_dns_result *result);

/*!
 * \brief Get the canonical name of the result
 *
 * \param result The DNS result
 *
 * \return the canonical name
 */
const char *ast_dns_result_get_canonical(const struct ast_dns_result *result);

/*!
 * \brief Get the first record of a DNS Result
 *
 * \param result The DNS result
 *
 * \return first DNS record
 */
const struct ast_dns_record *ast_dns_result_get_records(const struct ast_dns_result *result);

/*!
 * \brief Get the raw DNS answer from a DNS result
 *
 * \param result The DNS result
 *
 * \return The DNS result
 */
const char *ast_dns_result_get_answer(const struct ast_dns_result *result);

/*!
 * \brief Retrieve the lowest TTL from a result
 *
 * \param result The DNS result
 *
 * \return the lowest TTL
 *
 * \note If no records exist this function will return a TTL of 0
 */
int ast_dns_result_get_lowest_ttl(const struct ast_dns_result *result);

/*!
 * \brief Free the DNS result information
 *
 * \param result The DNS result
 */
void ast_dns_result_free(struct ast_dns_result *result);

/*! \brief Opaque structure for a DNS record */
struct ast_dns_record;

/*!
 * \brief Callback invoked when a query completes
 *
 * \param query The DNS query that was invoked
 */
typedef void (*ast_dns_resolve_callback)(const struct ast_dns_query *query);

/*!
 * \brief Get the resource record type of a DNS record
 *
 * \param record The DNS record
 *
 * \return the resource record type
 */
int ast_dns_record_get_rr_type(const struct ast_dns_record *record);

/*!
 * \brief Get the resource record class of a DNS record
 *
 * \param record The DNS record
 *
 * \return the resource record class
 */
int ast_dns_record_get_rr_class(const struct ast_dns_record *record);

/*!
 * \brief Get the TTL of a DNS record
 *
 * \param record The DNS record
 *
 * \return the TTL
 */
int ast_dns_record_get_ttl(const struct ast_dns_record *record);

/*!
 * \brief Retrieve the raw DNS record
 *
 * \param record The DNS record
 *
 * \return the raw DNS record
 */
const char *ast_dns_record_get_data(const struct ast_dns_record *record);

/*!
 * \brief Retrieve the size of the raw DNS record
 *
 * \param record The DNS record
 *
 * \return the size of the raw DNS record
 */
size_t ast_dns_record_get_data_size(const struct ast_dns_record *record);

/*!
 * \brief Get the next DNS record
 *
 * \param record The current DNS record
 *
 * \return the next DNS record
 */
const struct ast_dns_record *ast_dns_record_get_next(const struct ast_dns_record *record);

/*!
 * \brief Asynchronously resolve a DNS query
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
 * \note The result passed to the callback does not need to be freed
 *
 * \note The user data MUST be an ao2 object
 *
 * \note This function increments the reference count of the user data, it does NOT steal
 *
 * \note The active query must be released upon completion or cancellation using ao2_ref
 */
struct ast_dns_query_active *ast_dns_resolve_async(const char *name, int rr_type, int rr_class, ast_dns_resolve_callback callback, void *data);

/*!
 * \brief Cancel an asynchronous DNS resolution
 *
 * \param active The active DNS query returned from ast_dns_resolve_async
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note If successfully cancelled the callback will not be invoked
 */
int ast_dns_resolve_cancel(struct ast_dns_query_active *active);

/*!
 * \brief Synchronously resolve a DNS query
 *
 * \param name The name of what to resolve
 * \param rr_type Resource record type
 * \param rr_class Resource record class
 * \param result A pointer to hold the DNS result
 *
 * \retval 0 success - query was completed and result is available
 * \retval -1 failure
 */
int ast_dns_resolve(const char *name, int rr_type, int rr_class, struct ast_dns_result **result);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_DNS_CORE_H */
