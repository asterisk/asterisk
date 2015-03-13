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

#define DEFAULT_MAX_FORWARDS 70
#define DEFAULT_USERAGENT_PREFIX "Asterisk PBX"
#define DEFAULT_OUTBOUND_ENDPOINT "default_outbound_endpoint"

static char default_useragent[128];

struct global_config {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(useragent);
		AST_STRING_FIELD(default_outbound_endpoint);
		/*! Debug logging yes|no|host */
		AST_STRING_FIELD(debug);
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
	struct global_config *cfg = ast_sorcery_generic_alloc(sizeof(*cfg), global_destructor);

	if (!cfg || ast_string_field_init(cfg, 100)) {
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
	RAII_VAR(struct ao2_container *, globals, ast_sorcery_retrieve_by_fields(
			 ast_sip_get_sorcery(), "global", AST_RETRIEVE_FLAG_MULTIPLE,
			 NULL), ao2_cleanup);

	if (!globals) {
		return NULL;
	}

	return ao2_find(globals, NULL, 0);
}

char *ast_sip_global_default_outbound_endpoint(void)
{
	RAII_VAR(struct global_config *, cfg, get_global_cfg(), ao2_cleanup);

	if (!cfg) {
		return NULL;
	}

	return ast_strdup(cfg->default_outbound_endpoint);
}

char *ast_sip_get_debug(void)
{
	char *res;
	struct global_config *cfg = get_global_cfg();

	if (!cfg) {
		return ast_strdup("no");
	}

	res = ast_strdup(cfg->debug);
	ao2_ref(cfg, -1);

	return res;
}

unsigned int ast_sip_get_keep_alive_interval(void)
{
	unsigned int interval;
	struct global_config *cfg = get_global_cfg();

	if (!cfg) {
		return 0;
	}

	interval = cfg->keep_alive_interval;
	ao2_ref(cfg, -1);

	return interval;
}

int ast_sip_initialize_sorcery_global(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();

	snprintf(default_useragent, sizeof(default_useragent), "%s %s", DEFAULT_USERAGENT_PREFIX, ast_get_version());

	ast_sorcery_apply_default(sorcery, "global", "config", "pjsip.conf,criteria=type=global");

	if (ast_sorcery_object_register(sorcery, "global", global_alloc, NULL, global_apply)) {
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, "global", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, "global", "max_forwards", __stringify(DEFAULT_MAX_FORWARDS),
			OPT_UINT_T, 0, FLDSET(struct global_config, max_forwards));
	ast_sorcery_object_field_register(sorcery, "global", "user_agent", default_useragent,
			OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, useragent));
	ast_sorcery_object_field_register(sorcery, "global", "default_outbound_endpoint", DEFAULT_OUTBOUND_ENDPOINT,
			OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, default_outbound_endpoint));
	ast_sorcery_object_field_register(sorcery, "global", "debug", "no",
			OPT_STRINGFIELD_T, 0, STRFLDSET(struct global_config, debug));
	ast_sorcery_object_field_register(sorcery, "global", "keep_alive_interval", "",
			OPT_UINT_T, 0, FLDSET(struct global_config, keep_alive_interval));

	return 0;
}
