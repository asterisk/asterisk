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
#include "include/res_pjsip_private.h"
#include "asterisk/sorcery.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/ast_version.h"
#include "asterisk/res_pjsip_cli.h"

#define DEFAULT_MAX_FORWARDS 70
#define DEFAULT_KEEPALIVE_INTERVAL 0
#define DEFAULT_USERAGENT_PREFIX "Asterisk PBX"
#define DEFAULT_OUTBOUND_ENDPOINT "default_outbound_endpoint"
#define DEFAULT_DEBUG "no"
#define DEFAULT_ENDPOINT_IDENTIFIER_ORDER "ip,username,anonymous"
#define DEFAULT_MAX_INITIAL_QUALIFY_TIME 0
#define DEFAULT_FROM_USER "asterisk"
#define DEFAULT_REALM "asterisk"
#define DEFAULT_REGCONTEXT ""
#define DEFAULT_CONTACT_EXPIRATION_CHECK_INTERVAL 30
#define DEFAULT_DISABLE_MULTI_DOMAIN 0
#define DEFAULT_VOICEMAIL_EXTENSION ""
#define DEFAULT_UNIDENTIFIED_REQUEST_COUNT 5
#define DEFAULT_UNIDENTIFIED_REQUEST_PERIOD 5
#define DEFAULT_UNIDENTIFIED_REQUEST_PRUNE_INTERVAL 30
#define DEFAULT_MWI_TPS_QUEUE_HIGH AST_TASKPROCESSOR_HIGH_WATER_LEVEL
#define DEFAULT_MWI_TPS_QUEUE_LOW -1
#define DEFAULT_MWI_DISABLE_INITIAL_UNSOLICITED 0
#define DEFAULT_IGNORE_URI_USER_OPTIONS 0

/*!
 * \brief Cached global config object
 *
 * \details
 * Cached so we don't have to keep asking sorcery for the config.
 * We could ask for it hundreds of times a second if not more.
 */
static AO2_GLOBAL_OBJ_STATIC(global_cfg);

static char default_useragent[256];

struct global_config {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(useragent);
		AST_STRING_FIELD(regcontext);
		AST_STRING_FIELD(default_outbound_endpoint);
		/*! Debug logging yes|no|host */
		AST_STRING_FIELD(debug);
		/*! Order by which endpoint identifiers are checked (comma separated list) */
		AST_STRING_FIELD(endpoint_identifier_order);
		/*! User name to place in From header if there is no better option */
		AST_STRING_FIELD(default_from_user);
		/*! Default voicemail extension */
		AST_STRING_FIELD(default_voicemail_extension);
		/*! Realm to use in challenges before an endpoint is identified */
		AST_STRING_FIELD(default_realm);
	);
	/*! Value to put in Max-Forwards header */
	unsigned int max_forwards;
	/*! The interval at which to send keep alive messages to active connection-oriented transports */
	unsigned int keep_alive_interval;
	/*! The maximum time for all contacts to be qualified at startup */
	unsigned int max_initial_qualify_time;
	/*! The interval at which to check for expired contacts */
	unsigned int contact_expiration_check_interval;
	/*! Nonzero to disable multi domain support */
	unsigned int disable_multi_domain;
	/*! The maximum number of unidentified requests per source IP address before a security event is logged */
	unsigned int unidentified_request_count;
	/*! The period during which unidentified requests are accumulated */
	unsigned int unidentified_request_period;
	/*! Interval at which expired unidentifed requests will be pruned */
	unsigned int unidentified_request_prune_interval;
	struct {
		/*! Taskprocessor high water alert trigger level */
		unsigned int tps_queue_high;
		/*! Taskprocessor low water clear alert level. */
		int tps_queue_low;
		/*! Nonzero to disable sending unsolicited mwi to all endpoints on startup */
		unsigned int disable_initial_unsolicited;
	} mwi;
	/*! Nonzero if URI user field options are ignored. */
	unsigned int ignore_uri_user_options;
};

static void global_destructor(void *obj)
{
	struct global_config *cfg = obj;

	ast_string_field_free_memory(cfg);
}

static void *global_alloc(const char *name)
{
	struct global_config *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), global_destructor);
	if (!cfg || ast_string_field_init(cfg, 100)) {
		ao2_cleanup(cfg);
		return NULL;
	}

	return cfg;
}

static int global_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct global_config *cfg = obj;
	char max_forwards[10];

	if (ast_strlen_zero(cfg->debug)) {
		ast_log(LOG_ERROR,
			"Global option 'debug' can't be empty.  Set it to a valid value or remove the entry to accept 'no' as the default\n");
		return -1;
	}

	if (ast_strlen_zero(cfg->default_from_user)) {
		ast_log(LOG_ERROR,
			"Global option 'default_from_user' can't be empty.  Set it to a valid value or remove the entry to accept 'asterisk' as the default\n");
		return -1;
	}

	snprintf(max_forwards, sizeof(max_forwards), "%u", cfg->max_forwards);

	ast_sip_add_global_request_header("Max-Forwards", max_forwards, 1);
	ast_sip_add_global_request_header("User-Agent", cfg->useragent, 1);
	ast_sip_add_global_response_header("Server", cfg->useragent, 1);

	ao2_t_global_obj_replace_unref(global_cfg, cfg, "Applying global settings");
	return 0;
}

static struct global_config *get_global_cfg(void)
{
	return ao2_global_obj_ref(global_cfg);
}

char *ast_sip_global_default_outbound_endpoint(void)
{
	char *str;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return ast_strdup(DEFAULT_OUTBOUND_ENDPOINT);
	}

	str = ast_strdup(cfg->default_outbound_endpoint);
	ao2_ref(cfg, -1);
	return str;
}

char *ast_sip_get_debug(void)
{
	char *res;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return ast_strdup(DEFAULT_DEBUG);
	}

	res = ast_strdup(cfg->debug);
	ao2_ref(cfg, -1);
	return res;
}

char *ast_sip_get_regcontext(void)
{
	char *res;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return ast_strdup(DEFAULT_REGCONTEXT);
	}

	res = ast_strdup(cfg->regcontext);
	ao2_ref(cfg, -1);

	return res;
}

char *ast_sip_get_default_voicemail_extension(void)
{
	char *res;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return ast_strdup(DEFAULT_VOICEMAIL_EXTENSION);
	}

	res = ast_strdup(cfg->default_voicemail_extension);
	ao2_ref(cfg, -1);

	return res;
}

char *ast_sip_get_endpoint_identifier_order(void)
{
	char *res;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return ast_strdup(DEFAULT_ENDPOINT_IDENTIFIER_ORDER);
	}

	res = ast_strdup(cfg->endpoint_identifier_order);
	ao2_ref(cfg, -1);
	return res;
}

unsigned int ast_sip_get_keep_alive_interval(void)
{
	unsigned int interval;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return DEFAULT_KEEPALIVE_INTERVAL;
	}

	interval = cfg->keep_alive_interval;
	ao2_ref(cfg, -1);
	return interval;
}

unsigned int ast_sip_get_contact_expiration_check_interval(void)
{
	unsigned int interval;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return DEFAULT_CONTACT_EXPIRATION_CHECK_INTERVAL;
	}

	interval = cfg->contact_expiration_check_interval;
	ao2_ref(cfg, -1);
	return interval;
}

unsigned int ast_sip_get_disable_multi_domain(void)
{
	unsigned int disable_multi_domain;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return DEFAULT_DISABLE_MULTI_DOMAIN;
	}

	disable_multi_domain = cfg->disable_multi_domain;
	ao2_ref(cfg, -1);
	return disable_multi_domain;
}

unsigned int ast_sip_get_max_initial_qualify_time(void)
{
	unsigned int time;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return DEFAULT_MAX_INITIAL_QUALIFY_TIME;
	}

	time = cfg->max_initial_qualify_time;
	ao2_ref(cfg, -1);
	return time;
}

void ast_sip_get_unidentified_request_thresholds(unsigned int *count, unsigned int *period,
	unsigned int *prune_interval)
{
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		*count = DEFAULT_UNIDENTIFIED_REQUEST_COUNT;
		*period = DEFAULT_UNIDENTIFIED_REQUEST_PERIOD;
		*prune_interval = DEFAULT_UNIDENTIFIED_REQUEST_PRUNE_INTERVAL;
		return;
	}

	*count = cfg->unidentified_request_count;
	*period = cfg->unidentified_request_period;
	*prune_interval = cfg->unidentified_request_prune_interval;

	ao2_ref(cfg, -1);
	return;
}

void ast_sip_get_default_realm(char *realm, size_t size)
{
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		ast_copy_string(realm, DEFAULT_REALM, size);
	} else {
		ast_copy_string(realm, cfg->default_realm, size);
		ao2_ref(cfg, -1);
	}
}

void ast_sip_get_default_from_user(char *from_user, size_t size)
{
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		ast_copy_string(from_user, DEFAULT_FROM_USER, size);
	} else {
		ast_copy_string(from_user, cfg->default_from_user, size);
		ao2_ref(cfg, -1);
	}
}


unsigned int ast_sip_get_mwi_tps_queue_high(void)
{
	unsigned int tps_queue_high;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return DEFAULT_MWI_TPS_QUEUE_HIGH;
	}

	tps_queue_high = cfg->mwi.tps_queue_high;
	ao2_ref(cfg, -1);
	return tps_queue_high;
}

int ast_sip_get_mwi_tps_queue_low(void)
{
	int tps_queue_low;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return DEFAULT_MWI_TPS_QUEUE_LOW;
	}

	tps_queue_low = cfg->mwi.tps_queue_low;
	ao2_ref(cfg, -1);
	return tps_queue_low;
}

unsigned int ast_sip_get_mwi_disable_initial_unsolicited(void)
{
	unsigned int disable_initial_unsolicited;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return DEFAULT_MWI_DISABLE_INITIAL_UNSOLICITED;
	}

	disable_initial_unsolicited = cfg->mwi.disable_initial_unsolicited;
	ao2_ref(cfg, -1);
	return disable_initial_unsolicited;
}

unsigned int ast_sip_get_ignore_uri_user_options(void)
{
	unsigned int ignore_uri_user_options;
	struct global_config *cfg;

	cfg = get_global_cfg();
	if (!cfg) {
		return DEFAULT_IGNORE_URI_USER_OPTIONS;
	}

	ignore_uri_user_options = cfg->ignore_uri_user_options;
	ao2_ref(cfg, -1);
	return ignore_uri_user_options;
}

/*!
 * \internal
 * \brief Observer to set default global object if none exist.
 *
 * \param name Module name owning the sorcery instance.
 * \param sorcery Instance being observed.
 * \param object_type Name of object being observed.
 * \param reloaded Non-zero if the object is being reloaded.
 *
 * \return Nothing
 */
static void global_loaded_observer(const char *name, const struct ast_sorcery *sorcery, const char *object_type, int reloaded)
{
	struct ao2_container *globals;
	struct global_config *cfg;

	if (strcmp(object_type, "global")) {
		/* Not interested */
		return;
	}

	globals = ast_sorcery_retrieve_by_fields(sorcery, "global",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (globals) {
		int count;

		count = ao2_container_count(globals);
		ao2_ref(globals, -1);

		if (1 < count) {
			ast_log(LOG_ERROR,
				"At most one pjsip.conf type=global object can be defined.  You have %d defined.\n",
				count);
			return;
		}
		if (count) {
			return;
		}
	}

	ast_debug(1, "No pjsip.conf type=global object exists so applying defaults.\n");
	cfg = ast_sorcery_alloc(sorcery, "global", NULL);
	if (!cfg) {
		return;
	}
	global_apply(sorcery, cfg);
	ao2_ref(cfg, -1);
}

static const struct ast_sorcery_instance_observer observer_callbacks_global = {
	.object_type_loaded = global_loaded_observer,
};

int sip_cli_print_global(struct ast_sip_cli_context *context)
{
	struct global_config *cfg = get_global_cfg();

	if (!cfg) {
		cfg = ast_sorcery_alloc(ast_sip_get_sorcery(), "global", NULL);
		if (!cfg) {
			return -1;
		}
	}

	ast_str_append(&context->output_buffer, 0, "\nGlobal Settings:\n\n");
	ast_sip_cli_print_sorcery_objectset(cfg, context, 0);

	ao2_ref(cfg, -1);
	return 0;
}

int ast_sip_destroy_sorcery_global(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();

	ast_sorcery_instance_observer_remove(sorcery, &observer_callbacks_global);

	ao2_t_global_obj_release(global_cfg, "Module is unloading");

	return 0;
}

int ast_sip_initialize_sorcery_global(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();

	snprintf(default_useragent, sizeof(default_useragent), "%s %s",
		DEFAULT_USERAGENT_PREFIX, ast_get_version());

	ast_sorcery_apply_default(sorcery, "global", "config", "pjsip.conf,criteria=type=global");

	if (ast_sorcery_object_register(sorcery, "global", global_alloc, NULL, global_apply)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "global", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "global", "max_forwards",
		__stringify(DEFAULT_MAX_FORWARDS),
		OPT_UINT_T, 0, FLDSET(struct global_config, max_forwards));
	ast_sorcery_object_field_register(sorcery, "global", "user_agent", default_useragent,
		OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, useragent));
	ast_sorcery_object_field_register(sorcery, "global", "default_outbound_endpoint",
		DEFAULT_OUTBOUND_ENDPOINT,
		OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, default_outbound_endpoint));
	ast_sorcery_object_field_register(sorcery, "global", "debug", DEFAULT_DEBUG,
		OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, debug));
	ast_sorcery_object_field_register(sorcery, "global", "endpoint_identifier_order",
		DEFAULT_ENDPOINT_IDENTIFIER_ORDER,
		OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, endpoint_identifier_order));
	ast_sorcery_object_field_register(sorcery, "global", "keep_alive_interval",
		__stringify(DEFAULT_KEEPALIVE_INTERVAL),
		OPT_UINT_T, 0, FLDSET(struct global_config, keep_alive_interval));
	ast_sorcery_object_field_register(sorcery, "global", "max_initial_qualify_time",
		__stringify(DEFAULT_MAX_INITIAL_QUALIFY_TIME),
		OPT_UINT_T, 0, FLDSET(struct global_config, max_initial_qualify_time));
	ast_sorcery_object_field_register(sorcery, "global", "default_from_user", DEFAULT_FROM_USER,
		OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, default_from_user));
	ast_sorcery_object_field_register(sorcery, "global", "default_voicemail_extension",
		DEFAULT_VOICEMAIL_EXTENSION, OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config,
		default_voicemail_extension));
	ast_sorcery_object_field_register(sorcery, "global", "regcontext", DEFAULT_REGCONTEXT,
		OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, regcontext));
	ast_sorcery_object_field_register(sorcery, "global", "contact_expiration_check_interval",
		__stringify(DEFAULT_CONTACT_EXPIRATION_CHECK_INTERVAL),
		OPT_UINT_T, 0, FLDSET(struct global_config, contact_expiration_check_interval));
	ast_sorcery_object_field_register(sorcery, "global", "disable_multi_domain",
		DEFAULT_DISABLE_MULTI_DOMAIN ? "yes" : "no",
		OPT_BOOL_T, 1, FLDSET(struct global_config, disable_multi_domain));
	ast_sorcery_object_field_register(sorcery, "global", "unidentified_request_count",
		__stringify(DEFAULT_UNIDENTIFIED_REQUEST_COUNT),
		OPT_UINT_T, 0, FLDSET(struct global_config, unidentified_request_count));
	ast_sorcery_object_field_register(sorcery, "global", "unidentified_request_period",
		__stringify(DEFAULT_UNIDENTIFIED_REQUEST_PERIOD),
		OPT_UINT_T, 0, FLDSET(struct global_config, unidentified_request_period));
	ast_sorcery_object_field_register(sorcery, "global", "unidentified_request_prune_interval",
		__stringify(DEFAULT_UNIDENTIFIED_REQUEST_PRUNE_INTERVAL),
		OPT_UINT_T, 0, FLDSET(struct global_config, unidentified_request_prune_interval));
	ast_sorcery_object_field_register(sorcery, "global", "default_realm", DEFAULT_REALM,
		OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, default_realm));
	ast_sorcery_object_field_register(sorcery, "global", "mwi_tps_queue_high",
		__stringify(DEFAULT_MWI_TPS_QUEUE_HIGH),
		OPT_UINT_T, 0, FLDSET(struct global_config, mwi.tps_queue_high));
	ast_sorcery_object_field_register(sorcery, "global", "mwi_tps_queue_low",
		__stringify(DEFAULT_MWI_TPS_QUEUE_LOW),
		OPT_INT_T, 0, FLDSET(struct global_config, mwi.tps_queue_low));
	ast_sorcery_object_field_register(sorcery, "global", "mwi_disable_initial_unsolicited",
		DEFAULT_MWI_DISABLE_INITIAL_UNSOLICITED ? "yes" : "no",
		OPT_BOOL_T, 1, FLDSET(struct global_config, mwi.disable_initial_unsolicited));
	ast_sorcery_object_field_register(sorcery, "global", "ignore_uri_user_options",
		DEFAULT_IGNORE_URI_USER_OPTIONS ? "yes" : "no",
		OPT_BOOL_T, 1, FLDSET(struct global_config, ignore_uri_user_options));

	if (ast_sorcery_instance_observer_add(sorcery, &observer_callbacks_global)) {
		return -1;
	}

	return 0;
}
