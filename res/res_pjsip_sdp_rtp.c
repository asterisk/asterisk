/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

/*! \file
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \brief SIP SDP media stream handling
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_session</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjmedia.h>
#include <pjlib.h>

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/netsock2.h"
#include "asterisk/channel.h"
#include "asterisk/causes.h"
#include "asterisk/sched.h"
#include "asterisk/acl.h"
#include "asterisk/sdp_srtp.h"
#include "asterisk/dsp.h"
#include "asterisk/linkedlists.h"       /* for AST_LIST_NEXT */
#include "asterisk/stream.h"
#include "asterisk/logger_category.h"
#include "asterisk/format_cache.h"

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/res_pjsip_session_caps.h"

/*! \brief Scheduler for RTCP purposes */
static struct ast_sched_context *sched;

/*! \brief Address for RTP */
static struct ast_sockaddr address_rtp;

static const char STR_AUDIO[] = "audio";
static const char STR_VIDEO[] = "video";

static int send_keepalive(const void *data)
{
	struct ast_sip_session_media *session_media = (struct ast_sip_session_media *) data;
	struct ast_rtp_instance *rtp = session_media->rtp;
	int keepalive;
	time_t interval;
	int send_keepalive;

	if (!rtp) {
		return 0;
	}

	keepalive = ast_rtp_instance_get_keepalive(rtp);

	if (!ast_sockaddr_isnull(&session_media->direct_media_addr)) {
		ast_debug_rtp(3, "(%p) RTP not sending keepalive since direct media is in use\n", rtp);
		return keepalive * 1000;
	}

	interval = time(NULL) - ast_rtp_instance_get_last_tx(rtp);
	send_keepalive = interval >= keepalive;

	ast_debug_rtp(3, "(%p) RTP it has been %d seconds since RTP was last sent. %sending keepalive\n",
		rtp, (int) interval, send_keepalive ? "S" : "Not s");

	if (send_keepalive) {
		ast_rtp_instance_sendcng(rtp, 0);
		return keepalive * 1000;
	}

	return (keepalive - interval) * 1000;
}

/*! \brief Check whether RTP is being received or not */
static int rtp_check_timeout(const void *data)
{
	struct ast_sip_session_media *session_media = (struct ast_sip_session_media *)data;
	struct ast_rtp_instance *rtp = session_media->rtp;
	struct ast_channel *chan;
	int elapsed;
	int now;
	int timeout;

	if (!rtp) {
		return 0;
	}

	chan = ast_channel_get_by_name(ast_rtp_instance_get_channel_id(rtp));
	if (!chan) {
		return 0;
	}

	/* Store these values locally to avoid multiple function calls */
	now = time(NULL);
	timeout = ast_rtp_instance_get_timeout(rtp);

	/* If the channel is not in UP state or call is redirected
	 * outside Asterisk return for later check.
	 */
	if (ast_channel_state(chan) != AST_STATE_UP || !ast_sockaddr_isnull(&session_media->direct_media_addr)) {
		/* Avoiding immediately disconnect after channel up or direct media has been stopped */
		ast_rtp_instance_set_last_rx(rtp, now);
		ast_channel_unref(chan);
		/* Recheck after half timeout for avoiding possible races
		* and faster reacting to cases while there is no an RTP at all.
		*/
		return timeout * 500;
	}

	elapsed = now - ast_rtp_instance_get_last_rx(rtp);
	if (elapsed < timeout) {
		ast_channel_unref(chan);
		return (timeout - elapsed) * 1000;
	}

	ast_log(LOG_NOTICE, "Disconnecting channel '%s' for lack of %s RTP activity in %d seconds\n",
		ast_channel_name(chan), ast_codec_media_type2str(session_media->type), elapsed);

	ast_channel_lock(chan);
	ast_channel_hangupcause_set(chan, AST_CAUSE_REQUESTED_CHAN_UNAVAIL);
	ast_channel_unlock(chan);

	ast_softhangup(chan, AST_SOFTHANGUP_DEV);
	ast_channel_unref(chan);

	return 0;
}

/*!
 * \brief Enable RTCP on an RTP session.
 */
static void enable_rtcp(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	const struct pjmedia_sdp_media *remote_media)
{
	enum ast_rtp_instance_rtcp rtcp_type;

	if (session->endpoint->media.rtcp_mux && session_media->remote_rtcp_mux) {
		rtcp_type = AST_RTP_INSTANCE_RTCP_MUX;
	} else {
		rtcp_type = AST_RTP_INSTANCE_RTCP_STANDARD;
	}

	ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_RTCP, rtcp_type);
}

/*!
 * \brief Enable an RTP extension on an RTP session.
 */
static void enable_rtp_extension(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	enum ast_rtp_extension extension, enum ast_rtp_extension_direction direction,
	const pjmedia_sdp_session *sdp)
{
	int id = -1;

	/* For a bundle group the local unique identifier space is shared across all streams within
	 * it.
	 */
	if (session_media->bundle_group != -1) {
		int index;

		for (index = 0; index < sdp->media_count; ++index) {
			struct ast_sip_session_media *other_session_media;
			int other_id;

			if (index >= AST_VECTOR_SIZE(&session->pending_media_state->sessions)) {
				break;
			}

			other_session_media = AST_VECTOR_GET(&session->pending_media_state->sessions, index);
			if (!other_session_media->rtp || other_session_media->bundle_group != session_media->bundle_group) {
				continue;
			}

			other_id = ast_rtp_instance_extmap_get_id(other_session_media->rtp, extension);
			if (other_id == -1) {
				/* Worst case we have to fall back to the highest available free local unique identifier
				 * for the bundle group.
				 */
				other_id = ast_rtp_instance_extmap_count(other_session_media->rtp) + 1;
				if (id < other_id) {
					id = other_id;
				}
				continue;
			}

			id = other_id;
			break;
		}
	}

	ast_rtp_instance_extmap_enable(session_media->rtp, id, extension, direction);
}

/*! \brief Internal function which creates an RTP instance */
static int create_rtp(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	const pjmedia_sdp_session *sdp)
{
	struct ast_rtp_engine_ice *ice;
	struct ast_sockaddr temp_media_address;
	struct ast_sockaddr *media_address =  &address_rtp;

	if (session->endpoint->media.bind_rtp_to_media_address && !ast_strlen_zero(session->endpoint->media.address)) {
		if (ast_sockaddr_parse(&temp_media_address, session->endpoint->media.address, 0)) {
			ast_debug_rtp(1, "Endpoint %s: Binding RTP media to %s\n",
				ast_sorcery_object_get_id(session->endpoint),
				session->endpoint->media.address);
			media_address = &temp_media_address;
		} else {
			ast_debug_rtp(1, "Endpoint %s: RTP media address invalid: %s\n",
				ast_sorcery_object_get_id(session->endpoint),
				session->endpoint->media.address);
		}
	} else {
		struct ast_sip_transport *transport;

		transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport",
			session->endpoint->transport);
		if (transport) {
			struct ast_sip_transport_state *trans_state;

			trans_state = ast_sip_get_transport_state(ast_sorcery_object_get_id(transport));
			if (trans_state) {
				char hoststr[PJ_INET6_ADDRSTRLEN];

				pj_sockaddr_print(&trans_state->host, hoststr, sizeof(hoststr), 0);
				if (ast_sockaddr_parse(&temp_media_address, hoststr, 0)) {
					ast_debug_rtp(1, "Transport %s bound to %s: Using it for RTP media.\n",
						session->endpoint->transport, hoststr);
					media_address = &temp_media_address;
				} else {
					ast_debug_rtp(1, "Transport %s bound to %s: Invalid for RTP media.\n",
						session->endpoint->transport, hoststr);
				}
				ao2_ref(trans_state, -1);
			}
			ao2_ref(transport, -1);
		}
	}

	if (!(session_media->rtp = ast_rtp_instance_new(session->endpoint->media.rtp.engine, sched, media_address, NULL))) {
		ast_log(LOG_ERROR, "Unable to create RTP instance using RTP engine '%s'\n", session->endpoint->media.rtp.engine);
		return -1;
	}

	ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_NAT, session->endpoint->media.rtp.symmetric);
	ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_ASYMMETRIC_CODEC, session->endpoint->asymmetric_rtp_codec);

	if (!session->endpoint->media.rtp.ice_support && (ice = ast_rtp_instance_get_ice(session_media->rtp))) {
		ice->stop(session_media->rtp);
	}

	if (session->dtmf == AST_SIP_DTMF_RFC_4733 || session->dtmf == AST_SIP_DTMF_AUTO || session->dtmf == AST_SIP_DTMF_AUTO_INFO) {
		ast_rtp_instance_dtmf_mode_set(session_media->rtp, AST_RTP_DTMF_MODE_RFC2833);
		ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_DTMF, 1);
	} else if (session->dtmf == AST_SIP_DTMF_INBAND) {
		ast_rtp_instance_dtmf_mode_set(session_media->rtp, AST_RTP_DTMF_MODE_INBAND);
	}

	if (session_media->type == AST_MEDIA_TYPE_AUDIO &&
			(session->endpoint->media.tos_audio || session->endpoint->media.cos_audio)) {
		ast_rtp_instance_set_qos(session_media->rtp, session->endpoint->media.tos_audio,
				session->endpoint->media.cos_audio, "SIP RTP Audio");
	} else if (session_media->type == AST_MEDIA_TYPE_VIDEO) {
		ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_RETRANS_RECV, session->endpoint->media.webrtc);
		ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_RETRANS_SEND, session->endpoint->media.webrtc);
		ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_REMB, session->endpoint->media.webrtc);
		if (session->endpoint->media.webrtc) {
			enable_rtp_extension(session, session_media, AST_RTP_EXTENSION_ABS_SEND_TIME, AST_RTP_EXTENSION_DIRECTION_SENDRECV, sdp);
			enable_rtp_extension(session, session_media, AST_RTP_EXTENSION_TRANSPORT_WIDE_CC, AST_RTP_EXTENSION_DIRECTION_SENDRECV, sdp);
		}
		if (session->endpoint->media.tos_video || session->endpoint->media.cos_video) {
			ast_rtp_instance_set_qos(session_media->rtp, session->endpoint->media.tos_video,
					session->endpoint->media.cos_video, "SIP RTP Video");
		}
	}

	ast_rtp_instance_set_last_rx(session_media->rtp, time(NULL));

	return 0;
}

static void get_codecs(struct ast_sip_session *session, const struct pjmedia_sdp_media *stream, struct ast_rtp_codecs *codecs,
	struct ast_sip_session_media *session_media, struct ast_format_cap *astformats)
{
	pjmedia_sdp_attr *attr;
	pjmedia_sdp_rtpmap *rtpmap;
	pjmedia_sdp_fmtp fmtp;
	struct ast_format *format;
	int i, num = 0, tel_event = 0;
	char name[256];
	char media[20];
	char fmt_param[256];
	enum ast_rtp_options options = session->endpoint->media.g726_non_standard ?
		AST_RTP_OPT_G726_NONSTANDARD : 0;
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	ast_rtp_codecs_payloads_initialize(codecs);

	ast_format_cap_remove_by_type(astformats, AST_MEDIA_TYPE_UNKNOWN);

	/* Iterate through provided formats */
	for (i = 0; i < stream->desc.fmt_count; ++i) {
		/* The payload is kept as a string for things like t38 but for video it is always numerical */
		ast_rtp_codecs_payloads_set_m_type(codecs, NULL, pj_strtoul(&stream->desc.fmt[i]));
		/* Look for the optional rtpmap attribute */
		if (!(attr = pjmedia_sdp_media_find_attr2(stream, "rtpmap", &stream->desc.fmt[i]))) {
			continue;
		}

		/* Interpret the attribute as an rtpmap */
		if ((pjmedia_sdp_attr_to_rtpmap(session->inv_session->pool_prov, attr, &rtpmap)) != PJ_SUCCESS) {
			continue;
		}

		ast_copy_pj_str(name, &rtpmap->enc_name, sizeof(name));
		if (strcmp(name, "telephone-event") == 0) {
			tel_event++;
		}

		ast_copy_pj_str(media, (pj_str_t*)&stream->desc.media, sizeof(media));
		ast_rtp_codecs_payloads_set_rtpmap_type_rate(codecs, NULL,
			pj_strtoul(&stream->desc.fmt[i]), media, name, options, rtpmap->clock_rate);
		/* Look for an optional associated fmtp attribute */
		if (!(attr = pjmedia_sdp_media_find_attr2(stream, "fmtp", &rtpmap->pt))) {
			continue;
		}

		if ((pjmedia_sdp_attr_get_fmtp(attr, &fmtp)) == PJ_SUCCESS) {
			ast_copy_pj_str(fmt_param, &fmtp.fmt, sizeof(fmt_param));
			if (sscanf(fmt_param, "%30d", &num) != 1) {
				continue;
			}

			if ((format = ast_rtp_codecs_get_payload_format(codecs, num))) {
				struct ast_format *format_parsed;

				ast_copy_pj_str(fmt_param, &fmtp.fmt_param, sizeof(fmt_param));

				format_parsed = ast_format_parse_sdp_fmtp(format, fmt_param);
				if (format_parsed) {
					ast_rtp_codecs_payload_replace_format(codecs, num, format_parsed);
					ao2_ref(format_parsed, -1);
				}
				ao2_ref(format, -1);
			}
		}
	}

	/* Parsing done, now fill the ast_format_cap struct in the correct order */
	for (i = 0; i < stream->desc.fmt_count; ++i) {
		if ((format = ast_rtp_codecs_get_payload_format(codecs, pj_strtoul(&stream->desc.fmt[i])))) {
			ast_format_cap_append(astformats, format, 0);
			ao2_ref(format, -1);
		}
	}

	if (!tel_event && (session->dtmf == AST_SIP_DTMF_AUTO)) {
		ast_rtp_instance_dtmf_mode_set(session_media->rtp, AST_RTP_DTMF_MODE_INBAND);
		ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_DTMF, 0);
	}

	if (session->dtmf == AST_SIP_DTMF_AUTO_INFO) {
		if  (tel_event) {
			ast_rtp_instance_dtmf_mode_set(session_media->rtp, AST_RTP_DTMF_MODE_RFC2833);
			ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_DTMF, 1);
		} else {
			ast_rtp_instance_dtmf_mode_set(session_media->rtp, AST_RTP_DTMF_MODE_NONE);
			ast_rtp_instance_set_prop(session_media->rtp, AST_RTP_PROPERTY_DTMF, 0);
		}
	}


	/* Get the packetization, if it exists */
	if ((attr = pjmedia_sdp_media_find_attr2(stream, "ptime", NULL))) {
		unsigned long framing = pj_strtoul(pj_strltrim(&attr->value));
		if (framing && session->endpoint->media.rtp.use_ptime) {
			ast_rtp_codecs_set_framing(codecs, framing);
			ast_format_cap_set_framing(astformats, framing);
		}
	}

	SCOPE_EXIT_RTN();
}

static int apply_cap_to_bundled(struct ast_sip_session_media *session_media,
	struct ast_sip_session_media *session_media_transport,
	struct ast_stream *asterisk_stream, struct ast_format_cap *joint)
{
	if (!joint) {
		return -1;
	}

	ast_stream_set_formats(asterisk_stream, joint);

	/* If this is a bundled stream then apply the payloads to RTP instance acting as transport to prevent conflicts */
	if (session_media_transport != session_media && session_media->bundled) {
		int index;

		for (index = 0; index < ast_format_cap_count(joint); ++index) {
			struct ast_format *format = ast_format_cap_get_format(joint, index);
			int rtp_code;

			/* Ensure this payload is in the bundle group transport codecs, this purposely doesn't check the return value for
			 * things as the format is guaranteed to have a payload already.
			 */
			rtp_code = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(session_media->rtp), 1, format, 0);
			ast_rtp_codecs_payload_set_rx(ast_rtp_instance_get_codecs(session_media_transport->rtp), rtp_code, format);

			ao2_ref(format, -1);
		}
	}

	return 0;
}

static struct ast_format_cap *set_incoming_call_offer_cap(
	struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	const struct pjmedia_sdp_media *stream)
{
	struct ast_format_cap *incoming_call_offer_cap;
	struct ast_format_cap *remote;
	struct ast_rtp_codecs codecs = AST_RTP_CODECS_NULL_INIT;
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));


	remote = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!remote) {
		ast_log(LOG_ERROR, "Failed to allocate %s incoming remote capabilities\n",
				ast_codec_media_type2str(session_media->type));
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't allocate caps\n");
	}

	/* Get the peer's capabilities*/
	get_codecs(session, stream, &codecs, session_media, remote);

	incoming_call_offer_cap = ast_sip_session_create_joint_call_cap(
		session, session_media->type, remote);

	ao2_ref(remote, -1);

	if (!incoming_call_offer_cap || ast_format_cap_empty(incoming_call_offer_cap)) {
		ao2_cleanup(incoming_call_offer_cap);
		ast_rtp_codecs_payloads_destroy(&codecs);
		SCOPE_EXIT_RTN_VALUE(NULL, "No incoming call offer caps\n");
	}

	/*
	 * Setup rx payload type mapping to prefer the mapping
	 * from the peer that the RFC says we SHOULD use.
	 */
	ast_rtp_codecs_payloads_xover(&codecs, &codecs, NULL);

	ast_rtp_codecs_payloads_copy(&codecs,
		ast_rtp_instance_get_codecs(session_media->rtp), session_media->rtp);

	ast_rtp_codecs_payloads_destroy(&codecs);

	SCOPE_EXIT_RTN_VALUE(incoming_call_offer_cap);
}

static int set_caps(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media,
	struct ast_sip_session_media *session_media_transport,
	const struct pjmedia_sdp_media *stream,
	int is_offer, struct ast_stream *asterisk_stream)
{
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, peer, NULL, ao2_cleanup);
	RAII_VAR(struct ast_format_cap *, joint, NULL, ao2_cleanup);
	enum ast_media_type media_type = session_media->type;
	struct ast_rtp_codecs codecs = AST_RTP_CODECS_NULL_INIT;
	int direct_media_enabled = !ast_sockaddr_isnull(&session_media->direct_media_addr) &&
		ast_format_cap_count(session->direct_media_cap);
	int dsp_features = 0;
	SCOPE_ENTER(1, "%s %s\n", ast_sip_session_get_name(session), is_offer ? "OFFER" : "ANSWER");

	if (!(caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT)) ||
	    !(peer = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT)) ||
	    !(joint = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		ast_log(LOG_ERROR, "Failed to allocate %s capabilities\n",
			ast_codec_media_type2str(session_media->type));
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create %s capabilities\n",
			ast_codec_media_type2str(session_media->type));
	}

	/* get the endpoint capabilities */
	if (direct_media_enabled) {
		ast_format_cap_get_compatible(session->endpoint->media.codecs, session->direct_media_cap, caps);
	} else {
		ast_format_cap_append_from_cap(caps, session->endpoint->media.codecs, media_type);
	}

	/* get the capabilities on the peer */
	get_codecs(session, stream, &codecs, session_media, peer);

	/* get the joint capabilities between peer and endpoint */
	ast_format_cap_get_compatible(caps, peer, joint);
	if (!ast_format_cap_count(joint)) {
		struct ast_str *usbuf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
		struct ast_str *thembuf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

		ast_rtp_codecs_payloads_destroy(&codecs);
		ast_log(LOG_NOTICE, "No joint capabilities for '%s' media stream between our configuration(%s) and incoming SDP(%s)\n",
			ast_codec_media_type2str(session_media->type),
			ast_format_cap_get_names(caps, &usbuf),
			ast_format_cap_get_names(peer, &thembuf));
		SCOPE_EXIT_RTN_VALUE(-1, "No joint capabilities for '%s' media stream between our configuration(%s) and incoming SDP(%s)\n",
			ast_codec_media_type2str(session_media->type),
			ast_format_cap_get_names(caps, &usbuf),
			ast_format_cap_get_names(peer, &thembuf));
	}

	if (is_offer) {
		/*
		 * Setup rx payload type mapping to prefer the mapping
		 * from the peer that the RFC says we SHOULD use.
		 */
		ast_rtp_codecs_payloads_xover(&codecs, &codecs, NULL);
	}
	ast_rtp_codecs_payloads_copy(&codecs, ast_rtp_instance_get_codecs(session_media->rtp),
		session_media->rtp);

	apply_cap_to_bundled(session_media, session_media_transport, asterisk_stream, joint);

	if (session->channel && ast_sip_session_is_pending_stream_default(session, asterisk_stream)) {
		ast_channel_lock(session->channel);
		ast_format_cap_remove_by_type(caps, AST_MEDIA_TYPE_UNKNOWN);
		ast_format_cap_append_from_cap(caps, ast_channel_nativeformats(session->channel),
			AST_MEDIA_TYPE_UNKNOWN);
		ast_format_cap_remove_by_type(caps, media_type);

		if (session->endpoint->preferred_codec_only){
			struct ast_format *preferred_fmt = ast_format_cap_get_format(joint, 0);
			ast_format_cap_append(caps, preferred_fmt, 0);
			ao2_ref(preferred_fmt, -1);
		} else if (!session->endpoint->asymmetric_rtp_codec) {
			struct ast_format *best;
			/*
			 * If we don't allow the sending codec to be changed on our side
			 * then get the best codec from the joint capabilities of the media
			 * type and use only that. This ensures the core won't start sending
			 * out a format that we aren't currently sending.
			 */

			best = ast_format_cap_get_best_by_type(joint, media_type);
			if (best) {
				ast_format_cap_append(caps, best, ast_format_cap_get_framing(joint));
				ao2_ref(best, -1);
			}
		} else {
			ast_format_cap_append_from_cap(caps, joint, media_type);
		}

		/*
		 * Apply the new formats to the channel, potentially changing
		 * raw read/write formats and translation path while doing so.
		 */
		ast_channel_nativeformats_set(session->channel, caps);
		if (media_type == AST_MEDIA_TYPE_AUDIO) {
			ast_set_read_format(session->channel, ast_channel_readformat(session->channel));
			ast_set_write_format(session->channel, ast_channel_writeformat(session->channel));
		}

		if ( ((session->dtmf == AST_SIP_DTMF_AUTO) || (session->dtmf == AST_SIP_DTMF_AUTO_INFO) )
		    && (ast_rtp_instance_dtmf_mode_get(session_media->rtp) == AST_RTP_DTMF_MODE_RFC2833)
		    && (session->dsp)) {
			dsp_features = ast_dsp_get_features(session->dsp);
			dsp_features &= ~DSP_FEATURE_DIGIT_DETECT;
			if (dsp_features) {
				ast_dsp_set_features(session->dsp, dsp_features);
			} else {
				ast_dsp_free(session->dsp);
				session->dsp = NULL;
			}
		}

		if (ast_channel_is_bridged(session->channel)) {
			ast_channel_set_unbridged_nolock(session->channel, 1);
		}

		ast_channel_unlock(session->channel);
	}

	ast_rtp_codecs_payloads_destroy(&codecs);
	SCOPE_EXIT_RTN_VALUE(0);
}

static pjmedia_sdp_attr* generate_rtpmap_attr(struct ast_sip_session *session, pjmedia_sdp_media *media, pj_pool_t *pool,
					      int rtp_code, int asterisk_format, struct ast_format *format, int code)
{
#ifndef HAVE_PJSIP_ENDPOINT_COMPACT_FORM
	extern pj_bool_t pjsip_use_compact_form;
#else
	pj_bool_t pjsip_use_compact_form = pjsip_cfg()->endpt.use_compact_form;
#endif
	pjmedia_sdp_rtpmap rtpmap;
	pjmedia_sdp_attr *attr = NULL;
	char tmp[64];
	enum ast_rtp_options options = session->endpoint->media.g726_non_standard ?
		AST_RTP_OPT_G726_NONSTANDARD : 0;

	snprintf(tmp, sizeof(tmp), "%d", rtp_code);
	pj_strdup2(pool, &media->desc.fmt[media->desc.fmt_count++], tmp);

	if (rtp_code <= AST_RTP_PT_LAST_STATIC && pjsip_use_compact_form) {
		return NULL;
	}

	rtpmap.pt = media->desc.fmt[media->desc.fmt_count - 1];
	rtpmap.clock_rate = ast_rtp_lookup_sample_rate2(asterisk_format, format, code);
	pj_strdup2(pool, &rtpmap.enc_name, ast_rtp_lookup_mime_subtype2(asterisk_format, format, code, options));
	if (!pj_stricmp2(&rtpmap.enc_name, "opus")) {
		pj_cstr(&rtpmap.param, "2");
	} else {
		pj_cstr(&rtpmap.param, NULL);
	}

	pjmedia_sdp_rtpmap_to_attr(pool, &rtpmap, &attr);

	return attr;
}

static pjmedia_sdp_attr* generate_fmtp_attr(pj_pool_t *pool, struct ast_format *format, int rtp_code)
{
	struct ast_str *fmtp0 = ast_str_alloca(256);
	pj_str_t fmtp1;
	pjmedia_sdp_attr *attr = NULL;
	char *tmp;

	ast_format_generate_sdp_fmtp(format, rtp_code, &fmtp0);
	if (ast_str_strlen(fmtp0)) {
		tmp = ast_str_buffer(fmtp0) + ast_str_strlen(fmtp0) - 1;
		/* remove any carriage return line feeds */
		while (*tmp == '\r' || *tmp == '\n') --tmp;
		*++tmp = '\0';
		/* ast...generate gives us everything, just need value */
		tmp = strchr(ast_str_buffer(fmtp0), ':');
		if (tmp && tmp[1] != '\0') {
			fmtp1 = pj_str(tmp + 1);
		} else {
			fmtp1 = pj_str(ast_str_buffer(fmtp0));
		}
		attr = pjmedia_sdp_attr_create(pool, "fmtp", &fmtp1);
	}
	return attr;
}

/*! \brief Function which adds ICE attributes to a media stream */
static void add_ice_to_stream(struct ast_sip_session *session, struct ast_sip_session_media *session_media, pj_pool_t *pool, pjmedia_sdp_media *media,
	unsigned int include_candidates)
{
	struct ast_rtp_engine_ice *ice;
	struct ao2_container *candidates;
	const char *username, *password;
	pj_str_t stmp;
	pjmedia_sdp_attr *attr;
	struct ao2_iterator it_candidates;
	struct ast_rtp_engine_ice_candidate *candidate;

	if (!session->endpoint->media.rtp.ice_support || !(ice = ast_rtp_instance_get_ice(session_media->rtp))) {
		return;
	}

	if (!session_media->remote_ice) {
		ice->stop(session_media->rtp);
		return;
	}

	if ((username = ice->get_ufrag(session_media->rtp))) {
		attr = pjmedia_sdp_attr_create(pool, "ice-ufrag", pj_cstr(&stmp, username));
		media->attr[media->attr_count++] = attr;
	}

	if ((password = ice->get_password(session_media->rtp))) {
		attr = pjmedia_sdp_attr_create(pool, "ice-pwd", pj_cstr(&stmp, password));
		media->attr[media->attr_count++] = attr;
	}

	if (!include_candidates) {
		return;
	}

	candidates = ice->get_local_candidates(session_media->rtp);
	if (!candidates) {
		return;
	}

	it_candidates = ao2_iterator_init(candidates, 0);
	for (; (candidate = ao2_iterator_next(&it_candidates)); ao2_ref(candidate, -1)) {
		struct ast_str *attr_candidate = ast_str_create(128);

		ast_str_set(&attr_candidate, -1, "%s %u %s %d %s ", candidate->foundation, candidate->id, candidate->transport,
					candidate->priority, ast_sockaddr_stringify_addr_remote(&candidate->address));
		ast_str_append(&attr_candidate, -1, "%s typ ", ast_sockaddr_stringify_port(&candidate->address));

		switch (candidate->type) {
			case AST_RTP_ICE_CANDIDATE_TYPE_HOST:
				ast_str_append(&attr_candidate, -1, "host");
				break;
			case AST_RTP_ICE_CANDIDATE_TYPE_SRFLX:
				ast_str_append(&attr_candidate, -1, "srflx");
				break;
			case AST_RTP_ICE_CANDIDATE_TYPE_RELAYED:
				ast_str_append(&attr_candidate, -1, "relay");
				break;
		}

		if (!ast_sockaddr_isnull(&candidate->relay_address)) {
			ast_str_append(&attr_candidate, -1, " raddr %s rport", ast_sockaddr_stringify_addr_remote(&candidate->relay_address));
			ast_str_append(&attr_candidate, -1, " %s", ast_sockaddr_stringify_port(&candidate->relay_address));
		}

		attr = pjmedia_sdp_attr_create(pool, "candidate", pj_cstr(&stmp, ast_str_buffer(attr_candidate)));
		media->attr[media->attr_count++] = attr;

		ast_free(attr_candidate);
	}

	ao2_iterator_destroy(&it_candidates);
	ao2_ref(candidates, -1);
}

/*! \brief Function which checks for ice attributes in an audio stream */
static void check_ice_support(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				   const struct pjmedia_sdp_media *remote_stream)
{
	struct ast_rtp_engine_ice *ice;
	const pjmedia_sdp_attr *attr;
	unsigned int attr_i;

	if (!session->endpoint->media.rtp.ice_support || !(ice = ast_rtp_instance_get_ice(session_media->rtp))) {
		session_media->remote_ice = 0;
		return;
	}

	/* Find all of the candidates */
	for (attr_i = 0; attr_i < remote_stream->attr_count; ++attr_i) {
		attr = remote_stream->attr[attr_i];
		if (!pj_strcmp2(&attr->name, "candidate")) {
			session_media->remote_ice = 1;
			break;
		}
	}

	if (attr_i == remote_stream->attr_count) {
		session_media->remote_ice = 0;
	}
}

static void process_ice_auth_attrb(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				   const struct pjmedia_sdp_session *remote, const struct pjmedia_sdp_media *remote_stream)
{
	struct ast_rtp_engine_ice *ice;
	const pjmedia_sdp_attr *ufrag_attr, *passwd_attr;
	char ufrag_attr_value[256];
	char passwd_attr_value[256];

	/* If ICE support is not enabled or available exit early */
	if (!session->endpoint->media.rtp.ice_support || !(ice = ast_rtp_instance_get_ice(session_media->rtp))) {
		return;
	}

	ufrag_attr = pjmedia_sdp_media_find_attr2(remote_stream, "ice-ufrag", NULL);
	if (!ufrag_attr) {
		ufrag_attr = pjmedia_sdp_attr_find2(remote->attr_count, remote->attr, "ice-ufrag", NULL);
	}
	if (ufrag_attr) {
		ast_copy_pj_str(ufrag_attr_value, (pj_str_t*)&ufrag_attr->value, sizeof(ufrag_attr_value));
	} else {
		return;
	}
        passwd_attr = pjmedia_sdp_media_find_attr2(remote_stream, "ice-pwd", NULL);
	if (!passwd_attr) {
		passwd_attr = pjmedia_sdp_attr_find2(remote->attr_count, remote->attr, "ice-pwd", NULL);
	}
	if (passwd_attr) {
		ast_copy_pj_str(passwd_attr_value, (pj_str_t*)&passwd_attr->value, sizeof(passwd_attr_value));
	} else {
		return;
	}

	if (ufrag_attr && passwd_attr) {
		ice->set_authentication(session_media->rtp, ufrag_attr_value, passwd_attr_value);
	}
}

/*! \brief Function which processes ICE attributes in an audio stream */
static void process_ice_attributes(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				   const struct pjmedia_sdp_session *remote, const struct pjmedia_sdp_media *remote_stream)
{
	struct ast_rtp_engine_ice *ice;
	const pjmedia_sdp_attr *attr;
	char attr_value[256];
	unsigned int attr_i;

	/* If ICE support is not enabled or available exit early */
	if (!session->endpoint->media.rtp.ice_support || !(ice = ast_rtp_instance_get_ice(session_media->rtp))) {
		return;
	}

	ast_debug_ice(2, "(%p) ICE process attributes\n", session_media->rtp);

	attr = pjmedia_sdp_media_find_attr2(remote_stream, "ice-ufrag", NULL);
	if (!attr) {
		attr = pjmedia_sdp_attr_find2(remote->attr_count, remote->attr, "ice-ufrag", NULL);
	}
	if (attr) {
		ast_copy_pj_str(attr_value, (pj_str_t*)&attr->value, sizeof(attr_value));
		ice->set_authentication(session_media->rtp, attr_value, NULL);
	} else {
		ast_debug_ice(2, "(%p) ICE no, or invalid ice-ufrag\n", session_media->rtp);
		return;
	}

	attr = pjmedia_sdp_media_find_attr2(remote_stream, "ice-pwd", NULL);
	if (!attr) {
		attr = pjmedia_sdp_attr_find2(remote->attr_count, remote->attr, "ice-pwd", NULL);
	}
	if (attr) {
		ast_copy_pj_str(attr_value, (pj_str_t*)&attr->value, sizeof(attr_value));
		ice->set_authentication(session_media->rtp, NULL, attr_value);
	} else {
		ast_debug_ice(2, "(%p) ICE no, or invalid ice-pwd\n", session_media->rtp);
		return;
	}

	if (pjmedia_sdp_media_find_attr2(remote_stream, "ice-lite", NULL)) {
		ice->ice_lite(session_media->rtp);
	}

	/* Find all of the candidates */
	for (attr_i = 0; attr_i < remote_stream->attr_count; ++attr_i) {
		char foundation[33], transport[32], address[PJ_INET6_ADDRSTRLEN + 1], cand_type[6], relay_address[PJ_INET6_ADDRSTRLEN + 1] = "";
		unsigned int port, relay_port = 0;
		struct ast_rtp_engine_ice_candidate candidate = { 0, };

		attr = remote_stream->attr[attr_i];

		/* If this is not a candidate line skip it */
		if (pj_strcmp2(&attr->name, "candidate")) {
			continue;
		}

		ast_copy_pj_str(attr_value, (pj_str_t*)&attr->value, sizeof(attr_value));

		if (sscanf(attr_value, "%32s %30u %31s %30u %46s %30u typ %5s %*s %23s %*s %30u", foundation, &candidate.id, transport,
			(unsigned *)&candidate.priority, address, &port, cand_type, relay_address, &relay_port) < 7) {
			/* Candidate did not parse properly */
			continue;
		}

		if (session->endpoint->media.rtcp_mux && session_media->remote_rtcp_mux && candidate.id > 1) {
			/* Remote side may have offered RTP and RTCP candidates. However, if we're using RTCP MUX,
			 * then we should ignore RTCP candidates.
			 */
			continue;
		}

		candidate.foundation = foundation;
		candidate.transport = transport;

		ast_sockaddr_parse(&candidate.address, address, PARSE_PORT_FORBID);
		ast_sockaddr_set_port(&candidate.address, port);

		if (!strcasecmp(cand_type, "host")) {
			candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_HOST;
		} else if (!strcasecmp(cand_type, "srflx")) {
			candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_SRFLX;
		} else if (!strcasecmp(cand_type, "relay")) {
			candidate.type = AST_RTP_ICE_CANDIDATE_TYPE_RELAYED;
		} else {
			continue;
		}

		if (!ast_strlen_zero(relay_address)) {
			ast_sockaddr_parse(&candidate.relay_address, relay_address, PARSE_PORT_FORBID);
		}

		if (relay_port) {
			ast_sockaddr_set_port(&candidate.relay_address, relay_port);
		}

		ice->add_remote_candidate(session_media->rtp, &candidate);
	}

	ice->set_role(session_media->rtp, pjmedia_sdp_neg_was_answer_remote(session->inv_session->neg) == PJ_TRUE ?
		AST_RTP_ICE_ROLE_CONTROLLING : AST_RTP_ICE_ROLE_CONTROLLED);
	ice->start(session_media->rtp);
}

/*! \brief figure out if media stream has crypto lines for sdes */
static int media_stream_has_crypto(const struct pjmedia_sdp_media *stream)
{
	int i;

	for (i = 0; i < stream->attr_count; i++) {
		pjmedia_sdp_attr *attr;

		/* check the stream for the required crypto attribute */
		attr = stream->attr[i];
		if (pj_strcmp2(&attr->name, "crypto")) {
			continue;
		}

		return 1;
	}

	return 0;
}

/*! \brief figure out media transport encryption type from the media transport string */
static enum ast_sip_session_media_encryption get_media_encryption_type(pj_str_t transport,
	const struct pjmedia_sdp_media *stream, unsigned int *optimistic)
{
	RAII_VAR(char *, transport_str, ast_strndup(transport.ptr, transport.slen), ast_free);

	*optimistic = 0;

	if (!transport_str) {
		return AST_SIP_MEDIA_TRANSPORT_INVALID;
	}
	if (strstr(transport_str, "UDP/TLS")) {
		return AST_SIP_MEDIA_ENCRYPT_DTLS;
	} else if (strstr(transport_str, "SAVP")) {
		return AST_SIP_MEDIA_ENCRYPT_SDES;
	} else if (media_stream_has_crypto(stream)) {
		*optimistic = 1;
		return AST_SIP_MEDIA_ENCRYPT_SDES;
	} else {
		return AST_SIP_MEDIA_ENCRYPT_NONE;
	}
}

/*!
 * \brief Checks whether the encryption offered in SDP is compatible with the endpoint's configuration
 * \internal
 *
 * \param endpoint Media encryption configured for the endpoint
 * \param stream pjmedia_sdp_media stream description
 *
 * \retval AST_SIP_MEDIA_TRANSPORT_INVALID on encryption mismatch
 * \retval The encryption requested in the SDP
 */
static enum ast_sip_session_media_encryption check_endpoint_media_transport(
	struct ast_sip_endpoint *endpoint,
	const struct pjmedia_sdp_media *stream)
{
	enum ast_sip_session_media_encryption incoming_encryption;
	char transport_end = stream->desc.transport.ptr[stream->desc.transport.slen - 1];
	unsigned int optimistic;

	if ((transport_end == 'F' && !endpoint->media.rtp.use_avpf)
		|| (transport_end != 'F' && endpoint->media.rtp.use_avpf)) {
		return AST_SIP_MEDIA_TRANSPORT_INVALID;
	}

	incoming_encryption = get_media_encryption_type(stream->desc.transport, stream, &optimistic);

	if (incoming_encryption == endpoint->media.rtp.encryption) {
		return incoming_encryption;
	}

	if (endpoint->media.rtp.force_avp ||
		endpoint->media.rtp.encryption_optimistic) {
		return incoming_encryption;
	}

	/* If an optimistic offer has been made but encryption is not enabled consider it as having
	 * no offer of crypto at all instead of invalid so the session proceeds.
	 */
	if (optimistic) {
		return AST_SIP_MEDIA_ENCRYPT_NONE;
	}

	return AST_SIP_MEDIA_TRANSPORT_INVALID;
}

static int setup_srtp(struct ast_sip_session_media *session_media)
{
	if (!session_media->srtp) {
		session_media->srtp = ast_sdp_srtp_alloc();
		if (!session_media->srtp) {
			return -1;
		}
	}

	if (!session_media->srtp->crypto) {
		session_media->srtp->crypto = ast_sdp_crypto_alloc();
		if (!session_media->srtp->crypto) {
			return -1;
		}
	}

	return 0;
}

static int setup_dtls_srtp(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media)
{
	struct ast_rtp_engine_dtls *dtls;

	if (!session->endpoint->media.rtp.dtls_cfg.enabled || !session_media->rtp) {
		return -1;
	}

	dtls = ast_rtp_instance_get_dtls(session_media->rtp);
	if (!dtls) {
		return -1;
	}

	session->endpoint->media.rtp.dtls_cfg.suite = ((session->endpoint->media.rtp.srtp_tag_32) ? AST_AES_CM_128_HMAC_SHA1_32 : AST_AES_CM_128_HMAC_SHA1_80);
	if (dtls->set_configuration(session_media->rtp, &session->endpoint->media.rtp.dtls_cfg)) {
		ast_log(LOG_ERROR, "Attempted to set an invalid DTLS-SRTP configuration on RTP instance '%p'\n",
			session_media->rtp);
		return -1;
	}

	if (setup_srtp(session_media)) {
		return -1;
	}
	return 0;
}

static void apply_dtls_attrib(struct ast_sip_session_media *session_media,
	pjmedia_sdp_attr *attr)
{
	struct ast_rtp_engine_dtls *dtls = ast_rtp_instance_get_dtls(session_media->rtp);
	pj_str_t *value;

	if (!attr->value.ptr || !dtls) {
		return;
	}

	value = pj_strtrim(&attr->value);

	if (!pj_strcmp2(&attr->name, "setup")) {
		if (!pj_stricmp2(value, "active")) {
			dtls->set_setup(session_media->rtp, AST_RTP_DTLS_SETUP_ACTIVE);
		} else if (!pj_stricmp2(value, "passive")) {
			dtls->set_setup(session_media->rtp, AST_RTP_DTLS_SETUP_PASSIVE);
		} else if (!pj_stricmp2(value, "actpass")) {
			dtls->set_setup(session_media->rtp, AST_RTP_DTLS_SETUP_ACTPASS);
		} else if (!pj_stricmp2(value, "holdconn")) {
			dtls->set_setup(session_media->rtp, AST_RTP_DTLS_SETUP_HOLDCONN);
		} else {
			ast_log(LOG_WARNING, "Unsupported setup attribute value '%*s'\n", (int)value->slen, value->ptr);
		}
	} else if (!pj_strcmp2(&attr->name, "connection")) {
		if (!pj_stricmp2(value, "new")) {
			dtls->reset(session_media->rtp);
		} else if (!pj_stricmp2(value, "existing")) {
			/* Do nothing */
		} else {
			ast_log(LOG_WARNING, "Unsupported connection attribute value '%*s'\n", (int)value->slen, value->ptr);
		}
	} else if (!pj_strcmp2(&attr->name, "fingerprint")) {
		char hash_value[256], hash[32];
		char fingerprint_text[value->slen + 1];
		ast_copy_pj_str(fingerprint_text, value, sizeof(fingerprint_text));
			if (sscanf(fingerprint_text, "%31s %255s", hash, hash_value) == 2) {
			if (!strcasecmp(hash, "sha-1")) {
				dtls->set_fingerprint(session_media->rtp, AST_RTP_DTLS_HASH_SHA1, hash_value);
			} else if (!strcasecmp(hash, "sha-256")) {
				dtls->set_fingerprint(session_media->rtp, AST_RTP_DTLS_HASH_SHA256, hash_value);
			} else {
				ast_log(LOG_WARNING, "Unsupported fingerprint hash type '%s'\n",
				hash);
			}
		}
	}
}

static int parse_dtls_attrib(struct ast_sip_session_media *session_media,
	const struct pjmedia_sdp_session *sdp,
	const struct pjmedia_sdp_media *stream)
{
	int i;

	for (i = 0; i < sdp->attr_count; i++) {
		apply_dtls_attrib(session_media, sdp->attr[i]);
	}

	for (i = 0; i < stream->attr_count; i++) {
		apply_dtls_attrib(session_media, stream->attr[i]);
	}

	ast_set_flag(session_media->srtp, AST_SRTP_CRYPTO_OFFER_OK);

	return 0;
}

static int setup_sdes_srtp(struct ast_sip_session_media *session_media,
	const struct pjmedia_sdp_media *stream)
{
	int i;

	for (i = 0; i < stream->attr_count; i++) {
		pjmedia_sdp_attr *attr;
		RAII_VAR(char *, crypto_str, NULL, ast_free);

		/* check the stream for the required crypto attribute */
		attr = stream->attr[i];
		if (pj_strcmp2(&attr->name, "crypto")) {
			continue;
		}

		crypto_str = ast_strndup(attr->value.ptr, attr->value.slen);
		if (!crypto_str) {
			return -1;
		}

		if (setup_srtp(session_media)) {
			return -1;
		}

		if (!ast_sdp_crypto_process(session_media->rtp, session_media->srtp, crypto_str)) {
			/* found a valid crypto attribute */
			return 0;
		}

		ast_debug(1, "Ignoring crypto offer with unsupported parameters: %s\n", crypto_str);
	}

	/* no usable crypto attributes found */
	return -1;
}

static int setup_media_encryption(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media,
	const struct pjmedia_sdp_session *sdp,
	const struct pjmedia_sdp_media *stream)
{
	switch (session_media->encryption) {
	case AST_SIP_MEDIA_ENCRYPT_SDES:
		if (setup_sdes_srtp(session_media, stream)) {
			return -1;
		}
		break;
	case AST_SIP_MEDIA_ENCRYPT_DTLS:
		if (setup_dtls_srtp(session, session_media)) {
			return -1;
		}
		if (parse_dtls_attrib(session_media, sdp, stream)) {
			return -1;
		}
		break;
	case AST_SIP_MEDIA_TRANSPORT_INVALID:
	case AST_SIP_MEDIA_ENCRYPT_NONE:
		break;
	}

	return 0;
}

static void set_ice_components(struct ast_sip_session *session, struct ast_sip_session_media *session_media)
{
	struct ast_rtp_engine_ice *ice;

	ast_assert(session_media->rtp != NULL);

	ice = ast_rtp_instance_get_ice(session_media->rtp);
	if (!session->endpoint->media.rtp.ice_support || !ice) {
		return;
	}

	if (session->endpoint->media.rtcp_mux && session_media->remote_rtcp_mux) {
		/* We both support RTCP mux. Only one ICE component necessary */
		ice->change_components(session_media->rtp, 1);
	} else {
		/* They either don't support RTCP mux or we don't know if they do yet. */
		ice->change_components(session_media->rtp, 2);
	}
}

/*! \brief Function which adds ssrc attributes to a media stream */
static void add_ssrc_to_stream(struct ast_sip_session *session, struct ast_sip_session_media *session_media, pj_pool_t *pool, pjmedia_sdp_media *media)
{
	pj_str_t stmp;
	pjmedia_sdp_attr *attr;
	char tmp[128];

	if (!session->endpoint->media.bundle || session_media->bundle_group == -1) {
		return;
	}

	snprintf(tmp, sizeof(tmp), "%u cname:%s", ast_rtp_instance_get_ssrc(session_media->rtp), ast_rtp_instance_get_cname(session_media->rtp));
	attr = pjmedia_sdp_attr_create(pool, "ssrc", pj_cstr(&stmp, tmp));
	media->attr[media->attr_count++] = attr;
}

/*! \brief Function which processes ssrc attributes in a stream */
static void process_ssrc_attributes(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				   const struct pjmedia_sdp_media *remote_stream)
{
	int index;

	if (!session->endpoint->media.bundle) {
		return;
	}

	for (index = 0; index < remote_stream->attr_count; ++index) {
		pjmedia_sdp_attr *attr = remote_stream->attr[index];
		char attr_value[pj_strlen(&attr->value) + 1];
		char *ssrc_attribute_name, *ssrc_attribute_value = NULL;
		unsigned int ssrc;

		/* We only care about ssrc attributes */
		if (pj_strcmp2(&attr->name, "ssrc")) {
			continue;
		}

		ast_copy_pj_str(attr_value, &attr->value, sizeof(attr_value));

		if ((ssrc_attribute_name = strchr(attr_value, ' '))) {
			/* This has an actual attribute */
			*ssrc_attribute_name++ = '\0';
			ssrc_attribute_value = strchr(ssrc_attribute_name, ':');
			if (ssrc_attribute_value) {
				/* Values are actually optional according to the spec */
				*ssrc_attribute_value++ = '\0';
			}
		}

		if (sscanf(attr_value, "%30u", &ssrc) < 1) {
			continue;
		}

		/* If we are currently negotiating as a result of the remote side renegotiating then
		 * determine if the source for this stream has changed.
		 */
		if (pjmedia_sdp_neg_get_state(session->inv_session->neg) == PJMEDIA_SDP_NEG_STATE_REMOTE_OFFER &&
			session->active_media_state) {
			struct ast_rtp_instance_stats stats = { 0, };

			if (!ast_rtp_instance_get_stats(session_media->rtp, &stats, AST_RTP_INSTANCE_STAT_REMOTE_SSRC) &&
				stats.remote_ssrc != ssrc) {
				session_media->changed = 1;
			}
		}

		ast_rtp_instance_set_remote_ssrc(session_media->rtp, ssrc);
		break;
	}
}

static void add_msid_to_stream(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media, pj_pool_t *pool, pjmedia_sdp_media *media,
	struct ast_stream *stream)
{
	pj_str_t stmp;
	pjmedia_sdp_attr *attr;
	char msid[(AST_UUID_STR_LEN * 2) + 2];
	const char *stream_label = ast_stream_get_metadata(stream, "SDP:LABEL");

	if (!session->endpoint->media.webrtc) {
		return;
	}

	if (ast_strlen_zero(session_media->mslabel)) {
		/* If this stream is grouped with another then use its media stream label if possible */
		if (ast_stream_get_group(stream) != -1) {
			struct ast_sip_session_media *group_session_media = AST_VECTOR_GET(&session->pending_media_state->sessions, ast_stream_get_group(stream));

			ast_copy_string(session_media->mslabel, group_session_media->mslabel, sizeof(session_media->mslabel));
		}

		if (ast_strlen_zero(session_media->mslabel)) {
			ast_uuid_generate_str(session_media->mslabel, sizeof(session_media->mslabel));
		}
	}

	if (ast_strlen_zero(session_media->label)) {
		ast_uuid_generate_str(session_media->label, sizeof(session_media->label));
		/* add for stream identification to replace stream_name */
		ast_stream_set_metadata(stream, "MSID:LABEL", session_media->label);
	}

	snprintf(msid, sizeof(msid), "%s %s", session_media->mslabel, session_media->label);
	ast_debug(3, "Stream msid: %p %s %s\n", stream,
		ast_codec_media_type2str(ast_stream_get_type(stream)), msid);
	attr = pjmedia_sdp_attr_create(pool, "msid", pj_cstr(&stmp, msid));
	pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);

	/* 'label' must come after 'msid' */
	if (!ast_strlen_zero(stream_label)) {
		ast_debug(3, "Stream Label: %p %s %s\n", stream,
			ast_codec_media_type2str(ast_stream_get_type(stream)), stream_label);
		attr = pjmedia_sdp_attr_create(pool, "label", pj_cstr(&stmp, stream_label));
		pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);
	}
}

static void add_rtcp_fb_to_stream(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media, pj_pool_t *pool, pjmedia_sdp_media *media)
{
	pj_str_t stmp;
	pjmedia_sdp_attr *attr;

	if (!session->endpoint->media.webrtc) {
		return;
	}

	/* transport-cc is supposed to be for the entire transport, and any media sources so
	 * while the header does not appear in audio streams and isn't negotiated there, we still
	 * place this attribute in as Chrome does.
	 */
	attr = pjmedia_sdp_attr_create(pool, "rtcp-fb", pj_cstr(&stmp, "* transport-cc"));
	pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);

	if (session_media->type != AST_MEDIA_TYPE_VIDEO) {
		return;
	}

	/*
	 * For now just automatically add it the stream even though it hasn't
	 * necessarily been negotiated.
	 */
	attr = pjmedia_sdp_attr_create(pool, "rtcp-fb", pj_cstr(&stmp, "* ccm fir"));
	pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);

	attr = pjmedia_sdp_attr_create(pool, "rtcp-fb", pj_cstr(&stmp, "* goog-remb"));
	pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);

	attr = pjmedia_sdp_attr_create(pool, "rtcp-fb", pj_cstr(&stmp, "* nack"));
	pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);
}

static void add_extmap_to_stream(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media, pj_pool_t *pool, pjmedia_sdp_media *media)
{
	int idx;
	char extmap_value[256];

	if (!session->endpoint->media.webrtc || session_media->type != AST_MEDIA_TYPE_VIDEO) {
		return;
	}

	/* RTP extension local unique identifiers start at '1' */
	for (idx = 1; idx <= ast_rtp_instance_extmap_count(session_media->rtp); ++idx) {
		enum ast_rtp_extension extension = ast_rtp_instance_extmap_get_extension(session_media->rtp, idx);
		const char *direction_str = "";
		pj_str_t stmp;
		pjmedia_sdp_attr *attr;

		/* If this is an unsupported RTP extension we can't place it into the SDP */
		if (extension == AST_RTP_EXTENSION_UNSUPPORTED) {
			continue;
		}

		switch (ast_rtp_instance_extmap_get_direction(session_media->rtp, idx)) {
		case AST_RTP_EXTENSION_DIRECTION_SENDRECV:
			/* Lack of a direction indicates sendrecv, so we leave it out */
			direction_str = "";
			break;
		case AST_RTP_EXTENSION_DIRECTION_SENDONLY:
			direction_str = "/sendonly";
			break;
		case AST_RTP_EXTENSION_DIRECTION_RECVONLY:
			direction_str = "/recvonly";
			break;
		case AST_RTP_EXTENSION_DIRECTION_NONE:
			/* It is impossible for a "none" direction extension to be negotiated but just in case
			 * we treat it as inactive.
			 */
		case AST_RTP_EXTENSION_DIRECTION_INACTIVE:
			direction_str = "/inactive";
			break;
		}

		snprintf(extmap_value, sizeof(extmap_value), "%d%s %s", idx, direction_str,
			ast_rtp_instance_extmap_get_uri(session_media->rtp, idx));
		attr = pjmedia_sdp_attr_create(pool, "extmap", pj_cstr(&stmp, extmap_value));
		pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);
	}
}

/*! \brief Function which processes extmap attributes in a stream */
static void process_extmap_attributes(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				   const struct pjmedia_sdp_media *remote_stream)
{
	int index;

	if (!session->endpoint->media.webrtc || session_media->type != AST_MEDIA_TYPE_VIDEO) {
		return;
	}

	ast_rtp_instance_extmap_clear(session_media->rtp);

	for (index = 0; index < remote_stream->attr_count; ++index) {
		pjmedia_sdp_attr *attr = remote_stream->attr[index];
		char attr_value[pj_strlen(&attr->value) + 1];
		char *uri;
		int id;
		char direction_str[10] = "";
		char *attributes;
		enum ast_rtp_extension_direction direction = AST_RTP_EXTENSION_DIRECTION_SENDRECV;

		/* We only care about extmap attributes */
		if (pj_strcmp2(&attr->name, "extmap")) {
			continue;
		}

		ast_copy_pj_str(attr_value, &attr->value, sizeof(attr_value));

		/* Split the combined unique identifier and direction away from the URI and attributes for easier parsing */
		uri = strchr(attr_value, ' ');
		if (ast_strlen_zero(uri)) {
			continue;
		}
		*uri++ = '\0';

		if ((sscanf(attr_value, "%30d%9s", &id, direction_str) < 1) || (id < 1)) {
			/* We require at a minimum the unique identifier */
			continue;
		}

		/* Convert from the string to the internal representation */
		if (!strcasecmp(direction_str, "/sendonly")) {
			direction = AST_RTP_EXTENSION_DIRECTION_SENDONLY;
		} else if (!strcasecmp(direction_str, "/recvonly")) {
			direction = AST_RTP_EXTENSION_DIRECTION_RECVONLY;
		} else if (!strcasecmp(direction_str, "/inactive")) {
			direction = AST_RTP_EXTENSION_DIRECTION_INACTIVE;
		}

		attributes = strchr(uri, ' ');
		if (!ast_strlen_zero(attributes)) {
			*attributes++ = '\0';
		}

		ast_rtp_instance_extmap_negotiate(session_media->rtp, id, direction, uri, attributes);
	}
}

static void set_session_media_remotely_held(struct ast_sip_session_media *session_media,
											const struct ast_sip_session *session,
											const pjmedia_sdp_media *media,
											const struct ast_stream *stream,
											const struct ast_sockaddr *addrs)
{
	if (ast_sip_session_is_pending_stream_default(session, stream) &&
		(session_media->type == AST_MEDIA_TYPE_AUDIO)) {
		if (((addrs != NULL) && ast_sockaddr_isnull(addrs)) ||
			((addrs != NULL) && ast_sockaddr_is_any(addrs)) ||
			pjmedia_sdp_media_find_attr2(media, "sendonly", NULL) ||
			pjmedia_sdp_media_find_attr2(media, "inactive", NULL)) {
			if (!session_media->remotely_held) {
				session_media->remotely_held = 1;
				session_media->remotely_held_changed = 1;
			}
		} else if (session_media->remotely_held) {
			session_media->remotely_held = 0;
			session_media->remotely_held_changed = 1;
		}
	}
}

/*! \brief Function which negotiates an incoming media stream */
static int negotiate_incoming_sdp_stream(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media, const pjmedia_sdp_session *sdp,
	int index, struct ast_stream *asterisk_stream)
{
	char host[NI_MAXHOST];
	RAII_VAR(struct ast_sockaddr *, addrs, NULL, ast_free);
	pjmedia_sdp_media *stream = sdp->media[index];
	struct ast_sip_session_media *session_media_transport;
	enum ast_media_type media_type = session_media->type;
	enum ast_sip_session_media_encryption encryption = AST_SIP_MEDIA_ENCRYPT_NONE;
	struct ast_format_cap *joint;
	int res;
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	/* If no type formats have been configured reject this stream */
	if (!ast_format_cap_has_type(session->endpoint->media.codecs, media_type)) {
		ast_debug(3, "Endpoint has no codecs for media type '%s', declining stream\n",
			ast_codec_media_type2str(session_media->type));
		SCOPE_EXIT_RTN_VALUE(0, "Endpoint has no codecs\n");
	}

	/* Ensure incoming transport is compatible with the endpoint's configuration */
	if (!session->endpoint->media.rtp.use_received_transport) {
		encryption = check_endpoint_media_transport(session->endpoint, stream);

		if (encryption == AST_SIP_MEDIA_TRANSPORT_INVALID) {
			SCOPE_EXIT_RTN_VALUE(-1, "Incompatible transport\n");
		}
	}

	ast_copy_pj_str(host, stream->conn ? &stream->conn->addr : &sdp->conn->addr, sizeof(host));

	/* Ensure that the address provided is valid */
	if (ast_sockaddr_resolve(&addrs, host, PARSE_PORT_FORBID, AST_AF_UNSPEC) <= 0) {
		/* The provided host was actually invalid so we error out this negotiation */
		SCOPE_EXIT_RTN_VALUE(-1, "Invalid host\n");
	}

	/* Using the connection information create an appropriate RTP instance */
	if (!session_media->rtp && create_rtp(session, session_media, sdp)) {
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create rtp\n");
	}

	process_ssrc_attributes(session, session_media, stream);
	process_extmap_attributes(session, session_media, stream);
	session_media_transport = ast_sip_session_media_get_transport(session, session_media);

	if (session_media_transport == session_media || !session_media->bundled) {
		/* If this media session is carrying actual traffic then set up those aspects */
		session_media->remote_rtcp_mux = (pjmedia_sdp_media_find_attr2(stream, "rtcp-mux", NULL) != NULL);
		set_ice_components(session, session_media);

		enable_rtcp(session, session_media, stream);

		res = setup_media_encryption(session, session_media, sdp, stream);
		if (res) {
			if (!session->endpoint->media.rtp.encryption_optimistic ||
				!pj_strncmp2(&stream->desc.transport, "RTP/SAVP", 8)) {
				/* If optimistic encryption is disabled and crypto should have been enabled
				 * but was not this session must fail. This must also fail if crypto was
				 * required in the offer but could not be set up.
				 */
				SCOPE_EXIT_RTN_VALUE(-1, "Incompatible crypto\n");
			}
			/* There is no encryption, sad. */
			session_media->encryption = AST_SIP_MEDIA_ENCRYPT_NONE;
		}

		/* If we've been explicitly configured to use the received transport OR if
		 * encryption is on and crypto is present use the received transport.
		 * This is done in case of optimistic because it may come in as RTP/AVP or RTP/SAVP depending
		 * on the configuration of the remote endpoint (optimistic themselves or mandatory).
		 */
		if ((session->endpoint->media.rtp.use_received_transport) ||
			((encryption == AST_SIP_MEDIA_ENCRYPT_SDES) && !res)) {
			pj_strdup(session->inv_session->pool, &session_media->transport, &stream->desc.transport);
		}
	} else {
		/* This is bundled with another session, so mark it as such */
		ast_rtp_instance_bundle(session_media->rtp, session_media_transport->rtp);

		enable_rtcp(session, session_media, stream);
	}

	/* If ICE support is enabled find all the needed attributes */
	check_ice_support(session, session_media, stream);

	/* If ICE support is enabled then check remote ICE started? */
	if (session_media->remote_ice) {
		process_ice_auth_attrb(session, session_media, sdp, stream);
	}

	/* Check if incoming SDP is changing the remotely held state */
	set_session_media_remotely_held(session_media, session, stream, asterisk_stream, addrs);

	joint = set_incoming_call_offer_cap(session, session_media, stream);
	res = apply_cap_to_bundled(session_media, session_media_transport, asterisk_stream, joint);
	ao2_cleanup(joint);
	if (res != 0) {
		SCOPE_EXIT_RTN_VALUE(0, "Something failed\n");
	}

	SCOPE_EXIT_RTN_VALUE(1);
}

static int add_crypto_to_stream(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media,
	pj_pool_t *pool, pjmedia_sdp_media *media)
{
	pj_str_t stmp;
	pjmedia_sdp_attr *attr;
	enum ast_rtp_dtls_hash hash;
	const char *crypto_attribute;
	struct ast_rtp_engine_dtls *dtls;
	struct ast_sdp_srtp *tmp;
	static const pj_str_t STR_NEW = { "new", 3 };
	static const pj_str_t STR_EXISTING = { "existing", 8 };
	static const pj_str_t STR_ACTIVE = { "active", 6 };
	static const pj_str_t STR_PASSIVE = { "passive", 7 };
	static const pj_str_t STR_ACTPASS = { "actpass", 7 };
	static const pj_str_t STR_HOLDCONN = { "holdconn", 8 };
	static const pj_str_t STR_MEDSECREQ = { "requested", 9 };
	enum ast_rtp_dtls_setup setup;

	switch (session_media->encryption) {
	case AST_SIP_MEDIA_ENCRYPT_NONE:
	case AST_SIP_MEDIA_TRANSPORT_INVALID:
		break;
	case AST_SIP_MEDIA_ENCRYPT_SDES:
		if (!session_media->srtp) {
			session_media->srtp = ast_sdp_srtp_alloc();
			if (!session_media->srtp) {
				return -1;
			}
		}

		tmp = session_media->srtp;

		do {
			crypto_attribute = ast_sdp_srtp_get_attrib(tmp,
				0 /* DTLS running? No */,
				session->endpoint->media.rtp.srtp_tag_32 /* 32 byte tag length? */);
			if (!crypto_attribute) {
				/* No crypto attribute to add, bad news */
				return -1;
			}

			attr = pjmedia_sdp_attr_create(pool, "crypto",
				pj_cstr(&stmp, crypto_attribute));
			media->attr[media->attr_count++] = attr;
		} while ((tmp = AST_LIST_NEXT(tmp, sdp_srtp_list)));

		if (session->endpoint->security_negotiation == AST_SIP_SECURITY_NEG_MEDIASEC) {
			attr = pjmedia_sdp_attr_create(pool, "3ge2ae", &STR_MEDSECREQ);
			media->attr[media->attr_count++] = attr;
		}

		break;
	case AST_SIP_MEDIA_ENCRYPT_DTLS:
		if (setup_dtls_srtp(session, session_media)) {
			return -1;
		}

		dtls = ast_rtp_instance_get_dtls(session_media->rtp);
		if (!dtls) {
			return -1;
		}

		switch (dtls->get_connection(session_media->rtp)) {
		case AST_RTP_DTLS_CONNECTION_NEW:
			attr = pjmedia_sdp_attr_create(pool, "connection", &STR_NEW);
			media->attr[media->attr_count++] = attr;
			break;
		case AST_RTP_DTLS_CONNECTION_EXISTING:
			attr = pjmedia_sdp_attr_create(pool, "connection", &STR_EXISTING);
			media->attr[media->attr_count++] = attr;
			break;
		default:
			break;
		}

		/* If this is an answer we need to use our current state, if it's an offer we need to use
		 * the configured value.
		 */
		if (session->inv_session->neg
			&& pjmedia_sdp_neg_get_state(session->inv_session->neg) != PJMEDIA_SDP_NEG_STATE_DONE) {
			setup = dtls->get_setup(session_media->rtp);
		} else {
			setup = session->endpoint->media.rtp.dtls_cfg.default_setup;
		}

		switch (setup) {
		case AST_RTP_DTLS_SETUP_ACTIVE:
			attr = pjmedia_sdp_attr_create(pool, "setup", &STR_ACTIVE);
			media->attr[media->attr_count++] = attr;
			break;
		case AST_RTP_DTLS_SETUP_PASSIVE:
			attr = pjmedia_sdp_attr_create(pool, "setup", &STR_PASSIVE);
			media->attr[media->attr_count++] = attr;
			break;
		case AST_RTP_DTLS_SETUP_ACTPASS:
			attr = pjmedia_sdp_attr_create(pool, "setup", &STR_ACTPASS);
			media->attr[media->attr_count++] = attr;
			break;
		case AST_RTP_DTLS_SETUP_HOLDCONN:
			attr = pjmedia_sdp_attr_create(pool, "setup", &STR_HOLDCONN);
			break;
		default:
			break;
		}

		hash = dtls->get_fingerprint_hash(session_media->rtp);
		crypto_attribute = dtls->get_fingerprint(session_media->rtp);
		if (crypto_attribute && (hash == AST_RTP_DTLS_HASH_SHA1 || hash == AST_RTP_DTLS_HASH_SHA256)) {
			RAII_VAR(struct ast_str *, fingerprint, ast_str_create(64), ast_free);
			if (!fingerprint) {
				return -1;
			}

			if (hash == AST_RTP_DTLS_HASH_SHA1) {
				ast_str_set(&fingerprint, 0, "SHA-1 %s", crypto_attribute);
			} else {
				ast_str_set(&fingerprint, 0, "SHA-256 %s", crypto_attribute);
			}

			attr = pjmedia_sdp_attr_create(pool, "fingerprint", pj_cstr(&stmp, ast_str_buffer(fingerprint)));
			media->attr[media->attr_count++] = attr;
		}
		break;
	}

	return 0;
}

/*! \brief Function which creates an outgoing stream */
static int create_outgoing_sdp_stream(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				      struct pjmedia_sdp_session *sdp, const struct pjmedia_sdp_session *remote, struct ast_stream *stream)
{
	pj_pool_t *pool = session->inv_session->pool_prov;
	static const pj_str_t STR_RTP_AVP = { "RTP/AVP", 7 };
	static const pj_str_t STR_IN = { "IN", 2 };
	static const pj_str_t STR_IP4 = { "IP4", 3};
	static const pj_str_t STR_IP6 = { "IP6", 3};
	static const pj_str_t STR_SENDRECV = { "sendrecv", 8 };
	static const pj_str_t STR_SENDONLY = { "sendonly", 8 };
	static const pj_str_t STR_INACTIVE = { "inactive", 8 };
	static const pj_str_t STR_RECVONLY = { "recvonly", 8 };
	pjmedia_sdp_media *media;
	const char *hostip = NULL;
	struct ast_sockaddr addr;
	char tmp[512];
	pj_str_t stmp;
	pjmedia_sdp_attr *attr;
	int index = 0;
	int noncodec = (session->dtmf == AST_SIP_DTMF_RFC_4733 || session->dtmf == AST_SIP_DTMF_AUTO || session->dtmf == AST_SIP_DTMF_AUTO_INFO) ? AST_RTP_DTMF : 0;
	int min_packet_size = 0, max_packet_size = 0;
	int rtp_code;
	RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
	enum ast_media_type media_type = session_media->type;
	struct ast_sip_session_media *session_media_transport;
	pj_sockaddr ip;
	int direct_media_enabled = !ast_sockaddr_isnull(&session_media->direct_media_addr) &&
		ast_format_cap_count(session->direct_media_cap);
	SCOPE_ENTER(1, "%s Type: %s %s\n", ast_sip_session_get_name(session),
		ast_codec_media_type2str(media_type), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));

	media = pj_pool_zalloc(pool, sizeof(struct pjmedia_sdp_media));
	if (!media) {
		SCOPE_EXIT_RTN_VALUE(-1, "Pool alloc failure\n");
	}
	pj_strdup2(pool, &media->desc.media, ast_codec_media_type2str(session_media->type));

	/* If this is a removed (or declined) stream OR if no formats exist then construct a minimal stream in SDP */
	if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED || !ast_stream_get_formats(stream) ||
		!ast_format_cap_count(ast_stream_get_formats(stream))) {
		media->desc.port = 0;
		media->desc.port_count = 1;

		if (remote && remote->media[ast_stream_get_position(stream)]) {
			pjmedia_sdp_media *remote_media = remote->media[ast_stream_get_position(stream)];
			int index;

			media->desc.transport = remote_media->desc.transport;

			/* Preserve existing behavior by copying the formats provided from the offer */
			for (index = 0; index < remote_media->desc.fmt_count; ++index) {
				media->desc.fmt[index] = remote_media->desc.fmt[index];
			}
			media->desc.fmt_count = remote_media->desc.fmt_count;
		} else {
			/* This is actually an offer so put a dummy payload in that is ignored and sane transport */
			media->desc.transport = STR_RTP_AVP;
			pj_strdup2(pool, &media->desc.fmt[media->desc.fmt_count++], "32");
		}

		sdp->media[sdp->media_count++] = media;
		ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);

		SCOPE_EXIT_RTN_VALUE(1, "Stream removed or no formats\n");
	}

	if (!session_media->rtp && create_rtp(session, session_media, sdp)) {
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create rtp\n");
	}

	/* If this stream has not been bundled already it is new and we need to ensure there is no SSRC conflict */
	if (session_media->bundle_group != -1 && !session_media->bundled) {
		for (index = 0; index < sdp->media_count; ++index) {
			struct ast_sip_session_media *other_session_media;

			other_session_media = AST_VECTOR_GET(&session->pending_media_state->sessions, index);
			if (!other_session_media->rtp || other_session_media->bundle_group != session_media->bundle_group) {
				continue;
			}

			if (ast_rtp_instance_get_ssrc(session_media->rtp) == ast_rtp_instance_get_ssrc(other_session_media->rtp)) {
				ast_rtp_instance_change_source(session_media->rtp);
				/* Start the conflict check over again */
				index = -1;
				continue;
			}
		}
	}

	session_media_transport = ast_sip_session_media_get_transport(session, session_media);

	if (session_media_transport == session_media || !session_media->bundled) {
		set_ice_components(session, session_media);
		enable_rtcp(session, session_media, NULL);

		/* Crypto has to be added before setting the media transport so that SRTP is properly
		 * set up according to the configuration. This ends up changing the media transport.
		 */
		if (add_crypto_to_stream(session, session_media, pool, media)) {
			SCOPE_EXIT_RTN_VALUE(-1, "Couldn't add crypto\n");
		}

		if (pj_strlen(&session_media->transport)) {
			/* If a transport has already been specified use it */
			media->desc.transport = session_media->transport;
		} else {
			media->desc.transport = pj_str(ast_sdp_get_rtp_profile(
				/* Optimistic encryption places crypto in the normal RTP/AVP profile */
				!session->endpoint->media.rtp.encryption_optimistic &&
					(session_media->encryption == AST_SIP_MEDIA_ENCRYPT_SDES),
				session_media->rtp, session->endpoint->media.rtp.use_avpf,
				session->endpoint->media.rtp.force_avp));
		}

		media->conn = pj_pool_zalloc(pool, sizeof(struct pjmedia_sdp_conn));
		if (!media->conn) {
			SCOPE_EXIT_RTN_VALUE(-1, "Pool alloc failure\n");
		}

		/* Add connection level details */
		if (direct_media_enabled) {
			hostip = ast_sockaddr_stringify_fmt(&session_media->direct_media_addr, AST_SOCKADDR_STR_ADDR);
		} else if (ast_strlen_zero(session->endpoint->media.address)) {
			hostip = ast_sip_get_host_ip_string(session->endpoint->media.rtp.ipv6 ? pj_AF_INET6() : pj_AF_INET());
		} else {
			hostip = session->endpoint->media.address;
		}

		if (ast_strlen_zero(hostip)) {
			ast_log(LOG_ERROR, "No local host IP available for stream %s\n",
				ast_codec_media_type2str(session_media->type));
			SCOPE_EXIT_RTN_VALUE(-1, "No local host ip\n");
		}

		media->conn->net_type = STR_IN;
		/* Assume that the connection will use IPv4 until proven otherwise */
		media->conn->addr_type = STR_IP4;
		pj_strdup2(pool, &media->conn->addr, hostip);

		if ((pj_sockaddr_parse(pj_AF_UNSPEC(), 0, &media->conn->addr, &ip) == PJ_SUCCESS) &&
			(ip.addr.sa_family == pj_AF_INET6())) {
			media->conn->addr_type = STR_IP6;
		}

		/* Add ICE attributes and candidates */
		add_ice_to_stream(session, session_media, pool, media, 1);

		ast_rtp_instance_get_local_address(session_media->rtp, &addr);
		media->desc.port = direct_media_enabled ? ast_sockaddr_port(&session_media->direct_media_addr) : (pj_uint16_t) ast_sockaddr_port(&addr);
		media->desc.port_count = 1;
	} else {
		pjmedia_sdp_media *bundle_group_stream = sdp->media[session_media_transport->stream_num];

		/* As this is in a bundle group it shares the same details as the group instance */
		media->desc.transport = bundle_group_stream->desc.transport;
		media->conn = bundle_group_stream->conn;
		media->desc.port = bundle_group_stream->desc.port;

		if (add_crypto_to_stream(session, session_media_transport, pool, media)) {
			SCOPE_EXIT_RTN_VALUE(-1, "Couldn't add crypto\n");
		}

		add_ice_to_stream(session, session_media_transport, pool, media, 0);

		enable_rtcp(session, session_media, NULL);
	}

	if (!(caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		ast_log(LOG_ERROR, "Failed to allocate %s capabilities\n",
			ast_codec_media_type2str(session_media->type));
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create caps\n");
	}

	if (direct_media_enabled) {
		ast_format_cap_get_compatible(session->endpoint->media.codecs, session->direct_media_cap, caps);
	} else {
		ast_format_cap_append_from_cap(caps, ast_stream_get_formats(stream), media_type);
	}

	for (index = 0; index < ast_format_cap_count(caps); ++index) {
		struct ast_format *format = ast_format_cap_get_format(caps, index);

		if (ast_format_get_type(format) != media_type) {
			ao2_ref(format, -1);
			continue;
		}

		/* It is possible for some formats not to have SDP information available for them
		 * and if this is the case, skip over them so the SDP can still be created.
		 */
		if (!ast_rtp_lookup_sample_rate2(1, format, 0)) {
			ast_log(LOG_WARNING, "Format '%s' can not be added to SDP, consider disallowing it on endpoint '%s'\n",
				ast_format_get_name(format), ast_sorcery_object_get_id(session->endpoint));
			ao2_ref(format, -1);
			continue;
		}

		/* If this stream is not a transport we need to use the transport codecs structure for payload management to prevent
		 * conflicts.
		 */
		if (session_media_transport != session_media) {
			if ((rtp_code = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(session_media_transport->rtp), 1, format, 0)) == -1) {
				ast_log(LOG_WARNING,"Unable to get rtp codec payload code for %s\n", ast_format_get_name(format));
				ao2_ref(format, -1);
				continue;
			}
			/* Our instance has to match the payload number though */
			ast_rtp_codecs_payload_set_rx(ast_rtp_instance_get_codecs(session_media->rtp), rtp_code, format);
		} else {
			if ((rtp_code = ast_rtp_codecs_payload_code(ast_rtp_instance_get_codecs(session_media->rtp), 1, format, 0)) == -1) {
				ast_log(LOG_WARNING,"Unable to get rtp codec payload code for %s\n", ast_format_get_name(format));
				ao2_ref(format, -1);
				continue;
			}
		}

		if ((attr = generate_rtpmap_attr(session, media, pool, rtp_code, 1, format, 0))) {
			media->attr[media->attr_count++] = attr;
		}

		if ((attr = generate_fmtp_attr(pool, format, rtp_code))) {
			media->attr[media->attr_count++] = attr;
		}

		if (ast_format_get_maximum_ms(format) &&
			((ast_format_get_maximum_ms(format) < max_packet_size) || !max_packet_size)) {
			max_packet_size = ast_format_get_maximum_ms(format);
		}
		ao2_ref(format, -1);

		if (media->desc.fmt_count == PJMEDIA_MAX_SDP_FMT) {
			break;
		}
	}

	/* Add non-codec formats */
	if (ast_sip_session_is_pending_stream_default(session, stream) && media_type != AST_MEDIA_TYPE_VIDEO
		&& media->desc.fmt_count < PJMEDIA_MAX_SDP_FMT) {
		for (index = 1LL; index <= AST_RTP_MAX; index <<= 1) {
			if (!(noncodec & index)) {
				continue;
			}
			rtp_code = ast_rtp_codecs_payload_code(
				ast_rtp_instance_get_codecs(session_media->rtp), 0, NULL, index);
			if (rtp_code == -1) {
				continue;
			}

			if ((attr = generate_rtpmap_attr(session, media, pool, rtp_code, 0, NULL, index))) {
				media->attr[media->attr_count++] = attr;
			}

			if (index == AST_RTP_DTMF) {
				snprintf(tmp, sizeof(tmp), "%d 0-16", rtp_code);
				attr = pjmedia_sdp_attr_create(pool, "fmtp", pj_cstr(&stmp, tmp));
				media->attr[media->attr_count++] = attr;
			}

			if (media->desc.fmt_count == PJMEDIA_MAX_SDP_FMT) {
				break;
			}
		}
	}


	/* If no formats were actually added to the media stream don't add it to the SDP */
	if (!media->desc.fmt_count) {
		SCOPE_EXIT_RTN_VALUE(1, "No formats added to stream\n");
	}

	/* If ptime is set add it as an attribute */
	min_packet_size = ast_rtp_codecs_get_framing(ast_rtp_instance_get_codecs(session_media->rtp));
	if (!min_packet_size) {
		min_packet_size = ast_format_cap_get_framing(caps);
	}
	if (min_packet_size) {
		snprintf(tmp, sizeof(tmp), "%d", min_packet_size);
		attr = pjmedia_sdp_attr_create(pool, "ptime", pj_cstr(&stmp, tmp));
		media->attr[media->attr_count++] = attr;
	}

	if (max_packet_size) {
		snprintf(tmp, sizeof(tmp), "%d", max_packet_size);
		attr = pjmedia_sdp_attr_create(pool, "maxptime", pj_cstr(&stmp, tmp));
		media->attr[media->attr_count++] = attr;
	}

	attr = PJ_POOL_ZALLOC_T(pool, pjmedia_sdp_attr);
	if (session_media->locally_held) {
		if (session_media->remotely_held) {
			attr->name = STR_INACTIVE; /* To place on hold a recvonly stream, send inactive */
		} else {
			attr->name = STR_SENDONLY; /* Send sendonly to initate a local hold */
		}
	} else {
		if (session_media->remotely_held) {
			attr->name = STR_RECVONLY; /* Remote has sent sendonly, reply recvonly */
		} else if (ast_stream_get_state(stream) == AST_STREAM_STATE_SENDONLY) {
			attr->name = STR_SENDONLY; /* Stream has requested sendonly */
		} else if (ast_stream_get_state(stream) == AST_STREAM_STATE_RECVONLY) {
			attr->name = STR_RECVONLY; /* Stream has requested recvonly */
		} else if (ast_stream_get_state(stream) == AST_STREAM_STATE_INACTIVE) {
			attr->name = STR_INACTIVE; /* Stream has requested inactive */
		} else {
			attr->name = STR_SENDRECV; /* No hold in either direction */
		}
	}
	media->attr[media->attr_count++] = attr;

	/* If we've got rtcp-mux enabled, add it unless we received an offer without it */
	if (session->endpoint->media.rtcp_mux && session_media->remote_rtcp_mux) {
		attr = pjmedia_sdp_attr_create(pool, "rtcp-mux", NULL);
		pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);
	}

	add_ssrc_to_stream(session, session_media, pool, media);
	add_msid_to_stream(session, session_media, pool, media, stream);
	add_rtcp_fb_to_stream(session, session_media, pool, media);
	add_extmap_to_stream(session, session_media, pool, media);

	/* Add the media stream to the SDP */
	sdp->media[sdp->media_count++] = media;

	SCOPE_EXIT_RTN_VALUE(1, "RC: 1\n");
}

static struct ast_frame *media_session_rtp_read_callback(struct ast_sip_session *session, struct ast_sip_session_media *session_media)
{
	struct ast_frame *f;

	if (!session_media->rtp) {
		return &ast_null_frame;
	}

	f = ast_rtp_instance_read(session_media->rtp, 0);
	if (!f) {
		return NULL;
	}

	ast_rtp_instance_set_last_rx(session_media->rtp, time(NULL));

	return f;
}

static struct ast_frame *media_session_rtcp_read_callback(struct ast_sip_session *session, struct ast_sip_session_media *session_media)
{
	struct ast_frame *f;

	if (!session_media->rtp) {
		return &ast_null_frame;
	}

	f = ast_rtp_instance_read(session_media->rtp, 1);
	if (!f) {
		return NULL;
	}

	ast_rtp_instance_set_last_rx(session_media->rtp, time(NULL));

	return f;
}

static int media_session_rtp_write_callback(struct ast_sip_session *session, struct ast_sip_session_media *session_media, struct ast_frame *frame)
{
	if (!session_media->rtp) {
		return 0;
	}

	return ast_rtp_instance_write(session_media->rtp, frame);
}

static int apply_negotiated_sdp_stream(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media, const struct pjmedia_sdp_session *local,
	const struct pjmedia_sdp_session *remote, int index, struct ast_stream *asterisk_stream)
{
	RAII_VAR(struct ast_sockaddr *, addrs, NULL, ast_free);
	struct pjmedia_sdp_media *remote_stream = remote->media[index];
	enum ast_media_type media_type = session_media->type;
	char host[NI_MAXHOST];
	int res;
	struct ast_sip_session_media *session_media_transport;
	SCOPE_ENTER(1, "%s Stream: %s\n", ast_sip_session_get_name(session),
		ast_str_tmp(128, ast_stream_to_str(asterisk_stream, &STR_TMP)));

	if (!session->channel) {
		SCOPE_EXIT_RTN_VALUE(1, "No channel\n");
	}

	/* Ensure incoming transport is compatible with the endpoint's configuration */
	if (!session->endpoint->media.rtp.use_received_transport &&
		check_endpoint_media_transport(session->endpoint, remote_stream) == AST_SIP_MEDIA_TRANSPORT_INVALID) {
		SCOPE_EXIT_RTN_VALUE(-1, "Incompatible transport\n");
	}

	/* Create an RTP instance if need be */
	if (!session_media->rtp && create_rtp(session, session_media, local)) {
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create rtp\n");
	}

	process_ssrc_attributes(session, session_media, remote_stream);
	process_extmap_attributes(session, session_media, remote_stream);

	session_media_transport = ast_sip_session_media_get_transport(session, session_media);

	if (session_media_transport == session_media || !session_media->bundled) {
		session_media->remote_rtcp_mux = (pjmedia_sdp_media_find_attr2(remote_stream, "rtcp-mux", NULL) != NULL);
		set_ice_components(session, session_media);

		enable_rtcp(session, session_media, remote_stream);

		res = setup_media_encryption(session, session_media, remote, remote_stream);
		if (!session->endpoint->media.rtp.encryption_optimistic && res) {
			/* If optimistic encryption is disabled and crypto should have been enabled but was not
			 * this session must fail.
			 */
			SCOPE_EXIT_RTN_VALUE(-1, "Incompatible crypto\n");
		}

		if (!remote_stream->conn && !remote->conn) {
			SCOPE_EXIT_RTN_VALUE(1, "No connection info\n");
		}

		ast_copy_pj_str(host, remote_stream->conn ? &remote_stream->conn->addr : &remote->conn->addr, sizeof(host));

		/* Ensure that the address provided is valid */
		if (ast_sockaddr_resolve(&addrs, host, PARSE_PORT_FORBID, AST_AF_UNSPEC) <= 0) {
			/* The provided host was actually invalid so we error out this negotiation */
			SCOPE_EXIT_RTN_VALUE(-1, "Host invalid\n");
		}

		/* Apply connection information to the RTP instance */
		ast_sockaddr_set_port(addrs, remote_stream->desc.port);
		ast_rtp_instance_set_remote_address(session_media->rtp, addrs);

		ast_sip_session_media_set_write_callback(session, session_media, media_session_rtp_write_callback);
		ast_sip_session_media_add_read_callback(session, session_media, ast_rtp_instance_fd(session_media->rtp, 0),
			media_session_rtp_read_callback);
		if (!session->endpoint->media.rtcp_mux || !session_media->remote_rtcp_mux) {
			ast_sip_session_media_add_read_callback(session, session_media, ast_rtp_instance_fd(session_media->rtp, 1),
				media_session_rtcp_read_callback);
		}

		/* If ICE support is enabled find all the needed attributes */
		process_ice_attributes(session, session_media, remote, remote_stream);
	} else {
		/* This is bundled with another session, so mark it as such */
		ast_rtp_instance_bundle(session_media->rtp, session_media_transport->rtp);
		ast_sip_session_media_set_write_callback(session, session_media, media_session_rtp_write_callback);
		enable_rtcp(session, session_media, remote_stream);
	}

	if (set_caps(session, session_media, session_media_transport, remote_stream, 0, asterisk_stream)) {
		SCOPE_EXIT_RTN_VALUE(-1, "set_caps failed\n");
	}

	/* Set the channel uniqueid on the RTP instance now that it is becoming active */
	ast_channel_lock(session->channel);
	ast_rtp_instance_set_channel_id(session_media->rtp, ast_channel_uniqueid(session->channel));
	ast_channel_unlock(session->channel);

	/* Ensure the RTP instance is active */
	ast_rtp_instance_set_stream_num(session_media->rtp, ast_stream_get_position(asterisk_stream));
	ast_rtp_instance_activate(session_media->rtp);

	/* audio stream handles music on hold */
	if (media_type != AST_MEDIA_TYPE_AUDIO && media_type != AST_MEDIA_TYPE_VIDEO) {
		if ((pjmedia_sdp_neg_was_answer_remote(session->inv_session->neg) == PJ_FALSE)
			&& (session->inv_session->state == PJSIP_INV_STATE_CONFIRMED)) {
			ast_queue_control(session->channel, AST_CONTROL_UPDATE_RTP_PEER);
		}
		SCOPE_EXIT_RTN_VALUE(1, "moh\n");
	}

	set_session_media_remotely_held(session_media, session, remote_stream, asterisk_stream, addrs);

	if (session_media->remotely_held_changed) {
		if (session_media->remotely_held) {
			/* The remote side has put us on hold */
			ast_queue_hold(session->channel, session->endpoint->mohsuggest);
			ast_rtp_instance_stop(session_media->rtp);
			ast_queue_frame(session->channel, &ast_null_frame);
			session_media->remotely_held_changed = 0;
		} else {
			/* The remote side has taken us off hold */
			ast_queue_unhold(session->channel);
			ast_queue_frame(session->channel, &ast_null_frame);
			session_media->remotely_held_changed = 0;
		}
	} else if ((pjmedia_sdp_neg_was_answer_remote(session->inv_session->neg) == PJ_FALSE)
		&& (session->inv_session->state == PJSIP_INV_STATE_CONFIRMED)) {
		ast_queue_control(session->channel, AST_CONTROL_UPDATE_RTP_PEER);
	}

	/* This purposely resets the encryption to the configured in case it gets added later */
	session_media->encryption = session->endpoint->media.rtp.encryption;

	if (session->endpoint->media.rtp.keepalive > 0 &&
		(session_media->type == AST_MEDIA_TYPE_AUDIO ||
			session_media->type == AST_MEDIA_TYPE_VIDEO)) {
		ast_rtp_instance_set_keepalive(session_media->rtp, session->endpoint->media.rtp.keepalive);
		/* Schedule the initial keepalive early in case this is being used to punch holes through
		 * a NAT. This way there won't be an awkward delay before media starts flowing in some
		 * scenarios.
		 */
		AST_SCHED_DEL(sched, session_media->keepalive_sched_id);
		session_media->keepalive_sched_id = ast_sched_add_variable(sched, 500, send_keepalive,
			session_media, 1);
	}

	/* As the channel lock is not held during this process the scheduled item won't block if
	 * it is hanging up the channel at the same point we are applying this negotiated SDP.
	 */
	AST_SCHED_DEL(sched, session_media->timeout_sched_id);

	/* Due to the fact that we only ever have one scheduled timeout item for when we are both
	 * off hold and on hold we don't need to store the two timeouts differently on the RTP
	 * instance itself.
	 */
	ast_rtp_instance_set_timeout(session_media->rtp, 0);
	if (session->endpoint->media.rtp.timeout && !session_media->remotely_held && !session_media->locally_held) {
		ast_rtp_instance_set_timeout(session_media->rtp, session->endpoint->media.rtp.timeout);
	} else if (session->endpoint->media.rtp.timeout_hold && (session_media->remotely_held || session_media->locally_held)) {
		ast_rtp_instance_set_timeout(session_media->rtp, session->endpoint->media.rtp.timeout_hold);
	}

	if (ast_rtp_instance_get_timeout(session_media->rtp)) {
		session_media->timeout_sched_id = ast_sched_add_variable(sched,	500, rtp_check_timeout,
			session_media, 1);
	}

	SCOPE_EXIT_RTN_VALUE(1, "Handled\n");
}

/*! \brief Function which updates the media stream with external media address, if applicable */
static void change_outgoing_sdp_stream_media_address(pjsip_tx_data *tdata, struct pjmedia_sdp_media *stream, struct ast_sip_transport *transport)
{
	RAII_VAR(struct ast_sip_transport_state *, transport_state, ast_sip_get_transport_state(ast_sorcery_object_get_id(transport)), ao2_cleanup);
	char host[NI_MAXHOST];
	struct ast_sockaddr our_sdp_addr = { { 0, } };

	/* If the stream has been rejected there will be no connection line */
	if (!stream->conn || !transport_state) {
		return;
	}

	ast_copy_pj_str(host, &stream->conn->addr, sizeof(host));
	ast_sockaddr_parse(&our_sdp_addr, host, PARSE_PORT_FORBID);

	/* Reversed check here. We don't check the remote endpoint being
	 * in our local net, but whether our outgoing session IP is
	 * local. If it is not, we won't do rewriting. No localnet
	 * configured? Always rewrite. */
	if (ast_sip_transport_is_nonlocal(transport_state, &our_sdp_addr) && transport_state->localnet) {
		return;
	}
	ast_debug(5, "Setting media address to %s\n", ast_sockaddr_stringify_addr_remote(&transport_state->external_media_address));
	pj_strdup2(tdata->pool, &stream->conn->addr, ast_sockaddr_stringify_addr_remote(&transport_state->external_media_address));
}

/*! \brief Function which stops the RTP instance */
static void stream_stop(struct ast_sip_session_media *session_media)
{
	if (!session_media->rtp) {
		return;
	}

	AST_SCHED_DEL(sched, session_media->keepalive_sched_id);
	AST_SCHED_DEL(sched, session_media->timeout_sched_id);
	ast_rtp_instance_stop(session_media->rtp);
}

/*! \brief Function which destroys the RTP instance when session ends */
static void stream_destroy(struct ast_sip_session_media *session_media)
{
	if (session_media->rtp) {
		stream_stop(session_media);
		ast_rtp_instance_destroy(session_media->rtp);
	}
	session_media->rtp = NULL;
}

/*! \brief SDP handler for 'audio' media stream */
static struct ast_sip_session_sdp_handler audio_sdp_handler = {
	.id = STR_AUDIO,
	.negotiate_incoming_sdp_stream = negotiate_incoming_sdp_stream,
	.create_outgoing_sdp_stream = create_outgoing_sdp_stream,
	.apply_negotiated_sdp_stream = apply_negotiated_sdp_stream,
	.change_outgoing_sdp_stream_media_address = change_outgoing_sdp_stream_media_address,
	.stream_stop = stream_stop,
	.stream_destroy = stream_destroy,
};

/*! \brief SDP handler for 'video' media stream */
static struct ast_sip_session_sdp_handler video_sdp_handler = {
	.id = STR_VIDEO,
	.negotiate_incoming_sdp_stream = negotiate_incoming_sdp_stream,
	.create_outgoing_sdp_stream = create_outgoing_sdp_stream,
	.apply_negotiated_sdp_stream = apply_negotiated_sdp_stream,
	.change_outgoing_sdp_stream_media_address = change_outgoing_sdp_stream_media_address,
	.stream_stop = stream_stop,
	.stream_destroy = stream_destroy,
};

static int video_info_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	struct pjsip_transaction *tsx;
	pjsip_tx_data *tdata;

	if (!session->channel
		|| !ast_sip_are_media_types_equal(&rdata->msg_info.msg->body->content_type,
			&pjsip_media_type_application_media_control_xml)) {
		return 0;
	}

	tsx = pjsip_rdata_get_tsx(rdata);

	ast_queue_control(session->channel, AST_CONTROL_VIDUPDATE);

	if (pjsip_dlg_create_response(session->inv_session->dlg, rdata, 200, NULL, &tdata) == PJ_SUCCESS) {
		pjsip_dlg_send_response(session->inv_session->dlg, tsx, tdata);
	}

	return 0;
}

static struct ast_sip_session_supplement video_info_supplement = {
	.method = "INFO",
	.incoming_request = video_info_incoming_request,
};

/*! \brief Unloads the sdp RTP/AVP module from Asterisk */
static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&video_info_supplement);
	ast_sip_session_unregister_sdp_handler(&video_sdp_handler, STR_VIDEO);
	ast_sip_session_unregister_sdp_handler(&audio_sdp_handler, STR_AUDIO);

	if (sched) {
		ast_sched_context_destroy(sched);
	}

	return 0;
}

/*!
 * \brief Load the module
 *
 * Module loading including tests for configuration or dependencies.
 * This function can return AST_MODULE_LOAD_FAILURE, AST_MODULE_LOAD_DECLINE,
 * or AST_MODULE_LOAD_SUCCESS. If a dependency or environment variable fails
 * tests return AST_MODULE_LOAD_FAILURE. If the module can not load the
 * configuration file or other non-critical problem return
 * AST_MODULE_LOAD_DECLINE. On success return AST_MODULE_LOAD_SUCCESS.
 */
static int load_module(void)
{
	if (ast_check_ipv6()) {
		ast_sockaddr_parse(&address_rtp, "::", 0);
	} else {
		ast_sockaddr_parse(&address_rtp, "0.0.0.0", 0);
	}

	if (!(sched = ast_sched_context_create())) {
		ast_log(LOG_ERROR, "Unable to create scheduler context.\n");
		goto end;
	}

	if (ast_sched_start_thread(sched)) {
		ast_log(LOG_ERROR, "Unable to create scheduler context thread.\n");
		goto end;
	}

	if (ast_sip_session_register_sdp_handler(&audio_sdp_handler, STR_AUDIO)) {
		ast_log(LOG_ERROR, "Unable to register SDP handler for %s stream type\n", STR_AUDIO);
		goto end;
	}

	if (ast_sip_session_register_sdp_handler(&video_sdp_handler, STR_VIDEO)) {
		ast_log(LOG_ERROR, "Unable to register SDP handler for %s stream type\n", STR_VIDEO);
		goto end;
	}

	ast_sip_session_register_supplement(&video_info_supplement);

	return AST_MODULE_LOAD_SUCCESS;
end:
	unload_module();

	return AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP SDP RTP/AVP stream handler",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	.requires = "res_pjsip,res_pjsip_session",
);
