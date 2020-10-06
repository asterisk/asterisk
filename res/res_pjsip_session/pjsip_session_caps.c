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
#include "asterisk/stream.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/res_pjsip_session_caps.h"

static void log_caps(int level, const char *file, int line, const char *function,
	const struct ast_sip_session *session, enum ast_media_type media_type,
	const struct ast_format_cap *local, const struct ast_format_cap *remote,
	const struct ast_format_cap *joint)
{
	struct ast_str *s1;
	struct ast_str *s2;
	struct ast_str *s3;
	int outgoing = session->call_direction == AST_SIP_SESSION_OUTGOING_CALL;
	struct ast_flags pref =
		outgoing
		? session->endpoint->media.outgoing_call_offer_pref
		: session->endpoint->media.incoming_call_offer_pref;

	if (level == __LOG_DEBUG && !DEBUG_ATLEAST(3)) {
		return;
	}

	s1 = local ? ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN) : NULL;
	s2 = remote ? ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN) : NULL;
	s3 = joint ? ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN) : NULL;

	ast_log(level, file, line, function, "'%s' Caps for %s %s call with pref '%s' - remote: %s local: %s joint: %s\n",
		session->channel ? ast_channel_name(session->channel) :
			ast_sorcery_object_get_id(session->endpoint),
		outgoing? "outgoing" : "incoming",
		ast_codec_media_type2str(media_type),
		ast_sip_call_codec_pref_to_str(pref),
		s2 ? ast_format_cap_get_names(remote, &s2) : "(NONE)",
		s1 ? ast_format_cap_get_names(local, &s1) : "(NONE)",
		s3 ? ast_format_cap_get_names(joint, &s3) : "(NONE)");
}

struct ast_format_cap *ast_sip_create_joint_call_cap(const struct ast_format_cap *remote,
	struct ast_format_cap *local, enum ast_media_type media_type,
	struct ast_flags codec_pref)
{
	struct ast_format_cap *joint = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	struct ast_format_cap *local_filtered = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	struct ast_format_cap *remote_filtered = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	if (!joint || !local_filtered || !remote_filtered) {
		ast_log(LOG_ERROR, "Failed to allocate %s call offer capabilities\n",
				ast_codec_media_type2str(media_type));
		ao2_cleanup(joint);
		ao2_cleanup(local_filtered);
		ao2_cleanup(remote_filtered);
		return NULL;
	}

	ast_format_cap_append_from_cap(local_filtered, local, media_type);

	/* Remote should always be a subset of local, as local is what defines the underlying
	 * permitted formats.
	 */
	ast_format_cap_get_compatible(remote, local_filtered, remote_filtered);

	if (ast_sip_call_codec_pref_test(codec_pref, LOCAL)) {
		if (ast_sip_call_codec_pref_test(codec_pref, INTERSECT)) {
			ast_format_cap_get_compatible(local_filtered, remote_filtered, joint); /* Get common, prefer local */
		} else {
			ast_format_cap_append_from_cap(joint, local_filtered, media_type); /* Add local */
			ast_format_cap_append_from_cap(joint, remote_filtered, media_type); /* Then remote */
		}
	} else {
		if (ast_sip_call_codec_pref_test(codec_pref, INTERSECT)) {
			joint = remote_filtered; /* Get common, prefer remote - as was done when filtering initially */
			remote_filtered = NULL;
		} else {
			ast_format_cap_append_from_cap(joint, remote_filtered, media_type); /* Add remote */
			ast_format_cap_append_from_cap(joint, local_filtered, media_type); /* Then local */
		}
	}

	ao2_ref(local_filtered, -1);
	ao2_cleanup(remote_filtered);

	if (ast_format_cap_empty(joint)) {
		return joint;
	}

	if (ast_sip_call_codec_pref_test(codec_pref, FIRST)) {
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

	return joint;
}

struct ast_stream *ast_sip_session_create_joint_call_stream(const struct ast_sip_session *session,
	struct ast_stream *remote_stream)
{
	struct ast_stream *joint_stream = ast_stream_clone(remote_stream, NULL);
	const struct ast_format_cap *remote = ast_stream_get_formats(remote_stream);
	enum ast_media_type media_type = ast_stream_get_type(remote_stream);

	struct ast_format_cap *joint = ast_sip_create_joint_call_cap(remote,
		session->endpoint->media.codecs, media_type,
		session->call_direction == AST_SIP_SESSION_OUTGOING_CALL
				? session->endpoint->media.outgoing_call_offer_pref
				: session->endpoint->media.incoming_call_offer_pref);

	ast_stream_set_formats(joint_stream, joint);
	ao2_cleanup(joint);

	log_caps(LOG_DEBUG, session, media_type, session->endpoint->media.codecs, remote, joint);

	return joint_stream;
}

struct ast_format_cap *ast_sip_session_create_joint_call_cap(
	const struct ast_sip_session *session, enum ast_media_type media_type,
	const struct ast_format_cap *remote)
{
	struct ast_format_cap *joint = ast_sip_create_joint_call_cap(remote,
		session->endpoint->media.codecs, media_type,
		session->call_direction == AST_SIP_SESSION_OUTGOING_CALL
				? session->endpoint->media.outgoing_call_offer_pref
				: session->endpoint->media.incoming_call_offer_pref);

	log_caps(LOG_DEBUG, session, media_type, session->endpoint->media.codecs, remote, joint);

	return joint;
}
