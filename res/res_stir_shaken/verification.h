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

#ifndef VERIFICATION_H_
#define VERIFICATION_H_

#include "common_config.h"

struct ast_stir_shaken_vs_ctx {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(tag);
		AST_STRING_FIELD(caller_id);
		AST_STRING_FIELD(orig_tn);
		AST_STRING_FIELD(identity_hdr);
		AST_STRING_FIELD(date_hdr);
		AST_STRING_FIELD(filename);
		AST_STRING_FIELD(public_url);
		AST_STRING_FIELD(hash);
		AST_STRING_FIELD(hash_family);
		AST_STRING_FIELD(url_family);
		AST_STRING_FIELD(attestation);
	);
	struct ss_vs_cfg *vs;
	struct ss_profile *profile;
	struct ast_channel *chan;
	const char *ca_file;
	const char *ca_path;
	const char *crl_file;
	const char *crl_path;
	const char *cert_cache_dir;
	enum ast_stir_shaken_behavior behavior;
	enum ast_stir_shaken_failure_action failure_action;
	unsigned int curl_timeout;
	unsigned int max_iat_age;
	unsigned int max_date_header_age;
	unsigned int max_cache_entry_age;
	unsigned int max_cache_size;
	int use_rfc9410_responses;
	time_t date_hdr_time;
	time_t validity_check_time;
	const struct ast_acl_list *acl;
	long raw_key_len;
	unsigned char *raw_key;
	char expiration[32];
	X509 *xcert;
	enum ast_stir_shaken_vs_response_code failure_reason;
	char *failure_msg;
};

/*!
 * \brief Load the stir/shaken verification service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int ss_vs_load(void);

/*!
 * \brief Reload the stir/shaken verification service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int ss_vs_reload(void);

/*!
 * \brief Unload the stir/shaken verification service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int ss_vs_unload(void);

#endif /* VERIFICATION_H_ */
