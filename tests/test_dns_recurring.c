/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2015, Mark Michelson
 *
 * Mark Michelson <mmichelson@digium.com>
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
#include "asterisk/dns_core.h"
#include "asterisk/dns_resolver.h"
#include "asterisk/dns_recurring.h"
#include "asterisk/dns_internal.h"

struct recurring_data {
	/*! TTL to place in first returned result */
	int ttl1;
	/*! TTL to place in second returned result */
	int ttl2;
	/*! Boolean indicator if query has completed */
	int query_complete;
	/*! Number of times recurring resolution has completed */
	int complete_resolutions;
	/*! Number of times resolve() method has been called */
	int resolves;
	/*! Indicates that the query is expected to be canceled */
	int cancel_expected;
	/*! Indicates that the query is ready to be canceled */
	int cancel_ready;
	/*! Indicates that the query has been canceled */
	int canceled;
	ast_mutex_t lock;
	ast_cond_t cond;
};

static void recurring_data_destructor(void *obj)
{
	struct recurring_data *rdata = obj;

	ast_mutex_destroy(&rdata->lock);
	ast_cond_destroy(&rdata->cond);
}

static struct recurring_data *recurring_data_alloc(void)
{
	struct recurring_data *rdata;

	rdata = ao2_alloc(sizeof(*rdata), recurring_data_destructor);
	if (!rdata) {
		return NULL;
	}

	ast_mutex_init(&rdata->lock);
	ast_cond_init(&rdata->cond, NULL);

	return rdata;
}

#define DNS_ANSWER "Yes sirree"
#define DNS_ANSWER_SIZE strlen(DNS_ANSWER)

/*!
 * \brief Thread that performs asynchronous resolution.
 *
 * This thread uses the query's user data to determine how to
 * perform the resolution. The query may either be canceled or
 * it may be completed with records.
 *
 * \param dns_query The ast_dns_query that is being performed
 * \return NULL
 */
static void *resolution_thread(void *dns_query)
{
	struct ast_dns_query *query = dns_query;

	static const char *ADDR1 = "127.0.0.1";
	static const size_t ADDR1_BUFSIZE = sizeof(struct in_addr);
	char addr1_buf[ADDR1_BUFSIZE];

	static const char *ADDR2 = "192.168.0.1";
	static const size_t ADDR2_BUFSIZE = sizeof(struct in_addr);
	char addr2_buf[ADDR2_BUFSIZE];

	struct ast_dns_query_recurring *recurring = ast_dns_query_get_data(query);
	struct recurring_data *rdata = recurring->user_data;

	ast_assert(rdata != NULL);

	/* Canceling is an interesting dance. This thread needs to signal that it is
	 * ready to be canceled. Then it needs to wait until the query is actually canceled.
	 */
	if (rdata->cancel_expected) {
		ast_mutex_lock(&rdata->lock);
		rdata->cancel_ready = 1;
		ast_cond_signal(&rdata->cond);

		while (!rdata->canceled) {
			ast_cond_wait(&rdata->cond, &rdata->lock);
		}
		ast_mutex_unlock(&rdata->lock);

		ast_dns_resolver_completed(query);
		ao2_ref(query, -1);

		return NULL;
	}

	/* When the query isn't canceled, we set the TTL of the results based on what
	 * we've been told to set it to
	 */
	ast_dns_resolver_set_result(query, 0, 0, ns_r_noerror, "asterisk.org", DNS_ANSWER, DNS_ANSWER_SIZE);

	inet_pton(AF_INET, ADDR1, addr1_buf);
	ast_dns_resolver_add_record(query, ns_t_a, ns_c_in, rdata->ttl1, addr1_buf, ADDR1_BUFSIZE);

	inet_pton(AF_INET, ADDR2, addr2_buf);
	ast_dns_resolver_add_record(query, ns_t_a, ns_c_in, rdata->ttl2, addr2_buf, ADDR2_BUFSIZE);

	++rdata->complete_resolutions;

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
static int recurring_resolve(struct ast_dns_query *query)
{
	struct ast_dns_query_recurring *recurring = ast_dns_query_get_data(query);
	struct recurring_data *rdata = recurring->user_data;
	pthread_t resolver_thread;

	ast_assert(rdata != NULL);
	++rdata->resolves;
	return ast_pthread_create_detached(&resolver_thread, NULL, resolution_thread, ao2_bump(query));
}

/*!
 * \brief Resolver's cancel() method
 *
 * \param query The query to cancel
 * \return 0
 */
static int recurring_cancel(struct ast_dns_query *query)
{
	struct ast_dns_query_recurring *recurring = ast_dns_query_get_data(query);
	struct recurring_data *rdata = recurring->user_data;

	ast_mutex_lock(&rdata->lock);
	rdata->canceled = 1;
	ast_cond_signal(&rdata->cond);
	ast_mutex_unlock(&rdata->lock);

	return 0;
}

static struct ast_dns_resolver recurring_resolver = {
	.name = "test_recurring",
	.priority = 0,
	.resolve = recurring_resolve,
	.cancel = recurring_cancel,
};

/*!
 * \brief Wait for a successful resolution to complete
 *
 * This is called whenever a successful DNS resolution occurs. This function
 * serves to ensure that parameters are as we expect them to be.
 *
 * \param test The test being executed
 * \param rdata DNS query user data
 * \param expected_lapse The amount of time we expect to wait for the query to complete
 * \param num_resolves The number of DNS resolutions that have been executed
 * \param num_completed The number of DNS resolutions we expect to have completed successfully
 * \param canceled Whether the query is expected to have been canceled
 */
static int wait_for_resolution(struct ast_test *test, struct recurring_data *rdata,
		int expected_lapse, int num_resolves, int num_completed, int canceled)
{
	struct timespec begin;
	struct timespec end;
	struct timespec timeout;
	int secdiff;

	clock_gettime(CLOCK_REALTIME, &begin);

	timeout.tv_sec = begin.tv_sec + 20;
	timeout.tv_nsec = begin.tv_nsec;

	ast_mutex_lock(&rdata->lock);
	while (!rdata->query_complete) {
		if (ast_cond_timedwait(&rdata->cond, &rdata->lock, &timeout) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&rdata->lock);

	if (!rdata->query_complete) {
		ast_test_status_update(test, "Query timed out\n");
		return -1;
	}

	rdata->query_complete = 0;
	clock_gettime(CLOCK_REALTIME, &end);

	secdiff = end.tv_sec - begin.tv_sec;

	/* Give ourselves some wiggle room */
	if (secdiff < expected_lapse - 2 || secdiff > expected_lapse + 2) {
		ast_test_status_update(test, "Query did not complete in expected time\n");
		return -1;
	}

	if (rdata->resolves != num_resolves || rdata->complete_resolutions != num_completed) {
		ast_test_status_update(test, "Query has not undergone expected number of resolutions\n");
		return -1;
	}

	if (rdata->canceled != canceled) {
		ast_test_status_update(test, "Query was canceled unexpectedly\n");
		return -1;
	}

	ast_test_status_update(test, "Query completed in expected time frame\n");

	return 0;
}

static void async_callback(const struct ast_dns_query *query)
{
	struct recurring_data *rdata = ast_dns_query_get_data(query);

	ast_assert(rdata != NULL);

	ast_mutex_lock(&rdata->lock);
	rdata->query_complete = 1;
	ast_cond_signal(&rdata->cond);
	ast_mutex_unlock(&rdata->lock);
}

AST_TEST_DEFINE(recurring_query)
{
	RAII_VAR(struct ast_dns_query_recurring *, recurring_query, NULL, ao2_cleanup);
	RAII_VAR(struct recurring_data *, rdata, NULL, ao2_cleanup);

	enum ast_test_result_state res = AST_TEST_PASS;
	int expected_lapse;

	switch (cmd) {
	case TEST_INIT:
		info->name = "recurring_query";
		info->category = "/main/dns/recurring/";
		info->summary = "Test nominal asynchronous recurring DNS queries";
		info->description =
			"This tests nominal recurring queries in the following ways:\n"
			"\t* An asynchronous query is sent to a mock resolver\n"
			"\t* The mock resolver returns two records with different TTLs\n"
			"\t* We ensure that the query re-occurs according to the lower of the TTLs\n"
			"\t* The mock resolver returns two records, this time with different TTLs\n"
			"\t  from the first time the query was resolved\n"
			"\t* We ensure that the query re-occurs according to the new lower TTL";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_dns_resolver_register(&recurring_resolver)) {
		ast_test_status_update(test, "Failed to register recurring DNS resolver\n");
		return AST_TEST_FAIL;
	}

	rdata = recurring_data_alloc();
	if (!rdata) {
		ast_test_status_update(test, "Failed to allocate data necessary for recurring test\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	expected_lapse = 0;
	rdata->ttl1 = 5;
	rdata->ttl2 = 20;

	recurring_query = ast_dns_resolve_recurring("asterisk.org", ns_t_a, ns_c_in, async_callback, rdata);
	if (!recurring_query) {
		ast_test_status_update(test, "Failed to create recurring DNS query\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* This should be near instantaneous */
	if (wait_for_resolution(test, rdata, expected_lapse, 1, 1, 0)) {
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	expected_lapse = rdata->ttl1;
	rdata->ttl1 = 45;
	rdata->ttl2 = 10;

	/* This should take approximately 5 seconds */
	if (wait_for_resolution(test, rdata, expected_lapse, 2, 2, 0)) {
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	expected_lapse = rdata->ttl2;

	/* This should take approximately 10 seconds */
	if (wait_for_resolution(test, rdata, expected_lapse, 3, 3, 0)) {
		res = AST_TEST_FAIL;
		goto cleanup;
	}

cleanup:
	if (recurring_query) {
		/* XXX I don't like calling this here since I'm not testing
		 * canceling recurring queries, but I'm forced into it here
		 */
		ast_dns_resolve_recurring_cancel(recurring_query);
	}
	ast_dns_resolver_unregister(&recurring_resolver);
	return res;
}

static int fail_resolve(struct ast_dns_query *query)
{
	return -1;
}

static int stub_cancel(struct ast_dns_query *query)
{
	return 0;
}

static void stub_callback(const struct ast_dns_query *query)
{
	return;
}

AST_TEST_DEFINE(recurring_query_off_nominal)
{
	struct ast_dns_resolver terrible_resolver = {
		.name = "Harold P. Warren's Filmography",
		.priority = 0,
		.resolve = fail_resolve,
		.cancel = stub_cancel,
	};

	struct ast_dns_query_recurring *recurring;

	struct dns_resolve_data {
		const char *name;
		int rr_type;
		int rr_class;
		ast_dns_resolve_callback callback;
	} resolves [] = {
		{ NULL,           ns_t_a,       ns_c_in,      stub_callback },
		{ "asterisk.org", -1,           ns_c_in,      stub_callback },
		{ "asterisk.org", ns_t_max + 1, ns_c_in,      stub_callback },
		{ "asterisk.org", ns_t_a,       -1,           stub_callback },
		{ "asterisk.org", ns_t_a,       ns_c_max + 1, stub_callback },
		{ "asterisk.org", ns_t_a,       ns_c_in,      NULL },
	};
	int i;
	enum ast_test_result_state res = AST_TEST_PASS;

	switch (cmd) {
	case TEST_INIT:
		info->name = "recurring_query_off_nominal";
		info->category = "/main/dns/recurring/";
		info->summary = "Test off-nominal recurring DNS resolution";
		info->description =
			"This test performs several off-nominal recurring DNS resolutions:\n"
			"\t* Attempt resolution with NULL name\n"
			"\t* Attempt resolution with invalid RR type\n"
			"\t* Attempt resolution with invalid RR class\n"
			"\t* Attempt resolution with NULL callback pointer\n"
			"\t* Attempt resolution with resolver that returns an error";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_dns_resolver_register(&recurring_resolver)) {
		ast_test_status_update(test, "Failed to register test resolver\n");
		return AST_TEST_FAIL;
	}

	for (i = 0; i < ARRAY_LEN(resolves); ++i) {
		recurring = ast_dns_resolve_recurring(resolves[i].name, resolves[i].rr_type, resolves[i].rr_class,
				resolves[i].callback, NULL);
		if (recurring) {
			ast_test_status_update(test, "Successfully performed recurring resolution with invalid data\n");
			ast_dns_resolve_recurring_cancel(recurring);
			ao2_ref(recurring, -1);
			res = AST_TEST_FAIL;
		}
	}

	ast_dns_resolver_unregister(&recurring_resolver);

	if (ast_dns_resolver_register(&terrible_resolver)) {
		ast_test_status_update(test, "Failed to register the DNS resolver\n");
		return AST_TEST_FAIL;
	}

	recurring = ast_dns_resolve_recurring("asterisk.org", ns_t_a, ns_c_in, stub_callback, NULL);

	ast_dns_resolver_unregister(&terrible_resolver);

	if (recurring) {
		ast_test_status_update(test, "Successfully performed recurring resolution with invalid data\n");
		ast_dns_resolve_recurring_cancel(recurring);
		ao2_ref(recurring, -1);
		return AST_TEST_FAIL;
	}

	return res;
}

AST_TEST_DEFINE(recurring_query_cancel_between)
{
	RAII_VAR(struct ast_dns_query_recurring *, recurring_query, NULL, ao2_cleanup);
	RAII_VAR(struct recurring_data *, rdata, NULL, ao2_cleanup);

	enum ast_test_result_state res = AST_TEST_PASS;
	struct timespec timeout;

	switch (cmd) {
	case TEST_INIT:
		info->name = "recurring_query_cancel_between";
		info->category = "/main/dns/recurring/";
		info->summary = "Test canceling a recurring DNS query during the downtime between queries";
		info->description = "This test does the following:\n"
			"\t* Issue a recurring DNS query.\n"
			"\t* Once results have been returned, cancel the recurring query.\n"
			"\t* Wait a while to ensure that no more queries are occurring.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_dns_resolver_register(&recurring_resolver)) {
		ast_test_status_update(test, "Failed to register recurring DNS resolver\n");
		return AST_TEST_FAIL;
	}

	rdata = recurring_data_alloc();
	if (!rdata) {
		ast_test_status_update(test, "Failed to allocate data necessary for recurring test\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	rdata->ttl1 = 5;
	rdata->ttl2 = 20;

	recurring_query = ast_dns_resolve_recurring("asterisk.org", ns_t_a, ns_c_in, async_callback, rdata);
	if (!recurring_query) {
		ast_test_status_update(test, "Unable to make recurring query\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	if (wait_for_resolution(test, rdata, 0, 1, 1, 0)) {
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	if (ast_dns_resolve_recurring_cancel(recurring_query)) {
		ast_test_status_update(test, "Failed to cancel recurring query\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Query has been canceled, so let's wait to make sure that we don't get
	 * told another query has occurred.
	 */
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 10;

	ast_mutex_lock(&rdata->lock);
	while (!rdata->query_complete) {
		if (ast_cond_timedwait(&rdata->cond, &rdata->lock, &timeout) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&rdata->lock);

	if (rdata->query_complete) {
		ast_test_status_update(test, "Recurring query occurred after cancellation\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

cleanup:
	ast_dns_resolver_unregister(&recurring_resolver);
	return res;
}

AST_TEST_DEFINE(recurring_query_cancel_during)
{

	RAII_VAR(struct ast_dns_query_recurring *, recurring_query, NULL, ao2_cleanup);
	RAII_VAR(struct recurring_data *, rdata, NULL, ao2_cleanup);

	enum ast_test_result_state res = AST_TEST_PASS;
	struct timespec timeout;

	switch (cmd) {
	case TEST_INIT:
		info->name = "recurring_query_cancel_during";
		info->category = "/main/dns/recurring/";
		info->summary = "Cancel a recurring DNS query while a query is actually happening";
		info->description = "This test does the following:\n"
			"\t* Initiate a recurring DNS query.\n"
			"\t* Allow the initial query to complete, and a second query to start\n"
			"\t* Cancel the recurring query while the second query is executing\n"
			"\t* Ensure that the resolver's cancel() method was called\n"
			"\t* Wait a while to make sure that recurring queries are no longer occurring";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (ast_dns_resolver_register(&recurring_resolver)) {
		ast_test_status_update(test, "Failed to register recurring DNS resolver\n");
		return AST_TEST_FAIL;
	}

	rdata = recurring_data_alloc();
	if (!rdata) {
		ast_test_status_update(test, "Failed to allocate data necessary for recurring test\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	rdata->ttl1 = 5;
	rdata->ttl2 = 20;

	recurring_query = ast_dns_resolve_recurring("asterisk.org", ns_t_a, ns_c_in, async_callback, rdata);
	if (!recurring_query) {
		ast_test_status_update(test, "Failed to make recurring DNS query\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	if (wait_for_resolution(test, rdata, 0, 1, 1, 0)) {
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Initial query has completed. Now let's make the next query expect a cancelation */
	rdata->cancel_expected = 1;

	/* Wait to be told that the query should be canceled  */
	ast_mutex_lock(&rdata->lock);
	while (!rdata->cancel_ready) {
		ast_cond_wait(&rdata->cond, &rdata->lock);
	}
	rdata->cancel_expected = 0;
	ast_mutex_unlock(&rdata->lock);

	if (ast_dns_resolve_recurring_cancel(recurring_query)) {
		ast_test_status_update(test, "Failed to cancel recurring DNS query\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Query has been canceled. We'll be told that the query in flight has completed. */
	if (wait_for_resolution(test, rdata, 0, 2, 1, 1)) {
		res = AST_TEST_FAIL;
		goto cleanup;
	}

	/* Now ensure that no more queries get completed after cancellation. */
	clock_gettime(CLOCK_REALTIME, &timeout);
	timeout.tv_sec += 10;

	ast_mutex_lock(&rdata->lock);
	while (!rdata->query_complete) {
		if (ast_cond_timedwait(&rdata->cond, &rdata->lock, &timeout) == ETIMEDOUT) {
			break;
		}
	}
	ast_mutex_unlock(&rdata->lock);

	if (rdata->query_complete) {
		ast_test_status_update(test, "Recurring query occurred after cancellation\n");
		res = AST_TEST_FAIL;
		goto cleanup;
	}

cleanup:
	ast_dns_resolver_unregister(&recurring_resolver);
	return res;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(recurring_query);
	AST_TEST_UNREGISTER(recurring_query_off_nominal);
	AST_TEST_UNREGISTER(recurring_query_cancel_between);
	AST_TEST_UNREGISTER(recurring_query_cancel_during);

	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(recurring_query);
	AST_TEST_REGISTER(recurring_query_off_nominal);
	AST_TEST_REGISTER(recurring_query_cancel_between);
	AST_TEST_REGISTER(recurring_query_cancel_during);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Recurring DNS query tests");
