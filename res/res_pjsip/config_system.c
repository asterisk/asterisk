/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/sorcery.h"
#include "include/res_pjsip_private.h"
#include "asterisk/threadpool.h"
#include "asterisk/dns.h"
#include "asterisk/res_pjsip_cli.h"

#define TIMER_T1_MIN 100
#define DEFAULT_TIMER_T1 500
#define DEFAULT_TIMER_B 32000

struct system_config {
	SORCERY_OBJECT(details);
	/*! Transaction Timer T1 value */
	unsigned int timert1;
	/*! Transaction Timer B value */
	unsigned int timerb;
	/*! Should we use short forms for headers? */
	unsigned int compactheaders;
	struct {
		/*! Initial number of threads in the threadpool */
		int initial_size;
		/*! The amount by which the number of threads is incremented when necessary */
		int auto_increment;
		/*! Thread idle timeout in seconds */
		int idle_timeout;
		/*! Maxumum number of threads in the threadpool */
		int max_size;
	} threadpool;
	/*! Nonzero to disable switching from UDP to TCP transport */
	unsigned int disable_tcp_switch;
	/*!
	 * Although early media is enabled in pjproject by default, it's only
	 * enabled when the To tags are different. These options allow turning
	 * on or off the feature for different tags and same tags.
	 */
	unsigned int follow_early_media_fork;
	unsigned int accept_multiple_sdp_answers;
	/*! Disable the use of rport in outgoing requests */
	unsigned int disable_rport;
};

static struct ast_threadpool_options sip_threadpool_options = {
	.version = AST_THREADPOOL_OPTIONS_VERSION,
};

void sip_get_threadpool_options(struct ast_threadpool_options *threadpool_options)
{
	*threadpool_options = sip_threadpool_options;
}

static struct ast_sorcery *system_sorcery;

static void *system_alloc(const char *name)
{
	struct system_config *system = ast_sorcery_generic_alloc(sizeof(*system), NULL);

	if (!system) {
		return NULL;
	}

	return system;
}

static int system_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct system_config *system = obj;
	int min_timerb;

	if (system->timert1 < TIMER_T1_MIN) {
		ast_log(LOG_WARNING, "Timer T1 setting is too low. Setting to %d\n", TIMER_T1_MIN);
		system->timert1 = TIMER_T1_MIN;
	}

	min_timerb = 64 * system->timert1;

	if (system->timerb < min_timerb) {
		ast_log(LOG_WARNING, "Timer B setting is too low. Setting to %d\n", min_timerb);
		system->timerb = min_timerb;
	}

	pjsip_cfg()->tsx.t1 = system->timert1;
	pjsip_cfg()->tsx.td = system->timerb;

	pjsip_cfg()->endpt.follow_early_media_fork = system->follow_early_media_fork;
#ifdef HAVE_PJSIP_INV_ACCEPT_MULTIPLE_SDP_ANSWERS
	pjsip_cfg()->endpt.accept_multiple_sdp_answers = system->accept_multiple_sdp_answers;
#else
	if (system->accept_multiple_sdp_answers) {
		ast_log(LOG_WARNING,
			"The accept_multiple_sdp_answers flag is not supported in this version of pjproject. Ignoring\n");
	}
#endif

	if (system->compactheaders) {
#ifdef HAVE_PJSIP_ENDPOINT_COMPACT_FORM
		pjsip_cfg()->endpt.use_compact_form = PJ_TRUE;
#else
		extern pj_bool_t pjsip_use_compact_form;

		pjsip_use_compact_form = PJ_TRUE;
#endif
	}

	sip_threadpool_options.initial_size = system->threadpool.initial_size;
	sip_threadpool_options.auto_increment = system->threadpool.auto_increment;
	sip_threadpool_options.idle_timeout = system->threadpool.idle_timeout;
	sip_threadpool_options.max_size = system->threadpool.max_size;

	pjsip_cfg()->endpt.disable_tcp_switch =
		system->disable_tcp_switch ? PJ_TRUE : PJ_FALSE;

	pjsip_cfg()->endpt.disable_rport = system->disable_rport ? PJ_TRUE : PJ_FALSE;

	return 0;
}

static struct system_config *get_system_cfg(void)
{
	struct system_config *cfg;
	struct ao2_container *systems;
	systems = ast_sorcery_retrieve_by_fields(system_sorcery, "system",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	if (!systems) {
		return NULL;
	}

	cfg = ao2_find(systems, NULL, 0);
	ao2_ref(systems, -1);
	return cfg;
}

int sip_cli_print_system(struct ast_sip_cli_context *context)
{
	struct system_config *cfg = get_system_cfg();

	if (!cfg) {
		cfg = ast_sorcery_alloc(system_sorcery, "system", NULL);
		if (!cfg) {
			return -1;
		}
	}

	ast_str_append(&context->output_buffer, 0, "\nSystem Settings:\n\n");
	ast_sip_cli_print_sorcery_objectset(cfg, context, 0);

	ao2_ref(cfg, -1);
	return 0;
}

int ast_sip_initialize_system(void)
{
	RAII_VAR(struct ao2_container *, system_configs, NULL, ao2_cleanup);
	RAII_VAR(struct system_config *, system, NULL, ao2_cleanup);

	system_sorcery = ast_sorcery_open();
	if (!system_sorcery) {
		ast_log(LOG_ERROR, "Failed to open SIP system sorcery\n");
		return -1;
	}

	ast_sorcery_apply_default(system_sorcery, "system", "config", "pjsip.conf,criteria=type=system,single_object=yes,explicit_name=system");

	if (ast_sorcery_object_register_no_reload(system_sorcery, "system", system_alloc, NULL, system_apply)) {
		ast_log(LOG_ERROR, "Failed to register with sorcery (is res_sorcery_config loaded?)\n");
		ast_sorcery_unref(system_sorcery);
		system_sorcery = NULL;
		return -1;
	}

	ast_sorcery_object_field_register(system_sorcery, "system", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(system_sorcery, "system", "timer_t1", __stringify(DEFAULT_TIMER_T1),
			OPT_UINT_T, 0, FLDSET(struct system_config, timert1));
	ast_sorcery_object_field_register(system_sorcery, "system", "timer_b", __stringify(DEFAULT_TIMER_B),
			OPT_UINT_T, 0, FLDSET(struct system_config, timerb));
	ast_sorcery_object_field_register(system_sorcery, "system", "compact_headers", "no",
			OPT_BOOL_T, 1, FLDSET(struct system_config, compactheaders));
	ast_sorcery_object_field_register(system_sorcery, "system", "threadpool_initial_size", "0",
			OPT_UINT_T, 0, FLDSET(struct system_config, threadpool.initial_size));
	ast_sorcery_object_field_register(system_sorcery, "system", "threadpool_auto_increment", "5",
			OPT_UINT_T, 0, FLDSET(struct system_config, threadpool.auto_increment));
	ast_sorcery_object_field_register(system_sorcery, "system", "threadpool_idle_timeout", "60",
			OPT_UINT_T, 0, FLDSET(struct system_config, threadpool.idle_timeout));
	ast_sorcery_object_field_register(system_sorcery, "system", "threadpool_max_size", "50",
			OPT_UINT_T, 0, FLDSET(struct system_config, threadpool.max_size));
	ast_sorcery_object_field_register(system_sorcery, "system", "disable_tcp_switch", "yes",
			OPT_BOOL_T, 1, FLDSET(struct system_config, disable_tcp_switch));
	ast_sorcery_object_field_register(system_sorcery, "system", "follow_early_media_fork", "yes",
			OPT_BOOL_T, 1, FLDSET(struct system_config, follow_early_media_fork));
	ast_sorcery_object_field_register(system_sorcery, "system", "accept_multiple_sdp_answers", "no",
			OPT_BOOL_T, 1, FLDSET(struct system_config, accept_multiple_sdp_answers));
	ast_sorcery_object_field_register(system_sorcery, "system", "disable_rport", "no",
			OPT_BOOL_T, 1, FLDSET(struct system_config, disable_rport));

	ast_sorcery_load(system_sorcery);

	system_configs = ast_sorcery_retrieve_by_fields(system_sorcery, "system",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	if (ao2_container_count(system_configs)) {
		return 0;
	}

	/* No config present, allocate one and apply defaults */
	system = ast_sorcery_alloc(system_sorcery, "system", NULL);
	if (!system) {
		ast_log(LOG_ERROR, "Unable to allocate default system config.\n");
		ast_sorcery_unref(system_sorcery);
		return -1;
	}

	if (system_apply(system_sorcery, system)) {
		ast_log(LOG_ERROR, "Failed to apply default system config.\n");
		ast_sorcery_unref(system_sorcery);
		return -1;
	}

	return 0;
}

void ast_sip_destroy_system(void)
{
	ast_sorcery_unref(system_sorcery);
}

static int system_create_resolver_and_set_nameservers(void *data)
{
	struct ao2_container *discovered_nameservers;
	struct ao2_iterator it_nameservers;
	char *nameserver;
	pj_status_t status;
	pj_dns_resolver *resolver;
	pj_str_t nameservers[PJ_DNS_RESOLVER_MAX_NS];
	unsigned int count = 0;

	discovered_nameservers = ast_dns_get_nameservers();
	if (!discovered_nameservers) {
		ast_log(LOG_ERROR, "Could not retrieve local system nameservers, resorting to system resolution\n");
		return 0;
	}

	if (!ao2_container_count(discovered_nameservers)) {
		ast_log(LOG_ERROR, "There are no local system nameservers configured, resorting to system resolution\n");
		ao2_ref(discovered_nameservers, -1);
		return -1;
	}

	if (!(resolver = pjsip_endpt_get_resolver(ast_sip_get_pjsip_endpoint()))) {
		status = pjsip_endpt_create_resolver(ast_sip_get_pjsip_endpoint(), &resolver);
		if (status != PJ_SUCCESS) {
			ast_log(LOG_ERROR, "Could not create DNS resolver(%d), resorting to system resolution\n", status);
			ao2_ref(discovered_nameservers, -1);
			return 0;
		}
	}

	it_nameservers = ao2_iterator_init(discovered_nameservers, 0);
	while ((nameserver = ao2_iterator_next(&it_nameservers))) {
		pj_strset2(&nameservers[count++], nameserver);
		ao2_ref(nameserver, -1);

		if (count == (PJ_DNS_RESOLVER_MAX_NS - 1)) {
			break;
		}
	}
	ao2_iterator_destroy(&it_nameservers);

	status = pj_dns_resolver_set_ns(resolver, count, nameservers, NULL);

	/* Since we no longer need the nameservers we can drop the list of them */
	ao2_ref(discovered_nameservers, -1);

	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Could not set nameservers on DNS resolver in PJSIP(%d), resorting to system resolution\n",
			status);
		return 0;
	}

	if (!pjsip_endpt_get_resolver(ast_sip_get_pjsip_endpoint())) {
		status = pjsip_endpt_set_resolver(ast_sip_get_pjsip_endpoint(), resolver);
		if (status != PJ_SUCCESS) {
			ast_log(LOG_ERROR, "Could not set DNS resolver in PJSIP(%d), resorting to system resolution\n", status);
			return 0;
		}
	}

	return 0;
}

void ast_sip_initialize_dns(void)
{
	ast_sip_push_task_wait_servant(NULL, system_create_resolver_and_set_nameservers, NULL);
}
