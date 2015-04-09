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
#include "asterisk/ast_version.h"
#include "asterisk/res_pjsip_cli.h"

#define DEFAULT_MAX_FORWARDS 70
#define DEFAULT_KEEPALIVE_INTERVAL 0
#define DEFAULT_USERAGENT_PREFIX "Asterisk PBX"
#define DEFAULT_OUTBOUND_ENDPOINT "default_outbound_endpoint"
#define DEFAULT_DEBUG "no"
#define DEFAULT_ENDPOINT_IDENTIFIER_ORDER "ip,username,anonymous"

static char default_useragent[256];

struct global_config {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(useragent);
		AST_STRING_FIELD(default_outbound_endpoint);
		/*! Debug logging yes|no|host */
		AST_STRING_FIELD(debug);
		/*! Order by which endpoint identifiers are checked (comma separated list) */
		AST_STRING_FIELD(endpoint_identifier_order);
	);
	/* Value to put in Max-Forwards header */
	unsigned int max_forwards;
	/* The interval at which to send keep alive messages to active connection-oriented transports */
	unsigned int keep_alive_interval;
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

	snprintf(max_forwards, sizeof(max_forwards), "%u", cfg->max_forwards);

	ast_sip_add_global_request_header("Max-Forwards", max_forwards, 1);
	ast_sip_add_global_request_header("User-Agent", cfg->useragent, 1);
	ast_sip_add_global_response_header("Server", cfg->useragent, 1);
	return 0;
}

static struct global_config *get_global_cfg(void)
{
	struct global_config *cfg;
	struct ao2_container *globals;

	globals = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "global",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!globals) {
		return NULL;
	}

	cfg = ao2_find(globals, NULL, 0);
	ao2_ref(globals, -1);
	return cfg;
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

	if (ast_sorcery_instance_observer_add(sorcery, &observer_callbacks_global)) {
		return -1;
	}

	return 0;
}
