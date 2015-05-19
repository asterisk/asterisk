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

/*! \file ast_sdp_crypto.c
 *
 * \brief SRTP and SDP Security descriptions
 *
 * Specified in RFC 3711
 * Specified in RFC 4568
 *
 * \author Mikael Magnusson <mikma@users.sourceforge.net>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include <math.h>
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/sdp_srtp.h"

#define SRTP_MASTER_LEN 30
#define SRTP_MASTERKEY_LEN 16
#define SRTP_MASTERSALT_LEN ((SRTP_MASTER_LEN) - (SRTP_MASTERKEY_LEN))
#define SRTP_MASTER_LEN64 (((SRTP_MASTER_LEN) * 8 + 5) / 6 + 1)

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
	if (srtp->crypto) {
		ast_sdp_crypto_destroy(srtp->crypto);
	}
	srtp->crypto = NULL;
	ast_free(srtp);
}

struct ast_sdp_crypto {
	char *a_crypto;
	unsigned char local_key[SRTP_MASTER_LEN];
	int tag;
	char local_key64[SRTP_MASTER_LEN64];
	unsigned char remote_key[SRTP_MASTER_LEN];
};

static int set_crypto_policy(struct ast_srtp_policy *policy, int suite_val, const unsigned char *master_key, unsigned long ssrc, int inbound);

void ast_sdp_crypto_destroy(struct ast_sdp_crypto *crypto)
{
	ast_free(crypto->a_crypto);
	crypto->a_crypto = NULL;
	ast_free(crypto);
}

struct ast_sdp_crypto *ast_sdp_crypto_alloc(void)
{
	struct ast_sdp_crypto *p;
	int key_len;
	unsigned char remote_key[SRTP_MASTER_LEN];

	if (!ast_rtp_engine_srtp_is_registered()) {
		return NULL;
	}

	if (!(p = ast_calloc(1, sizeof(*p)))) {
		return NULL;
	}
	p->tag = 1;

	if (res_srtp->get_random(p->local_key, sizeof(p->local_key)) < 0) {
		ast_sdp_crypto_destroy(p);
		return NULL;
	}

	ast_base64encode(p->local_key64, p->local_key, SRTP_MASTER_LEN, sizeof(p->local_key64));

	key_len = ast_base64decode(remote_key, p->local_key64, sizeof(remote_key));

	if (key_len != SRTP_MASTER_LEN) {
		ast_log(LOG_ERROR, "base64 encode/decode bad len %d != %d\n", key_len, SRTP_MASTER_LEN);
		ast_sdp_crypto_destroy(p);
		return NULL;
	}

	if (memcmp(remote_key, p->local_key, SRTP_MASTER_LEN)) {
		ast_log(LOG_ERROR, "base64 encode/decode bad key\n");
		ast_sdp_crypto_destroy(p);
		return NULL;
	}

	ast_debug(1 , "local_key64 %s len %zu\n", p->local_key64, strlen(p->local_key64));

	return p;
}

static int set_crypto_policy(struct ast_srtp_policy *policy, int suite_val, const unsigned char *master_key, unsigned long ssrc, int inbound)
{
	const unsigned char *master_salt = NULL;

	if (!ast_rtp_engine_srtp_is_registered()) {
		return -1;
	}

	master_salt = master_key + SRTP_MASTERKEY_LEN;
	if (res_srtp_policy->set_master_key(policy, master_key, SRTP_MASTERKEY_LEN, master_salt, SRTP_MASTERSALT_LEN) < 0) {
		return -1;
	}

	if (res_srtp_policy->set_suite(policy, suite_val)) {
		ast_log(LOG_WARNING, "Could not set remote SRTP suite\n");
		return -1;
	}

	res_srtp_policy->set_ssrc(policy, ssrc, inbound);

	return 0;
}

static int crypto_activate(struct ast_sdp_crypto *p, int suite_val, unsigned char *remote_key, struct ast_rtp_instance *rtp)
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

	if (set_crypto_policy(local_policy, suite_val, p->local_key, stats.local_ssrc, 0) < 0) {
		goto err;
	}

	if (set_crypto_policy(remote_policy, suite_val, remote_key, 0, 1) < 0) {
		goto err;
	}

	/* Add the SRTP policies */
	if (ast_rtp_instance_add_srtp_policy(rtp, remote_policy, local_policy)) {
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
	int key_len = 0;
	int suite_val = 0;
	unsigned char remote_key[SRTP_MASTER_LEN];
	int taglen = 0;
	double sdes_lifetime;
	struct ast_sdp_crypto *crypto = srtp->crypto;

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
	if (sscanf(tag, "%30d", &crypto->tag) != 1 || crypto->tag <= 0 || crypto->tag > 999999999) {
		ast_log(LOG_WARNING, "Unacceptable a=crypto tag: %s\n", tag);
		return -1;
	}

	if (!ast_strlen_zero(session_params)) {
		ast_log(LOG_WARNING, "Unsupported crypto parameters: %s\n", session_params);
		return -1;
	}

	if (!strcmp(suite, "AES_CM_128_HMAC_SHA1_80")) {
		suite_val = AST_AES_CM_128_HMAC_SHA1_80;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_80);
		taglen = 80;
	} else if (!strcmp(suite, "AES_CM_128_HMAC_SHA1_32")) {
		suite_val = AST_AES_CM_128_HMAC_SHA1_32;
		ast_set_flag(srtp, AST_SRTP_CRYPTO_TAG_32);
		taglen = 32;
	} else {
		ast_log(LOG_WARNING, "Unsupported crypto suite: %s\n", suite);
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

	key_len = ast_base64decode(remote_key, key_salt, sizeof(remote_key));
	if (key_len != SRTP_MASTER_LEN) {
		ast_log(LOG_WARNING, "SRTP descriptions key length '%d' != master length '%d'\n",
			key_len, SRTP_MASTER_LEN);
		return -1;
	}

	if (!memcmp(crypto->remote_key, remote_key, sizeof(crypto->remote_key))) {
		ast_debug(1, "SRTP remote key unchanged; maintaining current policy\n");
		ast_set_flag(srtp, AST_SRTP_CRYPTO_OFFER_OK);
		return 0;
	}
	memcpy(crypto->remote_key, remote_key, sizeof(crypto->remote_key));

	if (crypto_activate(crypto, suite_val, remote_key, rtp) < 0) {
		return -1;
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

	if (ast_asprintf(&p->a_crypto, "%d AES_CM_128_HMAC_SHA1_%i inline:%s",
			 p->tag, taglen, p->local_key64) == -1) {
			ast_log(LOG_ERROR, "Could not allocate memory for crypto line\n");
		return -1;
	}

	ast_debug(1, "Crypto line: a=crypto:%s\n", p->a_crypto);

	return 0;
}

const char *ast_sdp_srtp_get_attrib(struct ast_sdp_srtp *srtp, int dtls_enabled, int default_taglen_32)
{
	int taglen = default_taglen_32 ? 32 : 80;

	if (!srtp) {
		return NULL;
	}

	/* Set encryption properties */
	if (!srtp->crypto) {
		srtp->crypto = ast_sdp_crypto_alloc();
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

