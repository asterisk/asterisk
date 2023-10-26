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
#include "asterisk/sorcery.h"
#include "asterisk/paths.h"

#include "stir_shaken.h"

#define CONFIG_TYPE "attestation"

#define DEFAULT_global_disable 0
#define DEFAULT_check_tn_cert_public_url 0
#define DEFAULT_default_private_key_path ""
#define DEFAULT_default_public_cert_url ""
#define DEFAULT_attestation ""

struct ss_as_cfg *ss_get_as_cfg(void)
{
	return ast_sorcery_retrieve_by_id(ss_sorcery(),
		CONFIG_TYPE, CONFIG_TYPE);
}

int ss_as_is_config_loaded(void)
{
	struct ss_as_cfg *cfg;
	int res = 0;

	cfg = ss_get_as_cfg();
	if (cfg) {
		res = 1;
		ao2_ref(cfg, -1);
	} else {
		ast_log(LOG_ERROR, "No 'attestation' section could be found in stir/shaken configuration");
	}
	return res;
}

config_enum_handler(ss_as_cfg, check_tn_cert_public_url, AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_UNKNOWN)
config_enum_to_str(ss_as_cfg, check_tn_cert_public_url)

config_enum_handler(ss_as_cfg, attest_level, AST_STIR_SHAKEN_ATTEST_LEVEL_UNKNOWN)
config_enum_to_str(ss_as_cfg, attest_level)

config_enum_handler(ss_as_cfg, send_mky, AST_STIR_SHAKEN_AS_SEND_MKY_UNKNOWN)
config_enum_to_str(ss_as_cfg, send_mky)

static void attestation_destructor(void *obj)
{
	struct ss_as_cfg *cfg = obj;

	ast_string_field_free_memory(cfg);
	if (cfg->raw_key_length) {
		ast_free(cfg->raw_key);
	}
}

static void *attestation_alloc(const char *name)
{
	struct ss_as_cfg *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), attestation_destructor);
	if (!cfg) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 10240)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

static int attestation_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ss_as_cfg *as_cfg = obj;
	const char *id = ast_sorcery_object_get_id(as_cfg);

	if (!ast_strlen_zero(as_cfg->private_key_file) &&
		!ast_file_is_readable(as_cfg->private_key_file)) {
		ast_log(LOG_ERROR, "'%s': default_private_key_path %s is missing or not readable\n", id,
			as_cfg->private_key_file);
		return -1;
	}

	if (!ast_strlen_zero(as_cfg->public_cert_url) &&
		as_cfg->check_tn_cert_public_url) {
		RAII_VAR(char *, public_cert_data, NULL, ast_std_free);
		X509 *public_cert;
		size_t public_cert_len;
		int rc = 0;
		long http_code;

		http_code = curl_download_to_memory(as_cfg->public_cert_url, &public_cert_len,
			&public_cert_data, NULL);
		if (http_code / 100 != 2) {
			ast_log(LOG_ERROR, "'%s': public_cert '%s' could not be downloaded\n", id,
				as_cfg->public_cert_url);
			return -1;
		}

		public_cert = ast_crypto_load_cert_from_memory(public_cert_data,
			public_cert_len);
		if (!public_cert) {
			ast_log(LOG_ERROR, "'%s': public_cert '%s' could not be parsed as a certificate\n", id,
				as_cfg->public_cert_url);
			return -1;
		}
		rc = ast_crypto_is_cert_time_valid(public_cert, 0);
		X509_free(public_cert);
		if (!rc) {
			ast_log(LOG_ERROR, "'%s': public_cert '%s' is not valid yet or has expired\n", id,
				as_cfg->public_cert_url);
			return -1;
		}

		rc = ast_crypto_has_private_key_from_memory(public_cert_data, public_cert_len);
		if (rc) {
			ast_log(LOG_ERROR, "'%s': DANGER!!! public_cert_url '%s' has a private key in the file!!!\n", id,
				as_cfg->public_cert_url);
			return -1;
		}
	}

	if (ast_strlen_zero(as_cfg->private_key_file)) {
		return 0;
	}

	as_cfg->private_key = ast_crypto_load_privkey_from_file(as_cfg->private_key_file);
	if (!as_cfg->private_key) {
		return -1;
	}

	as_cfg->raw_key_length = ast_crypto_extract_raw_privkey(as_cfg->private_key, &as_cfg->raw_key);
	if (as_cfg->raw_key_length == 0 || as_cfg->raw_key == NULL) {
		ast_log(LOG_ERROR, "'%s': Could not extract raw private key from file '%s'\n", id,
			as_cfg->private_key_file);
		return -1;
	}
	return 0;
}

static char *attestation_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ss_as_cfg *cfg;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken attestation show config";
		e->usage =
			"Usage: stir_shaken attestation show config\n"
			"       Show the stir/shaken attestation settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	cfg = ss_get_as_cfg();
	stir_shaken_cli_show(cfg, a, 0);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}

static struct ast_cli_entry attestation_cli[] = {
	AST_CLI_DEFINE(attestation_show, "Show stir/shaken attestation configuration"),
};

int ss_as_config_reload(void)
{
	struct ast_sorcery *sorcery = ss_sorcery();
	ast_sorcery_reload_object(sorcery, CONFIG_TYPE);
	return 0;
}

int ss_as_config_unload(void)
{
	ast_cli_unregister_multiple(attestation_cli,
		ARRAY_LEN(attestation_cli));

	return 0;
}

int ss_as_config_load(void)
{
	struct ast_sorcery *sorcery = ss_sorcery();

	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config",
		"stir_shaken.conf,criteria=type=" CONFIG_TYPE ",single_object=yes,explicit_name=" CONFIG_TYPE);

	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, attestation_alloc,
			NULL, attestation_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return -1;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, CONFIG_TYPE, "type", "",
		OPT_NOOP_T, 0, 0);

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "global_disable",
		DEFAULT_global_disable ? "yes" : "no",
		OPT_YESNO_T, 1, FLDSET(struct ss_as_cfg, global_disable));

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "check_tn_cert_public_url",
		DEFAULT_check_tn_cert_public_url ? "yes" : "no",
		OPT_YESNO_T, 1, FLDSET(struct ss_as_cfg, check_tn_cert_public_url));

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "private_key_file",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_as_cfg, private_key_file));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "public_cert_url",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_as_cfg, public_cert_url));

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "check_tn_cert_public_url",
		ast_stir_shaken_check_tn_cert_public_url_to_str(AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NOT_SET),
		check_tn_cert_public_url_handler,
		check_tn_cert_public_url_to_str, NULL, 0, 0);

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "attest_level",
		ast_stir_shaken_attest_level_to_str(AST_STIR_SHAKEN_ATTEST_LEVEL_NOT_SET), attest_level_handler,
		attest_level_to_str, NULL, 0, 0);

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "send_mky",
		ast_stir_shaken_send_mky_to_str(AST_STIR_SHAKEN_AS_SEND_MKY_NO), send_mky_handler,
		send_mky_to_str, NULL, 0, 0);

	ast_sorcery_load_object(sorcery, CONFIG_TYPE);

	ast_cli_register_multiple(attestation_cli,
		ARRAY_LEN(attestation_cli));

	return 0;
}
