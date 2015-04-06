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
 * \brief Core DNS Functionality
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/linkedlists.h"
#include "asterisk/vector.h"
#include "asterisk/astobj2.h"
#include "asterisk/strings.h"
#include "asterisk/sched.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_srv.h"
#include "asterisk/dns_tlsa.h"
#include "asterisk/dns_recurring.h"
#include "asterisk/dns_resolver.h"
#include "asterisk/dns_internal.h"

#include <netinet/in.h>
#include <arpa/nameser.h>

AST_RWLIST_HEAD_STATIC(resolvers, ast_dns_resolver);

static struct ast_sched_context *sched;

struct ast_sched_context *ast_dns_get_sched(void)
{
	return sched;
}

const char *ast_dns_query_get_name(const struct ast_dns_query *query)
{
	return query->name;
}

int ast_dns_query_get_rr_type(const struct ast_dns_query *query)
{
	return query->rr_type;
}

int ast_dns_query_get_rr_class(const struct ast_dns_query *query)
{
	return query->rr_class;
}

void *ast_dns_query_get_data(const struct ast_dns_query *query)
{
	return query->user_data;
}

struct ast_dns_result *ast_dns_query_get_result(const struct ast_dns_query *query)
{
	return query->result;
}

unsigned int ast_dns_result_get_secure(const struct ast_dns_result *result)
{
	return result->secure;
}

unsigned int ast_dns_result_get_bogus(const struct ast_dns_result *result)
{
	return result->bogus;
}

unsigned int ast_dns_result_get_rcode(const struct ast_dns_result *result)
{
	return result->rcode;
}

const char *ast_dns_result_get_canonical(const struct ast_dns_result *result)
{
	return result->canonical;
}

const struct ast_dns_record *ast_dns_result_get_records(const struct ast_dns_result *result)
{
	return AST_LIST_FIRST(&result->records);
}

const char *ast_dns_result_get_answer(const struct ast_dns_result *result)
{
	return result->answer;
}

int ast_dns_result_get_lowest_ttl(const struct ast_dns_result *result)
{
	int ttl = 0;
	const struct ast_dns_record *record;

	if (ast_dns_result_get_rcode(result) == ns_r_nxdomain) {
		return 0;
	}

	for (record = ast_dns_result_get_records(result); record; record = ast_dns_record_get_next(record)) {
		if (!ttl || (ast_dns_record_get_ttl(record) && (ast_dns_record_get_ttl(record) < ttl))) {
			ttl = ast_dns_record_get_ttl(record);
		}
	}

	return ttl;
}

void ast_dns_result_free(struct ast_dns_result *result)
{
	struct ast_dns_record *record;

	if (!result) {
		return;
	}

	while ((record = AST_LIST_REMOVE_HEAD(&result->records, list))) {
		ast_free(record);
	}

	ast_free(result);
}

int ast_dns_record_get_rr_type(const struct ast_dns_record *record)
{
	return record->rr_type;
}

int ast_dns_record_get_rr_class(const struct ast_dns_record *record)
{
	return record->rr_class;
}

int ast_dns_record_get_ttl(const struct ast_dns_record *record)
{
	return record->ttl;
}

const char *ast_dns_record_get_data(const struct ast_dns_record *record)
{
	return record->data_ptr;
}

const struct ast_dns_record *ast_dns_record_get_next(const struct ast_dns_record *record)
{
	return AST_LIST_NEXT(record, list);
}

/*! \brief Destructor for an active DNS query */
static void dns_query_active_destroy(void *data)
{
	struct ast_dns_query_active *active = data;

	ao2_cleanup(active->query);
}

/*! \brief \brief Destructor for a DNS query */
static void dns_query_destroy(void *data)
{
	struct ast_dns_query *query = data;

	ao2_cleanup(query->user_data);
	ao2_cleanup(query->resolver_data);
	ast_dns_result_free(query->result);
}

struct ast_dns_query_active *ast_dns_resolve_async(const char *name, int rr_type, int rr_class, ast_dns_resolve_callback callback, void *data)
{
	struct ast_dns_query_active *active;

	if (ast_strlen_zero(name)) {
		ast_log(LOG_WARNING, "Could not perform asynchronous resolution, no name provided\n");
		return NULL;
	} else if (rr_type > ns_t_max) {
		ast_log(LOG_WARNING, "Could not perform asynchronous resolution of '%s', resource record type '%d' exceeds maximum\n",
			name, rr_type);
		return NULL;
	} else if (rr_type < 0) {
		ast_log(LOG_WARNING, "Could not perform asynchronous resolution of '%s', invalid resource record type '%d'\n",
			name, rr_type);
		return NULL;
	} else if (rr_class > ns_c_max) {
		ast_log(LOG_WARNING, "Could not perform asynchronous resolution of '%s', resource record class '%d' exceeds maximum\n",
			name, rr_class);
		return NULL;
	} else if (rr_class < 0) {
		ast_log(LOG_WARNING, "Could not perform asynchronous resolution of '%s', invalid resource class '%d'\n",
			name, rr_class);
		return NULL;
	} else if (!callback) {
		ast_log(LOG_WARNING, "Could not perform asynchronous resolution of '%s', no callback provided\n",
			name);
		return NULL;
	}

	active = ao2_alloc_options(sizeof(*active), dns_query_active_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!active) {
		return NULL;
	}

	active->query = ao2_alloc_options(sizeof(*active->query) + strlen(name) + 1, dns_query_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!active->query) {
		ao2_ref(active, -1);
		return NULL;
	}

	active->query->callback = callback;
	active->query->user_data = ao2_bump(data);
	active->query->rr_type = rr_type;
	active->query->rr_class = rr_class;
	strcpy(active->query->name, name); /* SAFE */

	AST_RWLIST_RDLOCK(&resolvers);
	active->query->resolver = AST_RWLIST_FIRST(&resolvers);
	AST_RWLIST_UNLOCK(&resolvers);

	if (!active->query->resolver) {
		ast_log(LOG_ERROR, "Attempted to do a DNS query for '%s' of class '%d' and type '%d' but no resolver is available\n",
			name, rr_class, rr_type);
		ao2_ref(active, -1);
		return NULL;
	}

	if (active->query->resolver->resolve(active->query)) {
		ast_log(LOG_ERROR, "Resolver '%s' returned an error when resolving '%s' of class '%d' and type '%d'\n",
			active->query->resolver->name, name, rr_class, rr_type);
		ao2_ref(active, -1);
		return NULL;
	}

	return active;
}

int ast_dns_resolve_cancel(struct ast_dns_query_active *active)
{
	return active->query->resolver->cancel(active->query);
}

/*! \brief Structure used for signaling back for synchronous resolution completion */
struct dns_synchronous_resolve {
	/*! \brief Lock used for signaling */
	ast_mutex_t lock;
	/*! \brief Condition used for signaling */
	ast_cond_t cond;
	/*! \brief Whether the query has completed */
	unsigned int completed;
	/*! \brief The result from the query */
	struct ast_dns_result *result;
};

/*! \brief Destructor for synchronous resolution structure */
static void dns_synchronous_resolve_destroy(void *data)
{
	struct dns_synchronous_resolve *synchronous = data;

	ast_mutex_destroy(&synchronous->lock);
	ast_cond_destroy(&synchronous->cond);

	/* This purposely does not unref result as it has been passed to the caller */
}

/*! \brief Callback used to implement synchronous resolution */
static void dns_synchronous_resolve_callback(const struct ast_dns_query *query)
{
	struct dns_synchronous_resolve *synchronous = ast_dns_query_get_data(query);

	synchronous->result = query->result;
	((struct ast_dns_query *)query)->result = NULL;

	ast_mutex_lock(&synchronous->lock);
	synchronous->completed = 1;
	ast_cond_signal(&synchronous->cond);
	ast_mutex_unlock(&synchronous->lock);
}

int ast_dns_resolve(const char *name, int rr_type, int rr_class, struct ast_dns_result **result)
{
	struct dns_synchronous_resolve *synchronous;
	struct ast_dns_query_active *active;

	if (ast_strlen_zero(name)) {
		ast_log(LOG_WARNING, "Could not perform synchronous resolution, no name provided\n");
		return -1;
	} else if (rr_type > ns_t_max) {
		ast_log(LOG_WARNING, "Could not perform synchronous resolution of '%s', resource record type '%d' exceeds maximum\n",
			name, rr_type);
		return -1;
	} else if (rr_type < 0) {
		ast_log(LOG_WARNING, "Could not perform synchronous resolution of '%s', invalid resource record type '%d'\n",
			name, rr_type);
		return -1;
	} else if (rr_class > ns_c_max) {
		ast_log(LOG_WARNING, "Could not perform synchronous resolution of '%s', resource record class '%d' exceeds maximum\n",
			name, rr_class);
		return -1;
	} else if (rr_class < 0) {
		ast_log(LOG_WARNING, "Could not perform synchronous resolution of '%s', invalid resource class '%d'\n",
			name, rr_class);
		return -1;
	} else if (!result) {
		ast_log(LOG_WARNING, "Could not perform synchronous resolution of '%s', no result pointer provided for storing results\n",
			name);
		return -1;
	}

	synchronous = ao2_alloc_options(sizeof(*synchronous), dns_synchronous_resolve_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!synchronous) {
		return -1;
	}

	ast_mutex_init(&synchronous->lock);
	ast_cond_init(&synchronous->cond, NULL);

	active = ast_dns_resolve_async(name, rr_type, rr_class, dns_synchronous_resolve_callback, synchronous);
	if (active) {
		/* Wait for resolution to complete */
		ast_mutex_lock(&synchronous->lock);
		while (!synchronous->completed) {
			ast_cond_wait(&synchronous->cond, &synchronous->lock);
		}
		ast_mutex_unlock(&synchronous->lock);
		ao2_ref(active, -1);
	}

	*result = synchronous->result;
	ao2_ref(synchronous, -1);

	return *result ? 0 : -1;
}

int ast_dns_resolver_set_data(struct ast_dns_query *query, void *data)
{
	if (query->resolver_data) {
		return -1;
	}

	query->resolver_data = ao2_bump(data);

	return 0;
}

void *ast_dns_resolver_get_data(const struct ast_dns_query *query)
{
	return query->resolver_data;
}

int ast_dns_resolver_set_result(struct ast_dns_query *query, unsigned int secure, unsigned int bogus,
	unsigned int rcode, const char *canonical, const char *answer, size_t answer_size)
{
	char *buf_ptr;

	if (secure && bogus) {
		ast_debug(2, "Query '%p': Could not set result information, it can not be both secure and bogus\n",
			query);
		return -1;
	}

	if (ast_strlen_zero(canonical)) {
		ast_debug(2, "Query '%p': Could not set result information since no canonical name was provided\n",
			query);
		return -1;
	}

	if (!answer || answer_size == 0) {
		ast_debug(2, "Query '%p': Could not set result information since no DNS answer was provided\n",
			query);
		return -1;
	}

	ast_dns_result_free(query->result);

	query->result = ast_calloc(1, sizeof(*query->result) + strlen(canonical) + 1 + answer_size);
	if (!query->result) {
		return -1;
	}

	query->result->secure = secure;
	query->result->bogus = bogus;
	query->result->rcode = rcode;

	buf_ptr = query->result->buf;
	strcpy(buf_ptr, canonical); /* SAFE */
	query->result->canonical = buf_ptr;

	buf_ptr += strlen(canonical) + 1;
	memcpy(buf_ptr, answer, answer_size); /* SAFE */
	query->result->answer = buf_ptr;
	query->result->answer_size = answer_size;

	return 0;
}

static struct ast_dns_record *generic_record_alloc(struct ast_dns_query *query, const char *data, const size_t size)
{
	struct ast_dns_record *record;

	record = ast_calloc(1, sizeof(*record) + size);
	if (!record) {
		return NULL;
	}

	record->data_ptr = record->data;

	return record;
}

int ast_dns_resolver_add_record(struct ast_dns_query *query, int rr_type, int rr_class, int ttl, const char *data, const size_t size)
{
	struct ast_dns_record *record;

	if (rr_type < 0) {
		ast_debug(2, "Query '%p': Could not add record, invalid resource record type '%d'\n",
			query, rr_type);
		return -1;
	} else if (rr_type > ns_t_max) {
		ast_debug(2, "Query '%p': Could not add record, resource record type '%d' exceeds maximum\n",
			query, rr_type);
		return -1;
	} else if (rr_class < 0) {
		ast_debug(2, "Query '%p': Could not add record, invalid resource record class '%d'\n",
			query, rr_class);
		return -1;
	} else if (rr_class > ns_c_max) {
		ast_debug(2, "Query '%p': Could not add record, resource record class '%d' exceeds maximum\n",
			query, rr_class);
		return -1;
	} else if (ttl < 0) {
		ast_debug(2, "Query '%p': Could not add record, invalid TTL '%d'\n",
			query, ttl);
		return -1;
	} else if (!data || !size) {
		ast_debug(2, "Query '%p': Could not add record, no data specified\n",
			query);
		return -1;
	} else if (!query->result) {
		ast_debug(2, "Query '%p': No result was set on the query, thus records can not be added\n",
			query);
		return -1;
	}

	if (rr_type == ns_t_naptr) {
		record = dns_naptr_alloc(query, data, size);
	} else if (rr_type == ns_t_srv) {
		record = ast_dns_srv_alloc(query, data, size);
	} else {
		record = generic_record_alloc(query, data, size);
	}

	if (!record) {
		return -1;
	}

	record->rr_type = rr_type;
	record->rr_class = rr_class;
	record->ttl = ttl;
	record->data_len = size;
	memcpy(record->data_ptr, data, size);

	AST_LIST_INSERT_TAIL(&query->result->records, record, list);

	return 0;
}

void ast_dns_resolver_completed(struct ast_dns_query *query)
{
	if (ast_dns_query_get_rr_type(query) == ns_t_naptr) {
		dns_naptr_sort(query->result);
	} else if (ast_dns_query_get_rr_type(query) == ns_t_srv) {
		ast_dns_srv_sort(query->result);
	}

	query->callback(query);
}

static void dns_shutdown(void)
{
	if (sched) {
		ast_sched_context_destroy(sched);
		sched = NULL;
	}
}

int ast_dns_resolver_register(struct ast_dns_resolver *resolver)
{
	struct ast_dns_resolver *iter;
	int inserted = 0;

	if (!resolver) {
		return -1;
	} else if (ast_strlen_zero(resolver->name)) {
		ast_log(LOG_ERROR, "Registration of DNS resolver failed as it does not have a name\n");
		return -1;
	} else if (!resolver->resolve) {
		ast_log(LOG_ERROR, "DNS resolver '%s' does not implement the resolve callback which is required\n",
			resolver->name);
		return -1;
	} else if (!resolver->cancel) {
		ast_log(LOG_ERROR, "DNS resolver '%s' does not implement the cancel callback which is required\n",
			resolver->name);
		return -1;
	}

	AST_RWLIST_WRLOCK(&resolvers);

	/* On the first registration of a resolver start a scheduler for recurring queries */
	if (AST_LIST_EMPTY(&resolvers) && !sched) {
		sched = ast_sched_context_create();
		if (!sched) {
			ast_log(LOG_ERROR, "DNS resolver '%s' could not be registered: Failed to create scheduler for recurring DNS queries\n",
				resolver->name);
			AST_RWLIST_UNLOCK(&resolvers);
			return -1;
		}

		if (ast_sched_start_thread(sched)) {
			ast_log(LOG_ERROR, "DNS resolver '%s' could not be registered: Failed to start thread for recurring DNS queries\n",
				resolver->name);
			dns_shutdown();
			AST_RWLIST_UNLOCK(&resolvers);
			return -1;
		}

		ast_register_cleanup(dns_shutdown);
	}

	AST_LIST_TRAVERSE(&resolvers, iter, next) {
		if (!strcmp(iter->name, resolver->name)) {
			ast_log(LOG_ERROR, "A DNS resolver with the name '%s' is already registered\n", resolver->name);
			AST_RWLIST_UNLOCK(&resolvers);
			return -1;
		}
	}

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&resolvers, iter, next) {
		if (iter->priority > resolver->priority) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(resolver, next);
			inserted = 1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (!inserted) {
		AST_RWLIST_INSERT_TAIL(&resolvers, resolver, next);
	}

	AST_RWLIST_UNLOCK(&resolvers);

	ast_verb(2, "Registered DNS resolver '%s' with priority '%d'\n", resolver->name, resolver->priority);

	return 0;
}

void ast_dns_resolver_unregister(struct ast_dns_resolver *resolver)
{
	struct ast_dns_resolver *iter;

	if (!resolver) {
		return;
	}

	AST_RWLIST_WRLOCK(&resolvers);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&resolvers, iter, next) {
		if (resolver == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
	AST_RWLIST_UNLOCK(&resolvers);

	ast_verb(2, "Unregistered DNS resolver '%s'\n", resolver->name);
}
