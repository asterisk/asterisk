/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006 - 2007, Mikael Magnusson
 *
 * Mikael Magnusson <mikma@users.sourceforge.net>
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
 *
 * \brief SRTP and SDP Security descriptions
 *
 * Specified in RFC 3711, 6188, 7714, and 4568
 *
 * \author Mikael Magnusson <mikma@users.sourceforge.net>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <math.h>                       /* for pow */
#include <srtp/srtp.h>                  /* for SRTP_MAX_KEY_LEN, etc */

#include "asterisk/linkedlists.h"       /* for AST_LIST_NEXT, etc */
#include "asterisk/logger.h"            /* for ast_log, LOG_ERROR, etc */
#include "asterisk/rtp_engine.h"        /* for ast_rtp_engine_dtls, etc */
#include "asterisk/sdp_srtp.h"          /* for ast_sdp_srtp, etc */
#include "asterisk/strings.h"           /* for ast_strlen_zero */
#include "asterisk/utils.h"             /* for ast_set_flag, ast_test_flag, etc */

extern struct ast_srtp_res *res_srtp;
extern struct ast_srtp_policy_res *res_srtp_policy;

struct ast_sdp_srtp *ast_sdp_srtp_alloc(void)
{
	if (!ast_rtp_engine_srtp_is_registered()) {
	       ast_debug(1, "No SRTP module loaded, can't setup SRTP session.\n");
	       return NULL;
	}

	return ast_calloc(1, sizeof(struct ast_sdp_srtp));
}

void ast_sdp_srtp_destroy(struct ast_sdp_srtp *srtp)
{
	struct ast_sdp_srtp *next;

	for (next = AST_LIST_NEXT(srtp, sdp_srtp_list);
	     srtp;
	     srtp = next, next = srtp ? AST_LIST_NEXT(srtp, sdp_srtp_list) : NULL) {
		if (srtp->crypto) {
			ast_sdp_crypto_destroy(srtp->crypto);
		}
		srtp->crypto = NULL;
		ast_free(srtp);
	}
}

struct ast_sdp_crypto {
	char *a_crypto;
	unsigned char local_key[SRTP_MAX_KEY_LEN];
	int tag;
	char local_key64[((SRTP_MAX_KEY_LEN) * 8 + 5) / 6 + 1];
	unsigned char remote_key[SRTP_MAX_KEY_LEN];
	int key_len;
};

struct ast_sdp_crypto *sdp_crypto_alloc(const int key_len);
struct ast_sdp_crypto *int_crypto_alloc(struct ast_sdp_crypto *p, const int key_len);
static int set_crypto_policy(struct ast_srtp_policy *policy, int suite_val, const unsigned char *master_key, int key_len, unsigned long ssrc, int inbound);

void ast_sdp_crypto_destroy(struct ast_sdp_crypto *crypto)
{
	ast_free(crypto->a_crypto);
	crypto->a_crypto = NULL;
	ast_free(crypto);
}

struct ast_sdp_crypto *int_crypto_alloc(struct ast_sdp_crypto *p, const int key_len)
{
	unsigned char remote_key[key_len];

	if (res_srtp->get_random(p->local_key, key_len) < 0) {
		return NULL;
	}

	ast_base64encode(p->local_key64, p->local_key, key_len, sizeof(p->local_key64));

	p->key_len = ast_base64decode(remote_key, p->local_key64, sizeof(remote_key));

	if (p->key_len != key_len) {
		ast_log(LOG_ERROR, "base64 encode/decode bad len %d != %d\n", p->key_len, key_len);
		return NULL;
	}

	if (memcmp(remote_key, p->local_key, p->key_len)) {
		ast_log(LOG_ERROR, "base64 encode/decode bad key\n");
		return NULL;
	}

	ast_debug(1 , "local_key64 %s len %zu\n", p->local_key64, strlen(p->local_key64));

	return p;
}

struct ast_sdp_crypto *sdp_crypto_alloc(const int key_len)
{
	struct ast_sdp_crypto *p, *result;

	if (!ast_rtp_engine_srtp_is_registered()) {
		return NULL;
	}

	if (!(p = ast_calloc(1, sizeof(*p)))) {
		return NULL;
	}
	p->tag = 1;

	/* default is a key which uses AST_AES_CM_128_HMAC_SHA1_xx */
	result = int_crypto_alloc(p, key_len);
	if (!result) {
		ast_sdp_crypto_destroy(p);
	}

	return result;
}

struct ast_sdp_crypto *ast_sdp_crypto_alloc(void)
{
	return sdp_crypto_alloc(SRTP_MASTER_KEY_LEN);
}

static int set_crypto_policy(struct ast_srtp_policy *policy, int suite_val, const unsigned char *master_key, int key_len, unsigned long ssrc, int inbound)
{
	if (!ast_rtp_engine_srtp_is_registered()) {
		return -1;
	}

	if (res_srtp_policy->set_master_key(policy, master_key, key_len, NULL, 0) < 0) {
		return -1;
	}

	if (res_srtp_policy->set_suite(policy, suite_val)) {
		ast_log(LOG_WARNING, "Could not set remote SRTP suite\n");
		return -1;
	}

	res_srtp_policy->set_ssrc(policy, ssrc, inbound);

	return 0;
}

static int crypto_activate(struct ast_sdp_crypto *p, int suite_val, unsigned char *remote_key, int key_len, struct ast_rtp_instance *rtp)
{
	struct ast_srtp_policy *local_policy = NULL;
	struct ast_srtp_policy *remote_policy = NULL;
	struct ast_rtp_instance_stats stats = {0,};
	int res = -1;

	if (!ast_rtp_engine_srtp_is_registered()) {
		return -1;
	}

	if (!p) {
		return -1;
	}

	if (!(local_policy = res_srtp_policy->alloc())) {
		return -1;
	}

	if (!(remote_policy = res_srtp_policy->alloc())) {
		goto err;
	}

	if (ast_rtp_instance_get_stats(rtp, &stats, AST_RTP_INSTANCE_STAT_LOCAL_SSRC)) {
		goto err;
	}

	if (set_crypto_policy(local_policy, suite_val, p->local_key, key_len, stats.local_ssrc, 0) < 0) {
		goto err;
	}

	if (set_crypto_policy(remote_policy, suite_val, remote_key, key_len, 0, 1) < 0) {
		goto err;
	}

	/* Add the SRTP policies */
	if (ast_rtp_instance_add_srtp_policy(rtp, remote_policy, local_policy, 0)) {
		ast_log(LOG_WARNING, "Could not set SRTP policies\n");
		goto err;
	}

	ast_debug(1 , "SRTP policy activated\n");
	res = 0;

err:
	if (local_policy) {
		res_srtp_policy->destroy(local_policy);
	}

	if (remote_policy) {
		res_srtp_policy->destroy(remote_policy);
	}

	return res;
}

int ast_sdp_crypto_process(struct ast_rtp_instance *rtp, struct ast_sdp_srtp *srtp, const char *attr)
{
	char *str = NULL;
	char *tag = NULL;
	char *suite = NULL;
	char *key_params = NULL;
	char *key_param = NULL;
	char *session_params = NULL;
	char *key_salt = NULL;       /* The actual master key and key salt */
	char *lifetime = NULL;       /* Key lifetime (# of RTP packets) */
	char *mki = NULL;            /* Master Key Index */
	int found = 0;
	int key_len_from_sdp;
	int key_len_expected;
	int tag_from_sdp;
	int suite_val = 0;
	unsigned char remote_key[SRTP_MAX_KEY_LEN];
	int taglen;
	double sdes_lifetime;
	struct ast_sdp_crypto *crypto;
	struct ast_sdp_srtp *tmp;

	if (!ast_rtp_engine_srtp_is_registered()) {
		return -1;
	}

	str = ast_strdupa(attr);

	tag = strsep(&str, " ");
	suite = strsep(&str, " ");
	key_params = strsep(&str, " ");
	session_params = strsep(&str, " ");

	if (!tag || !suite) {
		ast_log(LOG_WARNING, "Unrecognized crypto attribute a=%s\n", attr);
		return -1;
	}

	/* RFC4568 9.1 - tag is 1-9 digits, greater than zero */
	if (sscanf(tag, "%30d", &tag_from_sdp) != 1 || tag_from_sdp <= 0 || tag_from_sdp > 999999999) {
		ast_log(LOG_WARNING, "Unacceptable a=crypto tag: %s\n", tag);
		return -1;
	}

	if (!ast_strlen_zero(session_params)) {
		ast_log(LOG_WARNING, "Unsupported crypto parameters: %s\n", session_params);
		return -1;
	}

	/* On egress, Asterisk sent several crypto lines in the SIP/SDP offer
	   The remote party might have choosen another line than the first */
	for (tmp = srtp; tmp && tmp->crypto && tmp->crypto->tag != tag_from_sdp;) {
		tmp = AST_LIST_NEXT(tmp, sdp_srtp_list);
	}
	if (tmp) { /* tag matched an already created crypto line */
		unsigned int flags = tmp->flags;

		/* Make that crypto line the head of the list, not by changing the
		   list structure but by exchanging the content of the list members */
		crypto = tmp->crypto;
		tmp->crypto = srtp->crypto;
		tmp->flags = srtp->flags;
		srtp->crypto = crypto;
		srtp->flags = flags;
	} else {
		crypto = srtp->crypto;
	}

	if (!strcmp(suite, "AES_CM_128_HMAC_SHA1_80")) {
		suite_val = AST_AES_CM_128_HMAC_SHA1_80;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_80);
		key_len_expected = 30;
	} else if (!strcmp(suite, "AES_CM_128_HMAC_SHA1_32")) {
		suite_val = AST_AES_CM_128_HMAC_SHA1_32;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_32);
		key_len_expected = 30;
#ifdef HAVE_SRTP_192
	} else if (!strcmp(suite, "AES_192_CM_HMAC_SHA1_80")) {
		suite_val = AST_AES_CM_192_HMAC_SHA1_80;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_80);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_192);
		key_len_expected = 38;
	} else if (!strcmp(suite, "AES_192_CM_HMAC_SHA1_32")) {
		suite_val = AST_AES_CM_192_HMAC_SHA1_32;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_32);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_192);
		key_len_expected = 38;
	/* RFC used a different name while in draft, some still use that */
	} else if (!strcmp(suite, "AES_CM_192_HMAC_SHA1_80")) {
		suite_val = AST_AES_CM_192_HMAC_SHA1_80;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_80);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_192);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_OLD_NAME);
		key_len_expected = 38;
	} else if (!strcmp(suite, "AES_CM_192_HMAC_SHA1_32")) {
		suite_val = AST_AES_CM_192_HMAC_SHA1_32;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_32);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_192);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_OLD_NAME);
		key_len_expected = 38;
#endif
#ifdef HAVE_SRTP_256
	} else if (!strcmp(suite, "AES_256_CM_HMAC_SHA1_80")) {
		suite_val = AST_AES_CM_256_HMAC_SHA1_80;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_80);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_256);
		key_len_expected = 46;
	} else if (!strcmp(suite, "AES_256_CM_HMAC_SHA1_32")) {
		suite_val = AST_AES_CM_256_HMAC_SHA1_32;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_32);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_256);
		key_len_expected = 46;
	/* RFC used a different name while in draft, some still use that */
	} else if (!strcmp(suite, "AES_CM_256_HMAC_SHA1_80")) {
		suite_val = AST_AES_CM_256_HMAC_SHA1_80;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_80);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_256);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_OLD_NAME);
		key_len_expected = 46;
	} else if (!strcmp(suite, "AES_CM_256_HMAC_SHA1_32")) {
		suite_val = AST_AES_CM_256_HMAC_SHA1_32;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_32);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_256);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_OLD_NAME);
		key_len_expected = 46;
#endif
#ifdef HAVE_SRTP_GCM
	} else if (!strcmp(suite, "AEAD_AES_128_GCM")) {
		suite_val = AST_AES_GCM_128;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_16);
		key_len_expected = AES_128_GCM_KEYSIZE_WSALT;
	} else if (!strcmp(suite, "AEAD_AES_256_GCM")) {
		suite_val = AST_AES_GCM_256;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_16);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_256);
		key_len_expected = AES_256_GCM_KEYSIZE_WSALT;
	/* RFC contained a (too) short auth tag for RTP media, some still use that */
	} else if (!strcmp(suite, "AEAD_AES_128_GCM_8")) {
		suite_val = AST_AES_GCM_128_8;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_8);
		key_len_expected = AES_128_GCM_KEYSIZE_WSALT;
	} else if (!strcmp(suite, "AEAD_AES_256_GCM_8")) {
		suite_val = AST_AES_GCM_256_8;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_8);
		ast_set_flag(srtp, AST_SRTP_CRYPTO_AES_256);
		key_len_expected = AES_256_GCM_KEYSIZE_WSALT;
#endif
	} else {
		ast_verb(1, "Unsupported crypto suite: %s\n", suite);
		return -1;
	}

	while ((key_param = strsep(&key_params, ";"))) {
		unsigned int n_lifetime;
		char *method = NULL;
		char *info = NULL;

		method = strsep(&key_param, ":");
		info = strsep(&key_param, ";");
		sdes_lifetime = 0;

		if (strcmp(method, "inline")) {
			continue;
		}

		key_salt = strsep(&info, "|");

		/* The next parameter can be either lifetime or MKI */
		lifetime = strsep(&info, "|");
		if (!lifetime) {
			found = 1;
			break;
		}

		mki = strchr(lifetime, ':');
		if (mki) {
			mki = lifetime;
			lifetime = NULL;
		} else {
			mki = strsep(&info, "|");
		}

		if (mki && *mki != '1') {
			ast_log(LOG_NOTICE, "Crypto MKI handling is not supported: ignoring attribute %s\n", attr);
			continue;
		}

		if (lifetime) {
			if (!strncmp(lifetime, "2^", 2)) {
				char *lifetime_val = lifetime + 2;

				/* Exponential lifetime */
				if (sscanf(lifetime_val, "%30u", &n_lifetime) != 1) {
					ast_log(LOG_NOTICE, "Failed to parse lifetime value in crypto attribute: %s\n", attr);
					continue;
				}

				if (n_lifetime > 48) {
					/* Yeah... that's a bit big. */
					ast_log(LOG_NOTICE, "Crypto lifetime exponent of '%u' is a bit large; using 48\n", n_lifetime);
					n_lifetime = 48;
				}
				sdes_lifetime = pow(2, n_lifetime);
			} else {
				/* Decimal lifetime */
				if (sscanf(lifetime, "%30u", &n_lifetime) != 1) {
					ast_log(LOG_NOTICE, "Failed to parse lifetime value in crypto attribute: %s\n", attr);
					continue;
				}
				sdes_lifetime = n_lifetime;
			}

			/* Accept anything above 10 hours. Less than 10; reject. */
			if (sdes_lifetime < 1800000) {
				ast_log(LOG_NOTICE, "Rejecting crypto attribute '%s': lifetime '%f' too short\n", attr, sdes_lifetime);
				continue;
			}
		}

		ast_debug(2, "Crypto attribute '%s' accepted with lifetime '%f', MKI '%s'\n",
			attr, sdes_lifetime, mki ? mki : "-");

		found = 1;
		break;
	}

	if (!found) {
		ast_log(LOG_NOTICE, "SRTP crypto offer not acceptable: '%s'\n", attr);
		return -1;
	}

	key_len_from_sdp = ast_base64decode(remote_key, key_salt, sizeof(remote_key));
	if (key_len_from_sdp != key_len_expected) {
		ast_log(LOG_WARNING, "SRTP descriptions key length is '%d', not '%d'\n",
			key_len_from_sdp, key_len_expected);
		return -1;
	}

	/* on default, the key is 30 (AES-128); throw that away (only) when the suite changed actually */
	/* ingress: optional, but saves one expensive call to get_random(.) */
	/*  egress: required, because the local key was communicated before the remote key is processed */
	if (crypto->key_len != key_len_from_sdp) {
		if (!int_crypto_alloc(crypto, key_len_from_sdp)) {
			return -1;
		}
	} else if (!memcmp(crypto->remote_key, remote_key, key_len_from_sdp)) {
		ast_debug(1, "SRTP remote key unchanged; maintaining current policy\n");
		ast_set_flag(srtp, AST_SRTP_CRYPTO_OFFER_OK);
		return 0;
	}

	if (key_len_from_sdp > sizeof(crypto->remote_key)) {
		ast_log(LOG_ERROR,
			"SRTP key buffer is %zu although it must be at least %d bytes\n",
			sizeof(crypto->remote_key), key_len_from_sdp);
		return -1;
	}
	memcpy(crypto->remote_key, remote_key, key_len_from_sdp);

	if (crypto_activate(crypto, suite_val, remote_key, key_len_from_sdp, rtp) < 0) {
		return -1;
	}

	if (ast_test_flag(srtp, AST_SRTP_CRYPTO_TAG_32)) {
		taglen = 32;
	} else if (ast_test_flag(srtp, AST_SRTP_CRYPTO_TAG_16)) {
		taglen = 16;
	} else if (ast_test_flag(srtp, AST_SRTP_CRYPTO_TAG_8)) {
		taglen = 8;
	} else {
		taglen = 80;
	}
	if (ast_test_flag(srtp, AST_SRTP_CRYPTO_AES_256)) {
		taglen |= 0x0200;
	} else if (ast_test_flag(srtp, AST_SRTP_CRYPTO_AES_192)) {
		taglen |= 0x0100;
	}
	if (ast_test_flag(srtp, AST_SRTP_CRYPTO_OLD_NAME)) {
		taglen |= 0x0080;
	}

	/* Finally, rebuild the crypto line */
	if (ast_sdp_crypto_build_offer(crypto, taglen)) {
		return -1;
	}

	ast_set_flag(srtp, AST_SRTP_CRYPTO_OFFER_OK);
	return 0;
}

int ast_sdp_crypto_build_offer(struct ast_sdp_crypto *p, int taglen)
{
	/* Rebuild the crypto line */
	if (p->a_crypto) {
		ast_free(p->a_crypto);
	}

	if ((taglen & 0x007f) == 8) {
		if (ast_asprintf(&p->a_crypto, "%d AEAD_AES_%d_GCM_%d inline:%s",
				 p->tag, 128 + ((taglen & 0x0300) >> 2), taglen & 0x007f, p->local_key64) == -1) {
				 ast_log(LOG_ERROR, "Could not allocate memory for crypto line\n");
			return -1;
		}
	} else if ((taglen & 0x007f) == 16) {
		if (ast_asprintf(&p->a_crypto, "%d AEAD_AES_%d_GCM inline:%s",
				 p->tag, 128 + ((taglen & 0x0300) >> 2), p->local_key64) == -1) {
				 ast_log(LOG_ERROR, "Could not allocate memory for crypto line\n");
			return -1;
		}
	} else if ((taglen & 0x0300) && !(taglen & 0x0080)) {
		if (ast_asprintf(&p->a_crypto, "%d AES_%d_CM_HMAC_SHA1_%d inline:%s",
				 p->tag, 128 + ((taglen & 0x0300) >> 2), taglen & 0x007f, p->local_key64) == -1) {
				 ast_log(LOG_ERROR, "Could not allocate memory for crypto line\n");
			return -1;
		}
	} else {
		if (ast_asprintf(&p->a_crypto, "%d AES_CM_%d_HMAC_SHA1_%d inline:%s",
				 p->tag, 128 + ((taglen & 0x0300) >> 2), taglen & 0x007f, p->local_key64) == -1) {
				 ast_log(LOG_ERROR, "Could not allocate memory for crypto line\n");
			return -1;
		}
	}

	ast_debug(1, "Crypto line: a=crypto:%s\n", p->a_crypto);

	return 0;
}

const char *ast_sdp_srtp_get_attrib(struct ast_sdp_srtp *srtp, int dtls_enabled, int default_taglen_32)
{
	int taglen;

	if (!srtp) {
		return NULL;
	}

	/* Set encryption properties */
	if (!srtp->crypto) {

		if (AST_LIST_NEXT(srtp, sdp_srtp_list)) {
			srtp->crypto = ast_sdp_crypto_alloc();
			ast_log(LOG_ERROR, "SRTP SDP list was not empty\n");
		} else {
			const int len = default_taglen_32 ? AST_SRTP_CRYPTO_TAG_32 : AST_SRTP_CRYPTO_TAG_80;
			const int attr[][3] = {
			/* This array creates the following list:
			 * a=crypto:1 AES_CM_128_HMAC_SHA1_ ...
			 * a=crypto:2 AEAD_AES_128_GCM ...
			 * a=crypto:3 AES_256_CM_HMAC_SHA1_ ...
			 * a=crypto:4 AEAD_AES_256_GCM ...
			 * a=crypto:5 AES_192_CM_HMAC_SHA1_ ...
			 * something like 'AEAD_AES_192_GCM' is not specified by the RFCs
			 *
			 * If you want to prefer another crypto suite or you want to
			 * exclude a suite, change this array and recompile Asterisk.
			 * This list cannot be changed from rtp.conf because you should
			 * know what you are doing. Especially AES-192 and AES-GCM are
			 * broken in many VoIP clients, see
			 * https://github.com/cisco/libsrtp/pull/170
			 * https://github.com/cisco/libsrtp/pull/184
			 * Furthermore, AES-GCM uses a shorter crypto-suite string which
			 * causes Nokia phones based on Symbian/S60 to reject the whole
			 * INVITE with status 500, even if a matching suite was offered.
			 * AES-256 might just waste your processor cycles, especially if
			 * your TLS transport is not secured with equivalent grade, see
			 * https://security.stackexchange.com/q/61361
			 * Therefore, AES-128 was preferred here.
			 *
			 * If you want to enable one of those defines, please, go for
			 * CFLAGS='-DENABLE_SRTP_AES_GCM' ./configure && sudo make install
			 */
				{ len, 0, 30 },
#if defined(HAVE_SRTP_GCM) && defined(ENABLE_SRTP_AES_GCM)
				{ AST_SRTP_CRYPTO_TAG_16, 0, AES_128_GCM_KEYSIZE_WSALT },
#endif
#if defined(HAVE_SRTP_256) && defined(ENABLE_SRTP_AES_256)
				{ len, AST_SRTP_CRYPTO_AES_256, 46 },
#endif
#if defined(HAVE_SRTP_GCM) && defined(ENABLE_SRTP_AES_GCM) && defined(ENABLE_SRTP_AES_256)
				{ AST_SRTP_CRYPTO_TAG_16, AST_SRTP_CRYPTO_AES_256, AES_256_GCM_KEYSIZE_WSALT },
#endif
#if defined(HAVE_SRTP_192) && defined(ENABLE_SRTP_AES_192)
				{ len, AST_SRTP_CRYPTO_AES_192, 38 },
#endif
			};
			struct ast_sdp_srtp *tmp = srtp;
			int i;

			for (i = 0; i < sizeof(attr) / sizeof(attr[0]); i++) {
				if (attr[i][0]) {
					ast_set_flag(tmp, attr[i][0]);
				}
				if (attr[i][1]) {
					ast_set_flag(tmp, attr[i][1]);
				}
				tmp->crypto = sdp_crypto_alloc(attr[i][2]); /* key_len */
				tmp->crypto->tag = (i + 1); /* tag starts at 1 */

				if (i < sizeof(attr) / sizeof(attr[0]) - 1) {
					AST_LIST_NEXT(tmp, sdp_srtp_list) = ast_sdp_srtp_alloc();
					tmp = AST_LIST_NEXT(tmp, sdp_srtp_list);
				}
			}
		}
	}

	if (dtls_enabled) {
		/* If DTLS-SRTP is enabled the key details will be pulled from TLS */
		return NULL;
	}

	/* set the key length based on INVITE or settings */
	if (ast_test_flag(srtp, AST_SRTP_CRYPTO_TAG_80)) {
		taglen = 80;
	} else if (ast_test_flag(srtp, AST_SRTP_CRYPTO_TAG_32)) {
		taglen = 32;
	} else if (ast_test_flag(srtp, AST_SRTP_CRYPTO_TAG_16)) {
		taglen = 16;
	} else if (ast_test_flag(srtp, AST_SRTP_CRYPTO_TAG_8)) {
		taglen = 8;
	} else {
		taglen = default_taglen_32 ? 32 : 80;
	}
	if (ast_test_flag(srtp, AST_SRTP_CRYPTO_AES_256)) {
		taglen |= 0x0200;
	} else if (ast_test_flag(srtp, AST_SRTP_CRYPTO_AES_192)) {
		taglen |= 0x0100;
	}
	if (ast_test_flag(srtp, AST_SRTP_CRYPTO_OLD_NAME)) {
		taglen |= 0x0080;
	}

	if (srtp->crypto && (ast_sdp_crypto_build_offer(srtp->crypto, taglen) >= 0)) {
		return srtp->crypto->a_crypto;
	}

	ast_log(LOG_WARNING, "No SRTP key management enabled\n");
	return NULL;
}

char *ast_sdp_get_rtp_profile(unsigned int sdes_active, struct ast_rtp_instance *instance, unsigned int using_avpf,
	unsigned int force_avp)
{
	struct ast_rtp_engine_dtls *dtls;

	if ((dtls = ast_rtp_instance_get_dtls(instance)) && dtls->active(instance)) {
		if (force_avp) {
			return using_avpf ? "RTP/SAVPF" : "RTP/SAVP";
		} else {
			return using_avpf ? "UDP/TLS/RTP/SAVPF" : "UDP/TLS/RTP/SAVP";
		}
	} else {
		if (using_avpf) {
			return sdes_active ? "RTP/SAVPF" : "RTP/AVPF";
		} else {
			return sdes_active ? "RTP/SAVP" : "RTP/AVP";
		}
	}
}

