/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2023, Sangoma Technologies Corporation
 *
 * George Joseph <gjoseph@sangoma.com>
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

#ifndef COMMON_CONFIG_H_
#define COMMON_CONFIG_H_

#include <openssl/evp.h>

#include "asterisk.h"
#include "asterisk/paths.h"
#include "asterisk/sorcery.h"
#include "asterisk/stringfields.h"

/*!
 * \brief Boolean field to/from string prototype generator
 *
 * Most of the boolean fields that appear in the verification and
 * attestation objects can be ovrridden in the profile object;
 * "use_rfc9410_responses" for instance. If they were registered as
 * normal YESNO types, we couldn't tell if a "0" value in the profile
 * object meant the user set it to "no" to override a value of "yes"
 * in the verification object, or it just defaulted to "0".  By making
 * the _NOT_SET enum a non-0/1 and making it the default value, we can
 * tell the difference. The _UNKNOWN enum gets set if the string value
 * provided to the _from_str function wasn't recognized as one of the
 * values acceptable to ast_true() or ast_false().
 *
 * The result of calling the generator for a field will look like:
 *
 \code
 enum use_rfc9410_responses_enum {
	use_rfc9410_responses_UNKNOWN = -1,
	use_rfc9410_responses_NO = 0,
	use_rfc9410_responses_YES,
	use_rfc9410_responses_NOT_SET,
};
enum use_rfc9410_responses_enum
	use_rfc9410_responses_from_str(const char *value);
const char *use_rfc9410_responses_to_str(enum use_rfc9410_responses_enum value);
\endcode

Most of the macros that follow depend on enum values formatted
as <param_name>_SOMETHING and their defaults as DEFAULT_<param_name>.
 */
#define generate_bool_string_prototypes(param_name) \
enum param_name ## _enum { \
	param_name ## _UNKNOWN = -1, \
	param_name ## _NO = 0, \
	param_name ## _YES, \
	param_name ## _NOT_SET, \
}; \
enum param_name ## _enum \
	param_name ## _from_str(const char *value); \
const char *param_name ## _to_str(enum param_name ## _enum value);

/*
 * Run the generators
 */
generate_bool_string_prototypes(use_rfc9410_responses);

generate_bool_string_prototypes(relax_x5u_port_scheme_restrictions);

generate_bool_string_prototypes(relax_x5u_path_restrictions);

generate_bool_string_prototypes(load_system_certs);

generate_bool_string_prototypes(check_tn_cert_public_url);

generate_bool_string_prototypes(send_mky);

/*!
 * \brief Enum field to/from string prototype generator
 *
 * This operates like the bool generator except you supply
 * a list of the enum values.  The first one MUST be
 * param_name_UNKNOWN with a value of -1 and the rest running
 * sequentially with the last being param_name_NOT_SET.
 */
#define generate_enum_string_prototypes(param_name, ...) \
enum param_name ## _enum { \
	__VA_ARGS__ \
}; \
enum param_name ## _enum \
	param_name ## _from_str(const char *value); \
const char *param_name ## _to_str(enum param_name ## _enum value);

generate_enum_string_prototypes(endpoint_behavior,
	endpoint_behavior_UNKNOWN = -1,
	endpoint_behavior_OFF = 0,
	endpoint_behavior_ATTEST,
	endpoint_behavior_VERIFY,
	endpoint_behavior_ON,
	endpoint_behavior_NOT_SET
);

generate_enum_string_prototypes(attest_level,
	attest_level_UNKNOWN = -1,
	attest_level_NOT_SET = 0,
	attest_level_A,
	attest_level_B,
	attest_level_C,
);

/*
 * enum stir_shaken_failure_action is defined in
 * res_stir_shaken.h because res_pjsip_stir_shaken needs it
 * we we need to just declare the function prototypes.
 */

enum stir_shaken_failure_action_enum
	stir_shaken_failure_action_from_str(const char *action_str);

const char *stir_shaken_failure_action_to_str(
	enum stir_shaken_failure_action_enum action);

/*!
 * \brief Enum sorcery handler generator
 *
 * These macros can create the two functions needed to
 * register an enum field with sorcery as long as there
 * are _to_str and _from_str functions defined elsewhere.
 *
 */
#define generate_sorcery_enum_to_str(__struct, __substruct, __lc_param) \
static int sorcery_ ## __lc_param ## _to_str(const void *obj, const intptr_t *args, char **buf) \
{ \
	const struct __struct *cfg = obj; \
	*buf = ast_strdup(__lc_param ## _to_str(cfg->__substruct __lc_param)); \
	return *buf ? 0 : -1; \
}

#define generate_sorcery_enum_from_str_ex(__struct, __substruct, __lc_param, __unknown) \
static int sorcery_ ## __lc_param ## _from_str(const struct aco_option *opt, struct ast_variable *var, void *obj) \
{ \
	struct __struct *cfg = obj; \
	cfg->__substruct __lc_param = __lc_param ## _from_str (var->value); \
	if (cfg->__substruct __lc_param == __unknown) { \
		ast_log(LOG_WARNING, "Unknown value '%s' specified for %s\n", \
			var->value, var->name); \
		return -1; \
	} \
	return 0; \
}

#define generate_sorcery_enum_from_str(__struct, __substruct, __lc_param, __unknown) \
	generate_sorcery_enum_from_str_ex(__struct, __substruct, __lc_param, __lc_param ## _ ## __unknown) \


#define generate_sorcery_acl_to_str(__struct, __lc_param) \
static int sorcery_acl_to_str(const void *obj, const intptr_t *args, char **buf) \
{ \
	const struct __struct *cfg = obj; \
	struct ast_acl *first_acl; \
	if (!ast_acl_list_is_empty(cfg->vcfg_common.acl)) { \
		AST_LIST_LOCK(cfg->vcfg_common.acl); \
		first_acl = AST_LIST_FIRST(cfg->vcfg_common.acl); \
		if (ast_strlen_zero(first_acl->name)) { \
			*buf = "deny/permit"; \
		} else { \
			*buf = first_acl->name; \
		} \
		AST_LIST_UNLOCK(cfg->vcfg_common.acl); \
	} \
	*buf = ast_strdup(*buf); \
	return 0; \
}

#define generate_sorcery_acl_from_str(__struct, __lc_param, __unknown) \
static int sorcery_acl_from_str(const struct aco_option *opt, struct ast_variable *var, void *obj) \
{ \
	struct __struct *cfg = obj; \
	int error = 0; \
	int ignore; \
	const char *name = var->name + strlen("x5u_"); \
	if (ast_strlen_zero(var->value)) { \
		return 0; \
	} \
	ast_append_acl(name, var->value, &cfg->vcfg_common.acl, &error, &ignore); \
	return error; \
}

struct ast_acl_list *get_default_acl_list(void);

#define EFFECTIVE_ENUM(__enum1, __enum2,  __field, __default) \
	( __enum1 != ( __field ## _ ## NOT_SET ) ? __enum1 : \
		(__enum2 != __field ## _ ## NOT_SET ? \
			__enum2 : __default ))

#define EFFECTIVE_ENUM_BOOL(__enum1, __enum2, __field, __default) \
	(( __enum1 != ( __field ## _ ## NOT_SET ) ? __enum1 : \
		(__enum2 != __field ## _ ## NOT_SET ? \
			__enum2 : __field ## _ ## __default )) == __field ## _ ## YES)

#define ENUM_BOOL(__enum1, __field) \
	(__enum1 == ( __field ## _ ## YES ))

/*!
 * \brief Common config copy utilities
 *
 * These macros are designed to be called from as_copy_cfg_common
 * and vs_copy_cfg_common only.  They'll only copy a field if the
 * field contains a vaild value.  Thus a NOT_SET value in the source
 * won't override a pre-existing good value in the dest.  A good
 * value in the source WILL overwrite a good value in the dest.
 *
 */
#define cfg_stringfield_copy(__cfg_dst, __cfg_src, __field) \
({ \
	int __res = 0; \
	if (!ast_strlen_zero(__cfg_src->__field)) { \
		__res = ast_string_field_set(__cfg_dst, __field, __cfg_src->__field); \
	} \
	__res; \
})

/*!
 * \brief cfg_copy_wrapper
 *
 * Invoke cfg_stringfield_copy and cause the calling runction to
 * return a -1 of the copy fails.
 */
#define cfg_sf_copy_wrapper(id, __cfg_dst, __cfg_src, __field) \
{ \
	int rc = cfg_stringfield_copy(__cfg_dst, __cfg_src, __field); \
	if (rc != 0) { \
		ast_log(LOG_ERROR, "%s: Unable to copy field %s from %s to %s\n", \
			id, #__field, #__cfg_src, #__cfg_dst); \
		return -1; \
	} \
}

/*!
 * \brief cfg_uint_copy
 *
 * Copy a uint from the source to the dest only if the source > 0.
 * For stir-shaken, 0 isn't a valid value for any uint fields.
 */
#define cfg_uint_copy(__cfg_dst, __cfg_src, __field) \
({ \
	if (__cfg_src->__field > 0) { \
		__cfg_dst->__field = __cfg_src->__field; \
	} \
})

/*!
 * \brief cfg_enum_copy
 *
 * Copy an enum from the source to the dest only if the source is
 * neither NOT_SET nor UNKNOWN
 */
#define cfg_enum_copy(__cfg_dst, __cfg_src, __field) \
({ \
	if (__cfg_src->__field != __field ## _NOT_SET \
		&& __cfg_src->__field != __field ## _UNKNOWN) { \
		__cfg_dst->__field = __cfg_src->__field; \
	} \
})

/*!
 * \brief Attestation Service configuration for stir/shaken
 *
 * The common structure also appears in profile_cfg.
 */
struct attestation_cfg_common {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(private_key_file);
		AST_STRING_FIELD(public_cert_url);
	);
	enum attest_level_enum attest_level;
	enum check_tn_cert_public_url_enum check_tn_cert_public_url;
	enum send_mky_enum send_mky;
	unsigned char *raw_key;
	size_t raw_key_length;
};

#define generate_acfg_common_sorcery_handlers(object) \
	generate_sorcery_enum_from_str(object, acfg_common., check_tn_cert_public_url, UNKNOWN); \
	generate_sorcery_enum_to_str(object, acfg_common., check_tn_cert_public_url); \
	generate_sorcery_enum_from_str(object, acfg_common., send_mky, UNKNOWN); \
	generate_sorcery_enum_to_str(object, acfg_common., send_mky); \
	generate_sorcery_enum_from_str(object, acfg_common., attest_level, UNKNOWN); \
	generate_sorcery_enum_to_str(object, acfg_common., attest_level);

int as_check_common_config(const char *id,
	struct attestation_cfg_common *acfg_common);

int as_copy_cfg_common(const char *id, struct attestation_cfg_common *cfg_dst,
	struct attestation_cfg_common *cfg_src);

void acfg_cleanup(struct attestation_cfg_common *cfg);

struct attestation_cfg {
	SORCERY_OBJECT(details);
	/*
	 * We need an empty AST_DECLARE_STRING_FIELDS() here
	 * because when STRFLDSET is used with sorcery, the
	 * memory for all sub-structures that have stringfields
	 * is allocated from the parent's stringfield pool.
	 */
	AST_DECLARE_STRING_FIELDS();
	struct attestation_cfg_common acfg_common;
	int global_disable;
};

struct attestation_cfg *as_get_cfg(void);
int as_is_config_loaded(void);
int as_config_load(void);
int as_config_reload(void);
int as_config_unload(void);

/*!
 * \brief Verification Service configuration for stir/shaken
 *
 * The common structure also appears in profile_cfg.
 */
struct verification_cfg_common {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(ca_file);
		AST_STRING_FIELD(ca_path);
		AST_STRING_FIELD(crl_file);
		AST_STRING_FIELD(crl_path);
		AST_STRING_FIELD(untrusted_cert_file);
		AST_STRING_FIELD(untrusted_cert_path);
		AST_STRING_FIELD(cert_cache_dir);
	);
	unsigned int curl_timeout;
	unsigned int max_iat_age;
	unsigned int max_date_header_age;
	unsigned int max_cache_entry_age;
	unsigned int max_cache_size;
	enum stir_shaken_failure_action_enum
		stir_shaken_failure_action;
	enum use_rfc9410_responses_enum use_rfc9410_responses;
	enum relax_x5u_port_scheme_restrictions_enum
		relax_x5u_port_scheme_restrictions;
	enum relax_x5u_path_restrictions_enum
		relax_x5u_path_restrictions;
	enum load_system_certs_enum load_system_certs;

	struct ast_acl_list *acl;
	struct crypto_cert_store *tcs;
};

#define generate_vcfg_common_sorcery_handlers(object) \
	generate_sorcery_enum_from_str(object, vcfg_common.,use_rfc9410_responses, UNKNOWN); \
	generate_sorcery_enum_to_str(object, vcfg_common.,use_rfc9410_responses); \
	generate_sorcery_enum_from_str(object, vcfg_common.,stir_shaken_failure_action, UNKNOWN); \
	generate_sorcery_enum_to_str(object, vcfg_common.,stir_shaken_failure_action); \
	generate_sorcery_enum_from_str(object, vcfg_common.,relax_x5u_port_scheme_restrictions, UNKNOWN); \
	generate_sorcery_enum_to_str(object, vcfg_common.,relax_x5u_port_scheme_restrictions); \
	generate_sorcery_enum_from_str(object, vcfg_common.,relax_x5u_path_restrictions, UNKNOWN); \
	generate_sorcery_enum_to_str(object, vcfg_common.,relax_x5u_path_restrictions); \
	generate_sorcery_enum_from_str(object, vcfg_common.,load_system_certs, UNKNOWN); \
	generate_sorcery_enum_to_str(object, vcfg_common.,load_system_certs); \
	generate_sorcery_acl_from_str(object, acl, NULL); \
	generate_sorcery_acl_to_str(object, acl);

int vs_check_common_config(const char *id,
	struct verification_cfg_common *vcfg_common);

int vs_copy_cfg_common(const char *id, struct verification_cfg_common *cfg_dst,
	struct verification_cfg_common *cfg_src);

void vcfg_cleanup(struct verification_cfg_common *cfg);

struct verification_cfg {
	SORCERY_OBJECT(details);
	/*
	 * We need an empty AST_DECLARE_STRING_FIELDS() here
	 * because when STRFLDSET is used with sorcery, the
	 * memory for all sub-structures that have stringfields
	 * is allocated from the parent's stringfield pool.
	 */
	AST_DECLARE_STRING_FIELDS();
	struct verification_cfg_common vcfg_common;
	int global_disable;
};

struct verification_cfg *vs_get_cfg(void);
int vs_is_config_loaded(void);
int vs_config_load(void);
int vs_config_reload(void);
int vs_config_unload(void);

/*!
 * \brief Profile configuration for stir/shaken
 */
struct profile_cfg {
	SORCERY_OBJECT(details);
	/*
	 * We need an empty AST_DECLARE_STRING_FIELDS() here
	 * because when STRFLDSET is used with sorcery, the
	 * memory for all sub-structures that have stringfields
	 * is allocated from the parent's stringfield pool.
	 */
	AST_DECLARE_STRING_FIELDS();
	struct attestation_cfg_common acfg_common;
	struct verification_cfg_common vcfg_common;
	enum endpoint_behavior_enum endpoint_behavior;
	struct profile_cfg *eprofile;
};

struct profile_cfg *profile_get_cfg(const char *id);
struct ao2_container *profile_get_all(void);
struct profile_cfg *eprofile_get_cfg(const char *id);
struct ao2_container *eprofile_get_all(void);
int profile_load(void);
int profile_reload(void);
int profile_unload(void);

#define PROFILE_ALLOW_ATTEST(__profile) \
	(__profile->endpoint_behavior == endpoint_behavior_ON || \
		__profile->endpoint_behavior == endpoint_behavior_ATTEST)

#define PROFILE_ALLOW_VERIFY(__profile) \
	(__profile->endpoint_behavior == endpoint_behavior_ON || \
		__profile->endpoint_behavior == endpoint_behavior_VERIFY)

/*!
 * \brief TN configuration for stir/shaken
 *
 * TN-specific attestation_cfg.
 */

struct tn_cfg {
	SORCERY_OBJECT(details);
	/*
	 * We need an empty AST_DECLARE_STRING_FIELDS() here
	 * because when STRFLDSET is used with sorcery, the
	 * memory for all sub-structures that have stringfields
	 * is allocated from the parent's stringfield pool.
	 */
	AST_DECLARE_STRING_FIELDS();
	struct attestation_cfg_common acfg_common;
};

struct tn_cfg *tn_get_cfg(const char *tn);
struct tn_cfg *tn_get_etn(const char *tn,
	struct profile_cfg *eprofile);
int tn_config_load(void);
int tn_config_reload(void);
int tn_config_unload(void);

/*!
 * \brief Sorcery fields register helpers
 *
 * Most of the fields on attestation_cfg and verification_cfg are also
 * in profile_cfg.  To prevent having to maintain duplicate sets of
 * sorcery register statements, we can do this once here and call
 * register_common_verification_fields() from both profile_config and
 * verification_config and call register_common_attestation_fields()
 * from profile_cfg and attestation_config.
 *
 * Most of the fields in question are in sub-structures like
 * verification_cfg.vcfg_common which is why there are separate name
 * and field parameters.  For verification_cfg.vcfg_common.ca_file
 * for instance, name would be ca_file and field would be
 * vcfg_common.ca_file.
 *
 *\note These macros depend on default values being defined
 * in the 4 _config.c files as DEFAULT_<field_name>.
 *
 */
#define stringfield_option_register(sorcery, CONFIG_TYPE, object, name, field, nodoc) \
	ast_sorcery_object_field_register ## nodoc(sorcery, CONFIG_TYPE, #name, \
		DEFAULT_ ## name, OPT_STRINGFIELD_T, 0, \
		STRFLDSET(struct object, field))

#define uint_option_register(sorcery, CONFIG_TYPE, object, name, field, nodoc) \
	ast_sorcery_object_field_register ## nodoc(sorcery, CONFIG_TYPE, #name, \
		__stringify(DEFAULT_ ## name), OPT_UINT_T, 0, \
		FLDSET(struct object, field))

#define enum_option_register_ex(sorcery, CONFIG_TYPE, name, field, nodoc) \
	ast_sorcery_object_field_register_custom ## nodoc(sorcery, CONFIG_TYPE, \
		#name, field ## _to_str(DEFAULT_ ## field), \
		sorcery_ ## field ## _from_str, sorcery_ ## field ## _to_str, NULL, 0, 0)

#define enum_option_register(sorcery, CONFIG_TYPE, name, nodoc) \
	enum_option_register_ex(sorcery, CONFIG_TYPE, name, name, nodoc)

#define register_common_verification_fields(sorcery, object, CONFIG_TYPE, nodoc) \
({ \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, ca_file, vcfg_common.ca_file, nodoc); \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, ca_path, vcfg_common.ca_path, nodoc); \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, crl_file, vcfg_common.crl_file, nodoc); \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, crl_path, vcfg_common.crl_path, nodoc); \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, untrusted_cert_file, vcfg_common.untrusted_cert_file, nodoc); \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, untrusted_cert_path, vcfg_common.untrusted_cert_path, nodoc); \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, cert_cache_dir, vcfg_common.cert_cache_dir, nodoc); \
\
	uint_option_register(sorcery, CONFIG_TYPE, object, curl_timeout, vcfg_common.curl_timeout, nodoc);\
	uint_option_register(sorcery, CONFIG_TYPE, object, max_iat_age, vcfg_common.max_iat_age, nodoc);\
	uint_option_register(sorcery, CONFIG_TYPE, object, max_date_header_age, vcfg_common.max_date_header_age, nodoc);\
	uint_option_register(sorcery, CONFIG_TYPE, object, max_cache_entry_age, vcfg_common.max_cache_entry_age, nodoc);\
	uint_option_register(sorcery, CONFIG_TYPE, object, max_cache_size, vcfg_common.max_cache_size, nodoc);\
\
	enum_option_register_ex(sorcery, CONFIG_TYPE, failure_action, stir_shaken_failure_action, nodoc); \
	enum_option_register(sorcery, CONFIG_TYPE, use_rfc9410_responses, nodoc); \
	enum_option_register(sorcery, CONFIG_TYPE, \
		relax_x5u_port_scheme_restrictions, nodoc); \
	enum_option_register(sorcery, CONFIG_TYPE, \
		relax_x5u_path_restrictions, nodoc); \
		enum_option_register(sorcery, CONFIG_TYPE, \
			load_system_certs, nodoc); \
\
	ast_sorcery_object_field_register_custom ## nodoc(sorcery, CONFIG_TYPE, "x5u_deny", "", sorcery_acl_from_str, NULL, NULL, 0, 0); \
	ast_sorcery_object_field_register_custom ## nodoc(sorcery, CONFIG_TYPE, "x5u_permit", "", sorcery_acl_from_str, NULL, NULL, 0, 0); \
	ast_sorcery_object_field_register_custom ## nodoc(sorcery, CONFIG_TYPE, "x5u_acl", "", sorcery_acl_from_str, sorcery_acl_to_str, NULL, 0, 0); \
})

#define register_common_attestation_fields(sorcery, object, CONFIG_TYPE, nodoc) \
({ \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, private_key_file, acfg_common.private_key_file, nodoc); \
	stringfield_option_register(sorcery, CONFIG_TYPE, object, public_cert_url, acfg_common.public_cert_url, nodoc); \
	enum_option_register(sorcery, CONFIG_TYPE, attest_level, nodoc); \
	enum_option_register(sorcery, CONFIG_TYPE, check_tn_cert_public_url, nodoc); \
	enum_option_register(sorcery, CONFIG_TYPE, send_mky, nodoc); \
})

int common_config_load(void);
int common_config_unload(void);
int common_config_reload(void);

enum config_object_type {
	config_object_type_attestation = 0,
	config_object_type_verification,
	config_object_type_profile,
	config_object_type_tn,
};

struct config_object_cli_data {
	const char *title;
	enum config_object_type object_type;
};

/*!
 * \brief Output configuration settings to the Asterisk CLI
 *
 * \param obj A sorcery object containing configuration data
 * \param arg Asterisk CLI argument object
 * \param flags ao2 container flags
 *
 * \retval 0
 */
int config_object_cli_show(void *obj, void *arg, void *data, int flags);

/*!
 * \brief Tab completion for name matching with STIR/SHAKEN CLI commands
 *
 * \param word The word to tab complete on
 * \param container The sorcery container to iterate through
 *
 * \retval The tab completion options
 */
char *config_object_tab_complete_name(const char *word, struct ao2_container *container);

/*!
 * \brief Canonicalize a TN
 *
 * \param tn TN to canonicalize
 * \param dest_tn Pointer to destination buffer to receive the new TN
 *
 * \retval dest_tn or NULL on failure
 */
char *canonicalize_tn(const char *tn, char *dest_tn);

/*!
 * \brief Canonicalize a TN into nre buffer
 *
 * \param tn TN to canonicalize
 *
 * \retval dest_tn (which must be freed with ast_free) or NULL on failure
 */
char *canonicalize_tn_alloc(const char *tn);

#endif /* COMMON_CONFIG_H_ */
