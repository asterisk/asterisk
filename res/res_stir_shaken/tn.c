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

struct ss_tn *ss_tn_get(const char *id)
{
	return ast_sorcery_retrieve_by_id(ss_sorcery(), CONFIG_TYPE, id);
}

static struct ao2_container *get_tn_all(void)
{
	return ast_sorcery_retrieve_by_fields(ss_sorcery(), CONFIG_TYPE,
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

config_enum_handler(ss_tn, attest_level, AST_STIR_SHAKEN_ATTEST_LEVEL_UNKNOWN)
config_enum_to_str(ss_tn, attest_level)

static void tn_destructor(void *obj)
{
	struct ss_tn *cfg = obj;

	ast_string_field_free_memory(cfg);
	ast_free(cfg->raw_key);
}

static void *tn_alloc(const char *name)
{
	struct ss_tn *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), tn_destructor);
	if (!cfg) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 1024)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

static int tn_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ss_tn *tn = obj;
	RAII_VAR(struct ss_as_cfg *, as_cfg,
		ss_get_as_cfg(), ao2_cleanup);
	const char *id = ast_sorcery_object_get_id(tn);
	int rc = 0;

	if (ast_strlen_zero(tn->private_key_file) &&
		ast_strlen_zero(as_cfg->private_key_file)) {
		ast_log(LOG_ERROR, "'%s': No private_key_file specified no default in attestation object\n", id);
		return -1;
	}

	if (tn->attest_level == AST_STIR_SHAKEN_ATTEST_LEVEL_NOT_SET &&
		as_cfg->attest_level == AST_STIR_SHAKEN_ATTEST_LEVEL_NOT_SET) {
		ast_log(LOG_ERROR, "'%s': No attest_level specified and no default in attestation object\n", id);
		return -1;
	}

	if (!ast_strlen_zero(tn->private_key_file) &&
		!ast_file_is_readable(tn->private_key_file)) {
		ast_log(LOG_ERROR, "'%s': private_key_file %s is missing or not readable\n", id,
			tn->private_key_file);
		return -1;
	}

	if (!ast_strlen_zero(tn->public_cert_url) &&
		as_cfg->check_tn_cert_public_url == AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_YES) {
		RAII_VAR(char *, public_cert_data, NULL, ast_std_free);
		X509 *public_cert;
		size_t public_cert_len;
		long http_code;

		http_code = curl_download_to_memory(tn->public_cert_url, &public_cert_len,
			&public_cert_data, NULL);
		if (http_code / 100 != 2) {
			ast_log(LOG_ERROR, "'%s': public_cert '%s' could not be downloaded\n", id,
				tn->public_cert_url);
			return -1;
		}

		public_cert = ast_crypto_load_cert_from_memory(public_cert_data,
			public_cert_len);
		if (!public_cert) {
			ast_log(LOG_ERROR, "'%s': public_cert '%s' could not be parsed as a certificate\n", id,
				tn->public_cert_url);
			return -1;
		}
		rc = ast_crypto_is_cert_time_valid(public_cert, 0);
		X509_free(public_cert);
		if (!rc) {
			ast_log(LOG_ERROR, "'%s': public_cert '%s' is not valid yet or has expired\n", id,
				tn->public_cert_url);
			return -1;
		}

		rc = ast_crypto_has_private_key_from_memory(public_cert_data, public_cert_len);
		if (rc) {
			ast_log(LOG_ERROR, "'%s': DANGER!!! public_cert_url '%s' has a private key in the file!!!\n", id,
				tn->public_cert_url);
			return -1;
		}
	}

	if (!ast_strlen_zero(tn->private_key_file)) {
		tn->private_key = ast_crypto_load_privkey_from_file(tn->private_key_file);
		if (!tn->private_key) {
			ast_log(LOG_ERROR, "'%s': Could not parse file '%s' as private key\n", id,
				tn->private_key_file);
			return -1;
		}

		tn->raw_key_length = ast_crypto_extract_raw_privkey(tn->private_key, &tn->raw_key);
		EVP_PKEY_free(tn->private_key);
		if (tn->raw_key_length == 0 || tn->raw_key == NULL) {
			ast_log(LOG_ERROR, "'%s': Could not extract raw private key from file '%s'\n", id,
				tn->private_key_file);
			return -1;
		}
		return 0;
	}

	if (as_cfg->raw_key_length == 0) {
		ast_log(LOG_ERROR, "'%s': No private key specified in tn object and no default in attestation object\n", id);
		return -1;
	}

	return rc;
}

static char *cli_tn_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken attestation tn show all";
		e->usage =
			"Usage: stir_shaken attestation tn show all\n"
			"       Show all attestation TNs\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	container = get_tn_all();
	if (!container || ao2_container_count(container) == 0) {
		ast_cli(a->fd, "No stir/shaken TNs found\n");
		ao2_cleanup(container);
		return CLI_SUCCESS;
	}

	ao2_callback(container, OBJ_NODATA, stir_shaken_cli_show, a);
	ao2_ref(container, -1);

	return CLI_SUCCESS;
}

static char *cli_tn_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ss_tn *cfg;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken attestation tn show";
		e->usage =
			"Usage: stir_shaken attestation tn show <id>\n"
			"       Show the settings for a given TN\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return stir_shaken_tab_complete_name(a->word, get_tn_all());
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	if (strcmp(a->argv[3], "all")) {
		return cli_tn_show_all(e, cmd, a);
	}
	cfg = ss_tn_get(a->argv[3]);
	stir_shaken_cli_show(cfg, a, 0);
	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}


static struct ast_cli_entry stir_shaken_certificate_cli[] = {
	AST_CLI_DEFINE(cli_tn_show, "Show stir/shaken attestation TN configuration by id"),
	AST_CLI_DEFINE(cli_tn_show_all, "Show all stir/shaken attestation TN configurations"),
};

int ss_tn_reload(void)
{
	struct ast_sorcery *sorcery = ss_sorcery();
	ast_sorcery_reload_object(sorcery, CONFIG_TYPE);
	return AST_MODULE_LOAD_SUCCESS;
}

int ss_tn_unload(void)
{
	ast_cli_unregister_multiple(stir_shaken_certificate_cli,
		ARRAY_LEN(stir_shaken_certificate_cli));

	return 0;
}

int ss_tn_load(void)
{
	struct ast_sorcery *sorcery = ss_sorcery();

	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config", "stir_shaken.conf,criteria=type=tn");

	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, tn_alloc,
			NULL, tn_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, CONFIG_TYPE, "type", "",
		OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "private_key_file",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_tn, private_key_file));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "public_cert_url",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_tn, public_cert_url));
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "attest_level",
		ast_stir_shaken_attest_level_to_str(AST_STIR_SHAKEN_ATTEST_LEVEL_NOT_SET), attest_level_handler,
		attest_level_to_str, NULL, 0, 0);

	ast_sorcery_load_object(sorcery, CONFIG_TYPE);

	ast_cli_register_multiple(stir_shaken_certificate_cli,
		ARRAY_LEN(stir_shaken_certificate_cli));

	return AST_MODULE_LOAD_SUCCESS;
}
