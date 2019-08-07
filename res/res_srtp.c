/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005, Mikael Magnusson
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
 *
 * Builds on libSRTP http://srtp.sourceforge.net
 */

/*! \file res_srtp.c
 *
 * \brief Secure RTP (SRTP)
 *
 * Secure RTP (SRTP)
 * Specified in RFC 3711.
 *
 * \author Mikael Magnusson <mikma@users.sourceforge.net>
 */

/*** MODULEINFO
	<depend>srtp</depend>
	<use type="external">openssl</use>
	<support_level>core</support_level>
***/

/* See https://wiki.asterisk.org/wiki/display/AST/Secure+Calling */

#include "asterisk.h"                   /* for NULL, size_t, memcpy, etc */

#include <math.h>                       /* for pow */

#if HAVE_SRTP_VERSION > 1
# include <srtp2/srtp.h>
# include "srtp/srtp_compat.h"
# include <openssl/rand.h>
#else
# include <srtp/srtp.h>
# ifdef HAVE_OPENSSL
#  include <openssl/rand.h>
# else
#  include <srtp/crypto_kernel.h>
# endif
#endif

#include "asterisk/astobj2.h"           /* for ao2_t_ref, etc */
#include "asterisk/frame.h"             /* for AST_FRIENDLY_OFFSET */
#include "asterisk/logger.h"            /* for ast_log, ast_debug, etc */
#include "asterisk/module.h"            /* for ast_module_info, etc */
#include "asterisk/sdp_srtp.h"
#include "asterisk/res_srtp.h"          /* for ast_srtp_cb, ast_srtp_suite, etc */
#include "asterisk/rtp_engine.h"        /* for ast_rtp_engine_register_srtp, etc */
#include "asterisk/utils.h"             /* for ast_free, ast_calloc */

struct ast_srtp {
	struct ast_rtp_instance *rtp;
	struct ao2_container *policies;
	srtp_t session;
	const struct ast_srtp_cb *cb;
	void *data;
	int warned;
	unsigned char buf[8192 + AST_FRIENDLY_OFFSET];
	unsigned char rtcpbuf[8192 + AST_FRIENDLY_OFFSET];
};

struct ast_srtp_policy {
	srtp_policy_t sp;
};

/*! Tracks whether or not we've initialized the libsrtp library */
static int g_initialized = 0;

/* SRTP functions */
static int ast_srtp_create(struct ast_srtp **srtp, struct ast_rtp_instance *rtp, struct ast_srtp_policy *policy);
static int ast_srtp_replace(struct ast_srtp **srtp, struct ast_rtp_instance *rtp, struct ast_srtp_policy *policy);
static void ast_srtp_destroy(struct ast_srtp *srtp);
static int ast_srtp_add_stream(struct ast_srtp *srtp, struct ast_srtp_policy *policy);
static int ast_srtp_change_source(struct ast_srtp *srtp, unsigned int from_ssrc, unsigned int to_ssrc);

static int ast_srtp_unprotect(struct ast_srtp *srtp, void *buf, int *len, int rtcp);
static int ast_srtp_protect(struct ast_srtp *srtp, void **buf, int *len, int rtcp);
static void ast_srtp_set_cb(struct ast_srtp *srtp, const struct ast_srtp_cb *cb, void *data);
static int ast_srtp_get_random(unsigned char *key, size_t len);

/* Policy functions */
static struct ast_srtp_policy *ast_srtp_policy_alloc(void);
static void ast_srtp_policy_destroy(struct ast_srtp_policy *policy);
static int ast_srtp_policy_set_suite(struct ast_srtp_policy *policy, enum ast_srtp_suite suite);
static int ast_srtp_policy_set_master_key(struct ast_srtp_policy *policy, const unsigned char *key, size_t key_len, const unsigned char *salt, size_t salt_len);
static void ast_srtp_policy_set_ssrc(struct ast_srtp_policy *policy, unsigned long ssrc, int inbound);

static struct ast_srtp_res srtp_res = {
	.create = ast_srtp_create,
	.replace = ast_srtp_replace,
	.destroy = ast_srtp_destroy,
	.add_stream = ast_srtp_add_stream,
	.change_source = ast_srtp_change_source,
	.set_cb = ast_srtp_set_cb,
	.unprotect = ast_srtp_unprotect,
	.protect = ast_srtp_protect,
	.get_random = ast_srtp_get_random
};

static struct ast_srtp_policy_res policy_res = {
	.alloc = ast_srtp_policy_alloc,
	.destroy = ast_srtp_policy_destroy,
	.set_suite = ast_srtp_policy_set_suite,
	.set_master_key = ast_srtp_policy_set_master_key,
	.set_ssrc = ast_srtp_policy_set_ssrc
};

static const char *srtp_errstr(int err)
{
	switch(err) {
	case err_status_ok:
		return "nothing to report";
	case err_status_fail:
		return "unspecified failure";
	case err_status_bad_param:
		return "unsupported parameter";
	case err_status_alloc_fail:
		return "couldn't allocate memory";
	case err_status_dealloc_fail:
		return "couldn't deallocate properly";
	case err_status_init_fail:
		return "couldn't initialize";
	case err_status_terminus:
		return "can't process as much data as requested";
	case err_status_auth_fail:
		return "authentication failure";
	case err_status_cipher_fail:
		return "cipher failure";
	case err_status_replay_fail:
		return "replay check failed (bad index)";
	case err_status_replay_old:
		return "replay check failed (index too old)";
	case err_status_algo_fail:
		return "algorithm failed test routine";
	case err_status_no_such_op:
		return "unsupported operation";
	case err_status_no_ctx:
		return "no appropriate context found";
	case err_status_cant_check:
		return "unable to perform desired validation";
	case err_status_key_expired:
		return "can't use key any more";
	default:
		return "unknown";
	}
}

static int policy_hash_fn(const void *obj, const int flags)
{
	const struct ast_srtp_policy *policy = obj;

	return policy->sp.ssrc.type == ssrc_specific ? policy->sp.ssrc.value : policy->sp.ssrc.type;
}

static int policy_cmp_fn(void *obj, void *arg, int flags)
{
	const struct ast_srtp_policy *one = obj, *two = arg;

	return one->sp.ssrc.type == two->sp.ssrc.type && one->sp.ssrc.value == two->sp.ssrc.value;
}

static struct ast_srtp_policy *find_policy(struct ast_srtp *srtp, const srtp_policy_t *policy, int flags)
{
	struct ast_srtp_policy tmp = {
		.sp = {
			.ssrc.type = policy->ssrc.type,
			.ssrc.value = policy->ssrc.value,
		},
	};

	return ao2_t_find(srtp->policies, &tmp, flags, "Looking for policy");
}

static struct ast_srtp *res_srtp_new(void)
{
	struct ast_srtp *srtp;

	if (!(srtp = ast_calloc(1, sizeof(*srtp)))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for srtp\n");
		return NULL;
	}

	srtp->policies = ao2_t_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, 5,
		policy_hash_fn, NULL, policy_cmp_fn, "SRTP policy container");
	if (!srtp->policies) {
		ast_free(srtp);
		return NULL;
	}

	srtp->warned = 1;

	return srtp;
}

/*
  struct ast_srtp_policy
*/
static void srtp_event_cb(srtp_event_data_t *data)
{
	switch (data->event) {
	case event_ssrc_collision:
		ast_debug(1, "SSRC collision\n");
		break;
	case event_key_soft_limit:
		ast_debug(1, "event_key_soft_limit\n");
		break;
	case event_key_hard_limit:
		ast_debug(1, "event_key_hard_limit\n");
		break;
	case event_packet_index_limit:
		ast_debug(1, "event_packet_index_limit\n");
		break;
	}
}

static void ast_srtp_policy_set_ssrc(struct ast_srtp_policy *policy,
		unsigned long ssrc, int inbound)
{
	if (ssrc) {
		policy->sp.ssrc.type = ssrc_specific;
		policy->sp.ssrc.value = ssrc;
	} else {
		policy->sp.ssrc.type = inbound ? ssrc_any_inbound : ssrc_any_outbound;
	}
}

static void policy_destructor(void *obj)
{
	struct ast_srtp_policy *policy = obj;

	if (policy->sp.key) {
		ast_free(policy->sp.key);
		policy->sp.key = NULL;
	}
}

static struct ast_srtp_policy *ast_srtp_policy_alloc()
{
	struct ast_srtp_policy *tmp;

	if (!(tmp = ao2_t_alloc(sizeof(*tmp), policy_destructor, "Allocating policy"))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for srtp_policy\n");
	}

	return tmp;
}

static void ast_srtp_policy_destroy(struct ast_srtp_policy *policy)
{
	ao2_t_ref(policy, -1, "Destroying policy");
}

static int policy_set_suite(crypto_policy_t *p, enum ast_srtp_suite suite)
{
	switch (suite) {
	case AST_AES_CM_128_HMAC_SHA1_80:
		crypto_policy_set_aes_cm_128_hmac_sha1_80(p);
		return 0;

	case AST_AES_CM_128_HMAC_SHA1_32:
		crypto_policy_set_aes_cm_128_hmac_sha1_32(p);
		return 0;

#ifdef HAVE_SRTP_192
	case AST_AES_CM_192_HMAC_SHA1_80:
		crypto_policy_set_aes_cm_192_hmac_sha1_80(p);
		return 0;

	case AST_AES_CM_192_HMAC_SHA1_32:
		crypto_policy_set_aes_cm_192_hmac_sha1_32(p);
		return 0;
#endif
#ifdef HAVE_SRTP_256
	case AST_AES_CM_256_HMAC_SHA1_80:
		crypto_policy_set_aes_cm_256_hmac_sha1_80(p);
		return 0;

	case AST_AES_CM_256_HMAC_SHA1_32:
		crypto_policy_set_aes_cm_256_hmac_sha1_32(p);
		return 0;
#endif
#ifdef HAVE_SRTP_GCM
	case AST_AES_GCM_128:
		crypto_policy_set_aes_gcm_128_16_auth(p);
		return 0;

	case AST_AES_GCM_256:
		crypto_policy_set_aes_gcm_256_16_auth(p);
		return 0;

	case AST_AES_GCM_128_8:
		crypto_policy_set_aes_gcm_128_8_auth(p);
		return 0;

	case AST_AES_GCM_256_8:
		crypto_policy_set_aes_gcm_256_8_auth(p);
		return 0;
#endif

	default:
		ast_log(LOG_ERROR, "Invalid crypto suite: %u\n", suite);
		return -1;
	}
}

static int ast_srtp_policy_set_suite(struct ast_srtp_policy *policy, enum ast_srtp_suite suite)
{
	return policy_set_suite(&policy->sp.rtp, suite) | policy_set_suite(&policy->sp.rtcp, suite);
}

static int ast_srtp_policy_set_master_key(struct ast_srtp_policy *policy, const unsigned char *key, size_t key_len, const unsigned char *salt, size_t salt_len)
{
	size_t size = key_len + salt_len;
	unsigned char *master_key;

	if (policy->sp.key) {
		ast_free(policy->sp.key);
		policy->sp.key = NULL;
	}

	if (!(master_key = ast_calloc(1, size))) {
		return -1;
	}

	memcpy(master_key, key, key_len);
	memcpy(master_key + key_len, salt, salt_len);

	policy->sp.key = master_key;

	return 0;
}

static int ast_srtp_get_random(unsigned char *key, size_t len)
{
#ifdef HAVE_OPENSSL
	return RAND_bytes(key, len) > 0 ? 0: -1;
#else
	return crypto_get_random(key, len) != err_status_ok ? -1: 0;
#endif
}

static void ast_srtp_set_cb(struct ast_srtp *srtp, const struct ast_srtp_cb *cb, void *data)
{
	if (!srtp) {
		return;
	}

	srtp->cb = cb;
	srtp->data = data;
}

/* Vtable functions */
static int ast_srtp_unprotect(struct ast_srtp *srtp, void *buf, int *len, int rtcp)
{
	int res = 0;
	int i;
	int retry = 0;
	struct ast_rtp_instance_stats stats = {0,};

tryagain:

	if (!srtp->session) {
		ast_log(LOG_ERROR, "SRTP unprotect %s - missing session\n", rtcp ? "rtcp" : "rtp");
		errno = EINVAL;
		return -1;
	}

	for (i = 0; i < 2; i++) {
		res = rtcp ? srtp_unprotect_rtcp(srtp->session, buf, len) : srtp_unprotect(srtp->session, buf, len);
		if (res != err_status_no_ctx) {
			break;
		}

		if (srtp->cb && srtp->cb->no_ctx) {
			if (ast_rtp_instance_get_stats(srtp->rtp, &stats, AST_RTP_INSTANCE_STAT_REMOTE_SSRC)) {
				break;
			}
			if (srtp->cb->no_ctx(srtp->rtp, stats.remote_ssrc, srtp->data) < 0) {
				break;
			}
		} else {
			break;
		}
	}

	if (retry == 0  && res == err_status_replay_old) {
		ast_log(AST_LOG_NOTICE, "SRTP unprotect failed with %s, retrying\n", srtp_errstr(res));

		if (srtp->session) {
			struct ast_srtp_policy *policy;
			struct ao2_iterator it;
			int policies_count;

			/* dealloc first */
			ast_debug(5, "SRTP destroy before re-create\n");
			srtp_dealloc(srtp->session);

			/* get the count */
			policies_count = ao2_container_count(srtp->policies);

			/* get the first to build up */
			it = ao2_iterator_init(srtp->policies, 0);
			policy = ao2_iterator_next(&it);

			ast_debug(5, "SRTP try to re-create\n");
			if (policy) {
				int res_srtp_create = srtp_create(&srtp->session, &policy->sp);
				if (res_srtp_create == err_status_ok) {
					ast_debug(5, "SRTP re-created with first policy\n");
					ao2_t_ref(policy, -1, "Unreffing first policy for re-creating srtp session");

					/* if we have more than one policy, add them */
					if (policies_count > 1) {
						ast_debug(5, "Add all the other %d policies\n",
							policies_count - 1);
						while ((policy = ao2_iterator_next(&it))) {
							srtp_add_stream(srtp->session, &policy->sp);
							ao2_t_ref(policy, -1, "Unreffing n-th policy for re-creating srtp session");
						}
					}

					retry++;
					ao2_iterator_destroy(&it);
					goto tryagain;
				}
				ast_log(LOG_ERROR, "SRTP session could not be re-created after unprotect failure: %s\n", srtp_errstr(res_srtp_create));

				/* If srtp_create() fails with a previously alloced session, it will have been dealloced before returning. */
				srtp->session = NULL;

				ao2_t_ref(policy, -1, "Unreffing first policy after srtp_create failed");
			}
			ao2_iterator_destroy(&it);
		}
	}

	if (!srtp->session) {
		errno = EINVAL;
		return -1;
	}

	if (res != err_status_ok && res != err_status_replay_fail ) {
		/*
		 * Authentication failures happen when an active attacker tries to
		 * insert malicious RTP packets. Furthermore, authentication failures
		 * happen, when the other party encrypts the sRTP data in an unexpected
		 * way. This happens quite often with RTCP. Therefore, when you see
		 * authentication failures, try to identify the implementation
		 * (author and product name) used by your other party. Try to investigate
		 * whether they use a custom library or an outdated version of libSRTP.
		 */
		if (rtcp) {
			ast_verb(2, "SRTCP unprotect failed on SSRC %u because of %s\n",
				ast_rtp_instance_get_ssrc(srtp->rtp), srtp_errstr(res));
		} else {
			if ((srtp->warned >= 10) && !((srtp->warned - 10) % 150)) {
				ast_verb(2, "SRTP unprotect failed on SSRC %u because of %s %d\n",
					ast_rtp_instance_get_ssrc(srtp->rtp), srtp_errstr(res), srtp->warned);
				srtp->warned = 11;
			} else {
				srtp->warned++;
			}
		}
		errno = EAGAIN;
		return -1;
	}

	return *len;
}

static int ast_srtp_protect(struct ast_srtp *srtp, void **buf, int *len, int rtcp)
{
	int res;
	unsigned char *localbuf;

	if (!srtp->session) {
		ast_log(LOG_ERROR, "SRTP protect %s - missing session\n", rtcp ? "rtcp" : "rtp");
		errno = EINVAL;
		return -1;
	}

	if ((*len + SRTP_MAX_TRAILER_LEN) > sizeof(srtp->buf)) {
		return -1;
	}

	localbuf = rtcp ? srtp->rtcpbuf : srtp->buf;

	memcpy(localbuf, *buf, *len);

	if ((res = rtcp ? srtp_protect_rtcp(srtp->session, localbuf, len) : srtp_protect(srtp->session, localbuf, len)) != err_status_ok && res != err_status_replay_fail) {
		ast_log(LOG_WARNING, "SRTP protect: %s\n", srtp_errstr(res));
		return -1;
	}

	*buf = localbuf;
	return *len;
}

static int ast_srtp_create(struct ast_srtp **srtp, struct ast_rtp_instance *rtp, struct ast_srtp_policy *policy)
{
	struct ast_srtp *temp;
	int status;

	if (!(temp = res_srtp_new())) {
		return -1;
	}
	ast_module_ref(ast_module_info->self);

	/* Any failures after this point can use ast_srtp_destroy to destroy the instance */
	status = srtp_create(&temp->session, &policy->sp);
	if (status != err_status_ok) {
		/* Session either wasn't created or was created and dealloced. */
		temp->session = NULL;
		ast_srtp_destroy(temp);
		ast_log(LOG_ERROR, "Failed to create srtp session on rtp instance (%p) - %s\n",
				rtp, srtp_errstr(status));
		return -1;
	}

	temp->rtp = rtp;
	*srtp = temp;

	ao2_t_link((*srtp)->policies, policy, "Created initial policy");

	return 0;
}

static int ast_srtp_replace(struct ast_srtp **srtp, struct ast_rtp_instance *rtp, struct ast_srtp_policy *policy)
{
	struct ast_srtp *old = *srtp;
	int res = ast_srtp_create(srtp, rtp, policy);

	if (!res && old) {
		ast_srtp_destroy(old);
	}

	if (res) {
		ast_log(LOG_ERROR, "Failed to replace srtp (%p) on rtp instance (%p) "
				"- keeping old\n", *srtp, rtp);
	}

	return res;
}

static void ast_srtp_destroy(struct ast_srtp *srtp)
{
	if (srtp->session) {
		srtp_dealloc(srtp->session);
	}

	ao2_t_callback(srtp->policies, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL, "Unallocate policy");
	ao2_t_ref(srtp->policies, -1, "Destroying container");

	ast_free(srtp);
	ast_module_unref(ast_module_info->self);
}

static int ast_srtp_add_stream(struct ast_srtp *srtp, struct ast_srtp_policy *policy)
{
	struct ast_srtp_policy *match;

	/* For existing streams, replace if its an SSRC stream, or bail if its a wildcard */
	if ((match = find_policy(srtp, &policy->sp, OBJ_POINTER))) {
		if (policy->sp.ssrc.type != ssrc_specific) {
			ast_log(AST_LOG_WARNING, "Cannot replace an existing wildcard policy\n");
			ao2_t_ref(match, -1, "Unreffing already existing policy");
			return -1;
		} else {
			if (srtp_remove_stream(srtp->session, match->sp.ssrc.value) != err_status_ok) {
				ast_log(AST_LOG_WARNING, "Failed to remove SRTP stream for SSRC %u\n", match->sp.ssrc.value);
			}
			ao2_t_unlink(srtp->policies, match, "Remove existing match policy");
			ao2_t_ref(match, -1, "Unreffing already existing policy");
		}
	}

	ast_debug(3, "Adding new policy for %s %u\n",
		policy->sp.ssrc.type == ssrc_specific ? "SSRC" : "type",
		policy->sp.ssrc.type == ssrc_specific ? policy->sp.ssrc.value : policy->sp.ssrc.type);
	if (srtp_add_stream(srtp->session, &policy->sp) != err_status_ok) {
		ast_log(AST_LOG_WARNING, "Failed to add SRTP stream for %s %u\n",
			policy->sp.ssrc.type == ssrc_specific ? "SSRC" : "type",
			policy->sp.ssrc.type == ssrc_specific ? policy->sp.ssrc.value : policy->sp.ssrc.type);
		return -1;
	}

	ao2_t_link(srtp->policies, policy, "Added additional stream");

	return 0;
}

static int ast_srtp_change_source(struct ast_srtp *srtp, unsigned int from_ssrc, unsigned int to_ssrc)
{
	struct ast_srtp_policy *match;
	struct srtp_policy_t sp = {
		.ssrc.type = ssrc_specific,
		.ssrc.value = from_ssrc,
	};
	err_status_t status;

	/* If we find a match, return and unlink it from the container so we
	 * can change the SSRC (which is part of the hash) and then have
	 * ast_srtp_add_stream link it back in if all is well */
	if ((match = find_policy(srtp, &sp, OBJ_POINTER | OBJ_UNLINK))) {
		match->sp.ssrc.value = to_ssrc;
		if (ast_srtp_add_stream(srtp, match)) {
			ast_log(LOG_WARNING, "Couldn't add stream\n");
		} else if ((status = srtp_remove_stream(srtp->session, from_ssrc))) {
			ast_debug(3, "Couldn't remove stream (%u)\n", status);
		}
		ao2_t_ref(match, -1, "Unreffing found policy in change_source");
	}

	return 0;
}

struct ast_sdp_crypto {
	char *a_crypto;
	unsigned char local_key[SRTP_MAX_KEY_LEN];
	int tag;
	char local_key64[((SRTP_MAX_KEY_LEN) * 8 + 5) / 6 + 1];
	unsigned char remote_key[SRTP_MAX_KEY_LEN];
	int key_len;
};

static void res_sdp_crypto_dtor(struct ast_sdp_crypto *crypto)
{
	if (crypto) {
		ast_free(crypto->a_crypto);
		crypto->a_crypto = NULL;
		ast_free(crypto);

		ast_module_unref(ast_module_info->self);
	}
}

static struct ast_sdp_crypto *crypto_init_keys(struct ast_sdp_crypto *p, const int key_len)
{
	unsigned char remote_key[key_len];

	if (srtp_res.get_random(p->local_key, key_len) < 0) {
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

static struct ast_sdp_crypto *sdp_crypto_alloc(const int key_len)
{
	struct ast_sdp_crypto *p, *result;

	if (!(p = ast_calloc(1, sizeof(*p)))) {
		return NULL;
	}
	p->tag = 1;
	ast_module_ref(ast_module_info->self);

	/* default is a key which uses AST_AES_CM_128_HMAC_SHA1_xx */
	result = crypto_init_keys(p, key_len);
	if (!result) {
		res_sdp_crypto_dtor(p);
	}

	return result;
}

static struct ast_sdp_crypto *res_sdp_crypto_alloc(void)
{
	return sdp_crypto_alloc(SRTP_MASTER_KEY_LEN);
}

static int res_sdp_crypto_build_offer(struct ast_sdp_crypto *p, int taglen)
{
	int res;

	/* Rebuild the crypto line */
	ast_free(p->a_crypto);
	p->a_crypto = NULL;

	if ((taglen & 0x007f) == 8) {
		res = ast_asprintf(&p->a_crypto, "%d AEAD_AES_%d_GCM_%d inline:%s",
			p->tag, 128 + ((taglen & 0x0300) >> 2), taglen & 0x007f, p->local_key64);
	} else if ((taglen & 0x007f) == 16) {
		res = ast_asprintf(&p->a_crypto, "%d AEAD_AES_%d_GCM inline:%s",
			p->tag, 128 + ((taglen & 0x0300) >> 2), p->local_key64);
	} else if ((taglen & 0x0300) && !(taglen & 0x0080)) {
		res = ast_asprintf(&p->a_crypto, "%d AES_%d_CM_HMAC_SHA1_%d inline:%s",
			p->tag, 128 + ((taglen & 0x0300) >> 2), taglen & 0x007f, p->local_key64);
	} else {
		res = ast_asprintf(&p->a_crypto, "%d AES_CM_%d_HMAC_SHA1_%d inline:%s",
			p->tag, 128 + ((taglen & 0x0300) >> 2), taglen & 0x007f, p->local_key64);
	}
	if (res == -1 || !p->a_crypto) {
		ast_log(LOG_ERROR, "Could not allocate memory for crypto line\n");
		return -1;
	}

	ast_debug(1, "Crypto line: a=crypto:%s\n", p->a_crypto);

	return 0;
}

static int set_crypto_policy(struct ast_srtp_policy *policy, int suite_val, const unsigned char *master_key, int key_len, unsigned long ssrc, int inbound)
{
	if (policy_res.set_master_key(policy, master_key, key_len, NULL, 0) < 0) {
		return -1;
	}

	if (policy_res.set_suite(policy, suite_val)) {
		ast_log(LOG_WARNING, "Could not set remote SRTP suite\n");
		return -1;
	}

	policy_res.set_ssrc(policy, ssrc, inbound);

	return 0;
}

static int crypto_activate(struct ast_sdp_crypto *p, int suite_val, unsigned char *remote_key, int key_len, struct ast_rtp_instance *rtp)
{
	struct ast_srtp_policy *local_policy = NULL;
	struct ast_srtp_policy *remote_policy = NULL;
	struct ast_rtp_instance_stats stats = {0,};
	int res = -1;

	if (!p) {
		return -1;
	}

	if (!(local_policy = policy_res.alloc())) {
		return -1;
	}

	if (!(remote_policy = policy_res.alloc())) {
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
		policy_res.destroy(local_policy);
	}

	if (remote_policy) {
		policy_res.destroy(remote_policy);
	}

	return res;
}

static int res_sdp_crypto_parse_offer(struct ast_rtp_instance *rtp, struct ast_sdp_srtp *srtp, const char *attr)
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

	str = ast_strdupa(attr);

	tag = strsep(&str, " ");
	suite = strsep(&str, " ");
	key_params = strsep(&str, " ");
	session_params = strsep(&str, " ");

	if (!tag || !suite) {
		ast_log(LOG_WARNING, "Unrecognized crypto attribute a=%s\n", attr);
		return -1;
	}

	/* RFC4568 9.1 - tag is 1-9 digits */
	if (sscanf(tag, "%30d", &tag_from_sdp) != 1 || tag_from_sdp < 0 || tag_from_sdp > 999999999) {
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
		crypto->tag = tag_from_sdp;
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

			/* Accept anything above ~5.8 hours. Less than ~5.8; reject. */
			if (sdes_lifetime < 1048576) {
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
		if (!crypto_init_keys(crypto, key_len_from_sdp)) {
			return -1;
		}
	} else if (!memcmp(crypto->remote_key, remote_key, key_len_from_sdp)) {
		ast_debug(1, "SRTP remote key unchanged; maintaining current policy\n");
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
	if (res_sdp_crypto_build_offer(crypto, taglen)) {
		return -1;
	}

	ast_set_flag(srtp, AST_SRTP_CRYPTO_OFFER_OK);
	return 0;
}

static const char *res_sdp_srtp_get_attr(struct ast_sdp_srtp *srtp, int dtls_enabled, int default_taglen_32)
{
	int taglen;

	if (!srtp) {
		return NULL;
	}

	/* Set encryption properties */
	if (!srtp->crypto) {
		if (AST_LIST_NEXT(srtp, sdp_srtp_list)) {
			srtp->crypto = res_sdp_crypto_alloc();
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

			for (i = 0; i < ARRAY_LEN(attr); i++) {
				if (attr[i][0]) {
					ast_set_flag(tmp, attr[i][0]);
				}
				if (attr[i][1]) {
					ast_set_flag(tmp, attr[i][1]);
				}
				tmp->crypto = sdp_crypto_alloc(attr[i][2]); /* key_len */
				tmp->crypto->tag = (i + 1); /* tag starts at 1 */

				if (i < ARRAY_LEN(attr) - 1) {
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

	if (srtp->crypto && (res_sdp_crypto_build_offer(srtp->crypto, taglen) >= 0)) {
		return srtp->crypto->a_crypto;
	}

	ast_log(LOG_WARNING, "No SRTP key management enabled\n");
	return NULL;
}

static struct ast_sdp_crypto_api res_sdp_crypto_api = {
	.dtor = res_sdp_crypto_dtor,
	.alloc = res_sdp_crypto_alloc,
	.build_offer = res_sdp_crypto_build_offer,
	.parse_offer = res_sdp_crypto_parse_offer,
	.get_attr = res_sdp_srtp_get_attr,
};

static void res_srtp_shutdown(void)
{
	ast_sdp_crypto_unregister(&res_sdp_crypto_api);
	ast_rtp_engine_unregister_srtp();
	srtp_install_event_handler(NULL);
#ifdef HAVE_SRTP_SHUTDOWN
	srtp_shutdown();
#endif
	g_initialized = 0;
}

static int res_srtp_init(void)
{
	if (g_initialized) {
		return 0;
	}

	if (srtp_init() != err_status_ok) {
		ast_log(AST_LOG_WARNING, "Failed to initialize libsrtp\n");
		return -1;
	}

	srtp_install_event_handler(srtp_event_cb);

	if (ast_rtp_engine_register_srtp(&srtp_res, &policy_res)) {
		ast_log(AST_LOG_WARNING, "Failed to register SRTP with rtp engine\n");
		res_srtp_shutdown();
		return -1;
	}

	if (ast_sdp_crypto_register(&res_sdp_crypto_api)) {
		ast_log(AST_LOG_WARNING, "Failed to register SDP SRTP crypto API\n");
		res_srtp_shutdown();
		return -1;
	}

#ifdef HAVE_SRTP_GET_VERSION
	ast_verb(2, "%s initialized\n", srtp_get_version_string());
#else
	ast_verb(2, "libsrtp initialized\n");
#endif

	g_initialized = 1;
	return 0;
}

/*
 * Exported functions
 */

static int load_module(void)
{
	return res_srtp_init();
}

static int unload_module(void)
{
	res_srtp_shutdown();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Secure RTP (SRTP)",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
