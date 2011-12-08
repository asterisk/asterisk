/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010 FIXME
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

/*! \file
 * \brief SRTP resource
 */

#ifndef _ASTERISK_RES_SRTP_H
#define _ASTERISK_RES_SRTP_H

struct ast_srtp;
struct ast_srtp_policy;
struct ast_rtp_instance;

struct ast_srtp_cb {
	int (*no_ctx)(struct ast_rtp_instance *rtp, unsigned long ssrc, void *data);
};

struct ast_srtp_res {
	int (*create)(struct ast_srtp **srtp, struct ast_rtp_instance *rtp, struct ast_srtp_policy *policy);
	void (*destroy)(struct ast_srtp *srtp);
	int (*add_stream)(struct ast_srtp *srtp, struct ast_srtp_policy *policy);
	int (*change_source)(struct ast_srtp *srtp, unsigned int from_ssrc, unsigned int to_ssrc);
	void (*set_cb)(struct ast_srtp *srtp, const struct ast_srtp_cb *cb, void *data);
	int (*unprotect)(struct ast_srtp *srtp, void *buf, int *size, int rtcp);
	int (*protect)(struct ast_srtp *srtp, void **buf, int *size, int rtcp);
	int (*get_random)(unsigned char *key, size_t len);
};

/* Crypto suites */
enum ast_srtp_suite {
	AST_AES_CM_128_HMAC_SHA1_80 = 1,
	AST_AES_CM_128_HMAC_SHA1_32 = 2,
	AST_F8_128_HMAC_SHA1_80     = 3
};

struct ast_srtp_policy_res {
	struct ast_srtp_policy *(*alloc)(void);
	void (*destroy)(struct ast_srtp_policy *policy);
	int (*set_suite)(struct ast_srtp_policy *policy, enum ast_srtp_suite suite);
	int (*set_master_key)(struct ast_srtp_policy *policy, const unsigned char *key, size_t key_len, const unsigned char *salt, size_t salt_len);
	void (*set_ssrc)(struct ast_srtp_policy *policy, unsigned long ssrc, int inbound);
};

#endif /* _ASTERISK_RES_SRTP_H */
