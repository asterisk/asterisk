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

#include "asterisk/cli.h"
#include "stir_shaken.h"

#define CONFIG_TYPE "verification"

#define DEFAULT_ca_file NULL
#define DEFAULT_ca_path NULL
#define DEFAULT_crl_file NULL
#define DEFAULT_crl_path NULL
static char DEFAULT_cert_cache_dir[PATH_MAX];

#define DEFAULT_global_disable 0
#define DEFAULT_load_system_certs 0
#define DEFAULT_curl_timeout 2
#define DEFAULT_max_iat_age 15
#define DEFAULT_max_date_header_age 15
#define DEFAULT_max_cache_entry_age 3600
#define DEFAULT_max_cache_size 1000
#define DEFAULT_failure_action SS_VS_FAILURE_CONTINUE

#define STIR_SHAKEN_DIR_NAME "stir_shaken"

struct ss_vs_cfg *ss_get_vs_cfg(void)
{
	return ast_sorcery_retrieve_by_id(ss_sorcery(),
		CONFIG_TYPE, CONFIG_TYPE);
}

int ss_vs_is_config_loaded(void)
{
	struct ss_vs_cfg *cfg;
	int res = 0;

	cfg = ss_get_vs_cfg();
	if (cfg) {
		res = 1;
		ao2_ref(cfg, -1);
	} else {
		ast_log(LOG_ERROR, "No 'verification' section could be found in stir/shaken configuration");
	}

	return res;
}

config_enum_handler(ss_vs_cfg, use_rfc9410_responses, AST_STIR_SHAKEN_VS_RFC9410_UNKNOWN)
config_enum_to_str(ss_vs_cfg, use_rfc9410_responses)

config_enum_handler(ss_vs_cfg, failure_action, AST_STIR_SHAKEN_VS_FAILURE_UNKNOWN)
config_enum_to_str(ss_vs_cfg, failure_action)

static void verification_destructor(void *obj)
{
	struct ss_vs_cfg *cfg = obj;
	ast_string_field_free_memory(cfg);
}

static void *verification_alloc(const char *name)
{
	struct ss_vs_cfg *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), verification_destructor);
	if (!cfg) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 1024)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

static int verification_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ss_vs_cfg *cfg = obj;
	FILE *fp;
	char *testfile;

	if (ast_strlen_zero(cfg->ca_file) && ast_strlen_zero(cfg->ca_path)) {
		ast_log(LOG_ERROR, "stir/shaken - ca_file and ca_path cannot both be empty\n");
		return -1;
	}

	if (!ast_strlen_zero(cfg->ca_file) && !ast_file_is_readable(cfg->ca_file)) {
		ast_log(LOG_ERROR, "stir/shaken - ca_file '%s' not found, or is unreadable\n",
			cfg->ca_file);
		return -1;
	}

	if (!ast_strlen_zero(cfg->ca_path) && !ast_file_is_readable(cfg->ca_path)) {
		ast_log(LOG_ERROR, "stir/shaken - ca_path '%s' not found, or is unreadable\n",
			cfg->ca_path);
		return -1;
	}

	if (!ast_strlen_zero(cfg->crl_file) && !ast_file_is_readable(cfg->crl_file)) {
		ast_log(LOG_ERROR, "stir/shaken - crl_file '%s' not found, or is unreadable\n",
			cfg->ca_file);
		return -1;
	}

	if (!ast_strlen_zero(cfg->crl_path) && !ast_file_is_readable(cfg->crl_path)) {
		ast_log(LOG_ERROR, "stir/shaken - crl_path '%s' not found, or is unreadable\n",
			cfg->ca_path);
		return -1;
	}

	if (ast_strlen_zero(cfg->cert_cache_dir)) {
		ast_log(LOG_ERROR, "stir/shaken - cert_cache_dir was not specified\n");
		return -1;
	}
	if (ast_asprintf(&testfile, "%s/testfile", cfg->cert_cache_dir) <= 0) {
		ast_log(LOG_ERROR, "Unable to allocate memory for testfile\n");
		return -1;
	}

	fp = fopen(testfile, "w+");
	if (!fp) {
		ast_free(testfile);
		ast_log(LOG_ERROR, "stir/shaken - cert_cache_dir '%s' was not writable\n",
			cfg->cert_cache_dir);
		return -1;
	}
	fclose(fp);
	remove(testfile);
	ast_free(testfile);

	return 0;
}

static char *cli_verification_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ss_vs_cfg *cfg;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken verification show config";
		e->usage =
			"Usage: stir_shaken verification show config\n"
			"       Show the stir/shaken verification settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	cfg = ss_get_vs_cfg();
	stir_shaken_cli_show(cfg, a, 0);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}

static struct ast_cli_entry verification_cli[] = {
	AST_CLI_DEFINE(cli_verification_show, "Show stir/shaken verification configuration"),
};

int ss_vs_config_reload(void)
{
	struct ast_sorcery *sorcery = ss_sorcery();
	ast_sorcery_reload_object(sorcery, CONFIG_TYPE);
	return 0;
}

int ss_vs_config_unload(void)
{
	ast_cli_unregister_multiple(verification_cli,
		ARRAY_LEN(verification_cli));

	return 0;
}

int ss_vs_config_load(void)
{
	struct ast_sorcery *sorcery = ss_sorcery();

	snprintf(DEFAULT_cert_cache_dir, sizeof(DEFAULT_cert_cache_dir), "%s/keys/%s/cache",
		ast_config_AST_DATA_DIR, STIR_SHAKEN_DIR_NAME);

	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config",
		"stir_shaken.conf,criteria=type=" CONFIG_TYPE ",single_object=yes,explicit_name=" CONFIG_TYPE);

	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, verification_alloc,
			NULL, verification_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return -1;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, CONFIG_TYPE, "type", "",
		OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "ca_file",
		DEFAULT_ca_file, OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, ca_file));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "ca_path",
		DEFAULT_ca_path, OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, ca_path));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "crl_file",
		DEFAULT_crl_file, OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, crl_file));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "crl_path",
		DEFAULT_crl_path, OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, crl_path));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "cert_cache_dir",
		DEFAULT_cert_cache_dir, OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, cert_cache_dir));

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "global_disable",
		DEFAULT_global_disable ? "yes" : "no",
		OPT_YESNO_T, 1, FLDSET(struct ss_vs_cfg, global_disable));

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "load_system_certs",
		DEFAULT_load_system_certs ? "yes" : "no",
		OPT_YESNO_T, 1, FLDSET(struct ss_vs_cfg, load_system_certs));

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "curl_timeout",
		__stringify(DEFAULT_curl_timeout), OPT_UINT_T, 0,
		FLDSET(struct ss_vs_cfg, curl_timeout));

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "max_iat_age",
		__stringify(DEFAULT_max_iat_age), OPT_UINT_T, 0,
		FLDSET(struct ss_vs_cfg, max_iat_age));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "max_date_header_age",
		__stringify(DEFAULT_max_date_header_age), OPT_UINT_T, 0,
		FLDSET(struct ss_vs_cfg, max_date_header_age));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "max_cache_entry_age",
		__stringify(DEFAULT_max_cache_entry_age), OPT_UINT_T, 0,
		FLDSET(struct ss_vs_cfg, max_cache_entry_age));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "max_cache_size",
		__stringify(DEFAULT_max_cache_size), OPT_UINT_T, 0,
		FLDSET(struct ss_vs_cfg, max_cache_size));

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "failure_action",
		ast_stir_shaken_failure_action_to_str(AST_STIR_SHAKEN_VS_FAILURE_CONTINUE), failure_action_handler,
		failure_action_to_str, NULL, 0, 0);

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "use_rfc9410_responses",
		ast_stir_shaken_use_rfc9410_responses_to_str(AST_STIR_SHAKEN_VS_RFC9410_NO), use_rfc9410_responses_handler,
		use_rfc9410_responses_to_str, NULL, 0, 0);

	ast_sorcery_load_object(sorcery, CONFIG_TYPE);

	ast_cli_register_multiple(verification_cli,
		ARRAY_LEN(verification_cli));

	return 0;
}
