/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@digium.com>
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

#include <sys/stat.h>

#include "asterisk/cli.h"
#include "asterisk/module.h"
#include "asterisk/sorcery.h"

#include "stir_shaken.h"

#define CONFIG_TYPE "tn"

#define DEFAULT_check_tn_cert_public_url check_tn_cert_public_url_NOT_SET
#define DEFAULT_private_key_file NULL
#define DEFAULT_public_cert_url NULL
#define DEFAULT_attest_level attest_level_NOT_SET
#define DEFAULT_send_mky send_mky_NOT_SET

struct tn_cfg *tn_get_cfg(const char *id)
{
	return ast_sorcery_retrieve_by_id(get_sorcery(), CONFIG_TYPE, id);
}

static struct ao2_container *get_tn_all(void)
{
	return ast_sorcery_retrieve_by_fields(get_sorcery(), CONFIG_TYPE,
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

generate_acfg_common_sorcery_handlers(tn_cfg);

static void tn_destructor(void *obj)
{
	struct tn_cfg *cfg = obj;

	ast_string_field_free_memory(cfg);
	acfg_cleanup(&cfg->acfg_common);
}

static int init_tn(struct tn_cfg *cfg)
{
	if (ast_string_field_init(cfg, 1024)) {
		return -1;
	}

	/*
	 * The memory for the commons actually comes from cfg
	 * due to the weirdness of the STRFLDSET macro used with
	 * sorcery.  We just use a token amount of memory in
	 * this call so the initialize doesn't fail.
	 */
	if (ast_string_field_init(&cfg->acfg_common, 8)) {
		return -1;
	}

	return 0;
}

static void *tn_alloc(const char *name)
{
	struct tn_cfg *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), tn_destructor);
	if (!cfg) {
		return NULL;
	}

	if (init_tn(cfg) != 0) {
		ao2_cleanup(cfg);
		cfg = NULL;
	}
	return cfg;
}

static void *etn_alloc(const char *name)
{
	struct tn_cfg *cfg;

	cfg = ao2_alloc_options(sizeof(*cfg), tn_destructor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!cfg) {
		return NULL;
	}

	if (init_tn(cfg) != 0) {
		ao2_cleanup(cfg);
		cfg = NULL;
	}
	return cfg;
}

struct tn_cfg *tn_get_etn(const char *id, struct profile_cfg *eprofile)
{
	RAII_VAR(struct tn_cfg *, tn,
		ast_sorcery_retrieve_by_id(get_sorcery(), CONFIG_TYPE, S_OR(id, "")),
		ao2_cleanup);
	struct tn_cfg *etn = etn_alloc(id);
	int rc = 0;

	if (!tn || !eprofile || !etn) {
		ao2_cleanup(etn);
		return NULL;
	}

	/* Initialize with the acfg from the eprofile first */
	rc = as_copy_cfg_common(id, &etn->acfg_common,
		&eprofile->acfg_common);
	if (rc != 0) {
		ao2_cleanup(etn);
		return NULL;
	}

	/* Overwrite with anything in the TN itself */
	rc = as_copy_cfg_common(id, &etn->acfg_common,
		&tn->acfg_common);
	if (rc != 0) {
		ao2_cleanup(etn);
		return NULL;
	}

	/*
	 * Unlike profile, we're not going to actually add a
	 * new object to sorcery because, although unlikely,
	 * the same TN could be used with multiple profiles.
	 */

	return etn;
}

static int tn_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct tn_cfg *cfg = obj;
	const char *id = ast_sorcery_object_get_id(cfg);
	int rc = 0;

	if (as_check_common_config(id, &cfg->acfg_common) != 0) {
		return -1;
	}

	return rc;
}

static char *cli_tn_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;
	struct config_object_cli_data data = {
		.title = "TN",
		.object_type = config_object_type_tn,
	};

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show tns";
		e->usage =
			"Usage: stir_shaken show tns\n"
			"       Show all attestation TNs\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	container = get_tn_all();
	if (!container || ao2_container_count(container) == 0) {
		ast_cli(a->fd, "No stir/shaken TNs found\n");
		ao2_cleanup(container);
		return CLI_SUCCESS;
	}

	ao2_callback_data(container, OBJ_NODATA, config_object_cli_show, a,&data);
	ao2_ref(container, -1);

	return CLI_SUCCESS;
}

static char *cli_tn_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct tn_cfg *cfg;
	struct config_object_cli_data data = {
		.title = "TN",
		.object_type = config_object_type_tn,
	};

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show tn";
		e->usage =
			"Usage: stir_shaken show tn <id>\n"
			"       Show the settings for a given TN\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return config_object_tab_complete_name(a->word, get_tn_all());
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	cfg = tn_get_cfg(a->argv[3]);
	config_object_cli_show(cfg, a, &data, 0);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}


static struct ast_cli_entry stir_shaken_certificate_cli[] = {
	AST_CLI_DEFINE(cli_tn_show, "Show stir/shaken TN configuration by id"),
	AST_CLI_DEFINE(cli_tn_show_all, "Show all stir/shaken attestation TN configurations"),
};

int tn_config_reload(void)
{
	struct ast_sorcery *sorcery = get_sorcery();
	ast_sorcery_force_reload_object(sorcery, CONFIG_TYPE);
	return AST_MODULE_LOAD_SUCCESS;
}

int tn_config_unload(void)
{
	ast_cli_unregister_multiple(stir_shaken_certificate_cli,
		ARRAY_LEN(stir_shaken_certificate_cli));

	return 0;
}

int tn_config_load(void)
{
	struct ast_sorcery *sorcery = get_sorcery();

	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config", "stir_shaken.conf,criteria=type=tn");

	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, tn_alloc,
			NULL, tn_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "type", "",
		OPT_NOOP_T, 0, 0);

	register_common_attestation_fields(sorcery, tn_cfg, CONFIG_TYPE,);

	ast_sorcery_load_object(sorcery, CONFIG_TYPE);

	ast_cli_register_multiple(stir_shaken_certificate_cli,
		ARRAY_LEN(stir_shaken_certificate_cli));

	return AST_MODULE_LOAD_SUCCESS;
}
