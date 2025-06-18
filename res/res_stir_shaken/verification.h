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
		AST_STRING_FIELD(cert_spc);
		AST_STRING_FIELD(cert_cn);
	);
	struct profile_cfg *eprofile;
	struct ast_channel *chan;
	time_t date_hdr_time;
	time_t validity_check_time;
	long raw_key_len;
	unsigned char *raw_key;
	char expiration[32];
	X509 *xcert;
	STACK_OF(X509) *cert_chain;
	enum ast_stir_shaken_vs_response_code failure_reason;
};

/*!
 * \brief Load the stir/shaken verification service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int vs_load(void);

/*!
 * \brief Reload the stir/shaken verification service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int vs_reload(void);

/*!
 * \brief Unload the stir/shaken verification service
 *
 * \retval 0 on success
 * \retval -1 on error
 */
int vs_unload(void);

#endif /* VERIFICATION_H_ */
