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
#include "asterisk/stringfields.h"

enum ast_stir_shaken_behavior {
	AST_STIR_SHAKEN_BEHAVIOR_UNKNOWN = -1,
	AST_STIR_SHAKEN_BEHAVIOR_OFF = 0,
	AST_STIR_SHAKEN_BEHAVIOR_ATTEST,
	AST_STIR_SHAKEN_BEHAVIOR_VERIFY,
	AST_STIR_SHAKEN_BEHAVIOR_ON,
};

enum ast_stir_shaken_behavior ast_stir_shaken_str_to_behavior(
	const char *behavior_str);

const char *ast_stir_shaken_behavior_to_str(
	enum ast_stir_shaken_behavior endpoint_behavior);

/*
 * The UNKNOWN and NOT_SET valued are needed, even for
 * simple booleans, because the value for the following
 * parameters can be set in more than one config object.
 * In these cases, a value of 0 may mean the value wasn't
 * set instead of a functional false.
 *
 * By making NOT_SET = a non-0/1 and making it the default value,
 * we can tell the difference.
 */

enum ast_stir_shaken_use_rfc9410_responses {
	AST_STIR_SHAKEN_VS_RFC9410_UNKNOWN = -1,
	AST_STIR_SHAKEN_VS_RFC9410_NO = 0,
	AST_STIR_SHAKEN_VS_RFC9410_YES,
	AST_STIR_SHAKEN_VS_RFC9410_NOT_SET,
};

enum ast_stir_shaken_use_rfc9410_responses
	ast_stir_shaken_str_to_use_rfc9410_responses(const char *use_rfc9410_str);

const char *ast_stir_shaken_use_rfc9410_responses_to_str(
	enum ast_stir_shaken_use_rfc9410_responses use_rfc9410);

enum ast_stir_shaken_check_tn_cert_public_url {
	AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_UNKNOWN = -1,
	AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NO = 0,
	AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_YES,
	AST_STIR_SHAKEN_AS_CHECK_TN_CERT_PUBLIC_URL_NOT_SET,
};

enum ast_stir_shaken_check_tn_cert_public_url
	ast_stir_shaken_str_to_check_tn_cert_public_url(const char *check_tn_cert_public_url_str);

const char *ast_stir_shaken_check_tn_cert_public_url_to_str(
	enum ast_stir_shaken_check_tn_cert_public_url check_tn_cert_public_url);


enum ast_stir_shaken_send_mky {
	AST_STIR_SHAKEN_AS_SEND_MKY_UNKNOWN = -1,
	AST_STIR_SHAKEN_AS_SEND_MKY_NO = 0,
	AST_STIR_SHAKEN_AS_SEND_MKY_YES,
	AST_STIR_SHAKEN_AS_SEND_MKY_NOT_SET,
};

enum ast_stir_shaken_send_mky
	ast_stir_shaken_str_to_send_mky(const char *send_mky_str);

const char *ast_stir_shaken_send_mky_to_str(
	enum ast_stir_shaken_send_mky send_mky);


enum ast_stir_shaken_attest_level {
	AST_STIR_SHAKEN_ATTEST_LEVEL_UNKNOWN = -1,
	AST_STIR_SHAKEN_ATTEST_LEVEL_A = 0,
	AST_STIR_SHAKEN_ATTEST_LEVEL_B,
	AST_STIR_SHAKEN_ATTEST_LEVEL_C,
	AST_STIR_SHAKEN_ATTEST_LEVEL_NOT_SET,
};

enum ast_stir_shaken_attest_level
	ast_stir_shaken_str_to_attest_level(const char *attest_str);

const char *ast_stir_shaken_attest_level_to_str(
	enum ast_stir_shaken_attest_level attest);

/*
 * enum ast_stir_shaken_failure_action is defined in
 * res_stir_shaken.h because res_pjsip_stir_shaken needs it.
 */

enum ast_stir_shaken_failure_action
	ast_stir_shaken_str_to_failure_action(const char *action_str);

const char *ast_stir_shaken_failure_action_to_str(
	enum ast_stir_shaken_failure_action action);

#define config_enum_to_str(__struct, __lc_param) \
static int __lc_param ## _to_str(const void *obj, const intptr_t *args, char **buf) \
{ \
	const struct __struct *cfg = obj; \
	*buf = ast_strdup(ast_stir_shaken_ ## __lc_param ## _to_str(cfg->__lc_param)); \
	return *buf ? 0 : -1; \
}

#define config_enum_handler(__struct, __lc_param, __unknown) \
static int __lc_param ## _handler(const struct aco_option *opt, struct ast_variable *var, void *obj) \
{ \
	struct __struct *cfg = obj; \
	cfg->__lc_param = ast_stir_shaken_str_to_ ## __lc_param (var->value); \
	if (cfg->__lc_param == __unknown) { \
		ast_log(LOG_WARNING, "Unknown value '%s' specified for %s\n", \
			var->value, var->name); \
		return -1; \
	} \
	return 0; \
}

#define EFFECTIVE_ENUM(__obj1, __obj2, __field, __stem, __default) \
	( __obj1->__field != ( __stem ## _ ## NOT_SET ) ? __obj1->__field : \
		(__obj2->__field != __stem ## _ ## NOT_SET ? \
			__obj2->__field : __default ))

#define EFFECTIVE_ENUM_BOOL(__obj1, __obj2, __field, __stem, __default) \
	(( __obj1->__field != ( __stem ## _ ## NOT_SET ) ? __obj1->__field : \
		(__obj2->__field != __stem ## _ ## NOT_SET ? \
			__obj2->__field : __default )) == __stem ## _ ## YES)

/*!
 * \brief Attestation Service configuration for stir/shaken
 */
struct ss_as_cfg {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(private_key_file);
		AST_STRING_FIELD(public_cert_url);
	);
	int global_disable;
	enum ast_stir_shaken_check_tn_cert_public_url check_tn_cert_public_url;
	EVP_PKEY *private_key;
	unsigned char *raw_key;
	size_t raw_key_length;
	enum ast_stir_shaken_attest_level attest_level;
	enum ast_stir_shaken_send_mky send_mky;
};

struct ss_as_cfg *ss_get_as_cfg(void);
int ss_as_is_config_loaded(void);
int ss_as_config_load(void);
int ss_as_config_reload(void);
int ss_as_config_unload(void);

/*!
 * \brief Profile configuration for stir/shaken
 */
struct ss_profile {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(private_key_file);
		AST_STRING_FIELD(public_cert_url);
		AST_STRING_FIELD(ca_file);
		AST_STRING_FIELD(ca_path);
		AST_STRING_FIELD(crl_file);
		AST_STRING_FIELD(crl_path);
		AST_STRING_FIELD(cert_cache_dir);
	);
	EVP_PKEY *private_key;
	unsigned char *raw_key;
	size_t raw_key_length;
	unsigned int curl_timeout;
	unsigned int max_iat_age;
	unsigned int max_date_header_age;
	unsigned int max_cache_entry_age;
	unsigned int max_cache_size;
	enum ast_stir_shaken_check_tn_cert_public_url check_tn_cert_public_url;
	enum ast_stir_shaken_attest_level attest_level;
	enum ast_stir_shaken_behavior behavior;
	enum ast_stir_shaken_failure_action failure_action;
	enum ast_stir_shaken_use_rfc9410_responses use_rfc9410_responses;
	enum ast_stir_shaken_send_mky send_mky;
	struct ast_acl_list *acl;
};

struct ss_profile *ss_get_profile(const char *id);
int ss_profile_reload(void);
int ss_profile_load(void);
int ss_profile_unload(void);

/*!
 * \brief TN configuration for stir/shaken
 */
struct ss_tn {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(private_key_file);
		AST_STRING_FIELD(public_cert_url);
	);
	EVP_PKEY *private_key;
	unsigned char *raw_key;
	size_t raw_key_length;
	enum ast_stir_shaken_attest_level attest_level;
};

struct ss_tn *ss_tn_get(const char *tn);
int ss_tn_load(void);
int ss_tn_reload(void);
int ss_tn_unload(void);

/*!
 * \brief Verification Service configuration for stir/shaken
 */
struct ss_vs_cfg {
	SORCERY_OBJECT(details);
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(ca_file);
		AST_STRING_FIELD(ca_path);
		AST_STRING_FIELD(crl_file);
		AST_STRING_FIELD(crl_path);
		AST_STRING_FIELD(cert_cache_dir);
	);
	int global_disable;
	int load_system_certs;
	unsigned int curl_timeout;
	unsigned int max_iat_age;
	unsigned int max_date_header_age;
	unsigned int max_cache_entry_age;
	unsigned int max_cache_size;
	enum ast_stir_shaken_failure_action failure_action;
	enum ast_stir_shaken_use_rfc9410_responses use_rfc9410_responses;
};

struct ss_vs_cfg *ss_get_vs_cfg(void);
int ss_vs_is_config_loaded(void);
int ss_vs_config_load(void);
int ss_vs_config_reload(void);
int ss_vs_config_unload(void);

int ss_config_load(void);
int ss_config_unload(void);
int ss_config_reload(void);

#endif /* COMMON_CONFIG_H_ */
