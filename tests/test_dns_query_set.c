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

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <arpa/nameser.h>
#include <arpa/inet.h>

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/vector.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_resolver.h"
#include "asterisk/dns_query_set.h"
#include "asterisk/dns_internal.h"

struct query_set_data {
	/*! Boolean indicator if query set has completed */
	int query_set_complete;
	/*! Number of times resolve() method has been called */
	int resolves;
	/*! Number of times resolve() method is allowed to be called */
	int resolves_allowed;
	/*! Number of times cancel() method has been called */
	int cancel;
	/*! Number of times cancel() method is allowed to be called */
	int cancel_allowed;
	ast_mutex_t lock;
	ast_cond_t cond;
};

static void query_set_data_destructor(void *obj)
{
	struct query_set_data *qsdata = obj;

	ast_mutex_destroy(&qsdata->lock);
	ast_cond_destroy(&qsdata->cond);
}

static struct query_set_data *query_set_data_alloc(void)
{
	struct query_set_data *qsdata;

	qsdata = ao2_alloc(sizeof(*qsdata), query_set_data_destructor);
	if (!qsdata) {
		return NULL;
	}

	ast_mutex_init(&qsdata->lock);
	ast_cond_init(&qsdata->cond, NULL);

	return qsdata;
}

#define DNS_ANSWER "Yes sirree"
#define DNS_ANSWER_SIZE strlen(DNS_ANSWER)

/*!
 * \brief Thread that performs asynchronous resolution.
 *
 * This thread uses the query's user data to determine how to
 * perform the resolution. If the allowed number of resolutions
 * has not been reached then this will succeed, otherwise the
 * query is expected to have been canceled.
 *
 * \param dns_query The ast_dns_query that is being performed
 * \return NULL
 */
static void *resolution_thread(void *dns_query)
{
	struct ast_dns_query *query = dns_query;
	struct ast_dns_query_set *query_set = ast_dns_query_get_data(query);
	struct query_set_data *qsdata = query_set->user_data;

	ast_assert(qsdata != NULL);

	ast_dns_resolver_set_result(query, 0, 0, ns_r_noerror, "asterisk.org", DNS_ANSWER, DNS_ANSWER_SIZE);
	ast_dns_resolver_completed(query);

	ao2_ref(query, -1);
	return NULL;
}

/*!
 * \brief Resolver's resolve() method
 *
 * \param query The query that is to be resolved
 * \retval 0 Successfully created thread to perform the resolution
 * \retval non-zero Failed to create resolution thread
 */
static int query_set_resolve(struct ast_dns_query *query)
{
	struct ast_dns_query_set *query_set = ast_dns_query_get_data(query);
	struct query_set_data *qsdata = query_set->user_data;
	pthread_t resolver_thread;

	/* Only the queries which will not be canceled actually start a thread */
	if (qsdata->resolves++ < qsdata->cancel_allowed) {
		return 0;
	}

	return ast_pthread_create_detached(&resolver_thread, NULL, resolution_thread, ao2_bump(query));
}

/*!
 * \brief Resolver's cancel() method
 *
 * \param query The query to cancel
 * \return 0
 */
static int query_set_cancel(struct ast_dns_query *query)
{
	struct ast_dns_query_set *query_set = ast_dns_query_get_data(query);
	struct query_set_data *qsdata;
	int res = -1;

	if (!query_set) {
		return -1;
	}
	qsdata = query_set->user_data;

	if (qsdata->cancel++ < qsdata->cancel_allowed) {
		res = 0;
	}

	return res;
}

static struct ast_dns_resolver query_set_resolver = {
	.name = "query_set",
	.priority = 0,
	.resolve = query_set_resolve,
	.cancel = query_set_cancel,
};

/*!
 * \brief Callback which is invoked upon query set completion
 *
 * \param query_set The query set
 */
static void query_set_callback(const struct ast_dns_query_set *query_set)
{
	struct query_set_data *qsdata = ast_dns_query_set_get_data(query_set);

	ast_mutex_lock(&qsdata->lock);
	qsdata->query_set_complete = 1;
	ast_cond_signal(&qsdata->cond);
	ast_mutex_unlock(&qsdata->lock);
}

/*!
 * \brief Framework for running a query set DNS test
 *
 * This function serves as a common way of testing various numbers of queries in a
 * query set and optional canceling of them.
 *
 * \param test The test being run
 * \param resolve The number of queries that should be allowed to complete resolution
 * \param cancel The number of queries that should be allowed to be canceled
 */
static enum ast_test_result_state query_set_test(struct ast_test *test, int resolve, int cancel)
{
	int total = resolve + cancel;
	RAII_VAR(struct ast_dns_query_set *, query_set, NULL, ao2_cleanup);
	RAII_VAR(struct query_set_data *, qsdata, NULL, ao2_cleanup);
	enum ast_test_result_state res = AST_TEST_PASS;
	int idx;
	struct timespec timeout;

	if (ast_dns_resolver_register(&query_set_resolver)) {
		ast_test_status_update(test, "Failed to register query set DNS resolver\n");
		return AST_TEST_FAIL;
	}

	qsdata = query_set_data_alloc();
	if (!qsdata) {
		ast_test_status_update(test, "Failed to allocate data necessary for query set test\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	query_set = ast_dns_query_set_create();
	if (!query_set) {
		ast_test_status_update(test, "Failed to create DNS query set\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	qsdata->resolves_allowed = resolve;
	qsdata->cancel_allowed = cancel;

	for (idx = 0; idx < total; ++idx) {
		if (ast_dns_query_set_add(query_set, "asterisk.org", ns_t_a, ns_c_in)) {
			ast_test_status_update(test, "Failed to add query to DNS query set\n");
			res = AST_TEST_FAIL;
			goto cleanup;
		}
	}

	if (ast_dns_query_set_num_queries(query_set) != total) {
		ast_test_status_update(test, "DNS query set does not contain the correct number of queries\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	ast_dns_query_set_resolve_async(query_set, query_set_callback, qsdata);

	if (cancel && (cancel == total)) {
		if (ast_dns_query_set_resolve_cancel(query_set)) {
			ast_test_status_update(test, "Failed to cancel DNS query set when it should be cancellable\n");
			res = AST_TEST_FAIL;
		}

		if (qsdata->query_set_complete) {
			ast_test_status_update(test, "Query set callback was invoked despite all queries being cancelled\n");
			res = AST_TEST_FAIL;
		}

		goto cleanup;
	} else if (cancel) {
		if (!ast_dns_query_set_resolve_cancel(query_set)) {
			ast_test_status_update(test, "Successfully cancelled DNS query set when it should not be possible\n");
			res = AST_TEST_FAIL;
			goto cleanup;
		}
	}

	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 10;

	ast_mutex_lock(&qsdata->lock);
	while (!qsdata->query_set_complete) {
		if (ast_cond_timedwait(&qsdata->cond, &qsdata->lock, &timeout) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&qsdata->lock);

	if (!qsdata->query_set_complete) {
		ast_test_status_update(test, "Query set did not complete when it should have\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	for (idx = 0; idx < ast_dns_query_set_num_queries(query_set); ++idx) {
		const struct ast_dns_query *query = ast_dns_query_set_get(query_set, idx);

		if (strcmp(ast_dns_query_get_name(query), "asterisk.org")) {
			ast_test_status_update(test, "Query did not have expected name\n");
			res = AST_TEST_FAIL;
		}
		if (ast_dns_query_get_rr_type(query) != ns_t_a) {
			ast_test_status_update(test, "Query did not have expected type\n");
			res = AST_TEST_FAIL;
		}
		if (ast_dns_query_get_rr_class(query) != ns_c_in) {
			ast_test_status_update(test, "Query did not have expected class\n");
			res = AST_TEST_FAIL;
		}
	}

cleanup:
	ast_dns_resolver_unregister(&query_set_resolver);
	return res;
}

AST_TEST_DEFINE(query_set)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "query_set";
		info->category = "/main/dns/query_set/";
		info->summary = "Test nominal asynchronous DNS query set";
		info->description =
			"This tests nominal query set in the following ways:\n"
			"\t* Multiple queries are added to a query set\n"
			"\t* The mock resolver is configured to respond to all queries\n"
			"\t* Asynchronous resolution of the query set is started\n"
			"\t* The mock resolver responds to all queries\n"
			"\t* We ensure that the query set callback is invoked upon completion";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return query_set_test(test, 4, 0);
}

AST_TEST_DEFINE(query_set_nominal_cancel)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "query_set_nominal_cancel";
		info->category = "/main/dns/query_set/";
		info->summary = "Test nominal asynchronous DNS query set cancellation";
		info->description =
			"This tests nominal query set cancellation in the following ways:\n"
			"\t* Multiple queries are added to a query set\n"
			"\t* The mock resolver is configured to NOT respond to any queries\n"
			"\t* Asynchronous resolution of the query set is started\n"
			"\t* The query set is canceled and is confirmed to return with success";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return query_set_test(test, 0, 4);
}

AST_TEST_DEFINE(query_set_off_nominal_cancel)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "query_set_off_nominal_cancel";
		info->category = "/main/dns/query_set/";
		info->summary = "Test off-nominal asynchronous DNS query set cancellation";
		info->description =
			"This tests nominal query set cancellation in the following ways:\n"
			"\t* Multiple queries are added to a query set\n"
			"\t* The mock resolver is configured to respond to half the queries\n"
			"\t* Asynchronous resolution of the query set is started\n"
			"\t* The query set is canceled and is confirmed to return failure\n"
			"\t* The query set callback is confirmed to run, since it could not be fully canceled";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return query_set_test(test, 2, 2);
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(query_set);
	AST_TEST_UNREGISTER(query_set_nominal_cancel);
	AST_TEST_UNREGISTER(query_set_off_nominal_cancel);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(query_set);
	AST_TEST_REGISTER(query_set_nominal_cancel);
	AST_TEST_REGISTER(query_set_off_nominal_cancel);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "DNS query set tests");
