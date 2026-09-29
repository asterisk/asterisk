/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

/*!
 * \file
 *
 * \author \verbatim Joshua Colp <jcolp@digium.com> \endverbatim
 * \author \verbatim Matt Jordan <mjordan@digium.com> \endverbatim
 *
 * \ingroup functions
 *
 * \brief PJSIP channel dialplan functions
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjlib.h>
#include <pjsip_ua.h>

#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/acl.h"
#include "asterisk/app.h"
#include "asterisk/conversions.h"
#include "asterisk/channel.h"
#include "asterisk/stream.h"
#include "asterisk/format.h"
#include "asterisk/dsp.h"
#include "asterisk/pbx.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "include/chan_pjsip.h"
#include "include/channel_info.h"
#include "include/dialplan_functions.h"

/*!
 * \brief String representations of the T.38 state enum
 */
static const char *t38state_to_string[T38_MAX_ENUM] = {
	[T38_DISABLED] = "DISABLED",
	[T38_LOCAL_REINVITE] = "LOCAL_REINVITE",
	[T38_PEER_REINVITE] = "REMOTE_REINVITE",
	[T38_ENABLED] = "ENABLED",
	[T38_REJECTED] = "REJECTED",
};

/*!
 * \internal \brief Escape a captured URI into the dialplan result buffer
 */
static int copy_escaped_uri(struct ast_channel *chan, const char *type,
	const struct pjsip_channel_uri *uri, char *buf, size_t size)
{
	if (uri->length < 0 || uri->length >= size) {
		ast_log(LOG_ERROR, "Channel %s: Unescaped %s too long for %zu byte buffer\n",
			ast_channel_name(chan), type, size);
		buf[0] = '\0';
		return -1;
	}
	ast_escape_quoted(uri->value, buf, size);
	return 0;
}

/*!
 * \internal \brief Handle reading RTP information
 */
static int channel_read_rtp(struct ast_channel *chan, struct ast_rtp_instance *rtp,
	const struct ast_sockaddr *direct_media_addr, unsigned int secure,
	unsigned int held, const char *type, const char *field, char *buf, size_t buflen)
{
	struct ast_sockaddr addr;

	if (ast_strlen_zero(type)) {
		ast_log(AST_LOG_WARNING, "You must supply a type field for 'rtp' information\n");
		return -1;
	}

	if (!ast_strlen_zero(field) && strcmp(field, "audio") && strcmp(field, "video")) {
		ast_log(AST_LOG_WARNING, "Unknown media type field '%s' for 'rtp' information\n", field);
		return -1;
	}

	if (!rtp) {
		ast_log(AST_LOG_WARNING, "Channel %s has no %s media/RTP session\n",
			ast_channel_name(chan), S_OR(field, "audio"));
		return -1;
	}

	if (!strcmp(type, "src")) {
		ast_rtp_instance_get_local_address(rtp, &addr);
		ast_copy_string(buf, ast_sockaddr_stringify(&addr), buflen);
	} else if (!strcmp(type, "dest")) {
		ast_rtp_instance_get_remote_address(rtp, &addr);
		ast_copy_string(buf, ast_sockaddr_stringify(&addr), buflen);
	} else if (!strcmp(type, "direct")) {
		ast_copy_string(buf, ast_sockaddr_stringify(direct_media_addr), buflen);
	} else if (!strcmp(type, "secure")) {
		snprintf(buf, buflen, "%u", secure);
	} else if (!strcmp(type, "hold")) {
		snprintf(buf, buflen, "%u", held);
	} else {
		ast_log(AST_LOG_WARNING, "Unknown type field '%s' specified for 'rtp' information\n", type);
		return -1;
	}

	return 0;
}

/*!
 * \internal \brief Handle reading RTCP information
 */
static int channel_read_rtcp(struct ast_channel *chan, struct ast_rtp_instance *rtp,
	const char *type, const char *field, char *buf, size_t buflen)
{
	if (ast_strlen_zero(type)) {
		ast_log(AST_LOG_WARNING, "You must supply a type field for 'rtcp' information\n");
		return -1;
	}

	if (!ast_strlen_zero(field) && strcmp(field, "audio") && strcmp(field, "video")) {
		ast_log(AST_LOG_WARNING, "Unknown media type field '%s' for 'rtcp' information\n", field);
		return -1;
	}

	if (!rtp) {
		ast_log(AST_LOG_WARNING, "Channel %s has no %s media/RTP session\n",
			ast_channel_name(chan), S_OR(field, "audio"));
		return -1;
	}

	if (!strncasecmp(type, "all", 3)) {
		enum ast_rtp_instance_stat_field stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY;

		if (!strcasecmp(type, "all_jitter")) {
			stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY_JITTER;
		} else if (!strcasecmp(type, "all_rtt")) {
			stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY_RTT;
		} else if (!strcasecmp(type, "all_loss")) {
			stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY_LOSS;
		} else if (!strcasecmp(type, "all_mes")) {
			stat_field = AST_RTP_INSTANCE_STAT_FIELD_QUALITY_MES;
		}

		if (!ast_rtp_instance_get_quality(rtp, stat_field, buf, buflen)) {
			ast_log(AST_LOG_WARNING, "Unable to retrieve 'rtcp' statistics for %s\n", ast_channel_name(chan));
			return -1;
		}
	} else {
		struct ast_rtp_instance_stats stats;
		int i;
		struct {
			const char *name;
			enum { INT, DBL } type;
			union {
				unsigned int *i4;
				double *d8;
			};
		} lookup[] = {
			{ "txcount",               INT, { .i4 = &stats.txcount, }, },
			{ "rxcount",               INT, { .i4 = &stats.rxcount, }, },
			{ "txjitter",              DBL, { .d8 = &stats.txjitter, }, },
			{ "rxjitter",              DBL, { .d8 = &stats.rxjitter, }, },
			{ "remote_maxjitter",      DBL, { .d8 = &stats.remote_maxjitter, }, },
			{ "remote_minjitter",      DBL, { .d8 = &stats.remote_minjitter, }, },
			{ "remote_normdevjitter",  DBL, { .d8 = &stats.remote_normdevjitter, }, },
			{ "remote_stdevjitter",    DBL, { .d8 = &stats.remote_stdevjitter, }, },
			{ "local_maxjitter",       DBL, { .d8 = &stats.local_maxjitter, }, },
			{ "local_minjitter",       DBL, { .d8 = &stats.local_minjitter, }, },
			{ "local_normdevjitter",   DBL, { .d8 = &stats.local_normdevjitter, }, },
			{ "local_stdevjitter",     DBL, { .d8 = &stats.local_stdevjitter, }, },
			{ "txploss",               INT, { .i4 = &stats.txploss, }, },
			{ "rxploss",               INT, { .i4 = &stats.rxploss, }, },
			{ "remote_maxrxploss",     DBL, { .d8 = &stats.remote_maxrxploss, }, },
			{ "remote_minrxploss",     DBL, { .d8 = &stats.remote_minrxploss, }, },
			{ "remote_normdevrxploss", DBL, { .d8 = &stats.remote_normdevrxploss, }, },
			{ "remote_stdevrxploss",   DBL, { .d8 = &stats.remote_stdevrxploss, }, },
			{ "local_maxrxploss",      DBL, { .d8 = &stats.local_maxrxploss, }, },
			{ "local_minrxploss",      DBL, { .d8 = &stats.local_minrxploss, }, },
			{ "local_normdevrxploss",  DBL, { .d8 = &stats.local_normdevrxploss, }, },
			{ "local_stdevrxploss",    DBL, { .d8 = &stats.local_stdevrxploss, }, },
			{ "rtt",                   DBL, { .d8 = &stats.rtt, }, },
			{ "maxrtt",                DBL, { .d8 = &stats.maxrtt, }, },
			{ "minrtt",                DBL, { .d8 = &stats.minrtt, }, },
			{ "normdevrtt",            DBL, { .d8 = &stats.normdevrtt, }, },
			{ "stdevrtt",              DBL, { .d8 = &stats.stdevrtt, }, },
			{ "local_ssrc",            INT, { .i4 = &stats.local_ssrc, }, },
			{ "remote_ssrc",           INT, { .i4 = &stats.remote_ssrc, }, },
			{ "txmes",                 DBL, { .d8 = &stats.txmes, }, },
			{ "rxmes",                 DBL, { .d8 = &stats.rxmes, }, },
			{ "remote_maxmes",         DBL, { .d8 = &stats.remote_maxmes, }, },
			{ "remote_minmes",         DBL, { .d8 = &stats.remote_minmes, }, },
			{ "remote_normdevmes",     DBL, { .d8 = &stats.remote_normdevmes, }, },
			{ "remote_stdevmes",       DBL, { .d8 = &stats.remote_stdevmes, }, },
			{ "local_maxmes",          DBL, { .d8 = &stats.local_maxmes, }, },
			{ "local_minmes",          DBL, { .d8 = &stats.local_minmes, }, },
			{ "local_normdevmes",      DBL, { .d8 = &stats.local_normdevmes, }, },
			{ "local_stdevmes",        DBL, { .d8 = &stats.local_stdevmes, }, },
			{ NULL, },
		};

		if (ast_rtp_instance_get_stats(rtp, &stats, AST_RTP_INSTANCE_STAT_ALL)) {
			ast_log(AST_LOG_WARNING, "Unable to retrieve 'rtcp' statistics for %s\n", ast_channel_name(chan));
			return -1;
		}

		for (i = 0; !ast_strlen_zero(lookup[i].name); i++) {
			if (!strcasecmp(type, lookup[i].name)) {
				if (lookup[i].type == INT) {
					snprintf(buf, buflen, "%u", *lookup[i].i4);
				} else {
					snprintf(buf, buflen, "%f", *lookup[i].d8);
				}
				return 0;
			}
		}
		ast_log(AST_LOG_WARNING, "Unrecognized argument '%s' for 'rtcp' information\n", type);
		return -1;
	}

	return 0;
}

/* The original Request-URI is cloned before channel creation and never changed. */
static int read_request_uri(struct ast_channel *chan, struct ast_sip_session *session,
	char *buf, size_t size)
{
	int length;
	char *copy;

	if (!session->request_uri) {
		return 0;
	}
	length = pjsip_uri_print(PJSIP_URI_IN_REQ_URI, session->request_uri, buf, size);
	if (length < 0 || length >= size) {
		ast_log(LOG_ERROR, "Channel %s: Request URI too long for %zu byte buffer\n",
			ast_channel_name(chan), size);
		buf[0] = '\0';
		return -1;
	}
	buf[length] = '\0';
	copy = ast_strdupa(buf);
	ast_escape_quoted(copy, buf, size);
	return 0;
}

/* The caller holds the channel lock. No dialog lock or serializer wait is allowed. */
static int channel_read_pjsip(struct ast_channel *chan, struct ast_sip_session *session,
	const char *type, char *buf, size_t len)
{
	const struct pjsip_channel_info *info;

	if (ast_strlen_zero(type)) {
		ast_log(LOG_WARNING, "You must supply a type field for 'pjsip' information\n");
		return -1;
	}

	if (!strcmp(type, "call-id") || !strcmp(type, "local_tag")) {
		pjsip_dialog *dlg = session->inv_session ? session->inv_session->dlg : NULL;

		if (!dlg) {
			return -1;
		}
		/* These identities are fixed for the lifetime of the retained dialog. */
		if (!strcmp(type, "call-id")) {
			ast_copy_pj_str(buf, &dlg->call_id->id, len);
		} else {
			char *copy;

			ast_copy_pj_str(buf, &dlg->local.info->tag, len);
			copy = ast_strdupa(buf);
			ast_escape_quoted(copy, buf, len);
		}
		return 0;
	}
	if (!strcmp(type, "request_uri")) {
		return read_request_uri(chan, session, buf, len);
	}
	if (!strcmp(type, "t38state")) {
		if (session->t38state < 0 || session->t38state >= T38_MAX_ENUM) {
			return -1;
		}
		ast_copy_string(buf, t38state_to_string[session->t38state], len);
		return 0;
	}
	if (!strcmp(type, "local_addr") || !strcmp(type, "remote_addr")) {
		RAII_VAR(struct ast_datastore *, datastore,
			ast_sip_session_get_datastore(session, "transport_info"), ao2_cleanup);
		struct transport_info_data *transport;
		const pj_sockaddr *addr;

		if (!datastore) {
			ast_log(LOG_WARNING, "No transport information for channel %s\n",
				ast_channel_name(chan));
			return -1;
		}
		/* The transport payload is fully initialized before publication and immutable. */
		transport = datastore->data;
		addr = !strcmp(type, "local_addr") ? &transport->local_addr : &transport->remote_addr;
		if (pj_sockaddr_has_addr(addr)) {
			pj_sockaddr_print(addr, buf, len, 3);
		}
		return 0;
	}

	info = pjsip_channel_info_get(chan);
	if (!info) {
		ast_log(LOG_WARNING, "Channel %s has no PJSIP channel information\n",
			ast_channel_name(chan));
		return -1;
	}
	if (!strcmp(type, "secure")) {
#ifdef HAVE_PJSIP_GET_DEST_INFO
		if (info->secure < 0) {
			return -1;
		}
		snprintf(buf, len, "%d", info->secure);
#else
		ast_log(LOG_WARNING, "Asterisk was built without support for CHANNEL(pjsip,secure)\n");
		return -1;
#endif
	} else if (!strcmp(type, "target_uri")) {
		return copy_escaped_uri(chan, type, &info->target_uri, buf, len);
	} else if (!strcmp(type, "local_uri")) {
		return copy_escaped_uri(chan, type, &info->local_uri, buf, len);
	} else if (!strcmp(type, "remote_uri")) {
		return copy_escaped_uri(chan, type, &info->remote_uri, buf, len);
	} else if (!strcmp(type, "remote_tag")) {
		if (!info->remote_tag) {
			return -1;
		}
		ast_escape_quoted(info->remote_tag, buf, len);
	} else {
		ast_log(LOG_WARNING, "Unrecognized argument '%s' for 'pjsip' information\n", type);
		return -1;
	}
	return 0;
}

int pjsip_acf_channel_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel;
	char *parse = ast_strdupa(data);
	int res = 0;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(param);
		AST_APP_ARG(type);
		AST_APP_ARG(field);
	);

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	/* Check for zero arguments */
	if (ast_strlen_zero(parse)) {
		ast_log(LOG_ERROR, "Cannot call %s without arguments\n", cmd);
		return -1;
	}

	if (!len) {
		ast_log(LOG_ERROR, "Cannot call %s without an output buffer\n", cmd);
		return -1;
	}

	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_lock(chan);

	/* Sanity check */
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return 0;
	}

	channel = ast_channel_tech_pvt(chan);
	if (!channel) {
		ast_log(LOG_WARNING, "Channel %s has no pvt!\n", ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	if (!channel->session) {
		ast_log(LOG_WARNING, "Channel %s has no session\n", ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	memset(buf, 0, len);

	if (!strcmp(args.param, "endpoint")) {
		if (!channel->session->endpoint) {
			res = -1;
		} else {
			ast_copy_string(buf, ast_sorcery_object_get_id(channel->session->endpoint), len);
		}
	} else if (!strcmp(args.param, "contact")) {
		if (channel->session->contact) {
			ast_copy_string(buf, ast_sorcery_object_get_id(channel->session->contact), len);
		}
	} else if (!strcmp(args.param, "aor")) {
		if (channel->session->aor) {
			ast_copy_string(buf, ast_sorcery_object_get_id(channel->session->aor), len);
		}
	} else if (!strcmp(args.param, "pjsip")) {
		res = channel_read_pjsip(chan, channel->session, args.type, buf, len);
	} else if (!strcmp(args.param, "rtp") || !strcmp(args.param, "rtcp")) {
		const struct pjsip_channel_info *info = pjsip_channel_info_get(chan);
		struct ast_rtp_instance *rtp;
		struct ast_sip_session_media *media;
		struct ast_sockaddr no_address = { { 0, } };
		const struct ast_sockaddr *direct_media_addr;
		unsigned int secure;
		unsigned int held;
		int video = !strcmp(S_OR(args.field, "audio"), "video");

		if (!info) {
			res = -1;
		} else {
			/* The channel lock keeps these retained instances alive throughout the read. */
			rtp = video ? info->video_rtp : info->audio_rtp;
			media = channel->session->active_media_state
				? channel->session->active_media_state->default_session[video
					? AST_MEDIA_TYPE_VIDEO : AST_MEDIA_TYPE_AUDIO] : NULL;
			direct_media_addr = media ? &media->direct_media_addr : &no_address;
			secure = video ? info->video_secure : info->audio_secure;
			held = video ? info->video_held : info->audio_held;
			if (!strcmp(args.param, "rtp")) {
				res = channel_read_rtp(chan, rtp, direct_media_addr, secure, held,
					args.type, args.field, buf, len);
			} else {
				res = channel_read_rtcp(chan, rtp, args.type, args.field, buf, len);
			}
		}
	} else {
		res = -1;
	}
	ast_channel_unlock(chan);
	return res;
}

int pjsip_acf_dial_contacts_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, dial, NULL, ast_free_ptr);
	const char *aor_name;
	char *rest;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(endpoint_name);
		AST_APP_ARG(aor_name);
		AST_APP_ARG(request_user);
	);

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.endpoint_name)) {
		ast_log(LOG_WARNING, "An endpoint name must be specified when using the '%s' dialplan function\n", cmd);
		return -1;
	} else if (!(endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", args.endpoint_name))) {
		ast_log(LOG_WARNING, "Specified endpoint '%s' was not found\n", args.endpoint_name);
		return -1;
	}

	aor_name = S_OR(args.aor_name, endpoint->aors);

	if (ast_strlen_zero(aor_name)) {
		ast_log(LOG_WARNING, "No AOR has been provided and no AORs are configured on endpoint '%s'\n", args.endpoint_name);
		return -1;
	} else if (!(dial = ast_str_create(len))) {
		ast_log(LOG_WARNING, "Could not get enough buffer space for dialing contacts\n");
		return -1;
	} else if (!(rest = ast_strdupa(aor_name))) {
		ast_log(LOG_WARNING, "Could not duplicate provided AORs\n");
		return -1;
	}

	while ((aor_name = ast_strip(strsep(&rest, ",")))) {
		RAII_VAR(struct ast_sip_aor *, aor, ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);
		struct ao2_iterator it_contacts;
		struct ast_sip_contact *contact;

		if (!aor) {
			/* If the AOR provided is not found skip it, there may be more */
			continue;
		} else if (!(contacts = ast_sip_location_retrieve_aor_contacts_filtered(aor, AST_SIP_CONTACT_FILTER_REACHABLE))) {
			/* No contacts are available, skip it as well */
			continue;
		} else if (!ao2_container_count(contacts)) {
			/* We were given a container but no contacts are in it... */
			continue;
		}

		it_contacts = ao2_iterator_init(contacts, 0);
		for (; (contact = ao2_iterator_next(&it_contacts)); ao2_ref(contact, -1)) {
			ast_str_append(&dial, -1, "PJSIP/");

			if (!ast_strlen_zero(args.request_user)) {
				ast_str_append(&dial, -1, "%s@", args.request_user);
			}
			ast_str_append(&dial, -1, "%s/%s&", args.endpoint_name, contact->uri);
		}
		ao2_iterator_destroy(&it_contacts);
	}

	/* Trim the '&' at the end off */
	ast_str_truncate(dial, ast_str_strlen(dial) - 1);

	ast_copy_string(buf, ast_str_buffer(dial), len);

	return 0;
}

/*! \brief Session refresh state information */
struct session_refresh_state {
	/*! \brief Created proposed media state */
	struct ast_sip_session_media_state *media_state;
};

/*! \brief Destructor for session refresh information */
static void session_refresh_state_destroy(void *obj)
{
	struct session_refresh_state *state = obj;

	ast_sip_session_media_state_free(state->media_state);
	ast_free(obj);
}

/*! \brief Datastore for attaching session refresh state information */
static const struct ast_datastore_info session_refresh_datastore = {
	.type = "pjsip_session_refresh",
	.destroy = session_refresh_state_destroy,
};

/*! \brief Helper function which retrieves or allocates a session refresh state information datastore */
static struct session_refresh_state *session_refresh_state_get_or_alloc(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_session_get_datastore(session, "pjsip_session_refresh"), ao2_cleanup);
	struct session_refresh_state *state;

	/* While the datastore refcount is decremented this is operating in the serializer so it will remain valid regardless */
	if (datastore) {
		return datastore->data;
	}

	if (!(datastore = ast_sip_session_alloc_datastore(&session_refresh_datastore, "pjsip_session_refresh"))
		|| !(datastore->data = ast_calloc(1, sizeof(struct session_refresh_state)))
		|| ast_sip_session_add_datastore(session, datastore)) {
		return NULL;
	}

	state = datastore->data;
	state->media_state = ast_sip_session_media_state_alloc();
	if (!state->media_state) {
		ast_sip_session_remove_datastore(session, "pjsip_session_refresh");
		return NULL;
	}
	state->media_state->topology = ast_stream_topology_clone(session->endpoint->media.topology);
	if (!state->media_state->topology) {
		ast_sip_session_remove_datastore(session, "pjsip_session_refresh");
		return NULL;
	}

	datastore->data = state;

	return state;
}

/*! \brief Struct used to push PJSIP_PARSE_URI function arguments to task processor */
struct parse_uri_args {
	const char *uri;
	const char *type;
	char *buf;
	size_t buflen;
	int ret;
};

/*! \internal \brief Taskprocessor callback that handles the PJSIP_PARSE_URI on a PJSIP thread */
static int parse_uri_cb(void *data)
{
	struct parse_uri_args *args = data;
	pj_pool_t *pool;
	pjsip_name_addr *uri;
	pjsip_sip_uri *sip_uri;
	pj_str_t tmp;

	args->ret = 0;

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "ParseUri", 128, 128);
	if (!pool) {
		ast_log(LOG_ERROR, "Failed to allocate ParseUri endpoint pool.\n");
		args->ret = -1;
		return 0;
	}

	pj_strdup2_with_null(pool, &tmp, args->uri);
	uri = (pjsip_name_addr *)pjsip_parse_uri(pool, tmp.ptr, tmp.slen, PJSIP_PARSE_URI_AS_NAMEADDR);
	if (!uri || (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		ast_log(LOG_WARNING, "Failed to parse URI '%s'\n", args->uri);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		args->ret = -1;
		return 0;
	}

	if (!strcmp(args->type, "scheme")) {
		ast_copy_pj_str(args->buf, pjsip_uri_get_scheme(uri), args->buflen);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return 0;
	} else if (!strcmp(args->type, "display")) {
		ast_copy_pj_str(args->buf, &uri->display, args->buflen);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return 0;
	}

	sip_uri = pjsip_uri_get_uri(uri);
	if (!sip_uri) {
		ast_log(LOG_ERROR, "Failed to get an URI object for '%s'\n", args->uri);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		args->ret = -1;
		return 0;
	}

	if (!strcmp(args->type, "user")) {
		ast_copy_pj_str(args->buf, &sip_uri->user, args->buflen);
	} else if (!strcmp(args->type, "passwd")) {
		ast_copy_pj_str(args->buf, &sip_uri->passwd, args->buflen);
	} else if (!strcmp(args->type, "host")) {
		ast_copy_pj_str(args->buf, &sip_uri->host, args->buflen);
	} else if (!strcmp(args->type, "port")) {
		snprintf(args->buf, args->buflen, "%d", sip_uri->port);
	} else if (!strcmp(args->type, "user_param")) {
		ast_copy_pj_str(args->buf, &sip_uri->user_param, args->buflen);
	} else if (!strcmp(args->type, "method_param")) {
		ast_copy_pj_str(args->buf, &sip_uri->method_param, args->buflen);
	} else if (!strcmp(args->type, "transport_param")) {
		ast_copy_pj_str(args->buf, &sip_uri->transport_param, args->buflen);
	} else if (!strcmp(args->type, "ttl_param")) {
		snprintf(args->buf, args->buflen, "%d", sip_uri->ttl_param);
	} else if (!strcmp(args->type, "lr_param")) {
		snprintf(args->buf, args->buflen, "%d", sip_uri->lr_param);
	} else if (!strcmp(args->type, "maddr_param")) {
		ast_copy_pj_str(args->buf, &sip_uri->maddr_param, args->buflen);
	} else {
		ast_log(AST_LOG_WARNING, "Unknown type part '%s' specified\n", args->type);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		args->ret = -1;
		return 0;
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);

	return 0;
}

int pjsip_acf_parse_uri_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t buflen)
{
	struct parse_uri_args func_args = { 0, };
	int reading_uri_from_var;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(uri_str);
		AST_APP_ARG(type);
	);

	AST_STANDARD_APP_ARGS(args, data);

	reading_uri_from_var = !strcasecmp(cmd, "PJSIP_PARSE_URI_FROM");

	if (reading_uri_from_var) {
		const char *var;

		if (ast_strlen_zero(args.uri_str)) {
			ast_log(LOG_WARNING, "The name of a variable containing a URI must be specified when using the '%s' dialplan function\n", cmd);
			return -1;
		}

		ast_channel_lock(chan);
		if ((var = pbx_builtin_getvar_helper(chan, args.uri_str))) {
			args.uri_str = ast_strdupa(var);
		}
		ast_channel_unlock(chan);
	}

	if (ast_strlen_zero(args.uri_str)) {
		if (reading_uri_from_var) {
			ast_log(LOG_WARNING, "The variable provided to the '%s' dialplan function must contain a URI\n", cmd);
		} else {
			ast_log(LOG_WARNING, "A URI must be specified when using the '%s' dialplan function\n", cmd);
		}
		return -1;
	}

	if (ast_strlen_zero(args.type)) {
		ast_log(LOG_WARNING, "A type part of the URI must be specified when using the '%s' dialplan function\n", cmd);
		return -1;
	}

	memset(buf, 0, buflen);

	func_args.uri = args.uri_str;
	func_args.type = args.type;
	func_args.buf = buf;
	func_args.buflen = buflen;
	if (ast_sip_push_task_wait_serializer(NULL, parse_uri_cb, &func_args)) {
		ast_log(LOG_WARNING, "Unable to parse URI: failed to push task\n");
		return -1;
	}

	return func_args.ret;
}

static int media_offer_read_av(struct ast_sip_session *session, char *buf,
			       size_t len, enum ast_media_type media_type)
{
	struct ast_stream_topology *topology;
	int idx;
	struct ast_stream *stream = NULL;
	const struct ast_format_cap *caps;
	size_t accum = 0;

	if (session->inv_session->dlg->state == PJSIP_DIALOG_STATE_ESTABLISHED) {
		struct session_refresh_state *state;

		/* As we've already answered we need to store our media state until we are ready to send it */
		state = session_refresh_state_get_or_alloc(session);
		if (!state) {
			return -1;
		}
		topology = state->media_state->topology;
	} else {
		/* The session is not yet up so we are initially answering or offering */
		if (!session->pending_media_state->topology) {
			session->pending_media_state->topology = ast_stream_topology_clone(session->endpoint->media.topology);
			if (!session->pending_media_state->topology) {
				return -1;
			}
		}
		topology = session->pending_media_state->topology;
	}

	/* Find the first suitable stream */
	for (idx = 0; idx < ast_stream_topology_get_count(topology); ++idx) {
		stream = ast_stream_topology_get_stream(topology, idx);

		if (ast_stream_get_type(stream) != media_type ||
			ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
			stream = NULL;
			continue;
		}

		break;
	}

	/* If no suitable stream then exit early */
	if (!stream) {
		buf[0] = '\0';
		return 0;
	}

	caps = ast_stream_get_formats(stream);

	/* Note: buf is not terminated while the string is being built. */
	for (idx = 0; idx < ast_format_cap_count(caps); ++idx) {
		struct ast_format *fmt;
		size_t size;

		fmt = ast_format_cap_get_format(caps, idx);

		/* Add one for a comma or terminator */
		size = strlen(ast_format_get_name(fmt)) + 1;
		if (len < size) {
			ao2_ref(fmt, -1);
			break;
		}

		/* Append the format name */
		strcpy(buf + accum, ast_format_get_name(fmt));/* Safe */
		ao2_ref(fmt, -1);

		accum += size;
		len -= size;

		/* The last comma on the built string will be set to the terminator. */
		buf[accum - 1] = ',';
	}

	/* Remove the trailing comma or terminate an empty buffer. */
	buf[accum ? accum - 1 : 0] = '\0';
	return 0;
}

struct media_offer_data {
	struct ast_sip_session *session;
	enum ast_media_type media_type;
	const char *value;
};

static int media_offer_write_av(void *obj)
{
	struct media_offer_data *data = obj;
	struct ast_stream_topology *topology;
	struct ast_stream *stream;
	struct ast_format_cap *caps;

	if (data->session->inv_session->dlg->state == PJSIP_DIALOG_STATE_ESTABLISHED) {
		struct session_refresh_state *state;

		/* As we've already answered we need to store our media state until we are ready to send it */
		state = session_refresh_state_get_or_alloc(data->session);
		if (!state) {
			return -1;
		}
		topology = state->media_state->topology;
	} else {
		/* The session is not yet up so we are initially answering or offering */
		if (!data->session->pending_media_state->topology) {
			data->session->pending_media_state->topology = ast_stream_topology_clone(data->session->endpoint->media.topology);
			if (!data->session->pending_media_state->topology) {
				return -1;
			}
		}
		topology = data->session->pending_media_state->topology;
	}

	/* XXX This method won't work when it comes time to do multistream support. The proper way to do this
	 * will either be to
	 * a) Alter all media streams of a particular type.
	 * b) Change the dialplan function to be able to specify which stream to alter and alter only that
	 * one stream
	 */
	stream = ast_stream_topology_get_first_stream_by_type(topology, data->media_type);
	if (!stream) {
		return 0;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		return -1;
	}

	ast_format_cap_append_from_cap(caps, ast_stream_get_formats(stream),
		AST_MEDIA_TYPE_UNKNOWN);
	ast_format_cap_remove_by_type(caps, data->media_type);
	ast_format_cap_update_by_allow_disallow(caps, data->value, 1);
	ast_stream_set_formats(stream, caps);
	ast_stream_set_metadata(stream, "pjsip_session_refresh", "force");
	ao2_ref(caps, -1);

	return 0;
}

int pjsip_acf_media_offer_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);

	if (!strcmp(data, "audio")) {
		return media_offer_read_av(channel->session, buf, len, AST_MEDIA_TYPE_AUDIO);
	} else if (!strcmp(data, "video")) {
		return media_offer_read_av(channel->session, buf, len, AST_MEDIA_TYPE_VIDEO);
	} else {
		/* Ensure that the buffer is empty */
		buf[0] = '\0';
	}

	return 0;
}

int pjsip_acf_media_offer_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;
	struct media_offer_data mdata = {
		.value = value
	};

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	mdata.session = channel->session;

	if (!strcmp(data, "audio")) {
		mdata.media_type = AST_MEDIA_TYPE_AUDIO;
	} else if (!strcmp(data, "video")) {
		mdata.media_type = AST_MEDIA_TYPE_VIDEO;
	}

	return ast_sip_push_task_wait_serializer(channel->session->serializer, media_offer_write_av, &mdata);
}

int pjsip_acf_dtmf_mode_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);

	if (ast_sip_dtmf_to_str(channel->session->dtmf, buf, len) < 0) {
		ast_log(LOG_WARNING, "Unknown DTMF mode %d on PJSIP channel %s\n", channel->session->dtmf, ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	ast_channel_unlock(chan);
	return 0;
}

int pjsip_acf_moh_passthrough_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (len < 3) {
		ast_log(LOG_WARNING, "%s: buffer too small\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	strncpy(buf, AST_YESNO(channel->session->moh_passthrough), len);

	ast_channel_unlock(chan);
	return 0;
}

struct refresh_data {
	struct ast_sip_session *session;
	enum ast_sip_session_refresh_method method;
};

static int sip_session_response_cb(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	struct ast_format *fmt;

	if (!session->channel) {
		/* Egads! */
		return 0;
	}

	fmt = ast_format_cap_get_best_by_type(ast_channel_nativeformats(session->channel), AST_MEDIA_TYPE_AUDIO);
	if (!fmt) {
		/* No format? That's weird. */
		return 0;
	}
	ast_channel_set_writeformat(session->channel, fmt);
	ast_channel_set_rawwriteformat(session->channel, fmt);
	ast_channel_set_readformat(session->channel, fmt);
	ast_channel_set_rawreadformat(session->channel, fmt);
	ao2_ref(fmt, -1);

	return 0;
}

static int dtmf_mode_refresh_cb(void *obj)
{
	struct refresh_data *data = obj;

	if (data->session->inv_session->state == PJSIP_INV_STATE_CONFIRMED) {
		ast_debug(3, "Changing DTMF mode on channel %s after OFFER/ANSWER completion. Sending session refresh\n", ast_channel_name(data->session->channel));

		ast_sip_session_refresh(data->session, NULL, NULL,
			sip_session_response_cb, data->method, 1, NULL);
	} else if (data->session->inv_session->state == PJSIP_INV_STATE_INCOMING) {
		ast_debug(3, "Changing DTMF mode on channel %s during OFFER/ANSWER exchange. Updating SDP answer\n", ast_channel_name(data->session->channel));
		ast_sip_session_regenerate_answer(data->session, NULL);
	}

	return 0;
}

int pjsip_acf_dtmf_mode_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;
	struct ast_sip_session_media *media;
	int dsp_features = 0;
	int dtmf = -1;
	struct refresh_data rdata = {
			.method = AST_SIP_SESSION_REFRESH_METHOD_INVITE,
		};

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	rdata.session = channel->session;

	dtmf = ast_sip_str_to_dtmf(value);

	if (dtmf == -1) {
		ast_log(LOG_WARNING, "Cannot set DTMF mode to '%s' on channel '%s' as value is invalid.\n", value,
			ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	if (channel->session->dtmf == dtmf) {
		/* DTMF mode unchanged, nothing to do! */
		ast_channel_unlock(chan);
		return 0;
	}

	channel->session->dtmf = dtmf;

	media = channel->session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];

	if (media && media->rtp) {
		if (channel->session->dtmf == AST_SIP_DTMF_RFC_4733) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 1);
			ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_RFC2833);
		} else if (channel->session->dtmf == AST_SIP_DTMF_INFO) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 0);
			ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_NONE);
		} else if (channel->session->dtmf == AST_SIP_DTMF_INBAND) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 0);
			ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_INBAND);
		} else if (channel->session->dtmf == AST_SIP_DTMF_NONE) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 0);
			ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_NONE);
		} else if (channel->session->dtmf == AST_SIP_DTMF_AUTO) {
			if (ast_rtp_instance_dtmf_mode_get(media->rtp) != AST_RTP_DTMF_MODE_RFC2833) {
				/* no RFC4733 negotiated, enable inband */
				ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_INBAND);
			}
		} else if (channel->session->dtmf == AST_SIP_DTMF_AUTO_INFO) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_DTMF, 0);
			if (ast_rtp_instance_dtmf_mode_get(media->rtp) == AST_RTP_DTMF_MODE_INBAND) {
				/* if inband, switch to INFO */
				ast_rtp_instance_dtmf_mode_set(media->rtp, AST_RTP_DTMF_MODE_NONE);
			}
		}
	}

	if (channel->session->dsp) {
		dsp_features = ast_dsp_get_features(channel->session->dsp);
	}
	if (channel->session->dtmf == AST_SIP_DTMF_INBAND ||
		channel->session->dtmf == AST_SIP_DTMF_AUTO) {
		dsp_features |= DSP_FEATURE_DIGIT_DETECT;
	} else {
		dsp_features &= ~DSP_FEATURE_DIGIT_DETECT;
	}
	if (dsp_features) {
		if (!channel->session->dsp) {
			if (!(channel->session->dsp = ast_dsp_new())) {
				ast_channel_unlock(chan);
				return 0;
			}
		}
		ast_dsp_set_features(channel->session->dsp, dsp_features);
	} else if (channel->session->dsp) {
		ast_dsp_free(channel->session->dsp);
		channel->session->dsp = NULL;
	}

	ast_channel_unlock(chan);

	return ast_sip_push_task_wait_serializer(channel->session->serializer, dtmf_mode_refresh_cb, &rdata);
}

int pjsip_acf_moh_passthrough_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	channel->session->moh_passthrough = ast_true(value);

	ast_channel_unlock(chan);

	return 0;
}

static int refresh_write_cb(void *obj)
{
	struct refresh_data *data = obj;
	struct session_refresh_state *state;

	state = session_refresh_state_get_or_alloc(data->session);
	if (!state) {
		return -1;
	}

	ast_sip_session_refresh(data->session, NULL, NULL,
		sip_session_response_cb, data->method, 1, state->media_state);

	state->media_state = NULL;
	ast_sip_session_remove_datastore(data->session, "pjsip_session_refresh");

	return 0;
}

int pjsip_acf_session_refresh_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;
	struct refresh_data rdata = {
		.method = AST_SIP_SESSION_REFRESH_METHOD_INVITE,
	};

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_log(LOG_WARNING, "'%s' not allowed on unanswered channel '%s'.\n", cmd, ast_channel_name(chan));
		return -1;
	}

	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel\n", cmd);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);
	rdata.session = channel->session;

	if (!strcmp(value, "invite")) {
		rdata.method = AST_SIP_SESSION_REFRESH_METHOD_INVITE;
	} else if (!strcmp(value, "update")) {
		rdata.method = AST_SIP_SESSION_REFRESH_METHOD_UPDATE;
	}

	return ast_sip_push_task_wait_serializer(channel->session->serializer, refresh_write_cb, &rdata);
}

struct hangup_data {
	struct ast_sip_session *session;
	int response_code;
};

/*!
 * \brief Serializer task to hangup channel
 */
static int pjsip_hangup(void *obj)
{
	struct hangup_data *hdata = obj;
	pjsip_tx_data *packet = NULL;

	if ((hdata->session->inv_session->state != PJSIP_INV_STATE_DISCONNECTED) &&
		(pjsip_inv_answer(hdata->session->inv_session, hdata->response_code, NULL, NULL, &packet) == PJ_SUCCESS)) {
		ast_sip_session_send_response(hdata->session, packet);
	}

	return 0;
}

/*!
 * \brief Callback that validates the response code
 */
static int response_code_validator(const char *channel_name,
	const char *response) {
	int response_code;

	int rc = ast_str_to_int(response, &response_code);
	if (rc != 0) {
		response_code = ast_sip_str2rc(response);
		if (response_code < 0) {
			ast_log(LOG_WARNING, "%s: Unrecognized response code parameter '%s'."
				" Defaulting to 603 DECLINE\n",
				channel_name, response);
			return PJSIP_SC_DECLINE;
		}
	}

	if (response_code < 400 || response_code > 699) {
		ast_log(LOG_WARNING, "%s: Response code %d is out of range 400 -> 699."
			" Defaulting to 603 DECLINE\n",
			channel_name, response_code);
		return PJSIP_SC_DECLINE;
	}
	return response_code;
}

/*!
 * \brief Called by pjsip_app_hangup and pjsip_action_hangup
 *        to actually perform the hangup
 */
static void pjsip_app_hangup_handler(struct ast_channel *chan, int response_code)
{
	struct ast_sip_channel_pvt *channel;
	struct hangup_data hdata = { NULL, -1 };
	const char *tag = ast_channel_name(chan);

	hdata.response_code = response_code;

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "%s: Not a PJSIP channel\n", tag);
		ast_channel_unlock(chan);
		return;
	}

	channel = ast_channel_tech_pvt(chan);
	hdata.session = channel->session;

	if (hdata.session->inv_session->role != PJSIP_ROLE_UAS || (
		hdata.session->inv_session->state != PJSIP_INV_STATE_INCOMING &&
		hdata.session->inv_session->state != PJSIP_INV_STATE_EARLY)) {
		ast_log(LOG_WARNING, "%s: Not an incoming channel or invalid state '%s'\n",
			tag, pjsip_inv_state_name(hdata.session->inv_session->state));
		ast_channel_unlock(chan);
		return;
	}

	ast_channel_unlock(chan);

	if (ast_sip_push_task_wait_serializer(channel->session->serializer,
		pjsip_hangup, &hdata) != 0) {
		ast_log(LOG_WARNING, "%s: failed to push hangup task to serializer\n", tag);
	}

	return;
}

/*!
 * \brief PJSIPHangup Dialplan App
 */
int pjsip_app_hangup(struct ast_channel *chan, const char *data)
{
	int response_code;
	const char *tag = ast_channel_name(chan);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s: Missing response code parameter\n", tag);
		return -1;
	}

	response_code = response_code_validator(tag, data);

	pjsip_app_hangup_handler(chan, response_code);

	return -1;
}

/*!
 * \brief PJSIPHangup Manager Action
 */
int pjsip_action_hangup(struct mansession *s, const struct message *m)
{
	return ast_manager_hangup_helper(s, m,
		pjsip_app_hangup_handler, response_code_validator);
}

int pjsip_transfer_handling_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel;
	int ret = 0;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (strcmp(ast_channel_tech(chan)->type, "PJSIP")) {
		ast_log(LOG_WARNING, "Cannot call %s on a non-PJSIP channel %s\n", cmd, ast_channel_name(chan));
		ast_channel_unlock(chan);
		return -1;
	}

	channel = ast_channel_tech_pvt(chan);

	if (ast_strlen_zero(value) || !strcmp(value, "core")) {
		channel->session->transferhandling_ari = 0;
	} else if (!strcmp(value, "ari-only")) {
		channel->session->transferhandling_ari = 1;
	} else {
		ast_log(AST_LOG_WARNING, "Cannot set unknown transfer handling '%s' on channel '%s', transfer handling will remain unchanged.",
			value, ast_channel_name(chan));
		ret = -1;
	}

	ast_channel_unlock(chan);
	return ret;
}
