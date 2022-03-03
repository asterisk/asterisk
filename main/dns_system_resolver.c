/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Digium, Inc.
 *
 * Ashley Sanders <asanders@digium.com>
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
 * \brief The default DNS resolver for Asterisk.
 *
 * \arg See also \ref res_resolver_unbound.c
 *
 * \author Ashley Sanders <asanders@digium.com>
 */

#include "asterisk.h"

#include "asterisk/_private.h"
#include "asterisk/astobj2.h"
#include "asterisk/dns.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_resolver.h"
#include "asterisk/linkedlists.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/utils.h"

/*! \brief The consideration priority for this resolver implementation. */
#define DNS_SYSTEM_RESOLVER_PRIORITY INT_MAX

/*! \brief Resolver return code upon success. */
#define DNS_SYSTEM_RESOLVER_SUCCESS 0

/*! \brief Resolver return code upon failure. */
#define DNS_SYSTEM_RESOLVER_FAILURE -1


static int dns_system_resolver_add_record(void *context, unsigned char *record, int record_len, int ttl);
static int dns_system_resolver_cancel(struct ast_dns_query *query);
static void dns_system_resolver_destroy(void);
static int dns_system_resolver_process_query(void *data);
static int dns_system_resolver_resolve(struct ast_dns_query *query);
static int dns_system_resolver_set_response(void *context, unsigned char *dns_response, int dns_response_len, int rcode);


/*! \brief The task processor to use for making DNS searches asynchronous. */
static struct ast_taskprocessor *dns_system_resolver_tp;

/*! \brief The base definition for the dns_system_resolver */
struct ast_dns_resolver dns_system_resolver_base = {
	.name = "system",
	.priority = DNS_SYSTEM_RESOLVER_PRIORITY,
	.resolve = dns_system_resolver_resolve,
	.cancel = dns_system_resolver_cancel,
};

/*!
 * \brief Callback to handle processing resource records.
 *
 * \details Adds an individual resource record discovered with ast_search_dns_ex to the
 *          ast_dns_query currently being resolved.
 *
 * \internal
 *
 * \param context     A void pointer to the ast_dns_query being processed.
 * \param record      An individual resource record discovered during the DNS search.
 * \param record_len  The length of the resource record.
 * \param ttl         The resource record's expiration time limit (time to live).
 *
 * \retval  0 on success
 * \retval -1 on failure
 */
static int dns_system_resolver_add_record(void *context, unsigned char *record, int record_len, int ttl)
{
	struct ast_dns_query *query = context;

	/* Add the record to the query.*/
	return ast_dns_resolver_add_record(query,
	                                   ast_dns_query_get_rr_type(query),
	                                   ast_dns_query_get_rr_class(query),
	                                   ttl,
	                                   (const char*) record,
	                                   record_len);
}

/*!
 * \brief Cancels processing resolution for a given query.
 *
 * \note The system API calls block so there is no way to cancel them. Therefore, this function always
 * returns failure when invoked.
 *
 * \internal
 *
 * \param query  The ast_dns_query to cancel.
 *
 * \retval  0 on success
 * \retval -1 on failure
 */
static int dns_system_resolver_cancel(struct ast_dns_query *query)
{
	return DNS_SYSTEM_RESOLVER_FAILURE;
}

/*!
 * \brief Destructor.
 *
 * \internal
 */
static void dns_system_resolver_destroy(void)
{
	/* Unreference the task processor */
	dns_system_resolver_tp = ast_taskprocessor_unreference(dns_system_resolver_tp);

	/* Unregister the base resolver */
	ast_dns_resolver_unregister(&dns_system_resolver_base);
}

/*!
 * \brief Callback to handle processing the query from the ast_taskprocessor instance.
 *
 * \internal
 *
 * \param data  A void pointer to the ast_dns_query being processed.
 *
 * \retval -1 on search failure
 * \retval  0 on no records found
 * \retval  1 on success
 */
static int dns_system_resolver_process_query(void *data)
{
	struct ast_dns_query *query = data;

	/* Perform the DNS search */
	enum ast_dns_search_result res = ast_search_dns_ex(query,
	                                                   ast_dns_query_get_name(query),
	                                                   ast_dns_query_get_rr_class(query),
	                                                   ast_dns_query_get_rr_type(query),
	                                                   dns_system_resolver_set_response,
	                                                   dns_system_resolver_add_record);

	/* Handle the possible return values from the DNS search */
	if (res == AST_DNS_SEARCH_FAILURE) {
		ast_debug(1, "DNS search failed for query: '%s'\n",
		        ast_dns_query_get_name(query));
	} else if (res == AST_DNS_SEARCH_NO_RECORDS) {
		ast_debug(1, "DNS search failed to yield any results for query: '%s'\n",
		        ast_dns_query_get_name(query));
	}

	/* Mark the query as complete */
	ast_dns_resolver_completed(query);

	/* Reduce the reference count on the query object */
	ao2_ref(query, -1);

	return res;
}

/*!
 * \brief Resolves a DNS query.
 *
 * \internal
 *
 * \param query  The ast_dns_query to resolve.
 *
 * \retval  0 on successful load of query handler to the ast_taskprocessor instance
 * \retval -1 on failure to load the query handler to the ast_taskprocessor instance
 */
static int dns_system_resolver_resolve(struct ast_dns_query *query)
{
	/* Add query processing handler to the task processor */
	int res = ast_taskprocessor_push(dns_system_resolver_tp,
	                                 dns_system_resolver_process_query,
	                                 ao2_bump(query));

	/* The query processing handler was not added to the task processor */
	if (res < 0) {
		ast_log(LOG_ERROR, "Failed to perform async DNS resolution of '%s'\n",
		        ast_dns_query_get_name(query));
		ao2_ref(query, -1);
	}

	/* Return the result of adding the query processing handler to the task processor */
	return res;
}

/*!
 * \brief Callback to handle initializing the results field.
 *
 * \internal
 *
 * \param context A void pointer to the ast_dns_query being processed.
 * \param dns_response The full DNS response.
 * \param dns_response_len The length of the full DNS response.
 * \param rcode The DNS response code.
 *
 * \retval  0 on success
 * \retval -1 on failure
 */
static int dns_system_resolver_set_response(void *context, unsigned char *dns_response, int dns_response_len, int rcode)
{
	struct ast_dns_query *query = context;
	int res;

	/* Instantiate the query's result field (if necessary). */
	if (!ast_dns_query_get_result(query)) {
		res = ast_dns_resolver_set_result(query,
		                                  0,
		                                  0,
		                                  rcode,
		                                  ast_dns_query_get_name(query),
		                                  (const char*) dns_response,
		                                  dns_response_len);

		if (res) {
			/* There was a problem instantiating the results field. */
			ast_log(LOG_ERROR, "Could not instantiate the results field for query: '%s'\n",
			        ast_dns_query_get_name(query));
		}
	} else {
		res = DNS_SYSTEM_RESOLVER_SUCCESS;
	}

	return res;
}

/*!
 * \brief Initializes the resolver.
 *
 * \retval  0 on success
 * \retval -1 on failure
 */
int ast_dns_system_resolver_init(void)
{
	/* Register the base resolver */
	int res = ast_dns_resolver_register(&dns_system_resolver_base);

	if (res) {
		return DNS_SYSTEM_RESOLVER_FAILURE;
	}

	/* Instantiate the task processor */
	dns_system_resolver_tp = ast_taskprocessor_get("dns_system_resolver_tp",
	                                                TPS_REF_DEFAULT);

	/* Return error if the task processor failed to instantiate */
	if (!dns_system_resolver_tp) {
		return DNS_SYSTEM_RESOLVER_FAILURE;
	}

	/* Register the cleanup function */
	ast_register_cleanup(dns_system_resolver_destroy);

	return DNS_SYSTEM_RESOLVER_SUCCESS;
}
