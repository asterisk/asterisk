/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief PSJIP SIP Channel Driver
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<depend>res_pjsip_pubsub</depend>
	<depend>res_pjsip_session</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/acl.h"
#include "asterisk/callerid.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/musiconhold.h"
#include "asterisk/causes.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/dsp.h"
#include "asterisk/stasis_endpoints.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/indications.h"
#include "asterisk/format_cache.h"
#include "asterisk/translate.h"
#include "asterisk/threadstorage.h"
#include "asterisk/features_config.h"
#include "asterisk/pickup.h"
#include "asterisk/test.h"
#include "asterisk/message.h"

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/stream.h"

#include "pjsip/include/chan_pjsip.h"
#include "pjsip/include/dialplan_functions.h"
#include "pjsip/include/cli_functions.h"

AST_THREADSTORAGE(uniqueid_threadbuf);
#define UNIQUEID_BUFSIZE 256

static const char channel_type[] = "PJSIP";

static unsigned int chan_idx;

static void chan_pjsip_pvt_dtor(void *obj)
{
}

/*! \brief Asterisk core interaction functions */
static struct ast_channel *chan_pjsip_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static struct ast_channel *chan_pjsip_request_with_stream_topology(const char *type,
	struct ast_stream_topology *topology, const struct ast_assigned_ids *assignedids,
	const struct ast_channel *requestor, const char *data, int *cause);
static int chan_pjsip_sendtext_data(struct ast_channel *ast, struct ast_msg_data *msg);
static int chan_pjsip_sendtext(struct ast_channel *ast, const char *text);
static int chan_pjsip_digit_begin(struct ast_channel *ast, char digit);
static int chan_pjsip_digit_end(struct ast_channel *ast, char digit, unsigned int duration);
static int chan_pjsip_call(struct ast_channel *ast, const char *dest, int timeout);
static int chan_pjsip_hangup(struct ast_channel *ast);
static int chan_pjsip_answer(struct ast_channel *ast);
static struct ast_frame *chan_pjsip_read_stream(struct ast_channel *ast);
static int chan_pjsip_write(struct ast_channel *ast, struct ast_frame *f);
static int chan_pjsip_write_stream(struct ast_channel *ast, int stream_num, struct ast_frame *f);
static int chan_pjsip_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen);
static int chan_pjsip_transfer(struct ast_channel *ast, const char *target);
static int chan_pjsip_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);
static int chan_pjsip_devicestate(const char *data);
static int chan_pjsip_queryoption(struct ast_channel *ast, int option, void *data, int *datalen);
static const char *chan_pjsip_get_uniqueid(struct ast_channel *ast);

/*! \brief PBX interface structure for channel registration */
struct ast_channel_tech chan_pjsip_tech = {
	.type = channel_type,
	.description = "PJSIP Channel Driver",
	.requester = chan_pjsip_request,
	.requester_with_stream_topology = chan_pjsip_request_with_stream_topology,
	.send_text = chan_pjsip_sendtext,
	.send_text_data = chan_pjsip_sendtext_data,
	.send_digit_begin = chan_pjsip_digit_begin,
	.send_digit_end = chan_pjsip_digit_end,
	.call = chan_pjsip_call,
	.hangup = chan_pjsip_hangup,
	.answer = chan_pjsip_answer,
	.read_stream = chan_pjsip_read_stream,
	.write = chan_pjsip_write,
	.write_stream = chan_pjsip_write_stream,
	.exception = chan_pjsip_read_stream,
	.indicate = chan_pjsip_indicate,
	.transfer = chan_pjsip_transfer,
	.fixup = chan_pjsip_fixup,
	.devicestate = chan_pjsip_devicestate,
	.queryoption = chan_pjsip_queryoption,
	.func_channel_read = pjsip_acf_channel_read,
	.get_pvt_uniqueid = chan_pjsip_get_uniqueid,
	.properties = AST_CHAN_TP_WANTSJITTER | AST_CHAN_TP_CREATESJITTER | AST_CHAN_TP_SEND_TEXT_DATA
};

/*! \brief SIP session interaction functions */
static void chan_pjsip_session_begin(struct ast_sip_session *session);
static void chan_pjsip_session_end(struct ast_sip_session *session);
static int chan_pjsip_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata);
static void chan_pjsip_incoming_response(struct ast_sip_session *session, struct pjsip_rx_data *rdata);
static void chan_pjsip_incoming_response_update_cause(struct ast_sip_session *session, struct pjsip_rx_data *rdata);

/*! \brief SIP session supplement structure */
static struct ast_sip_session_supplement chan_pjsip_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL,
	.session_begin = chan_pjsip_session_begin,
	.session_end = chan_pjsip_session_end,
	.incoming_request = chan_pjsip_incoming_request,
	.incoming_response = chan_pjsip_incoming_response,
	/* It is important that this supplement runs after media has been negotiated */
	.response_priority = AST_SIP_SESSION_AFTER_MEDIA,
};

/*! \brief SIP session supplement structure just for responses */
static struct ast_sip_session_supplement chan_pjsip_supplement_response = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL,
	.incoming_response = chan_pjsip_incoming_response_update_cause,
	.response_priority = AST_SIP_SESSION_BEFORE_MEDIA | AST_SIP_SESSION_AFTER_MEDIA,
};

static int chan_pjsip_incoming_ack(struct ast_sip_session *session, struct pjsip_rx_data *rdata);

static struct ast_sip_session_supplement chan_pjsip_ack_supplement = {
	.method = "ACK",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL,
	.incoming_request = chan_pjsip_incoming_ack,
};

/*! \brief Function called by RTP engine to get local audio RTP peer */
static enum ast_rtp_glue_result chan_pjsip_get_rtp_peer(struct ast_channel *chan, struct ast_rtp_instance **instance)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	struct ast_sip_endpoint *endpoint;
	struct ast_datastore *datastore;
	struct ast_sip_session_media *media;

	if (!channel || !channel->session) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	/* XXX Getting the first RTP instance for direct media related stuff seems just
	 * absolutely wrong. But the native RTP bridge knows no other method than single-stream
	 * for direct media. So this is the best we can do.
	 */
	media = channel->session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];
	if (!media || !media->rtp) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	datastore = ast_sip_session_get_datastore(channel->session, "t38");
	if (datastore) {
		ao2_ref(datastore, -1);
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	endpoint = channel->session->endpoint;

	*instance = media->rtp;
	ao2_ref(*instance, +1);

	ast_assert(endpoint != NULL);
	if (endpoint->media.rtp.encryption != AST_SIP_MEDIA_ENCRYPT_NONE) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	if (endpoint->media.direct_media.enabled) {
		return AST_RTP_GLUE_RESULT_REMOTE;
	}

	return AST_RTP_GLUE_RESULT_LOCAL;
}

/*! \brief Function called by RTP engine to get local video RTP peer */
static enum ast_rtp_glue_result chan_pjsip_get_vrtp_peer(struct ast_channel *chan, struct ast_rtp_instance **instance)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	struct ast_sip_endpoint *endpoint;
	struct ast_sip_session_media *media;

	if (!channel || !channel->session) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	media = channel->session->active_media_state->default_session[AST_MEDIA_TYPE_VIDEO];
	if (!media || !media->rtp) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	endpoint = channel->session->endpoint;

	*instance = media->rtp;
	ao2_ref(*instance, +1);

	ast_assert(endpoint != NULL);
	if (endpoint->media.rtp.encryption != AST_SIP_MEDIA_ENCRYPT_NONE) {
		return AST_RTP_GLUE_RESULT_FORBID;
	}

	return AST_RTP_GLUE_RESULT_LOCAL;
}

/*! \brief Function called by RTP engine to get peer capabilities */
static void chan_pjsip_get_codec(struct ast_channel *chan, struct ast_format_cap *result)
{
	SCOPE_ENTER(1, "%s Native formats %s\n", ast_channel_name(chan),
		ast_str_tmp(AST_FORMAT_CAP_NAMES_LEN, ast_format_cap_get_names(ast_channel_nativeformats(chan), &STR_TMP)));
	ast_format_cap_append_from_cap(result, ast_channel_nativeformats(chan), AST_MEDIA_TYPE_UNKNOWN);
	SCOPE_EXIT_RTN();
}

/*! \brief Destructor function for \ref transport_info_data */
static void transport_info_destroy(void *obj)
{
	struct transport_info_data *data = obj;
	ast_free(data);
}

/*! \brief Datastore used to store local/remote addresses for the
 * INVITE request that created the PJSIP channel */
static struct ast_datastore_info transport_info = {
	.type = "chan_pjsip_transport_info",
	.destroy = transport_info_destroy,
};

static struct ast_datastore_info direct_media_mitigation_info = { };

static int direct_media_mitigate_glare(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);

	if (session->endpoint->media.direct_media.glare_mitigation ==
			AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_NONE) {
		return 0;
	}

	datastore = ast_sip_session_get_datastore(session, "direct_media_glare_mitigation");
	if (!datastore) {
		return 0;
	}

	/* Removing the datastore ensures we won't try to mitigate glare on subsequent reinvites */
	ast_sip_session_remove_datastore(session, "direct_media_glare_mitigation");

	if ((session->endpoint->media.direct_media.glare_mitigation ==
			AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_OUTGOING &&
			session->inv_session->role == PJSIP_ROLE_UAC) ||
			(session->endpoint->media.direct_media.glare_mitigation ==
			AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_INCOMING &&
			session->inv_session->role == PJSIP_ROLE_UAS)) {
		return 1;
	}

	return 0;
}

/*! \brief Helper function to find the position for RTCP */
static int rtp_find_rtcp_fd_position(struct ast_sip_session *session, struct ast_rtp_instance *rtp)
{
	int index;

	for (index = 0; index < AST_VECTOR_SIZE(&session->active_media_state->read_callbacks); ++index) {
		struct ast_sip_session_media_read_callback_state *callback_state =
			AST_VECTOR_GET_ADDR(&session->active_media_state->read_callbacks, index);

		if (callback_state->fd != ast_rtp_instance_fd(rtp, 1)) {
			continue;
		}

		return index;
	}

	return -1;
}

/*!
 * \pre chan is locked
 */
static int check_for_rtp_changes(struct ast_channel *chan, struct ast_rtp_instance *rtp,
		struct ast_sip_session_media *media, struct ast_sip_session *session)
{
	int changed = 0, position = -1;

	if (media->rtp) {
		position = rtp_find_rtcp_fd_position(session, media->rtp);
	}

	if (rtp) {
		changed = ast_rtp_instance_get_and_cmp_remote_address(rtp, &media->direct_media_addr);
		if (media->rtp) {
			if (position != -1) {
				ast_channel_set_fd(chan, position + AST_EXTENDED_FDS, -1);
			}
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_RTCP, 0);
		}
	} else if (!ast_sockaddr_isnull(&media->direct_media_addr)){
		ast_sockaddr_setnull(&media->direct_media_addr);
		changed = 1;
		if (media->rtp) {
			ast_rtp_instance_set_prop(media->rtp, AST_RTP_PROPERTY_RTCP, 1);
			if (position != -1) {
				ast_channel_set_fd(chan, position + AST_EXTENDED_FDS, ast_rtp_instance_fd(media->rtp, 1));
			}
		}
	}

	return changed;
}

struct rtp_direct_media_data {
	struct ast_channel *chan;
	struct ast_rtp_instance *rtp;
	struct ast_rtp_instance *vrtp;
	struct ast_format_cap *cap;
	struct ast_sip_session *session;
};

static void rtp_direct_media_data_destroy(void *data)
{
	struct rtp_direct_media_data *cdata = data;

	ao2_cleanup(cdata->session);
	ao2_cleanup(cdata->cap);
	ao2_cleanup(cdata->vrtp);
	ao2_cleanup(cdata->rtp);
	ao2_cleanup(cdata->chan);
}

static struct rtp_direct_media_data *rtp_direct_media_data_create(
	struct ast_channel *chan, struct ast_rtp_instance *rtp, struct ast_rtp_instance *vrtp,
	const struct ast_format_cap *cap, struct ast_sip_session *session)
{
	struct rtp_direct_media_data *cdata = ao2_alloc(sizeof(*cdata), rtp_direct_media_data_destroy);

	if (!cdata) {
		return NULL;
	}

	cdata->chan = ao2_bump(chan);
	cdata->rtp = ao2_bump(rtp);
	cdata->vrtp = ao2_bump(vrtp);
	cdata->cap = ao2_bump((struct ast_format_cap *)cap);
	cdata->session = ao2_bump(session);

	return cdata;
}

static int send_direct_media_request(void *data)
{
	struct rtp_direct_media_data *cdata = data;
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(cdata->chan);
	struct ast_sip_session *session;
	int changed = 0;
	int res = 0;

	/* XXX In an ideal world each media stream would be direct, but for now preserve behavior
	 * and connect only the default media sessions for audio and video.
	 */

	/* The channel needs to be locked when checking for RTP changes.
	 * Otherwise, we could end up destroying an underlying RTCP structure
	 * at the same time that the channel thread is attempting to read RTCP
	 */
	ast_channel_lock(cdata->chan);
	session = channel->session;
	if (session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO]) {
		changed |= check_for_rtp_changes(
			cdata->chan, cdata->rtp, session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO], session);
	}
	if (session->active_media_state->default_session[AST_MEDIA_TYPE_VIDEO]) {
		changed |= check_for_rtp_changes(
			cdata->chan, cdata->vrtp, session->active_media_state->default_session[AST_MEDIA_TYPE_VIDEO], session);
	}
	ast_channel_unlock(cdata->chan);

	if (direct_media_mitigate_glare(cdata->session)) {
		ast_debug(4, "Disregarding setting RTP on %s: mitigating re-INVITE glare\n", ast_channel_name(cdata->chan));
		ao2_ref(cdata, -1);
		return 0;
	}

	if (cdata->cap && ast_format_cap_count(cdata->cap) &&
	    !ast_format_cap_identical(cdata->session->direct_media_cap, cdata->cap)) {
		ast_format_cap_remove_by_type(cdata->session->direct_media_cap, AST_MEDIA_TYPE_UNKNOWN);
		ast_format_cap_append_from_cap(cdata->session->direct_media_cap, cdata->cap, AST_MEDIA_TYPE_UNKNOWN);
		changed = 1;
	}

	if (changed) {
		ast_debug(4, "RTP changed on %s; initiating direct media update\n", ast_channel_name(cdata->chan));
		res = ast_sip_session_refresh(cdata->session, NULL, NULL, NULL,
			cdata->session->endpoint->media.direct_media.method, 1, NULL);
	}

	ao2_ref(cdata, -1);
	return res;
}

/*! \brief Function called by RTP engine to change where the remote party should send media */
static int chan_pjsip_set_rtp_peer(struct ast_channel *chan,
		struct ast_rtp_instance *rtp,
		struct ast_rtp_instance *vrtp,
		struct ast_rtp_instance *tpeer,
		const struct ast_format_cap *cap,
		int nat_active)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	struct ast_sip_session *session = channel->session;
	struct rtp_direct_media_data *cdata;
	SCOPE_ENTER(1, "%s %s\n", ast_channel_name(chan),
		ast_str_tmp(AST_FORMAT_CAP_NAMES_LEN, ast_format_cap_get_names(cap, &STR_TMP)));

	/* Don't try to do any direct media shenanigans on early bridges */
	if ((rtp || vrtp || tpeer) && !ast_channel_is_bridged(chan)) {
		ast_debug(4, "Disregarding setting RTP on %s: channel is not bridged\n", ast_channel_name(chan));
		SCOPE_EXIT_RTN_VALUE(0, "Channel not bridged\n");
	}

	if (nat_active && session->endpoint->media.direct_media.disable_on_nat) {
		ast_debug(4, "Disregarding setting RTP on %s: NAT is active\n", ast_channel_name(chan));
		SCOPE_EXIT_RTN_VALUE(0, "NAT is active\n");
	}

	cdata = rtp_direct_media_data_create(chan, rtp, vrtp, cap, session);
	if (!cdata) {
		SCOPE_EXIT_RTN_VALUE(0);
	}

	if (ast_sip_push_task(session->serializer, send_direct_media_request, cdata)) {
		ast_log(LOG_ERROR, "Unable to send direct media request for channel %s\n", ast_channel_name(chan));
		ao2_ref(cdata, -1);
	}

	SCOPE_EXIT_RTN_VALUE(0);
}

/*! \brief Local glue for interacting with the RTP engine core */
static struct ast_rtp_glue chan_pjsip_rtp_glue = {
	.type = "PJSIP",
	.get_rtp_info = chan_pjsip_get_rtp_peer,
	.get_vrtp_info = chan_pjsip_get_vrtp_peer,
	.get_codec = chan_pjsip_get_codec,
	.update_peer = chan_pjsip_set_rtp_peer,
};

static void set_channel_on_rtp_instance(const struct ast_sip_session *session,
	const char *channel_id)
{
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&session->active_media_state->sessions); ++i) {
		struct ast_sip_session_media *session_media;

		session_media = AST_VECTOR_GET(&session->active_media_state->sessions, i);
		if (!session_media || !session_media->rtp) {
			continue;
		}

		ast_rtp_instance_set_channel_id(session_media->rtp, channel_id);
	}
}

/*!
 * \brief Determine if a topology is compatible with format capabilities
 *
 * This will return true if ANY formats in the topology are compatible with the format
 * capabilities.
 *
 * XXX When supporting true multistream, we will need to be sure to mark which streams from
 * top1 are compatible with which streams from top2. Then the ones that are not compatible
 * will need to be marked as "removed" so that they are negotiated as expected.
 *
 * \param top Topology
 * \param cap Format capabilities
 * \retval 1 The topology has at least one compatible format
 * \retval 0 The topology has no compatible formats or an error occurred.
 */
static int compatible_formats_exist(struct ast_stream_topology *top, struct ast_format_cap *cap)
{
	struct ast_format_cap *cap_from_top;
	int res;
	SCOPE_ENTER(1, "Topology: %s Formats: %s\n",
		ast_str_tmp(AST_FORMAT_CAP_NAMES_LEN, ast_stream_topology_to_str(top, &STR_TMP)),
		ast_str_tmp(AST_FORMAT_CAP_NAMES_LEN, ast_format_cap_get_names(cap, &STR_TMP)));

	cap_from_top = ast_stream_topology_get_formats(top);

	if (!cap_from_top) {
		SCOPE_EXIT_RTN_VALUE(0, "Topology had no formats\n");
	}

	res = ast_format_cap_iscompatible(cap_from_top, cap);
	ao2_ref(cap_from_top, -1);

	SCOPE_EXIT_RTN_VALUE(res, "Compatible? %s\n", res ? "yes" : "no");
}

/*! \brief Function called to create a new PJSIP Asterisk channel */
static struct ast_channel *chan_pjsip_new(struct ast_sip_session *session, int state, const char *exten, const char *title, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *cid_name)
{
	struct ast_channel *chan;
	struct ast_format_cap *caps;
	RAII_VAR(struct chan_pjsip_pvt *, pvt, NULL, ao2_cleanup);
	struct ast_sip_channel_pvt *channel;
	struct ast_variable *var;
	struct ast_stream_topology *topology;
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	if (!(pvt = ao2_alloc_options(sizeof(*pvt), chan_pjsip_pvt_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK))) {
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't create pvt\n");
	}

	chan = ast_channel_alloc_with_endpoint(1, state,
		S_COR(session->id.number.valid, session->id.number.str, ""),
		S_COR(session->id.name.valid, session->id.name.str, ""),
		session->endpoint->accountcode,
		exten, session->endpoint->context,
		assignedids, requestor, 0,
		session->endpoint->persistent, "PJSIP/%s-%08x",
		ast_sorcery_object_get_id(session->endpoint),
		(unsigned) ast_atomic_fetchadd_int((int *) &chan_idx, +1));
	if (!chan) {
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't create channel\n");
	}

	ast_channel_tech_set(chan, &chan_pjsip_tech);

	if (!(channel = ast_sip_channel_pvt_alloc(pvt, session))) {
		ast_channel_unlock(chan);
		ast_hangup(chan);
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't create pvt channel\n");
	}

	ast_channel_tech_pvt_set(chan, channel);

	if (!ast_stream_topology_get_count(session->pending_media_state->topology) ||
		!compatible_formats_exist(session->pending_media_state->topology, session->endpoint->media.codecs)) {
		caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!caps) {
			ast_channel_unlock(chan);
			ast_hangup(chan);
			SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't create caps\n");
		}
		ast_format_cap_append_from_cap(caps, session->endpoint->media.codecs, AST_MEDIA_TYPE_UNKNOWN);
		topology = ast_stream_topology_clone(session->endpoint->media.topology);
	} else {
		caps = ast_stream_topology_get_formats(session->pending_media_state->topology);
		topology = ast_stream_topology_clone(session->pending_media_state->topology);
	}

	if (!topology || !caps) {
		ao2_cleanup(caps);
		ast_stream_topology_free(topology);
		ast_channel_unlock(chan);
		ast_hangup(chan);
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't get caps or clone topology\n");
	}

	ast_channel_stage_snapshot(chan);

	ast_channel_nativeformats_set(chan, caps);
	ast_channel_set_stream_topology(chan, topology);

	if (!ast_format_cap_empty(caps)) {
		struct ast_format *fmt;

		fmt = ast_format_cap_get_best_by_type(caps, AST_MEDIA_TYPE_AUDIO);
		if (!fmt) {
			/* Since our capabilities aren't empty, this will succeed */
			fmt = ast_format_cap_get_format(caps, 0);
		}
		ast_channel_set_writeformat(chan, fmt);
		ast_channel_set_rawwriteformat(chan, fmt);
		ast_channel_set_readformat(chan, fmt);
		ast_channel_set_rawreadformat(chan, fmt);
		ao2_ref(fmt, -1);
	}

	ao2_ref(caps, -1);

	if (state == AST_STATE_RING) {
		ast_channel_rings_set(chan, 1);
	}

	ast_channel_adsicpe_set(chan, AST_ADSI_UNAVAILABLE);

	ast_party_id_copy(&ast_channel_caller(chan)->id, &session->id);
	ast_party_id_copy(&ast_channel_caller(chan)->ani, &session->id);
	ast_channel_caller(chan)->ani2 = session->ani2;

	if (!ast_strlen_zero(exten)) {
		/* Set provided DNID on the new channel. */
		ast_channel_dialed(chan)->number.str = ast_strdup(exten);
	}

	ast_channel_priority_set(chan, 1);

	ast_channel_callgroup_set(chan, session->endpoint->pickup.callgroup);
	ast_channel_pickupgroup_set(chan, session->endpoint->pickup.pickupgroup);

	ast_channel_named_callgroups_set(chan, session->endpoint->pickup.named_callgroups);
	ast_channel_named_pickupgroups_set(chan, session->endpoint->pickup.named_pickupgroups);

	if (!ast_strlen_zero(session->endpoint->language)) {
		ast_channel_language_set(chan, session->endpoint->language);
	}

	if (!ast_strlen_zero(session->endpoint->zone)) {
		struct ast_tone_zone *zone = ast_get_indication_zone(session->endpoint->zone);
		if (!zone) {
			ast_log(LOG_ERROR, "Unknown country code '%s' for tonezone. Check indications.conf for available country codes.\n", session->endpoint->zone);
		}
		ast_channel_zone_set(chan, zone);
	}

	for (var = session->endpoint->channel_vars; var; var = var->next) {
		char buf[512];
		pbx_builtin_setvar_helper(chan, var->name, ast_get_encoded_str(
						  var->value, buf, sizeof(buf)));
	}

	ast_channel_stage_snapshot_done(chan);
	ast_channel_unlock(chan);

	set_channel_on_rtp_instance(session, ast_channel_uniqueid(chan));

	SCOPE_EXIT_RTN_VALUE(chan);
}

struct answer_data {
	struct ast_sip_session *session;
	unsigned long indent;
};

static int answer(void *data)
{
	struct answer_data *ans_data = data;
	pj_status_t status = PJ_SUCCESS;
	pjsip_tx_data *packet = NULL;
	struct ast_sip_session *session = ans_data->session;
	SCOPE_ENTER_TASK(1, ans_data->indent, "%s\n", ast_sip_session_get_name(session));

	if (session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Session already DISCONNECTED [reason=%d (%s)]\n",
			session->inv_session->cause,
			pjsip_get_status_text(session->inv_session->cause)->ptr);
		SCOPE_EXIT_RTN_VALUE(0, "Disconnected\n");
	}

	pjsip_dlg_inc_lock(session->inv_session->dlg);
	if (session->inv_session->invite_tsx) {
		status = pjsip_inv_answer(session->inv_session, 200, NULL, NULL, &packet);
	} else {
		ast_log(LOG_ERROR,"Cannot answer '%s' because there is no associated SIP transaction\n",
			ast_channel_name(session->channel));
	}
	pjsip_dlg_dec_lock(session->inv_session->dlg);

	if (status == PJ_SUCCESS && packet) {
		ast_sip_session_send_response(session, packet);
	}

	if (status != PJ_SUCCESS) {
		char err[PJ_ERR_MSG_SIZE];

		pj_strerror(status, err, sizeof(err));
		ast_log(LOG_WARNING,"Cannot answer '%s': %s\n",
			ast_channel_name(session->channel), err);
		/*
		 * Return this value so we can distinguish between this
		 * failure and the threadpool synchronous push failing.
		 */
		SCOPE_EXIT_RTN_VALUE(-2, "pjproject failure\n");
	}
	SCOPE_EXIT_RTN_VALUE(0);
}

/*! \brief Function called by core when we should answer a PJSIP session */
static int chan_pjsip_answer(struct ast_channel *ast)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session;
	struct answer_data ans_data = { 0, };
	int res;
	SCOPE_ENTER(1, "%s\n", ast_channel_name(ast));

	if (ast_channel_state(ast) == AST_STATE_UP) {
		SCOPE_EXIT_RTN_VALUE(0, "Already up\n");
		return 0;
	}

	ast_setstate(ast, AST_STATE_UP);
	session = ao2_bump(channel->session);

	/* the answer task needs to be pushed synchronously otherwise a race condition
	   can occur between this thread and bridging (specifically when native bridging
	   attempts to do direct media) */
	ast_channel_unlock(ast);
	ans_data.session = session;
	ans_data.indent = ast_trace_get_indent();
	res = ast_sip_push_task_wait_serializer(session->serializer, answer, &ans_data);
	if (res) {
		if (res == -1) {
			ast_log(LOG_ERROR,"Cannot answer '%s': Unable to push answer task to the threadpool.\n",
				ast_channel_name(session->channel));
		}
		ao2_ref(session, -1);
		ast_channel_lock(ast);
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't push task\n");
	}
	ao2_ref(session, -1);
	ast_channel_lock(ast);

	SCOPE_EXIT_RTN_VALUE(0);
}

/*! \brief Internal helper function called when CNG tone is detected */
static struct ast_frame *chan_pjsip_cng_tone_detected(struct ast_channel *ast, struct ast_sip_session *session,
	struct ast_frame *f)
{
	const char *target_context;
	int exists;
	int dsp_features;

	dsp_features = ast_dsp_get_features(session->dsp);
	dsp_features &= ~DSP_FEATURE_FAX_DETECT;
	if (dsp_features) {
		ast_dsp_set_features(session->dsp, dsp_features);
	} else {
		ast_dsp_free(session->dsp);
		session->dsp = NULL;
	}

	/* If already executing in the fax extension don't do anything */
	if (!strcmp(ast_channel_exten(ast), "fax")) {
		return f;
	}

	target_context = S_OR(ast_channel_macrocontext(ast), ast_channel_context(ast));

	/*
	 * We need to unlock the channel here because ast_exists_extension has the
	 * potential to start and stop an autoservice on the channel. Such action
	 * is prone to deadlock if the channel is locked.
	 *
	 * ast_async_goto() has its own restriction on not holding the channel lock.
	 */
	ast_channel_unlock(ast);
	ast_frfree(f);
	f = &ast_null_frame;
	exists = ast_exists_extension(ast, target_context, "fax", 1,
		S_COR(ast_channel_caller(ast)->id.number.valid,
			ast_channel_caller(ast)->id.number.str, NULL));
	if (exists) {
		ast_verb(2, "Redirecting '%s' to fax extension due to CNG detection\n",
			ast_channel_name(ast));
		pbx_builtin_setvar_helper(ast, "FAXEXTEN", ast_channel_exten(ast));
		if (ast_async_goto(ast, target_context, "fax", 1)) {
			ast_log(LOG_ERROR, "Failed to async goto '%s' into fax extension in '%s'\n",
				ast_channel_name(ast), target_context);
		}
	} else {
		ast_log(LOG_NOTICE, "FAX CNG detected on '%s' but no fax extension in '%s'\n",
			ast_channel_name(ast), target_context);
	}

	/* It's possible for a masquerade to have occurred when doing the ast_async_goto resulting in
	 * the channel on the session having changed. Since we need to return with the original channel
	 * locked we lock the channel that was passed in and not session->channel.
	 */
	ast_channel_lock(ast);

	return f;
}

/*! \brief Determine if the given frame is in a format we've negotiated */
static int is_compatible_format(struct ast_sip_session *session, struct ast_frame *f)
{
	struct ast_stream_topology *topology = session->active_media_state->topology;
	struct ast_stream *stream = ast_stream_topology_get_stream(topology, f->stream_num);
	const struct ast_format_cap *cap = ast_stream_get_formats(stream);

	return ast_format_cap_iscompatible_format(cap, f->subclass.format) != AST_FORMAT_CMP_NOT_EQUAL;
}

/*!
 * \brief Function called by core to read any waiting frames
 *
 * \note The channel is already locked.
 */
static struct ast_frame *chan_pjsip_read_stream(struct ast_channel *ast)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session = channel->session;
	struct ast_sip_session_media_read_callback_state *callback_state;
	struct ast_frame *f;
	int fdno = ast_channel_fdno(ast) - AST_EXTENDED_FDS;
	struct ast_frame *cur;

	if (fdno >= AST_VECTOR_SIZE(&session->active_media_state->read_callbacks)) {
		return &ast_null_frame;
	}

	callback_state = AST_VECTOR_GET_ADDR(&session->active_media_state->read_callbacks, fdno);
	f = callback_state->read_callback(session, callback_state->session);

	if (!f) {
		return f;
	}

	for (cur = f; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
		if (cur->frametype == AST_FRAME_VOICE) {
			break;
		}
	}

	if (!cur || callback_state->session != session->active_media_state->default_session[callback_state->session->type]) {
		return f;
	}

	session = channel->session;

	/*
	 * Asymmetric RTP only has one native format set at a time.
	 * Therefore we need to update the native format to the current
	 * raw read format BEFORE the native format check
	 */
	if (!session->endpoint->asymmetric_rtp_codec &&
		ast_format_cmp(ast_channel_rawwriteformat(ast), cur->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL &&
		is_compatible_format(session, cur)) {
		struct ast_format_cap *caps;

		/* For maximum compatibility we ensure that the formats match that of the received media */
		ast_debug(1, "Oooh, got a frame with format of %s on channel '%s' when we're sending '%s', switching to match\n",
			ast_format_get_name(cur->subclass.format), ast_channel_name(ast),
			ast_format_get_name(ast_channel_rawwriteformat(ast)));

		caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (caps) {
			ast_format_cap_append_from_cap(caps, ast_channel_nativeformats(ast), AST_MEDIA_TYPE_UNKNOWN);
			ast_format_cap_remove_by_type(caps, AST_MEDIA_TYPE_AUDIO);
			ast_format_cap_append(caps, cur->subclass.format, 0);
			ast_channel_nativeformats_set(ast, caps);
			ao2_ref(caps, -1);
		}

		ast_set_write_format_path(ast, ast_channel_writeformat(ast), cur->subclass.format);
		ast_set_read_format_path(ast, ast_channel_readformat(ast), cur->subclass.format);

		if (ast_channel_is_bridged(ast)) {
			ast_channel_set_unbridged_nolock(ast, 1);
		}
	}

	if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), cur->subclass.format)
			== AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Oooh, got a frame with format of %s on channel '%s' when it has not been negotiated\n",
				ast_format_get_name(cur->subclass.format), ast_channel_name(ast));
		ast_frfree(f);
		return &ast_null_frame;
	}

	if (session->dsp) {
		int dsp_features;

		dsp_features = ast_dsp_get_features(session->dsp);
		if ((dsp_features & DSP_FEATURE_FAX_DETECT)
			&& session->endpoint->faxdetect_timeout
			&& session->endpoint->faxdetect_timeout <= ast_channel_get_up_time(ast)) {
			dsp_features &= ~DSP_FEATURE_FAX_DETECT;
			if (dsp_features) {
				ast_dsp_set_features(session->dsp, dsp_features);
			} else {
				ast_dsp_free(session->dsp);
				session->dsp = NULL;
			}
			ast_debug(3, "Channel driver fax CNG detection timeout on %s\n",
				ast_channel_name(ast));
		}
	}
	if (session->dsp) {
		f = ast_dsp_process(ast, session->dsp, f);
		if (f && (f->frametype == AST_FRAME_DTMF)) {
			if (f->subclass.integer == 'f') {
				ast_debug(3, "Channel driver fax CNG detected on %s\n",
					ast_channel_name(ast));
				f = chan_pjsip_cng_tone_detected(ast, session, f);
				/* When chan_pjsip_cng_tone_detected returns it is possible for the
				 * channel pointed to by ast and by session->channel to differ due to a
				 * masquerade. It's best not to touch things after this.
				 */
			} else {
				ast_debug(3, "* Detected inband DTMF '%c' on '%s'\n", f->subclass.integer,
					ast_channel_name(ast));
			}
		}
	}

	return f;
}

static int chan_pjsip_write_stream(struct ast_channel *ast, int stream_num, struct ast_frame *frame)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	struct ast_sip_session *session = channel->session;
	struct ast_sip_session_media *media = NULL;
	int res = 0;

	/* The core provides a guarantee that the stream will exist when we are called if stream_num is provided */
	if (stream_num >= 0) {
		/* What is not guaranteed is that a media session will exist */
		if (stream_num < AST_VECTOR_SIZE(&channel->session->active_media_state->sessions)) {
			media = AST_VECTOR_GET(&channel->session->active_media_state->sessions, stream_num);
		}
	}

	switch (frame->frametype) {
	case AST_FRAME_VOICE:
		if (!media) {
			return 0;
		} else if (media->type != AST_MEDIA_TYPE_AUDIO) {
			ast_debug(3, "Channel %s stream %d is of type '%s', not audio!\n",
				ast_channel_name(ast), stream_num, ast_codec_media_type2str(media->type));
			return 0;
		} else if (media == channel->session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO] &&
			ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), frame->subclass.format) == AST_FORMAT_CMP_NOT_EQUAL) {
			struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);
			struct ast_str *write_transpath = ast_str_alloca(256);
			struct ast_str *read_transpath = ast_str_alloca(256);

			ast_log(LOG_WARNING,
				"Channel %s asked to send %s frame when native formats are %s (rd:%s->%s;%s wr:%s->%s;%s)\n",
				ast_channel_name(ast),
				ast_format_get_name(frame->subclass.format),
				ast_format_cap_get_names(ast_channel_nativeformats(ast), &cap_buf),
				ast_format_get_name(ast_channel_rawreadformat(ast)),
				ast_format_get_name(ast_channel_readformat(ast)),
				ast_translate_path_to_str(ast_channel_readtrans(ast), &read_transpath),
				ast_format_get_name(ast_channel_writeformat(ast)),
				ast_format_get_name(ast_channel_rawwriteformat(ast)),
				ast_translate_path_to_str(ast_channel_writetrans(ast), &write_transpath));
			return 0;
		} else if (media->write_callback) {
			res = media->write_callback(session, media, frame);

		}
		break;
	case AST_FRAME_VIDEO:
		if (!media) {
			return 0;
		} else if (media->type != AST_MEDIA_TYPE_VIDEO) {
			ast_debug(3, "Channel %s stream %d is of type '%s', not video!\n",
				ast_channel_name(ast), stream_num, ast_codec_media_type2str(media->type));
			return 0;
		} else if (media->write_callback) {
			res = media->write_callback(session, media, frame);
		}
		break;
	case AST_FRAME_MODEM:
		if (!media) {
			return 0;
		} else if (media->type != AST_MEDIA_TYPE_IMAGE) {
			ast_debug(3, "Channel %s stream %d is of type '%s', not image!\n",
				ast_channel_name(ast), stream_num, ast_codec_media_type2str(media->type));
			return 0;
		} else if (media->write_callback) {
			res = media->write_callback(session, media, frame);
		}
		break;
	case AST_FRAME_CNG:
		break;
	case AST_FRAME_RTCP:
		/* We only support writing out feedback */
		if (frame->subclass.integer != AST_RTP_RTCP_PSFB || !media) {
			return 0;
		} else if (media->type != AST_MEDIA_TYPE_VIDEO) {
			ast_debug(3, "Channel %s stream %d is of type '%s', not video! Unable to write RTCP feedback.\n",
				ast_channel_name(ast), stream_num, ast_codec_media_type2str(media->type));
			return 0;
		} else if (media->write_callback) {
			res = media->write_callback(session, media, frame);
		}
		break;
	default:
		ast_log(LOG_WARNING, "Can't send %u type frames with PJSIP\n", frame->frametype);
		break;
	}

	return res;
}

static int chan_pjsip_write(struct ast_channel *ast, struct ast_frame *frame)
{
	return chan_pjsip_write_stream(ast, -1, frame);
}

/*! \brief Function called by core to change the underlying owner channel */
static int chan_pjsip_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(newchan);

	if (channel->session->channel != oldchan) {
		return -1;
	}

	/*
	 * The masquerade has suspended the channel's session
	 * serializer so we can safely change it outside of
	 * the serializer thread.
	 */
	channel->session->channel = newchan;

	set_channel_on_rtp_instance(channel->session, ast_channel_uniqueid(newchan));

	return 0;
}

/*! AO2 hash function for on hold UIDs */
static int uid_hold_hash_fn(const void *obj, const int flags)
{
	const char *key = obj;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		break;
	case OBJ_SEARCH_OBJECT:
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! AO2 sort function for on hold UIDs */
static int uid_hold_sort_fn(const void *obj_left, const void *obj_right, const int flags)
{
	const char *left = obj_left;
	const char *right = obj_right;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
	case OBJ_SEARCH_KEY:
		cmp = strcmp(left, right);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(left, right, strlen(right));
		break;
	default:
		/* Sort can only work on something with a full or partial key. */
		ast_assert(0);
		cmp = 0;
		break;
	}
	return cmp;
}

static struct ao2_container *pjsip_uids_onhold;

/*!
 * \brief Add a channel ID to the list of PJSIP channels on hold
 *
 * \param chan_uid - Unique ID of the channel being put into the hold list
 *
 * \retval 0 Channel has been added to or was already in the hold list
 * \retval -1 Failed to add channel to the hold list
 */
static int chan_pjsip_add_hold(const char *chan_uid)
{
	RAII_VAR(char *, hold_uid, NULL, ao2_cleanup);

	hold_uid = ao2_find(pjsip_uids_onhold, chan_uid, OBJ_SEARCH_KEY);
	if (hold_uid) {
		/* Device is already on hold. Nothing to do. */
		return 0;
	}

	/* Device wasn't in hold list already. Create a new one. */
	hold_uid = ao2_alloc_options(strlen(chan_uid) + 1, NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!hold_uid) {
		return -1;
	}

	ast_copy_string(hold_uid, chan_uid, strlen(chan_uid) + 1);

	if (ao2_link(pjsip_uids_onhold, hold_uid) == 0) {
		return -1;
	}

	return 0;
}

/*!
 * \brief Remove a channel ID from the list of PJSIP channels on hold
 *
 * \param chan_uid - Unique ID of the channel being taken out of the hold list
 */
static void chan_pjsip_remove_hold(const char *chan_uid)
{
	ao2_find(pjsip_uids_onhold, chan_uid, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
}

/*!
 * \brief Determine whether a channel ID is in the list of PJSIP channels on hold
 *
 * \param chan_uid - Channel being checked
 *
 * \retval 0 The channel is not in the hold list
 * \retval 1 The channel is in the hold list
 */
static int chan_pjsip_get_hold(const char *chan_uid)
{
	RAII_VAR(char *, hold_uid, NULL, ao2_cleanup);

	hold_uid = ao2_find(pjsip_uids_onhold, chan_uid, OBJ_SEARCH_KEY);
	if (!hold_uid) {
		return 0;
	}

	return 1;
}

/*! \brief Function called to get the device state of an endpoint */
static int chan_pjsip_devicestate(const char *data)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", data), ao2_cleanup);
	enum ast_device_state state = AST_DEVICE_UNKNOWN;
	RAII_VAR(struct ast_endpoint_snapshot *, endpoint_snapshot, NULL, ao2_cleanup);
	struct ast_devstate_aggregate aggregate;
	int num, inuse = 0;

	if (!endpoint) {
		return AST_DEVICE_INVALID;
	}

	endpoint_snapshot = ast_endpoint_latest_snapshot(ast_endpoint_get_tech(endpoint->persistent),
		ast_endpoint_get_resource(endpoint->persistent));

	if (!endpoint_snapshot) {
		return AST_DEVICE_INVALID;
	}

	if (endpoint_snapshot->state == AST_ENDPOINT_OFFLINE) {
		state = AST_DEVICE_UNAVAILABLE;
	} else if (endpoint_snapshot->state == AST_ENDPOINT_ONLINE) {
		state = AST_DEVICE_NOT_INUSE;
	}

	if (!endpoint_snapshot->num_channels) {
		return state;
	}

	ast_devstate_aggregate_init(&aggregate);

	for (num = 0; num < endpoint_snapshot->num_channels; num++) {
		struct ast_channel_snapshot *snapshot;

		snapshot = ast_channel_snapshot_get_latest(endpoint_snapshot->channel_ids[num]);
		if (!snapshot) {
			continue;
		}

		if (chan_pjsip_get_hold(snapshot->base->uniqueid)) {
			ast_devstate_aggregate_add(&aggregate, AST_DEVICE_ONHOLD);
		} else {
			ast_devstate_aggregate_add(&aggregate, ast_state_chan2dev(snapshot->state));
		}

		if ((snapshot->state == AST_STATE_UP) || (snapshot->state == AST_STATE_RING) ||
			(snapshot->state == AST_STATE_BUSY)) {
			inuse++;
		}

		ao2_ref(snapshot, -1);
	}

	if (endpoint->devicestate_busy_at && (inuse == endpoint->devicestate_busy_at)) {
		state = AST_DEVICE_BUSY;
	} else if (ast_devstate_aggregate_result(&aggregate) != AST_DEVICE_INVALID) {
		state = ast_devstate_aggregate_result(&aggregate);
	}

	return state;
}

/*! \brief Function called to query options on a channel */
static int chan_pjsip_queryoption(struct ast_channel *ast, int option, void *data, int *datalen)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	int res = -1;
	enum ast_t38_state state = T38_STATE_UNAVAILABLE;

	if (!channel) {
		return -1;
	}

	switch (option) {
	case AST_OPTION_T38_STATE:
		if (channel->session->endpoint->media.t38.enabled) {
			switch (channel->session->t38state) {
			case T38_LOCAL_REINVITE:
			case T38_PEER_REINVITE:
				state = T38_STATE_NEGOTIATING;
				break;
			case T38_ENABLED:
				state = T38_STATE_NEGOTIATED;
				break;
			case T38_REJECTED:
				state = T38_STATE_REJECTED;
				break;
			default:
				state = T38_STATE_UNKNOWN;
				break;
			}
		}

		*((enum ast_t38_state *) data) = state;
		res = 0;

		break;
	default:
		break;
	}

	return res;
}

static const char *chan_pjsip_get_uniqueid(struct ast_channel *ast)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	char *uniqueid = ast_threadstorage_get(&uniqueid_threadbuf, UNIQUEID_BUFSIZE);

	if (!channel || !uniqueid) {
		return "";
	}

	ast_copy_pj_str(uniqueid, &channel->session->inv_session->dlg->call_id->id, UNIQUEID_BUFSIZE);

	return uniqueid;
}

struct indicate_data {
	struct ast_sip_session *session;
	int condition;
	int response_code;
	void *frame_data;
	size_t datalen;
};

static void indicate_data_destroy(void *obj)
{
	struct indicate_data *ind_data = obj;

	ast_free(ind_data->frame_data);
	ao2_ref(ind_data->session, -1);
}

static struct indicate_data *indicate_data_alloc(struct ast_sip_session *session,
		int condition, int response_code, const void *frame_data, size_t datalen)
{
	struct indicate_data *ind_data = ao2_alloc(sizeof(*ind_data), indicate_data_destroy);

	if (!ind_data) {
		return NULL;
	}

	ind_data->frame_data = ast_malloc(datalen);
	if (!ind_data->frame_data) {
		ao2_ref(ind_data, -1);
		return NULL;
	}

	memcpy(ind_data->frame_data, frame_data, datalen);
	ind_data->datalen = datalen;
	ind_data->condition = condition;
	ind_data->response_code = response_code;
	ao2_ref(session, +1);
	ind_data->session = session;

	return ind_data;
}

static int indicate(void *data)
{
	pjsip_tx_data *packet = NULL;
	struct indicate_data *ind_data = data;
	struct ast_sip_session *session = ind_data->session;
	int response_code = ind_data->response_code;

	if ((session->inv_session->state != PJSIP_INV_STATE_DISCONNECTED) &&
		(pjsip_inv_answer(session->inv_session, response_code, NULL, NULL, &packet) == PJ_SUCCESS)) {
		ast_sip_session_send_response(session, packet);
	}

	ao2_ref(ind_data, -1);

	return 0;
}

/*! \brief Send SIP INFO with video update request */
static int transmit_info_with_vidupdate(void *data)
{
	const char * xml =
		"<?xml version=\"1.0\" encoding=\"utf-8\" ?>\r\n"
		" <media_control>\r\n"
		"  <vc_primitive>\r\n"
		"   <to_encoder>\r\n"
		"    <picture_fast_update/>\r\n"
		"   </to_encoder>\r\n"
		"  </vc_primitive>\r\n"
		" </media_control>\r\n";

	const struct ast_sip_body body = {
		.type = "application",
		.subtype = "media_control+xml",
		.body_text = xml
	};

	RAII_VAR(struct ast_sip_session *, session, data, ao2_cleanup);
	struct pjsip_tx_data *tdata;

	if (session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Session already DISCONNECTED [reason=%d (%s)]\n",
			session->inv_session->cause,
			pjsip_get_status_text(session->inv_session->cause)->ptr);
		return -1;
	}

	if (ast_sip_create_request("INFO", session->inv_session->dlg, session->endpoint, NULL, NULL, &tdata)) {
		ast_log(LOG_ERROR, "Could not create text video update INFO request\n");
		return -1;
	}
	if (ast_sip_add_body(tdata, &body)) {
		ast_log(LOG_ERROR, "Could not add body to text video update INFO request\n");
		return -1;
	}
	ast_sip_session_send_request(session, tdata);

	return 0;
}

/*!
 * \internal
 * \brief TRUE if a COLP update can be sent to the peer.
 * \since 13.3.0
 *
 * \param session The session to see if the COLP update is allowed.
 *
 * \retval 0 Update is not allowed.
 * \retval 1 Update is allowed.
 */
static int is_colp_update_allowed(struct ast_sip_session *session)
{
	struct ast_party_id connected_id;
	int update_allowed = 0;

	if (!session->endpoint->id.send_connected_line
		|| (!session->endpoint->id.send_pai && !session->endpoint->id.send_rpid)) {
		return 0;
	}

	/*
	 * Check if privacy allows the update.  Check while the channel
	 * is locked so we can work with the shallow connected_id copy.
	 */
	ast_channel_lock(session->channel);
	connected_id = ast_channel_connected_effective_id(session->channel);
	if (connected_id.number.valid
		&& (session->endpoint->id.trust_outbound
			|| (ast_party_id_presentation(&connected_id) & AST_PRES_RESTRICTION) == AST_PRES_ALLOWED)) {
		update_allowed = 1;
	}
	ast_channel_unlock(session->channel);

	return update_allowed;
}

/*! \brief Update connected line information */
static int update_connected_line_information(void *data)
{
	struct ast_sip_session *session = data;

	if (session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Session already DISCONNECTED [reason=%d (%s)]\n",
			session->inv_session->cause,
			pjsip_get_status_text(session->inv_session->cause)->ptr);
		ao2_ref(session, -1);
		return -1;
	}

	if (ast_channel_state(session->channel) == AST_STATE_UP
		|| session->inv_session->role == PJSIP_ROLE_UAC) {
		if (is_colp_update_allowed(session)) {
			enum ast_sip_session_refresh_method method;
			int generate_new_sdp;

			method = session->endpoint->id.refresh_method;
			if (session->inv_session->options & PJSIP_INV_SUPPORT_UPDATE) {
				method = AST_SIP_SESSION_REFRESH_METHOD_UPDATE;
			}

			/* Only the INVITE method actually needs SDP, UPDATE can do without */
			generate_new_sdp = (method == AST_SIP_SESSION_REFRESH_METHOD_INVITE);

			ast_sip_session_refresh(session, NULL, NULL, NULL, method, generate_new_sdp, NULL);
		}
	} else if (session->endpoint->id.rpid_immediate
		&& session->inv_session->state != PJSIP_INV_STATE_DISCONNECTED
		&& is_colp_update_allowed(session)) {
		int response_code = 0;

		if (ast_channel_state(session->channel) == AST_STATE_RING) {
			response_code = !session->endpoint->inband_progress ? 180 : 183;
		} else if (ast_channel_state(session->channel) == AST_STATE_RINGING) {
			response_code = 183;
		}

		if (response_code) {
			struct pjsip_tx_data *packet = NULL;

			if (pjsip_inv_answer(session->inv_session, response_code, NULL, NULL, &packet) == PJ_SUCCESS) {
				ast_sip_session_send_response(session, packet);
			}
		}
	}

	ao2_ref(session, -1);
	return 0;
}

/*! \brief Update local hold state and send a re-INVITE with the new SDP */
static int remote_send_hold_refresh(struct ast_sip_session *session, unsigned int held)
{
	struct ast_sip_session_media *session_media = session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];
	if (session_media) {
		session_media->locally_held = held;
	}
	ast_sip_session_refresh(session, NULL, NULL, NULL, AST_SIP_SESSION_REFRESH_METHOD_INVITE, 1, NULL);
	ao2_ref(session, -1);

	return 0;
}

/*! \brief Update local hold state to be held */
static int remote_send_hold(void *data)
{
	return remote_send_hold_refresh(data, 1);
}

/*! \brief Update local hold state to be unheld */
static int remote_send_unhold(void *data)
{
	return remote_send_hold_refresh(data, 0);
}

struct topology_change_refresh_data {
	struct ast_sip_session *session;
	struct ast_sip_session_media_state *media_state;
};

static void topology_change_refresh_data_free(struct topology_change_refresh_data *refresh_data)
{
	ao2_cleanup(refresh_data->session);

	ast_sip_session_media_state_free(refresh_data->media_state);
	ast_free(refresh_data);
}

static struct topology_change_refresh_data *topology_change_refresh_data_alloc(
	struct ast_sip_session *session, const struct ast_stream_topology *topology)
{
	struct topology_change_refresh_data *refresh_data;

	refresh_data = ast_calloc(1, sizeof(*refresh_data));
	if (!refresh_data) {
		return NULL;
	}

	refresh_data->session = ao2_bump(session);
	refresh_data->media_state = ast_sip_session_media_state_alloc();
	if (!refresh_data->media_state) {
		topology_change_refresh_data_free(refresh_data);
		return NULL;
	}
	refresh_data->media_state->topology = ast_stream_topology_clone(topology);
	if (!refresh_data->media_state->topology) {
		topology_change_refresh_data_free(refresh_data);
		return NULL;
	}

	return refresh_data;
}

static int on_topology_change_response(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	SCOPE_ENTER(3, "%s: Received response code %d.  PT: %s  AT: %s\n", ast_sip_session_get_name(session),
		rdata->msg_info.msg->line.status.code,
		ast_str_tmp(256, ast_stream_topology_to_str(session->pending_media_state->topology, &STR_TMP)),
		ast_str_tmp(256, ast_stream_topology_to_str(session->active_media_state->topology, &STR_TMP)));


	if (PJSIP_IS_STATUS_IN_CLASS(rdata->msg_info.msg->line.status.code, 200)) {
		/* The topology was changed to something new so give notice to what requested
		 * it so it queries the channel and updates accordingly.
		 */
		if (session->channel) {
			ast_queue_control(session->channel, AST_CONTROL_STREAM_TOPOLOGY_CHANGED);
			SCOPE_EXIT_RTN_VALUE(0, "%s: Queued topology change frame\n", ast_sip_session_get_name(session));
		}
		SCOPE_EXIT_RTN_VALUE(0, "%s: No channel?  Can't queue topology change frame\n", ast_sip_session_get_name(session));
	} else if (300 <= rdata->msg_info.msg->line.status.code) {
		/* The topology change failed, so drop the current pending media state */
		ast_sip_session_media_state_reset(session->pending_media_state);
		SCOPE_EXIT_RTN_VALUE(0, "%s: response code > 300.  Resetting pending media state\n", ast_sip_session_get_name(session));
	}

	SCOPE_EXIT_RTN_VALUE(0, "%s: Nothing to do\n", ast_sip_session_get_name(session));
}

static int send_topology_change_refresh(void *data)
{
	struct topology_change_refresh_data *refresh_data = data;
	struct ast_sip_session *session = refresh_data->session;
	int ret;
	SCOPE_ENTER(3, "%s: %s\n", ast_sip_session_get_name(session),
		ast_str_tmp(256, ast_stream_topology_to_str(refresh_data->media_state->topology, &STR_TMP)));


	ret = ast_sip_session_refresh(session, NULL, NULL, on_topology_change_response,
		AST_SIP_SESSION_REFRESH_METHOD_INVITE, 1, refresh_data->media_state);
	refresh_data->media_state = NULL;
	topology_change_refresh_data_free(refresh_data);

	SCOPE_EXIT_RTN_VALUE(ret, "%s\n", ast_sip_session_get_name(session));
}

static int handle_topology_request_change(struct ast_sip_session *session,
	const struct ast_stream_topology *proposed)
{
	struct topology_change_refresh_data *refresh_data;
	int res;
	SCOPE_ENTER(1);

	refresh_data = topology_change_refresh_data_alloc(session, proposed);
	if (!refresh_data) {
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create refresh_data\n");
	}

	res = ast_sip_push_task(session->serializer, send_topology_change_refresh, refresh_data);
	if (res) {
		topology_change_refresh_data_free(refresh_data);
	}
	SCOPE_EXIT_RTN_VALUE(res, "RC: %d\n", res);
}

/* Forward declarations */
static int transmit_info_dtmf(void *data);
static struct info_dtmf_data *info_dtmf_data_alloc(struct ast_sip_session *session, char digit, unsigned int duration);

/*! \brief Function called by core to ask the channel to indicate some sort of condition */
static int chan_pjsip_indicate(struct ast_channel *ast, int condition, const void *data, size_t datalen)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	struct ast_sip_session_media *media;
	int response_code = 0;
	int res = 0;
	char *device_buf;
	size_t device_buf_size;
	int i;
	const struct ast_stream_topology *topology;
	struct ast_frame f = {
		.frametype = AST_FRAME_CONTROL,
		.subclass = {
			.integer = condition
		},
		.datalen = datalen,
		.data.ptr = (void *)data,
	};
	char condition_name[256];
	unsigned int duration;
	char digit;
	struct info_dtmf_data *dtmf_data;

	SCOPE_ENTER(3, "%s: Indicated %s\n", ast_channel_name(ast),
		ast_frame_subclass2str(&f, condition_name, sizeof(condition_name), NULL, 0));

	switch (condition) {
	case AST_CONTROL_RINGING:
		if (ast_channel_state(ast) == AST_STATE_RING) {
			if (channel->session->endpoint->inband_progress ||
				(channel->session->inv_session && channel->session->inv_session->neg &&
				pjmedia_sdp_neg_get_state(channel->session->inv_session->neg) == PJMEDIA_SDP_NEG_STATE_DONE)) {
				res = -1;
				if (ast_sip_get_allow_sending_180_after_183()) {
					response_code = 180;
				} else {
					response_code = 183;
				}
			} else {
				response_code = 180;
			}
		} else {
			res = -1;
		}
		ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "PJSIP/%s", ast_sorcery_object_get_id(channel->session->endpoint));
		break;
	case AST_CONTROL_BUSY:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 486;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_CONGESTION:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 503;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_INCOMPLETE:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 484;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_PROCEEDING:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 100;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_PROGRESS:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 183;
		} else {
			res = -1;
		}
		ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE, "PJSIP/%s", ast_sorcery_object_get_id(channel->session->endpoint));
		break;
	case AST_CONTROL_FLASH:
		duration = 300;
		digit = '!';
		dtmf_data = info_dtmf_data_alloc(channel->session, digit, duration);

		if (!dtmf_data) {
			res = -1;
			break;
		}

		if (ast_sip_push_task(channel->session->serializer, transmit_info_dtmf, dtmf_data)) {
			ast_log(LOG_WARNING, "Error sending FLASH via INFO on channel %s\n", ast_channel_name(ast));
			ao2_ref(dtmf_data, -1); /* dtmf_data can't be null here */
			res = -1;
		}
		break;
	case AST_CONTROL_VIDUPDATE:
		for (i = 0; i < AST_VECTOR_SIZE(&channel->session->active_media_state->sessions); ++i) {
			media = AST_VECTOR_GET(&channel->session->active_media_state->sessions, i);
			if (!media || media->type != AST_MEDIA_TYPE_VIDEO) {
				continue;
			}
			if (media->rtp) {
				/* FIXME: Only use this for VP8. Additional work would have to be done to
				 * fully support other video codecs */

				if (ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), ast_format_vp8) != AST_FORMAT_CMP_NOT_EQUAL ||
					ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), ast_format_vp9) != AST_FORMAT_CMP_NOT_EQUAL ||
					ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), ast_format_h265) != AST_FORMAT_CMP_NOT_EQUAL ||
					(channel->session->endpoint->media.webrtc &&
					 ast_format_cap_iscompatible_format(ast_channel_nativeformats(ast), ast_format_h264) != AST_FORMAT_CMP_NOT_EQUAL)) {
					/* FIXME Fake RTP write, this will be sent as an RTCP packet. Ideally the
					 * RTP engine would provide a way to externally write/schedule RTCP
					 * packets */
					struct ast_frame fr;
					fr.frametype = AST_FRAME_CONTROL;
					fr.subclass.integer = AST_CONTROL_VIDUPDATE;
					res = ast_rtp_instance_write(media->rtp, &fr);
				} else {
					ao2_ref(channel->session, +1);
					if (ast_sip_push_task(channel->session->serializer, transmit_info_with_vidupdate, channel->session)) {
						ao2_cleanup(channel->session);
					}
				}
				ast_test_suite_event_notify("AST_CONTROL_VIDUPDATE", "Result: Success");
			} else {
				ast_test_suite_event_notify("AST_CONTROL_VIDUPDATE", "Result: Failure");
				res = -1;
			}
		}
		/* XXX If there were no video streams, then this should set
		 * res to -1
		 */
		break;
	case AST_CONTROL_CONNECTED_LINE:
		ao2_ref(channel->session, +1);
		if (ast_sip_push_task(channel->session->serializer, update_connected_line_information, channel->session)) {
			ao2_cleanup(channel->session);
		}
		break;
	case AST_CONTROL_UPDATE_RTP_PEER:
		break;
	case AST_CONTROL_PVT_CAUSE_CODE:
		res = -1;
		break;
	case AST_CONTROL_MASQUERADE_NOTIFY:
		ast_assert(datalen == sizeof(int));
		if (*(int *) data) {
			/*
			 * Masquerade is beginning:
			 * Wait for session serializer to get suspended.
			 */
			ast_channel_unlock(ast);
			ast_sip_session_suspend(channel->session);
			ast_channel_lock(ast);
		} else {
			/*
			 * Masquerade is complete:
			 * Unsuspend the session serializer.
			 */
			ast_sip_session_unsuspend(channel->session);
		}
		break;
	case AST_CONTROL_HOLD:
		chan_pjsip_add_hold(ast_channel_uniqueid(ast));
		device_buf_size = strlen(ast_channel_name(ast)) + 1;
		device_buf = alloca(device_buf_size);
		ast_channel_get_device_name(ast, device_buf, device_buf_size);
		ast_devstate_changed_literal(AST_DEVICE_ONHOLD, 1, device_buf);
		if (!channel->session->moh_passthrough) {
			ast_moh_start(ast, data, NULL);
		} else {
			if (ast_sip_push_task(channel->session->serializer, remote_send_hold, ao2_bump(channel->session))) {
				ast_log(LOG_WARNING, "Could not queue task to remotely put session '%s' on hold with endpoint '%s'\n",
					ast_sorcery_object_get_id(channel->session), ast_sorcery_object_get_id(channel->session->endpoint));
				ao2_ref(channel->session, -1);
			}
		}
		break;
	case AST_CONTROL_UNHOLD:
		chan_pjsip_remove_hold(ast_channel_uniqueid(ast));
		device_buf_size = strlen(ast_channel_name(ast)) + 1;
		device_buf = alloca(device_buf_size);
		ast_channel_get_device_name(ast, device_buf, device_buf_size);
		ast_devstate_changed_literal(AST_DEVICE_UNKNOWN, 1, device_buf);
		if (!channel->session->moh_passthrough) {
			ast_moh_stop(ast);
		} else {
			if (ast_sip_push_task(channel->session->serializer, remote_send_unhold, ao2_bump(channel->session))) {
				ast_log(LOG_WARNING, "Could not queue task to remotely take session '%s' off hold with endpoint '%s'\n",
					ast_sorcery_object_get_id(channel->session), ast_sorcery_object_get_id(channel->session->endpoint));
				ao2_ref(channel->session, -1);
			}
		}
		break;
	case AST_CONTROL_SRCUPDATE:
		break;
	case AST_CONTROL_SRCCHANGE:
		break;
	case AST_CONTROL_REDIRECTING:
		if (ast_channel_state(ast) != AST_STATE_UP) {
			response_code = 181;
		} else {
			res = -1;
		}
		break;
	case AST_CONTROL_T38_PARAMETERS:
		res = 0;

		if (channel->session->t38state == T38_PEER_REINVITE) {
			const struct ast_control_t38_parameters *parameters = data;

			if (parameters->request_response == AST_T38_REQUEST_PARMS) {
				res = AST_T38_REQUEST_PARMS;
			}
		}

		break;
	case AST_CONTROL_STREAM_TOPOLOGY_REQUEST_CHANGE:
		topology = data;
		ast_trace(-1, "%s: New topology: %s\n", ast_channel_name(ast),
			ast_str_tmp(256, ast_stream_topology_to_str(topology, &STR_TMP)));
		res = handle_topology_request_change(channel->session, topology);
		break;
	case AST_CONTROL_STREAM_TOPOLOGY_CHANGED:
		break;
	case AST_CONTROL_STREAM_TOPOLOGY_SOURCE_CHANGED:
		break;
	case -1:
		res = -1;
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to indicate condition %d\n", condition);
		res = -1;
		break;
	}

	if (response_code) {
		struct indicate_data *ind_data = indicate_data_alloc(channel->session, condition, response_code, data, datalen);

		if (!ind_data) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Couldn't alloc indicate data\n", ast_channel_name(ast));
		}

		if (ast_sip_push_task(channel->session->serializer, indicate, ind_data)) {
			ast_log(LOG_ERROR, "%s: Cannot send response code %d to endpoint %s. Could not queue task properly\n",
					ast_channel_name(ast), response_code, ast_sorcery_object_get_id(channel->session->endpoint));
			ao2_cleanup(ind_data);
			res = -1;
		}
	}

	SCOPE_EXIT_RTN_VALUE(res, "%s\n", ast_channel_name(ast));
}

struct transfer_data {
	struct ast_sip_session *session;
	char *target;
};

static void transfer_data_destroy(void *obj)
{
	struct transfer_data *trnf_data = obj;

	ast_free(trnf_data->target);
	ao2_cleanup(trnf_data->session);
}

static struct transfer_data *transfer_data_alloc(struct ast_sip_session *session, const char *target)
{
	struct transfer_data *trnf_data = ao2_alloc(sizeof(*trnf_data), transfer_data_destroy);

	if (!trnf_data) {
		return NULL;
	}

	if (!(trnf_data->target = ast_strdup(target))) {
		ao2_ref(trnf_data, -1);
		return NULL;
	}

	ao2_ref(session, +1);
	trnf_data->session = session;

	return trnf_data;
}

static void transfer_redirect(struct ast_sip_session *session, const char *target)
{
	pjsip_tx_data *packet;
	enum ast_control_transfer message = AST_TRANSFER_SUCCESS;
	pjsip_contact_hdr *contact;
	pj_str_t tmp;

	if (pjsip_inv_end_session(session->inv_session, 302, NULL, &packet) != PJ_SUCCESS
		|| !packet) {
		ast_log(LOG_WARNING, "Failed to redirect PJSIP session for channel %s\n",
			ast_channel_name(session->channel));
		message = AST_TRANSFER_FAILED;
		ast_queue_control_data(session->channel, AST_CONTROL_TRANSFER, &message, sizeof(message));

		return;
	}

	if (!(contact = pjsip_msg_find_hdr(packet->msg, PJSIP_H_CONTACT, NULL))) {
		contact = pjsip_contact_hdr_create(packet->pool);
	}

	pj_strdup2_with_null(packet->pool, &tmp, target);
	if (!(contact->uri = pjsip_parse_uri(packet->pool, tmp.ptr, tmp.slen, PJSIP_PARSE_URI_AS_NAMEADDR))) {
		ast_log(LOG_WARNING, "Failed to parse destination URI '%s' for channel %s\n",
			target, ast_channel_name(session->channel));
		message = AST_TRANSFER_FAILED;
		ast_queue_control_data(session->channel, AST_CONTROL_TRANSFER, &message, sizeof(message));
		pjsip_tx_data_dec_ref(packet);

		return;
	}
	pjsip_msg_add_hdr(packet->msg, (pjsip_hdr *) contact);

	ast_sip_session_send_response(session, packet);
	ast_queue_control_data(session->channel, AST_CONTROL_TRANSFER, &message, sizeof(message));
}

/*! \brief REFER Callback module, used to attach session data structure to subscription */
static pjsip_module refer_callback_module = {
	.name = { "REFER Callback", 14 },
	.id = -1,
};

/*!
 * \brief Callback function to report status of implicit REFER-NOTIFY subscription.
 *
 * This function will be called on any state change in the REFER-NOTIFY subscription.
 * Its primary purpose is to report SUCCESS/FAILURE of a transfer initiated via
 * \ref transfer_refer as well as to terminate the subscription, if necessary.
 */
static void xfer_client_on_evsub_state(pjsip_evsub *sub, pjsip_event *event)
{
	struct ast_channel *chan;
	enum ast_control_transfer message = AST_TRANSFER_SUCCESS;
	int res = 0;

	if (!event) {
		return;
	}

	chan = pjsip_evsub_get_mod_data(sub, refer_callback_module.id);
	if (!chan) {
		return;
	}

	if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_ACCEPTED) {
		/* Check if subscription is suppressed and terminate and send completion code, if so. */
		pjsip_rx_data *rdata;
		pjsip_generic_string_hdr *refer_sub;
		const pj_str_t REFER_SUB = { "Refer-Sub", 9 };

		ast_debug(3, "Transfer accepted on channel %s\n", ast_channel_name(chan));

		/* Check if response message */
		if (event->type == PJSIP_EVENT_TSX_STATE && event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
			rdata = event->body.tsx_state.src.rdata;

			/* Find Refer-Sub header */
			refer_sub = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &REFER_SUB, NULL);

			/* Check if subscription is suppressed. If it is, the far end will not terminate it,
			 * and the subscription will remain active until it times out.  Terminating it here
			 * eliminates the unnecessary timeout.
			 */
			if (refer_sub && !pj_stricmp2(&refer_sub->hvalue, "false")) {
				/* Since no subscription is desired, assume that call has been transferred successfully. */
				/* Channel reference will be released at end of function */
				/* Terminate subscription. */
				pjsip_evsub_set_mod_data(sub, refer_callback_module.id, NULL);
				pjsip_evsub_terminate(sub, PJ_TRUE);
				res = -1;
			}
		}
	} else if (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_ACTIVE ||
			pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED) {
		/* Check for NOTIFY complete or error. */
		pjsip_msg *msg;
		pjsip_msg_body *body;
		pjsip_status_line status_line = { .code = 0 };
		pj_bool_t is_last;
		pj_status_t status;

		if (event->type == PJSIP_EVENT_TSX_STATE && event->body.tsx_state.type == PJSIP_EVENT_RX_MSG) {
			pjsip_rx_data *rdata;

			rdata = event->body.tsx_state.src.rdata;
			msg = rdata->msg_info.msg;

			if (msg->type == PJSIP_REQUEST_MSG) {
				if (!pjsip_method_cmp(&msg->line.req.method, pjsip_get_notify_method())) {
					body = msg->body;
					if (body && !pj_stricmp2(&body->content_type.type, "message")
						&& !pj_stricmp2(&body->content_type.subtype, "sipfrag")) {
						pjsip_parse_status_line((char *)body->data, body->len, &status_line);
					}
				}
			} else {
				status_line.code = msg->line.status.code;
				status_line.reason = msg->line.status.reason;
			}
		} else {
			status_line.code = 500;
			status_line.reason = *pjsip_get_status_text(500);
		}

		is_last = (pjsip_evsub_get_state(sub) == PJSIP_EVSUB_STATE_TERMINATED);
		/* If the status code is >= 200, the subscription is finished. */
		if (status_line.code >= 200 || is_last) {
			res = -1;

			/* If the subscription has terminated, return AST_TRANSFER_SUCCESS for 2XX.
			 * Return AST_TRANSFER_FAILED for any code < 200.
			 * Otherwise, return the status code.
			 * The subscription should not terminate for any code < 200,
			 * but if it does, that constitutes a failure. */
			if (status_line.code < 200) {
				message = AST_TRANSFER_FAILED;
			} else if (status_line.code >= 300) {
				message = status_line.code;
			}

			/* If subscription not terminated and subscription is finished (status code >= 200)
			 * terminate it */
			if (!is_last) {
				pjsip_tx_data *tdata;

				status = pjsip_evsub_initiate(sub, pjsip_get_subscribe_method(), 0, &tdata);
				if (status == PJ_SUCCESS) {
					pjsip_evsub_send_request(sub, tdata);
				}
			}
			/* Finished. Remove session from subscription */
			pjsip_evsub_set_mod_data(sub, refer_callback_module.id, NULL);
			ast_debug(3, "Transfer channel %s completed: %d %.*s (%s)\n",
					ast_channel_name(chan),
					status_line.code,
					(int)status_line.reason.slen, status_line.reason.ptr,
					(message == AST_TRANSFER_SUCCESS) ? "Success" : "Failure");
		}
	}

	if (res) {
		ast_queue_control_data(chan, AST_CONTROL_TRANSFER, &message, sizeof(message));
		ao2_ref(chan, -1);
	}
}

static void transfer_refer(struct ast_sip_session *session, const char *target)
{
	pjsip_evsub *sub;
	enum ast_control_transfer message = AST_TRANSFER_SUCCESS;
	pj_str_t tmp;
	pjsip_tx_data *packet;
	const char *ref_by_val;
	char local_info[pj_strlen(&session->inv_session->dlg->local.info_str) + 1];
	struct pjsip_evsub_user xfer_cb;
	struct ast_channel *chan = session->channel;

	pj_bzero(&xfer_cb, sizeof(xfer_cb));
	xfer_cb.on_evsub_state = &xfer_client_on_evsub_state;

	if (pjsip_xfer_create_uac(session->inv_session->dlg, &xfer_cb, &sub) != PJ_SUCCESS) {
		message = AST_TRANSFER_FAILED;
		ast_queue_control_data(chan, AST_CONTROL_TRANSFER, &message, sizeof(message));

		return;
	}

	/* refer_callback_module requires a reference to chan
	 * which will be released in xfer_client_on_evsub_state()
	 * when the implicit REFER subscription terminates */
	pjsip_evsub_set_mod_data(sub, refer_callback_module.id, chan);
	ao2_ref(chan, +1);

	if (pjsip_xfer_initiate(sub, pj_cstr(&tmp, target), &packet) != PJ_SUCCESS) {
		goto failure;
	}

	ref_by_val = pbx_builtin_getvar_helper(chan, "SIPREFERREDBYHDR");
	if (!ast_strlen_zero(ref_by_val)) {
		ast_sip_add_header(packet, "Referred-By", ref_by_val);
	} else {
		ast_copy_pj_str(local_info, &session->inv_session->dlg->local.info_str, sizeof(local_info));
		ast_sip_add_header(packet, "Referred-By", local_info);
	}

	if (pjsip_xfer_send_request(sub, packet) == PJ_SUCCESS) {
		return;
	}

failure:
	message = AST_TRANSFER_FAILED;
	ast_queue_control_data(chan, AST_CONTROL_TRANSFER, &message, sizeof(message));
	pjsip_evsub_set_mod_data(sub, refer_callback_module.id, NULL);
	pjsip_evsub_terminate(sub, PJ_FALSE);

	ao2_ref(chan, -1);
}

static int transfer(void *data)
{
	struct transfer_data *trnf_data = data;
	struct ast_sip_endpoint *endpoint = NULL;
	struct ast_sip_contact *contact = NULL;
	const char *target = trnf_data->target;

	if (trnf_data->session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Session already DISCONNECTED [reason=%d (%s)]\n",
			trnf_data->session->inv_session->cause,
			pjsip_get_status_text(trnf_data->session->inv_session->cause)->ptr);
	} else {
		/* See if we have an endpoint; if so, use its contact */
		endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", target);
		if (endpoint) {
			contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
			if (contact && !ast_strlen_zero(contact->uri)) {
				target = contact->uri;
			}
		}

		if (ast_channel_state(trnf_data->session->channel) == AST_STATE_RING) {
			transfer_redirect(trnf_data->session, target);
		} else {
			transfer_refer(trnf_data->session, target);
		}
	}

	ao2_ref(trnf_data, -1);
	ao2_cleanup(endpoint);
	ao2_cleanup(contact);
	return 0;
}

/*! \brief Function called by core for Asterisk initiated transfer */
static int chan_pjsip_transfer(struct ast_channel *chan, const char *target)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	struct transfer_data *trnf_data = transfer_data_alloc(channel->session, target);

	if (!trnf_data) {
		return -1;
	}

	if (ast_sip_push_task(channel->session->serializer, transfer, trnf_data)) {
		ast_log(LOG_WARNING, "Error requesting transfer\n");
		ao2_cleanup(trnf_data);
		return -1;
	}

	return 0;
}

/*! \brief Function called by core to start a DTMF digit */
static int chan_pjsip_digit_begin(struct ast_channel *chan, char digit)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	struct ast_sip_session_media *media;

	media = channel->session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];

	switch (channel->session->dtmf) {
	case AST_SIP_DTMF_RFC_4733:
		if (!media || !media->rtp) {
			return 0;
		}

		ast_rtp_instance_dtmf_begin(media->rtp, digit);
		break;
	case AST_SIP_DTMF_AUTO:
		if (!media || !media->rtp) {
			return 0;
		}

		if (ast_rtp_instance_dtmf_mode_get(media->rtp) == AST_RTP_DTMF_MODE_INBAND) {
			return -1;
		}

		ast_rtp_instance_dtmf_begin(media->rtp, digit);
		break;
	case AST_SIP_DTMF_AUTO_INFO:
		if (!media || !media->rtp || (ast_rtp_instance_dtmf_mode_get(media->rtp) == AST_RTP_DTMF_MODE_NONE)) {
			return 0;
		}
		ast_rtp_instance_dtmf_begin(media->rtp, digit);
		break;
	case AST_SIP_DTMF_NONE:
		break;
	case AST_SIP_DTMF_INBAND:
		return -1;
	default:
		break;
	}

	return 0;
}

struct info_dtmf_data {
	struct ast_sip_session *session;
	char digit;
	unsigned int duration;
};

static void info_dtmf_data_destroy(void *obj)
{
	struct info_dtmf_data *dtmf_data = obj;
	ao2_ref(dtmf_data->session, -1);
}

static struct info_dtmf_data *info_dtmf_data_alloc(struct ast_sip_session *session, char digit, unsigned int duration)
{
	struct info_dtmf_data *dtmf_data = ao2_alloc(sizeof(*dtmf_data), info_dtmf_data_destroy);
	if (!dtmf_data) {
		return NULL;
	}
	ao2_ref(session, +1);
	dtmf_data->session = session;
	dtmf_data->digit = digit;
	dtmf_data->duration = duration;
	return dtmf_data;
}

static int transmit_info_dtmf(void *data)
{
	RAII_VAR(struct info_dtmf_data *, dtmf_data, data, ao2_cleanup);

	struct ast_sip_session *session = dtmf_data->session;
	struct pjsip_tx_data *tdata;

	RAII_VAR(struct ast_str *, body_text, NULL, ast_free_ptr);

	struct ast_sip_body body = {
		.type = "application",
		.subtype = "dtmf-relay",
	};

	if (session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Session already DISCONNECTED [reason=%d (%s)]\n",
			session->inv_session->cause,
			pjsip_get_status_text(session->inv_session->cause)->ptr);
		return -1;
	}

	if (!(body_text = ast_str_create(32))) {
		ast_log(LOG_ERROR, "Could not allocate buffer for INFO DTMF.\n");
		return -1;
	}
	ast_str_set(&body_text, 0, "Signal=%c\r\nDuration=%u\r\n", dtmf_data->digit, dtmf_data->duration);

	body.body_text = ast_str_buffer(body_text);

	if (ast_sip_create_request("INFO", session->inv_session->dlg, session->endpoint, NULL, NULL, &tdata)) {
		ast_log(LOG_ERROR, "Could not create DTMF INFO request\n");
		return -1;
	}
	if (ast_sip_add_body(tdata, &body)) {
		ast_log(LOG_ERROR, "Could not add body to DTMF INFO request\n");
		pjsip_tx_data_dec_ref(tdata);
		return -1;
	}
	ast_sip_session_send_request(session, tdata);

	return 0;
}

/*! \brief Function called by core to stop a DTMF digit */
static int chan_pjsip_digit_end(struct ast_channel *ast, char digit, unsigned int duration)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	struct ast_sip_session_media *media;

	if (!channel || !channel->session) {
		/* This happens when the channel is hungup while a DTMF digit is playing. See ASTERISK-28086 */
		ast_debug(3, "Channel %s disappeared while calling digit_end\n", ast_channel_name(ast));
		return -1;
	}

	media = channel->session->active_media_state->default_session[AST_MEDIA_TYPE_AUDIO];

	switch (channel->session->dtmf) {
	case AST_SIP_DTMF_AUTO_INFO:
	{
		if (!media || !media->rtp) {
			return 0;
		}

		if (ast_rtp_instance_dtmf_mode_get(media->rtp) != AST_RTP_DTMF_MODE_NONE) {
			ast_debug(3, "Told to send end of digit on Auto-Info channel %s RFC4733 negotiated so using it.\n", ast_channel_name(ast));
			ast_rtp_instance_dtmf_end_with_duration(media->rtp, digit, duration);
			break;
		}
		/* If RFC_4733 was not negotiated, fail through to the DTMF_INFO processing */
		ast_debug(3, "Told to send end of digit on Auto-Info channel %s RFC4733 NOT negotiated using INFO instead.\n", ast_channel_name(ast));
	}

	case AST_SIP_DTMF_INFO:
	{
		struct info_dtmf_data *dtmf_data = info_dtmf_data_alloc(channel->session, digit, duration);

		if (!dtmf_data) {
			return -1;
		}

		if (ast_sip_push_task(channel->session->serializer, transmit_info_dtmf, dtmf_data)) {
			ast_log(LOG_WARNING, "Error sending DTMF via INFO.\n");
			ao2_cleanup(dtmf_data);
			return -1;
		}
		break;
	}
	case AST_SIP_DTMF_RFC_4733:
		if (!media || !media->rtp) {
			return 0;
		}

		ast_rtp_instance_dtmf_end_with_duration(media->rtp, digit, duration);
		break;
	case AST_SIP_DTMF_AUTO:
		if (!media || !media->rtp) {
			return 0;
		}

		if (ast_rtp_instance_dtmf_mode_get(media->rtp) == AST_RTP_DTMF_MODE_INBAND) {
			 return -1;
		}

		ast_rtp_instance_dtmf_end_with_duration(media->rtp, digit, duration);
		break;
	case AST_SIP_DTMF_NONE:
		break;
	case AST_SIP_DTMF_INBAND:
		return -1;
	}

	return 0;
}

static void update_initial_connected_line(struct ast_sip_session *session)
{
	struct ast_party_connected_line connected;

	/*
	 * Use the channel CALLERID() as the initial connected line data.
	 * The core or a predial handler may have supplied missing values
	 * from the session->endpoint->id.self about who we are calling.
	 */
	ast_channel_lock(session->channel);
	ast_party_id_copy(&session->id, &ast_channel_caller(session->channel)->id);
	ast_channel_unlock(session->channel);

	/* Supply initial connected line information if available. */
	if (!session->id.number.valid && !session->id.name.valid) {
		return;
	}

	ast_party_connected_line_init(&connected);
	connected.id = session->id;
	connected.source = AST_CONNECTED_LINE_UPDATE_SOURCE_ANSWER;

	ast_channel_queue_connected_line_update(session->channel, &connected, NULL);
}

static int call(void *data)
{
	struct ast_sip_channel_pvt *channel = data;
	struct ast_sip_session *session = channel->session;
	pjsip_tx_data *tdata;
	int res = 0;
	SCOPE_ENTER(1, "%s Topology: %s\n",
		ast_sip_session_get_name(session),
		ast_str_tmp(256, ast_stream_topology_to_str(channel->session->pending_media_state->topology, &STR_TMP))
		);


	res = ast_sip_session_create_invite(session, &tdata);

	if (res) {
		ast_set_hangupsource(session->channel, ast_channel_name(session->channel), 0);
		ast_queue_hangup(session->channel);
	} else {
		set_channel_on_rtp_instance(session, ast_channel_uniqueid(session->channel));
		update_initial_connected_line(session);
		ast_sip_session_send_request(session, tdata);
	}
	ao2_ref(channel, -1);
	SCOPE_EXIT_RTN_VALUE(res, "RC: %d\n", res);
}

/*! \brief Function called by core to actually start calling a remote party */
static int chan_pjsip_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	SCOPE_ENTER(1, "%s Topology: %s\n", ast_sip_session_get_name(channel->session),
		ast_str_tmp(256, ast_stream_topology_to_str(channel->session->pending_media_state->topology, &STR_TMP)));

	ao2_ref(channel, +1);
	if (ast_sip_push_task(channel->session->serializer, call, channel)) {
		ast_log(LOG_WARNING, "Error attempting to place outbound call to '%s'\n", dest);
		ao2_cleanup(channel);
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't push task\n");
	}

	SCOPE_EXIT_RTN_VALUE(0, "'call' task pushed\n");
}

/*! \brief Internal function which translates from Asterisk cause codes to SIP response codes */
static int hangup_cause2sip(int cause)
{
	switch (cause) {
	case AST_CAUSE_UNALLOCATED:             /* 1 */
	case AST_CAUSE_NO_ROUTE_DESTINATION:    /* 3 IAX2: Can't find extension in context */
	case AST_CAUSE_NO_ROUTE_TRANSIT_NET:    /* 2 */
		return 404;
	case AST_CAUSE_CONGESTION:              /* 34 */
	case AST_CAUSE_SWITCH_CONGESTION:       /* 42 */
		return 503;
	case AST_CAUSE_NO_USER_RESPONSE:        /* 18 */
		return 408;
	case AST_CAUSE_NO_ANSWER:               /* 19 */
	case AST_CAUSE_UNREGISTERED:        /* 20 */
		return 480;
	case AST_CAUSE_CALL_REJECTED:           /* 21 */
		return 403;
	case AST_CAUSE_NUMBER_CHANGED:          /* 22 */
		return 410;
	case AST_CAUSE_NORMAL_UNSPECIFIED:      /* 31 */
		return 480;
	case AST_CAUSE_INVALID_NUMBER_FORMAT:
		return 484;
	case AST_CAUSE_USER_BUSY:
		return 486;
	case AST_CAUSE_FAILURE:
		return 500;
	case AST_CAUSE_FACILITY_REJECTED:       /* 29 */
		return 501;
	case AST_CAUSE_CHAN_NOT_IMPLEMENTED:
		return 503;
	case AST_CAUSE_DESTINATION_OUT_OF_ORDER:
		return 502;
	case AST_CAUSE_BEARERCAPABILITY_NOTAVAIL:       /* Can't find codec to connect to host */
		return 488;
	case AST_CAUSE_INTERWORKING:    /* Unspecified Interworking issues */
		return 500;
	case AST_CAUSE_NOTDEFINED:
	default:
		ast_debug(1, "AST hangup cause %d (no match found in PJSIP)\n", cause);
		return 0;
	}

	/* Never reached */
	return 0;
}

struct hangup_data {
	int cause;
	struct ast_channel *chan;
};

static void hangup_data_destroy(void *obj)
{
	struct hangup_data *h_data = obj;

	h_data->chan = ast_channel_unref(h_data->chan);
}

static struct hangup_data *hangup_data_alloc(int cause, struct ast_channel *chan)
{
	struct hangup_data *h_data = ao2_alloc(sizeof(*h_data), hangup_data_destroy);

	if (!h_data) {
		return NULL;
	}

	h_data->cause = cause;
	h_data->chan = ast_channel_ref(chan);

	return h_data;
}

/*! \brief Clear a channel from a session along with its PVT */
static void clear_session_and_channel(struct ast_sip_session *session, struct ast_channel *ast)
{
	session->channel = NULL;
	set_channel_on_rtp_instance(session, "");
	ast_channel_tech_pvt_set(ast, NULL);
}

static int hangup(void *data)
{
	struct hangup_data *h_data = data;
	struct ast_channel *ast = h_data->chan;
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	SCOPE_ENTER(1, "%s\n", ast_channel_name(ast));

	/*
	 * Before cleaning we have to ensure that channel or its session is not NULL
	 * we have seen rare case when taskprocessor calls hangup but channel is NULL
	 * due to SIP session timeout and answer happening at the same time
	 */
	if (channel) {
		struct ast_sip_session *session = channel->session;
		if (session) {
			int cause = h_data->cause;

			/*
	 		* It's possible that session_terminate might cause the session to be destroyed
	 		* immediately so we need to keep a reference to it so we can NULL session->channel
	 		* afterwards.
	 		*/
			ast_sip_session_terminate(ao2_bump(session), cause);
			clear_session_and_channel(session, ast);
			ao2_cleanup(session);
		}
		ao2_cleanup(channel);
	}
	ao2_cleanup(h_data);
	SCOPE_EXIT_RTN_VALUE(0);
}

/*! \brief Function called by core to hang up a PJSIP session */
static int chan_pjsip_hangup(struct ast_channel *ast)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	int cause;
	struct hangup_data *h_data;
	SCOPE_ENTER(1, "%s\n", ast_channel_name(ast));

	if (!channel || !channel->session) {
		SCOPE_EXIT_RTN_VALUE(-1, "No channel or session\n");
	}

	cause = hangup_cause2sip(ast_channel_hangupcause(channel->session->channel));
	h_data = hangup_data_alloc(cause, ast);

	if (!h_data) {
		goto failure;
	}

	if (ast_sip_push_task(channel->session->serializer, hangup, h_data)) {
		ast_log(LOG_WARNING, "Unable to push hangup task to the threadpool. Expect bad things\n");
		goto failure;
	}

	SCOPE_EXIT_RTN_VALUE(0, "Cause: %d\n", cause);

failure:
	/* Go ahead and do our cleanup of the session and channel even if we're not going
	 * to be able to send our SIP request/response
	 */
	clear_session_and_channel(channel->session, ast);
	ao2_cleanup(channel);
	ao2_cleanup(h_data);

	SCOPE_EXIT_RTN_VALUE(-1, "Cause: %d\n", cause);
}

struct request_data {
	struct ast_sip_session *session;
	struct ast_stream_topology *topology;
	const char *dest;
	int cause;
};

static int request(void *obj)
{
	struct request_data *req_data = obj;
	struct ast_sip_session *session = NULL;
	char *tmp = ast_strdupa(req_data->dest), *endpoint_name = NULL, *request_user = NULL;
	struct ast_sip_endpoint *endpoint;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(endpoint);
		AST_APP_ARG(aor);
	);
	SCOPE_ENTER(1, "%s\n",tmp);

	if (ast_strlen_zero(tmp)) {
		ast_log(LOG_ERROR, "Unable to create PJSIP channel with empty destination\n");
		req_data->cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
		SCOPE_EXIT_RTN_VALUE(-1, "Empty destination\n");
	}

	AST_NONSTANDARD_APP_ARGS(args, tmp, '/');

	if (ast_sip_get_disable_multi_domain()) {
		/* If a request user has been specified extract it from the endpoint name portion */
		if ((endpoint_name = strchr(args.endpoint, '@'))) {
			request_user = args.endpoint;
			*endpoint_name++ = '\0';
		} else {
			endpoint_name = args.endpoint;
		}

		if (ast_strlen_zero(endpoint_name)) {
			if (request_user) {
				ast_log(LOG_ERROR, "Unable to create PJSIP channel with empty endpoint name: %s@<endpoint-name>\n",
					request_user);
			} else {
				ast_log(LOG_ERROR, "Unable to create PJSIP channel with empty endpoint name\n");
			}
			req_data->cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
			SCOPE_EXIT_RTN_VALUE(-1, "Empty endpoint name\n");
		}
		endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
			endpoint_name);
		if (!endpoint) {
			ast_log(LOG_ERROR, "Unable to create PJSIP channel - endpoint '%s' was not found\n", endpoint_name);
			req_data->cause = AST_CAUSE_NO_ROUTE_DESTINATION;
			SCOPE_EXIT_RTN_VALUE(-1, "Endpoint not found\n");
		}
	} else {
		/* First try to find an exact endpoint match, for single (user) or multi-domain (user@domain) */
		endpoint_name = args.endpoint;
		if (ast_strlen_zero(endpoint_name)) {
			ast_log(LOG_ERROR, "Unable to create PJSIP channel with empty endpoint name\n");
			req_data->cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
			SCOPE_EXIT_RTN_VALUE(-1, "Empty endpoint name\n");
		}
		endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
			endpoint_name);
		if (!endpoint) {
			/* It seems it's not a multi-domain endpoint or single endpoint exact match,
			 * it's possible that it's a SIP trunk with a specified user (user@trunkname),
			 * so extract the user before @ sign.
			 */
			endpoint_name = strchr(args.endpoint, '@');
			if (!endpoint_name) {
				/*
				 * Couldn't find an '@' so it had to be an endpoint
				 * name that doesn't exist.
				 */
				ast_log(LOG_ERROR, "Unable to create PJSIP channel - endpoint '%s' was not found\n",
					args.endpoint);
				req_data->cause = AST_CAUSE_NO_ROUTE_DESTINATION;
				SCOPE_EXIT_RTN_VALUE(-1, "Endpoint not found\n");
			}
			request_user = args.endpoint;
			*endpoint_name++ = '\0';

			if (ast_strlen_zero(endpoint_name)) {
				ast_log(LOG_ERROR, "Unable to create PJSIP channel with empty endpoint name: %s@<endpoint-name>\n",
					request_user);
				req_data->cause = AST_CAUSE_CHANNEL_UNACCEPTABLE;
				SCOPE_EXIT_RTN_VALUE(-1, "Empty endpoint name\n");
			}

			endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
				endpoint_name);
			if (!endpoint) {
				ast_log(LOG_ERROR, "Unable to create PJSIP channel - endpoint '%s' was not found\n", endpoint_name);
				req_data->cause = AST_CAUSE_NO_ROUTE_DESTINATION;
				SCOPE_EXIT_RTN_VALUE(-1, "Endpoint not found\n");
			}
		}
	}

	session = ast_sip_session_create_outgoing(endpoint, NULL, args.aor, request_user,
		req_data->topology);
	ao2_ref(endpoint, -1);
	if (!session) {
		ast_log(LOG_ERROR, "Failed to create outgoing session to endpoint '%s'\n", endpoint_name);
		req_data->cause = AST_CAUSE_NO_ROUTE_DESTINATION;
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create session\n");
	}

	req_data->session = session;

	SCOPE_EXIT_RTN_VALUE(0);
}

/*! \brief Function called by core to create a new outgoing PJSIP session */
static struct ast_channel *chan_pjsip_request_with_stream_topology(const char *type, struct ast_stream_topology *topology, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct request_data req_data;
	RAII_VAR(struct ast_sip_session *, session, NULL, ao2_cleanup);
	SCOPE_ENTER(1, "%s Topology: %s\n", data,
		ast_str_tmp(256, ast_stream_topology_to_str(topology, &STR_TMP)));

	req_data.topology = topology;
	req_data.dest = data;
	/* Default failure value in case ast_sip_push_task_wait_servant() itself fails. */
	req_data.cause = AST_CAUSE_FAILURE;

	if (ast_sip_push_task_wait_servant(NULL, request, &req_data)) {
		*cause = req_data.cause;
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't push task\n");
	}

	session = req_data.session;

	if (!(session->channel = chan_pjsip_new(session, AST_STATE_DOWN, NULL, NULL, assignedids, requestor, NULL))) {
		/* Session needs to be terminated prematurely */
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't create channel\n");
	}

	SCOPE_EXIT_RTN_VALUE(session->channel, "Channel: %s\n", ast_channel_name(session->channel));
}

static struct ast_channel *chan_pjsip_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	struct ast_stream_topology *topology;
	struct ast_channel *chan;

	topology = ast_stream_topology_create_from_format_cap(cap);
	if (!topology) {
		return NULL;
	}

	chan = chan_pjsip_request_with_stream_topology(type, topology, assignedids, requestor, data, cause);

	ast_stream_topology_free(topology);

	return chan;
}

struct sendtext_data {
	struct ast_sip_session *session;
	struct ast_msg_data *msg;
};

static void sendtext_data_destroy(void *obj)
{
	struct sendtext_data *data = obj;
	ao2_cleanup(data->session);
	ast_free(data->msg);
}

static struct sendtext_data* sendtext_data_create(struct ast_channel *chan,
	struct ast_msg_data *msg)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);
	struct sendtext_data *data = ao2_alloc(sizeof(*data), sendtext_data_destroy);

	if (!data) {
		return NULL;
	}

	data->msg = ast_msg_data_dup(msg);
	if (!data->msg) {
		ao2_cleanup(data);
		return NULL;
	}
	data->session = channel->session;
	ao2_ref(data->session, +1);

	return data;
}

static int sendtext(void *obj)
{
	struct sendtext_data *data = obj;
	pjsip_tx_data *tdata;
	const char *body_text = ast_msg_data_get_attribute(data->msg, AST_MSG_DATA_ATTR_BODY);
	const char *content_type = ast_msg_data_get_attribute(data->msg, AST_MSG_DATA_ATTR_CONTENT_TYPE);
	char *sep;
	struct ast_sip_body body = {
		.type = "text",
		.subtype = "plain",
		.body_text = body_text,
	};

	if (!ast_strlen_zero(content_type)) {
		sep = strchr(content_type, '/');
		if (sep) {
			*sep = '\0';
			body.type = content_type;
			body.subtype = ++sep;
		}
	}

	if (data->session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Session already DISCONNECTED [reason=%d (%s)]\n",
			data->session->inv_session->cause,
			pjsip_get_status_text(data->session->inv_session->cause)->ptr);
	} else {
		pjsip_from_hdr *hdr;
		pjsip_name_addr *name_addr;
		const char *from = ast_msg_data_get_attribute(data->msg, AST_MSG_DATA_ATTR_FROM);
		const char *to = ast_msg_data_get_attribute(data->msg, AST_MSG_DATA_ATTR_TO);
		int invalidate_tdata = 0;

		ast_sip_create_request("MESSAGE", data->session->inv_session->dlg, data->session->endpoint, NULL, NULL, &tdata);
		ast_sip_add_body(tdata, &body);

		/*
		 * If we have a 'from' in the msg, set the display name in the From
		 * header to it.
		 */
		if (!ast_strlen_zero(from)) {
			hdr = PJSIP_MSG_FROM_HDR(tdata->msg);
			name_addr = (pjsip_name_addr *) hdr->uri;
			pj_strdup2(tdata->pool, &name_addr->display, from);
			invalidate_tdata = 1;
		}

		/*
		 * If we have a 'to' in the msg, set the display name in the To
		 * header to it.
		 */
		if (!ast_strlen_zero(to)) {
			hdr = PJSIP_MSG_TO_HDR(tdata->msg);
			name_addr = (pjsip_name_addr *) hdr->uri;
			pj_strdup2(tdata->pool, &name_addr->display, to);
			invalidate_tdata = 1;
		}

		if (invalidate_tdata) {
			pjsip_tx_data_invalidate_msg(tdata);
		}

		ast_sip_send_request(tdata, data->session->inv_session->dlg, data->session->endpoint, NULL, NULL);
	}

	ao2_cleanup(data);

	return 0;
}

/*! \brief Function called by core to send text on PJSIP session */
static int chan_pjsip_sendtext_data(struct ast_channel *ast, struct ast_msg_data *msg)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(ast);
	struct sendtext_data *data = sendtext_data_create(ast, msg);

	ast_debug(1, "Sending MESSAGE from '%s' to '%s:%s': %s\n",
		ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_FROM),
		ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_TO),
		ast_channel_name(ast),
		ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_BODY));

	if (!data) {
		return -1;
	}

	if (ast_sip_push_task(channel->session->serializer, sendtext, data)) {
		ao2_ref(data, -1);
		return -1;
	}
	return 0;
}

static int chan_pjsip_sendtext(struct ast_channel *ast, const char *text)
{
	struct ast_msg_data *msg;
	int rc;
	struct ast_msg_data_attribute attrs[] =
	{
		{
			.type = AST_MSG_DATA_ATTR_BODY,
			.value = (char *)text,
		}
	};

	msg = ast_msg_data_alloc(AST_MSG_DATA_SOURCE_TYPE_UNKNOWN, attrs, ARRAY_LEN(attrs));
	if (!msg) {
		return -1;
	}
	rc = chan_pjsip_sendtext_data(ast, msg);
	ast_free(msg);

	return rc;
}

/*! \brief Convert SIP hangup causes to Asterisk hangup causes */
static int hangup_sip2cause(int cause)
{
	/* Possible values taken from causes.h */

	switch(cause) {
	case 401:       /* Unauthorized */
		return AST_CAUSE_CALL_REJECTED;
	case 403:       /* Not found */
		return AST_CAUSE_CALL_REJECTED;
	case 404:       /* Not found */
		return AST_CAUSE_UNALLOCATED;
	case 405:       /* Method not allowed */
		return AST_CAUSE_INTERWORKING;
	case 407:       /* Proxy authentication required */
		return AST_CAUSE_CALL_REJECTED;
	case 408:       /* No reaction */
		return AST_CAUSE_NO_USER_RESPONSE;
	case 409:       /* Conflict */
		return AST_CAUSE_NORMAL_TEMPORARY_FAILURE;
	case 410:       /* Gone */
		return AST_CAUSE_NUMBER_CHANGED;
	case 411:       /* Length required */
		return AST_CAUSE_INTERWORKING;
	case 413:       /* Request entity too large */
		return AST_CAUSE_INTERWORKING;
	case 414:       /* Request URI too large */
		return AST_CAUSE_INTERWORKING;
	case 415:       /* Unsupported media type */
		return AST_CAUSE_INTERWORKING;
	case 420:       /* Bad extension */
		return AST_CAUSE_NO_ROUTE_DESTINATION;
	case 480:       /* No answer */
		return AST_CAUSE_NO_ANSWER;
	case 481:       /* No answer */
		return AST_CAUSE_INTERWORKING;
	case 482:       /* Loop detected */
		return AST_CAUSE_INTERWORKING;
	case 483:       /* Too many hops */
		return AST_CAUSE_NO_ANSWER;
	case 484:       /* Address incomplete */
		return AST_CAUSE_INVALID_NUMBER_FORMAT;
	case 485:       /* Ambiguous */
		return AST_CAUSE_UNALLOCATED;
	case 486:       /* Busy everywhere */
		return AST_CAUSE_BUSY;
	case 487:       /* Request terminated */
		return AST_CAUSE_INTERWORKING;
	case 488:       /* No codecs approved */
		return AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
	case 491:       /* Request pending */
		return AST_CAUSE_INTERWORKING;
	case 493:       /* Undecipherable */
		return AST_CAUSE_INTERWORKING;
	case 500:       /* Server internal failure */
		return AST_CAUSE_FAILURE;
	case 501:       /* Call rejected */
		return AST_CAUSE_FACILITY_REJECTED;
	case 502:
		return AST_CAUSE_DESTINATION_OUT_OF_ORDER;
	case 503:       /* Service unavailable */
		return AST_CAUSE_CONGESTION;
	case 504:       /* Gateway timeout */
		return AST_CAUSE_RECOVERY_ON_TIMER_EXPIRE;
	case 505:       /* SIP version not supported */
		return AST_CAUSE_INTERWORKING;
	case 600:       /* Busy everywhere */
		return AST_CAUSE_USER_BUSY;
	case 603:       /* Decline */
		return AST_CAUSE_CALL_REJECTED;
	case 604:       /* Does not exist anywhere */
		return AST_CAUSE_UNALLOCATED;
	case 606:       /* Not acceptable */
		return AST_CAUSE_BEARERCAPABILITY_NOTAVAIL;
	default:
		if (cause < 500 && cause >= 400) {
			/* 4xx class error that is unknown - someting wrong with our request */
			return AST_CAUSE_INTERWORKING;
		} else if (cause < 600 && cause >= 500) {
			/* 5xx class error - problem in the remote end */
			return AST_CAUSE_CONGESTION;
		} else if (cause < 700 && cause >= 600) {
			/* 6xx - global errors in the 4xx class */
			return AST_CAUSE_INTERWORKING;
		}
		return AST_CAUSE_NORMAL;
	}
	/* Never reached */
	return 0;
}

static void chan_pjsip_session_begin(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	if (session->endpoint->media.direct_media.glare_mitigation ==
			AST_SIP_DIRECT_MEDIA_GLARE_MITIGATION_NONE) {
		SCOPE_EXIT_RTN("Direct media no glare mitigation\n");
	}

	datastore = ast_sip_session_alloc_datastore(&direct_media_mitigation_info,
			"direct_media_glare_mitigation");

	if (!datastore) {
		SCOPE_EXIT_RTN("Couldn't create datastore\n");
	}

	ast_sip_session_add_datastore(session, datastore);
	SCOPE_EXIT_RTN();
}

/*! \brief Function called when the session ends */
static void chan_pjsip_session_end(struct ast_sip_session *session)
{
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	if (!session->channel) {
		SCOPE_EXIT_RTN("No channel\n");
	}

	chan_pjsip_remove_hold(ast_channel_uniqueid(session->channel));

	ast_set_hangupsource(session->channel, ast_channel_name(session->channel), 0);
	if (!ast_channel_hangupcause(session->channel) && session->inv_session) {
		int cause = hangup_sip2cause(session->inv_session->cause);

		ast_queue_hangup_with_cause(session->channel, cause);
	} else {
		ast_queue_hangup(session->channel);
	}

	SCOPE_EXIT_RTN();
}

static void set_sipdomain_variable(struct ast_sip_session *session)
{
	pjsip_sip_uri *sip_ruri = pjsip_uri_get_uri(session->request_uri);
	size_t size = pj_strlen(&sip_ruri->host) + 1;
	char *domain = ast_alloca(size);

	ast_copy_pj_str(domain, &sip_ruri->host, size);

	pbx_builtin_setvar_helper(session->channel, "SIPDOMAIN", domain);
	return;
}

/*! \brief Function called when a request is received on the session */
static int chan_pjsip_incoming_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	struct transport_info_data *transport_data;
	pjsip_tx_data *packet = NULL;
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	if (session->channel) {
		SCOPE_EXIT_RTN_VALUE(0, "%s: No channel\n", ast_sip_session_get_name(session));
	}

	/* Check for a to-tag to determine if this is a reinvite */
	if (rdata->msg_info.to->tag.slen) {
		/* Weird case. We've received a reinvite but we don't have a channel. The most
		 * typical case for this happening is that a blind transfer fails, and so the
		 * transferer attempts to reinvite himself back into the call. We already got
		 * rid of that channel, and the other side of the call is unrecoverable.
		 *
		 * We treat this as a failure, so our best bet is to just hang this call
		 * up and not create a new channel. Clearing defer_terminate here ensures that
		 * calling ast_sip_session_terminate() can result in a BYE being sent ASAP.
		 */
		session->defer_terminate = 0;
		ast_sip_session_terminate(session, 400);
		SCOPE_EXIT_RTN_VALUE(-1, "%s: We have a To tag but no channel.  Terminating session\n", ast_sip_session_get_name(session));
	}

	datastore = ast_sip_session_alloc_datastore(&transport_info, "transport_info");
	if (!datastore) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Couldn't alloc transport_info datastore\n", ast_sip_session_get_name(session));
	}

	transport_data = ast_calloc(1, sizeof(*transport_data));
	if (!transport_data) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Couldn't alloc transport_info\n", ast_sip_session_get_name(session));
	}
	pj_sockaddr_cp(&transport_data->local_addr, &rdata->tp_info.transport->local_addr);
	pj_sockaddr_cp(&transport_data->remote_addr, &rdata->pkt_info.src_addr);
	datastore->data = transport_data;
	ast_sip_session_add_datastore(session, datastore);

	if (!(session->channel = chan_pjsip_new(session, AST_STATE_RING, session->exten, NULL, NULL, NULL, NULL))) {
		if (pjsip_inv_end_session(session->inv_session, 503, NULL, &packet) == PJ_SUCCESS
			&& packet) {
			ast_sip_session_send_response(session, packet);
		}

		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Failed to allocate new PJSIP channel on incoming SIP INVITE\n",
			 ast_sip_session_get_name(session));
	}

	set_sipdomain_variable(session);

	/* channel gets created on incoming request, but we wait to call start
           so other supplements have a chance to run */
	SCOPE_EXIT_RTN_VALUE(0, "%s\n", ast_sip_session_get_name(session));
}

static int call_pickup_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	struct ast_features_pickup_config *pickup_cfg;
	struct ast_channel *chan;

	/* Check for a to-tag to determine if this is a reinvite */
	if (rdata->msg_info.to->tag.slen) {
		/* We don't care about reinvites */
		return 0;
	}

	pickup_cfg = ast_get_chan_features_pickup_config(session->channel);
	if (!pickup_cfg) {
		ast_log(LOG_ERROR, "Unable to retrieve pickup configuration options. Unable to detect call pickup extension.\n");
		return 0;
	}

	if (strcmp(session->exten, pickup_cfg->pickupexten)) {
		ao2_ref(pickup_cfg, -1);
		return 0;
	}
	ao2_ref(pickup_cfg, -1);

	/* We can't directly use session->channel because the pickup operation will cause a masquerade to occur,
	 * changing the channel pointer in session to a different channel. To ensure we work on the right channel
	 * we store a pointer locally before we begin and keep a reference so it remains valid no matter what.
	 */
	chan = ast_channel_ref(session->channel);
	if (ast_pickup_call(chan)) {
		ast_channel_hangupcause_set(chan, AST_CAUSE_CALL_REJECTED);
	} else {
		ast_channel_hangupcause_set(chan, AST_CAUSE_NORMAL_CLEARING);
	}
	/* A hangup always occurs because the pickup operation will have either failed resulting in the call
	 * needing to be hung up OR the pickup operation was a success and the channel we now have is actually
	 * the channel that was replaced, which should be hung up since it is literally in limbo not connected
	 * to anything at all.
	 */
	ast_hangup(chan);
	ast_channel_unref(chan);

	return 1;
}

static struct ast_sip_session_supplement call_pickup_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_LAST - 1,
	.incoming_request = call_pickup_incoming_request,
};

static int pbx_start_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	int res;
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	/* Check for a to-tag to determine if this is a reinvite */
	if (rdata->msg_info.to->tag.slen) {
		/* We don't care about reinvites */
		SCOPE_EXIT_RTN_VALUE(0, "Reinvite\n");
	}

	res = ast_pbx_start(session->channel);

	switch (res) {
	case AST_PBX_FAILED:
		ast_log(LOG_WARNING, "Failed to start PBX ;(\n");
		ast_channel_hangupcause_set(session->channel, AST_CAUSE_SWITCH_CONGESTION);
		ast_hangup(session->channel);
		break;
	case AST_PBX_CALL_LIMIT:
		ast_log(LOG_WARNING, "Failed to start PBX (call limit reached) \n");
		ast_channel_hangupcause_set(session->channel, AST_CAUSE_SWITCH_CONGESTION);
		ast_hangup(session->channel);
		break;
	case AST_PBX_SUCCESS:
	default:
		break;
	}

	ast_debug(3, "Started PBX on new PJSIP channel %s\n", ast_channel_name(session->channel));

	SCOPE_EXIT_RTN_VALUE((res == AST_PBX_SUCCESS) ? 0 : -1, "RC: %d\n", res);
}

static struct ast_sip_session_supplement pbx_start_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_LAST,
	.incoming_request = pbx_start_incoming_request,
};

/*! \brief Function called when a response is received on the session */
static void chan_pjsip_incoming_response_update_cause(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	struct pjsip_status_line status = rdata->msg_info.msg->line.status;
	struct ast_control_pvt_cause_code *cause_code;
	int data_size = sizeof(*cause_code);
	SCOPE_ENTER(3, "%s: Status: %d\n", ast_sip_session_get_name(session), status.code);

	if (!session->channel) {
		SCOPE_EXIT_RTN("%s: No channel\n", ast_sip_session_get_name(session));
	}

	/* Build and send the tech-specific cause information */
	/* size of the string making up the cause code is "SIP " number + " " + reason length */
	data_size += 4 + 4 + pj_strlen(&status.reason);
	cause_code = ast_alloca(data_size);
	memset(cause_code, 0, data_size);

	ast_copy_string(cause_code->chan_name, ast_channel_name(session->channel), AST_CHANNEL_NAME);

	snprintf(cause_code->code, data_size - sizeof(*cause_code) + 1, "SIP %d %.*s", status.code,
	(int) pj_strlen(&status.reason), pj_strbuf(&status.reason));

	cause_code->ast_cause = hangup_sip2cause(status.code);
	ast_queue_control_data(session->channel, AST_CONTROL_PVT_CAUSE_CODE, cause_code, data_size);
	ast_channel_hangupcause_hash_set(session->channel, cause_code, data_size);

	SCOPE_EXIT_RTN("%s\n", ast_sip_session_get_name(session));
}

/*! \brief Function called when a response is received on the session */
static void chan_pjsip_incoming_response(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	struct pjsip_status_line status = rdata->msg_info.msg->line.status;
	SCOPE_ENTER(3, "%s: Status: %d\n", ast_sip_session_get_name(session), status.code);

	if (!session->channel) {
		SCOPE_EXIT_RTN("%s: No channel\n", ast_sip_session_get_name(session));
	}

	switch (status.code) {
	case 180: {
		pjsip_rdata_sdp_info *sdp = pjsip_rdata_get_sdp_info(rdata);
		if (sdp && sdp->body.ptr) {
			ast_trace(-1, "%s: Queueing PROGRESS\n", ast_sip_session_get_name(session));
			ast_queue_control(session->channel, AST_CONTROL_PROGRESS);
		} else {
			ast_trace(-1, "%s: Queueing RINGING\n", ast_sip_session_get_name(session));
			ast_queue_control(session->channel, AST_CONTROL_RINGING);
		}

		ast_channel_lock(session->channel);
		if (ast_channel_state(session->channel) != AST_STATE_UP) {
			ast_setstate(session->channel, AST_STATE_RINGING);
		}
		ast_channel_unlock(session->channel);
		break;
	}
	case 183:
		if (session->endpoint->ignore_183_without_sdp) {
			pjsip_rdata_sdp_info *sdp = pjsip_rdata_get_sdp_info(rdata);
			if (sdp && sdp->body.ptr) {
				ast_trace(-1, "%s: Queueing PROGRESS\n", ast_sip_session_get_name(session));
				ast_trace(1, "%s Method: %.*s Status: %d  Queueing PROGRESS with SDP\n", ast_sip_session_get_name(session),
					(int)rdata->msg_info.cseq->method.name.slen, rdata->msg_info.cseq->method.name.ptr, status.code);
				ast_queue_control(session->channel, AST_CONTROL_PROGRESS);
			}
		} else {
			ast_trace(-1, "%s: Queueing PROGRESS\n", ast_sip_session_get_name(session));
			ast_trace(1, "%s Method: %.*s Status: %d  Queueing PROGRESS without SDP\n", ast_sip_session_get_name(session),
				(int)rdata->msg_info.cseq->method.name.slen, rdata->msg_info.cseq->method.name.ptr, status.code);
			ast_queue_control(session->channel, AST_CONTROL_PROGRESS);
		}
		break;
	case 200:
		ast_trace(-1, "%s: Queueing ANSWER\n", ast_sip_session_get_name(session));
		ast_queue_control(session->channel, AST_CONTROL_ANSWER);
		break;
	default:
		ast_trace(-1, "%s: Not queueing anything\n", ast_sip_session_get_name(session));
		break;
	}

	SCOPE_EXIT_RTN("%s\n", ast_sip_session_get_name(session));
}

static int chan_pjsip_incoming_ack(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	if (rdata->msg_info.msg->line.req.method.id == PJSIP_ACK_METHOD) {
		if (session->endpoint->media.direct_media.enabled && session->channel) {
			ast_trace(-1, "%s: Queueing SRCCHANGE\n", ast_sip_session_get_name(session));
			ast_queue_control(session->channel, AST_CONTROL_SRCCHANGE);
		}
	}
	SCOPE_EXIT_RTN_VALUE(0, "%s\n", ast_sip_session_get_name(session));
}

static int update_devstate(void *obj, void *arg, int flags)
{
	ast_devstate_changed(AST_DEVICE_UNKNOWN, AST_DEVSTATE_CACHABLE,
			     "PJSIP/%s", ast_sorcery_object_get_id(obj));
	return 0;
}

static struct ast_custom_function chan_pjsip_dial_contacts_function = {
	.name = "PJSIP_DIAL_CONTACTS",
	.read = pjsip_acf_dial_contacts_read,
};

static struct ast_custom_function chan_pjsip_parse_uri_function = {
	.name = "PJSIP_PARSE_URI",
	.read = pjsip_acf_parse_uri_read,
};

static struct ast_custom_function media_offer_function = {
	.name = "PJSIP_MEDIA_OFFER",
	.read = pjsip_acf_media_offer_read,
	.write = pjsip_acf_media_offer_write
};

static struct ast_custom_function dtmf_mode_function = {
	.name = "PJSIP_DTMF_MODE",
	.read = pjsip_acf_dtmf_mode_read,
	.write = pjsip_acf_dtmf_mode_write
};

static struct ast_custom_function moh_passthrough_function = {
	.name = "PJSIP_MOH_PASSTHROUGH",
	.read = pjsip_acf_moh_passthrough_read,
	.write = pjsip_acf_moh_passthrough_write
};

static struct ast_custom_function session_refresh_function = {
	.name = "PJSIP_SEND_SESSION_REFRESH",
	.write = pjsip_acf_session_refresh_write,
};

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
	struct ao2_container *endpoints;

	if (!(chan_pjsip_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_format_cap_append_by_type(chan_pjsip_tech.capabilities, AST_MEDIA_TYPE_AUDIO);

	ast_rtp_glue_register(&chan_pjsip_rtp_glue);

	if (ast_channel_register(&chan_pjsip_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class %s\n", channel_type);
		goto end;
	}

	if (ast_custom_function_register(&chan_pjsip_dial_contacts_function)) {
		ast_log(LOG_ERROR, "Unable to register PJSIP_DIAL_CONTACTS dialplan function\n");
		goto end;
	}

	if (ast_custom_function_register(&chan_pjsip_parse_uri_function)) {
		ast_log(LOG_ERROR, "Unable to register PJSIP_PARSE_URI dialplan function\n");
		goto end;
	}

	if (ast_custom_function_register(&media_offer_function)) {
		ast_log(LOG_WARNING, "Unable to register PJSIP_MEDIA_OFFER dialplan function\n");
		goto end;
	}

	if (ast_custom_function_register(&dtmf_mode_function)) {
		ast_log(LOG_WARNING, "Unable to register PJSIP_DTMF_MODE dialplan function\n");
		goto end;
	}

	if (ast_custom_function_register(&moh_passthrough_function)) {
		ast_log(LOG_WARNING, "Unable to register PJSIP_MOH_PASSTHROUGH dialplan function\n");
		goto end;
	}

	if (ast_custom_function_register(&session_refresh_function)) {
		ast_log(LOG_WARNING, "Unable to register PJSIP_SEND_SESSION_REFRESH dialplan function\n");
		goto end;
	}

	ast_sip_register_service(&refer_callback_module);

	ast_sip_session_register_supplement(&chan_pjsip_supplement);
	ast_sip_session_register_supplement(&chan_pjsip_supplement_response);

	if (!(pjsip_uids_onhold = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_RWLOCK,
			AO2_CONTAINER_ALLOC_OPT_DUPS_REJECT, 37, uid_hold_hash_fn,
			uid_hold_sort_fn, NULL))) {
		ast_log(LOG_ERROR, "Unable to create held channels container\n");
		goto end;
	}

	ast_sip_session_register_supplement(&call_pickup_supplement);
	ast_sip_session_register_supplement(&pbx_start_supplement);
	ast_sip_session_register_supplement(&chan_pjsip_ack_supplement);

	if (pjsip_channel_cli_register()) {
		ast_log(LOG_ERROR, "Unable to register PJSIP Channel CLI\n");
		goto end;
	}

	/* since endpoints are loaded before the channel driver their device
	   states get set to 'invalid', so they need to be updated */
	if ((endpoints = ast_sip_get_endpoints())) {
		ao2_callback(endpoints, OBJ_NODATA, update_devstate, NULL);
		ao2_ref(endpoints, -1);
	}

	return 0;

end:
	ao2_cleanup(pjsip_uids_onhold);
	pjsip_uids_onhold = NULL;
	ast_sip_session_unregister_supplement(&chan_pjsip_ack_supplement);
	ast_sip_session_unregister_supplement(&pbx_start_supplement);
	ast_sip_session_unregister_supplement(&chan_pjsip_supplement_response);
	ast_sip_session_unregister_supplement(&chan_pjsip_supplement);
	ast_sip_session_unregister_supplement(&call_pickup_supplement);
	ast_sip_unregister_service(&refer_callback_module);
	ast_custom_function_unregister(&dtmf_mode_function);
	ast_custom_function_unregister(&moh_passthrough_function);
	ast_custom_function_unregister(&media_offer_function);
	ast_custom_function_unregister(&chan_pjsip_dial_contacts_function);
	ast_custom_function_unregister(&chan_pjsip_parse_uri_function);
	ast_custom_function_unregister(&session_refresh_function);
	ast_channel_unregister(&chan_pjsip_tech);
	ast_rtp_glue_unregister(&chan_pjsip_rtp_glue);

	return AST_MODULE_LOAD_DECLINE;
}

/*! \brief Unload the PJSIP channel from Asterisk */
static int unload_module(void)
{
	ao2_cleanup(pjsip_uids_onhold);
	pjsip_uids_onhold = NULL;

	pjsip_channel_cli_unregister();

	ast_sip_session_unregister_supplement(&chan_pjsip_supplement_response);
	ast_sip_session_unregister_supplement(&chan_pjsip_supplement);
	ast_sip_session_unregister_supplement(&pbx_start_supplement);
	ast_sip_session_unregister_supplement(&chan_pjsip_ack_supplement);
	ast_sip_session_unregister_supplement(&call_pickup_supplement);

	ast_sip_unregister_service(&refer_callback_module);

	ast_custom_function_unregister(&dtmf_mode_function);
	ast_custom_function_unregister(&moh_passthrough_function);
	ast_custom_function_unregister(&media_offer_function);
	ast_custom_function_unregister(&chan_pjsip_dial_contacts_function);
	ast_custom_function_unregister(&chan_pjsip_parse_uri_function);
	ast_custom_function_unregister(&session_refresh_function);

	ast_channel_unregister(&chan_pjsip_tech);
	ao2_ref(chan_pjsip_tech.capabilities, -1);
	ast_rtp_glue_unregister(&chan_pjsip_rtp_glue);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Channel Driver",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	.requires = "res_pjsip,res_pjsip_session,res_pjsip_pubsub",
);
