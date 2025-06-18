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

#define _TRACE_PREFIX_ "ac",__LINE__, ""

#include "asterisk.h"

#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/sorcery.h"
#include "asterisk/paths.h"

#include "stir_shaken.h"

#define CONFIG_TYPE "attestation"

#define DEFAULT_global_disable 0

#define DEFAULT_check_tn_cert_public_url check_tn_cert_public_url_NO
#define DEFAULT_private_key_file NULL
#define DEFAULT_public_cert_url NULL
#define DEFAULT_attest_level attest_level_NOT_SET
#define DEFAULT_unknown_tn_attest_level attest_level_NOT_SET
#define DEFAULT_send_mky send_mky_NO

static struct attestation_cfg *empty_cfg = NULL;

struct attestation_cfg *as_get_cfg(void)
{
	struct attestation_cfg *cfg = ast_sorcery_retrieve_by_id(get_sorcery(),
		CONFIG_TYPE, CONFIG_TYPE);
	if (cfg) {
		return cfg;
	}

	return empty_cfg ? ao2_bump(empty_cfg) : NULL;
}

int as_is_config_loaded(void)
{
	struct attestation_cfg *cfg = ast_sorcery_retrieve_by_id(get_sorcery(),
		CONFIG_TYPE, CONFIG_TYPE);
	ao2_cleanup(cfg);

	return !!cfg;
}

generate_acfg_common_sorcery_handlers(attestation_cfg);

generate_sorcery_enum_from_str_ex(attestation_cfg,,unknown_tn_attest_level, attest_level, UNKNOWN);
generate_sorcery_enum_to_str_ex(attestation_cfg,,unknown_tn_attest_level, attest_level);

void acfg_cleanup(struct attestation_cfg_common *acfg_common)
{
	if (!acfg_common) {
		return;
	}
	ast_string_field_free_memory(acfg_common);
	ao2_cleanup(acfg_common->raw_key);
}

static void attestation_destructor(void *obj)
{
	struct attestation_cfg *cfg = obj;

	ast_string_field_free_memory(cfg);
	acfg_cleanup(&cfg->acfg_common);
}

static void *attestation_alloc(const char *name)
{
	struct attestation_cfg *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), attestation_destructor);
	if (!cfg) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 1024)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	/*
	 * The memory for acfg_common actually comes from cfg
	 * due to the weirdness of the STRFLDSET macro used with
	 * sorcery.  We just use a token amount of memory in
	 * this call so the initialize doesn't fail.
	 */
	if (ast_string_field_init(&cfg->acfg_common, 8)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

int as_copy_cfg_common(const char *id, struct attestation_cfg_common *cfg_dst,
	struct attestation_cfg_common *cfg_src)
{
	int rc = 0;

	if (!cfg_dst || !cfg_src) {
		return -1;
	}

	cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, private_key_file);
	cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, public_cert_url);

	cfg_enum_copy(cfg_dst, cfg_src, attest_level);
	cfg_enum_copy(cfg_dst, cfg_src, check_tn_cert_public_url);
	cfg_enum_copy(cfg_dst, cfg_src, send_mky);

	if (cfg_src->raw_key) {
		/* Free and overwrite the destination */
		ao2_cleanup(cfg_dst->raw_key);
		cfg_dst->raw_key = ao2_bump(cfg_src->raw_key);
		cfg_dst->raw_key_length = cfg_src->raw_key_length;
	}

	return rc;
}

int as_check_common_config(const char *id, struct attestation_cfg_common *acfg_common)
{
	SCOPE_ENTER(3, "%s: Checking common config\n", id);

	if (!ast_strlen_zero(acfg_common->private_key_file)
		&& !ast_file_is_readable(acfg_common->private_key_file)) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: default_private_key_path %s is missing or not readable\n", id,
			acfg_common->private_key_file);
	}

	if (ENUM_BOOL(acfg_common->check_tn_cert_public_url,
		check_tn_cert_public_url)
		&& !ast_strlen_zero(acfg_common->public_cert_url)) {
		RAII_VAR(char *, public_cert_data, NULL, ast_std_free);
		X509 *public_cert;
		size_t public_cert_len;
		int rc = 0;
		long http_code;
		SCOPE_ENTER(3 , "%s: Checking public cert url '%s'\n",
			id, acfg_common->public_cert_url);

		http_code = curl_download_to_memory(acfg_common->public_cert_url,
			&public_cert_len, &public_cert_data, NULL);
		if (http_code / 100 != 2) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: public_cert '%s' could not be downloaded\n", id,
				acfg_common->public_cert_url);
		}

		public_cert = crypto_load_cert_chain_from_memory(public_cert_data,
			public_cert_len, NULL);
		if (!public_cert) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: public_cert '%s' could not be parsed as a certificate\n", id,
				acfg_common->public_cert_url);
		}
		rc = crypto_is_cert_time_valid(public_cert, 0);
		X509_free(public_cert);
		if (!rc) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: public_cert '%s' is not valid yet or has expired\n", id,
				acfg_common->public_cert_url);
		}

		rc = crypto_has_private_key_from_memory(public_cert_data, public_cert_len);
		if (rc) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: DANGER!!! public_cert_url '%s' has a private key in the file!!!\n", id,
				acfg_common->public_cert_url);
		}
		SCOPE_EXIT("%s: Done\n", id);
	}

	if (!ast_strlen_zero(acfg_common->private_key_file)) {
		EVP_PKEY *private_key;
		RAII_VAR(unsigned char *, raw_key, NULL, ast_free);

		private_key = crypto_load_privkey_from_file(acfg_common->private_key_file);
		if (!private_key) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Could not extract raw private key from file '%s'\n", id,
				acfg_common->private_key_file);
		}

		acfg_common->raw_key_length = crypto_extract_raw_privkey(private_key, &raw_key);
		EVP_PKEY_free(private_key);
		if (acfg_common->raw_key_length == 0 || raw_key == NULL) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Could not extract raw private key from file '%s'\n", id,
				acfg_common->private_key_file);
		}

		/*
		 * We're making this an ao2 object so it can be referenced
		 * by a profile instead of having to copy it.
		 */
		acfg_common->raw_key = ao2_alloc(acfg_common->raw_key_length, NULL);
		if (!acfg_common->raw_key) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
				"%s: Could not allocate memory for raw private key\n", id);
		}
		memcpy(acfg_common->raw_key, raw_key, acfg_common->raw_key_length);

	}

	SCOPE_EXIT_RTN_VALUE(0, "%s: Done\n", id);
}

static int attestation_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct attestation_cfg *cfg = obj;
	const char *id = ast_sorcery_object_get_id(cfg);

	if (as_check_common_config(id, &cfg->acfg_common) != 0) {
		return -1;
	}

	return 0;
}

static char *attestation_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct attestation_cfg *cfg;
	struct config_object_cli_data data = {
		.title = "Default Attestation",
		.object_type = config_object_type_attestation,
	};

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show attestation";
		e->usage =
			"Usage: stir_shaken show attestation\n"
			"       Show the stir/shaken attestation settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	if (!as_is_config_loaded()) {
		ast_log(LOG_WARNING,"Stir/Shaken attestation service disabled.  Either there were errors in the 'attestation' object in stir_shaken.conf or it was missing altogether.\n");
		return CLI_FAILURE;
	}

	cfg = as_get_cfg();
	config_object_cli_show(cfg, a, &data, 0);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}

static struct ast_cli_entry attestation_cli[] = {
	AST_CLI_DEFINE(attestation_show, "Show stir/shaken attestation configuration"),
};

int as_config_reload(void)
{
	struct ast_sorcery *sorcery = get_sorcery();
	ast_sorcery_force_reload_object(sorcery, CONFIG_TYPE);

	if (!as_is_config_loaded()) {
		ast_log(LOG_WARNING,"Stir/Shaken attestation service disabled.  Either there were errors in the 'attestation' object in stir_shaken.conf or it was missing altogether.\n");
	}
	if (!empty_cfg) {
		empty_cfg = attestation_alloc(CONFIG_TYPE);
		if (!empty_cfg) {
			return -1;
		}
		empty_cfg->global_disable = 1;
	}

	return 0;
}

int as_config_unload(void)
{
	ast_cli_unregister_multiple(attestation_cli,
		ARRAY_LEN(attestation_cli));
	ao2_cleanup(empty_cfg);

	return 0;
}

int as_config_load(void)
{
	struct ast_sorcery *sorcery = get_sorcery();

	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config",
		"stir_shaken.conf,criteria=type=" CONFIG_TYPE ",single_object=yes,explicit_name=" CONFIG_TYPE);

	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, attestation_alloc,
			NULL, attestation_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return -1;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, CONFIG_TYPE, "type",
		"", OPT_NOOP_T, 0, 0);

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "global_disable",
		DEFAULT_global_disable ? "yes" : "no",
		OPT_YESNO_T, 1, FLDSET(struct attestation_cfg, global_disable));

	enum_option_register_ex(sorcery, CONFIG_TYPE, unknown_tn_attest_level,
		unknown_tn_attest_level, attest_level,);

	register_common_attestation_fields(sorcery, attestation_cfg, CONFIG_TYPE,);

	ast_sorcery_load_object(sorcery, CONFIG_TYPE);

	if (!as_is_config_loaded()) {
		ast_log(LOG_WARNING,"Stir/Shaken attestation service disabled.  Either there were errors in the 'attestation' object in stir_shaken.conf or it was missing altogether.\n");
	}
	if (!empty_cfg) {
		empty_cfg = attestation_alloc(CONFIG_TYPE);
		if (!empty_cfg) {
			return -1;
		}
		empty_cfg->global_disable = 1;
	}

	ast_cli_register_multiple(attestation_cli,
		ARRAY_LEN(attestation_cli));

	return 0;
}
