/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2020, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@digium.com>
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

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/channel.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/logger.h"
#include "asterisk/sorcery.h"

#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/res_pjsip_session_caps.h"

struct ast_sip_session_caps {
	struct ast_format_cap *incoming_call_offer_cap;
};

static void log_caps(int level, const char *file, int line, const char *function,
	const char *msg, const struct ast_sip_session *session,
	const struct ast_sip_session_media *session_media, const struct ast_format_cap *local,
	const struct ast_format_cap *remote, const struct ast_format_cap *joint)
{
	struct ast_str *s1;
	struct ast_str *s2;
	struct ast_str *s3;

	if (level == __LOG_DEBUG && !DEBUG_ATLEAST(3)) {
		return;
	}

	s1 = local ? ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN) : NULL;
	s2 = remote ? ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN) : NULL;
	s3 = joint ? ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN) : NULL;

	ast_log(level, file, line, function, "'%s' %s '%s' capabilities -%s%s%s%s%s%s\n",
		session->channel ? ast_channel_name(session->channel) :
			ast_sorcery_object_get_id(session->endpoint),
		msg ? msg : "-", ast_codec_media_type2str(session_media->type),
		s1 ? " local: " : "", s1 ? ast_format_cap_get_names(local, &s1) : "",
		s2 ? " remote: " : "", s2 ? ast_format_cap_get_names(remote, &s2) : "",
		s3 ? " joint: " : "", s3 ? ast_format_cap_get_names(joint, &s3) : "");
}

static void sip_session_caps_destroy(void *obj)
{
	struct ast_sip_session_caps *caps = obj;

	ao2_cleanup(caps->incoming_call_offer_cap);
}

struct ast_sip_session_caps *ast_sip_session_caps_alloc(void)
{
	return ao2_alloc_options(sizeof(struct ast_sip_session_caps),
		sip_session_caps_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
}

void ast_sip_session_set_incoming_call_offer_cap(struct ast_sip_session_caps *caps,
	struct ast_format_cap *cap)
{
	ao2_cleanup(caps->incoming_call_offer_cap);
	caps->incoming_call_offer_cap = ao2_bump(cap);
}

const struct ast_format_cap *ast_sip_session_get_incoming_call_offer_cap(
	const struct ast_sip_session_caps *caps)
{
	return caps->incoming_call_offer_cap;
}

const struct ast_format_cap *ast_sip_session_join_incoming_call_offer_cap(
	const struct ast_sip_session *session, const struct ast_sip_session_media *session_media,
	const struct ast_format_cap *remote)
{
	enum ast_sip_call_codec_pref pref;
	struct ast_format_cap *joint;
	struct ast_format_cap *local;

	joint = session_media->caps->incoming_call_offer_cap;

	if (joint) {
		/*
		 * If the incoming call offer capabilities have been set elsewhere, e.g. dialplan
		 * then those take precedence.
		 */
		return joint;
	}

	joint = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	local = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	if (!joint || !local) {
		ast_log(LOG_ERROR, "Failed to allocate %s incoming call offer capabilities\n",
				ast_codec_media_type2str(session_media->type));

		ao2_cleanup(joint);
		ao2_cleanup(local);
		return NULL;
	}

	pref = session->endpoint->media.incoming_call_offer_pref;
	ast_format_cap_append_from_cap(local, session->endpoint->media.codecs,
		session_media->type);

	if (pref < AST_SIP_CALL_CODEC_PREF_REMOTE) {
		ast_format_cap_get_compatible(local, remote, joint); /* Prefer local */
	} else {
		ast_format_cap_get_compatible(remote, local, joint); /* Prefer remote */
	}

	if (ast_format_cap_empty(joint)) {
		log_caps(LOG_NOTICE, "No joint incoming", session, session_media, local, remote, NULL);

		ao2_ref(joint, -1);
		ao2_ref(local, -1);
		return NULL;
	}

	if (pref == AST_SIP_CALL_CODEC_PREF_LOCAL_SINGLE ||
		pref == AST_SIP_CALL_CODEC_PREF_REMOTE_SINGLE ||
		session->endpoint->preferred_codec_only) {

		/*
		 * Save the most preferred one. Session capabilities are per stream and
		 * a stream only carries a single media type, so no reason to worry with
		 * the type here (i.e different or multiple types)
		 */
		struct ast_format *single = ast_format_cap_get_format(joint, 0);
		/* Remove all formats */
		ast_format_cap_remove_by_type(joint, AST_MEDIA_TYPE_UNKNOWN);
		/* Put the most preferred one back */
		ast_format_cap_append(joint, single, 0);
		ao2_ref(single, -1);
	}

	log_caps(LOG_DEBUG, "Joint incoming", session, session_media, local, remote, joint);

	ao2_ref(local, -1);

	ast_sip_session_set_incoming_call_offer_cap(session_media->caps, joint);

	return joint;
}
