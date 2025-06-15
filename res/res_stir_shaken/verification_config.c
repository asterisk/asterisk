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

#define _TRACE_PREFIX_ "vc",__LINE__, ""

#include "asterisk.h"

#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "stir_shaken.h"

#define CONFIG_TYPE "verification"

#define DEFAULT_global_disable 0

#define DEFAULT_ca_file NULL
#define DEFAULT_ca_path NULL
#define DEFAULT_crl_file NULL
#define DEFAULT_crl_path NULL
#define DEFAULT_untrusted_cert_file NULL
#define DEFAULT_untrusted_cert_path NULL
static char DEFAULT_cert_cache_dir[PATH_MAX];

#define DEFAULT_curl_timeout 2
#define DEFAULT_max_iat_age 15
#define DEFAULT_max_date_header_age 15
#define DEFAULT_max_cache_entry_age 3600
#define DEFAULT_max_cache_size 1000
#define DEFAULT_stir_shaken_failure_action stir_shaken_failure_action_CONTINUE
#define DEFAULT_use_rfc9410_responses use_rfc9410_responses_NO
#define DEFAULT_relax_x5u_port_scheme_restrictions relax_x5u_port_scheme_restrictions_NO
#define DEFAULT_relax_x5u_path_restrictions relax_x5u_path_restrictions_NO
#define DEFAULT_load_system_certs load_system_certs_NO
#define DEFAULT_ignore_sip_date_header ignore_sip_date_header_NO

static struct verification_cfg *empty_cfg = NULL;

#define STIR_SHAKEN_DIR_NAME "stir_shaken"

struct verification_cfg *vs_get_cfg(void)
{
	struct verification_cfg *cfg = ast_sorcery_retrieve_by_id(get_sorcery(),
		CONFIG_TYPE, CONFIG_TYPE);
	if (cfg) {
		return cfg;
	}

	return empty_cfg ? ao2_bump(empty_cfg) : NULL;
}

int vs_is_config_loaded(void)
{
	struct verification_cfg *cfg = ast_sorcery_retrieve_by_id(get_sorcery(),
		CONFIG_TYPE, CONFIG_TYPE);
	ao2_cleanup(cfg);

	return !!cfg;
}

generate_vcfg_common_sorcery_handlers(verification_cfg);

void vcfg_cleanup(struct verification_cfg_common *vcfg_common)
{
	if (!vcfg_common) {
		return;
	}
	ast_string_field_free_memory(vcfg_common);
	if (vcfg_common->tcs) {
		crypto_free_cert_store(vcfg_common->tcs);
	}
	ast_free_acl_list(vcfg_common->acl);
}

static void verification_destructor(void *obj)
{
	struct verification_cfg *cfg = obj;
	ast_string_field_free_memory(cfg);
	vcfg_cleanup(&cfg->vcfg_common);
}

static void *verification_alloc(const char *name)
{
	struct verification_cfg *cfg;

	cfg = ast_sorcery_generic_alloc(sizeof(*cfg), verification_destructor);
	if (!cfg) {
		return NULL;
	}

	if (ast_string_field_init(cfg, 1024)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	/*
	 * The memory for vcfg_common actually comes from cfg
	 * due to the weirdness of the STRFLDSET macro used with
	 * sorcery.  We just use a token amount of memory in
	 * this call so the initialize doesn't fail.
	 */
	if (ast_string_field_init(&cfg->vcfg_common, 8)) {
		ao2_ref(cfg, -1);
		return NULL;
	}

	return cfg;
}

int vs_copy_cfg_common(const char *id, struct verification_cfg_common *cfg_dst,
	struct verification_cfg_common *cfg_src)
{
	int rc = 0;

	if (!cfg_dst || !cfg_src) {
		return -1;
	}

	if (!cfg_dst->tcs && cfg_src->tcs) {
		cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, ca_file);
		cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, ca_path);
		cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, crl_file);
		cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, crl_path);
		cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, untrusted_cert_file);
		cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, untrusted_cert_path);
		ao2_bump(cfg_src->tcs);
		cfg_dst->tcs = cfg_src->tcs;
	}

	cfg_sf_copy_wrapper(id, cfg_dst, cfg_src, cert_cache_dir);

	cfg_uint_copy(cfg_dst, cfg_src, curl_timeout);
	cfg_uint_copy(cfg_dst, cfg_src, max_iat_age);
	cfg_uint_copy(cfg_dst, cfg_src, max_date_header_age);
	cfg_uint_copy(cfg_dst, cfg_src, max_cache_entry_age);
	cfg_uint_copy(cfg_dst, cfg_src, max_cache_size);

	cfg_enum_copy(cfg_dst, cfg_src, stir_shaken_failure_action);
	cfg_enum_copy(cfg_dst, cfg_src, use_rfc9410_responses);
	cfg_enum_copy(cfg_dst, cfg_src, relax_x5u_port_scheme_restrictions);
	cfg_enum_copy(cfg_dst, cfg_src, relax_x5u_path_restrictions);
	cfg_enum_copy(cfg_dst, cfg_src, load_system_certs);
	cfg_enum_copy(cfg_dst, cfg_src, ignore_sip_date_header);

	if (cfg_src->acl) {
		ast_free_acl_list(cfg_dst->acl);
		cfg_dst->acl = ast_duplicate_acl_list(cfg_src->acl);
	}

	return rc;
}

int vs_check_common_config(const char *id,
	struct verification_cfg_common *vcfg_common)
{
	SCOPE_ENTER(3, "%s: Checking common config\n", id);

	if (!ast_strlen_zero(vcfg_common->ca_file)
		&& !ast_file_is_readable(vcfg_common->ca_file)) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
			"%s: ca_file '%s' not found, or is unreadable\n",
			id, vcfg_common->ca_file);
	}

	if (!ast_strlen_zero(vcfg_common->ca_path)
		&& !ast_file_is_readable(vcfg_common->ca_path)) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
			"%s: ca_path '%s' not found, or is unreadable\n",
			id, vcfg_common->ca_path);
	}

	if (!ast_strlen_zero(vcfg_common->crl_file)
		&& !ast_file_is_readable(vcfg_common->crl_file)) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
			"%s: crl_file '%s' not found, or is unreadable\n",
			id, vcfg_common->crl_file);
	}

	if (!ast_strlen_zero(vcfg_common->crl_path)
		&& !ast_file_is_readable(vcfg_common->crl_path)) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
			"%s: crl_path '%s' not found, or is unreadable\n",
			id, vcfg_common->crl_path);
	}

	if (!ast_strlen_zero(vcfg_common->untrusted_cert_file)
		&& !ast_file_is_readable(vcfg_common->untrusted_cert_file)) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
			"%s: untrusted_cert_file '%s' not found, or is unreadable\n",
			id, vcfg_common->untrusted_cert_file);
	}

	if (!ast_strlen_zero(vcfg_common->untrusted_cert_path)
		&& !ast_file_is_readable(vcfg_common->untrusted_cert_path)) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
			"%s: untrusted_cert_path '%s' not found, or is unreadable\n",
			id, vcfg_common->untrusted_cert_path);
	}

	if (!ast_strlen_zero(vcfg_common->ca_file)
		|| !ast_strlen_zero(vcfg_common->ca_path)) {
		int rc = 0;

		if (!vcfg_common->tcs) {
			vcfg_common->tcs = crypto_create_cert_store();
			if (!vcfg_common->tcs) {
				SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
					"%s: Unable to create CA cert store\n", id);
			}
		}
		rc = crypto_load_cert_store(vcfg_common->tcs,
			vcfg_common->ca_file, vcfg_common->ca_path);
		if (rc != 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
				"%s: Unable to load CA cert store from '%s' or '%s'\n",
				id, vcfg_common->ca_file, vcfg_common->ca_path);
		}
	}

	if (!ast_strlen_zero(vcfg_common->crl_file)
		|| !ast_strlen_zero(vcfg_common->crl_path)) {
		int rc = 0;

		if (!vcfg_common->tcs) {
			vcfg_common->tcs = crypto_create_cert_store();
			if (!vcfg_common->tcs) {
				SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
					"%s: Unable to create CA cert store\n", id);
			}
		}
		rc = crypto_load_crl_store(vcfg_common->tcs,
			vcfg_common->crl_file, vcfg_common->crl_path);
		if (rc != 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
				"%s: Unable to load CA CRL store from '%s' or '%s'\n",
				id, vcfg_common->crl_file, vcfg_common->crl_path);
		}
	}

	if (!ast_strlen_zero(vcfg_common->untrusted_cert_file)
		|| !ast_strlen_zero(vcfg_common->untrusted_cert_path)) {
		int rc = 0;

		if (!vcfg_common->tcs) {
			vcfg_common->tcs = crypto_create_cert_store();
			if (!vcfg_common->tcs) {
				SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
					"%s: Unable to create CA cert store\n", id);
			}
		}
		rc = crypto_load_untrusted_cert_store(vcfg_common->tcs,
			vcfg_common->untrusted_cert_file, vcfg_common->untrusted_cert_path);
		if (rc != 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
				"%s: Unable to load CA CRL store from '%s' or '%s'\n",
				id, vcfg_common->untrusted_cert_file, vcfg_common->untrusted_cert_path);
		}
	}

	if (vcfg_common->tcs) {
		if (ENUM_BOOL(vcfg_common->load_system_certs, load_system_certs)) {
			X509_STORE_set_default_paths(vcfg_common->tcs->certs);
		}

		if (!ast_strlen_zero(vcfg_common->crl_file)
			|| !ast_strlen_zero(vcfg_common->crl_path)) {
			X509_STORE_set_flags(vcfg_common->tcs->certs, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_EXTENDED_CRL_SUPPORT);
		}
	}

	if (!ast_strlen_zero(vcfg_common->cert_cache_dir)) {
		FILE *fp;
		char *testfile;

		if (ast_asprintf(&testfile, "%s/testfile", vcfg_common->cert_cache_dir) <= 0) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
				"%s: Unable to allocate memory for testfile\n", id);
		}

		fp = fopen(testfile, "w+");
		if (!fp) {
			ast_free(testfile);
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR,
				"%s: cert_cache_dir '%s' was not writable\n",
				id, vcfg_common->cert_cache_dir);
		}
		fclose(fp);
		remove(testfile);
		ast_free(testfile);
	}

	SCOPE_EXIT_RTN_VALUE(0, "%s: Done\n", id);
}

static char *special_addresses[] = {
	"0.0.0.0/8",
	"10.0.0.0/8",
	"100.64.0.0/10",
	"127.0.0.0/8",
	"169.254.0.0/16",
	"172.16.0.0/12",
	"192.0.0.0/24",
	"192.0.0.0/29",
	"192.88.99.0/24",
	"192.168.0.0/16",
	"198.18.0.0/15",
	"198.51.100.0/24",
	"203.0.113.0/24",
	"240.0.0.0/4",
	"255.255.255.255/32",
	"::1/128",
	"::/128",
/*	"64:ff9b::/96", IPv4-IPv6 translation addresses should probably not be blocked by default */
/*	"::ffff:0:0/96", IPv4 mapped addresses should probably not be blocked by default */
	"100::/64",
	"2001::/23",
	"2001::/32",
	"2001:2::/48",
	"2001:db8::/32",
	"2001:10::/28",
/*	"2002::/16", 6to4 should problably not be blocked by default */
	"fc00::/7",
	"fe80::/10",
};

static int verification_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct verification_cfg *cfg = obj;
	const char *id = ast_sorcery_object_get_id(cfg);

	if (vs_check_common_config("verification", &cfg->vcfg_common) !=0) {
		return -1;
	}

	if (!cfg->vcfg_common.acl) {
		int error = 0;
		int ignore;
		int i;

		ast_append_acl("permit", "0.0.0.0/0", &cfg->vcfg_common.acl, &error, &ignore);
		if (error) {
			ast_free_acl_list(cfg->vcfg_common.acl);
			cfg->vcfg_common.acl = NULL;
			ast_log(LOG_ERROR, "%s: Unable to create default acl rule for '%s: %s'\n",
				id, "permit", "0.0.0.0/0");
			return -1;
		}

		for (i = 0; i < ARRAY_LEN(special_addresses); i++) {
			ast_append_acl("deny", special_addresses[i], &cfg->vcfg_common.acl, &error, &ignore);
			if (error) {
				ast_free_acl_list(cfg->vcfg_common.acl);
				cfg->vcfg_common.acl = NULL;
				ast_log(LOG_ERROR, "%s: Unable to create default acl rule for '%s: %s'\n",
					id, "deny", special_addresses[i]);
				return -1;
			}
		}
	}

	return 0;
}

static char *cli_verification_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct verification_cfg *cfg;
	struct config_object_cli_data data = {
		.title = "Default Verification",
		.object_type = config_object_type_verification,
	};

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show verification";
		e->usage =
			"Usage: stir_shaken show verification\n"
			"       Show the stir/shaken verification settings\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	if (!vs_is_config_loaded()) {
		ast_log(LOG_WARNING,"Stir/Shaken verification service disabled.  Either there were errors in the 'verification' object in stir_shaken.conf or it was missing altogether.\n");
		return CLI_FAILURE;
	}

	cfg = vs_get_cfg();
	config_object_cli_show(cfg, a, &data, 0);

	ao2_cleanup(cfg);

	return CLI_SUCCESS;
}

static struct ast_cli_entry verification_cli[] = {
	AST_CLI_DEFINE(cli_verification_show, "Show stir/shaken verification configuration"),
};

int vs_config_reload(void)
{
	struct ast_sorcery *sorcery = get_sorcery();
	ast_sorcery_force_reload_object(sorcery, CONFIG_TYPE);

	if (!vs_is_config_loaded()) {
		ast_log(LOG_WARNING,"Stir/Shaken verification service disabled.  Either there were errors in the 'verification' object in stir_shaken.conf or it was missing altogether.\n");
	}
	if (!empty_cfg) {
		empty_cfg = verification_alloc(CONFIG_TYPE);
		if (!empty_cfg) {
			return -1;
		}
		empty_cfg->global_disable = 1;
	}

	return 0;
}

int vs_config_unload(void)
{
	ast_cli_unregister_multiple(verification_cli,
		ARRAY_LEN(verification_cli));
	ao2_cleanup(empty_cfg);

	return 0;
}

int vs_config_load(void)
{
	struct ast_sorcery *sorcery = get_sorcery();

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

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "global_disable",
		DEFAULT_global_disable ? "yes" : "no",
		OPT_YESNO_T, 1, FLDSET(struct verification_cfg, global_disable));

	register_common_verification_fields(sorcery, verification_cfg, CONFIG_TYPE,);

	ast_sorcery_load_object(sorcery, CONFIG_TYPE);

	if (!vs_is_config_loaded()) {
		ast_log(LOG_WARNING,"Stir/Shaken verification service disabled.  Either there were errors in the 'verification' object in stir_shaken.conf or it was missing altogether.\n");
	}
	if (!empty_cfg) {
		empty_cfg = verification_alloc(CONFIG_TYPE);
		if (!empty_cfg) {
			return -1;
		}
		empty_cfg->global_disable = 1;
	}

	ast_cli_register_multiple(verification_cli,
		ARRAY_LEN(verification_cli));

	return 0;
}
