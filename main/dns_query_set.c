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
 *
 * \brief DNS Query Set API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/vector.h"
#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_query_set.h"
#include "asterisk/dns_internal.h"
#include "asterisk/dns_resolver.h"

/*! \brief The default number of expected queries to be added to the query set */
#define DNS_QUERY_SET_EXPECTED_QUERY_COUNT 5

/*! \brief Release all queries held in a query set */
static void dns_query_set_release(struct ast_dns_query_set *query_set)
{
	int idx;

	for (idx = 0; idx < AST_VECTOR_SIZE(&query_set->queries); ++idx) {
		struct dns_query_set_query *query = AST_VECTOR_GET_ADDR(&query_set->queries, idx);

		ao2_ref(query->query, -1);
	}

	AST_VECTOR_FREE(&query_set->queries);
}

/*! \brief Destructor for DNS query set */
static void dns_query_set_destroy(void *data)
{
	struct ast_dns_query_set *query_set = data;

	dns_query_set_release(query_set);
	ao2_cleanup(query_set->user_data);
}

struct ast_dns_query_set *ast_dns_query_set_create(void)
{
	struct ast_dns_query_set *query_set;

	query_set = ao2_alloc_options(sizeof(*query_set), dns_query_set_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!query_set) {
		return NULL;
	}

	if (AST_VECTOR_INIT(&query_set->queries, DNS_QUERY_SET_EXPECTED_QUERY_COUNT)) {
		ao2_ref(query_set, -1);
		return NULL;
	}

	return query_set;
}

/*! \brief Callback invoked upon completion of a DNS query */
static void dns_query_set_callback(const struct ast_dns_query *query)
{
	struct ast_dns_query_set *query_set = ast_dns_query_get_data(query);

	if (ast_atomic_fetchadd_int(&query_set->queries_completed, +1) != (AST_VECTOR_SIZE(&query_set->queries) - 1)) {
		return;
	}

	/* All queries have been completed, invoke final callback */
	if (query_set->queries_cancelled != AST_VECTOR_SIZE(&query_set->queries)) {
		query_set->callback(query_set);
	}

	ao2_cleanup(query_set->user_data);
	query_set->user_data = NULL;

	dns_query_set_release(query_set);
}

int ast_dns_query_set_add(struct ast_dns_query_set *query_set, const char *name, int rr_type, int rr_class)
{
	struct dns_query_set_query query = {
		.started = 0,
	};

	ast_assert(!query_set->in_progress);
	if (query_set->in_progress) {
		ast_log(LOG_ERROR, "Attempted to add additional query to query set '%p' after resolution has started\n",
			query_set);
		return -1;
	}

	query.query = dns_query_alloc(name, rr_type, rr_class, dns_query_set_callback, query_set);
	if (!query.query) {
		return -1;
	}

	AST_VECTOR_APPEND(&query_set->queries, query);

	return 0;
}

size_t ast_dns_query_set_num_queries(const struct ast_dns_query_set *query_set)
{
	return AST_VECTOR_SIZE(&query_set->queries);
}

struct ast_dns_query *ast_dns_query_set_get(const struct ast_dns_query_set *query_set, unsigned int index)
{
	/* Only once all queries have been completed can results be retrieved */
	if (query_set->queries_completed != AST_VECTOR_SIZE(&query_set->queries)) {
		return NULL;
	}

	/* If the index exceeds the number of queries... no query for you */
	if (index >= AST_VECTOR_SIZE(&query_set->queries)) {
		return NULL;
	}

	return AST_VECTOR_GET_ADDR(&query_set->queries, index)->query;
}

void *ast_dns_query_set_get_data(const struct ast_dns_query_set *query_set)
{
	return query_set->user_data;
}

void ast_dns_query_set_resolve_async(struct ast_dns_query_set *query_set, ast_dns_query_set_callback callback, void *data)
{
	int idx;

	ast_assert(!query_set->in_progress);
	if (query_set->in_progress) {
		ast_log(LOG_ERROR, "Attempted to start asynchronous resolution of query set '%p' when it has already started\n",
			query_set);
		return;
	}

	query_set->in_progress = 1;
	query_set->callback = callback;
	query_set->user_data = ao2_bump(data);

	for (idx = 0; idx < AST_VECTOR_SIZE(&query_set->queries); ++idx) {
		struct dns_query_set_query *query = AST_VECTOR_GET_ADDR(&query_set->queries, idx);

		if (!query->query->resolver->resolve(query->query)) {
			query->started = 1;
			continue;
		}

		dns_query_set_callback(query->query);
	}
}

/*! \brief Structure used for signaling back for synchronous resolution completion */
struct dns_synchronous_resolve {
	/*! \brief Lock used for signaling */
	ast_mutex_t lock;
	/*! \brief Condition used for signaling */
	ast_cond_t cond;
	/*! \brief Whether the query has completed */
	unsigned int completed;
};

/*! \brief Destructor for synchronous resolution structure */
static void dns_synchronous_resolve_destroy(void *data)
{
	struct dns_synchronous_resolve *synchronous = data;

	ast_mutex_destroy(&synchronous->lock);
	ast_cond_destroy(&synchronous->cond);
}

/*! \brief Callback used to implement synchronous resolution */
static void dns_synchronous_resolve_callback(const struct ast_dns_query_set *query_set)
{
	struct dns_synchronous_resolve *synchronous = ast_dns_query_set_get_data(query_set);

	ast_mutex_lock(&synchronous->lock);
	synchronous->completed = 1;
	ast_cond_signal(&synchronous->cond);
	ast_mutex_unlock(&synchronous->lock);
}

int ast_query_set_resolve(struct ast_dns_query_set *query_set)
{
	struct dns_synchronous_resolve *synchronous;

	synchronous = ao2_alloc_options(sizeof(*synchronous), dns_synchronous_resolve_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!synchronous) {
		return -1;
	}

	ast_mutex_init(&synchronous->lock);
	ast_cond_init(&synchronous->cond, NULL);

	ast_dns_query_set_resolve_async(query_set, dns_synchronous_resolve_callback, synchronous);

	/* Wait for resolution to complete */
	ast_mutex_lock(&synchronous->lock);
	while (!synchronous->completed) {
		ast_cond_wait(&synchronous->cond, &synchronous->lock);
	}
	ast_mutex_unlock(&synchronous->lock);

	ao2_ref(synchronous, -1);

	return 0;
}

int ast_dns_query_set_resolve_cancel(struct ast_dns_query_set *query_set)
{
	int idx;
	size_t query_count = AST_VECTOR_SIZE(&query_set->queries);

	for (idx = 0; idx < AST_VECTOR_SIZE(&query_set->queries); ++idx) {
		struct dns_query_set_query *query = AST_VECTOR_GET_ADDR(&query_set->queries, idx);

		if (query->started) {
			if (!query->query->resolver->cancel(query->query)) {
				query_set->queries_cancelled++;
				dns_query_set_callback(query->query);
			}
		} else {
			query_set->queries_cancelled++;
		}
	}

	return (query_set->queries_cancelled == query_count) ? 0 : -1;
}