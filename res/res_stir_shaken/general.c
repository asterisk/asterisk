/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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

#include "asterisk/cli.h"
#include "asterisk/sorcery.h"

#include "stir_shaken.h"
#include "general.h"
#include "asterisk/res_stir_shaken.h"

#define CONFIG_TYPE "general"

#define DEFAULT_CA_FILE ""
#define DEFAULT_CA_PATH ""
#define DEFAULT_CACHE_MAX_SIZE 1000
#define DEFAULT_CURL_TIMEOUT 2
#define DEFAULT_SIGNATURE_TIMEOUT 15

struct stir_shaken_general {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		/*! File path to a certificate authority */
		AST_STRING_FIELD(ca_file);
		/*! File path to a chain of trust */
		AST_STRING_FIELD(ca_path);
	);
	/*! Maximum size of public keys cache */
	unsigned int cache_max_size;
	/*! Maximum time to wait to CURL certificates */
	unsigned int curl_timeout;
	/*! Amount of time a signature is valid for */
	unsigned int signature_timeout;
};

static struct stir_shaken_general *default_config = NULL;

struct stir_shaken_general *stir_shaken_general_get()
{
	struct stir_shaken_general *cfg;
	struct ao2_container *container;

	container = ast_sorcery_retrieve_by_fields(ast_stir_shaken_sorcery(), CONFIG_TYPE,
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!container || ao2_container_count(container) == 0) {
		ao2_cleanup(container);
		return ao2_bump(default_config);
	}

	cfg = ao2_find(container, NULL, 0);
	ao2_ref(container, -1);

	return cfg;
}

const char *ast_stir_shaken_ca_file(const struct stir_shaken_general *cfg)
{
	return cfg ? cfg->ca_file : DEFAULT_CA_FILE;
}

const char *ast_stir_shaken_ca_path(const struct stir_shaken_general *cfg)
{
	return cfg ? cfg->ca_path : DEFAULT_CA_PATH;
}

unsigned int ast_stir_shaken_cache_max_size(const struct stir_shaken_general *cfg)
{
	return cfg ? cfg->cache_max_size : DEFAULT_CACHE_MAX_SIZE;
}

unsigned int ast_stir_shaken_curl_timeout(const struct stir_shaken_general *cfg)
{
	return cfg ? cfg->curl_timeout : DEFAULT_CURL_TIMEOUT;
}

unsigned int ast_stir_shaken_signature_timeout(const struct stir_shaken_general *cfg)
{
	return cfg ? cfg->signature_timeout : DEFAULT_SIGNATURE_TIMEOUT;
}

static void stir_shaken_general_destructor(void *obj)
{
	struct stir_shaken_general *cfg = obj;

	ast_string_field_free_memory(cfg);
}

static void *stir_shaken_general_alloc(const char *name)
{
	struct stir_shaken_general *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), stir_shaken_general_destructor);
	if (!cfg) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 512)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

static int stir_shaken_general_apply(const struct ast_sorcery *sorcery, void *obj)
{
	return 0;
}

static void stir_shaken_general_loaded(const char *name, const struct ast_sorcery *sorcery,
	const char *object_type, int reloaded)
{
	struct stir_shaken_general *cfg;

	if (strcmp(object_type, CONFIG_TYPE)) {
		/* Not interested */
		return;
	}

	if (default_config) {
		ao2_ref(default_config, -1);
		default_config = NULL;
	}

	cfg = stir_shaken_general_get();
	if (cfg) {
		ao2_ref(cfg, -1);
		return;
	}

	/* Use the default configuration if on is not specified */
	default_config = ast_sorcery_alloc(sorcery, CONFIG_TYPE, NULL);
	if (default_config) {
		stir_shaken_general_apply(sorcery, default_config);
	}
}

static const struct ast_sorcery_instance_observer stir_shaken_general_observer = {
	.object_type_loaded = stir_shaken_general_loaded,
};

static char *stir_shaken_general_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct stir_shaken_general *cfg;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show general";
		e->usage =
			"Usage: stir_shaken show general\n"
			"       Show the general stir/shaken settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	cfg = stir_shaken_general_get();
	stir_shaken_cli_show(cfg, a, 0);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}

static struct ast_cli_entry stir_shaken_general_cli[] = {
	AST_CLI_DEFINE(stir_shaken_general_show, "Show stir/shaken general configuration"),
};

static int on_load_ca_file(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct stir_shaken_general *cfg = obj;

	if (!ast_file_is_readable(var->value)) {
		ast_log(LOG_ERROR, "stir/shaken - %s '%s' not found, or is unreadable\n",
				var->name, var->value);
		return -1;
	}

	return ast_string_field_set(cfg, ca_file, var->value);
}

static int ca_file_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct stir_shaken_general *cfg = obj;

	*buf = ast_strdup(cfg->ca_file);

	return 0;
}

static int on_load_ca_path(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct stir_shaken_general *cfg = obj;

	if (!ast_file_is_readable(var->value)) {
		ast_log(LOG_ERROR, "stir/shaken - %s '%s' not found, or is unreadable\n",
				var->name, var->value);
		return -1;
	}

	return ast_string_field_set(cfg, ca_path, var->value);
}

static int ca_path_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct stir_shaken_general *cfg = obj;

	*buf = ast_strdup(cfg->ca_path);

	return 0;
}

int stir_shaken_general_unload(void)
{
	ast_cli_unregister_multiple(stir_shaken_general_cli,
		ARRAY_LEN(stir_shaken_general_cli));

	ast_sorcery_instance_observer_remove(ast_stir_shaken_sorcery(),
		&stir_shaken_general_observer);

	if (default_config) {
		ao2_ref(default_config, -1);
		default_config = NULL;
	}

	return 0;
}

int stir_shaken_general_load(void)
{
	struct ast_sorcery *sorcery = ast_stir_shaken_sorcery();

	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config",
		"stir_shaken.conf,criteria=type=general,single_object=yes,explicit_name=general");

	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, stir_shaken_general_alloc,
			NULL, stir_shaken_general_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "ca_file",
		DEFAULT_CA_FILE, on_load_ca_file, ca_file_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "ca_path",
		DEFAULT_CA_PATH, on_load_ca_path, ca_path_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "cache_max_size",
		__stringify(DEFAULT_CACHE_MAX_SIZE), OPT_UINT_T, 0,
		FLDSET(struct stir_shaken_general, cache_max_size));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "curl_timeout",
		__stringify(DEFAULT_CURL_TIMEOUT), OPT_UINT_T, 0,
		FLDSET(struct stir_shaken_general, curl_timeout));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "signature_timeout",
		__stringify(DEFAULT_SIGNATURE_TIMEOUT), OPT_UINT_T, 0,
		FLDSET(struct stir_shaken_general, signature_timeout));

	if (ast_sorcery_instance_observer_add(sorcery, &stir_shaken_general_observer)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register loaded observer for '%s' "
				"sorcery object type\n", CONFIG_TYPE);
		return -1;
	}

	ast_cli_register_multiple(stir_shaken_general_cli,
		ARRAY_LEN(stir_shaken_general_cli));

	return 0;
}
