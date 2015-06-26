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
 * \brief SIP T.38 handling
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

ASTERISK_REGISTER_FILE()

#include "asterisk/module.h"
#include "asterisk/udptl.h"
#include "asterisk/netsock2.h"
#include "asterisk/channel.h"
#include "asterisk/acl.h"

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"

/*! \brief The number of seconds after receiving a T.38 re-invite before automatically rejecting it */
#define T38_AUTOMATIC_REJECTION_SECONDS 5

/*! \brief Address for IPv4 UDPTL */
static struct ast_sockaddr address_ipv4;

/*! \brief Address for IPv6 UDPTL */
static struct ast_sockaddr address_ipv6;

/*! \brief T.38 state information */
struct t38_state {
	/*! \brief Current state */
	enum ast_sip_session_t38state state;
	/*! \brief Our T.38 parameters */
	struct ast_control_t38_parameters our_parms;
	/*! \brief Their T.38 parameters */
	struct ast_control_t38_parameters their_parms;
	/*! \brief Timer entry for automatically rejecting an inbound re-invite */
	pj_timer_entry timer;
};

/*! \brief Destructor for T.38 state information */
static void t38_state_destroy(void *obj)
{
	ast_free(obj);
}

/*! \brief Datastore for attaching T.38 state information */
static const struct ast_datastore_info t38_datastore = {
	.type = "t38",
	.destroy = t38_state_destroy,
};

/*! \brief Structure for T.38 parameters task data */
struct t38_parameters_task_data {
	/*! \brief Session itself */
	struct ast_sip_session *session;
	/*! \brief T.38 control frame */
	struct ast_frame *frame;
};

/*! \brief Destructor for T.38 data */
static void t38_parameters_task_data_destroy(void *obj)
{
	struct t38_parameters_task_data *data = obj;

	ao2_cleanup(data->session);

	if (data->frame) {
		ast_frfree(data->frame);
	}
}

/*! \brief Allocator for T.38 data */
static struct t38_parameters_task_data *t38_parameters_task_data_alloc(struct ast_sip_session *session,
	struct ast_frame *frame)
{
	struct t38_parameters_task_data *data = ao2_alloc(sizeof(*data), t38_parameters_task_data_destroy);

	if (!data) {
		return NULL;
	}

	data->session = session;
	ao2_ref(session, +1);
	data->frame = ast_frdup(frame);
	if (!data->frame) {
		ao2_ref(data, -1);
		data = NULL;
	}

	return data;
}

/*! \brief Helper function for changing the T.38 state */
static void t38_change_state(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	struct t38_state *state, enum ast_sip_session_t38state new_state)
{
	enum ast_sip_session_t38state old_state = session->t38state;
	struct ast_control_t38_parameters parameters = { .request_response = 0, };
	pj_time_val delay = { .sec = T38_AUTOMATIC_REJECTION_SECONDS };

	if (old_state == new_state) {
		return;
	}

	session->t38state = new_state;
	ast_debug(2, "T.38 state changed to '%u' from '%u' on channel '%s'\n",
		new_state, old_state,
		session->channel ? ast_channel_name(session->channel) : "<gone>");

	if (pj_timer_heap_cancel(pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()), &state->timer)) {
		ast_debug(2, "Automatic T.38 rejection on channel '%s' terminated\n",
			session->channel ? ast_channel_name(session->channel) : "<gone>");
		ao2_ref(session, -1);
	}

	if (!session->channel) {
		return;
	}

	switch (new_state) {
	case T38_PEER_REINVITE:
		ao2_ref(session, +1);
		if (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(), &state->timer, &delay) != PJ_SUCCESS) {
			ast_log(LOG_WARNING, "Scheduling of automatic T.38 rejection for channel '%s' failed\n",
				ast_channel_name(session->channel));
			ao2_ref(session, -1);
		}
		parameters = state->their_parms;
		parameters.max_ifp = ast_udptl_get_far_max_ifp(session_media->udptl);
		parameters.request_response = AST_T38_REQUEST_NEGOTIATE;
		ast_udptl_set_tag(session_media->udptl, "%s", ast_channel_name(session->channel));
		break;
	case T38_ENABLED:
		parameters = state->their_parms;
		parameters.max_ifp = ast_udptl_get_far_max_ifp(session_media->udptl);
		parameters.request_response = AST_T38_NEGOTIATED;
		ast_udptl_set_tag(session_media->udptl, "%s", ast_channel_name(session->channel));
		break;
	case T38_REJECTED:
	case T38_DISABLED:
		if (old_state == T38_ENABLED) {
			parameters.request_response = AST_T38_TERMINATED;
		} else if (old_state == T38_LOCAL_REINVITE) {
			parameters.request_response = AST_T38_REFUSED;
		}
		break;
	case T38_LOCAL_REINVITE:
		/* wait until we get a peer response before responding to local reinvite */
		break;
	case T38_MAX_ENUM:
		/* Well, that shouldn't happen */
		ast_assert(0);
		break;
	}

	if (parameters.request_response) {
		ast_queue_control_data(session->channel, AST_CONTROL_T38_PARAMETERS, &parameters, sizeof(parameters));
	}
}

/*! \brief Task function which rejects a T.38 re-invite and resumes handling it */
static int t38_automatic_reject(void *obj)
{
	RAII_VAR(struct ast_sip_session *, session, obj, ao2_cleanup);
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_session_get_datastore(session, "t38"), ao2_cleanup);
	RAII_VAR(struct ast_sip_session_media *, session_media, ao2_find(session->media, "image", OBJ_KEY), ao2_cleanup);

	if (!datastore) {
		return 0;
	}

	ast_debug(2, "Automatically rejecting T.38 request on channel '%s'\n",
		session->channel ? ast_channel_name(session->channel) : "<gone>");

	t38_change_state(session, session_media, datastore->data, T38_REJECTED);
	ast_sip_session_resume_reinvite(session);

	return 0;
}

/*! \brief Timer entry callback which queues a task to reject a T.38 re-invite and resume handling it */
static void t38_automatic_reject_timer_cb(pj_timer_heap_t *timer_heap, struct pj_timer_entry *entry)
{
	struct ast_sip_session *session = entry->user_data;

	if (ast_sip_push_task(session->serializer, t38_automatic_reject, session)) {
		ao2_ref(session, -1);
	}
}

/*! \brief Helper function which retrieves or allocates a T.38 state information datastore */
static struct t38_state *t38_state_get_or_alloc(struct ast_sip_session *session)
{
	RAII_VAR(struct ast_datastore *, datastore, ast_sip_session_get_datastore(session, "t38"), ao2_cleanup);
	struct t38_state *state;

	/* While the datastore refcount is decremented this is operating in the serializer so it will remain valid regardless */
	if (datastore) {
		return datastore->data;
	}

	if (!(datastore = ast_sip_session_alloc_datastore(&t38_datastore, "t38"))
		|| !(datastore->data = ast_calloc(1, sizeof(struct t38_state)))
		|| ast_sip_session_add_datastore(session, datastore)) {
		return NULL;
	}

	state = datastore->data;

	/* This will get bumped up before scheduling */
	state->timer.user_data = session;
	state->timer.cb = t38_automatic_reject_timer_cb;

	datastore->data = state;

	return state;
}

/*! \brief Initializes UDPTL support on a session, only done when actually needed */
static int t38_initialize_session(struct ast_sip_session *session, struct ast_sip_session_media *session_media)
{
	if (session_media->udptl) {
		return 0;
	}

	if (!(session_media->udptl = ast_udptl_new_with_bindaddr(NULL, NULL, 0,
		session->endpoint->media.t38.ipv6 ? &address_ipv6 : &address_ipv4))) {
		return -1;
	}

	ast_channel_set_fd(session->channel, 5, ast_udptl_fd(session_media->udptl));
	ast_udptl_set_error_correction_scheme(session_media->udptl, session->endpoint->media.t38.error_correction);
	ast_udptl_setnat(session_media->udptl, session->endpoint->media.t38.nat);
	ast_udptl_set_far_max_datagram(session_media->udptl, session->endpoint->media.t38.maxdatagram);

	return 0;
}

/*! \brief Callback for when T.38 reinvite SDP is created */
static int t38_reinvite_sdp_cb(struct ast_sip_session *session, pjmedia_sdp_session *sdp)
{
	int stream;

	/* Move the image media stream to the front and have it as the only stream, pjmedia will fill in
	 * dummy streams for the rest
	 */
	for (stream = 0; stream < sdp->media_count++; ++stream) {
		if (!pj_strcmp2(&sdp->media[stream]->desc.media, "image")) {
			sdp->media[0] = sdp->media[stream];
			sdp->media_count = 1;
			break;
		}
	}

	return 0;
}

/*! \brief Callback for when a response is received for a T.38 re-invite */
static int t38_reinvite_response_cb(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	struct pjsip_status_line status = rdata->msg_info.msg->line.status;
	struct t38_state *state;
	RAII_VAR(struct ast_sip_session_media *, session_media, NULL, ao2_cleanup);

	if (status.code == 100) {
		return 0;
	}

	if (!(state = t38_state_get_or_alloc(session)) ||
		!(session_media = ao2_find(session->media, "image", OBJ_KEY))) {
		ast_log(LOG_WARNING, "Received response to T.38 re-invite on '%s' but state unavailable\n",
			ast_channel_name(session->channel));
		return 0;
	}

	t38_change_state(session, session_media, state, (status.code == 200) ? T38_ENABLED : T38_REJECTED);

	return 0;
}

/*! \brief Task for reacting to T.38 control frame */
static int t38_interpret_parameters(void *obj)
{
	RAII_VAR(struct t38_parameters_task_data *, data, obj, ao2_cleanup);
	const struct ast_control_t38_parameters *parameters = data->frame->data.ptr;
	struct t38_state *state = t38_state_get_or_alloc(data->session);
	RAII_VAR(struct ast_sip_session_media *, session_media, ao2_find(data->session->media, "image", OBJ_KEY), ao2_cleanup);

	/* Without session media or state we can't interpret parameters */
	if (!session_media || !state) {
		return 0;
	}

	switch (parameters->request_response) {
	case AST_T38_NEGOTIATED:
	case AST_T38_REQUEST_NEGOTIATE:         /* Request T38 */
		/* Negotiation can not take place without a valid max_ifp value. */
		if (!parameters->max_ifp) {
			t38_change_state(data->session, session_media, state, T38_REJECTED);
			if (data->session->t38state == T38_PEER_REINVITE) {
				ast_sip_session_resume_reinvite(data->session);
			}
			break;
		} else if (data->session->t38state == T38_PEER_REINVITE) {
			state->our_parms = *parameters;
			/* modify our parameters to conform to the peer's parameters,
			 * based on the rules in the ITU T.38 recommendation
			 */
			if (!state->their_parms.fill_bit_removal) {
				state->our_parms.fill_bit_removal = 0;
			}
			if (!state->their_parms.transcoding_mmr) {
				state->our_parms.transcoding_mmr = 0;
			}
			if (!state->their_parms.transcoding_jbig) {
				state->our_parms.transcoding_jbig = 0;
			}
			state->our_parms.version = MIN(state->our_parms.version, state->their_parms.version);
			state->our_parms.rate_management = state->their_parms.rate_management;
			ast_udptl_set_local_max_ifp(session_media->udptl, state->our_parms.max_ifp);
			t38_change_state(data->session, session_media, state, T38_ENABLED);
			ast_sip_session_resume_reinvite(data->session);
		} else if (data->session->t38state != T38_ENABLED) {
			if (t38_initialize_session(data->session, session_media)) {
				break;
			}
			state->our_parms = *parameters;
			ast_udptl_set_local_max_ifp(session_media->udptl, state->our_parms.max_ifp);
			t38_change_state(data->session, session_media, state, T38_LOCAL_REINVITE);
			ast_sip_session_refresh(data->session, NULL, t38_reinvite_sdp_cb, t38_reinvite_response_cb,
				AST_SIP_SESSION_REFRESH_METHOD_INVITE, 1);
		}
		break;
	case AST_T38_TERMINATED:
	case AST_T38_REFUSED:
	case AST_T38_REQUEST_TERMINATE:         /* Shutdown T38 */
		if (data->session->t38state == T38_PEER_REINVITE) {
			t38_change_state(data->session, session_media, state, T38_REJECTED);
			ast_sip_session_resume_reinvite(data->session);
		} else if (data->session->t38state == T38_ENABLED) {
			t38_change_state(data->session, session_media, state, T38_DISABLED);
			ast_sip_session_refresh(data->session, NULL, NULL, NULL, AST_SIP_SESSION_REFRESH_METHOD_INVITE, 1);
		}
		break;
	case AST_T38_REQUEST_PARMS: {		/* Application wants remote's parameters re-sent */
		struct ast_control_t38_parameters parameters = state->their_parms;

		if (data->session->t38state == T38_PEER_REINVITE) {
			parameters.max_ifp = ast_udptl_get_far_max_ifp(session_media->udptl);
			parameters.request_response = AST_T38_REQUEST_NEGOTIATE;
			ast_queue_control_data(data->session->channel, AST_CONTROL_T38_PARAMETERS, &parameters, sizeof(parameters));
		}
		break;
	}
	default:
		break;
	}

	return 0;
}

/*! \brief Frame hook callback for writing */
static struct ast_frame *t38_framehook_write(struct ast_sip_session *session, struct ast_frame *f)
{
	if (f->frametype == AST_FRAME_CONTROL && f->subclass.integer == AST_CONTROL_T38_PARAMETERS &&
		session->endpoint->media.t38.enabled) {
		struct t38_parameters_task_data *data = t38_parameters_task_data_alloc(session, f);

		if (!data) {
			return f;
		}

		if (ast_sip_push_task(session->serializer, t38_interpret_parameters, data)) {
			ao2_ref(data, -1);
		}
	} else if (f->frametype == AST_FRAME_MODEM) {
		RAII_VAR(struct ast_sip_session_media *, session_media, NULL, ao2_cleanup);

		if ((session_media = ao2_find(session->media, "image", OBJ_KEY)) &&
			session_media->udptl) {
			ast_udptl_write(session_media->udptl, f);
		}
	}

	return f;
}

/*! \brief Frame hook callback for reading */
static struct ast_frame *t38_framehook_read(struct ast_sip_session *session, struct ast_frame *f)
{
	if (ast_channel_fdno(session->channel) == 5) {
		RAII_VAR(struct ast_sip_session_media *, session_media, NULL, ao2_cleanup);

		if ((session_media = ao2_find(session->media, "image", OBJ_KEY)) &&
			session_media->udptl) {
			f = ast_udptl_read(session_media->udptl);
		}
	}

	return f;
}

/*! \brief Frame hook callback for T.38 related stuff */
static struct ast_frame *t38_framehook(struct ast_channel *chan, struct ast_frame *f,
	enum ast_framehook_event event, void *data)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);

	if (event == AST_FRAMEHOOK_EVENT_READ) {
		f = t38_framehook_read(channel->session, f);
	} else if (event == AST_FRAMEHOOK_EVENT_WRITE) {
		f = t38_framehook_write(channel->session, f);
	}

	return f;
}

static void t38_masq(void *data, int framehook_id,
        struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	if (ast_channel_tech(old_chan) == ast_channel_tech(new_chan)) {
		return;
	}

	/* This framehook is only applicable to PJSIP channels */
	ast_framehook_detach(new_chan, framehook_id);
}

static const struct ast_datastore_info t38_framehook_datastore = {
	.type = "T38 framehook",
};

/*! \brief Function called to attach T.38 framehook to channel when appropriate */
static void t38_attach_framehook(struct ast_sip_session *session)
{
	int framehook_id;
	struct ast_datastore *datastore = NULL;
	static struct ast_framehook_interface hook = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = t38_framehook,
		.chan_fixup_cb = t38_masq,
		.chan_breakdown_cb = t38_masq,
	};

	/* If the channel's already gone, bail */
	if (!session->channel) {
		return;
	}

	/* Only attach the framehook if t38 is enabled for the endpoint */
	if (!session->endpoint->media.t38.enabled) {
		return;
	}

	/* Skip attaching the framehook if the T.38 datastore already exists for the channel */
	ast_channel_lock(session->channel);
	if ((datastore = ast_channel_datastore_find(session->channel, &t38_framehook_datastore, NULL))) {
		ast_channel_unlock(session->channel);
		return;
	}
	ast_channel_unlock(session->channel);

	framehook_id = ast_framehook_attach(session->channel, &hook);
	if (framehook_id < 0) {
		ast_log(LOG_WARNING, "Could not attach T.38 Frame hook to channel, T.38 will be unavailable on '%s'\n",
			ast_channel_name(session->channel));
		return;
	}

	ast_channel_lock(session->channel);
	datastore = ast_datastore_alloc(&t38_framehook_datastore, NULL);
	if (!datastore) {
		ast_log(LOG_ERROR, "Could not attach T.38 Frame hook to channel, T.38 will be unavailable on '%s'\n",
			ast_channel_name(session->channel));
		ast_framehook_detach(session->channel, framehook_id);
		ast_channel_unlock(session->channel);
		return;
	}

	ast_channel_datastore_add(session->channel, datastore);
	ast_channel_unlock(session->channel);
}

/*! \brief Function called when an INVITE arrives */
static int t38_incoming_invite_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	t38_attach_framehook(session);
	return 0;
}

/*! \brief Function called when an INVITE is sent */
static void t38_outgoing_invite_request(struct ast_sip_session *session, struct pjsip_tx_data *tdata)
{
	t38_attach_framehook(session);
}

/*! \brief Get Max T.38 Transmission rate from T38 capabilities */
static unsigned int t38_get_rate(enum ast_control_t38_rate rate)
{
	switch (rate) {
	case AST_T38_RATE_2400:
		return 2400;
	case AST_T38_RATE_4800:
		return 4800;
	case AST_T38_RATE_7200:
		return 7200;
	case AST_T38_RATE_9600:
		return 9600;
	case AST_T38_RATE_12000:
		return 12000;
	case AST_T38_RATE_14400:
		return 14400;
	default:
		return 0;
	}
}

/*! \brief Supplement for adding framehook to session channel */
static struct ast_sip_session_supplement t38_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL + 1,
	.incoming_request = t38_incoming_invite_request,
	.outgoing_request = t38_outgoing_invite_request,
};

/*! \brief Parse a T.38 image stream and store the attribute information */
static void t38_interpret_sdp(struct t38_state *state, struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	const struct pjmedia_sdp_media *stream)
{
	unsigned int attr_i;

	for (attr_i = 0; attr_i < stream->attr_count; attr_i++) {
		pjmedia_sdp_attr *attr = stream->attr[attr_i];

		if (!pj_stricmp2(&attr->name, "t38faxmaxbuffer")) {
			/* This is purposely left empty, it is unused */
		} else if (!pj_stricmp2(&attr->name, "t38maxbitrate") || !pj_stricmp2(&attr->name, "t38faxmaxrate")) {
			switch (pj_strtoul(&attr->value)) {
			case 14400:
				state->their_parms.rate = AST_T38_RATE_14400;
				break;
			case 12000:
				state->their_parms.rate = AST_T38_RATE_12000;
				break;
			case 9600:
				state->their_parms.rate = AST_T38_RATE_9600;
				break;
			case 7200:
				state->their_parms.rate = AST_T38_RATE_7200;
				break;
			case 4800:
				state->their_parms.rate = AST_T38_RATE_4800;
				break;
			case 2400:
				state->their_parms.rate = AST_T38_RATE_2400;
				break;
			}
		} else if (!pj_stricmp2(&attr->name, "t38faxversion")) {
			state->their_parms.version = pj_strtoul(&attr->value);
		} else if (!pj_stricmp2(&attr->name, "t38faxmaxdatagram") || !pj_stricmp2(&attr->name, "t38maxdatagram")) {
			if (!session->endpoint->media.t38.maxdatagram) {
				ast_udptl_set_far_max_datagram(session_media->udptl, pj_strtoul(&attr->value));
			}
		} else if (!pj_stricmp2(&attr->name, "t38faxfillbitremoval")) {
			state->their_parms.fill_bit_removal = 1;
		} else if (!pj_stricmp2(&attr->name, "t38faxtranscodingmmr")) {
			state->their_parms.transcoding_mmr = 1;
		} else if (!pj_stricmp2(&attr->name, "t38faxtranscodingjbig")) {
			state->their_parms.transcoding_jbig = 1;
		} else if (!pj_stricmp2(&attr->name, "t38faxratemanagement")) {
			if (!pj_stricmp2(&attr->value, "localTCF")) {
				state->their_parms.rate_management = AST_T38_RATE_MANAGEMENT_LOCAL_TCF;
			} else if (!pj_stricmp2(&attr->value, "transferredTCF")) {
				state->their_parms.rate_management = AST_T38_RATE_MANAGEMENT_TRANSFERRED_TCF;
			}
		} else if (!pj_stricmp2(&attr->name, "t38faxudpec")) {
			if (!pj_stricmp2(&attr->value, "t38UDPRedundancy")) {
				ast_udptl_set_error_correction_scheme(session_media->udptl, UDPTL_ERROR_CORRECTION_REDUNDANCY);
			} else if (!pj_stricmp2(&attr->value, "t38UDPFEC")) {
				ast_udptl_set_error_correction_scheme(session_media->udptl, UDPTL_ERROR_CORRECTION_FEC);
			} else {
				ast_udptl_set_error_correction_scheme(session_media->udptl, UDPTL_ERROR_CORRECTION_NONE);
			}
		}

	}
}

/*! \brief Function which defers an incoming media stream */
static enum ast_sip_session_sdp_stream_defer defer_incoming_sdp_stream(
	struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	const struct pjmedia_sdp_session *sdp, const struct pjmedia_sdp_media *stream)
{
	struct t38_state *state;

	if (!session->endpoint->media.t38.enabled) {
		return AST_SIP_SESSION_SDP_DEFER_NOT_HANDLED;
	}

	if (t38_initialize_session(session, session_media)) {
		return AST_SIP_SESSION_SDP_DEFER_ERROR;
	}

	if (!(state = t38_state_get_or_alloc(session))) {
		return AST_SIP_SESSION_SDP_DEFER_ERROR;
	}

	t38_interpret_sdp(state, session, session_media, stream);

	/* If they are initiating the re-invite we need to defer responding until later */
	if (session->t38state == T38_DISABLED) {
		t38_change_state(session, session_media, state, T38_PEER_REINVITE);
		return AST_SIP_SESSION_SDP_DEFER_NEEDED;
	}

	return AST_SIP_SESSION_SDP_DEFER_NOT_NEEDED;
}

/*! \brief Function which negotiates an incoming media stream */
static int negotiate_incoming_sdp_stream(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
					 const struct pjmedia_sdp_session *sdp, const struct pjmedia_sdp_media *stream)
{
	struct t38_state *state;
	char host[NI_MAXHOST];
	RAII_VAR(struct ast_sockaddr *, addrs, NULL, ast_free);

	if (!session->endpoint->media.t38.enabled) {
		return -1;
	}

	if (!(state = t38_state_get_or_alloc(session))) {
		return -1;
	}

	if ((session->t38state == T38_REJECTED) || (session->t38state == T38_DISABLED)) {
		t38_change_state(session, session_media, state, T38_DISABLED);
		return -1;
	}

	ast_copy_pj_str(host, stream->conn ? &stream->conn->addr : &sdp->conn->addr, sizeof(host));

	/* Ensure that the address provided is valid */
	if (ast_sockaddr_resolve(&addrs, host, PARSE_PORT_FORBID, AST_AF_INET) <= 0) {
		/* The provided host was actually invalid so we error out this negotiation */
		return -1;
	}

	/* Check the address family to make sure it matches configured */
	if ((ast_sockaddr_is_ipv6(addrs) && !session->endpoint->media.t38.ipv6) ||
		(ast_sockaddr_is_ipv4(addrs) && session->endpoint->media.t38.ipv6)) {
		/* The address does not match configured */
		return -1;
	}

	return 1;
}

/*! \brief Function which creates an outgoing stream */
static int create_outgoing_sdp_stream(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				      struct pjmedia_sdp_session *sdp)
{
	pj_pool_t *pool = session->inv_session->pool_prov;
	static const pj_str_t STR_IN = { "IN", 2 };
	static const pj_str_t STR_IP4 = { "IP4", 3};
	static const pj_str_t STR_IP6 = { "IP6", 3};
	static const pj_str_t STR_UDPTL = { "udptl", 5 };
	static const pj_str_t STR_T38 = { "t38", 3 };
	static const pj_str_t STR_TRANSFERREDTCF = { "transferredTCF", 14 };
	static const pj_str_t STR_LOCALTCF = { "localTCF", 8 };
	static const pj_str_t STR_T38UDPFEC = { "t38UDPFEC", 9 };
	static const pj_str_t STR_T38UDPREDUNDANCY = { "t38UDPRedundancy", 16 };
	struct t38_state *state;
	pjmedia_sdp_media *media;
	char hostip[PJ_INET6_ADDRSTRLEN+2];
	struct ast_sockaddr addr;
	char tmp[512];
	pj_str_t stmp;

	if (!session->endpoint->media.t38.enabled) {
		return 1;
	} else if ((session->t38state != T38_LOCAL_REINVITE) && (session->t38state != T38_PEER_REINVITE) &&
		(session->t38state != T38_ENABLED)) {
		return 1;
	} else if (!(state = t38_state_get_or_alloc(session))) {
		return -1;
	} else if (t38_initialize_session(session, session_media)) {
		return -1;
	}

	if (!(media = pj_pool_zalloc(pool, sizeof(struct pjmedia_sdp_media))) ||
		!(media->conn = pj_pool_zalloc(pool, sizeof(struct pjmedia_sdp_conn)))) {
		return -1;
	}

	media->desc.media = pj_str(session_media->stream_type);
	media->desc.transport = STR_UDPTL;

	if (ast_strlen_zero(session->endpoint->media.address)) {
		pj_sockaddr localaddr;

		if (pj_gethostip(session->endpoint->media.t38.ipv6 ? pj_AF_INET6() : pj_AF_INET(), &localaddr)) {
			return -1;
		}
		pj_sockaddr_print(&localaddr, hostip, sizeof(hostip), 2);
	} else {
		ast_copy_string(hostip, session->endpoint->media.address, sizeof(hostip));
	}

	media->conn->net_type = STR_IN;
	media->conn->addr_type = session->endpoint->media.t38.ipv6 ? STR_IP6 : STR_IP4;
	pj_strdup2(pool, &media->conn->addr, hostip);
	ast_udptl_get_us(session_media->udptl, &addr);
	media->desc.port = (pj_uint16_t) ast_sockaddr_port(&addr);
	media->desc.port_count = 1;
	media->desc.fmt[media->desc.fmt_count++] = STR_T38;

	snprintf(tmp, sizeof(tmp), "%u", state->our_parms.version);
	media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxVersion", pj_cstr(&stmp, tmp));

	snprintf(tmp, sizeof(tmp), "%u", t38_get_rate(state->our_parms.rate));
	media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38MaxBitRate", pj_cstr(&stmp, tmp));

	if (state->our_parms.fill_bit_removal) {
		media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxFillBitRemoval", NULL);
	}

	if (state->our_parms.transcoding_mmr) {
		media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxTranscodingMMR", NULL);
	}

	if (state->our_parms.transcoding_jbig) {
		media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxTranscodingJBIG", NULL);
	}

	switch (state->our_parms.rate_management) {
	case AST_T38_RATE_MANAGEMENT_TRANSFERRED_TCF:
		media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxRateManagement", &STR_TRANSFERREDTCF);
		break;
	case AST_T38_RATE_MANAGEMENT_LOCAL_TCF:
		media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxRateManagement", &STR_LOCALTCF);
		break;
	}

	snprintf(tmp, sizeof(tmp), "%u", ast_udptl_get_local_max_datagram(session_media->udptl));
	media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxMaxDatagram", pj_cstr(&stmp, tmp));

	switch (ast_udptl_get_error_correction_scheme(session_media->udptl)) {
	case UDPTL_ERROR_CORRECTION_NONE:
		break;
	case UDPTL_ERROR_CORRECTION_FEC:
		media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxUdpEC", &STR_T38UDPFEC);
		break;
	case UDPTL_ERROR_CORRECTION_REDUNDANCY:
		media->attr[media->attr_count++] = pjmedia_sdp_attr_create(pool, "T38FaxUdpEC", &STR_T38UDPREDUNDANCY);
		break;
	}

	sdp->media[sdp->media_count++] = media;

	return 1;
}

/*! \brief Function which applies a negotiated stream */
static int apply_negotiated_sdp_stream(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				       const struct pjmedia_sdp_session *local, const struct pjmedia_sdp_media *local_stream,
				       const struct pjmedia_sdp_session *remote, const struct pjmedia_sdp_media *remote_stream)
{
	RAII_VAR(struct ast_sockaddr *, addrs, NULL, ast_free);
	char host[NI_MAXHOST];
	struct t38_state *state;

	if (!session_media->udptl) {
		return 0;
	}

	if (!(state = t38_state_get_or_alloc(session))) {
		return -1;
	}

	ast_copy_pj_str(host, remote_stream->conn ? &remote_stream->conn->addr : &remote->conn->addr, sizeof(host));

	/* Ensure that the address provided is valid */
	if (ast_sockaddr_resolve(&addrs, host, PARSE_PORT_FORBID, AST_AF_UNSPEC) <= 0) {
		/* The provided host was actually invalid so we error out this negotiation */
		return -1;
	}

	ast_sockaddr_set_port(addrs, remote_stream->desc.port);
	ast_udptl_set_peer(session_media->udptl, addrs);

	t38_interpret_sdp(state, session, session_media, remote_stream);

	return 0;
}

/*! \brief Function which updates the media stream with external media address, if applicable */
static void change_outgoing_sdp_stream_media_address(pjsip_tx_data *tdata, struct pjmedia_sdp_media *stream, struct ast_sip_transport *transport)
{
	char host[NI_MAXHOST];
	struct ast_sockaddr addr = { { 0, } };

	/* If the stream has been rejected there will be no connection line */
	if (!stream->conn) {
		return;
	}

	ast_copy_pj_str(host, &stream->conn->addr, sizeof(host));
	ast_sockaddr_parse(&addr, host, PARSE_PORT_FORBID);

	/* Is the address within the SDP inside the same network? */
	if (ast_apply_ha(transport->localnet, &addr) == AST_SENSE_ALLOW) {
		return;
	}

	pj_strdup2(tdata->pool, &stream->conn->addr, transport->external_media_address);
}

/*! \brief Function which destroys the UDPTL instance when session ends */
static void stream_destroy(struct ast_sip_session_media *session_media)
{
	if (session_media->udptl) {
		ast_udptl_destroy(session_media->udptl);
	}
	session_media->udptl = NULL;
}

/*! \brief SDP handler for 'image' media stream */
static struct ast_sip_session_sdp_handler image_sdp_handler = {
	.id = "image",
	.defer_incoming_sdp_stream = defer_incoming_sdp_stream,
	.negotiate_incoming_sdp_stream = negotiate_incoming_sdp_stream,
	.create_outgoing_sdp_stream = create_outgoing_sdp_stream,
	.apply_negotiated_sdp_stream = apply_negotiated_sdp_stream,
	.change_outgoing_sdp_stream_media_address = change_outgoing_sdp_stream_media_address,
	.stream_destroy = stream_destroy,
};

/*! \brief Unloads the SIP T.38 module from Asterisk */
static int unload_module(void)
{
	ast_sip_session_unregister_sdp_handler(&image_sdp_handler, "image");
	ast_sip_session_unregister_supplement(&t38_supplement);

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
	CHECK_PJSIP_SESSION_MODULE_LOADED();

	ast_sockaddr_parse(&address_ipv4, "0.0.0.0", 0);
	ast_sockaddr_parse(&address_ipv6, "::", 0);

	if (ast_sip_session_register_supplement(&t38_supplement)) {
		ast_log(LOG_ERROR, "Unable to register T.38 session supplement\n");
		goto end;
	}

	if (ast_sip_session_register_sdp_handler(&image_sdp_handler, "image")) {
		ast_log(LOG_ERROR, "Unable to register SDP handler for image stream type\n");
		goto end;
	}

	return AST_MODULE_LOAD_SUCCESS;
end:
	unload_module();

	return AST_MODULE_LOAD_FAILURE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP T.38 UDPTL Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
);
