/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2022, Sangoma Technologies Corporation
 *
 * Ben Ford <bford@sangoma.com>
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
#include "asterisk/acl.h"

#include "stir_shaken.h"

#define CONFIG_TYPE "profile"

static void profile_destructor(void *obj)
{
	struct ss_profile *cfg = obj;
	ast_free_acl_list(cfg->acl);
	ast_string_field_free_memory(cfg);
	ast_free(cfg->raw_key);

	return;
}

static void *profile_alloc(const char *name)
{
	struct ss_profile *profile;

	profile = ast_sorcery_generic_alloc(sizeof(*profile), profile_destructor);
	if (!profile) {
		return NULL;
	}

	if (ast_string_field_init(profile, 1024)) {
		ao2_ref(profile, -1);
		return NULL;
	}

	return profile;
}

static struct ao2_container *profile_get_all(void)
{
	return ast_sorcery_retrieve_by_fields(ss_sorcery(), CONFIG_TYPE,
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

struct ss_profile *ss_get_profile(const char *id)
{
	if (ast_strlen_zero(id)) {
		return NULL;
	}
	return ast_sorcery_retrieve_by_id(ss_sorcery(), CONFIG_TYPE, id);
}

static int profile_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ss_profile *cfg = obj;
	struct ss_trusted_cert_store *tcs = ss_get_trusted_cert_store();
	RAII_VAR(struct ss_as_cfg *, as_cfg,
		ss_get_as_cfg(), ao2_cleanup);
	const char *id = ast_sorcery_object_get_id(cfg);
	int rc = 0;

	if (!ast_strlen_zero(cfg->private_key_file) &&
		!ast_file_is_readable(cfg->private_key_file)) {
		ast_log(LOG_ERROR, "%s: private_key_file %s is missing or not readable\n", id,
			cfg->private_key_file);
		return -1;
	}

	if (!ast_strlen_zero(cfg->public_cert_url) &&
		as_cfg->check_tn_cert_public_url) {
		RAII_VAR(char *, public_cert_data, NULL, ast_std_free);
		X509 *public_cert;
		size_t public_cert_len;
		long http_code;

		http_code = curl_download_to_memory(cfg->public_cert_url, &public_cert_len,
			&public_cert_data, NULL);
		if (http_code / 100 != 2) {
			ast_log(LOG_ERROR, "%s: public_cert '%s' could not be downloaded\n", id,
				cfg->public_cert_url);
			return -1;
		}

		public_cert = ast_crypto_load_cert_from_memory(public_cert_data,
			public_cert_len);
		if (!public_cert) {
			ast_log(LOG_ERROR, "%s: public_cert '%s' could not be parsed as a certificate\n", id,
				cfg->public_cert_url);
			return -1;
		}
		rc = ast_crypto_is_cert_time_valid(public_cert, 0);
		X509_free(public_cert);
		if (!rc) {
			ast_log(LOG_ERROR, "%s: public_cert '%s' is not valid yet or has expired\n", id,
				cfg->public_cert_url);
			return -1;
		}

		rc = ast_crypto_has_private_key_from_memory(public_cert_data, public_cert_len);
		if (rc) {
			ast_log(LOG_ERROR, "%s: DANGER!!! public_cert_url '%s' has a private key in the file!!!\n", id,
				cfg->public_cert_url);
			return -1;
		}
	}

	if (!ast_strlen_zero(cfg->private_key_file)) {
		cfg->private_key = ast_crypto_load_privkey_from_file(cfg->private_key_file);
		if (!cfg->private_key) {
			ast_log(LOG_ERROR, "%s: Could not parse file '%s' as private key\n", id,
				cfg->private_key_file);
			return -1;
		}

		cfg->raw_key_length = ast_crypto_extract_raw_privkey(cfg->private_key, &cfg->raw_key);
		EVP_PKEY_free(cfg->private_key);
		if (cfg->raw_key_length == 0 || cfg->raw_key == NULL) {
			ast_log(LOG_ERROR, "%s: Could not extract raw private key from file '%s'\n", id,
				cfg->private_key_file);
			return -1;
		}
		return 0;
	}

	if (!ast_strlen_zero(cfg->ca_file) && !ast_file_is_readable(cfg->ca_file)) {
		ast_log(LOG_ERROR, "%s: ca_file '%s' not found, or is unreadable\n",
			id, cfg->ca_file);
		return -1;
	}

	if (!ast_strlen_zero(cfg->ca_path) && !ast_file_is_readable(cfg->ca_path)) {
		ast_log(LOG_ERROR, "%s: ca_path '%s' not found, or is unreadable\n",
			id, cfg->ca_path);
		return -1;
	}

	if (!ast_strlen_zero(cfg->crl_file) && !ast_file_is_readable(cfg->crl_file)) {
		ast_log(LOG_ERROR, "%s: crl_file '%s' not found, or is unreadable\n",
			id, cfg->ca_file);
		return -1;
	}

	if (!ast_strlen_zero(cfg->crl_path) && !ast_file_is_readable(cfg->crl_path)) {
		ast_log(LOG_ERROR, "%s: stir/shaken - crl_path '%s' not found, or is unreadable\n",
			id, cfg->ca_path);
		return -1;
	}

	if (!ast_strlen_zero(cfg->ca_file) || !ast_strlen_zero(cfg->ca_path)) {
		rc = ast_crypto_load_cert_store(tcs->store, cfg->ca_file, cfg->ca_path);
		if (rc != 0) {
			ast_log(LOG_ERROR, "%s: Unable to load CA cert store from '%s' or '%s'\n",
				id, cfg->ca_file, cfg->ca_path);
			return -1;
		}
	}

	if (!ast_strlen_zero(cfg->crl_file) || !ast_strlen_zero(cfg->crl_path)) {
		rc = ast_crypto_load_cert_store(tcs->store, cfg->crl_file, cfg->crl_path);
		if (rc != 0) {
			ast_log(LOG_ERROR, "%s: Unable to load CA CRL store from '%s' or '%s'\n",
				id, cfg->crl_file, cfg->crl_path);
			return -1;
		}
	}

	if (!ast_strlen_zero(cfg->cert_cache_dir)) {
		FILE *fp;
		char *testfile;
		if (ast_asprintf(&testfile, "%s/testfile", cfg->cert_cache_dir) <= 0) {
			ast_log(LOG_ERROR, "%s: Unable to allocate memory for testfile\n", id);
			return -1;
		}

		fp = fopen(testfile, "w+");
		if (!fp) {
			ast_free(testfile);
			ast_log(LOG_ERROR, "%s: cert_cache_dir '%s' was not writable\n",
				id, cfg->cert_cache_dir);
			return -1;
		}
		fclose(fp);
		remove(testfile);
		ast_free(testfile);
	}

	return 0;
}

config_enum_handler(ss_profile, behavior, AST_STIR_SHAKEN_BEHAVIOR_UNKNOWN)
config_enum_to_str(ss_profile, behavior)

config_enum_handler(ss_profile, use_rfc9410_responses, AST_STIR_SHAKEN_VS_RFC9410_UNKNOWN)
config_enum_to_str(ss_profile, use_rfc9410_responses)

config_enum_handler(ss_profile, failure_action, AST_STIR_SHAKEN_VS_FAILURE_UNKNOWN)
config_enum_to_str(ss_profile, failure_action)

config_enum_handler(ss_profile, check_tn_cert_public_url, AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_UNKNOWN)
config_enum_to_str(ss_profile, check_tn_cert_public_url)

config_enum_handler(ss_profile, send_mky, AST_STIR_SHAKEN_AS_SEND_MKY_UNKNOWN)
config_enum_to_str(ss_profile, send_mky)

config_enum_handler(ss_profile, attest_level, AST_STIR_SHAKEN_ATTEST_LEVEL_UNKNOWN)
config_enum_to_str(ss_profile, attest_level)

static int acl_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ss_profile *profile = obj;
	int error = 0;
	int ignore;

	if (ast_strlen_zero(var->value)) {
		return 0;
	}

	ast_append_acl(var->name, var->value, &profile->acl, &error, &ignore);

	return error;
}

static int acl_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ss_profile *profile = obj;
	struct ast_acl *first_acl;

	if (!ast_acl_list_is_empty(profile->acl)) {
		AST_LIST_LOCK(profile->acl);
		first_acl = AST_LIST_FIRST(profile->acl);
		if (ast_strlen_zero(first_acl->name)) {
			*buf = "deny/permit";
		} else {
			*buf = first_acl->name;
		}
		AST_LIST_UNLOCK(profile->acl);
	}

	*buf = ast_strdup(*buf);
	return 0;
}

static char *cli_profile_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ss_profile *profile;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show profile";
		e->usage =
			"Usage: stir_shaken show profile <id>\n"
			"       Show the stir/shaken profile settings for a given id\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return stir_shaken_tab_complete_name(a->word, profile_get_all());
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	profile = ss_get_profile(a->argv[3]);
	stir_shaken_cli_show(profile, a, 0);
	if (profile->acl) {
		ast_acl_output(a->fd, profile->acl, NULL);
	}
	ao2_cleanup(profile);

	return CLI_SUCCESS;
}

static char *cli_profile_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show profiles";
		e->usage =
			"Usage: stir_shaken show profiles\n"
			"       Show all profiles for stir/shaken\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	container = profile_get_all();
	if (!container || ao2_container_count(container) == 0) {
		ast_cli(a->fd, "No stir/shaken ACLs found\n");
		ao2_cleanup(container);
		return CLI_SUCCESS;
	}

	ao2_callback(container, OBJ_NODATA, stir_shaken_cli_show, a);
	ao2_ref(container, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry stir_shaken_profile_cli[] = {
	AST_CLI_DEFINE(cli_profile_show, "Show stir/shaken profile by id"),
	AST_CLI_DEFINE(cli_profile_show_all, "Show all stir/shaken profiles"),
};

int ss_profile_reload(void)
{
	struct ast_sorcery *sorcery = ss_sorcery();
	ast_sorcery_reload_object(sorcery, CONFIG_TYPE);
	return 0;
}

int ss_profile_unload(void)
{
	ast_cli_unregister_multiple(stir_shaken_profile_cli,
		ARRAY_LEN(stir_shaken_profile_cli));

	return 0;
}

int ss_profile_load(void)
{
	struct ast_sorcery *sorcery = ss_sorcery();

	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config", "stir_shaken.conf,criteria=type=profile");

	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, profile_alloc,
		NULL, profile_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "type", "", OPT_NOOP_T, 0, 0);

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "ca_file",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, ca_file));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "ca_path",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, ca_path));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "crl_file",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, crl_file));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "crl_path",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, crl_path));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "cert_cache_dir",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_vs_cfg, cert_cache_dir));

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "curl_timeout",
		0, OPT_UINT_T, 0, FLDSET(struct ss_vs_cfg, curl_timeout));

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "max_iat_age",
		0, OPT_UINT_T, 0, FLDSET(struct ss_vs_cfg, max_iat_age));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "max_date_header_age",
		0, OPT_UINT_T, 0, FLDSET(struct ss_vs_cfg, max_date_header_age));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "max_cache_entry_age",
		0, OPT_UINT_T, 0, FLDSET(struct ss_vs_cfg, max_cache_entry_age));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "max_cache_size",
		0, OPT_UINT_T, 0, FLDSET(struct ss_vs_cfg, max_cache_size));

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE,
		"behavior", ast_stir_shaken_behavior_to_str(AST_STIR_SHAKEN_BEHAVIOR_OFF),
		behavior_handler, behavior_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "failure_action",
		ast_stir_shaken_failure_action_to_str(AST_STIR_SHAKEN_VS_FAILURE_NOT_SET), failure_action_handler,
		failure_action_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "use_rfc9410_responses",
		ast_stir_shaken_use_rfc9410_responses_to_str(AST_STIR_SHAKEN_VS_RFC9410_NOT_SET), use_rfc9410_responses_handler,
		use_rfc9410_responses_to_str, NULL, 0, 0);

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "check_tn_cert_public_url",
		ast_stir_shaken_check_tn_cert_public_url_to_str(AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NOT_SET),
		check_tn_cert_public_url_handler,
		check_tn_cert_public_url_to_str, NULL, 0, 0);

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "private_key_file",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_as_cfg, private_key_file));
	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "public_cert_url",
		"", OPT_STRINGFIELD_T, 0,
		STRFLDSET(struct ss_as_cfg, public_cert_url));

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "attest_level",
		ast_stir_shaken_attest_level_to_str(AST_STIR_SHAKEN_ATTEST_LEVEL_NOT_SET), attest_level_handler,
		attest_level_to_str, NULL, 0, 0);

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "send_mky",
		ast_stir_shaken_send_mky_to_str(AST_STIR_SHAKEN_AS_SEND_MKY_NOT_SET), send_mky_handler,
		send_mky_to_str, NULL, 0, 0);

	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "deny", "", acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "permit", "", acl_handler, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, CONFIG_TYPE, "acllist", "", acl_handler, acl_to_str, NULL, 0, 0);

	ast_sorcery_load_object(sorcery, CONFIG_TYPE);

	ast_cli_register_multiple(stir_shaken_profile_cli,
		ARRAY_LEN(stir_shaken_profile_cli));

	return 0;
}
