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
	<depend>unbound</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <unbound.h>
#include <arpa/nameser.h>

#include "asterisk/module.h"
#include "asterisk/linkedlists.h"
#include "asterisk/dns_core.h"
#include "asterisk/dns_resolver.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/test.h"

/*** DOCUMENTATION
	<configInfo name="res_resolver_unbound" language="en_US">
		<configFile name="resolver_unbound.conf">
			<configObject name="globals">
				<synopsis>Options that apply globally to res_resolver_unbound</synopsis>
				<configOption name="hosts">
					<synopsis>Full path to an optional hosts file</synopsis>
					<description><para>Hosts specified in a hosts file will be resolved within the resolver itself. If a value
					of system is provided the system-specific file will be used.</para></description>
				</configOption>
				<configOption name="resolv">
					<synopsis>Full path to an optional resolv.conf file</synopsis>
					<description><para>The resolv.conf file specifies the nameservers to contact when resolving queries. If a
					value of system is provided the system-specific file will be used. If provided alongside explicit nameservers the
					nameservers contained within the resolv.conf file will be used after all others.</para></description>
				</configOption>
				<configOption name="nameserver">
					<synopsis>Nameserver to use for queries</synopsis>
					<description><para>An explicit nameserver can be specified which is used for resolving queries. If multiple
					nameserver lines are specified the first will be the primary with failover occurring, in order, to the other
					nameservers as backups. If provided alongside a resolv.conf file the nameservers explicitly specified will be
					used before all others.</para></description>
				</configOption>
				<configOption name="debug">
					<synopsis>Unbound debug level</synopsis>
					<description><para>The debugging level for the unbound resolver. While there is no explicit range generally
					the higher the number the more debug is output.</para></description>
				</configOption>
				<configOption name="ta_file">
					<synopsis>Trust anchor file</synopsis>
					<description><para>Full path to a file with DS and DNSKEY records in zone file format. This file is provided
					to unbound and is used as a source for trust anchors.</para></description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/*! \brief Structure for an unbound resolver */
struct unbound_resolver {
	/*! \brief Resolver context itself */
	struct ub_ctx *context;
	/*! \brief Thread handling the resolver */
	pthread_t thread;
};

/*! \brief Structure for query resolver data */
struct unbound_resolver_data {
	/*! \brief ID for the specific query */
	int id;
	/*! \brief The resolver in use for the query */
	struct unbound_resolver *resolver;
};

/*! \brief Unbound configuration state information */
struct unbound_config_state {
	/*! \brief The configured resolver */
	struct unbound_resolver *resolver;
};

/*! \brief A structure to hold global configuration-related options */
struct unbound_global_config {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(hosts);   /*!< Optional hosts file */
		AST_STRING_FIELD(resolv);  /*!< Optional resolv.conf file */
		AST_STRING_FIELD(ta_file); /*!< Optional trust anchor file */
	);
	/*! \brief List of nameservers (in order) to use for queries */
	struct ao2_container *nameservers;
	/*! \brief Debug level for the resolver */
	unsigned int debug;
	/*! \brief State information */
	struct unbound_config_state *state;
};

/*! \brief A container for config related information */
struct unbound_config {
	struct unbound_global_config *global;
};

/*!
 * \brief Allocate a unbound_config to hold a snapshot of the complete results of parsing a config
 * \internal
 * \returns A void pointer to a newly allocated unbound_config
 */
static void *unbound_config_alloc(void);

/*! \brief An aco_type structure to link the "general" category to the unbound_global_config type */
static struct aco_type global_option = {
	.type = ACO_GLOBAL,
	.name = "globals",
	.item_offset = offsetof(struct unbound_config, global),
	.category_match = ACO_WHITELIST,
	.category = "^general$",
};

static struct aco_type *global_options[] = ACO_TYPES(&global_option);

static struct aco_file resolver_unbound_conf = {
	.filename = "resolver_unbound.conf",
	.types = ACO_TYPES(&global_option),
};

/*! \brief A global object container that will contain the global_config that gets swapped out on reloads */
static AO2_GLOBAL_OBJ_STATIC(globals);

/*!
 * \brief Finish initializing new configuration
 * \internal
 */
static int unbound_config_preapply_callback(void);

/*! \brief Register information about the configs being processed by this module */
CONFIG_INFO_STANDARD(cfg_info, globals, unbound_config_alloc,
	.files = ACO_FILES(&resolver_unbound_conf),
	.pre_apply_config = unbound_config_preapply_callback,
);

/*! \brief Destructor for unbound resolver */
static void unbound_resolver_destroy(void *obj)
{
	struct unbound_resolver *resolver = obj;

	if (resolver->context) {
		ub_ctx_delete(resolver->context);
	}
}

/*! \brief Allocator for unbound resolver */
static struct unbound_resolver *unbound_resolver_alloc(void)
{
	struct unbound_resolver *resolver;

	resolver = ao2_alloc_options(sizeof(*resolver), unbound_resolver_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!resolver) {
		return NULL;
	}

	resolver->thread = AST_PTHREADT_NULL;

	resolver->context = ub_ctx_create();
	if (!resolver->context) {
		ao2_ref(resolver, -1);
		return NULL;
	}

	/* Each async result should be invoked in a separate thread so others are not blocked */
	ub_ctx_async(resolver->context, 1);

	return resolver;
}

/*! \brief Resolver thread which waits and handles results */
static void *unbound_resolver_thread(void *data)
{
	struct unbound_resolver *resolver = data;

	ast_debug(1, "Starting processing for unbound resolver\n");

	while (resolver->thread != AST_PTHREADT_STOP) {
		/* Wait for any results to come in */
		ast_wait_for_input(ub_fd(resolver->context), -1);

		/* Finally process any results */
		ub_process(resolver->context);
	}

	ast_debug(1, "Terminating processing for unbound resolver\n");

	ao2_ref(resolver, -1);

	return NULL;
}

/*! \brief Start function for the unbound resolver */
static int unbound_resolver_start(struct unbound_resolver *resolver)
{
	int res;

	if (resolver->thread != AST_PTHREADT_NULL) {
		return 0;
	}

	ast_debug(1, "Starting thread for unbound resolver\n");

	res = ast_pthread_create(&resolver->thread, NULL, unbound_resolver_thread, ao2_bump(resolver));
	if (res) {
		ast_debug(1, "Could not start thread for unbound resolver\n");
		ao2_ref(resolver, -1);
	}

	return res;
}

/*! \brief Stop function for the unbound resolver */
static void unbound_resolver_stop(struct unbound_resolver *resolver)
{
	pthread_t thread;

	if (resolver->thread == AST_PTHREADT_NULL) {
		return;
	}

	ast_debug(1, "Stopping processing thread for unbound resolver\n");

	thread = resolver->thread;
	resolver->thread = AST_PTHREADT_STOP;
	pthread_kill(thread, SIGURG);
	pthread_join(thread, NULL);

	ast_debug(1, "Stopped processing thread for unbound resolver\n");
}

/*! \brief Callback invoked when resolution completes on a query */
static void unbound_resolver_callback(void *data, int err, struct ub_result *ub_result)
{
	struct ast_dns_query *query = data;

	if (!ast_dns_resolver_set_result(query, ub_result->secure, ub_result->bogus, ub_result->rcode,
		S_OR(ub_result->canonname, ast_dns_query_get_name(query)), ub_result->answer_packet, ub_result->answer_len)) {
		int i;
		char *data;

		for (i = 0; (data = ub_result->data[i]); i++) {
			if (ast_dns_resolver_add_record(query, ub_result->qtype, ub_result->qclass, ub_result->ttl,
				data, ub_result->len[i])) {
				break;
			}
		}
	}

	ast_dns_resolver_completed(query);
	ao2_ref(query, -1);
	ub_resolve_free(ub_result);
}

static int unbound_resolver_resolve(struct ast_dns_query *query)
{
	struct unbound_config *cfg = ao2_global_obj_ref(globals);
	struct unbound_resolver_data *data;
	int res;

	data = ao2_alloc_options(sizeof(*data), NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!data) {
		ast_log(LOG_ERROR, "Failed to allocate resolver data for resolution of '%s'\n",
			ast_dns_query_get_name(query));
		return -1;
	}
	data->resolver = ao2_bump(cfg->global->state->resolver);
	ast_dns_resolver_set_data(query, data);

	res = ub_resolve_async(data->resolver->context, ast_dns_query_get_name(query),
		ast_dns_query_get_rr_type(query), ast_dns_query_get_rr_class(query),
		ao2_bump(query), unbound_resolver_callback, &data->id);

	if (res) {
		ast_log(LOG_ERROR, "Failed to perform async DNS resolution of '%s'\n",
			ast_dns_query_get_name(query));
		ao2_ref(query, -1);
	}

	ao2_ref(data, -1);
	ao2_ref(cfg, -1);

	return res;
}

static int unbound_resolver_cancel(struct ast_dns_query *query)
{
	struct unbound_resolver_data *data = ast_dns_resolver_get_data(query);
	int res;

	res = ub_cancel(data->resolver->context, data->id);
	if (!res) {
		/* When this query was started we bumped the ref, now that it has been cancelled we have ownership and
		 * need to drop it
		 */
		ao2_ref(query, -1);
	}

	return res;
}

struct ast_dns_resolver unbound_resolver = {
	.name = "unbound",
	.priority = 100,
	.resolve = unbound_resolver_resolve,
	.cancel = unbound_resolver_cancel,
};

static void unbound_config_destructor(void *obj)
{
	struct unbound_config *cfg = obj;

	ao2_cleanup(cfg->global);
}

static void unbound_global_config_destructor(void *obj)
{
	struct unbound_global_config *global = obj;

	ast_string_field_free_memory(global);
	ao2_cleanup(global->nameservers);
	ao2_cleanup(global->state);
}

static void unbound_config_state_destructor(void *obj)
{
	struct unbound_config_state *state = obj;

	if (state->resolver) {
		unbound_resolver_stop(state->resolver);
		ao2_ref(state->resolver, -1);
	}
}

static void *unbound_config_alloc(void)
{
	struct unbound_config *cfg;

	cfg = ao2_alloc_options(sizeof(*cfg), unbound_config_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg) {
		return NULL;
	}

	/* Allocate/initialize memory */
	cfg->global = ao2_alloc_options(sizeof(*cfg->global), unbound_global_config_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg->global) {
		goto error;
	}

	if (ast_string_field_init(cfg->global, 128)) {
		goto error;
	}

	return cfg;
error:
	ao2_ref(cfg, -1);
	return NULL;
}

static int unbound_config_preapply(struct unbound_config *cfg)
{
	int res = 0;

	cfg->global->state = ao2_alloc_options(sizeof(*cfg->global->state), unbound_config_state_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg->global->state) {
		ast_log(LOG_ERROR, "Could not allocate unbound resolver state structure\n");
		return -1;
	}

	cfg->global->state->resolver = unbound_resolver_alloc();
	if (!cfg->global->state->resolver) {
		ast_log(LOG_ERROR, "Could not create an unbound resolver\n");
		return -1;
	}

	ub_ctx_debuglevel(cfg->global->state->resolver->context, cfg->global->debug);

	if (!strcmp(cfg->global->hosts, "system")) {
		res = ub_ctx_hosts(cfg->global->state->resolver->context, NULL);
	} else if (!ast_strlen_zero(cfg->global->hosts)) {
		res = ub_ctx_hosts(cfg->global->state->resolver->context, cfg->global->hosts);
	}

	if (res) {
		ast_log(LOG_ERROR, "Failed to set hosts file to '%s' in unbound resolver: %s\n",
			cfg->global->hosts, ub_strerror(res));
		return -1;
	}

	if (cfg->global->nameservers) {
		struct ao2_iterator it_nameservers;
		const char *nameserver;

		it_nameservers = ao2_iterator_init(cfg->global->nameservers, 0);
		while ((nameserver = ao2_iterator_next(&it_nameservers))) {
			res = ub_ctx_set_fwd(cfg->global->state->resolver->context, nameserver);

			if (res) {
				ast_log(LOG_ERROR, "Failed to add nameserver '%s' to unbound resolver: %s\n",
					nameserver, ub_strerror(res));
				ao2_iterator_destroy(&it_nameservers);
				return -1;
			}
		}
		ao2_iterator_destroy(&it_nameservers);
	}

	if (!strcmp(cfg->global->resolv, "system")) {
		res = ub_ctx_resolvconf(cfg->global->state->resolver->context, NULL);
	} else if (!ast_strlen_zero(cfg->global->resolv)) {
		res = ub_ctx_resolvconf(cfg->global->state->resolver->context, cfg->global->resolv);
	}

	if (res) {
		ast_log(LOG_ERROR, "Failed to set resolv.conf file to '%s' in unbound resolver: %s\n",
			cfg->global->resolv, ub_strerror(res));
		return -1;
	}

	if (!ast_strlen_zero(cfg->global->ta_file)) {
		res = ub_ctx_add_ta_file(cfg->global->state->resolver->context, cfg->global->ta_file);

		if (res) {
			ast_log(LOG_ERROR, "Failed to set trusted anchor file to '%s' in unbound resolver: %s\n",
				cfg->global->ta_file, ub_strerror(res));
			return -1;
		}
	}

	if (unbound_resolver_start(cfg->global->state->resolver)) {
		ast_log(LOG_ERROR, "Could not start unbound resolver thread\n");
		return -1;
	}

	return 0;
}

static int unbound_config_apply_default(void)
{
	struct unbound_config *cfg;

	cfg = unbound_config_alloc();
	if (!cfg) {
		ast_log(LOG_ERROR, "Could not create default configuration for unbound resolver\n");
		return -1;
	}

	aco_set_defaults(&global_option, "general", cfg->global);

	if (unbound_config_preapply(cfg)) {
		ao2_ref(cfg, -1);
		return -1;
	}

	ast_verb(1, "Starting unbound resolver using default configuration\n");

	ao2_global_obj_replace_unref(globals, cfg);
	ao2_ref(cfg, -1);

	return 0;
}

static int unbound_config_preapply_callback(void)
{
	return unbound_config_preapply(aco_pending_config(&cfg_info));
}

#ifdef TEST_FRAMEWORK

/*!
 * \brief A DNS record to be used during a test
 */
struct dns_record {
	/*! String representation of the record, as would be found in a file */
	const char *as_string;
	/*! The domain this record belongs to */
	const char *domain;
	/*! The type of the record */
	int rr_type;
	/*! The class of the record */
	int rr_class;
	/*! The TTL of the record, in seconds */
	int ttl;
	/*! The RDATA of the DNS record */
	const char *buf;
	/*! The size of the RDATA */
	const size_t bufsize;
	/*! Whether a record checker has visited this record */
	int visited;
};

/*!
 * \brief Resolution function for tests.
 *
 * Several tests will have similar setups but will want to make use of a different
 * means of actually making queries and checking their results. This pluggable
 * function pointer allows for similar tests to be operated in different ways.
 *
 * \param test The test being run
 * \param domain The domain to look up
 * \param rr_type The record type to look up
 * \param rr_class The class of record to look up
 * \param records All records that exist for the test.
 * \param num_records Number of records in the records array.
 *
 * \retval 0 The test has passed thus far.
 * \retval -1 The test has failed.
 */
typedef int (*resolve_fn)(struct ast_test *test, const char *domain, int rr_type,
		int rr_class, struct dns_record *records, size_t num_records);

/*!
 * \brief Pluggable function for running a synchronous query and checking its results
 */
static int nominal_sync_run(struct ast_test *test, const char *domain, int rr_type,
		int rr_class, struct dns_record *records, size_t num_records)
{
	RAII_VAR(struct ast_dns_result *, result, NULL, ast_dns_result_free);
	const struct ast_dns_record *record;
	int i;

	/* Start by making sure no records have been visited */
	for (i = 0; i < num_records; ++i) {
		records[i].visited = 0;
	}

	ast_test_status_update(test, "Performing DNS query '%s', type %d\n", domain, rr_type);

	if (ast_dns_resolve(domain, rr_type, rr_class, &result)) {
		ast_test_status_update(test, "Failed to perform synchronous resolution of domain %s\n", domain);
		return -1;
	}

	if (!result) {
		ast_test_status_update(test, "Successful synchronous resolution of domain %s gave NULL result\n", domain);
		return -1;
	}

	for (record = ast_dns_result_get_records(result); record; record = ast_dns_record_get_next(record)) {
		int match = 0;

		/* Let's make sure this matches one of our known records */
		for (i = 0; i < num_records; ++i) {
			if (ast_dns_record_get_rr_type(record) == records[i].rr_type &&
					ast_dns_record_get_rr_class(record) == records[i].rr_class &&
					ast_dns_record_get_ttl(record) == records[i].ttl &&
					!memcmp(ast_dns_record_get_data(record), records[i].buf, records[i].bufsize)) {
				match = 1;
				records[i].visited = 1;
				break;
			}
		}

		if (!match) {
			ast_test_status_update(test, "Unknown DNS record returned from domain %s\n", domain);
			return -1;
		}
	}

	return 0;
}

/*!
 * \brief Data required for an asynchronous callback
 */
struct async_data {
	/*! The set of DNS records on a test */
	struct dns_record *records;
	/*! The number of DNS records on the test */
	size_t num_records;
	/*! Whether an asynchronous query failed */
	int failed;
	/*! Indicates the asynchronous query is complete */
	int complete;
	ast_mutex_t lock;
	ast_cond_t cond;
};

static void async_data_destructor(void *obj)
{
	struct async_data *adata = obj;

	ast_mutex_destroy(&adata->lock);
	ast_cond_destroy(&adata->cond);
}

static struct async_data *async_data_alloc(struct dns_record *records, size_t num_records)
{
	struct async_data *adata;

	adata = ao2_alloc(sizeof(*adata), async_data_destructor);
	if (!adata) {
		return NULL;
	}

	ast_mutex_init(&adata->lock);
	ast_cond_init(&adata->cond, NULL);
	adata->records = records;
	adata->num_records = num_records;

	return adata;
}

/*!
 * \brief Callback for asynchronous queries
 *
 * This query will check that the records in the DNS result match
 * records that the test has created. The success or failure of the
 * query is indicated through the async_data failed field.
 *
 * \param query The DNS query that has been resolved
 */
static void async_callback(const struct ast_dns_query *query)
{
	struct async_data *adata = ast_dns_query_get_data(query);
	struct ast_dns_result *result = ast_dns_query_get_result(query);
	const struct ast_dns_record *record;
	int i;

	if (!result) {
		adata->failed = -1;
		goto end;
	}

	for (record = ast_dns_result_get_records(result); record; record = ast_dns_record_get_next(record)) {
		int match = 0;

		/* Let's make sure this matches one of our known records */
		for (i = 0; i < adata->num_records; ++i) {
			if (ast_dns_record_get_rr_type(record) == adata->records[i].rr_type &&
					ast_dns_record_get_rr_class(record) == adata->records[i].rr_class &&
					ast_dns_record_get_ttl(record) == adata->records[i].ttl &&
					!memcmp(ast_dns_record_get_data(record), adata->records[i].buf, adata->records[i].bufsize)) {
				match = 1;
				adata->records[i].visited = 1;
				break;
			}
		}

		if (!match) {
			adata->failed = -1;
			goto end;
		}
	}

end:
	ast_mutex_lock(&adata->lock);
	adata->complete = 1;
	ast_cond_signal(&adata->cond);
	ast_mutex_unlock(&adata->lock);
}

/*!
 * \brief Pluggable function for performing an asynchronous query during a test
 *
 * Unlike the synchronous version, this does not check the records, instead leaving
 * that to be done in the asynchronous callback.
 */
static int nominal_async_run(struct ast_test *test, const char *domain, int rr_type,
		int rr_class, struct dns_record *records, size_t num_records)
{
	RAII_VAR(struct ast_dns_query_active *, active, NULL, ao2_cleanup);
	RAII_VAR(struct async_data *, adata, NULL, ao2_cleanup);
	int i;

	adata = async_data_alloc(records, num_records);
	if (!adata) {
		ast_test_status_update(test, "Unable to allocate data for async query\n");
		return -1;
	}

	/* Start by making sure no records have been visited */
	for (i = 0; i < num_records; ++i) {
		records[i].visited = 0;
	}

	ast_test_status_update(test, "Performing DNS query '%s', type %d\n", domain, rr_type);

	active = ast_dns_resolve_async(domain, rr_type, rr_class, async_callback, adata);
	if (!active) {
		ast_test_status_update(test, "Failed to perform asynchronous resolution of domain %s\n", domain);
		return -1;
	}

	ast_mutex_lock(&adata->lock);
	while (!adata->complete) {
		ast_cond_wait(&adata->cond, &adata->lock);
	}
	ast_mutex_unlock(&adata->lock);

	if (adata->failed) {
		ast_test_status_update(test, "Unknown DNS record returned from domain %s\n", domain);
	}
	return adata->failed;
}

/*!
 * \brief Framework for running a nominal DNS test
 *
 * Synchronous and asynchronous tests mostly have the same setup, so this function
 * serves as a common way to set up both types of tests by accepting a pluggable
 * function to determine which type of lookup is used
 *
 * \param test The test being run
 * \param runner The method for resolving queries on this test
 */
static enum ast_test_result_state nominal_test(struct ast_test *test, resolve_fn runner)
{
	RAII_VAR(struct unbound_resolver *, resolver, NULL, ao2_cleanup);
	RAII_VAR(struct unbound_config *, cfg, NULL, ao2_cleanup);

	static const size_t V4_SIZE = sizeof(struct in_addr);
	static const size_t V6_SIZE = sizeof(struct in6_addr);

	static const char *DOMAIN1 = "goose.feathers";
	static const char *DOMAIN2 = "duck.feathers";

	static const char *ADDR1 = "127.0.0.2";
	static const char *ADDR2 = "127.0.0.3";
	static const char *ADDR3 = "::1";
	static const char *ADDR4 = "127.0.0.4";

	char addr1_buf[V4_SIZE];
	char addr2_buf[V4_SIZE];
	char addr3_buf[V6_SIZE];
	char addr4_buf[V4_SIZE];

	struct dns_record records [] = {
		{ "goose.feathers 12345 IN A 127.0.0.2", DOMAIN1, ns_t_a,    ns_c_in, 12345, addr1_buf, V4_SIZE, 0 },
		{ "goose.feathers 12345 IN A 127.0.0.3", DOMAIN1, ns_t_a,    ns_c_in, 12345, addr2_buf, V4_SIZE, 0 },
		{ "goose.feathers 12345 IN AAAA ::1",    DOMAIN1, ns_t_aaaa, ns_c_in, 12345, addr3_buf, V6_SIZE, 0 },
		{ "duck.feathers 12345 IN A 127.0.0.4",  DOMAIN2, ns_t_a,    ns_c_in, 12345, addr4_buf, V4_SIZE, 0 },
	};

	struct {
		const char *domain;
		int rr_type;
		int rr_class;
		int visited[ARRAY_LEN(records)];
	} runs [] = {
		{ DOMAIN1, ns_t_a,    ns_c_in, { 1, 1, 0, 0 } },
		{ DOMAIN1, ns_t_aaaa, ns_c_in, { 0, 0, 1, 0 } },
		{ DOMAIN2, ns_t_a,    ns_c_in, { 0, 0, 0, 1 } },
	};

	int i;
	enum ast_test_result_state res = AST_TEST_PASS;

	inet_pton(AF_INET,  ADDR1, addr1_buf);
	inet_pton(AF_INET,  ADDR2, addr2_buf);
	inet_pton(AF_INET6,  ADDR3, addr3_buf);
	inet_pton(AF_INET, ADDR4, addr4_buf);

	cfg = ao2_global_obj_ref(globals);
	resolver = ao2_bump(cfg->global->state->resolver);

	ub_ctx_zone_add(resolver->context, DOMAIN1, "static");
	ub_ctx_zone_add(resolver->context, DOMAIN2, "static");

	for (i = 0; i < ARRAY_LEN(records); ++i) {
		ub_ctx_data_add(resolver->context, records[i].as_string);
	}

	for (i = 0; i < ARRAY_LEN(runs); ++i) {
		int j;

		if (runner(test, runs[i].domain, runs[i].rr_type, runs[i].rr_class, records, ARRAY_LEN(records))) {
			res = AST_TEST_FAIL;
			goto cleanup;
		}

		for (j = 0; j < ARRAY_LEN(records); ++j) {
			if (records[j].visited != runs[i].visited[j]) {
				ast_test_status_update(test, "DNS results match unexpected records\n");
				res = AST_TEST_FAIL;
				goto cleanup;
			}
		}
	}

cleanup:
	for (i = 0; i < ARRAY_LEN(records); ++i) {
		ub_ctx_data_remove(resolver->context, records[i].as_string);
	}
	ub_ctx_zone_remove(resolver->context, DOMAIN1);
	ub_ctx_zone_remove(resolver->context, DOMAIN2);

	return res;
}

AST_TEST_DEFINE(resolve_sync)
{

	switch (cmd) {
	case TEST_INIT:
		info->name = "resolve_sync";
		info->category = "/res/res_resolver_unbound/";
		info->summary = "Test nominal synchronous resolution using libunbound\n";
		info->description = "This test performs the following:\n"
			"\t* Set two static A records and one static AAAA record on one domain\n"
			"\t* Set an A record for a second domain\n"
			"\t* Perform an A record lookup on the first domain\n"
			"\t* Ensure that both A records are returned and no AAAA record is returned\n"
			"\t* Perform an AAAA record lookup on the first domain\n"
			"\t* Ensure that the AAAA record is returned and no A record is returned\n"
			"\t* Perform an A record lookup on the second domain\n"
			"\t* Ensure that the A record from the second domain is returned\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_test(test, nominal_sync_run);
}

AST_TEST_DEFINE(resolve_async)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "resolve_async";
		info->category = "/res/res_resolver_unbound/";
		info->summary = "Test nominal asynchronous resolution using libunbound\n";
		info->description = "This test performs the following:\n"
			"\t* Set two static A records and one static AAAA record on one domain\n"
			"\t* Set an A record for a second domain\n"
			"\t* Perform an A record lookup on the first domain\n"
			"\t* Ensure that both A records are returned and no AAAA record is returned\n"
			"\t* Perform an AAAA record lookup on the first domain\n"
			"\t* Ensure that the AAAA record is returned and no A record is returned\n"
			"\t* Perform an A record lookup on the second domain\n"
			"\t* Ensure that the A record from the second domain is returned\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return nominal_test(test, nominal_async_run);
}

typedef int (*off_nominal_resolve_fn)(struct ast_test *test, const char *domain, int rr_type,
		int rr_class, int expected_rcode);

static int off_nominal_sync_run(struct ast_test *test, const char *domain, int rr_type,
		int rr_class, int expected_rcode)
{
	struct ast_dns_result *result;
	int res = 0;

	if (ast_dns_resolve(domain, rr_type, rr_class, &result)) {
		ast_test_status_update(test, "Failed to perform resolution :(\n");
		return -1;
	}

	if (!result) {
		ast_test_status_update(test, "Resolution returned no result\n");
		return -1;
	}

	if (ast_dns_result_get_rcode(result) != expected_rcode) {
		ast_test_status_update(test, "Unexpected rcode from DNS resolution\n");
		res = -1;
	}

	if (ast_dns_result_get_records(result)) {
		ast_test_status_update(test, "DNS resolution returned records unexpectedly\n");
		res = -1;
	}

	ast_dns_result_free(result);
	return res;
}

/*!
 * \brief User data for off-nominal async resolution test
 */
struct off_nominal_async_data {
	/*! The DNS result's expected rcode */
	int expected_rcode;
	/*! Whether an asynchronous query failed */
	int failed;
	/*! Indicates the asynchronous query is complete */
	int complete;
	ast_mutex_t lock;
	ast_cond_t cond;
};

static void off_nominal_async_data_destructor(void *obj)
{
	struct off_nominal_async_data *adata = obj;

	ast_mutex_destroy(&adata->lock);
	ast_cond_destroy(&adata->cond);
}

static struct off_nominal_async_data *off_nominal_async_data_alloc(int expected_rcode)
{
	struct off_nominal_async_data *adata;

	adata = ao2_alloc(sizeof(*adata), off_nominal_async_data_destructor);
	if (!adata) {
		return NULL;
	}

	ast_mutex_init(&adata->lock);
	ast_cond_init(&adata->cond, NULL);

	adata->expected_rcode = expected_rcode;

	return adata;
}

/*!
 * \brief Async callback for off-nominal async test
 *
 * This test ensures that there is a result present on the query, then it checks
 * that the rcode on the result is the expected value and that there are no
 * records on the result.
 *
 * Once completed, the testing thread is signaled that the async query has
 * completed.
 */
static void off_nominal_async_callback(const struct ast_dns_query *query)
{
	struct off_nominal_async_data *adata = ast_dns_query_get_data(query);
	struct ast_dns_result *result = ast_dns_query_get_result(query);

	if (!result) {
		adata->failed = -1;
		goto end;
	}

	if (ast_dns_result_get_rcode(result) != adata->expected_rcode) {
		adata->failed = -1;
	}

	if (ast_dns_result_get_records(result)) {
		adata->failed = -1;
	}

end:
	ast_mutex_lock(&adata->lock);
	adata->complete = 1;
	ast_cond_signal(&adata->cond);
	ast_mutex_unlock(&adata->lock);
}

static int off_nominal_async_run(struct ast_test *test, const char *domain, int rr_type,
		int rr_class, int expected_rcode)
{
	RAII_VAR(struct ast_dns_query_active *, active, NULL, ao2_cleanup);
	RAII_VAR(struct off_nominal_async_data *, adata, NULL, ao2_cleanup);

	adata = off_nominal_async_data_alloc(expected_rcode);
	if (!adata) {
		ast_test_status_update(test, "Unable to allocate data for async query\n");
		return -1;
	}

	ast_test_status_update(test, "Performing DNS query '%s', type %d\n", domain, rr_type);

	active = ast_dns_resolve_async(domain, rr_type, rr_class, off_nominal_async_callback, adata);
	if (!active) {
		ast_test_status_update(test, "Failed to perform asynchronous resolution of domain %s\n", domain);
		return -1;
	}

	ast_mutex_lock(&adata->lock);
	while (!adata->complete) {
		ast_cond_wait(&adata->cond, &adata->lock);
	}
	ast_mutex_unlock(&adata->lock);

	if (adata->failed) {
		ast_test_status_update(test, "Asynchronous resolution failure %s\n", domain);
	}
	return adata->failed;
}

static enum ast_test_result_state off_nominal_test(struct ast_test *test,
		off_nominal_resolve_fn runner)
{
	RAII_VAR(struct unbound_resolver *, resolver, NULL, ao2_cleanup);
	RAII_VAR(struct unbound_config *, cfg, NULL, ao2_cleanup);

	static const size_t V4_SIZE = sizeof(struct in_addr);

	static const char *DOMAIN1 = "goose.feathers";
	static const char *DOMAIN2 = "duck.feathers";

	static const char *ADDR1 = "127.0.0.2";

	char addr1_buf[V4_SIZE];

	struct dns_record records [] = {
		{ "goose.feathers 12345 IN A 127.0.0.2", DOMAIN1, ns_t_a, ns_c_in, 12345, addr1_buf, V4_SIZE, 0, },
	};

	int i;
	enum ast_test_result_state res = AST_TEST_PASS;

	struct {
		const char *domain;
		int rr_type;
		int rr_class;
		int rcode;
	} runs [] = {
		{ DOMAIN2, ns_t_a,    ns_c_in, ns_r_nxdomain },
		{ DOMAIN1, ns_t_aaaa, ns_c_in, ns_r_noerror },
		{ DOMAIN1, ns_t_a,    ns_c_chaos, ns_r_refused },
	};

	inet_pton(AF_INET,  ADDR1, addr1_buf);

	cfg = ao2_global_obj_ref(globals);
	resolver = ao2_bump(cfg->global->state->resolver);

	ub_ctx_zone_add(resolver->context, DOMAIN1, "static");
	ub_ctx_zone_add(resolver->context, DOMAIN2, "static");

	for (i = 0; i < ARRAY_LEN(records); ++i) {
		ub_ctx_data_add(resolver->context, records[i].as_string);
	}

	for (i = 0; i < ARRAY_LEN(runs); ++i) {
		if (runner(test, runs[i].domain, runs[i].rr_type, runs[i].rr_class, runs[i].rcode)) {
			res = AST_TEST_FAIL;
		}
	}

	return res;
}

AST_TEST_DEFINE(resolve_sync_off_nominal)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "resolve_sync_off_nominal";
		info->category = "/res/res_resolver_unbound/";
		info->summary = "Test off-nominal synchronous resolution using libunbound\n";
		info->description = "This test performs the following:\n"
			"\t* Attempt a lookup of a non-existent domain\n"
			"\t* Attempt a lookup of a AAAA record on a domain that contains only A records\n"
			"\t* Attempt a lookup of an A record on Chaos-net\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return off_nominal_test(test, off_nominal_sync_run);
}

AST_TEST_DEFINE(resolve_async_off_nominal)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "resolve_async_off_nominal";
		info->category = "/res/res_resolver_unbound/";
		info->summary = "Test off-nominal synchronous resolution using libunbound\n";
		info->description = "This test performs the following:\n"
			"\t* Attempt a lookup of a non-existent domain\n"
			"\t* Attempt a lookup of a AAAA record on a domain that contains only A records\n"
			"\t* Attempt a lookup of an A record on Chaos-net\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	return off_nominal_test(test, off_nominal_async_run);
}

/*!
 * \brief Minimal data required to signal the completion of an async resolve
 */
struct async_minimal_data {
	int complete;
	ast_mutex_t lock;
	ast_cond_t cond;
};

static void async_minimal_data_destructor(void *obj)
{
	struct async_minimal_data *adata = obj;

	ast_mutex_destroy(&adata->lock);
	ast_cond_destroy(&adata->cond);
}

static struct async_minimal_data *async_minimal_data_alloc(void)
{
	struct async_minimal_data *adata;

	adata = ao2_alloc(sizeof(*adata), async_minimal_data_destructor);
	if (!adata) {
		return NULL;
	}

	ast_mutex_init(&adata->lock);
	ast_cond_init(&adata->cond, NULL);

	return adata;
}

/*!
 * \brief Async callback for off-nominal cancellation test.
 *
 * This simply signals the testing thread that the query completed
 */
static void minimal_callback(const struct ast_dns_query *query)
{
	struct async_minimal_data *adata = ast_dns_query_get_data(query);

	ast_mutex_lock(&adata->lock);
	adata->complete = 1;
	ast_cond_signal(&adata->cond);
	ast_mutex_unlock(&adata->lock);
}

AST_TEST_DEFINE(resolve_cancel_off_nominal)
{
	RAII_VAR(struct ast_dns_query_active *, active, NULL, ao2_cleanup);
	RAII_VAR(struct async_minimal_data *, adata, NULL, ao2_cleanup);

	switch (cmd) {
	case TEST_INIT:
		info->name = "resolve_cancel_off_nominal";
		info->category = "/res/res_resolver_unbound/";
		info->summary = "Off nominal cancellation test using libunbound\n";
		info->description = "This test does the following:\n"
			"\t* Perform an asynchronous query\n"
			"\t* Once the query has completed, attempt to cancel it\n";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	adata = async_minimal_data_alloc();
	if (!adata) {
		ast_test_status_update(test, "Failed to allocate necessary data for test\n");
		return AST_TEST_FAIL;
	}

	active = ast_dns_resolve_async("crunchy.peanut.butter", ns_t_a, ns_c_in, minimal_callback, adata);
	if (!active) {
		ast_test_status_update(test, "Failed to perform asynchronous query\n");
		return AST_TEST_FAIL;
	}

	/* Wait for async query to complete */
	ast_mutex_lock(&adata->lock);
	while (!adata->complete) {
		ast_cond_wait(&adata->cond, &adata->lock);
	}
	ast_mutex_unlock(&adata->lock);

	if (!ast_dns_resolve_cancel(active)) {
		ast_test_status_update(test, "Successfully canceled completed query\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}
#endif

static int reload_module(void)
{
	if (aco_process_config(&cfg_info, 1) == ACO_PROCESS_ERROR) {
		return AST_MODULE_RELOAD_ERROR;
	}

	return 0;
}

static int unload_module(void)
{
	aco_info_destroy(&cfg_info);
	ao2_global_obj_release(globals);

	AST_TEST_UNREGISTER(resolve_sync);
	AST_TEST_UNREGISTER(resolve_async);
	AST_TEST_UNREGISTER(resolve_sync_off_nominal);
	AST_TEST_UNREGISTER(resolve_sync_off_nominal);
	AST_TEST_UNREGISTER(resolve_cancel_off_nominal);
	return 0;
}

static int custom_nameserver_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct unbound_global_config *global = obj;

	if (!global->nameservers) {
		global->nameservers = ast_str_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK, 1);
		if (!global->nameservers) {
			return -1;
		}
	}

	return ast_str_container_add(global->nameservers, var->value);
}

static int load_module(void)
{
	struct ast_config *cfg;
	struct ast_flags cfg_flags = { 0, };

	if (aco_info_init(&cfg_info)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	aco_option_register(&cfg_info, "hosts", ACO_EXACT, global_options, "system", OPT_STRINGFIELD_T, 0, STRFLDSET(struct unbound_global_config, hosts));
	aco_option_register(&cfg_info, "resolv", ACO_EXACT, global_options, "system", OPT_STRINGFIELD_T, 0, STRFLDSET(struct unbound_global_config, resolv));
	aco_option_register_custom(&cfg_info, "nameserver", ACO_EXACT, global_options, "", custom_nameserver_handler, 0);
	aco_option_register(&cfg_info, "debug", ACO_EXACT, global_options, "0", OPT_UINT_T, 0, FLDSET(struct unbound_global_config, debug));
	aco_option_register(&cfg_info, "ta_file", ACO_EXACT, global_options, "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct unbound_global_config, ta_file));

	/* This purposely checks for a configuration file so we don't output an error message in ACO if one is not present */
	cfg = ast_config_load(resolver_unbound_conf.filename, cfg_flags);
	if (!cfg) {
		if (unbound_config_apply_default()) {
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}
	} else {
		ast_config_destroy(cfg);
		if (aco_process_config(&cfg_info, 0) == ACO_PROCESS_ERROR) {
			unload_module();
			return AST_MODULE_LOAD_DECLINE;
		}
	}

	ast_dns_resolver_register(&unbound_resolver);

	ast_module_shutdown_ref(ast_module_info->self);

	AST_TEST_REGISTER(resolve_sync);
	AST_TEST_REGISTER(resolve_async);
	AST_TEST_REGISTER(resolve_sync_off_nominal);
	AST_TEST_REGISTER(resolve_async_off_nominal);
	AST_TEST_REGISTER(resolve_cancel_off_nominal);

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Unbound DNS Resolver Support",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.reload = reload_module,
		.load_pri = AST_MODPRI_CHANNEL_DEPEND - 4,
	       );
