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

#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/udptl.h"
#include "asterisk/netsock2.h"
#include "asterisk/channel.h"
#include "asterisk/acl.h"
#include "asterisk/stream.h"
#include "asterisk/format_cache.h"

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"

/*! \brief The number of seconds after receiving a T.38 re-invite before automatically rejecting it */
#define T38_AUTOMATIC_REJECTION_SECONDS 5

/*! \brief Address for UDPTL */
static struct ast_sockaddr address;

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
	/*! Preserved media state for when T.38 ends */
	struct ast_sip_session_media_state *media_state;
};

/*! \brief Destructor for T.38 state information */
static void t38_state_destroy(void *obj)
{
	struct t38_state *state = obj;

	ast_sip_session_media_state_free(state->media_state);
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

	if (pj_timer_heap_cancel_if_active(pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()),
		&state->timer, 0)) {
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

		/* Inform the bridge the channel is in that it needs to be reconfigured */
		ast_channel_set_unbridged(session->channel, 1);
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
		/* Inform the bridge the channel is in that it needs to be reconfigured */
		ast_channel_set_unbridged(session->channel, 1);
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
	struct ast_sip_session_media *session_media;

	if (!datastore) {
		return 0;
	}

	ast_debug(2, "Automatically rejecting T.38 request on channel '%s'\n",
		session->channel ? ast_channel_name(session->channel) : "<gone>");

	session_media = session->pending_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
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
	pj_timer_entry_init(&state->timer, 0, session, t38_automatic_reject_timer_cb);

	return state;
}

/*! \brief Initializes UDPTL support on a session, only done when actually needed */
static int t38_initialize_session(struct ast_sip_session *session, struct ast_sip_session_media *session_media)
{
	if (session_media->udptl) {
		return 0;
	}

	if (!(session_media->udptl = ast_udptl_new_with_bindaddr(NULL, NULL, 0, &address))) {
		return -1;
	}

	ast_udptl_set_error_correction_scheme(session_media->udptl, session->endpoint->media.t38.error_correction);
	ast_udptl_setnat(session_media->udptl, session->endpoint->media.t38.nat);
	ast_udptl_set_far_max_datagram(session_media->udptl, session->endpoint->media.t38.maxdatagram);
	ast_debug(3, "UDPTL initialized on session for %s\n", ast_channel_name(session->channel));

	return 0;
}

/*! \brief Callback for when T.38 reinvite SDP is created */
static int t38_reinvite_sdp_cb(struct ast_sip_session *session, pjmedia_sdp_session *sdp)
{
	struct t38_state *state;

	state = t38_state_get_or_alloc(session);
	if (!state) {
		return -1;
	}

	state->media_state = ast_sip_session_media_state_clone(session->active_media_state);

	return 0;
}

/*! \brief Callback for when a response is received for a T.38 re-invite */
static int t38_reinvite_response_cb(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	struct pjsip_status_line status = rdata->msg_info.msg->line.status;
	struct t38_state *state;
	struct ast_sip_session_media *session_media = NULL;

	if (status.code / 100 <= 1) {
		/* Ignore any non-final responses (1xx) */
		return 0;
	}

	if (session->t38state != T38_LOCAL_REINVITE) {
		/* Do nothing.  We have already processed a final response. */
		ast_debug(3, "Received %d response to T.38 re-invite on '%s' but already had a final response (T.38 state:%d)\n",
			status.code,
			session->channel ? ast_channel_name(session->channel) : "unknown channel",
			session->t38state);
		return 0;
	}

	state = t38_state_get_or_alloc(session);
	if (!session->channel || !state) {
		ast_log(LOG_WARNING, "Received %d response to T.38 re-invite on '%s' but state unavailable\n",
			status.code,
			session->channel ? ast_channel_name(session->channel) : "unknown channel");
		return 0;
	}

	if (status.code / 100 == 2) {
		/* Accept any 2xx response as successfully negotiated */
		int index;

		session_media = session->active_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
		t38_change_state(session, session_media, state, T38_ENABLED);

		/* Stop all the streams in the stored away active state, they'll go back to being active once
		 * we reinvite back.
		 */
		for (index = 0; index < AST_VECTOR_SIZE(&state->media_state->sessions); ++index) {
			struct ast_sip_session_media *session_media = AST_VECTOR_GET(&state->media_state->sessions, index);

			if (session_media && session_media->handler && session_media->handler->stream_stop) {
				session_media->handler->stream_stop(session_media);
			}
		}
	} else {
		session_media = session->pending_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
		t38_change_state(session, session_media, state, T38_REJECTED);

		/* Abort this attempt at switching to T.38 by resetting the pending state and freeing our stored away active state */
		ast_sip_session_media_state_free(state->media_state);
		state->media_state = NULL;
		ast_sip_session_media_state_reset(session->pending_media_state);
	}

	return 0;
}

/*! \brief Helper function which creates a media state for strictly T.38 */
static struct ast_sip_session_media_state *t38_create_media_state(struct ast_sip_session *session)
{
	struct ast_sip_session_media_state *media_state;
	struct ast_stream *stream;
	struct ast_format_cap *caps;
	struct ast_sip_session_media *session_media;

	media_state = ast_sip_session_media_state_alloc();
	if (!media_state) {
		return NULL;
	}

	media_state->topology = ast_stream_topology_alloc();
	if (!media_state->topology) {
		ast_sip_session_media_state_free(media_state);
		return NULL;
	}

	stream = ast_stream_alloc("t38", AST_MEDIA_TYPE_IMAGE);
	if (!stream) {
		ast_sip_session_media_state_free(media_state);
		return NULL;
	}

	ast_stream_set_state(stream, AST_STREAM_STATE_SENDRECV);
	if (ast_stream_topology_set_stream(media_state->topology, 0, stream)) {
		ast_stream_free(stream);
		ast_sip_session_media_state_free(media_state);
		return NULL;
	}

	caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!caps) {
		ast_sip_session_media_state_free(media_state);
		return NULL;
	}

	ast_stream_set_formats(stream, caps);
	/* stream holds a reference to cap, release the local reference
	 * now so we don't have to deal with it in the error condition. */
	ao2_ref(caps, -1);
	if (ast_format_cap_append(caps, ast_format_t38, 0)) {
		ast_sip_session_media_state_free(media_state);
		return NULL;
	}

	session_media = ast_sip_session_media_state_add(session, media_state, AST_MEDIA_TYPE_IMAGE, 0);
	if (!session_media) {
		ast_sip_session_media_state_free(media_state);
		return NULL;
	}

	if (t38_initialize_session(session, session_media)) {
		ast_sip_session_media_state_free(media_state);
		return NULL;
	}

	return media_state;
}

/*! \brief Task for reacting to T.38 control frame */
static int t38_interpret_parameters(void *obj)
{
	RAII_VAR(struct t38_parameters_task_data *, data, obj, ao2_cleanup);
	const struct ast_control_t38_parameters *parameters = data->frame->data.ptr;
	struct t38_state *state = t38_state_get_or_alloc(data->session);
	struct ast_sip_session_media *session_media = NULL;

	if (!state) {
		return 0;
	}

	switch (parameters->request_response) {
	case AST_T38_NEGOTIATED:
	case AST_T38_REQUEST_NEGOTIATE:         /* Request T38 */
		/* Negotiation can not take place without a valid max_ifp value. */
		if (!parameters->max_ifp) {
			if (data->session->t38state == T38_PEER_REINVITE) {
				session_media = data->session->pending_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
				t38_change_state(data->session, session_media, state, T38_REJECTED);
				ast_sip_session_resume_reinvite(data->session);
			} else if (data->session->t38state == T38_ENABLED) {
				session_media = data->session->active_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
				t38_change_state(data->session, session_media, state, T38_DISABLED);
				ast_sip_session_refresh(data->session, NULL, NULL, NULL,
					AST_SIP_SESSION_REFRESH_METHOD_INVITE, 1, state->media_state);
				state->media_state = NULL;
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
			session_media = data->session->pending_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
			ast_udptl_set_local_max_ifp(session_media->udptl, state->our_parms.max_ifp);
			t38_change_state(data->session, session_media, state, T38_ENABLED);
			ast_sip_session_resume_reinvite(data->session);
		} else if ((data->session->t38state != T38_ENABLED) ||
				((data->session->t38state == T38_ENABLED) &&
                                (parameters->request_response == AST_T38_REQUEST_NEGOTIATE))) {
			struct ast_sip_session_media_state *media_state;

			media_state = t38_create_media_state(data->session);
			if (!media_state) {
				break;
			}
			state->our_parms = *parameters;
			session_media = media_state->default_session[AST_MEDIA_TYPE_IMAGE];
			ast_udptl_set_local_max_ifp(session_media->udptl, state->our_parms.max_ifp);
			t38_change_state(data->session, session_media, state, T38_LOCAL_REINVITE);
			ast_sip_session_refresh(data->session, NULL, t38_reinvite_sdp_cb, t38_reinvite_response_cb,
				AST_SIP_SESSION_REFRESH_METHOD_INVITE, 1, media_state);
		}
		break;
	case AST_T38_TERMINATED:
	case AST_T38_REFUSED:
	case AST_T38_REQUEST_TERMINATE:         /* Shutdown T38 */
		if (data->session->t38state == T38_PEER_REINVITE) {
			session_media = data->session->pending_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
			t38_change_state(data->session, session_media, state, T38_REJECTED);
			ast_sip_session_resume_reinvite(data->session);
		} else if (data->session->t38state == T38_ENABLED) {
			session_media = data->session->active_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
			t38_change_state(data->session, session_media, state, T38_DISABLED);
			ast_sip_session_refresh(data->session, NULL, NULL, NULL, AST_SIP_SESSION_REFRESH_METHOD_INVITE, 1, state->media_state);
			state->media_state = NULL;
		}
		break;
	case AST_T38_REQUEST_PARMS: {		/* Application wants remote's parameters re-sent */
		struct ast_control_t38_parameters parameters = state->their_parms;

		if (data->session->t38state == T38_PEER_REINVITE) {
			session_media = data->session->pending_media_state->default_session[AST_MEDIA_TYPE_IMAGE];
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

/*! \brief Frame hook callback for T.38 related stuff */
static struct ast_frame *t38_framehook(struct ast_channel *chan, struct ast_frame *f,
	enum ast_framehook_event event, void *data)
{
	struct ast_sip_channel_pvt *channel = ast_channel_tech_pvt(chan);

	if (event != AST_FRAMEHOOK_EVENT_WRITE) {
		return f;
	}

	if (f->frametype == AST_FRAME_CONTROL
		&& f->subclass.integer == AST_CONTROL_T38_PARAMETERS) {
		if (channel->session->endpoint->media.t38.enabled) {
			struct t38_parameters_task_data *data;

			data = t38_parameters_task_data_alloc(channel->session, f);
			if (data
				&& ast_sip_push_task(channel->session->serializer,
					t38_interpret_parameters, data)) {
				ao2_ref(data, -1);
			}
		} else {
			static const struct ast_control_t38_parameters rsp_refused = {
				.request_response = AST_T38_REFUSED,
			};
			static const struct ast_control_t38_parameters rsp_terminated = {
				.request_response = AST_T38_TERMINATED,
			};
			const struct ast_control_t38_parameters *parameters = f->data.ptr;

			switch (parameters->request_response) {
			case AST_T38_REQUEST_NEGOTIATE:
				ast_debug(2, "T.38 support not enabled on %s, refusing T.38 negotiation\n",
					ast_channel_name(chan));
				ast_queue_control_data(chan, AST_CONTROL_T38_PARAMETERS,
					&rsp_refused, sizeof(rsp_refused));
				break;
			case AST_T38_REQUEST_TERMINATE:
				ast_debug(2, "T.38 support not enabled on %s, 'terminating' T.38 session\n",
					ast_channel_name(chan));
				ast_queue_control_data(chan, AST_CONTROL_T38_PARAMETERS,
					&rsp_terminated, sizeof(rsp_terminated));
				break;
			default:
				break;
			}
		}
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

static int t38_consume(void *data, enum ast_frame_type type)
{
	return (type == AST_FRAME_CONTROL) ? 1 : 0;
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
		.consume_cb = t38_consume,
		.chan_fixup_cb = t38_masq,
		.chan_breakdown_cb = t38_masq,
	};

	/* If the channel's already gone, bail */
	if (!session->channel) {
		return;
	}

	/* Always attach the framehook so we can quickly reject */

	ast_channel_lock(session->channel);

	/* Skip attaching the framehook if the T.38 datastore already exists for the channel */
	datastore = ast_channel_datastore_find(session->channel, &t38_framehook_datastore,
		NULL);
	if (datastore) {
		ast_channel_unlock(session->channel);
		return;
	}

	framehook_id = ast_framehook_attach(session->channel, &hook);
	if (framehook_id < 0) {
		ast_log(LOG_WARNING, "Could not attach T.38 Frame hook, T.38 will be unavailable on '%s'\n",
			ast_channel_name(session->channel));
		ast_channel_unlock(session->channel);
		return;
	}

	datastore = ast_datastore_alloc(&t38_framehook_datastore, NULL);
	if (!datastore) {
		ast_log(LOG_ERROR, "Could not alloc T.38 Frame hook datastore, T.38 will be unavailable on '%s'\n",
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
		ast_debug(3, "Not deferring incoming SDP stream: T.38 not enabled on %s\n", ast_channel_name(session->channel));
		return AST_SIP_SESSION_SDP_DEFER_NOT_HANDLED;
	}

	if (t38_initialize_session(session, session_media)) {
		ast_debug(3, "Not deferring incoming SDP stream: Failed to initialize UDPTL on %s\n", ast_channel_name(session->channel));
		return AST_SIP_SESSION_SDP_DEFER_ERROR;
	}

	if (!(state = t38_state_get_or_alloc(session))) {
		return AST_SIP_SESSION_SDP_DEFER_ERROR;
	}

	t38_interpret_sdp(state, session, session_media, stream);

	/* If they are initiating the re-invite we need to defer responding until later */
	if (session->t38state == T38_DISABLED) {
		t38_change_state(session, session_media, state, T38_PEER_REINVITE);
		ast_debug(3, "Deferring incoming SDP stream on %s for peer re-invite\n", ast_channel_name(session->channel));
		return AST_SIP_SESSION_SDP_DEFER_NEEDED;
	}

	return AST_SIP_SESSION_SDP_DEFER_NOT_NEEDED;
}

/*! \brief Function which negotiates an incoming media stream */
static int negotiate_incoming_sdp_stream(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media, const struct pjmedia_sdp_session *sdp,
	int index, struct ast_stream *asterisk_stream)
{
	struct t38_state *state;
	char host[NI_MAXHOST];
	pjmedia_sdp_media *stream = sdp->media[index];
	RAII_VAR(struct ast_sockaddr *, addrs, NULL, ast_free);

	if (!session->endpoint->media.t38.enabled) {
		ast_debug(3, "Declining; T.38 not enabled on session\n");
		return 0;
	}

	if (!(state = t38_state_get_or_alloc(session))) {
		return 0;
	}

	if ((session->t38state == T38_REJECTED) || (session->t38state == T38_DISABLED)) {
		ast_debug(3, "Declining; T.38 state is rejected or declined\n");
		t38_change_state(session, session_media, state, T38_DISABLED);
		return 0;
	}

	ast_copy_pj_str(host, stream->conn ? &stream->conn->addr : &sdp->conn->addr, sizeof(host));

	/* Ensure that the address provided is valid */
	if (ast_sockaddr_resolve(&addrs, host, PARSE_PORT_FORBID, AST_AF_INET) <= 0) {
		/* The provided host was actually invalid so we error out this negotiation */
		ast_debug(3, "Declining; provided host is invalid\n");
		return 0;
	}

	/* Check the address family to make sure it matches configured */
	if ((ast_sockaddr_is_ipv6(addrs) && !session->endpoint->media.t38.ipv6) ||
		(ast_sockaddr_is_ipv4(addrs) && session->endpoint->media.t38.ipv6)) {
		/* The address does not match configured */
		ast_debug(3, "Declining, provided host does not match configured address family\n");
		return 0;
	}

	return 1;
}

/*! \brief Function which creates an outgoing stream */
static int create_outgoing_sdp_stream(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
				      struct pjmedia_sdp_session *sdp, const struct pjmedia_sdp_session *remote, struct ast_stream *stream)
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
	const char *hostip = NULL;
	struct ast_sockaddr addr;
	char tmp[512];
	pj_str_t stmp;

	if (!session->endpoint->media.t38.enabled) {
		ast_debug(3, "Not creating outgoing SDP stream: T.38 not enabled\n");
		return 1;
	} else if ((session->t38state != T38_LOCAL_REINVITE) && (session->t38state != T38_PEER_REINVITE) &&
		(session->t38state != T38_ENABLED)) {
		ast_debug(3, "Not creating outgoing SDP stream: T.38 not enabled\n");
		return 1;
	} else if (!(state = t38_state_get_or_alloc(session))) {
		return -1;
	} else if (t38_initialize_session(session, session_media)) {
		ast_debug(3, "Not creating outgoing SDP stream: Failed to initialize T.38 session\n");
		return -1;
	}

	if (!(media = pj_pool_zalloc(pool, sizeof(struct pjmedia_sdp_media))) ||
		!(media->conn = pj_pool_zalloc(pool, sizeof(struct pjmedia_sdp_conn)))) {
		return -1;
	}

	pj_strdup2(pool, &media->desc.media, ast_codec_media_type2str(session_media->type));
	media->desc.transport = STR_UDPTL;

	if (ast_strlen_zero(session->endpoint->media.address)) {
		hostip = ast_sip_get_host_ip_string(session->endpoint->media.t38.ipv6 ? pj_AF_INET6() : pj_AF_INET());
	} else {
		hostip = session->endpoint->media.address;
	}

	if (ast_strlen_zero(hostip)) {
		ast_debug(3, "Not creating outgoing SDP stream: no known host IP\n");
		return -1;
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

static struct ast_frame *media_session_udptl_read_callback(struct ast_sip_session *session, struct ast_sip_session_media *session_media)
{
	struct ast_frame *frame;

	if (!session_media->udptl) {
		return &ast_null_frame;
	}

	frame = ast_udptl_read(session_media->udptl);
	if (!frame) {
		return NULL;
	}

	frame->stream_num = session_media->stream_num;

	return frame;
}

static int media_session_udptl_write_callback(struct ast_sip_session *session, struct ast_sip_session_media *session_media, struct ast_frame *frame)
{
	if (!session_media->udptl) {
		return 0;
	}

	return ast_udptl_write(session_media->udptl, frame);
}

/*! \brief Function which applies a negotiated stream */
static int apply_negotiated_sdp_stream(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media, const struct pjmedia_sdp_session *local,
	const struct pjmedia_sdp_session *remote, int index, struct ast_stream *asterisk_stream)
{
	RAII_VAR(struct ast_sockaddr *, addrs, NULL, ast_free);
	pjmedia_sdp_media *remote_stream = remote->media[index];
	char host[NI_MAXHOST];
	struct t38_state *state;

	if (!session_media->udptl) {
		ast_debug(3, "Not applying negotiated SDP stream: no UDTPL session\n");
		return 0;
	}

	if (!(state = t38_state_get_or_alloc(session))) {
		return -1;
	}

	ast_copy_pj_str(host, remote_stream->conn ? &remote_stream->conn->addr : &remote->conn->addr, sizeof(host));

	/* Ensure that the address provided is valid */
	if (ast_sockaddr_resolve(&addrs, host, PARSE_PORT_FORBID, AST_AF_UNSPEC) <= 0) {
		/* The provided host was actually invalid so we error out this negotiation */
		ast_debug(3, "Not applying negotiated SDP stream: failed to resolve remote stream host\n");
		return -1;
	}

	ast_sockaddr_set_port(addrs, remote_stream->desc.port);
	ast_udptl_set_peer(session_media->udptl, addrs);

	t38_interpret_sdp(state, session, session_media, remote_stream);

	ast_sip_session_media_set_write_callback(session, session_media, media_session_udptl_write_callback);
	ast_sip_session_media_add_read_callback(session, session_media, ast_udptl_fd(session_media->udptl),
		media_session_udptl_read_callback);

	return 0;
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
	ast_debug(5, "Setting media address to %s\n", ast_sockaddr_stringify_host(&transport_state->external_media_address));
	pj_strdup2(tdata->pool, &stream->conn->addr, ast_sockaddr_stringify_host(&transport_state->external_media_address));
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
	if (ast_check_ipv6()) {
		ast_sockaddr_parse(&address, "::", 0);
	} else {
		ast_sockaddr_parse(&address, "0.0.0.0", 0);
	}

	ast_sip_session_register_supplement(&t38_supplement);

	if (ast_sip_session_register_sdp_handler(&image_sdp_handler, "image")) {
		ast_log(LOG_ERROR, "Unable to register SDP handler for image stream type\n");
		goto end;
	}

	return AST_MODULE_LOAD_SUCCESS;
end:
	unload_module();

	return AST_MODULE_LOAD_DECLINE;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP T.38 UDPTL Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	.requires = "res_pjsip,res_pjsip_session,udptl",
);
