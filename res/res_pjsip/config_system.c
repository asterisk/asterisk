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

#include <stdarg.h>
#include <stdio.h>
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

static int system_apply(const struct ast_sorcery *system_sorcery, void *obj)
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

	if (system->compactheaders) {
		extern pj_bool_t pjsip_use_compact_form;

		pjsip_use_compact_form = PJ_TRUE;
	}

	sip_threadpool_options.initial_size = system->threadpool.initial_size;
	sip_threadpool_options.auto_increment = system->threadpool.auto_increment;
	sip_threadpool_options.idle_timeout = system->threadpool.idle_timeout;
	sip_threadpool_options.max_size = system->threadpool.max_size;

	pjsip_cfg()->endpt.disable_tcp_switch =
		system->disable_tcp_switch ? PJ_TRUE : PJ_FALSE;

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

static pj_log_func *log_cb_orig;
static unsigned decor_orig;

struct pjsip_show_buildopts {
	pthread_t thread;
	int fd;
	const char *option;
	const char *format_string;
	va_list *arg_ptr;
	int sscanf_result;
};

static struct pjsip_show_buildopts show_buildopts = {
	.thread = AST_PTHREADT_NULL,
	.fd = -1,
};

static pjproject_logger_cb *logger;

void ast_sip_set_pjproject_logger(pjproject_logger_cb *logger_cb)
{
	logger =logger_cb;
}

static AST_VECTOR(buildopts, char *) buildopts;

static void log_cb(int level, const char *data, int len)
{
	if (show_buildopts.fd == -1) {
		if (strstr(data, "Teluu") || strstr(data, "Dumping")) {
			return;
		}

		AST_VECTOR_ADD_SORTED(&buildopts, ast_strdup(ast_skip_blanks(data)), strcmp);

		return;
	}

	if (logger) {
		logger(level, data, len);
	}
}

/**
 * The pragmas are needed to prevent the compiler from emitting a warning
 * that suggests the use of 'attribute format' because of vsscanf.
 *
 * Clang does respect pragma GCC
 */
#pragma GCC diagnostic ignored "-Wmissing-format-attribute"
int ast_sip_get_pjproject_buildopt(char *option, char *format_string, ...)
{
	va_list arg_ptr;
	int res = 0;
	char *format_temp;
	int i;

	va_start(arg_ptr, format_string);

	format_temp = ast_alloca(strlen(option) + strlen(" : ") + strlen(format_string) + 1);

	sprintf(format_temp, "%s : %s", option, format_string);


	for(i = 0; i < AST_VECTOR_SIZE(&buildopts); i++) {
		res = vsscanf(AST_VECTOR_GET(&buildopts, i), format_temp, arg_ptr);
		if (res) {
			break;
		}
	}
	va_end(arg_ptr);

	return res;
}
#pragma GCC diagnostic warning "-Wmissing-format-attribute"

static char *handle_pjsip_show_buildopts(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	int i;

	switch(cmd) {
	case CLI_INIT:
		e->command = "pjsip show buildopts";
		e->usage =
			"Usage: pjsip show buildopts\n"
			"       Show the compile time config of pjproject that res_pjsip is\n"
			"       running against.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, "PJPROJECT compile time config currently running against:\n");

	for(i = 0; i < AST_VECTOR_SIZE(&buildopts); i++) {
		ast_cli(a->fd, "%s\n", AST_VECTOR_GET(&buildopts, i));
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry pjsip_cli[] = {
	AST_CLI_DEFINE(handle_pjsip_show_buildopts, "Show the compiled config of pjproject in use"),
};

int ast_sip_initialize_system(void)
{
	RAII_VAR(struct ao2_container *, system_configs, NULL, ao2_cleanup);
	RAII_VAR(struct system_config *, system, NULL, ao2_cleanup);

	decor_orig = pj_log_get_decor();
	log_cb_orig = pj_log_get_log_func();
	pj_log_set_log_func(log_cb);

	AST_VECTOR_INIT(&buildopts,64);

	show_buildopts.fd = -1;
	pj_log_set_decor(0);
	pj_dump_config();
	pj_log_set_decor(PJ_LOG_HAS_SENDER | PJ_LOG_HAS_INDENT);
	show_buildopts.fd = 0;

	system_sorcery = ast_sorcery_open();
	if (!system_sorcery) {
		ast_log(LOG_ERROR, "Failed to open SIP system sorcery\n");
		return -1;
	}

	ast_sorcery_apply_default(system_sorcery, "system", "config", "pjsip.conf,criteria=type=system");

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

	ast_cli_register_multiple(pjsip_cli, ARRAY_LEN(pjsip_cli));

	return 0;
}

#define NOT_EQUALS(a, b) (a != b)

void ast_sip_destroy_system(void)
{
	ast_sorcery_unref(system_sorcery);

	ast_cli_unregister_multiple(pjsip_cli, ARRAY_LEN(pjsip_cli));
	pj_log_set_log_func(log_cb_orig);
	pj_log_set_decor(decor_orig);

	AST_VECTOR_REMOVE_CMP_UNORDERED(&buildopts, NULL, NOT_EQUALS, ast_free);
	AST_VECTOR_FREE(&buildopts);
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
	ast_sip_push_task_synchronous(NULL, system_create_resolver_and_set_nameservers, NULL);
}
