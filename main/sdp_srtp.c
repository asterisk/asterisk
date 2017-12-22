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

#include "asterisk/linkedlists.h"       /* for AST_LIST_NEXT, etc */
#include "asterisk/logger.h"            /* for ast_log, LOG_ERROR, etc */
#include "asterisk/sdp_srtp.h"          /* for ast_sdp_srtp, etc */

/*! Registered SDP crypto API */
static struct ast_sdp_crypto_api *sdp_crypto_api;

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
		ast_sdp_crypto_destroy(srtp->crypto);
		srtp->crypto = NULL;
		ast_free(srtp);
	}
}

void ast_sdp_crypto_destroy(struct ast_sdp_crypto *crypto)
{
	if (sdp_crypto_api) {
		sdp_crypto_api->dtor(crypto);
	}
}

struct ast_sdp_crypto *ast_sdp_crypto_alloc(void)
{
	if (!sdp_crypto_api) {
		return NULL;
	}
	return sdp_crypto_api->alloc();
}

int ast_sdp_crypto_process(struct ast_rtp_instance *rtp, struct ast_sdp_srtp *srtp, const char *attr)
{
	if (!sdp_crypto_api) {
		return -1;
	}
	return sdp_crypto_api->parse_offer(rtp, srtp, attr);
}

int ast_sdp_crypto_build_offer(struct ast_sdp_crypto *p, int taglen)
{
	if (!sdp_crypto_api) {
		return -1;
	}
	return sdp_crypto_api->build_offer(p, taglen);
}

const char *ast_sdp_srtp_get_attrib(struct ast_sdp_srtp *srtp, int dtls_enabled, int default_taglen_32)
{
	if (!sdp_crypto_api) {
		return NULL;
	}
	return sdp_crypto_api->get_attr(srtp, dtls_enabled, default_taglen_32);
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

int ast_sdp_crypto_register(struct ast_sdp_crypto_api *api)
{
	if (sdp_crypto_api) {
		return -1;
	}
	sdp_crypto_api = api;
	return 0;
}

void ast_sdp_crypto_unregister(struct ast_sdp_crypto_api *api)
{
	if (sdp_crypto_api == api) {
		sdp_crypto_api = NULL;
	}
}
