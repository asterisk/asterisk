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
***/

/* The SIP channel will automatically use sdescriptions if received in a SDP offer,
   and res_srtp is loaded. SRTP with sdescriptions key exchange can be activated
  in outgoing offers by setting _SIPSRTP_CRYPTO=enable in extension.conf before executing Dial

  The dial fails if the callee doesn't support SRTP and sdescriptions.

  exten => 2345,1,Set(_SIPSRTP_CRYPTO=enable)
  exten => 2345,2,Dial(SIP/1001)
*/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <srtp/srtp.h>

#include "asterisk/lock.h"
#include "asterisk/sched.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/rtp_engine.h"

struct ast_srtp {
	struct ast_rtp_instance *rtp;
	srtp_t session;
	const struct ast_srtp_cb *cb;
	void *data;
	unsigned char buf[8192 + AST_FRIENDLY_OFFSET];
	unsigned int has_stream:1;
};

struct ast_srtp_policy {
	srtp_policy_t sp;
};

static int g_initialized = 0;

/* SRTP functions */
static int ast_srtp_create(struct ast_srtp **srtp, struct ast_rtp_instance *rtp, struct ast_srtp_policy *policy);
static void ast_srtp_destroy(struct ast_srtp *srtp);
static int ast_srtp_add_stream(struct ast_srtp *srtp, struct ast_srtp_policy *policy);

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
	.destroy = ast_srtp_destroy,
	.add_stream = ast_srtp_add_stream,
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

static struct ast_srtp *res_srtp_new(void)
{
	struct ast_srtp *srtp;

	if (!(srtp = ast_calloc(1, sizeof(*srtp)))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for srtp\n");
		return NULL;
	}

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

static struct ast_srtp_policy *ast_srtp_policy_alloc()
{
	struct ast_srtp_policy *tmp;

	if (!(tmp = ast_calloc(1, sizeof(*tmp)))) {
		ast_log(LOG_ERROR, "Unable to allocate memory for srtp_policy\n");
	}

	return tmp;
}

static void ast_srtp_policy_destroy(struct ast_srtp_policy *policy)
{
	if (policy->sp.key) {
		ast_free(policy->sp.key);
		policy->sp.key = NULL;
	}
	ast_free(policy);
}

static int policy_set_suite(crypto_policy_t *p, enum ast_srtp_suite suite)
{
	switch (suite) {
	case AST_AES_CM_128_HMAC_SHA1_80:
		p->cipher_type = AES_128_ICM;
		p->cipher_key_len = 30;
		p->auth_type = HMAC_SHA1;
		p->auth_key_len = 20;
		p->auth_tag_len = 10;
		p->sec_serv = sec_serv_conf_and_auth;
		return 0;

	case AST_AES_CM_128_HMAC_SHA1_32:
		p->cipher_type = AES_128_ICM;
		p->cipher_key_len = 30;
		p->auth_type = HMAC_SHA1;
		p->auth_key_len = 20;
		p->auth_tag_len = 4;
		p->sec_serv = sec_serv_conf_and_auth;
		return 0;

	default:
		ast_log(LOG_ERROR, "Invalid crypto suite: %d\n", suite);
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
	return crypto_get_random(key, len) != err_status_ok ? -1: 0;
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
	struct ast_rtp_instance_stats stats = {0,};

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

	if (res != err_status_ok && res != err_status_replay_fail ) {
		ast_debug(1, "SRTP unprotect: %s\n", srtp_errstr(res));
		return -1;
	}

	return *len;
}

static int ast_srtp_protect(struct ast_srtp *srtp, void **buf, int *len, int rtcp)
{
	int res;

	if ((*len + SRTP_MAX_TRAILER_LEN) > sizeof(srtp->buf)) {
		return -1;
	}

	memcpy(srtp->buf, *buf, *len);

	if ((res = rtcp ? srtp_protect_rtcp(srtp->session, srtp->buf, len) : srtp_protect(srtp->session, srtp->buf, len)) != err_status_ok && res != err_status_replay_fail) {
		ast_debug(1, "SRTP protect: %s\n", srtp_errstr(res));
		return -1;
	}

	*buf = srtp->buf;
	return *len;
}

static int ast_srtp_create(struct ast_srtp **srtp, struct ast_rtp_instance *rtp, struct ast_srtp_policy *policy)
{
	struct ast_srtp *temp;

	if (!(temp = res_srtp_new())) {
		return -1;
	}

	if (srtp_create(&temp->session, &policy->sp) != err_status_ok) {
		return -1;
	}

	temp->rtp = rtp;
	*srtp = temp;

	return 0;
}

static void ast_srtp_destroy(struct ast_srtp *srtp)
{
	if (srtp->session) {
		srtp_dealloc(srtp->session);
	}

	ast_free(srtp);
}

static int ast_srtp_add_stream(struct ast_srtp *srtp, struct ast_srtp_policy *policy)
{
	if (!srtp->has_stream && srtp_add_stream(srtp->session, &policy->sp) != err_status_ok) {
		return -1;
	}

	srtp->has_stream = 1;

	return 0;
}

static int res_srtp_init(void)
{
	if (g_initialized) {
		return 0;
	}

	if (srtp_init() != err_status_ok) {
		return -1;
	}

	srtp_install_event_handler(srtp_event_cb);

	return ast_rtp_engine_register_srtp(&srtp_res, &policy_res);
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
	ast_rtp_engine_unregister_srtp();
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS, "Secure RTP (SRTP)",
	.load = load_module,
	.unload = unload_module,
);
