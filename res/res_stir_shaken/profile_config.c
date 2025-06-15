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
#include "asterisk/stasis.h"
#include "asterisk/security_events.h"

#include "stir_shaken.h"

#define CONFIG_TYPE "profile"

#define DEFAULT_endpoint_behavior endpoint_behavior_OFF

#define DEFAULT_ca_file NULL
#define DEFAULT_ca_path NULL
#define DEFAULT_crl_file NULL
#define DEFAULT_crl_path NULL
#define DEFAULT_untrusted_cert_file NULL
#define DEFAULT_untrusted_cert_path NULL
#define DEFAULT_cert_cache_dir NULL

#define DEFAULT_curl_timeout 0
#define DEFAULT_max_iat_age 0
#define DEFAULT_max_date_header_age 0
#define DEFAULT_max_cache_entry_age 0
#define DEFAULT_max_cache_size 0

#define DEFAULT_stir_shaken_failure_action stir_shaken_failure_action_NOT_SET
#define DEFAULT_use_rfc9410_responses use_rfc9410_responses_NOT_SET
#define DEFAULT_relax_x5u_port_scheme_restrictions relax_x5u_port_scheme_restrictions_NOT_SET
#define DEFAULT_relax_x5u_path_restrictions relax_x5u_path_restrictions_NOT_SET
#define DEFAULT_load_system_certs load_system_certs_NOT_SET
#define DEFAULT_ignore_sip_date_header ignore_sip_date_header_NOT_SET

#define DEFAULT_check_tn_cert_public_url check_tn_cert_public_url_NOT_SET
#define DEFAULT_private_key_file NULL
#define DEFAULT_public_cert_url NULL
#define DEFAULT_attest_level attest_level_NOT_SET
#define DEFAULT_unknown_tn_attest_level attest_level_NOT_SET
#define DEFAULT_send_mky send_mky_NOT_SET

static void profile_destructor(void *obj)
{
	struct profile_cfg *cfg = obj;
	ast_string_field_free_memory(cfg);

	acfg_cleanup(&cfg->acfg_common);
	vcfg_cleanup(&cfg->vcfg_common);

	ao2_cleanup(cfg->eprofile);

	return;
}

static void *profile_alloc(const char *name)
{
	struct profile_cfg *profile;

	profile = ast_sorcery_generic_alloc(sizeof(*profile), profile_destructor);
	if (!profile) {
		return NULL;
	}

	if (ast_string_field_init(profile, 2048)) {
		ao2_ref(profile, -1);
		return NULL;
	}

	/*
	 * The memory for the commons actually comes from cfg
	 * due to the weirdness of the STRFLDSET macro used with
	 * sorcery.  We just use a token amount of memory in
	 * this call so the initialize doesn't fail.
	 */
	if (ast_string_field_init(&profile->acfg_common, 8)) {
		ao2_ref(profile, -1);
		return NULL;
	}

	if (ast_string_field_init(&profile->vcfg_common, 8)) {
		ao2_ref(profile, -1);
		return NULL;
	}

	return profile;
}

struct ao2_container *profile_get_all(void)
{
	return ast_sorcery_retrieve_by_fields(get_sorcery(), CONFIG_TYPE,
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

struct profile_cfg *profile_get_cfg(const char *id)
{
	if (ast_strlen_zero(id)) {
		return NULL;
	}
	return ast_sorcery_retrieve_by_id(get_sorcery(), CONFIG_TYPE, id);
}

struct ao2_container *eprofile_get_all(void)
{
	return ast_sorcery_retrieve_by_fields(get_sorcery(), "eprofile",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
}

struct profile_cfg *eprofile_get_cfg(const char *id)
{
	if (ast_strlen_zero(id)) {
		return NULL;
	}
	return ast_sorcery_retrieve_by_id(get_sorcery(), "eprofile", id);
}

static struct profile_cfg *create_effective_profile(
	struct profile_cfg *base_profile)
{
	struct profile_cfg *eprofile;
	struct profile_cfg *existing_eprofile;
	RAII_VAR(struct attestation_cfg*, acfg, as_get_cfg(), ao2_cleanup);
	RAII_VAR(struct verification_cfg*, vcfg, vs_get_cfg(), ao2_cleanup);
	const char *id = ast_sorcery_object_get_id(base_profile);
	int rc = 0;

	eprofile = ast_sorcery_alloc(get_sorcery(), "eprofile", id);
	if (!eprofile) {
		ast_log(LOG_ERROR, "%s: Unable to allocate memory for effective profile\n", id);
		return NULL;
	}

	rc = vs_copy_cfg_common(id, &eprofile->vcfg_common,
		&vcfg->vcfg_common);
	if (rc != 0) {
		ao2_cleanup(eprofile);
		return NULL;
	}

	rc = vs_copy_cfg_common(id, &eprofile->vcfg_common,
		&base_profile->vcfg_common);
	if (rc != 0) {
		ao2_cleanup(eprofile);
		return NULL;
	}

	rc = as_copy_cfg_common(id, &eprofile->acfg_common,
		&acfg->acfg_common);
	if (rc != 0) {
		ao2_cleanup(eprofile);
		return NULL;
	}

	cfg_enum_copy_ex(eprofile, acfg, unknown_tn_attest_level,
		attest_level_NOT_SET, attest_level_UNKNOWN);

	rc = as_copy_cfg_common(id, &eprofile->acfg_common,
		&base_profile->acfg_common);
	if (rc != 0) {
		ao2_cleanup(eprofile);
		return NULL;
	}

	cfg_enum_copy_ex(eprofile, base_profile, unknown_tn_attest_level,
		attest_level_NOT_SET, attest_level_UNKNOWN);


	eprofile->endpoint_behavior = base_profile->endpoint_behavior;

	if (eprofile->endpoint_behavior == endpoint_behavior_ON) {
		if (acfg->global_disable && vcfg->global_disable) {
			eprofile->endpoint_behavior = endpoint_behavior_OFF;
		} else if (acfg->global_disable && !vcfg->global_disable) {
			eprofile->endpoint_behavior = endpoint_behavior_VERIFY;
		} else if (!acfg->global_disable && vcfg->global_disable) {
			eprofile->endpoint_behavior = endpoint_behavior_ATTEST;
		}
	} else if (eprofile->endpoint_behavior == endpoint_behavior_ATTEST
		&& acfg->global_disable) {
			eprofile->endpoint_behavior = endpoint_behavior_OFF;
	} else if (eprofile->endpoint_behavior == endpoint_behavior_VERIFY
		&& vcfg->global_disable) {
			eprofile->endpoint_behavior = endpoint_behavior_OFF;
	}

	existing_eprofile = ast_sorcery_retrieve_by_id(get_sorcery(), "eprofile", id);
	if (existing_eprofile) {
		ao2_cleanup(existing_eprofile);
		ast_sorcery_update(get_sorcery(), eprofile);
	} else {
		ast_sorcery_create(get_sorcery(), eprofile);
	}

	/*
	 * This triggers eprofile_apply.  We _could_ just call
	 * eprofile_apply directly but this seems more keeping
	 * with how sorcery works.
	 */
	ast_sorcery_objectset_apply(get_sorcery(), eprofile, NULL);

	return eprofile;
}

static int profile_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct profile_cfg *cfg = obj;
	const char *id = ast_sorcery_object_get_id(cfg);

	if (PROFILE_ALLOW_ATTEST(cfg)
		&& as_check_common_config(id, &cfg->acfg_common) != 0) {
		return -1;
	}

	if (PROFILE_ALLOW_VERIFY(cfg)
		&& vs_check_common_config(id, &cfg->vcfg_common) !=0) {
		return -1;
	}

	cfg->eprofile = create_effective_profile(cfg);
	if (!cfg->eprofile) {
		return -1;
	}

	return 0;
}

static int eprofile_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct profile_cfg *cfg = obj;
	const char *id = ast_sorcery_object_get_id(cfg);

	if (PROFILE_ALLOW_VERIFY(cfg) && !cfg->vcfg_common.tcs) {
		ast_log(LOG_ERROR, "%s: Neither this profile nor default"
			" verification options specify ca_file or ca_path\n", id);
		return -1;
	}

	return 0;
}
generate_acfg_common_sorcery_handlers(profile_cfg);
generate_vcfg_common_sorcery_handlers(profile_cfg);

generate_sorcery_enum_from_str(profile_cfg, , endpoint_behavior, UNKNOWN);
generate_sorcery_enum_to_str(profile_cfg, , endpoint_behavior);

generate_sorcery_enum_from_str_ex(profile_cfg,,unknown_tn_attest_level, attest_level, UNKNOWN);
generate_sorcery_enum_to_str_ex(profile_cfg,,unknown_tn_attest_level, attest_level);

static char *cli_profile_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct profile_cfg *profile;
	struct config_object_cli_data data = {
		.title = "Profile",
		.object_type = config_object_type_profile,
	};

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show profile";
		e->usage =
			"Usage: stir_shaken show profile <id>\n"
			"       Show the stir/shaken profile settings for a given id\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return config_object_tab_complete_name(a->word, profile_get_all());
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	profile = profile_get_cfg(a->argv[3]);
	if (!profile) {
		ast_log(LOG_ERROR,"Profile %s doesn't exist\n", a->argv[3]);
		return CLI_FAILURE;
	}
	config_object_cli_show(profile, a, &data, 0);

	ao2_cleanup(profile);

	return CLI_SUCCESS;
}

static char *cli_profile_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;
	struct config_object_cli_data data = {
		.title = "Profile",
		.object_type = config_object_type_profile,
	};

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
		ast_cli(a->fd, "No stir/shaken profiles found\n");
		ao2_cleanup(container);
		return CLI_SUCCESS;
	}

	ao2_callback_data(container, OBJ_NODATA, config_object_cli_show, a, &data);
	ao2_ref(container, -1);

	return CLI_SUCCESS;
}

static char *cli_eprofile_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct profile_cfg *profile;
	struct config_object_cli_data data = {
		.title = "Effective Profile",
		.object_type = config_object_type_profile,
	};

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show eprofile";
		e->usage =
			"Usage: stir_shaken show eprofile <id>\n"
			"       Show the stir/shaken eprofile settings for a given id\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return config_object_tab_complete_name(a->word, eprofile_get_all());
		} else {
			return NULL;
		}
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	profile = eprofile_get_cfg(a->argv[3]);
	if (!profile) {
		ast_log(LOG_ERROR,"Effective Profile %s doesn't exist\n", a->argv[3]);
		return CLI_FAILURE;
	}
	config_object_cli_show(profile, a, &data, 0);

	ao2_cleanup(profile);

	return CLI_SUCCESS;
}

static char *cli_eprofile_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_container *container;
	struct config_object_cli_data data = {
		.title = "Effective Profile",
		.object_type = config_object_type_profile,
	};

	switch(cmd) {
	case CLI_INIT:
		e->command = "stir_shaken show eprofiles";
		e->usage =
			"Usage: stir_shaken show eprofiles\n"
			"       Show all eprofiles for stir/shaken\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	container = eprofile_get_all();
	if (!container || ao2_container_count(container) == 0) {
		ast_cli(a->fd, "No stir/shaken eprofiles found\n");
		ao2_cleanup(container);
		return CLI_SUCCESS;
	}

	ao2_callback_data(container, OBJ_NODATA, config_object_cli_show, a, &data);
	ao2_ref(container, -1);

	return CLI_SUCCESS;
}

static struct ast_cli_entry stir_shaken_profile_cli[] = {
	AST_CLI_DEFINE(cli_profile_show, "Show stir/shaken profile by id"),
	AST_CLI_DEFINE(cli_profile_show_all, "Show all stir/shaken profiles"),
	AST_CLI_DEFINE(cli_eprofile_show, "Show stir/shaken eprofile by id"),
	AST_CLI_DEFINE(cli_eprofile_show_all, "Show all stir/shaken eprofiles"),
};

int profile_reload(void)
{
	struct ast_sorcery *sorcery = get_sorcery();
	ast_sorcery_force_reload_object(sorcery, CONFIG_TYPE);
	ast_sorcery_force_reload_object(sorcery, "eprofile");
	return 0;
}

int profile_unload(void)
{
	ast_cli_unregister_multiple(stir_shaken_profile_cli,
		ARRAY_LEN(stir_shaken_profile_cli));

	return 0;
}

int profile_load(void)
{
	struct ast_sorcery *sorcery = get_sorcery();
	enum ast_sorcery_apply_result apply_rc;

	/*
	 * eprofile MUST be registered first because profile needs it.
	 */
	apply_rc = ast_sorcery_apply_default(sorcery, "eprofile", "memory", NULL);
	if (apply_rc != AST_SORCERY_APPLY_SUCCESS) {
		abort();
	}
	if (ast_sorcery_internal_object_register(sorcery, "eprofile",
		profile_alloc, NULL, eprofile_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", "eprofile");
		return -1;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, "eprofile", "type", "", OPT_NOOP_T, 0, 0);
	enum_option_register(sorcery, "eprofile", endpoint_behavior, _nodoc);
	enum_option_register_ex(sorcery, "eprofile", unknown_tn_attest_level,
		unknown_tn_attest_level, attest_level,_nodoc);

	register_common_verification_fields(sorcery, profile_cfg, "eprofile", _nodoc);
	register_common_attestation_fields(sorcery, profile_cfg, "eprofile", _nodoc);

	/*
	 * Now we can do profile
	 */
	ast_sorcery_apply_default(sorcery, CONFIG_TYPE, "config", "stir_shaken.conf,criteria=type=profile");
	if (ast_sorcery_object_register(sorcery, CONFIG_TYPE, profile_alloc,
		NULL, profile_apply)) {
		ast_log(LOG_ERROR, "stir/shaken - failed to register '%s' sorcery object\n", CONFIG_TYPE);
		return -1;
	}

	ast_sorcery_object_field_register(sorcery, CONFIG_TYPE, "type", "", OPT_NOOP_T, 0, 0);
	enum_option_register(sorcery, CONFIG_TYPE, endpoint_behavior,);
	enum_option_register_ex(sorcery, CONFIG_TYPE, unknown_tn_attest_level,
		unknown_tn_attest_level, attest_level,);

	register_common_verification_fields(sorcery, profile_cfg, CONFIG_TYPE,);
	register_common_attestation_fields(sorcery, profile_cfg, CONFIG_TYPE,);

	ast_sorcery_load_object(sorcery, CONFIG_TYPE);
	ast_sorcery_load_object(sorcery, "eprofile");

	ast_cli_register_multiple(stir_shaken_profile_cli,
		ARRAY_LEN(stir_shaken_profile_cli));

	return 0;
}
