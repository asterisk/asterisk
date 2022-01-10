/*
* Asterisk -- An open source telephony toolkit.
*
* Copyright (C) 2013, Digium, Inc.
*
* Mark Michelson <mmichelson@digium.com>
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>
#include <pjmedia.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/res_pjsip_session_caps.h"
#include "asterisk/callerid.h"
#include "asterisk/datastore.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/astobj2.h"
#include "asterisk/lock.h"
#include "asterisk/uuid.h"
#include "asterisk/pbx.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/causes.h"
#include "asterisk/sdp_srtp.h"
#include "asterisk/dsp.h"
#include "asterisk/acl.h"
#include "asterisk/features_config.h"
#include "asterisk/pickup.h"
#include "asterisk/test.h"
#include "asterisk/stream.h"
#include "asterisk/vector.h"

#define SDP_HANDLER_BUCKETS 11

#define MOD_DATA_ON_RESPONSE "on_response"
#define MOD_DATA_NAT_HOOK "nat_hook"

/* Most common case is one audio and one video stream */
#define DEFAULT_NUM_SESSION_MEDIA 2

/* Some forward declarations */
static void handle_session_begin(struct ast_sip_session *session);
static void handle_session_end(struct ast_sip_session *session);
static void handle_session_destroy(struct ast_sip_session *session);
static void handle_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata);
static void handle_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata,
		enum ast_sip_session_response_priority response_priority);
static int handle_incoming(struct ast_sip_session *session, pjsip_rx_data *rdata,
		enum ast_sip_session_response_priority response_priority);
static void handle_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata);
static void handle_outgoing_response(struct ast_sip_session *session, pjsip_tx_data *tdata);
static int sip_session_refresh(struct ast_sip_session *session,
		ast_sip_session_request_creation_cb on_request_creation,
		ast_sip_session_sdp_creation_cb on_sdp_creation,
		ast_sip_session_response_cb on_response,
		enum ast_sip_session_refresh_method method, int generate_new_sdp,
		struct ast_sip_session_media_state *pending_media_state,
		struct ast_sip_session_media_state *active_media_state,
		int queued);

/*! \brief NAT hook for modifying outgoing messages with SDP */
static struct ast_sip_nat_hook *nat_hook;

/*!
 * \brief Registered SDP stream handlers
 *
 * This container is keyed on stream types. Each
 * object in the container is a linked list of
 * handlers for the stream type.
 */
static struct ao2_container *sdp_handlers;

/*!
 * These are the objects in the sdp_handlers container
 */
struct sdp_handler_list {
	/* The list of handlers to visit */
	AST_LIST_HEAD_NOLOCK(, ast_sip_session_sdp_handler) list;
	/* The handlers in this list handle streams of this type */
	char stream_type[1];
};

static struct pjmedia_sdp_session *create_local_sdp(pjsip_inv_session *inv, struct ast_sip_session *session, const pjmedia_sdp_session *offer);

static int sdp_handler_list_hash(const void *obj, int flags)
{
	const struct sdp_handler_list *handler_list = obj;
	const char *stream_type = flags & OBJ_KEY ? obj : handler_list->stream_type;

	return ast_str_hash(stream_type);
}

const char *ast_sip_session_get_name(const struct ast_sip_session *session)
{
	if (!session) {
		return "(null session)";
	}
	if (session->channel) {
		return ast_channel_name(session->channel);
	} else if (session->endpoint) {
		return ast_sorcery_object_get_id(session->endpoint);
	} else {
		return "unknown";
	}
}

static int sdp_handler_list_cmp(void *obj, void *arg, int flags)
{
	struct sdp_handler_list *handler_list1 = obj;
	struct sdp_handler_list *handler_list2 = arg;
	const char *stream_type2 = flags & OBJ_KEY ? arg : handler_list2->stream_type;

	return strcmp(handler_list1->stream_type, stream_type2) ? 0 : CMP_MATCH | CMP_STOP;
}

int ast_sip_session_register_sdp_handler(struct ast_sip_session_sdp_handler *handler, const char *stream_type)
{
	RAII_VAR(struct sdp_handler_list *, handler_list,
			ao2_find(sdp_handlers, stream_type, OBJ_KEY), ao2_cleanup);
	SCOPED_AO2LOCK(lock, sdp_handlers);

	if (handler_list) {
		struct ast_sip_session_sdp_handler *iter;
		/* Check if this handler is already registered for this stream type */
		AST_LIST_TRAVERSE(&handler_list->list, iter, next) {
			if (!strcmp(iter->id, handler->id)) {
				ast_log(LOG_WARNING, "Handler '%s' already registered for stream type '%s'.\n", handler->id, stream_type);
				return -1;
			}
		}
		AST_LIST_INSERT_TAIL(&handler_list->list, handler, next);
		ast_debug(1, "Registered SDP stream handler '%s' for stream type '%s'\n", handler->id, stream_type);

		return 0;
	}

	/* No stream of this type has been registered yet, so we need to create a new list */
	handler_list = ao2_alloc(sizeof(*handler_list) + strlen(stream_type), NULL);
	if (!handler_list) {
		return -1;
	}
	/* Safe use of strcpy */
	strcpy(handler_list->stream_type, stream_type);
	AST_LIST_HEAD_INIT_NOLOCK(&handler_list->list);
	AST_LIST_INSERT_TAIL(&handler_list->list, handler, next);
	if (!ao2_link(sdp_handlers, handler_list)) {
		return -1;
	}
	ast_debug(1, "Registered SDP stream handler '%s' for stream type '%s'\n", handler->id, stream_type);

	return 0;
}

static int remove_handler(void *obj, void *arg, void *data, int flags)
{
	struct sdp_handler_list *handler_list = obj;
	struct ast_sip_session_sdp_handler *handler = data;
	struct ast_sip_session_sdp_handler *iter;
	const char *stream_type = arg;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&handler_list->list, iter, next) {
		if (!strcmp(iter->id, handler->id)) {
			AST_LIST_REMOVE_CURRENT(next);
			ast_debug(1, "Unregistered SDP stream handler '%s' for stream type '%s'\n", handler->id, stream_type);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (AST_LIST_EMPTY(&handler_list->list)) {
		ast_debug(3, "No more handlers exist for stream type '%s'\n", stream_type);
		return CMP_MATCH;
	} else {
		return CMP_STOP;
	}
}

void ast_sip_session_unregister_sdp_handler(struct ast_sip_session_sdp_handler *handler, const char *stream_type)
{
	ao2_callback_data(sdp_handlers, OBJ_KEY | OBJ_UNLINK | OBJ_NODATA, remove_handler, (void *)stream_type, handler);
}

static int media_stats_local_ssrc_cmp(
		const struct ast_rtp_instance_stats *vec_elem, const struct ast_rtp_instance_stats *srch)
{
	if (vec_elem->local_ssrc == srch->local_ssrc) {
		return 1;
	}

	return 0;
}

static struct ast_sip_session_media_state *internal_sip_session_media_state_alloc(
	size_t sessions, size_t read_callbacks)
{
	struct ast_sip_session_media_state *media_state;

	media_state = ast_calloc(1, sizeof(*media_state));
	if (!media_state) {
		return NULL;
	}

	if (AST_VECTOR_INIT(&media_state->sessions, sessions) < 0) {
		ast_free(media_state);
		return NULL;
	}

	if (AST_VECTOR_INIT(&media_state->read_callbacks, read_callbacks) < 0) {
		AST_VECTOR_FREE(&media_state->sessions);
		ast_free(media_state);
		return NULL;
	}

	return media_state;
}

struct ast_sip_session_media_state *ast_sip_session_media_state_alloc(void)
{
	return internal_sip_session_media_state_alloc(
		DEFAULT_NUM_SESSION_MEDIA, DEFAULT_NUM_SESSION_MEDIA);
}

void ast_sip_session_media_stats_save(struct ast_sip_session *sip_session, struct ast_sip_session_media_state *media_state)
{
	int i;
	int ret;

	if (!media_state || !sip_session) {
		return;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&media_state->sessions); i++) {
		struct ast_rtp_instance_stats *stats_tmp = NULL;
		struct ast_sip_session_media *media = AST_VECTOR_GET(&media_state->sessions, i);
		if (!media || !media->rtp) {
			continue;
		}

		stats_tmp = ast_calloc(1, sizeof(struct ast_rtp_instance_stats));
		if (!stats_tmp) {
			return;
		}

		ret = ast_rtp_instance_get_stats(media->rtp, stats_tmp, AST_RTP_INSTANCE_STAT_ALL);
		if (ret) {
			ast_free(stats_tmp);
			continue;
		}

		/* remove all the duplicated stats if exist */
		AST_VECTOR_REMOVE_CMP_UNORDERED(&sip_session->media_stats, stats_tmp, media_stats_local_ssrc_cmp, ast_free);

		AST_VECTOR_APPEND(&sip_session->media_stats, stats_tmp);
	}
}

void ast_sip_session_media_state_reset(struct ast_sip_session_media_state *media_state)
{
	int index;

	if (!media_state) {
		return;
	}

	AST_VECTOR_RESET(&media_state->sessions, ao2_cleanup);
	AST_VECTOR_RESET(&media_state->read_callbacks, AST_VECTOR_ELEM_CLEANUP_NOOP);

	for (index = 0; index < AST_MEDIA_TYPE_END; ++index) {
		media_state->default_session[index] = NULL;
	}

	ast_stream_topology_free(media_state->topology);
	media_state->topology = NULL;
}

struct ast_sip_session_media_state *ast_sip_session_media_state_clone(const struct ast_sip_session_media_state *media_state)
{
	struct ast_sip_session_media_state *cloned;
	int index;

	if (!media_state) {
		return NULL;
	}

	cloned = internal_sip_session_media_state_alloc(
		AST_VECTOR_SIZE(&media_state->sessions),
		AST_VECTOR_SIZE(&media_state->read_callbacks));
	if (!cloned) {
		return NULL;
	}

	if (media_state->topology) {
		cloned->topology = ast_stream_topology_clone(media_state->topology);
		if (!cloned->topology) {
			ast_sip_session_media_state_free(cloned);
			return NULL;
		}
	}

	for (index = 0; index < AST_VECTOR_SIZE(&media_state->sessions); ++index) {
		struct ast_sip_session_media *session_media = AST_VECTOR_GET(&media_state->sessions, index);
		enum ast_media_type type = ast_stream_get_type(ast_stream_topology_get_stream(cloned->topology, index));

		ao2_bump(session_media);
		if (AST_VECTOR_REPLACE(&cloned->sessions, index, session_media)) {
			ao2_cleanup(session_media);
		}
		if (ast_stream_get_state(ast_stream_topology_get_stream(cloned->topology, index)) != AST_STREAM_STATE_REMOVED &&
			!cloned->default_session[type]) {
			cloned->default_session[type] = session_media;
		}
	}

	for (index = 0; index < AST_VECTOR_SIZE(&media_state->read_callbacks); ++index) {
		struct ast_sip_session_media_read_callback_state *read_callback = AST_VECTOR_GET_ADDR(&media_state->read_callbacks, index);

		AST_VECTOR_REPLACE(&cloned->read_callbacks, index, *read_callback);
	}

	return cloned;
}

void ast_sip_session_media_state_free(struct ast_sip_session_media_state *media_state)
{
	if (!media_state) {
		return;
	}

	/* This will reset the internal state so we only have to free persistent things */
	ast_sip_session_media_state_reset(media_state);

	AST_VECTOR_FREE(&media_state->sessions);
	AST_VECTOR_FREE(&media_state->read_callbacks);

	ast_free(media_state);
}

int ast_sip_session_is_pending_stream_default(const struct ast_sip_session *session, const struct ast_stream *stream)
{
	int index;

	if (!session->pending_media_state->topology) {
		ast_log(LOG_WARNING, "Pending topology was NULL for channel '%s'\n",
			session->channel ? ast_channel_name(session->channel) : "unknown");
		return 0;
	}

	if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
		return 0;
	}

	for (index = 0; index < ast_stream_topology_get_count(session->pending_media_state->topology); ++index) {
		if (ast_stream_get_type(ast_stream_topology_get_stream(session->pending_media_state->topology, index)) !=
			ast_stream_get_type(stream)) {
			continue;
		}

		return ast_stream_topology_get_stream(session->pending_media_state->topology, index) == stream ? 1 : 0;
	}

	return 0;
}

int ast_sip_session_media_add_read_callback(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	int fd, ast_sip_session_media_read_cb callback)
{
	struct ast_sip_session_media_read_callback_state callback_state = {
		.fd = fd,
		.read_callback = callback,
		.session = session_media,
	};

	/* The contents of the vector are whole structs and not pointers */
	return AST_VECTOR_APPEND(&session->pending_media_state->read_callbacks, callback_state);
}

int ast_sip_session_media_set_write_callback(struct ast_sip_session *session, struct ast_sip_session_media *session_media,
	ast_sip_session_media_write_cb callback)
{
	if (session_media->write_callback) {
		if (session_media->write_callback == callback) {
			return 0;
		}

		return -1;
	}

	session_media->write_callback = callback;

	return 0;
}

struct ast_sip_session_media *ast_sip_session_media_get_transport(struct ast_sip_session *session, struct ast_sip_session_media *session_media)
{
	int index;

	if (!session->endpoint->media.bundle || ast_strlen_zero(session_media->mid)) {
		return session_media;
	}

	for (index = 0; index < AST_VECTOR_SIZE(&session->pending_media_state->sessions); ++index) {
		struct ast_sip_session_media *bundle_group_session_media;

		bundle_group_session_media = AST_VECTOR_GET(&session->pending_media_state->sessions, index);

		/* The first session which is in the bundle group is considered the authoritative session for transport */
		if (bundle_group_session_media->bundle_group == session_media->bundle_group) {
			return bundle_group_session_media;
		}
	}

	return session_media;
}

/*!
 * \brief Set an SDP stream handler for a corresponding session media.
 *
 * \note Always use this function to set the SDP handler for a session media.
 *
 * This function will properly free resources on the SDP handler currently being
 * used by the session media, then set the session media to use the new SDP
 * handler.
 */
static void session_media_set_handler(struct ast_sip_session_media *session_media,
		struct ast_sip_session_sdp_handler *handler)
{
	ast_assert(session_media->handler != handler);

	if (session_media->handler) {
		session_media->handler->stream_destroy(session_media);
	}
	session_media->handler = handler;
}

static int stream_destroy(void *obj, void *arg, int flags)
{
	struct sdp_handler_list *handler_list = obj;
	struct ast_sip_session_media *session_media = arg;
	struct ast_sip_session_sdp_handler *handler;

	AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
		handler->stream_destroy(session_media);
	}

	return 0;
}

static void session_media_dtor(void *obj)
{
	struct ast_sip_session_media *session_media = obj;

	/* It is possible for multiple handlers to have allocated memory on the
	 * session media (usually through a stream changing types). Therefore, we
	 * traverse all the SDP handlers and let them all call stream_destroy on
	 * the session_media
	 */
	ao2_callback(sdp_handlers, 0, stream_destroy, session_media);

	if (session_media->srtp) {
		ast_sdp_srtp_destroy(session_media->srtp);
	}

	ast_free(session_media->mid);
	ast_free(session_media->remote_mslabel);
	ast_free(session_media->remote_label);
	ast_free(session_media->stream_name);
}

struct ast_sip_session_media *ast_sip_session_media_state_add(struct ast_sip_session *session,
	struct ast_sip_session_media_state *media_state, enum ast_media_type type, int position)
{
	struct ast_sip_session_media *session_media = NULL;
	struct ast_sip_session_media *current_session_media = NULL;
	SCOPE_ENTER(1, "%s Adding position %d\n", ast_sip_session_get_name(session), position);

	/* It is possible for this media state to already contain a session for the stream. If this
	 * is the case we simply return it.
	 */
	if (position < AST_VECTOR_SIZE(&media_state->sessions)) {
		current_session_media = AST_VECTOR_GET(&media_state->sessions, position);
		if (current_session_media && current_session_media->type == type) {
			SCOPE_EXIT_RTN_VALUE(current_session_media, "Using existing media_session\n");
		}
	}

	/* Determine if we can reuse the session media from the active media state if present */
	if (position < AST_VECTOR_SIZE(&session->active_media_state->sessions)) {
		session_media = AST_VECTOR_GET(&session->active_media_state->sessions, position);
		/* A stream can never exist without an accompanying media session */
		if (session_media->type == type) {
			ao2_ref(session_media, +1);
			ast_trace(1, "Reusing existing media session\n");
			/*
			 * If this session_media was previously removed, its bundle group was probably reset
			 * to -1 so if bundling is enabled on the endpoint, we need to reset it to 0, set
			 * the bundled flag and reset its mid.
			 */
			if (session->endpoint->media.bundle && session_media->bundle_group == -1) {
				session_media->bundled = session->endpoint->media.webrtc;
				session_media->bundle_group = 0;
				ast_free(session_media->mid);
				if (ast_asprintf(&session_media->mid, "%s-%d", ast_codec_media_type2str(type), position) < 0) {
					ao2_ref(session_media, -1);
					SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't alloc mid\n");
				}
			}
		} else {
			ast_trace(1, "Can't reuse existing media session because the types are different. %s <> %s\n",
				ast_codec_media_type2str(type), ast_codec_media_type2str(session_media->type));
			session_media = NULL;
		}
	}

	if (!session_media) {
		/* No existing media session we can use so create a new one */
		session_media = ao2_alloc_options(sizeof(*session_media), session_media_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
		if (!session_media) {
			return NULL;
		}
		ast_trace(1, "Creating new media session\n");

		session_media->encryption = session->endpoint->media.rtp.encryption;
		session_media->remote_ice = session->endpoint->media.rtp.ice_support;
		session_media->remote_rtcp_mux = session->endpoint->media.rtcp_mux;
		session_media->keepalive_sched_id = -1;
		session_media->timeout_sched_id = -1;
		session_media->type = type;
		session_media->stream_num = position;

		if (session->endpoint->media.bundle) {
			/* This is a new stream so create a new mid based on media type and position, which makes it unique.
			 * If this is the result of an offer the mid will just end up getting replaced.
			 */
			if (ast_asprintf(&session_media->mid, "%s-%d", ast_codec_media_type2str(type), position) < 0) {
				ao2_ref(session_media, -1);
				SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't alloc mid\n");
			}
			session_media->bundle_group = 0;

			/* Some WebRTC clients can't handle an offer to bundle media streams. Instead they expect them to
			 * already be bundled. Every client handles this scenario though so if WebRTC is enabled just go
			 * ahead and treat the streams as having already been bundled.
			 */
			session_media->bundled = session->endpoint->media.webrtc;
		} else {
			session_media->bundle_group = -1;
		}
	}

	ast_free(session_media->stream_name);
	session_media->stream_name = ast_strdup(ast_stream_get_name(ast_stream_topology_get_stream(media_state->topology, position)));

	if (AST_VECTOR_REPLACE(&media_state->sessions, position, session_media)) {
		ao2_ref(session_media, -1);

		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't replace media_session\n");
	}

	ao2_cleanup(current_session_media);

	/* If this stream will be active in some way and it is the first of this type then consider this the default media session to match */
	if (!media_state->default_session[type] && ast_stream_get_state(ast_stream_topology_get_stream(media_state->topology, position)) != AST_STREAM_STATE_REMOVED) {
		ast_trace(1, "Setting media session as default for %s\n", ast_codec_media_type2str(session_media->type));

		media_state->default_session[type] = session_media;
	}

	SCOPE_EXIT_RTN_VALUE(session_media, "Done\n");
}

static int is_stream_limitation_reached(enum ast_media_type type, const struct ast_sip_endpoint *endpoint, int *type_streams)
{
	switch (type) {
	case AST_MEDIA_TYPE_AUDIO:
		return !(type_streams[type] < endpoint->media.max_audio_streams);
	case AST_MEDIA_TYPE_VIDEO:
		return !(type_streams[type] < endpoint->media.max_video_streams);
	case AST_MEDIA_TYPE_IMAGE:
		/* We don't have an option for image (T.38) streams so cap it to one. */
		return (type_streams[type] > 0);
	case AST_MEDIA_TYPE_UNKNOWN:
	case AST_MEDIA_TYPE_TEXT:
	default:
		/* We don't want any unknown or "other" streams on our endpoint,
		 * so always just say we've reached the limit
		 */
		return 1;
	}
}

static int get_mid_bundle_group(const pjmedia_sdp_session *sdp, const char *mid)
{
	int bundle_group = 0;
	int index;

	for (index = 0; index < sdp->attr_count; ++index) {
		pjmedia_sdp_attr *attr = sdp->attr[index];
		char value[pj_strlen(&attr->value) + 1], *mids = value, *attr_mid;

		if (pj_strcmp2(&attr->name, "group") || pj_strncmp2(&attr->value, "BUNDLE", 6)) {
			continue;
		}

		ast_copy_pj_str(value, &attr->value, sizeof(value));

		/* Skip the BUNDLE at the front */
		mids += 7;

		while ((attr_mid = strsep(&mids, " "))) {
			if (!strcmp(attr_mid, mid)) {
				/* The ordering of attributes determines our internal identification of the bundle group based on number,
				 * with -1 being not in a bundle group. Since this is only exposed internally for response purposes it's
				 * actually even fine if things move around.
				 */
				return bundle_group;
			}
		}

		bundle_group++;
	}

	return -1;
}

static int set_mid_and_bundle_group(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media,
	const pjmedia_sdp_session *sdp,
	const struct pjmedia_sdp_media *stream)
{
	pjmedia_sdp_attr *attr;

	if (!session->endpoint->media.bundle) {
		return 0;
	}

	/* By default on an incoming negotiation we assume no mid and bundle group is present */
	ast_free(session_media->mid);
	session_media->mid = NULL;
	session_media->bundle_group = -1;
	session_media->bundled = 0;

	/* Grab the media identifier for the stream */
	attr = pjmedia_sdp_media_find_attr2(stream, "mid", NULL);
	if (!attr) {
		return 0;
	}

	session_media->mid = ast_calloc(1, attr->value.slen + 1);
	if (!session_media->mid) {
		return 0;
	}
	ast_copy_pj_str(session_media->mid, &attr->value, attr->value.slen + 1);

	/* Determine what bundle group this is part of */
	session_media->bundle_group = get_mid_bundle_group(sdp, session_media->mid);

	/* If this is actually part of a bundle group then the other side requested or accepted the bundle request */
	session_media->bundled = session_media->bundle_group != -1;

	return 0;
}

static void set_remote_mslabel_and_stream_group(struct ast_sip_session *session,
	struct ast_sip_session_media *session_media,
	const pjmedia_sdp_session *sdp,
	const struct pjmedia_sdp_media *stream,
	struct ast_stream *asterisk_stream)
{
	int index;

	ast_free(session_media->remote_mslabel);
	session_media->remote_mslabel = NULL;
	ast_free(session_media->remote_label);
	session_media->remote_label = NULL;

	for (index = 0; index < stream->attr_count; ++index) {
		pjmedia_sdp_attr *attr = stream->attr[index];
		char attr_value[pj_strlen(&attr->value) + 1];
		char *ssrc_attribute_name, *ssrc_attribute_value = NULL;
		char *msid, *tmp = attr_value;
		static const pj_str_t STR_msid = { "msid", 4 };
		static const pj_str_t STR_ssrc = { "ssrc", 4 };
		static const pj_str_t STR_label = { "label", 5 };

		if (!pj_strcmp(&attr->name, &STR_label)) {
			ast_copy_pj_str(attr_value, &attr->value, sizeof(attr_value));
			session_media->remote_label = ast_strdup(attr_value);
		} else if (!pj_strcmp(&attr->name, &STR_msid)) {
			ast_copy_pj_str(attr_value, &attr->value, sizeof(attr_value));
			msid = strsep(&tmp, " ");
			session_media->remote_mslabel = ast_strdup(msid);
			break;
		} else if (!pj_strcmp(&attr->name, &STR_ssrc)) {
			ast_copy_pj_str(attr_value, &attr->value, sizeof(attr_value));

			if ((ssrc_attribute_name = strchr(attr_value, ' '))) {
				/* This has an actual attribute */
				*ssrc_attribute_name++ = '\0';
				ssrc_attribute_value = strchr(ssrc_attribute_name, ':');
				if (ssrc_attribute_value) {
					/* Values are actually optional according to the spec */
					*ssrc_attribute_value++ = '\0';
				}

				if (!strcasecmp(ssrc_attribute_name, "mslabel") && !ast_strlen_zero(ssrc_attribute_value)) {
					session_media->remote_mslabel = ast_strdup(ssrc_attribute_value);
					break;
				}
			}
		}
	}

	if (ast_strlen_zero(session_media->remote_mslabel)) {
		return;
	}

	/* Iterate through the existing streams looking for a match and if so then group this with it */
	for (index = 0; index < AST_VECTOR_SIZE(&session->pending_media_state->sessions); ++index) {
		struct ast_sip_session_media *group_session_media;

		group_session_media = AST_VECTOR_GET(&session->pending_media_state->sessions, index);

		if (ast_strlen_zero(group_session_media->remote_mslabel) ||
			strcmp(group_session_media->remote_mslabel, session_media->remote_mslabel)) {
			continue;
		}

		ast_stream_set_group(asterisk_stream, index);
		break;
	}
}

static void remove_stream_from_bundle(struct ast_sip_session_media *session_media,
	struct ast_stream *stream)
{
	ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
	ast_free(session_media->mid);
	session_media->mid = NULL;
	session_media->bundle_group = -1;
	session_media->bundled = 0;
}

static int handle_incoming_sdp(struct ast_sip_session *session, const pjmedia_sdp_session *sdp)
{
	int i;
	int handled = 0;
	int type_streams[AST_MEDIA_TYPE_END] = {0};
	SCOPE_ENTER(3, "%s: Media count: %d\n", ast_sip_session_get_name(session), sdp->media_count);

	if (session->inv_session && session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Failed to handle incoming SDP. Session has been already disconnected\n",
			ast_sip_session_get_name(session));
	}

	/* It is possible for SDP deferral to have already created a pending topology */
	if (!session->pending_media_state->topology) {
		session->pending_media_state->topology = ast_stream_topology_alloc();
		if (!session->pending_media_state->topology) {
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "%s: Couldn't alloc pending topology\n",
				ast_sip_session_get_name(session));
		}
	}

	for (i = 0; i < sdp->media_count; ++i) {
		/* See if there are registered handlers for this media stream type */
		char media[20];
		struct ast_sip_session_sdp_handler *handler;
		RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);
		struct ast_sip_session_media *session_media = NULL;
		int res;
		enum ast_media_type type;
		struct ast_stream *stream = NULL;
		pjmedia_sdp_media *remote_stream = sdp->media[i];
		SCOPE_ENTER(4, "%s: Processing stream %d\n", ast_sip_session_get_name(session), i);

		/* We need a null-terminated version of the media string */
		ast_copy_pj_str(media, &sdp->media[i]->desc.media, sizeof(media));
		type = ast_media_type_from_str(media);

		/* See if we have an already existing stream, which can occur from SDP deferral checking */
		if (i < ast_stream_topology_get_count(session->pending_media_state->topology)) {
			stream = ast_stream_topology_get_stream(session->pending_media_state->topology, i);
			ast_trace(-1, "%s: Using existing pending stream %s\n", ast_sip_session_get_name(session),
				ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
		}
		if (!stream) {
			struct ast_stream *existing_stream = NULL;
			char *stream_name = NULL, *stream_name_allocated = NULL;
			const char *stream_label = NULL;

			if (session->active_media_state->topology &&
				(i < ast_stream_topology_get_count(session->active_media_state->topology))) {
				existing_stream = ast_stream_topology_get_stream(session->active_media_state->topology, i);
				ast_trace(-1, "%s: Found existing active stream %s\n", ast_sip_session_get_name(session),
					ast_str_tmp(128, ast_stream_to_str(existing_stream, &STR_TMP)));

				if (ast_stream_get_state(existing_stream) != AST_STREAM_STATE_REMOVED) {
					stream_name = (char *)ast_stream_get_name(existing_stream);
					stream_label = ast_stream_get_metadata(existing_stream, "SDP:LABEL");
				}
			}

			if (ast_strlen_zero(stream_name)) {
				if (ast_asprintf(&stream_name_allocated, "%s-%d", ast_codec_media_type2str(type), i) < 0) {
					handled = 0;
					SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Couldn't alloc stream name\n",
						 ast_sip_session_get_name(session));

				}
				stream_name = stream_name_allocated;
				ast_trace(-1, "%s: Using %s for new stream name\n", ast_sip_session_get_name(session),
					stream_name);
			}

			stream = ast_stream_alloc(stream_name, type);
			ast_free(stream_name_allocated);
			if (!stream) {
				handled = 0;
				SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Couldn't alloc stream\n",
					 ast_sip_session_get_name(session));
			}

			if (!ast_strlen_zero(stream_label)) {
				ast_stream_set_metadata(stream, "SDP:LABEL", stream_label);
				ast_trace(-1, "%s: Using %s for new stream label\n", ast_sip_session_get_name(session),
					stream_label);

			}

			if (ast_stream_topology_set_stream(session->pending_media_state->topology, i, stream)) {
				ast_stream_free(stream);
				handled = 0;
				SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Couldn't set stream in topology\n",
					 ast_sip_session_get_name(session));
			}

			/* For backwards compatibility with the core the default audio stream is always sendrecv */
			if (!ast_sip_session_is_pending_stream_default(session, stream) || strcmp(media, "audio")) {
				if (pjmedia_sdp_media_find_attr2(remote_stream, "sendonly", NULL)) {
					/* Stream state reflects our state of a stream, so in the case of
					 * sendonly and recvonly we store the opposite since that is what ours
					 * is.
					 */
					ast_stream_set_state(stream, AST_STREAM_STATE_RECVONLY);
				} else if (pjmedia_sdp_media_find_attr2(remote_stream, "recvonly", NULL)) {
					ast_stream_set_state(stream, AST_STREAM_STATE_SENDONLY);
				} else if (pjmedia_sdp_media_find_attr2(remote_stream, "inactive", NULL)) {
					ast_stream_set_state(stream, AST_STREAM_STATE_INACTIVE);
				} else {
					ast_stream_set_state(stream, AST_STREAM_STATE_SENDRECV);
				}
			} else {
				ast_stream_set_state(stream, AST_STREAM_STATE_SENDRECV);
			}
			ast_trace(-1, "%s: Using new stream %s\n", ast_sip_session_get_name(session),
				ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
		}

		session_media = ast_sip_session_media_state_add(session, session->pending_media_state, ast_media_type_from_str(media), i);
		if (!session_media) {
			SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Couldn't alloc session media\n",
				 ast_sip_session_get_name(session));
		}

		/* If this stream is already declined mark it as such, or mark it as such if we've reached the limit */
		if (!remote_stream->desc.port || is_stream_limitation_reached(type, session->endpoint, type_streams)) {
			remove_stream_from_bundle(session_media, stream);
			SCOPE_EXIT_EXPR(continue, "%s: Declining incoming SDP media stream %s'\n",
				ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
		}

		set_mid_and_bundle_group(session, session_media, sdp, remote_stream);
		set_remote_mslabel_and_stream_group(session, session_media, sdp, remote_stream, stream);

		if (session_media->handler) {
			handler = session_media->handler;
			ast_trace(-1, "%s: Negotiating incoming SDP media stream %s using %s SDP handler\n",
				ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)),
				session_media->handler->id);
			res = handler->negotiate_incoming_sdp_stream(session, session_media, sdp, i, stream);
			if (res < 0) {
				/* Catastrophic failure. Abort! */
				SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Couldn't negotiate stream %s\n",
					 ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
			} else if (res == 0) {
				remove_stream_from_bundle(session_media, stream);
				SCOPE_EXIT_EXPR(continue, "%s: Declining incoming SDP media stream %s\n",
					ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
			} else if (res > 0) {
				handled = 1;
				++type_streams[type];
				/* Handled by this handler. Move to the next stream */
				SCOPE_EXIT_EXPR(continue, "%s: Media stream %s handled by %s\n",
					ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)),
					session_media->handler->id);
			}
		}

		handler_list = ao2_find(sdp_handlers, media, OBJ_KEY);
		if (!handler_list) {
			SCOPE_EXIT_EXPR(continue, "%s: Media stream %s has no registered handlers\n",
				ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
		}
		AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
			if (handler == session_media->handler) {
				continue;
			}
			ast_trace(-1, "%s: Negotiating incoming SDP media stream %s using %s SDP handler\n",
				ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)),
				handler->id);

			res = handler->negotiate_incoming_sdp_stream(session, session_media, sdp, i, stream);
			if (res < 0) {
				/* Catastrophic failure. Abort! */
				handled = 0;
				SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Couldn't negotiate stream %s\n",
					 ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
			} else if (res == 0) {
				remove_stream_from_bundle(session_media, stream);
				ast_trace(-1, "%s: Declining incoming SDP media stream %s\n",
					ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
				continue;
			} else if (res > 0) {
				session_media_set_handler(session_media, handler);
				handled = 1;
				++type_streams[type];
				ast_trace(-1, "%s: Media stream %s handled by %s\n",
					ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)),
					session_media->handler->id);
				break;
			}
		}

		SCOPE_EXIT("%s: Done with stream %s\n", ast_sip_session_get_name(session),
			ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
	}

end:
	SCOPE_EXIT_RTN_VALUE(handled ? 0 : -1, "%s: Handled? %s\n", ast_sip_session_get_name(session),
		handled ? "yes" : "no");
}

static int handle_negotiated_sdp_session_media(struct ast_sip_session_media *session_media,
		struct ast_sip_session *session, const pjmedia_sdp_session *local,
		const pjmedia_sdp_session *remote, int index, struct ast_stream *asterisk_stream)
{
	/* See if there are registered handlers for this media stream type */
	struct pjmedia_sdp_media *local_stream = local->media[index];
	char media[20];
	struct ast_sip_session_sdp_handler *handler;
	RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);
	int res;
	SCOPE_ENTER(1, "%s\n", session ? ast_sip_session_get_name(session) : "unknown");

	/* We need a null-terminated version of the media string */
	ast_copy_pj_str(media, &local->media[index]->desc.media, sizeof(media));

	/* For backwards compatibility we only reflect the stream state correctly on
	 * the non-default streams and any non-audio streams. This is because the stream
	 * state of the default audio stream is also used for signaling that someone has
	 * placed us on hold. This situation is not handled currently and can result in
	 * the remote side being sorted of placed on hold too.
	 */
	if (!ast_sip_session_is_pending_stream_default(session, asterisk_stream) || strcmp(media, "audio")) {
		/* Determine the state of the stream based on our local SDP */
		if (pjmedia_sdp_media_find_attr2(local_stream, "sendonly", NULL)) {
			ast_stream_set_state(asterisk_stream, AST_STREAM_STATE_SENDONLY);
		} else if (pjmedia_sdp_media_find_attr2(local_stream, "recvonly", NULL)) {
			ast_stream_set_state(asterisk_stream, AST_STREAM_STATE_RECVONLY);
		} else if (pjmedia_sdp_media_find_attr2(local_stream, "inactive", NULL)) {
			ast_stream_set_state(asterisk_stream, AST_STREAM_STATE_INACTIVE);
		} else {
			ast_stream_set_state(asterisk_stream, AST_STREAM_STATE_SENDRECV);
		}
	} else {
		ast_stream_set_state(asterisk_stream, AST_STREAM_STATE_SENDRECV);
	}

	set_mid_and_bundle_group(session, session_media, remote, remote->media[index]);
	set_remote_mslabel_and_stream_group(session, session_media, remote, remote->media[index], asterisk_stream);

	handler = session_media->handler;
	if (handler) {
		ast_debug(4, "%s: Applying negotiated SDP media stream '%s' using %s SDP handler\n",
			ast_sip_session_get_name(session), ast_codec_media_type2str(session_media->type),
			handler->id);
		res = handler->apply_negotiated_sdp_stream(session, session_media, local, remote, index, asterisk_stream);
		if (res >= 0) {
			ast_debug(4, "%s: Applied negotiated SDP media stream '%s' using %s SDP handler\n",
				ast_sip_session_get_name(session), ast_codec_media_type2str(session_media->type),
				handler->id);
			SCOPE_EXIT_RTN_VALUE(0,  "%s: Applied negotiated SDP media stream '%s' using %s SDP handler\n",
				ast_sip_session_get_name(session), ast_codec_media_type2str(session_media->type),
				handler->id);
		}
		SCOPE_EXIT_RTN_VALUE(-1,  "%s: Failed to apply negotiated SDP media stream '%s' using %s SDP handler\n",
			ast_sip_session_get_name(session), ast_codec_media_type2str(session_media->type),
			handler->id);
	}

	handler_list = ao2_find(sdp_handlers, media, OBJ_KEY);
	if (!handler_list) {
		ast_debug(4, "%s: No registered SDP handlers for media type '%s'\n", ast_sip_session_get_name(session), media);
		return -1;
	}
	AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
		if (handler == session_media->handler) {
			continue;
		}
		ast_debug(4, "%s: Applying negotiated SDP media stream '%s' using %s SDP handler\n",
			ast_sip_session_get_name(session), ast_codec_media_type2str(session_media->type),
			handler->id);
		res = handler->apply_negotiated_sdp_stream(session, session_media, local, remote, index, asterisk_stream);
		if (res < 0) {
			/* Catastrophic failure. Abort! */
			SCOPE_EXIT_RTN_VALUE(-1, "%s: Handler '%s' returned %d\n",
				ast_sip_session_get_name(session), handler->id, res);
		}
		if (res > 0) {
			ast_debug(4, "%s: Applied negotiated SDP media stream '%s' using %s SDP handler\n",
				ast_sip_session_get_name(session), ast_codec_media_type2str(session_media->type),
				handler->id);
			/* Handled by this handler. Move to the next stream */
			session_media_set_handler(session_media, handler);
			SCOPE_EXIT_RTN_VALUE(0, "%s: Handler '%s' handled this sdp stream\n",
				ast_sip_session_get_name(session), handler->id);
		}
	}

	res = 0;
	if (session_media->handler && session_media->handler->stream_stop) {
		ast_debug(4, "%s: Stopping SDP media stream '%s' as it is not currently negotiated\n",
			ast_sip_session_get_name(session), ast_codec_media_type2str(session_media->type));
		session_media->handler->stream_stop(session_media);
	}

	SCOPE_EXIT_RTN_VALUE(0, "%s: Media type '%s' %s\n",
		ast_sip_session_get_name(session), ast_codec_media_type2str(session_media->type),
		res ? "not negotiated.  Stopped" : "handled");
}

static int handle_negotiated_sdp(struct ast_sip_session *session, const pjmedia_sdp_session *local, const pjmedia_sdp_session *remote)
{
	int i;
	struct ast_stream_topology *topology;
	unsigned int changed = 0; /* 0 = unchanged, 1 = new source, 2 = new topology */
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	if (!session->pending_media_state->topology) {
		if (session->active_media_state->topology) {
			/*
			 * This happens when we have negotiated media after receiving a 183,
			 * and we're now receiving a 200 with a new SDP.  In this case, there
			 * is active_media_state, but the pending_media_state has been reset.
			 */
			struct ast_sip_session_media_state *active_media_state_clone;

			active_media_state_clone =
				ast_sip_session_media_state_clone(session->active_media_state);
			if (!active_media_state_clone) {
				ast_log(LOG_WARNING, "%s: Unable to clone active media state\n",
					ast_sip_session_get_name(session));
				return -1;
			}

			ast_sip_session_media_state_free(session->pending_media_state);
			session->pending_media_state = active_media_state_clone;
		} else {
			ast_log(LOG_WARNING, "%s: No pending or active media state\n",
				ast_sip_session_get_name(session));
			return -1;
		}
	}

	/* If we're handling negotiated streams, then we should already have set
	 * up session media instances (and Asterisk streams) that correspond to
	 * the local SDP, and there should be the same number of session medias
	 * and streams as there are local SDP streams
	 */
	if (ast_stream_topology_get_count(session->pending_media_state->topology) != local->media_count
		|| AST_VECTOR_SIZE(&session->pending_media_state->sessions) != local->media_count) {
		ast_log(LOG_WARNING, "%s: Local SDP contains %d media streams while we expected it to contain %u\n",
			ast_sip_session_get_name(session),
			ast_stream_topology_get_count(session->pending_media_state->topology), local->media_count);
		SCOPE_EXIT_RTN_VALUE(-1, "Media stream count mismatch\n");
	}

	for (i = 0; i < local->media_count; ++i) {
		struct ast_sip_session_media *session_media;
		struct ast_stream *stream;

		if (!remote->media[i]) {
			continue;
		}

		session_media = AST_VECTOR_GET(&session->pending_media_state->sessions, i);
		stream = ast_stream_topology_get_stream(session->pending_media_state->topology, i);

		/* Make sure that this stream is in the correct state. If we need to change
		 * the state to REMOVED, then our work here is done, so go ahead and move on
		 * to the next stream.
		 */
		if (!remote->media[i]->desc.port) {
			ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
			continue;
		}

		/* If the stream state is REMOVED, nothing needs to be done, so move on to the
		 * next stream. This can occur if an internal thing has requested it to be
		 * removed, or if we remove it as a result of the stream limit being reached.
		 */
		if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
			/*
			 * Defer removing the handler until we are ready to activate
			 * the new topology.  The channel's thread may still be using
			 * the stream and we could crash before we are ready.
			 */
			continue;
		}

		if (handle_negotiated_sdp_session_media(session_media, session, local, remote, i, stream)) {
			SCOPE_EXIT_RTN_VALUE(-1, "Unable to handle negotiated session media\n");
		}

		changed |= session_media->changed;
		session_media->changed = 0;
	}

	/* Apply the pending media state to the channel and make it active */
	ast_channel_lock(session->channel);

	/* Now update the stream handler for any declined/removed streams */
	for (i = 0; i < local->media_count; ++i) {
		struct ast_sip_session_media *session_media;
		struct ast_stream *stream;

		if (!remote->media[i]) {
			continue;
		}

		session_media = AST_VECTOR_GET(&session->pending_media_state->sessions, i);
		stream = ast_stream_topology_get_stream(session->pending_media_state->topology, i);

		if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED
			&& session_media->handler) {
			/*
			 * This stream is no longer being used and the channel's thread
			 * is held off because we have the channel lock so release any
			 * resources the handler may have on it.
			 */
			session_media_set_handler(session_media, NULL);
		}
	}

	/* Update the topology on the channel to match the accepted one */
	topology = ast_stream_topology_clone(session->pending_media_state->topology);
	if (topology) {
		ast_channel_set_stream_topology(session->channel, topology);
		/* If this is a remotely done renegotiation that has changed the stream topology notify what is
		 * currently handling this channel. Note that fax uses its own process, so if we are transitioning
		 * between audio and fax or vice versa we don't notify.
		 */
		if (pjmedia_sdp_neg_was_answer_remote(session->inv_session->neg) == PJ_FALSE &&
			session->active_media_state && session->active_media_state->topology &&
			!ast_stream_topology_equal(session->active_media_state->topology, topology) &&
			!session->active_media_state->default_session[AST_MEDIA_TYPE_IMAGE] &&
			!session->pending_media_state->default_session[AST_MEDIA_TYPE_IMAGE]) {
			changed = 2;
		}
	}

	/* Remove all current file descriptors from the channel */
	for (i = 0; i < AST_VECTOR_SIZE(&session->active_media_state->read_callbacks); ++i) {
		ast_channel_internal_fd_clear(session->channel, i + AST_EXTENDED_FDS);
	}

	/* Add all the file descriptors from the pending media state */
	for (i = 0; i < AST_VECTOR_SIZE(&session->pending_media_state->read_callbacks); ++i) {
		struct ast_sip_session_media_read_callback_state *callback_state;

		callback_state = AST_VECTOR_GET_ADDR(&session->pending_media_state->read_callbacks, i);
		ast_channel_internal_fd_set(session->channel, i + AST_EXTENDED_FDS, callback_state->fd);
	}

	/* Active and pending flip flop as needed */
	ast_sip_session_media_stats_save(session, session->active_media_state);
	SWAP(session->active_media_state, session->pending_media_state);
	ast_sip_session_media_state_reset(session->pending_media_state);

	ast_channel_unlock(session->channel);

	if (changed == 1) {
		struct ast_frame f = { AST_FRAME_CONTROL, .subclass.integer = AST_CONTROL_STREAM_TOPOLOGY_SOURCE_CHANGED };

		ast_queue_frame(session->channel, &f);
	} else if (changed == 2) {
		ast_channel_stream_topology_changed_externally(session->channel);
	} else {
		ast_queue_frame(session->channel, &ast_null_frame);
	}

	SCOPE_EXIT_RTN_VALUE(0);
}

#define DATASTORE_BUCKETS 53
#define MEDIA_BUCKETS 7

static void session_datastore_destroy(void *obj)
{
	struct ast_datastore *datastore = obj;

	/* Using the destroy function (if present) destroy the data */
	if (datastore->info->destroy != NULL && datastore->data != NULL) {
		datastore->info->destroy(datastore->data);
		datastore->data = NULL;
	}

	ast_free((void *) datastore->uid);
	datastore->uid = NULL;
}

struct ast_datastore *ast_sip_session_alloc_datastore(const struct ast_datastore_info *info, const char *uid)
{
	RAII_VAR(struct ast_datastore *, datastore, NULL, ao2_cleanup);
	char uuid_buf[AST_UUID_STR_LEN];
	const char *uid_ptr = uid;

	if (!info) {
		return NULL;
	}

	datastore = ao2_alloc(sizeof(*datastore), session_datastore_destroy);
	if (!datastore) {
		return NULL;
	}

	datastore->info = info;
	if (ast_strlen_zero(uid)) {
		/* They didn't provide an ID so we'll provide one ourself */
		uid_ptr = ast_uuid_generate_str(uuid_buf, sizeof(uuid_buf));
	}

	datastore->uid = ast_strdup(uid_ptr);
	if (!datastore->uid) {
		return NULL;
	}

	ao2_ref(datastore, +1);
	return datastore;
}

int ast_sip_session_add_datastore(struct ast_sip_session *session, struct ast_datastore *datastore)
{
	ast_assert(datastore != NULL);
	ast_assert(datastore->info != NULL);
	ast_assert(ast_strlen_zero(datastore->uid) == 0);

	if (!ao2_link(session->datastores, datastore)) {
		return -1;
	}
	return 0;
}

struct ast_datastore *ast_sip_session_get_datastore(struct ast_sip_session *session, const char *name)
{
	return ao2_find(session->datastores, name, OBJ_KEY);
}

void ast_sip_session_remove_datastore(struct ast_sip_session *session, const char *name)
{
	ao2_callback(session->datastores, OBJ_KEY | OBJ_UNLINK | OBJ_NODATA, NULL, (void *) name);
}

enum delayed_method {
	DELAYED_METHOD_INVITE,
	DELAYED_METHOD_UPDATE,
	DELAYED_METHOD_BYE,
};

/*!
 * \internal
 * \brief Convert delayed method enum value to a string.
 * \since 13.3.0
 *
 * \param method Delayed method enum value to convert to a string.
 *
 * \return String value of delayed method.
 */
static const char *delayed_method2str(enum delayed_method method)
{
	const char *str = "<unknown>";

	switch (method) {
	case DELAYED_METHOD_INVITE:
		str = "INVITE";
		break;
	case DELAYED_METHOD_UPDATE:
		str = "UPDATE";
		break;
	case DELAYED_METHOD_BYE:
		str = "BYE";
		break;
	}

	return str;
}

/*!
 * \brief Structure used for sending delayed requests
 *
 * Requests are typically delayed because the current transaction
 * state of an INVITE. Once the pending INVITE transaction terminates,
 * the delayed request will be sent
 */
struct ast_sip_session_delayed_request {
	/*! Method of the request */
	enum delayed_method method;
	/*! Callback to call when the delayed request is created. */
	ast_sip_session_request_creation_cb on_request_creation;
	/*! Callback to call when the delayed request SDP is created */
	ast_sip_session_sdp_creation_cb on_sdp_creation;
	/*! Callback to call when the delayed request receives a response */
	ast_sip_session_response_cb on_response;
	/*! Whether to generate new SDP */
	int generate_new_sdp;
	/*! Requested media state for the SDP */
	struct ast_sip_session_media_state *pending_media_state;
	/*! Active media state at the time of the original request */
	struct ast_sip_session_media_state *active_media_state;

	AST_LIST_ENTRY(ast_sip_session_delayed_request) next;
};

static struct ast_sip_session_delayed_request *delayed_request_alloc(
	enum delayed_method method,
	ast_sip_session_request_creation_cb on_request_creation,
	ast_sip_session_sdp_creation_cb on_sdp_creation,
	ast_sip_session_response_cb on_response,
	int generate_new_sdp,
	struct ast_sip_session_media_state *pending_media_state,
	struct ast_sip_session_media_state *active_media_state)
{
	struct ast_sip_session_delayed_request *delay = ast_calloc(1, sizeof(*delay));

	if (!delay) {
		return NULL;
	}
	delay->method = method;
	delay->on_request_creation = on_request_creation;
	delay->on_sdp_creation = on_sdp_creation;
	delay->on_response = on_response;
	delay->generate_new_sdp = generate_new_sdp;
	delay->pending_media_state = pending_media_state;
	delay->active_media_state = active_media_state;
	return delay;
}

static void delayed_request_free(struct ast_sip_session_delayed_request *delay)
{
	ast_sip_session_media_state_free(delay->pending_media_state);
	ast_sip_session_media_state_free(delay->active_media_state);
	ast_free(delay);
}

/*!
 * \internal
 * \brief Send a delayed request
 *
 * \retval -1 failure
 * \retval 0 success
 * \retval 1 refresh request not sent as no change would occur
 */
static int send_delayed_request(struct ast_sip_session *session, struct ast_sip_session_delayed_request *delay)
{
	int res;
	SCOPE_ENTER(3, "%s: sending delayed %s request\n",
		ast_sip_session_get_name(session),
		delayed_method2str(delay->method));

	switch (delay->method) {
	case DELAYED_METHOD_INVITE:
		res = sip_session_refresh(session, delay->on_request_creation,
			delay->on_sdp_creation, delay->on_response,
			AST_SIP_SESSION_REFRESH_METHOD_INVITE, delay->generate_new_sdp, delay->pending_media_state,
			delay->active_media_state, 1);
		/* Ownership of media state transitions to ast_sip_session_refresh */
		delay->pending_media_state = NULL;
		delay->active_media_state = NULL;
		SCOPE_EXIT_RTN_VALUE(res, "%s\n", ast_sip_session_get_name(session));
	case DELAYED_METHOD_UPDATE:
		res = sip_session_refresh(session, delay->on_request_creation,
			delay->on_sdp_creation, delay->on_response,
			AST_SIP_SESSION_REFRESH_METHOD_UPDATE, delay->generate_new_sdp, delay->pending_media_state,
			delay->active_media_state, 1);
		/* Ownership of media state transitions to ast_sip_session_refresh */
		delay->pending_media_state = NULL;
		delay->active_media_state = NULL;
		SCOPE_EXIT_RTN_VALUE(res, "%s\n", ast_sip_session_get_name(session));
	case DELAYED_METHOD_BYE:
		ast_sip_session_terminate(session, 0);
		SCOPE_EXIT_RTN_VALUE(0, "%s: Terminating session on delayed BYE\n", ast_sip_session_get_name(session));
	}

	SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: Don't know how to send delayed %s(%d) request.\n",
		ast_sip_session_get_name(session),
		delayed_method2str(delay->method), delay->method);
}

/*!
 * \internal
 * \brief The current INVITE transaction is in the PROCEEDING state.
 * \since 13.3.0
 *
 * \param vsession Session object.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int invite_proceeding(void *vsession)
{
	struct ast_sip_session *session = vsession;
	struct ast_sip_session_delayed_request *delay;
	int found = 0;
	int res = 0;
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	AST_LIST_TRAVERSE_SAFE_BEGIN(&session->delayed_requests, delay, next) {
		switch (delay->method) {
		case DELAYED_METHOD_INVITE:
			break;
		case DELAYED_METHOD_UPDATE:
			AST_LIST_REMOVE_CURRENT(next);
			ast_trace(-1, "%s: Sending delayed %s request\n", ast_sip_session_get_name(session),
				delayed_method2str(delay->method));
			res = send_delayed_request(session, delay);
			delayed_request_free(delay);
			if (!res) {
				found = 1;
			}
			break;
		case DELAYED_METHOD_BYE:
			/* A BYE is pending so don't bother anymore. */
			found = 1;
			break;
		}
		if (found) {
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	ao2_ref(session, -1);
	SCOPE_EXIT_RTN_VALUE(res, "%s\n", ast_sip_session_get_name(session));
}

/*!
 * \internal
 * \brief The current INVITE transaction is in the TERMINATED state.
 * \since 13.3.0
 *
 * \param vsession Session object.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int invite_terminated(void *vsession)
{
	struct ast_sip_session *session = vsession;
	struct ast_sip_session_delayed_request *delay;
	int found = 0;
	int res = 0;
	int timer_running;
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	/* re-INVITE collision timer running? */
	timer_running = pj_timer_entry_running(&session->rescheduled_reinvite);

	AST_LIST_TRAVERSE_SAFE_BEGIN(&session->delayed_requests, delay, next) {
		switch (delay->method) {
		case DELAYED_METHOD_INVITE:
			if (!timer_running) {
				found = 1;
			}
			break;
		case DELAYED_METHOD_UPDATE:
		case DELAYED_METHOD_BYE:
			found = 1;
			break;
		}
		if (found) {
			AST_LIST_REMOVE_CURRENT(next);
			ast_trace(-1, "%s: Sending delayed %s request\n", ast_sip_session_get_name(session),
				delayed_method2str(delay->method));
			res = send_delayed_request(session, delay);
			delayed_request_free(delay);
			if (!res) {
				break;
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	ao2_ref(session, -1);
	SCOPE_EXIT_RTN_VALUE(res, "%s\n", ast_sip_session_get_name(session));
}

/*!
 * \internal
 * \brief INVITE collision timeout.
 * \since 13.3.0
 *
 * \param vsession Session object.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int invite_collision_timeout(void *vsession)
{
	struct ast_sip_session *session = vsession;
	int res;
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	if (session->inv_session->invite_tsx) {
		/*
		 * INVITE transaction still active.  Let it send
		 * the collision re-INVITE when it terminates.
		 */
		ao2_ref(session, -1);
		res = 0;
	} else {
		res = invite_terminated(session);
	}

	SCOPE_EXIT_RTN_VALUE(res, "%s\n", ast_sip_session_get_name(session));
}

/*!
 * \internal
 * \brief The current UPDATE transaction is in the COMPLETED state.
 * \since 13.3.0
 *
 * \param vsession Session object.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int update_completed(void *vsession)
{
	struct ast_sip_session *session = vsession;
	int res;

	if (session->inv_session->invite_tsx) {
		res = invite_proceeding(session);
	} else {
		res = invite_terminated(session);
	}

	return res;
}

static void check_delayed_requests(struct ast_sip_session *session,
	int (*cb)(void *vsession))
{
	ao2_ref(session, +1);
	if (ast_sip_push_task(session->serializer, cb, session)) {
		ao2_ref(session, -1);
	}
}

static int delay_request(struct ast_sip_session *session,
	ast_sip_session_request_creation_cb on_request,
	ast_sip_session_sdp_creation_cb on_sdp_creation,
	ast_sip_session_response_cb on_response,
	int generate_new_sdp,
	enum delayed_method method,
	struct ast_sip_session_media_state *pending_media_state,
	struct ast_sip_session_media_state *active_media_state,
	int queue_head)
{
	struct ast_sip_session_delayed_request *delay = delayed_request_alloc(method,
			on_request, on_sdp_creation, on_response, generate_new_sdp, pending_media_state,
			active_media_state);
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	if (!delay) {
		ast_sip_session_media_state_free(pending_media_state);
		ast_sip_session_media_state_free(active_media_state);
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_ERROR, "Unable to allocate delay request\n");
	}

	if (method == DELAYED_METHOD_BYE || queue_head) {
		/* Send BYE as early as possible */
		AST_LIST_INSERT_HEAD(&session->delayed_requests, delay, next);
	} else {
		AST_LIST_INSERT_TAIL(&session->delayed_requests, delay, next);
	}
	SCOPE_EXIT_RTN_VALUE(0);
}

static pjmedia_sdp_session *generate_session_refresh_sdp(struct ast_sip_session *session)
{
	pjsip_inv_session *inv_session = session->inv_session;
	const pjmedia_sdp_session *previous_sdp = NULL;
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	if (inv_session->neg) {
		if (pjmedia_sdp_neg_was_answer_remote(inv_session->neg)) {
			pjmedia_sdp_neg_get_active_remote(inv_session->neg, &previous_sdp);
		} else {
			pjmedia_sdp_neg_get_active_local(inv_session->neg, &previous_sdp);
		}
	}
	SCOPE_EXIT_RTN_VALUE(create_local_sdp(inv_session, session, previous_sdp));
}

static void set_from_header(struct ast_sip_session *session)
{
	struct ast_party_id effective_id;
	struct ast_party_id connected_id;
	pj_pool_t *dlg_pool;
	pjsip_fromto_hdr *dlg_info;
	pjsip_contact_hdr *dlg_contact;
	pjsip_name_addr *dlg_info_name_addr;
	pjsip_sip_uri *dlg_info_uri;
	pjsip_sip_uri *dlg_contact_uri;
	int restricted;
	const char *pjsip_from_domain;

	if (!session->channel || session->saved_from_hdr) {
		return;
	}

	/* We need to save off connected_id for RPID/PAI generation */
	ast_party_id_init(&connected_id);
	ast_channel_lock(session->channel);
	effective_id = ast_channel_connected_effective_id(session->channel);
	ast_party_id_copy(&connected_id, &effective_id);
	ast_channel_unlock(session->channel);

	restricted =
		((ast_party_id_presentation(&connected_id) & AST_PRES_RESTRICTION) != AST_PRES_ALLOWED);

	/* Now set up dlg->local.info so pjsip can correctly generate From */

	dlg_pool = session->inv_session->dlg->pool;
	dlg_info = session->inv_session->dlg->local.info;
	dlg_contact = session->inv_session->dlg->local.contact;
	dlg_info_name_addr = (pjsip_name_addr *) dlg_info->uri;
	dlg_info_uri = pjsip_uri_get_uri(dlg_info_name_addr);
	dlg_contact_uri = (pjsip_sip_uri*)pjsip_uri_get_uri(dlg_contact->uri);

	if (session->endpoint->id.trust_outbound || !restricted) {
		ast_sip_modify_id_header(dlg_pool, dlg_info, &connected_id);
		if (ast_sip_get_use_callerid_contact() && ast_strlen_zero(session->endpoint->contact_user)) {
			pj_strdup2(dlg_pool, &dlg_contact_uri->user, S_COR(connected_id.number.valid, connected_id.number.str, ""));
		}
	}

	ast_party_id_free(&connected_id);

	if (!ast_strlen_zero(session->endpoint->fromuser)) {
		dlg_info_name_addr->display.ptr = NULL;
		dlg_info_name_addr->display.slen = 0;
		pj_strdup2(dlg_pool, &dlg_info_uri->user, session->endpoint->fromuser);
	}

	if (!ast_strlen_zero(session->endpoint->fromdomain)) {
		pj_strdup2(dlg_pool, &dlg_info_uri->host, session->endpoint->fromdomain);
	}

	/*
	 * Channel variable for compatibility with chan_sip SIPFROMDOMAIN
	 */
	ast_channel_lock(session->channel);
	pjsip_from_domain = pbx_builtin_getvar_helper(session->channel, "SIPFROMDOMAIN");
	if (!ast_strlen_zero(pjsip_from_domain)) {
		ast_debug(3, "%s: From header domain reset by channel variable SIPFROMDOMAIN (%s)\n",
			ast_sip_session_get_name(session), pjsip_from_domain);
		pj_strdup2(dlg_pool, &dlg_info_uri->host, pjsip_from_domain);
	}
	ast_channel_unlock(session->channel);

	/* We need to save off the non-anonymized From for RPID/PAI generation (for domain) */
	session->saved_from_hdr = pjsip_hdr_clone(dlg_pool, dlg_info);
	ast_sip_add_usereqphone(session->endpoint, dlg_pool, session->saved_from_hdr->uri);

	/* In chan_sip, fromuser and fromdomain trump restricted so we only
	 * anonymize if they're not set.
	 */
	if (restricted) {
		/* fromuser doesn't provide a display name so we always set it */
		pj_strdup2(dlg_pool, &dlg_info_name_addr->display, "Anonymous");

		if (ast_strlen_zero(session->endpoint->fromuser)) {
			pj_strdup2(dlg_pool, &dlg_info_uri->user, "anonymous");
		}

		if (ast_sip_get_use_callerid_contact() && ast_strlen_zero(session->endpoint->contact_user)) {
			pj_strdup2(dlg_pool, &dlg_contact_uri->user, "anonymous");
		}

		if (ast_strlen_zero(session->endpoint->fromdomain)) {
			pj_strdup2(dlg_pool, &dlg_info_uri->host, "anonymous.invalid");
		}
	} else {
		ast_sip_add_usereqphone(session->endpoint, dlg_pool, dlg_info->uri);
    }
}

/*
 * Helper macros for merging and validating media states
 */
#define STREAM_REMOVED(_stream) (ast_stream_get_state(_stream) == AST_STREAM_STATE_REMOVED)
#define STATE_REMOVED(_stream_state) (_stream_state == AST_STREAM_STATE_REMOVED)
#define STATE_NONE(_stream_state) (_stream_state == AST_STREAM_STATE_END)
#define GET_STREAM_SAFE(_topology, _i) (_i < ast_stream_topology_get_count(_topology) ? ast_stream_topology_get_stream(_topology, _i) : NULL)
#define GET_STREAM_STATE_SAFE(_stream) (_stream ? ast_stream_get_state(_stream) : AST_STREAM_STATE_END)
#define GET_STREAM_NAME_SAFE(_stream) (_stream ? ast_stream_get_name(_stream) : "")

/*!
 * \internal
 * \brief Validate a media state
 *
 * \param session_name For log messages
 * \param state Media state
 *
 * \retval 1 The media state is valid
 * \retval 0 The media state is NOT valid
 *
 */
static int is_media_state_valid(const char *session_name, struct ast_sip_session_media_state *state)
{
	int stream_count = ast_stream_topology_get_count(state->topology);
	int session_count = AST_VECTOR_SIZE(&state->sessions);
	int i;
	int res = 0;
	SCOPE_ENTER(3, "%s: Topology: %s\n", session_name,
		ast_str_tmp(256, ast_stream_topology_to_str(state->topology, &STR_TMP)));

	if (session_count != stream_count) {
		SCOPE_EXIT_RTN_VALUE(0, "%s: %d media sessions but %d streams\n", session_name,
			session_count, stream_count);
	}

	for (i = 0; i < stream_count; i++) {
		struct ast_sip_session_media *media = NULL;
		struct ast_stream *stream = ast_stream_topology_get_stream(state->topology, i);
		const char *stream_name = NULL;
		int j;
		SCOPE_ENTER(4, "%s: Checking stream %s\n", session_name, ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));

		if (!stream) {
			SCOPE_EXIT_EXPR(goto end, "%s: stream %d is null\n", session_name, i);
		}
		stream_name = ast_stream_get_name(stream);

		for (j = 0; j < stream_count; j++) {
			struct ast_stream *possible_dup = ast_stream_topology_get_stream(state->topology, j);
			if (j == i || !possible_dup) {
				continue;
			}
			if (!STREAM_REMOVED(stream) && ast_strings_equal(stream_name, GET_STREAM_NAME_SAFE(possible_dup))) {
				SCOPE_EXIT_EXPR(goto end, "%s: stream %i %s is duplicated to %d\n", session_name,
					i, stream_name, j);
			}
		}

		media = AST_VECTOR_GET(&state->sessions, i);
		if (!media) {
			SCOPE_EXIT_EXPR(continue, "%s: media %d is null\n", session_name, i);
		}

		for (j = 0; j < session_count; j++) {
			struct ast_sip_session_media *possible_dup = AST_VECTOR_GET(&state->sessions, j);
			if (j == i || !possible_dup) {
				continue;
			}
			if (!ast_strlen_zero(media->label) && !ast_strlen_zero(possible_dup->label)
				&& ast_strings_equal(media->label, possible_dup->label)) {
				SCOPE_EXIT_EXPR(goto end, "%s: media %d %s is duplicated to %d\n", session_name,
					i, media->label, j);
			}
		}

		if (media->stream_num != i) {
			SCOPE_EXIT_EXPR(goto end, "%s: media %d has stream_num %d\n", session_name,
				i, media->stream_num);
		}

		if (media->type != ast_stream_get_type(stream)) {
			SCOPE_EXIT_EXPR(goto end, "%s: media %d has type %s but stream has type %s\n", stream_name,
				i, ast_codec_media_type2str(media->type), ast_codec_media_type2str(ast_stream_get_type(stream)));
		}
		SCOPE_EXIT("%s: Done with stream %s\n", session_name, ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
	}

	res = 1;
end:
	SCOPE_EXIT_RTN_VALUE(res, "%s: %s\n", session_name, res ? "Valid" : "NOT Valid");
}

/*!
 * \internal
 * \brief Merge media states for a delayed session refresh
 *
 * \param session_name For log messages
 * \param delayed_pending_state The pending media state at the time the resuest was queued
 * \param delayed_active_state The active media state  at the time the resuest was queued
 * \param current_active_state The current active media state
 * \param run_post_validation Whether to run validation on the resulting media state or not
 *
 * \returns New merged topology or NULL if there's an error
 *
 */
static struct ast_sip_session_media_state *resolve_refresh_media_states(
	const char *session_name,
	struct ast_sip_session_media_state *delayed_pending_state,
	struct ast_sip_session_media_state *delayed_active_state,
	struct ast_sip_session_media_state *current_active_state,
	int run_post_validation)
{
	RAII_VAR(struct ast_sip_session_media_state *, new_pending_state, NULL, ast_sip_session_media_state_free);
	struct ast_sip_session_media_state *returned_media_state = NULL;
	struct ast_stream_topology *delayed_pending = delayed_pending_state->topology;
	struct ast_stream_topology *delayed_active = delayed_active_state->topology;
	struct ast_stream_topology *current_active = current_active_state->topology;
	struct ast_stream_topology *new_pending = NULL;
	int i;
	int max_stream_count;
	int res;
	SCOPE_ENTER(2, "%s: DP: %s  DA: %s  CA: %s\n", session_name,
		ast_str_tmp(256, ast_stream_topology_to_str(delayed_pending, &STR_TMP)),
		ast_str_tmp(256, ast_stream_topology_to_str(delayed_active, &STR_TMP)),
		ast_str_tmp(256, ast_stream_topology_to_str(current_active, &STR_TMP))
	);

	max_stream_count = MAX(ast_stream_topology_get_count(delayed_pending),
		ast_stream_topology_get_count(delayed_active));
	max_stream_count = MAX(max_stream_count, ast_stream_topology_get_count(current_active));

	/*
	 * The new_pending_state is always based on the currently negotiated state because
	 * the stream ordering in its topology must be preserved.
	 */
	new_pending_state = ast_sip_session_media_state_clone(current_active_state);
	if (!new_pending_state) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Couldn't clone current_active_state to new_pending_state\n", session_name);
	}
	new_pending = new_pending_state->topology;

	for (i = 0; i < max_stream_count; i++) {
		struct ast_stream *dp_stream = GET_STREAM_SAFE(delayed_pending, i);
		struct ast_stream *da_stream = GET_STREAM_SAFE(delayed_active, i);
		struct ast_stream *ca_stream = GET_STREAM_SAFE(current_active, i);
		struct ast_stream *np_stream = GET_STREAM_SAFE(new_pending, i);
		struct ast_stream *found_da_stream = NULL;
		struct ast_stream *found_np_stream = NULL;
		enum ast_stream_state dp_state = GET_STREAM_STATE_SAFE(dp_stream);
		enum ast_stream_state da_state = GET_STREAM_STATE_SAFE(da_stream);
		enum ast_stream_state ca_state = GET_STREAM_STATE_SAFE(ca_stream);
		enum ast_stream_state np_state = GET_STREAM_STATE_SAFE(np_stream);
		enum ast_stream_state found_da_state = AST_STREAM_STATE_END;
		enum ast_stream_state found_np_state = AST_STREAM_STATE_END;
		const char *da_name = GET_STREAM_NAME_SAFE(da_stream);
		const char *dp_name = GET_STREAM_NAME_SAFE(dp_stream);
		const char *ca_name = GET_STREAM_NAME_SAFE(ca_stream);
		const char *np_name = GET_STREAM_NAME_SAFE(np_stream);
		const char *found_da_name __attribute__((unused)) = "";
		const char *found_np_name __attribute__((unused)) = "";
		int found_da_slot __attribute__((unused)) = -1;
		int found_np_slot = -1;
		int removed_np_slot = -1;
		int j;
		SCOPE_ENTER(3, "%s: slot: %d DP: %s  DA: %s  CA: %s\n", session_name, i,
			ast_str_tmp(128, ast_stream_to_str(dp_stream, &STR_TMP)),
			ast_str_tmp(128, ast_stream_to_str(da_stream, &STR_TMP)),
			ast_str_tmp(128, ast_stream_to_str(ca_stream, &STR_TMP)));

		if (STATE_NONE(da_state) && STATE_NONE(dp_state) && STATE_NONE(ca_state)) {
			SCOPE_EXIT_EXPR(break, "%s: All gone\n", session_name);
		}

		/*
		 * Simple cases are handled first to avoid having to search the NP and DA
		 * topologies for streams with the same name but not in the same position.
		 */

		if (STATE_NONE(dp_state) && !STATE_NONE(da_state)) {
		    /*
		     * The slot in the delayed pending topology can't be empty if the delayed
		     * active topology has a stream there.  Streams can't just go away.  They
		     * can be reused or marked "removed" but they can't go away.
		     */
		    SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING, "%s: DP slot is empty but DA is not\n", session_name);
		}

		if (STATE_NONE(dp_state)) {
		    /*
		     * The current active topology can certainly have streams that weren't
		     * in existence when the delayed request was queued.  In this case,
		     * no action is needed since we already copied the current active topology
		     * to the new pending one.
		     */
		    SCOPE_EXIT_EXPR(continue, "%s: No DP stream so use CA stream as is\n", session_name);
		}

		if (ast_strings_equal(dp_name, da_name) && ast_strings_equal(da_name, ca_name)) {
			/*
			 * The delayed pending stream in this slot matches by name, the streams
			 * in the same slot in the other two topologies.  Easy case.
			 */
			ast_trace(-1, "%s: Same stream in all 3 states\n", session_name);
			if (dp_state == da_state && da_state == ca_state) {
				/* All the same state, no need to update. */
				SCOPE_EXIT_EXPR(continue, "%s: All in the same state so nothing to do\n", session_name);
			}
			if (da_state != ca_state) {
				/*
				 * Something set the CA state between the time this request was queued
				 * and now.  The CA state wins so we don't do anything.
				 */
				SCOPE_EXIT_EXPR(continue, "%s: Ignoring request to change state from %s to %s\n",
					session_name, ast_stream_state2str(ca_state), ast_stream_state2str(dp_state));
			}
			if (dp_state != da_state) {
				/* DP needs to update the state */
				ast_stream_set_state(np_stream, dp_state);
				SCOPE_EXIT_EXPR(continue, "%s: Changed NP stream state from %s to %s\n",
					session_name, ast_stream_state2str(ca_state), ast_stream_state2str(dp_state));
			}
		}

		/*
		 * We're done with the simple cases.  For the rest, we need to identify if the
		 * DP stream we're trying to take action on is already in the other topologies
		 * possibly in a different slot.  To do that, if the stream in the DA or CA slots
		 * doesn't match the current DP stream, we need to iterate over the topology
		 * looking for a stream with the same name.
		 */

		/*
		 * Since we already copied all of the CA streams to the NP topology, we'll use it
		 * instead of CA because we'll be updating the NP as we go.
		 */
		if (!ast_strings_equal(dp_name, np_name)) {
			/*
			 * The NP stream in this slot doesn't have the same name as the DP stream
			 * so we need to see if it's in another NP slot.  We're not going to stop
			 * when we find a matching stream because we also want to find the first
			 * removed removed slot, if any, so we can re-use this slot.  We'll break
			 * early if we find both before we reach the end.
			 */
			ast_trace(-1, "%s: Checking if DP is already in NP somewhere\n", session_name);
			for (j = 0; j < ast_stream_topology_get_count(new_pending); j++) {
				struct ast_stream *possible_existing = ast_stream_topology_get_stream(new_pending, j);
				const char *possible_existing_name = GET_STREAM_NAME_SAFE(possible_existing);

				ast_trace(-1, "%s: Checking %s against %s\n", session_name, dp_name, possible_existing_name);
				if (found_np_slot == -1 && ast_strings_equal(dp_name, possible_existing_name)) {
					ast_trace(-1, "%s: Pending stream %s slot %d is in NP slot %d\n", session_name,
					dp_name, i, j);
					found_np_slot = j;
					found_np_stream = possible_existing;
					found_np_state = ast_stream_get_state(possible_existing);
					found_np_name = ast_stream_get_name(possible_existing);
				}
				if (STREAM_REMOVED(possible_existing) && removed_np_slot == -1) {
					removed_np_slot = j;
				}
				if (removed_np_slot >= 0 && found_np_slot >= 0) {
					break;
				}
			}
		} else {
			/* Makes the subsequent code easier */
			found_np_slot = i;
			found_np_stream = np_stream;
			found_np_state = np_state;
			found_np_name = np_name;
		}

		if (!ast_strings_equal(dp_name, da_name)) {
			/*
			 * The DA stream in this slot doesn't have the same name as the DP stream
			 * so we need to see if it's in another DA slot.  In real life, the DA stream
			 * in this slot could have a different name but there shouldn't be a case
			 * where the DP stream is another slot in the DA topology.  Just in case though.
			 * We don't care about removed slots in the DA topology.
			 */
			ast_trace(-1, "%s: Checking if DP is already in DA somewhere\n", session_name);
			for (j = 0; j < ast_stream_topology_get_count(delayed_active); j++) {
				struct ast_stream *possible_existing = ast_stream_topology_get_stream(delayed_active, j);
				const char *possible_existing_name = GET_STREAM_NAME_SAFE(possible_existing);

				ast_trace(-1, "%s: Checking %s against %s\n", session_name, dp_name, possible_existing_name);
				if (ast_strings_equal(dp_name, possible_existing_name)) {
					ast_trace(-1, "%s: Pending stream %s slot %d is already in delayed active slot %d\n",
						session_name, dp_name, i, j);
					found_da_slot = j;
					found_da_stream = possible_existing;
					found_da_state = ast_stream_get_state(possible_existing);
					found_da_name = ast_stream_get_name(possible_existing);
					break;
				}
			}
		} else {
			/* Makes the subsequent code easier */
			found_da_slot = i;
			found_da_stream = da_stream;
			found_da_state = da_state;
			found_da_name = da_name;
		}

		ast_trace(-1, "%s: Found NP slot: %d  Found removed NP slot: %d Found DA slot: %d\n",
			session_name, found_np_slot, removed_np_slot, found_da_slot);

		/*
		 * Now we know whether the DP stream is new or changing state and we know if the DP
		 * stream exists in the other topologies and if so, where in those topologies it exists.
		 */

		if (!found_da_stream) {
			/*
			 * The DP stream isn't in the DA topology which would imply that the intention of the
			 * request was to add the stream, not change its state.  It's possible though that
			 * the stream was added by another request between the time this request was queued
			 * and now so we need to check the CA topology as well.
			 */
			ast_trace(-1, "%s: There was no corresponding DA stream so the request was to add a stream\n", session_name);

			if (found_np_stream) {
				/*
				 * We found it in the CA topology.  Since the intention was to add it
				 * and it's already there, there's nothing to do.
				 */
				SCOPE_EXIT_EXPR(continue, "%s: New stream requested but it's already in CA\n", session_name);
			} else {
				/* OK, it's not in either which would again imply that the intention of the
				 * request was to add the stream.
				 */
				ast_trace(-1, "%s: There was no corresponding NP stream\n", session_name);
				if (STATE_REMOVED(dp_state)) {
					/*
					 * How can DP request to remove a stream that doesn't seem to exist anythere?
					 * It's not.  It's possible that the stream was already removed and the slot
					 * reused in the CA topology, but it would still have to exist in the DA
					 * topology.  Bail.
					 */
					SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR,
						"%s: Attempting to remove stream %d:%s but it doesn't exist anywhere.\n", session_name, i, dp_name);
				} else {
					/*
					 * We're now sure we want to add the the stream.  Since we can re-use
					 * slots in the CA topology that have streams marked as "removed", we
					 * use the slot we saved in removed_np_slot if it exists.
					 */
					ast_trace(-1, "%s: Checking for open slot\n", session_name);
					if (removed_np_slot >= 0) {
						struct ast_sip_session_media *old_media = AST_VECTOR_GET(&new_pending_state->sessions, removed_np_slot);
						res = ast_stream_topology_set_stream(new_pending, removed_np_slot, ast_stream_clone(dp_stream, NULL));
						if (res != 0) {
						    SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING, "%s: Couldn't set stream in new topology\n", session_name);
						}
						/*
						 * Since we're reusing the removed_np_slot slot for something else, we need
						 * to free and remove any session media already in it.
						 * ast_stream_topology_set_stream() took care of freeing the old stream.
						 */
						res = AST_VECTOR_REPLACE(&new_pending_state->sessions, removed_np_slot, NULL);
						if (res != 0) {
						    SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING, "%s: Couldn't replace media session\n", session_name);
						}

						ao2_cleanup(old_media);
						SCOPE_EXIT_EXPR(continue, "%s: Replaced removed stream in slot %d\n",
							session_name, removed_np_slot);
					} else {
						int new_slot = ast_stream_topology_append_stream(new_pending, ast_stream_clone(dp_stream, NULL));
						if (new_slot < 0) {
						    SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING, "%s: Couldn't append stream in new topology\n", session_name);
						}

						res = AST_VECTOR_REPLACE(&new_pending_state->sessions, new_slot, NULL);
						if (res != 0) {
						    SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_WARNING, "%s: Couldn't replace media session\n", session_name);
						}
						SCOPE_EXIT_EXPR(continue, "%s: Appended new stream to slot %d\n",
							session_name, new_slot);
					}
				}
			}
		} else {
			/*
			 * The DP stream exists in the DA topology so it's a change of some sort.
			 */
			ast_trace(-1, "%s: There was a corresponding DA stream so the request was to change/remove a stream\n", session_name);
			if (dp_state == found_da_state) {
				/* No change? Let's see if it's in CA */
				if (!found_np_stream) {
					/*
					 * The DP and DA state are the same which would imply that the stream
					 * already exists but it's not in the CA topology.  It's possible that
					 * between the time this request was queued and now the stream was removed
					 * from the CA topology and the slot used for something else.  Nothing
					 * we can do here.
					 */
					SCOPE_EXIT_EXPR(continue, "%s: Stream doesn't exist in CA so nothing to do\n", session_name);
				} else if (dp_state == found_np_state) {
					SCOPE_EXIT_EXPR(continue, "%s: States are the same all around so nothing to do\n", session_name);
				} else {
					SCOPE_EXIT_EXPR(continue, "%s: Something changed the CA state so we're going to leave it as is\n", session_name);
				}
			} else {
				/* We have a state change. */
				ast_trace(-1, "%s: Requesting state change to %s\n", session_name, ast_stream_state2str(dp_state));
				if (!found_np_stream) {
					SCOPE_EXIT_EXPR(continue, "%s: Stream doesn't exist in CA so nothing to do\n", session_name);
				} else if (da_state == found_np_state) {
					ast_stream_set_state(found_np_stream, dp_state);
					SCOPE_EXIT_EXPR(continue, "%s: Changed NP stream state from %s to %s\n",
						session_name, ast_stream_state2str(found_np_state), ast_stream_state2str(dp_state));
				} else {
					SCOPE_EXIT_EXPR(continue, "%s: Something changed the CA state so we're going to leave it as is\n",
						session_name);
				}
			}
		}

		SCOPE_EXIT("%s: Done with slot %d\n", session_name, i);
	}

	ast_trace(-1, "%s: Resetting default media states\n", session_name);
	for (i = 0; i < AST_MEDIA_TYPE_END; i++) {
		int j;
		new_pending_state->default_session[i] = NULL;
		for (j = 0; j < AST_VECTOR_SIZE(&new_pending_state->sessions); j++) {
			struct ast_sip_session_media *media = AST_VECTOR_GET(&new_pending_state->sessions, j);
			struct ast_stream *stream = ast_stream_topology_get_stream(new_pending_state->topology, j);

			if (media && media->type == i && !STREAM_REMOVED(stream)) {
				new_pending_state->default_session[i] = media;
				break;
			}
		}
	}

	if (run_post_validation) {
		ast_trace(-1, "%s: Running post-validation\n", session_name);
		if (!is_media_state_valid(session_name, new_pending_state)) {
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "State not consistent\n");
		}
	}

	/*
	 * We need to move the new pending state to another variable and set new_pending_state to NULL
	 * so RAII_VAR doesn't free it.
	 */
	returned_media_state = new_pending_state;
	new_pending_state = NULL;
	SCOPE_EXIT_RTN_VALUE(returned_media_state, "%s: NP: %s\n", session_name,
		ast_str_tmp(256, ast_stream_topology_to_str(new_pending, &STR_TMP)));
}

static int sip_session_refresh(struct ast_sip_session *session,
		ast_sip_session_request_creation_cb on_request_creation,
		ast_sip_session_sdp_creation_cb on_sdp_creation,
		ast_sip_session_response_cb on_response,
		enum ast_sip_session_refresh_method method, int generate_new_sdp,
		struct ast_sip_session_media_state *pending_media_state,
		struct ast_sip_session_media_state *active_media_state,
		int queued)
{
	pjsip_inv_session *inv_session = session->inv_session;
	pjmedia_sdp_session *new_sdp = NULL;
	pjsip_tx_data *tdata;
	int res = -1;
	SCOPE_ENTER(3, "%s: New SDP? %s  Queued? %s DP: %s  DA: %s\n", ast_sip_session_get_name(session),
		generate_new_sdp ? "yes" : "no", queued ? "yes" : "no",
		pending_media_state ? ast_str_tmp(256, ast_stream_topology_to_str(pending_media_state->topology, &STR_TMP)) : "none",
		active_media_state ? ast_str_tmp(256, ast_stream_topology_to_str(active_media_state->topology, &STR_TMP)) : "none");

	if (pending_media_state && (!pending_media_state->topology || !generate_new_sdp)) {

		ast_sip_session_media_state_free(pending_media_state);
		ast_sip_session_media_state_free(active_media_state);
		SCOPE_EXIT_RTN_VALUE(-1, "%s: Not sending reinvite because %s%s\n", ast_sip_session_get_name(session),
			pending_media_state->topology == NULL ? "pending topology is null " : "",
				!generate_new_sdp ? "generate_new_sdp is false" : "");
	}

	if (inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		/* Don't try to do anything with a hung-up call */
		ast_sip_session_media_state_free(pending_media_state);
		ast_sip_session_media_state_free(active_media_state);
		SCOPE_EXIT_RTN_VALUE(0, "%s: Not sending reinvite because of disconnected state\n",
				ast_sip_session_get_name(session));
	}

	/* If the dialog has not yet been established we have to defer until it has */
	if (inv_session->dlg->state != PJSIP_DIALOG_STATE_ESTABLISHED) {
		res = delay_request(session, on_request_creation, on_sdp_creation, on_response,
			generate_new_sdp,
			method == AST_SIP_SESSION_REFRESH_METHOD_INVITE
				? DELAYED_METHOD_INVITE : DELAYED_METHOD_UPDATE,
			pending_media_state, active_media_state ? active_media_state : ast_sip_session_media_state_clone(session->active_media_state), queued);
		SCOPE_EXIT_RTN_VALUE(res, "%s: Delay sending reinvite because dialog has not been established\n",
			ast_sip_session_get_name(session));
	}

	if (method == AST_SIP_SESSION_REFRESH_METHOD_INVITE) {
		if (inv_session->invite_tsx) {
			/* We can't send a reinvite yet, so delay it */
			res = delay_request(session, on_request_creation, on_sdp_creation,
				on_response, generate_new_sdp, DELAYED_METHOD_INVITE, pending_media_state,
				active_media_state ? active_media_state : ast_sip_session_media_state_clone(session->active_media_state), queued);
			SCOPE_EXIT_RTN_VALUE(res, "%s: Delay sending reinvite because of outstanding transaction\n",
				ast_sip_session_get_name(session));
		} else if (inv_session->state != PJSIP_INV_STATE_CONFIRMED) {
			/* Initial INVITE transaction failed to progress us to a confirmed state
			 * which means re-invites are not possible
			 */
			ast_sip_session_media_state_free(pending_media_state);
			ast_sip_session_media_state_free(active_media_state);
			SCOPE_EXIT_RTN_VALUE(0, "%s: Not sending reinvite because not in confirmed state\n",
				ast_sip_session_get_name(session));
		}
	}

	if (generate_new_sdp) {
		/* SDP can only be generated if current negotiation has already completed */
		if (inv_session->neg
			&& pjmedia_sdp_neg_get_state(inv_session->neg)
				!= PJMEDIA_SDP_NEG_STATE_DONE) {
			res = delay_request(session, on_request_creation, on_sdp_creation,
				on_response, generate_new_sdp,
				method == AST_SIP_SESSION_REFRESH_METHOD_INVITE
					? DELAYED_METHOD_INVITE : DELAYED_METHOD_UPDATE, pending_media_state,
				active_media_state ? active_media_state : ast_sip_session_media_state_clone(session->active_media_state), queued);
			SCOPE_EXIT_RTN_VALUE(res, "%s: Delay session refresh with new SDP because SDP negotiation is not yet done\n",
				ast_sip_session_get_name(session));
		}

		/* If an explicitly requested media state has been provided use it instead of any pending one */
		if (pending_media_state) {
			int index;
			int type_streams[AST_MEDIA_TYPE_END] = {0};

			ast_trace(-1, "%s: Pending media state exists\n", ast_sip_session_get_name(session));

			/* Media state conveys a desired media state, so if there are outstanding
			 * delayed requests we need to ensure we go into the queue and not jump
			 * ahead. If we sent this media state now then updates could go out of
			 * order.
			 */
			if (!queued && !AST_LIST_EMPTY(&session->delayed_requests)) {
				res = delay_request(session, on_request_creation, on_sdp_creation,
					on_response, generate_new_sdp,
					method == AST_SIP_SESSION_REFRESH_METHOD_INVITE
						? DELAYED_METHOD_INVITE : DELAYED_METHOD_UPDATE, pending_media_state,
						active_media_state ? active_media_state : ast_sip_session_media_state_clone(session->active_media_state), queued);
				SCOPE_EXIT_RTN_VALUE(res, "%s: Delay sending reinvite because of outstanding requests\n",
					ast_sip_session_get_name(session));
			}

			/*
			 * Attempt to resolve only if objects are available, and it's not
			 * switching to or from an image type.
			 */
			if (active_media_state && active_media_state->topology &&
				(!active_media_state->default_session[AST_MEDIA_TYPE_IMAGE] ==
				 !pending_media_state->default_session[AST_MEDIA_TYPE_IMAGE])) {

				struct ast_sip_session_media_state *new_pending_state;

				ast_trace(-1, "%s: Active media state exists and is%s equal to pending\n", ast_sip_session_get_name(session),
					!ast_stream_topology_equal(active_media_state->topology,pending_media_state->topology) ? " not" : "");
				ast_trace(-1, "%s: DP: %s\n", ast_sip_session_get_name(session), ast_str_tmp(256, ast_stream_topology_to_str(pending_media_state->topology, &STR_TMP)));
				ast_trace(-1, "%s: DA: %s\n", ast_sip_session_get_name(session), ast_str_tmp(256, ast_stream_topology_to_str(active_media_state->topology, &STR_TMP)));
				ast_trace(-1, "%s: CP: %s\n", ast_sip_session_get_name(session), ast_str_tmp(256, ast_stream_topology_to_str(session->pending_media_state->topology, &STR_TMP)));
				ast_trace(-1, "%s: CA: %s\n", ast_sip_session_get_name(session), ast_str_tmp(256, ast_stream_topology_to_str(session->active_media_state->topology, &STR_TMP)));

				new_pending_state = resolve_refresh_media_states(ast_sip_session_get_name(session),
					pending_media_state, active_media_state, session->active_media_state, 1);
				if (new_pending_state) {
					ast_trace(-1, "%s: NP: %s\n", ast_sip_session_get_name(session), ast_str_tmp(256, ast_stream_topology_to_str(new_pending_state->topology, &STR_TMP)));
					ast_sip_session_media_state_free(pending_media_state);
					pending_media_state = new_pending_state;
				} else {
					ast_sip_session_media_state_reset(pending_media_state);
					ast_sip_session_media_state_free(active_media_state);
					SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: Unable to merge media states\n", ast_sip_session_get_name(session));
				}
			}

			/* Prune the media state so the number of streams fit within the configured limits - we do it here
			 * so that the index of the resulting streams in the SDP match. If we simply left the streams out
			 * of the SDP when producing it we'd be in trouble. We also enforce formats here for media types that
			 * are configurable on the endpoint.
			 */
			ast_trace(-1, "%s: Pruning and checking formats of streams\n", ast_sip_session_get_name(session));

			for (index = 0; index < ast_stream_topology_get_count(pending_media_state->topology); ++index) {
				struct ast_stream *existing_stream = NULL;
				struct ast_stream *stream = ast_stream_topology_get_stream(pending_media_state->topology, index);
				SCOPE_ENTER(4, "%s: Checking stream %s\n", ast_sip_session_get_name(session),
					ast_stream_get_name(stream));

				if (session->active_media_state->topology &&
					index < ast_stream_topology_get_count(session->active_media_state->topology)) {
					existing_stream = ast_stream_topology_get_stream(session->active_media_state->topology, index);
					ast_trace(-1, "%s: Found existing stream %s\n", ast_sip_session_get_name(session),
						ast_stream_get_name(existing_stream));
				}

				if (is_stream_limitation_reached(ast_stream_get_type(stream), session->endpoint, type_streams)) {
					if (index < AST_VECTOR_SIZE(&pending_media_state->sessions)) {
						struct ast_sip_session_media *session_media = AST_VECTOR_GET(&pending_media_state->sessions, index);

						ao2_cleanup(session_media);
						AST_VECTOR_REMOVE(&pending_media_state->sessions, index, 1);
					}

					ast_stream_topology_del_stream(pending_media_state->topology, index);
					ast_trace(-1, "%s: Dropped overlimit stream %s\n", ast_sip_session_get_name(session),
						ast_stream_get_name(stream));

					/* A stream has potentially moved into our spot so we need to jump back so we process it */
					index -= 1;
					SCOPE_EXIT_EXPR(continue);
				}

				/* No need to do anything with stream if it's media state is removed */
				if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
					/* If there is no existing stream we can just not have this stream in the topology at all. */
					if (!existing_stream) {
						ast_trace(-1, "%s: Dropped removed stream %s\n", ast_sip_session_get_name(session),
							ast_stream_get_name(stream));
						ast_stream_topology_del_stream(pending_media_state->topology, index);
						/* TODO: Do we need to remove the corresponding media state? */
						index -= 1;
					}
					SCOPE_EXIT_EXPR(continue);
				}

				/* Enforce the configured allowed codecs on audio and video streams */
				if ((ast_stream_get_type(stream) == AST_MEDIA_TYPE_AUDIO || ast_stream_get_type(stream) == AST_MEDIA_TYPE_VIDEO) &&
					!ast_stream_get_metadata(stream, "pjsip_session_refresh")) {
					struct ast_format_cap *joint_cap;

					joint_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
					if (!joint_cap) {
						ast_sip_session_media_state_free(pending_media_state);
						ast_sip_session_media_state_free(active_media_state);
						res = -1;
						SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Unable to alloc format caps\n", ast_sip_session_get_name(session));
					}
					ast_format_cap_get_compatible(ast_stream_get_formats(stream), session->endpoint->media.codecs, joint_cap);
					if (!ast_format_cap_count(joint_cap)) {
						ao2_ref(joint_cap, -1);

						if (!existing_stream) {
							/* If there is no existing stream we can just not have this stream in the topology
							 * at all.
							 */
							ast_stream_topology_del_stream(pending_media_state->topology, index);
							index -= 1;
							SCOPE_EXIT_EXPR(continue, "%s: Dropped incompatible stream %s\n",
								ast_sip_session_get_name(session), ast_stream_get_name(stream));
						} else if (ast_stream_get_state(stream) != ast_stream_get_state(existing_stream) ||
								strcmp(ast_stream_get_name(stream), ast_stream_get_name(existing_stream))) {
							/* If the underlying stream is a different type or different name then we have to
							 * mark it as removed, as it is replacing an existing stream. We do this so order
							 * is preserved.
							 */
							ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
							SCOPE_EXIT_EXPR(continue, "%s: Dropped incompatible stream %s\n",
								ast_sip_session_get_name(session), ast_stream_get_name(stream));
						} else {
							/* However if the stream is otherwise remaining the same we can keep the formats
							 * that exist on it already which allows media to continue to flow. We don't modify
							 * the format capabilities but do need to cast it so that ao2_bump can raise the
							 * reference count.
							 */
							joint_cap = ao2_bump((struct ast_format_cap *)ast_stream_get_formats(existing_stream));
						}
					}
					ast_stream_set_formats(stream, joint_cap);
					ao2_cleanup(joint_cap);
				}

				++type_streams[ast_stream_get_type(stream)];

				SCOPE_EXIT();
			}

			if (session->active_media_state->topology) {
				/* SDP is a fun thing. Take for example the fact that streams are never removed. They just become
				 * declined. To better handle this in the case where something requests a topology change for fewer
				 * streams than are currently present we fill in the topology to match the current number of streams
				 * that are active.
				 */

				for (index = ast_stream_topology_get_count(pending_media_state->topology);
					index < ast_stream_topology_get_count(session->active_media_state->topology); ++index) {
					struct ast_stream *stream = ast_stream_topology_get_stream(session->active_media_state->topology, index);
					struct ast_stream *cloned;
					int position;
					SCOPE_ENTER(4, "%s: Stream %s not in pending\n", ast_sip_session_get_name(session),
						ast_stream_get_name(stream));

					cloned = ast_stream_clone(stream, NULL);
					if (!cloned) {
						ast_sip_session_media_state_free(pending_media_state);
						ast_sip_session_media_state_free(active_media_state);
						res = -1;
						SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Unable to clone stream %s\n",
							ast_sip_session_get_name(session), ast_stream_get_name(stream));
					}

					ast_stream_set_state(cloned, AST_STREAM_STATE_REMOVED);
					position = ast_stream_topology_append_stream(pending_media_state->topology, cloned);
					if (position < 0) {
						ast_stream_free(cloned);
						ast_sip_session_media_state_free(pending_media_state);
						ast_sip_session_media_state_free(active_media_state);
						res = -1;
						SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Unable to append cloned stream\n",
							ast_sip_session_get_name(session));
					}
					SCOPE_EXIT("%s: Appended empty stream in position %d to make counts match\n",
						ast_sip_session_get_name(session), position);
				}

				/*
				 * We can suppress this re-invite if the pending topology is equal to the currently
				 * active topology.
				 */
				if (ast_stream_topology_equal(session->active_media_state->topology, pending_media_state->topology)) {
					ast_trace(-1, "%s: CA: %s\n", ast_sip_session_get_name(session), ast_str_tmp(256, ast_stream_topology_to_str(session->active_media_state->topology, &STR_TMP)));
					ast_trace(-1, "%s: NP: %s\n", ast_sip_session_get_name(session), ast_str_tmp(256, ast_stream_topology_to_str(pending_media_state->topology, &STR_TMP)));
					ast_sip_session_media_state_free(pending_media_state);
					ast_sip_session_media_state_free(active_media_state);
					/* For external consumers we return 0 to say success, but internally for
					 * send_delayed_request we return a separate value to indicate that this
					 * session refresh would be redundant so we didn't send it
					 */
					SCOPE_EXIT_RTN_VALUE(queued ? 1 : 0, "%s: Topologies are equal. Not sending re-invite\n",
						ast_sip_session_get_name(session));
				}
			}

			ast_sip_session_media_state_free(session->pending_media_state);
			session->pending_media_state = pending_media_state;
		}

		new_sdp = generate_session_refresh_sdp(session);
		if (!new_sdp) {
			ast_sip_session_media_state_reset(session->pending_media_state);
			ast_sip_session_media_state_free(active_media_state);
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: Failed to generate session refresh SDP. Not sending session refresh\n",
				ast_sip_session_get_name(session));
		}
		if (on_sdp_creation) {
			if (on_sdp_creation(session, new_sdp)) {
				ast_sip_session_media_state_reset(session->pending_media_state);
				ast_sip_session_media_state_free(active_media_state);
				SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: on_sdp_creation failed\n", ast_sip_session_get_name(session));
			}
		}
	}

	if (method == AST_SIP_SESSION_REFRESH_METHOD_INVITE) {
		if (pjsip_inv_reinvite(inv_session, NULL, new_sdp, &tdata)) {
			if (generate_new_sdp) {
				ast_sip_session_media_state_reset(session->pending_media_state);
			}
			ast_sip_session_media_state_free(active_media_state);
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: Failed to create reinvite properly\n", ast_sip_session_get_name(session));
		}
	} else if (pjsip_inv_update(inv_session, NULL, new_sdp, &tdata)) {
		if (generate_new_sdp) {
			ast_sip_session_media_state_reset(session->pending_media_state);
		}
		ast_sip_session_media_state_free(active_media_state);
		SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: Failed to create UPDATE properly\n", ast_sip_session_get_name(session));
	}
	if (on_request_creation) {
		if (on_request_creation(session, tdata)) {
			if (generate_new_sdp) {
				ast_sip_session_media_state_reset(session->pending_media_state);
			}
			ast_sip_session_media_state_free(active_media_state);
			SCOPE_EXIT_LOG_RTN_VALUE(-1, LOG_WARNING, "%s: on_request_creation failed.\n", ast_sip_session_get_name(session));
		}
	}
	ast_sip_session_send_request_with_cb(session, tdata, on_response);
	ast_sip_session_media_state_free(active_media_state);

end:
	SCOPE_EXIT_RTN_VALUE(res, "%s: Sending session refresh SDP via %s\n", ast_sip_session_get_name(session),
		method == AST_SIP_SESSION_REFRESH_METHOD_INVITE ? "re-INVITE" : "UPDATE");
}

int ast_sip_session_refresh(struct ast_sip_session *session,
		ast_sip_session_request_creation_cb on_request_creation,
		ast_sip_session_sdp_creation_cb on_sdp_creation,
		ast_sip_session_response_cb on_response,
		enum ast_sip_session_refresh_method method, int generate_new_sdp,
		struct ast_sip_session_media_state *media_state)
{
	return sip_session_refresh(session, on_request_creation, on_sdp_creation,
		on_response, method, generate_new_sdp, media_state, NULL, 0);
}

int ast_sip_session_regenerate_answer(struct ast_sip_session *session,
		ast_sip_session_sdp_creation_cb on_sdp_creation)
{
	pjsip_inv_session *inv_session = session->inv_session;
	pjmedia_sdp_session *new_answer = NULL;
	const pjmedia_sdp_session *previous_offer = NULL;
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	/* The SDP answer can only be regenerated if it is still pending to be sent */
	if (!inv_session->neg || (pjmedia_sdp_neg_get_state(inv_session->neg) != PJMEDIA_SDP_NEG_STATE_REMOTE_OFFER &&
		pjmedia_sdp_neg_get_state(inv_session->neg) != PJMEDIA_SDP_NEG_STATE_WAIT_NEGO)) {
		ast_log(LOG_WARNING, "Requested to regenerate local SDP answer for channel '%s' but negotiation in state '%s'\n",
			ast_channel_name(session->channel), pjmedia_sdp_neg_state_str(pjmedia_sdp_neg_get_state(inv_session->neg)));
		SCOPE_EXIT_RTN_VALUE(-1, "Bad negotiation state\n");
	}

	pjmedia_sdp_neg_get_neg_remote(inv_session->neg, &previous_offer);
	if (pjmedia_sdp_neg_get_state(inv_session->neg) == PJMEDIA_SDP_NEG_STATE_WAIT_NEGO) {
		/* Transition the SDP negotiator back to when it received the remote offer */
		pjmedia_sdp_neg_negotiate(inv_session->pool, inv_session->neg, 0);
		pjmedia_sdp_neg_set_remote_offer(inv_session->pool, inv_session->neg, previous_offer);
	}

	new_answer = create_local_sdp(inv_session, session, previous_offer);
	if (!new_answer) {
		ast_log(LOG_WARNING, "Could not create a new local SDP answer for channel '%s'\n",
			ast_channel_name(session->channel));
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create new SDP\n");
	}

	if (on_sdp_creation) {
		if (on_sdp_creation(session, new_answer)) {
			SCOPE_EXIT_RTN_VALUE(-1, "Callback failed\n");
		}
	}

	pjsip_inv_set_sdp_answer(inv_session, new_answer);

	SCOPE_EXIT_RTN_VALUE(0);
}

void ast_sip_session_send_response(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	handle_outgoing_response(session, tdata);
	pjsip_inv_send_msg(session->inv_session, tdata);
	return;
}

static pj_bool_t session_on_rx_request(pjsip_rx_data *rdata);
static pj_bool_t session_on_rx_response(pjsip_rx_data *rdata);
static void session_on_tsx_state(pjsip_transaction *tsx, pjsip_event *e);

static pjsip_module session_module = {
	.name = {"Session Module", 14},
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = session_on_rx_request,
	.on_rx_response = session_on_rx_response,
	.on_tsx_state = session_on_tsx_state,
};

/*! \brief Determine whether the SDP provided requires deferral of negotiating or not
 *
 * \retval 1 re-invite should be deferred and resumed later
 * \retval 0 re-invite should not be deferred
 */
static int sdp_requires_deferral(struct ast_sip_session *session, const pjmedia_sdp_session *sdp)
{
	int i;

	if (!session->pending_media_state->topology) {
		session->pending_media_state->topology = ast_stream_topology_alloc();
		if (!session->pending_media_state->topology) {
			return -1;
		}
	}

	for (i = 0; i < sdp->media_count; ++i) {
		/* See if there are registered handlers for this media stream type */
		char media[20];
		struct ast_sip_session_sdp_handler *handler;
		RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);
		struct ast_stream *existing_stream = NULL;
		struct ast_stream *stream;
		enum ast_media_type type;
		struct ast_sip_session_media *session_media = NULL;
		enum ast_sip_session_sdp_stream_defer res;
		pjmedia_sdp_media *remote_stream = sdp->media[i];

		/* We need a null-terminated version of the media string */
		ast_copy_pj_str(media, &sdp->media[i]->desc.media, sizeof(media));

		if (session->active_media_state->topology &&
			(i < ast_stream_topology_get_count(session->active_media_state->topology))) {
			existing_stream = ast_stream_topology_get_stream(session->active_media_state->topology, i);
		}

		type = ast_media_type_from_str(media);
		stream = ast_stream_alloc(existing_stream ? ast_stream_get_name(existing_stream) : ast_codec_media_type2str(type), type);
		if (!stream) {
			return -1;
		}

		/* As this is only called on an incoming SDP offer before processing it is not possible
		 * for streams and their media sessions to exist.
		 */
		if (ast_stream_topology_set_stream(session->pending_media_state->topology, i, stream)) {
			ast_stream_free(stream);
			return -1;
		}

		if (existing_stream) {
			const char *stream_label = ast_stream_get_metadata(existing_stream, "SDP:LABEL");

			if (!ast_strlen_zero(stream_label)) {
				ast_stream_set_metadata(stream, "SDP:LABEL", stream_label);
			}
		}

		session_media = ast_sip_session_media_state_add(session, session->pending_media_state, ast_media_type_from_str(media), i);
		if (!session_media) {
			return -1;
		}

		/* For backwards compatibility with the core the default audio stream is always sendrecv */
		if (!ast_sip_session_is_pending_stream_default(session, stream) || strcmp(media, "audio")) {
			if (pjmedia_sdp_media_find_attr2(remote_stream, "sendonly", NULL)) {
				/* Stream state reflects our state of a stream, so in the case of
				 * sendonly and recvonly we store the opposite since that is what ours
				 * is.
				 */
				ast_stream_set_state(stream, AST_STREAM_STATE_RECVONLY);
			} else if (pjmedia_sdp_media_find_attr2(remote_stream, "recvonly", NULL)) {
				ast_stream_set_state(stream, AST_STREAM_STATE_SENDONLY);
			} else if (pjmedia_sdp_media_find_attr2(remote_stream, "inactive", NULL)) {
				ast_stream_set_state(stream, AST_STREAM_STATE_INACTIVE);
			} else {
				ast_stream_set_state(stream, AST_STREAM_STATE_SENDRECV);
			}
		} else {
			ast_stream_set_state(stream, AST_STREAM_STATE_SENDRECV);
		}

		if (session_media->handler) {
			handler = session_media->handler;
			if (handler->defer_incoming_sdp_stream) {
				res = handler->defer_incoming_sdp_stream(session, session_media, sdp,
					sdp->media[i]);
				switch (res) {
				case AST_SIP_SESSION_SDP_DEFER_NOT_HANDLED:
					break;
				case AST_SIP_SESSION_SDP_DEFER_ERROR:
					return 0;
				case AST_SIP_SESSION_SDP_DEFER_NOT_NEEDED:
					break;
				case AST_SIP_SESSION_SDP_DEFER_NEEDED:
					return 1;
				}
			}
			/* Handled by this handler. Move to the next stream */
			continue;
		}

		handler_list = ao2_find(sdp_handlers, media, OBJ_KEY);
		if (!handler_list) {
			ast_debug(3, "%s: No registered SDP handlers for media type '%s'\n", ast_sip_session_get_name(session), media);
			continue;
		}
		AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
			if (handler == session_media->handler) {
				continue;
			}
			if (!handler->defer_incoming_sdp_stream) {
				continue;
			}
			res = handler->defer_incoming_sdp_stream(session, session_media, sdp,
				sdp->media[i]);
			switch (res) {
			case AST_SIP_SESSION_SDP_DEFER_NOT_HANDLED:
				continue;
			case AST_SIP_SESSION_SDP_DEFER_ERROR:
				session_media_set_handler(session_media, handler);
				return 0;
			case AST_SIP_SESSION_SDP_DEFER_NOT_NEEDED:
				/* Handled by this handler. */
				session_media_set_handler(session_media, handler);
				break;
			case AST_SIP_SESSION_SDP_DEFER_NEEDED:
				/* Handled by this handler. */
				session_media_set_handler(session_media, handler);
				return 1;
			}
			/* Move to the next stream */
			break;
		}
	}
	return 0;
}

static pj_bool_t session_reinvite_on_rx_request(pjsip_rx_data *rdata)
{
	pjsip_dialog *dlg;
	RAII_VAR(struct ast_sip_session *, session, NULL, ao2_cleanup);
	pjsip_rdata_sdp_info *sdp_info;
	int deferred;

	if (rdata->msg_info.msg->line.req.method.id != PJSIP_INVITE_METHOD ||
		!(dlg = pjsip_ua_find_dialog(&rdata->msg_info.cid->id, &rdata->msg_info.to->tag, &rdata->msg_info.from->tag, PJ_FALSE)) ||
		!(session = ast_sip_dialog_get_session(dlg)) ||
		!session->channel) {
		return PJ_FALSE;
	}

	if (session->inv_session->invite_tsx) {
		/* There's a transaction in progress so bail now and let pjproject send 491 */
		return PJ_FALSE;
	}

	if (session->deferred_reinvite) {
		pj_str_t key, deferred_key;
		pjsip_tx_data *tdata;

		/* We use memory from the new request on purpose so the deferred reinvite pool does not grow uncontrollably */
		pjsip_tsx_create_key(rdata->tp_info.pool, &key, PJSIP_ROLE_UAS, &rdata->msg_info.cseq->method, rdata);
		pjsip_tsx_create_key(rdata->tp_info.pool, &deferred_key, PJSIP_ROLE_UAS, &session->deferred_reinvite->msg_info.cseq->method,
			session->deferred_reinvite);

		/* If this is a retransmission ignore it */
		if (!pj_strcmp(&key, &deferred_key)) {
			return PJ_TRUE;
		}

		/* Otherwise this is a new re-invite, so reject it */
		if (pjsip_dlg_create_response(dlg, rdata, 491, NULL, &tdata) == PJ_SUCCESS) {
			if (pjsip_endpt_send_response2(ast_sip_get_pjsip_endpoint(), rdata, tdata, NULL, NULL) != PJ_SUCCESS) {
				pjsip_tx_data_dec_ref(tdata);
			}
		}

		return PJ_TRUE;
	}

	if (!(sdp_info = pjsip_rdata_get_sdp_info(rdata)) ||
		(sdp_info->sdp_err != PJ_SUCCESS)) {
		return PJ_FALSE;
	}

	if (!sdp_info->sdp) {
		return PJ_FALSE;
	}

	deferred = sdp_requires_deferral(session, sdp_info->sdp);
	if (deferred == -1) {
		ast_sip_session_media_state_reset(session->pending_media_state);
		return PJ_FALSE;
	} else if (!deferred) {
		return PJ_FALSE;
	}

	pjsip_rx_data_clone(rdata, 0, &session->deferred_reinvite);

	return PJ_TRUE;
}

void ast_sip_session_resume_reinvite(struct ast_sip_session *session)
{
	if (!session->deferred_reinvite) {
		return;
	}

	if (session->channel) {
		pjsip_endpt_process_rx_data(ast_sip_get_pjsip_endpoint(),
			session->deferred_reinvite, NULL, NULL);
	}
	pjsip_rx_data_free_cloned(session->deferred_reinvite);
	session->deferred_reinvite = NULL;
}

static pjsip_module session_reinvite_module = {
	.name = { "Session Re-Invite Module", 24 },
	.priority = PJSIP_MOD_PRIORITY_UA_PROXY_LAYER - 1,
	.on_rx_request = session_reinvite_on_rx_request,
};

void ast_sip_session_send_request_with_cb(struct ast_sip_session *session, pjsip_tx_data *tdata,
		ast_sip_session_response_cb on_response)
{
	pjsip_inv_session *inv_session = session->inv_session;

	/* For every request except BYE we disallow sending of the message when
	 * the session has been disconnected. A BYE request is special though
	 * because it can be sent again after the session is disconnected except
	 * with credentials.
	 */
	if (inv_session->state == PJSIP_INV_STATE_DISCONNECTED &&
		tdata->msg->line.req.method.id != PJSIP_BYE_METHOD) {
		return;
	}

	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, session_module.id,
			     MOD_DATA_ON_RESPONSE, on_response);

	handle_outgoing_request(session, tdata);
	pjsip_inv_send_msg(session->inv_session, tdata);

	return;
}

void ast_sip_session_send_request(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	ast_sip_session_send_request_with_cb(session, tdata, NULL);
}

int ast_sip_session_create_invite(struct ast_sip_session *session, pjsip_tx_data **tdata)
{
	pjmedia_sdp_session *offer;
	SCOPE_ENTER(1, "%s\n", ast_sip_session_get_name(session));

	if (!(offer = create_local_sdp(session->inv_session, session, NULL))) {
		pjsip_inv_terminate(session->inv_session, 500, PJ_FALSE);
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't create offer\n");
	}

	pjsip_inv_set_local_sdp(session->inv_session, offer);
	pjmedia_sdp_neg_set_prefer_remote_codec_order(session->inv_session->neg, PJ_FALSE);
#ifdef PJMEDIA_SDP_NEG_ANSWER_MULTIPLE_CODECS
	if (!session->endpoint->preferred_codec_only) {
		pjmedia_sdp_neg_set_answer_multiple_codecs(session->inv_session->neg, PJ_TRUE);
	}
#endif

	/*
	 * We MUST call set_from_header() before pjsip_inv_invite.  If we don't, the
	 * From in the initial INVITE will be wrong but the rest of the messages will be OK.
	 */
	set_from_header(session);

	if (pjsip_inv_invite(session->inv_session, tdata) != PJ_SUCCESS) {
		SCOPE_EXIT_RTN_VALUE(-1, "pjsip_inv_invite failed\n");
	}

	SCOPE_EXIT_RTN_VALUE(0);
}

static int datastore_hash(const void *obj, int flags)
{
	const struct ast_datastore *datastore = obj;
	const char *uid = flags & OBJ_KEY ? obj : datastore->uid;

	ast_assert(uid != NULL);

	return ast_str_hash(uid);
}

static int datastore_cmp(void *obj, void *arg, int flags)
{
	const struct ast_datastore *datastore1 = obj;
	const struct ast_datastore *datastore2 = arg;
	const char *uid2 = flags & OBJ_KEY ? arg : datastore2->uid;

	ast_assert(datastore1->uid != NULL);
	ast_assert(uid2 != NULL);

	return strcmp(datastore1->uid, uid2) ? 0 : CMP_MATCH | CMP_STOP;
}

static void session_destructor(void *obj)
{
	struct ast_sip_session *session = obj;
	struct ast_sip_session_delayed_request *delay;

#ifdef TEST_FRAMEWORK
	/* We dup the endpoint ID in case the endpoint gets freed out from under us */
	const char *endpoint_name = session->endpoint ?
		ast_strdupa(ast_sorcery_object_get_id(session->endpoint)) : "<none>";
#endif

	ast_debug(3, "%s: Destroying SIP session\n", ast_sip_session_get_name(session));

	ast_test_suite_event_notify("SESSION_DESTROYING",
		"Endpoint: %s\r\n"
		"AOR: %s\r\n"
		"Contact: %s"
		, endpoint_name
		, session->aor ? ast_sorcery_object_get_id(session->aor) : "<none>"
		, session->contact ? ast_sorcery_object_get_id(session->contact) : "<none>"
		);

	/* fire session destroy handler */
	handle_session_destroy(session);

	/* remove all registered supplements */
	ast_sip_session_remove_supplements(session);
	AST_LIST_HEAD_DESTROY(&session->supplements);

	/* remove all saved media stats */
	AST_VECTOR_RESET(&session->media_stats, ast_free);
	AST_VECTOR_FREE(&session->media_stats);

	ast_taskprocessor_unreference(session->serializer);
	ao2_cleanup(session->datastores);
	ast_sip_session_media_state_free(session->active_media_state);
	ast_sip_session_media_state_free(session->pending_media_state);

	while ((delay = AST_LIST_REMOVE_HEAD(&session->delayed_requests, next))) {
		delayed_request_free(delay);
	}
	ast_party_id_free(&session->id);
	ao2_cleanup(session->endpoint);
	ao2_cleanup(session->aor);
	ao2_cleanup(session->contact);
	ao2_cleanup(session->direct_media_cap);

	ast_dsp_free(session->dsp);

	if (session->inv_session) {
		struct pjsip_dialog *dlg = session->inv_session->dlg;

		/* The INVITE session uses the dialog pool for memory, so we need to
		 * decrement its reference first before that of the dialog.
		 */

#ifdef HAVE_PJSIP_INV_SESSION_REF
		pjsip_inv_dec_ref(session->inv_session);
#endif
		pjsip_dlg_dec_session(dlg, &session_module);
	}

	ast_test_suite_event_notify("SESSION_DESTROYED", "Endpoint: %s", endpoint_name);
}

/*! \brief Destructor for SIP channel */
static void sip_channel_destroy(void *obj)
{
	struct ast_sip_channel_pvt *channel = obj;

	ao2_cleanup(channel->pvt);
	ao2_cleanup(channel->session);
}

struct ast_sip_channel_pvt *ast_sip_channel_pvt_alloc(void *pvt, struct ast_sip_session *session)
{
	struct ast_sip_channel_pvt *channel = ao2_alloc(sizeof(*channel), sip_channel_destroy);

	if (!channel) {
		return NULL;
	}

	ao2_ref(pvt, +1);
	channel->pvt = pvt;
	ao2_ref(session, +1);
	channel->session = session;

	return channel;
}

struct ast_sip_session *ast_sip_session_alloc(struct ast_sip_endpoint *endpoint,
	struct ast_sip_contact *contact, pjsip_inv_session *inv_session, pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_session *, session, NULL, ao2_cleanup);
	struct ast_sip_session *ret_session;
	int dsp_features = 0;

	session = ao2_alloc(sizeof(*session), session_destructor);
	if (!session) {
		return NULL;
	}

	AST_LIST_HEAD_INIT(&session->supplements);
	AST_LIST_HEAD_INIT_NOLOCK(&session->delayed_requests);
	ast_party_id_init(&session->id);

	session->direct_media_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!session->direct_media_cap) {
		return NULL;
	}
	session->datastores = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		DATASTORE_BUCKETS, datastore_hash, NULL, datastore_cmp);
	if (!session->datastores) {
		return NULL;
	}
	session->active_media_state = ast_sip_session_media_state_alloc();
	if (!session->active_media_state) {
		return NULL;
	}
	session->pending_media_state = ast_sip_session_media_state_alloc();
	if (!session->pending_media_state) {
		return NULL;
	}
	if (AST_VECTOR_INIT(&session->media_stats, 1) < 0) {
		return NULL;
	}

	if (endpoint->dtmf == AST_SIP_DTMF_INBAND || endpoint->dtmf == AST_SIP_DTMF_AUTO) {
		dsp_features |= DSP_FEATURE_DIGIT_DETECT;
	}
	if (endpoint->faxdetect) {
		dsp_features |= DSP_FEATURE_FAX_DETECT;
	}
	if (dsp_features) {
		session->dsp = ast_dsp_new();
		if (!session->dsp) {
			return NULL;
		}

		ast_dsp_set_features(session->dsp, dsp_features);
	}

	session->endpoint = ao2_bump(endpoint);

	if (rdata) {
		/*
		 * We must continue using the serializer that the original
		 * INVITE came in on for the dialog.  There may be
		 * retransmissions already enqueued in the original
		 * serializer that can result in reentrancy and message
		 * sequencing problems.
		 */
		session->serializer = ast_sip_get_distributor_serializer(rdata);
	} else {
		char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];

		/* Create name with seq number appended. */
		ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "pjsip/outsess/%s",
			ast_sorcery_object_get_id(endpoint));

		session->serializer = ast_sip_create_serializer(tps_name);
	}
	if (!session->serializer) {
		return NULL;
	}
	ast_sip_dialog_set_serializer(inv_session->dlg, session->serializer);
	ast_sip_dialog_set_endpoint(inv_session->dlg, endpoint);

	/* When a PJSIP INVITE session is created it is created with a reference
	 * count of 1, with that reference being managed by the underlying state
	 * of the INVITE session itself. When the INVITE session transitions to
	 * a DISCONNECTED state that reference is released. This means we can not
	 * rely on that reference to ensure the INVITE session remains for the
	 * lifetime of our session. To ensure it does we add our own reference
	 * and release it when our own session goes away, ensuring that the INVITE
	 * session remains for the lifetime of session.
	 */

#ifdef HAVE_PJSIP_INV_SESSION_REF
	if (pjsip_inv_add_ref(inv_session) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Can't increase the session reference counter\n");
		return NULL;
	}
#endif

	pjsip_dlg_inc_session(inv_session->dlg, &session_module);
	inv_session->mod_data[session_module.id] = ao2_bump(session);
	session->contact = ao2_bump(contact);
	session->inv_session = inv_session;

	session->dtmf = endpoint->dtmf;
	session->moh_passthrough = endpoint->moh_passthrough;

	if (ast_sip_session_add_supplements(session)) {
		/* Release the ref held by session->inv_session */
		ao2_ref(session, -1);
		return NULL;
	}

	session->authentication_challenge_count = 0;

	/* Fire session begin handlers */
	handle_session_begin(session);

	/* Avoid unnecessary ref manipulation to return a session */
	ret_session = session;
	session = NULL;
	return ret_session;
}

/*! \brief struct controlling the suspension of the session's serializer. */
struct ast_sip_session_suspender {
	ast_cond_t cond_suspended;
	ast_cond_t cond_complete;
	int suspended;
	int complete;
};

static void sip_session_suspender_dtor(void *vdoomed)
{
	struct ast_sip_session_suspender *doomed = vdoomed;

	ast_cond_destroy(&doomed->cond_suspended);
	ast_cond_destroy(&doomed->cond_complete);
}

/*!
 * \internal
 * \brief Block the session serializer thread task.
 *
 * \param data Pushed serializer task data for suspension.
 *
 * \retval 0
 */
static int sip_session_suspend_task(void *data)
{
	struct ast_sip_session_suspender *suspender = data;

	ao2_lock(suspender);

	/* Signal that the serializer task is now suspended. */
	suspender->suspended = 1;
	ast_cond_signal(&suspender->cond_suspended);

	/* Wait for the serializer suspension to be completed. */
	while (!suspender->complete) {
		ast_cond_wait(&suspender->cond_complete, ao2_object_get_lockaddr(suspender));
	}

	ao2_unlock(suspender);
	ao2_ref(suspender, -1);

	return 0;
}

void ast_sip_session_suspend(struct ast_sip_session *session)
{
	struct ast_sip_session_suspender *suspender;
	int res;

	ast_assert(session->suspended == NULL);

	if (ast_taskprocessor_is_task(session->serializer)) {
		/* I am the session's serializer thread so I cannot suspend. */
		return;
	}

	if (ast_taskprocessor_is_suspended(session->serializer)) {
		/* The serializer already suspended. */
		return;
	}

	suspender = ao2_alloc(sizeof(*suspender), sip_session_suspender_dtor);
	if (!suspender) {
		/* We will just have to hope that the system does not deadlock */
		return;
	}
	ast_cond_init(&suspender->cond_suspended, NULL);
	ast_cond_init(&suspender->cond_complete, NULL);

	ao2_ref(suspender, +1);
	res = ast_sip_push_task(session->serializer, sip_session_suspend_task, suspender);
	if (res) {
		/* We will just have to hope that the system does not deadlock */
		ao2_ref(suspender, -2);
		return;
	}

	session->suspended = suspender;

	/* Wait for the serializer to get suspended. */
	ao2_lock(suspender);
	while (!suspender->suspended) {
		ast_cond_wait(&suspender->cond_suspended, ao2_object_get_lockaddr(suspender));
	}
	ao2_unlock(suspender);

	ast_taskprocessor_suspend(session->serializer);
}

void ast_sip_session_unsuspend(struct ast_sip_session *session)
{
	struct ast_sip_session_suspender *suspender = session->suspended;

	if (!suspender) {
		/* Nothing to do */
		return;
	}
	session->suspended = NULL;

	/* Signal that the serializer task suspension is now complete. */
	ao2_lock(suspender);
	suspender->complete = 1;
	ast_cond_signal(&suspender->cond_complete);
	ao2_unlock(suspender);

	ao2_ref(suspender, -1);

	ast_taskprocessor_unsuspend(session->serializer);
}

/*!
 * \internal
 * \brief Handle initial INVITE challenge response message.
 * \since 13.5.0
 *
 * \param rdata PJSIP receive response message data.
 *
 * \retval PJ_FALSE Did not handle message.
 * \retval PJ_TRUE Handled message.
 */
static pj_bool_t outbound_invite_auth(pjsip_rx_data *rdata)
{
	pjsip_transaction *tsx;
	pjsip_dialog *dlg;
	pjsip_inv_session *inv;
	pjsip_tx_data *tdata;
	struct ast_sip_session *session;

	if (rdata->msg_info.msg->line.status.code != 401
		&& rdata->msg_info.msg->line.status.code != 407) {
		/* Doesn't pertain to us. Move on */
		return PJ_FALSE;
	}

	tsx = pjsip_rdata_get_tsx(rdata);
	dlg = pjsip_rdata_get_dlg(rdata);
	if (!dlg || !tsx) {
		return PJ_FALSE;
	}

	if (tsx->method.id != PJSIP_INVITE_METHOD) {
		/* Not an INVITE that needs authentication */
		return PJ_FALSE;
	}

	inv = pjsip_dlg_get_inv_session(dlg);
	session = inv->mod_data[session_module.id];

	if (PJSIP_INV_STATE_CONFIRMED <= inv->state) {
		/*
		 * We cannot handle reINVITE authentication at this
		 * time because the reINVITE transaction is still in
		 * progress.
		 */
		ast_debug(3, "%s: A reINVITE is being challenged\n", ast_sip_session_get_name(session));
		return PJ_FALSE;
	}
	ast_debug(3, "%s: Initial INVITE is being challenged.\n", ast_sip_session_get_name(session));

	if (++session->authentication_challenge_count > MAX_RX_CHALLENGES) {
		ast_debug(3, "%s: Initial INVITE reached maximum number of auth attempts.\n", ast_sip_session_get_name(session));
		return PJ_FALSE;
	}

	if (ast_sip_create_request_with_auth(&session->endpoint->outbound_auths, rdata,
		tsx->last_tx, &tdata)) {
		return PJ_FALSE;
	}

	/*
	 * Restart the outgoing initial INVITE transaction to deal
	 * with authentication.
	 */
	pjsip_inv_uac_restart(inv, PJ_FALSE);

	ast_sip_session_send_request(session, tdata);
	return PJ_TRUE;
}

static pjsip_module outbound_invite_auth_module = {
	.name = {"Outbound INVITE Auth", 20},
	.priority = PJSIP_MOD_PRIORITY_DIALOG_USAGE,
	.on_rx_response = outbound_invite_auth,
};

/*!
 * \internal
 * \brief Setup outbound initial INVITE authentication.
 * \since 13.5.0
 *
 * \param dlg PJSIP dialog to attach outbound authentication.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_outbound_invite_auth(pjsip_dialog *dlg)
{
	pj_status_t status;

	++dlg->sess_count;
	status = pjsip_dlg_add_usage(dlg, &outbound_invite_auth_module, NULL);
	--dlg->sess_count;

	return status != PJ_SUCCESS ? -1 : 0;
}

struct ast_sip_session *ast_sip_session_create_outgoing(struct ast_sip_endpoint *endpoint,
	struct ast_sip_contact *contact, const char *location, const char *request_user,
	struct ast_stream_topology *req_topology)
{
	const char *uri = NULL;
	RAII_VAR(struct ast_sip_aor *, found_aor, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_contact *, found_contact, NULL, ao2_cleanup);
	pjsip_timer_setting timer;
	pjsip_dialog *dlg;
	struct pjsip_inv_session *inv_session;
	RAII_VAR(struct ast_sip_session *, session, NULL, ao2_cleanup);
	struct ast_sip_session *ret_session;
	SCOPE_ENTER(1, "%s %s Topology: %s\n", ast_sorcery_object_get_id(endpoint), request_user,
		ast_str_tmp(256, ast_stream_topology_to_str(req_topology, &STR_TMP)));

	/* If no location has been provided use the AOR list from the endpoint itself */
	if (location || !contact) {
		location = S_OR(location, endpoint->aors);

		ast_sip_location_retrieve_contact_and_aor_from_list_filtered(location, AST_SIP_CONTACT_FILTER_REACHABLE,
			&found_aor, &found_contact);
		if (!found_contact || ast_strlen_zero(found_contact->uri)) {
			uri = location;
		} else {
			uri = found_contact->uri;
		}
	} else {
		uri = contact->uri;
	}

	/* If we still have no URI to dial fail to create the session */
	if (ast_strlen_zero(uri)) {
		ast_log(LOG_ERROR, "Endpoint '%s': No URI available.  Is endpoint registered?\n",
			ast_sorcery_object_get_id(endpoint));
		SCOPE_EXIT_RTN_VALUE(NULL, "No URI\n");
	}

	if (!(dlg = ast_sip_create_dialog_uac(endpoint, uri, request_user))) {
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't create dialog\n");
	}

	if (setup_outbound_invite_auth(dlg)) {
		pjsip_dlg_terminate(dlg);
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't setup auth\n");
	}

	if (pjsip_inv_create_uac(dlg, NULL, endpoint->extensions.flags, &inv_session) != PJ_SUCCESS) {
		pjsip_dlg_terminate(dlg);
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't create uac\n");
	}
#if defined(HAVE_PJSIP_REPLACE_MEDIA_STREAM) || defined(PJMEDIA_SDP_NEG_ALLOW_MEDIA_CHANGE)
	inv_session->sdp_neg_flags = PJMEDIA_SDP_NEG_ALLOW_MEDIA_CHANGE;
#endif

	pjsip_timer_setting_default(&timer);
	timer.min_se = endpoint->extensions.timer.min_se;
	timer.sess_expires = endpoint->extensions.timer.sess_expires;
	pjsip_timer_init_session(inv_session, &timer);

	session = ast_sip_session_alloc(endpoint, found_contact ? found_contact : contact,
		inv_session, NULL);
	if (!session) {
		pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		return NULL;
	}
	session->aor = ao2_bump(found_aor);
	session->call_direction = AST_SIP_SESSION_OUTGOING_CALL;

	ast_party_id_copy(&session->id, &endpoint->id.self);

	if (ast_stream_topology_get_count(req_topology) > 0) {
		/* get joint caps between req_topology and endpoint topology */
		int i;

		for (i = 0; i < ast_stream_topology_get_count(req_topology); ++i) {
			struct ast_stream *req_stream;
			struct ast_stream *clone_stream;

			req_stream = ast_stream_topology_get_stream(req_topology, i);

			if (ast_stream_get_state(req_stream) == AST_STREAM_STATE_REMOVED) {
				continue;
			}

			clone_stream = ast_sip_session_create_joint_call_stream(session, req_stream);
			if (!clone_stream || ast_stream_get_format_count(clone_stream) == 0) {
				ast_stream_free(clone_stream);
				continue;
			}

			if (!session->pending_media_state->topology) {
				session->pending_media_state->topology = ast_stream_topology_alloc();
				if (!session->pending_media_state->topology) {
					pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
					ao2_ref(session, -1);
					SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't create topology\n");
				}
			}

			if (ast_stream_topology_append_stream(session->pending_media_state->topology, clone_stream) < 0) {
				ast_stream_free(clone_stream);
				continue;
			}
		}
	}

	if (!session->pending_media_state->topology) {
		/* Use the configured topology on the endpoint as the pending one */
		session->pending_media_state->topology = ast_stream_topology_clone(endpoint->media.topology);
		if (!session->pending_media_state->topology) {
			pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
			ao2_ref(session, -1);
			SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't clone topology\n");
		}
	}

	if (pjsip_dlg_add_usage(dlg, &session_module, NULL) != PJ_SUCCESS) {
		pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		/* Since we are not notifying ourselves that the INVITE session is being terminated
		 * we need to manually drop its reference to session
		 */
		ao2_ref(session, -1);
		SCOPE_EXIT_RTN_VALUE(NULL, "Couldn't add usage\n");
	}

	/* Avoid unnecessary ref manipulation to return a session */
	ret_session = session;
	session = NULL;
	SCOPE_EXIT_RTN_VALUE(ret_session);
}

static int session_end(void *vsession);
static int session_end_completion(void *vsession);

void ast_sip_session_terminate(struct ast_sip_session *session, int response)
{
	pj_status_t status;
	pjsip_tx_data *packet = NULL;
	SCOPE_ENTER(1, "%s Response %d\n", ast_sip_session_get_name(session), response);

	if (session->defer_terminate) {
		session->terminate_while_deferred = 1;
		SCOPE_EXIT_RTN("Deferred\n");
	}

	if (!response) {
		response = 603;
	}

	/* The media sessions need to exist for the lifetime of the underlying channel
	 * to ensure that anything (such as bridge_native_rtp) has access to them as
	 * appropriate. Since ast_sip_session_terminate is called by chan_pjsip and other
	 * places when the session is to be terminated we terminate any existing
	 * media sessions here.
	 */
	ast_sip_session_media_stats_save(session, session->active_media_state);
	SWAP(session->active_media_state, session->pending_media_state);
	ast_sip_session_media_state_reset(session->pending_media_state);

	switch (session->inv_session->state) {
	case PJSIP_INV_STATE_NULL:
		if (!session->inv_session->invite_tsx) {
			/*
			 * Normally, it's pjproject's transaction cleanup that ultimately causes the
			 * final session reference to be released but if both STATE and invite_tsx are NULL,
			 * we never created a transaction in the first place.  In this case, we need to
			 * do the cleanup ourselves.
			 */
			/* Transfer the inv_session session reference to the session_end_task */
			session->inv_session->mod_data[session_module.id] = NULL;
			pjsip_inv_terminate(session->inv_session, response, PJ_TRUE);
			session_end(session);
			/*
			 * session_end_completion will cleanup the final session reference unless
			 * ast_sip_session_terminate's caller is holding one.
			 */
			session_end_completion(session);
		} else {
			pjsip_inv_terminate(session->inv_session, response, PJ_TRUE);
		}
		break;
	case PJSIP_INV_STATE_CONFIRMED:
		if (session->inv_session->invite_tsx) {
			ast_debug(3, "%s: Delay sending BYE because of outstanding transaction...\n",
				ast_sip_session_get_name(session));
			/* If this is delayed the only thing that will happen is a BYE request so we don't
			 * actually need to store the response code for when it happens.
			 */
			delay_request(session, NULL, NULL, NULL, 0, DELAYED_METHOD_BYE, NULL, NULL, 1);
			break;
		}
		/* Fall through */
	default:
		status = pjsip_inv_end_session(session->inv_session, response, NULL, &packet);
		if (status == PJ_SUCCESS && packet) {
			struct ast_sip_session_delayed_request *delay;

			/* Flush any delayed requests so they cannot overlap this transaction. */
			while ((delay = AST_LIST_REMOVE_HEAD(&session->delayed_requests, next))) {
				delayed_request_free(delay);
			}

			if (packet->msg->type == PJSIP_RESPONSE_MSG) {
				ast_sip_session_send_response(session, packet);
			} else {
				ast_sip_session_send_request(session, packet);
			}
		}
		break;
	}
	SCOPE_EXIT_RTN();
}

static int session_termination_task(void *data)
{
	struct ast_sip_session *session = data;

	if (session->defer_terminate) {
		session->defer_terminate = 0;
		if (session->inv_session) {
			ast_sip_session_terminate(session, 0);
		}
	}

	ao2_ref(session, -1);
	return 0;
}

static void session_termination_cb(pj_timer_heap_t *timer_heap, struct pj_timer_entry *entry)
{
	struct ast_sip_session *session = entry->user_data;

	if (ast_sip_push_task(session->serializer, session_termination_task, session)) {
		ao2_cleanup(session);
	}
}

int ast_sip_session_defer_termination(struct ast_sip_session *session)
{
	pj_time_val delay = { .sec = 60, };
	int res;

	/* The session should not have an active deferred termination request. */
	ast_assert(!session->defer_terminate);

	session->defer_terminate = 1;

	session->defer_end = 1;
	session->ended_while_deferred = 0;

	ao2_ref(session, +1);
	pj_timer_entry_init(&session->scheduled_termination, 0, session, session_termination_cb);

	res = (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(),
		&session->scheduled_termination, &delay) != PJ_SUCCESS) ? -1 : 0;
	if (res) {
		session->defer_terminate = 0;
		ao2_ref(session, -1);
	}
	return res;
}

/*!
 * \internal
 * \brief Stop the defer termination timer if it is still running.
 * \since 13.5.0
 *
 * \param session Which session to stop the timer.
 */
static void sip_session_defer_termination_stop_timer(struct ast_sip_session *session)
{
	if (pj_timer_heap_cancel_if_active(pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()),
		&session->scheduled_termination, session->scheduled_termination.id)) {
		ao2_ref(session, -1);
	}
}

void ast_sip_session_defer_termination_cancel(struct ast_sip_session *session)
{
	if (!session->defer_terminate) {
		/* Already canceled or timer fired. */
		return;
	}

	session->defer_terminate = 0;

	if (session->terminate_while_deferred) {
		/* Complete the termination started by the upper layer. */
		ast_sip_session_terminate(session, 0);
	}

	/* Stop the termination timer if it is still running. */
	sip_session_defer_termination_stop_timer(session);
}

void ast_sip_session_end_if_deferred(struct ast_sip_session *session)
{
	if (!session->defer_end) {
		return;
	}

	session->defer_end = 0;

	if (session->ended_while_deferred) {
		/* Complete the session end started by the remote hangup. */
		ast_debug(3, "%s: Ending session after being deferred\n", ast_sip_session_get_name(session));
		session->ended_while_deferred = 0;
		session_end(session);
	}
}

struct ast_sip_session *ast_sip_dialog_get_session(pjsip_dialog *dlg)
{
	pjsip_inv_session *inv_session = pjsip_dlg_get_inv_session(dlg);
	struct ast_sip_session *session;

	if (!inv_session ||
		!(session = inv_session->mod_data[session_module.id])) {
		return NULL;
	}

	ao2_ref(session, +1);

	return session;
}

enum sip_get_destination_result {
	/*! The extension was successfully found */
	SIP_GET_DEST_EXTEN_FOUND,
	/*! The extension specified in the RURI was not found */
	SIP_GET_DEST_EXTEN_NOT_FOUND,
	/*! The extension specified in the RURI was a partial match */
	SIP_GET_DEST_EXTEN_PARTIAL,
	/*! The RURI is of an unsupported scheme */
	SIP_GET_DEST_UNSUPPORTED_URI,
};

/*!
 * \brief Determine where in the dialplan a call should go
 *
 * This uses the username in the request URI to try to match
 * an extension in the endpoint's configured context in order
 * to route the call.
 *
 * \param session The inbound SIP session
 * \param rdata The SIP INVITE
 */
static enum sip_get_destination_result get_destination(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	pjsip_uri *ruri = rdata->msg_info.msg->line.req.uri;
	pjsip_sip_uri *sip_ruri;
	struct ast_features_pickup_config *pickup_cfg;
	const char *pickupexten;

	if (!PJSIP_URI_SCHEME_IS_SIP(ruri) && !PJSIP_URI_SCHEME_IS_SIPS(ruri)) {
		return SIP_GET_DEST_UNSUPPORTED_URI;
	}

	sip_ruri = pjsip_uri_get_uri(ruri);
	ast_copy_pj_str(session->exten, &sip_ruri->user, sizeof(session->exten));

	/*
	 * We may want to match in the dialplan without any user
	 * options getting in the way.
	 */
	AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(session->exten);

	pickup_cfg = ast_get_chan_features_pickup_config(NULL); /* session->channel doesn't exist yet, using NULL */
	if (!pickup_cfg) {
		ast_log(LOG_ERROR, "%s: Unable to retrieve pickup configuration options. Unable to detect call pickup extension\n",
			ast_sip_session_get_name(session));
		pickupexten = "";
	} else {
		pickupexten = ast_strdupa(pickup_cfg->pickupexten);
		ao2_ref(pickup_cfg, -1);
	}

	if (!strcmp(session->exten, pickupexten) ||
		ast_exists_extension(NULL, session->endpoint->context, session->exten, 1, NULL)) {
		/*
		 * Save off the INVITE Request-URI in case it is
		 * needed: CHANNEL(pjsip,request_uri)
		 */
		session->request_uri = pjsip_uri_clone(session->inv_session->pool, ruri);

		return SIP_GET_DEST_EXTEN_FOUND;
	}

	/*
	 * Check for partial match via overlap dialling (if enabled)
	 */
	if (session->endpoint->allow_overlap && (
		!strncmp(session->exten, pickupexten, strlen(session->exten)) ||
		ast_canmatch_extension(NULL, session->endpoint->context, session->exten, 1, NULL))) {
		/* Overlap partial match */
		return SIP_GET_DEST_EXTEN_PARTIAL;
	}

	return SIP_GET_DEST_EXTEN_NOT_FOUND;
}

/*!
 * \internal
 * \brief Process initial answer for an incoming invite
 *
 * This function should only be called during the setup, and handling of a
 * new incoming invite. Most, if not all of the time, this will be called
 * when an error occurs and we need to respond as such.
 *
 * When a SIP session termination code is given for the answer it's assumed
 * this call then will be the final bit of processing before ending session
 * setup. As such, we've been holding a lock, and a reference on the invite
 * session's dialog. So before returning this function removes that reference,
 * and unlocks the dialog.
 *
 * \param inv_session The session on which to answer
 * \param rdata The original request
 * \param answer_code The answer's numeric code
 * \param terminate_code The termination code if the answer fails
 * \param notify Whether or not to call on_state_changed
 *
 * \retval 0 if invite successfully answered, -1 if an error occurred
 */
static int new_invite_initial_answer(pjsip_inv_session *inv_session, pjsip_rx_data *rdata,
	int answer_code, int terminate_code, pj_bool_t notify)
{
	pjsip_tx_data *tdata = NULL;
	int res = 0;

	if (inv_session->state != PJSIP_INV_STATE_DISCONNECTED) {
		if (pjsip_inv_initial_answer(
				inv_session, rdata, answer_code, NULL, NULL, &tdata) != PJ_SUCCESS) {

			pjsip_inv_terminate(inv_session, terminate_code ? terminate_code : answer_code, notify);
			res = -1;
		} else {
			pjsip_inv_send_msg(inv_session, tdata);
		}
	}

	if (answer_code >= 300) {
		/*
		 * A session is ending. The dialog has a reference that needs to be
		 * removed and holds a lock that needs to be unlocked before returning.
		 */
		pjsip_dlg_dec_lock(inv_session->dlg);
	}

	return res;
}

/*!
 * \internal
 * \brief Create and initialize a pjsip invite session
 *
 * pjsip_inv_session adds, and maintains a reference to the dialog upon a successful
 * invite session creation until the session is destroyed. However, we'll wait to
 * remove the reference that was added for the dialog when it gets created since we're
 * not ready to unlock the dialog in this function.
 *
 * So, if this function successfully returns that means it returns with its newly
 * created, and associated dialog locked and with two references (i.e. dialog's
 * reference count should be 2).
 *
 * \param rdata The request that is starting the dialog
 * \param endpoint A pointer to the endpoint
 *
 * \return A pjsip invite session object
 * \retval NULL on error
 */
static pjsip_inv_session *pre_session_setup(pjsip_rx_data *rdata, const struct ast_sip_endpoint *endpoint)
{
	pjsip_tx_data *tdata;
	pjsip_dialog *dlg;
	pjsip_inv_session *inv_session;
	unsigned int options = endpoint->extensions.flags;
	pj_status_t dlg_status = PJ_EUNKNOWN;

	if (pjsip_inv_verify_request(rdata, &options, NULL, NULL, ast_sip_get_pjsip_endpoint(), &tdata) != PJ_SUCCESS) {
		if (tdata) {
			if (pjsip_endpt_send_response2(ast_sip_get_pjsip_endpoint(), rdata, tdata, NULL, NULL) != PJ_SUCCESS) {
				pjsip_tx_data_dec_ref(tdata);
			}
		} else {
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		}
		return NULL;
	}

	dlg = ast_sip_create_dialog_uas_locked(endpoint, rdata, &dlg_status);
	if (!dlg) {
		if (dlg_status != PJ_EEXISTS) {
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		}
		return NULL;
	}

	/*
	 * The returned dialog holds a lock and has a reference added. Any paths where the
	 * dialog invite session is not returned must unlock the dialog and remove its reference.
	 */

	if (pjsip_inv_create_uas(dlg, rdata, NULL, options, &inv_session) != PJ_SUCCESS) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		/*
		 * The acquired dialog holds a lock, and a reference. Since the dialog is not
		 * going to be returned here it must first be unlocked and de-referenced. This
		 * must be done prior to calling dialog termination.
		 */
		pjsip_dlg_dec_lock(dlg);
		pjsip_dlg_terminate(dlg);
		return NULL;
	}

#if defined(HAVE_PJSIP_REPLACE_MEDIA_STREAM) || defined(PJMEDIA_SDP_NEG_ALLOW_MEDIA_CHANGE)
	inv_session->sdp_neg_flags = PJMEDIA_SDP_NEG_ALLOW_MEDIA_CHANGE;
#endif
	if (pjsip_dlg_add_usage(dlg, &session_module, NULL) != PJ_SUCCESS) {
		/* Dialog's lock and a reference are removed in new_invite_initial_answer */
		new_invite_initial_answer(inv_session, rdata, 500, 500, PJ_FALSE);
		/* Remove 2nd reference added at inv_session creation */
		pjsip_dlg_dec_session(inv_session->dlg, &session_module);
		return NULL;
	}

	return inv_session;
}

struct new_invite {
	/*! \brief Session created for the new INVITE */
	struct ast_sip_session *session;

	/*! \brief INVITE request itself */
	pjsip_rx_data *rdata;
};

static int check_sdp_content_type_supported(pjsip_media_type *content_type)
{
	pjsip_media_type app_sdp;
	pjsip_media_type_init2(&app_sdp, "application", "sdp");

	if (!pjsip_media_type_cmp(content_type, &app_sdp, 0)) {
		return 1;
	}

	return 0;
}

static int check_content_disposition_in_multipart(pjsip_multipart_part *part)
{
	pjsip_hdr *hdr = part->hdr.next;
	static const pj_str_t str_handling_required = {"handling=required", 16};

	while (hdr != &part->hdr) {
		if (hdr->type == PJSIP_H_OTHER) {
			pjsip_generic_string_hdr *generic_hdr = (pjsip_generic_string_hdr*)hdr;

			if (!pj_stricmp2(&hdr->name, "Content-Disposition") &&
				pj_stristr(&generic_hdr->hvalue, &str_handling_required) &&
				!check_sdp_content_type_supported(&part->body->content_type)) {
				return 1;
			}
		}
		hdr = hdr->next;
	}

	return 0;
}

/**
 * if there is required media we don't understand, return 1
 */
static int check_content_disposition(pjsip_rx_data *rdata)
{
	pjsip_msg_body *body = rdata->msg_info.msg->body;
	pjsip_ctype_hdr *ctype_hdr = rdata->msg_info.ctype;

	if (body && ctype_hdr &&
		ast_sip_is_media_type_in(&ctype_hdr->media, &pjsip_media_type_multipart_mixed,
			&pjsip_media_type_multipart_alternative, SENTINEL)) {
		pjsip_multipart_part *part = pjsip_multipart_get_first_part(body);
		while (part != NULL) {
			if (check_content_disposition_in_multipart(part)) {
				return 1;
			}
			part = pjsip_multipart_get_next_part(body, part);
		}
	}
	return 0;
}

static int new_invite(struct new_invite *invite)
{
	pjsip_tx_data *tdata = NULL;
	pjsip_timer_setting timer;
	pjsip_rdata_sdp_info *sdp_info;
	pjmedia_sdp_session *local = NULL;
	char buffer[AST_SOCKADDR_BUFLEN];
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(invite->session));


	/* From this point on, any calls to pjsip_inv_terminate have the last argument as PJ_TRUE
	 * so that we will be notified so we can destroy the session properly
	 */

	if (invite->session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_trace_log(-1, LOG_ERROR, "%s: Session already DISCONNECTED [reason=%d (%s)]\n",
			ast_sip_session_get_name(invite->session),
			invite->session->inv_session->cause,
			pjsip_get_status_text(invite->session->inv_session->cause)->ptr);
		SCOPE_EXIT_RTN_VALUE(-1);
	}

	switch (get_destination(invite->session, invite->rdata)) {
	case SIP_GET_DEST_EXTEN_FOUND:
		/* Things worked. Keep going */
		break;
	case SIP_GET_DEST_UNSUPPORTED_URI:
		ast_trace(-1, "%s: Call (%s:%s) to extension '%s' - unsupported uri\n",
			ast_sip_session_get_name(invite->session),
			invite->rdata->tp_info.transport->type_name,
			pj_sockaddr_print(&invite->rdata->pkt_info.src_addr, buffer, sizeof(buffer), 3),
			invite->session->exten);
		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 416, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 416, PJ_TRUE);
		}
		goto end;
	case SIP_GET_DEST_EXTEN_PARTIAL:
		ast_trace(-1, "%s: Call (%s:%s) to extension '%s' - partial match\n",
			ast_sip_session_get_name(invite->session),
			invite->rdata->tp_info.transport->type_name,
			pj_sockaddr_print(&invite->rdata->pkt_info.src_addr, buffer, sizeof(buffer), 3),
			invite->session->exten);

		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 484, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 484, PJ_TRUE);
		}
		goto end;
	case SIP_GET_DEST_EXTEN_NOT_FOUND:
	default:
		ast_trace_log(-1, LOG_NOTICE, "%s: Call (%s:%s) to extension '%s' rejected because extension not found in context '%s'.\n",
			ast_sip_session_get_name(invite->session),
			invite->rdata->tp_info.transport->type_name,
			pj_sockaddr_print(&invite->rdata->pkt_info.src_addr, buffer, sizeof(buffer), 3),
			invite->session->exten,
			invite->session->endpoint->context);

		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 404, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 404, PJ_TRUE);
		}
		goto end;
	};

	if (check_content_disposition(invite->rdata)) {
		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 415, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 415, PJ_TRUE);
		}
		goto end;
	}

	pjsip_timer_setting_default(&timer);
	timer.min_se = invite->session->endpoint->extensions.timer.min_se;
	timer.sess_expires = invite->session->endpoint->extensions.timer.sess_expires;
	pjsip_timer_init_session(invite->session->inv_session, &timer);

	/*
	 * At this point, we've verified what we can that won't take awhile,
	 * so let's go ahead and send a 100 Trying out to stop any
	 * retransmissions.
	 */
	ast_trace(-1, "%s: Call (%s:%s) to extension '%s' sending 100 Trying\n",
		ast_sip_session_get_name(invite->session),
		invite->rdata->tp_info.transport->type_name,
		pj_sockaddr_print(&invite->rdata->pkt_info.src_addr, buffer, sizeof(buffer), 3),
		invite->session->exten);
	if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 100, NULL, NULL, &tdata) != PJ_SUCCESS) {
		pjsip_inv_terminate(invite->session->inv_session, 500, PJ_TRUE);
		goto end;
	}
	ast_sip_session_send_response(invite->session, tdata);

	sdp_info = pjsip_rdata_get_sdp_info(invite->rdata);
	if (sdp_info && (sdp_info->sdp_err == PJ_SUCCESS) && sdp_info->sdp) {
		if (handle_incoming_sdp(invite->session, sdp_info->sdp)) {
			tdata = NULL;
			if (pjsip_inv_end_session(invite->session->inv_session, 488, NULL, &tdata) == PJ_SUCCESS
				&& tdata) {
				ast_sip_session_send_response(invite->session, tdata);
			}
			goto end;
		}
		/* We are creating a local SDP which is an answer to their offer */
		local = create_local_sdp(invite->session->inv_session, invite->session, sdp_info->sdp);
	} else {
		/* We are creating a local SDP which is an offer */
		local = create_local_sdp(invite->session->inv_session, invite->session, NULL);
	}

	/* If we were unable to create a local SDP terminate the session early, it won't go anywhere */
	if (!local) {
		tdata = NULL;
		if (pjsip_inv_end_session(invite->session->inv_session, 500, NULL, &tdata) == PJ_SUCCESS
			&& tdata) {
			ast_sip_session_send_response(invite->session, tdata);
		}
		goto end;
	}

	pjsip_inv_set_local_sdp(invite->session->inv_session, local);
	pjmedia_sdp_neg_set_prefer_remote_codec_order(invite->session->inv_session->neg, PJ_FALSE);
#ifdef PJMEDIA_SDP_NEG_ANSWER_MULTIPLE_CODECS
	if (!invite->session->endpoint->preferred_codec_only) {
		pjmedia_sdp_neg_set_answer_multiple_codecs(invite->session->inv_session->neg, PJ_TRUE);
	}
#endif

	handle_incoming_request(invite->session, invite->rdata);

end:
	SCOPE_EXIT_RTN_VALUE(0, "%s\n", ast_sip_session_get_name(invite->session));
}

static void handle_new_invite_request(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint,
			ast_pjsip_rdata_get_endpoint(rdata), ao2_cleanup);
	static const pj_str_t identity_str = { "Identity", 8 };
	const pj_str_t use_identity_header_str = {
		AST_STIR_SHAKEN_RESPONSE_STR_USE_IDENTITY_HEADER,
		strlen(AST_STIR_SHAKEN_RESPONSE_STR_USE_IDENTITY_HEADER)
	};
	pjsip_inv_session *inv_session = NULL;
	struct ast_sip_session *session;
	struct new_invite invite;
	char *req_uri = TRACE_ATLEAST(1) ? ast_alloca(256) : "";
	int res = TRACE_ATLEAST(1) ? pjsip_uri_print(PJSIP_URI_IN_REQ_URI, rdata->msg_info.msg->line.req.uri, req_uri, 256) : 0;
	SCOPE_ENTER(1, "Request: %s\n", res ? req_uri : "");

	ast_assert(endpoint != NULL);

	if ((endpoint->stir_shaken & AST_SIP_STIR_SHAKEN_VERIFY) &&
		!ast_sip_rdata_get_header_value(rdata, identity_str)) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata,
			AST_STIR_SHAKEN_RESPONSE_CODE_USE_IDENTITY_HEADER, &use_identity_header_str, NULL, NULL);
		ast_debug(3, "No Identity header when we require one\n");
		return;
	}

	inv_session = pre_session_setup(rdata, endpoint);
	if (!inv_session) {
		/* pre_session_setup() returns a response on failure */
		SCOPE_EXIT_RTN("Failure in pre session setup\n");
	}

	/*
	 * Upon a successful pre_session_setup the associated dialog is returned locked
	 * and with an added reference. Well actually two references. One added when the
	 * dialog itself was created, and another added when the pjsip invite session was
	 * created and the dialog was added to it.
	 *
	 * In order to ensure the dialog's, and any of its internal attributes, lifetimes
	 * we'll hold the lock and maintain the reference throughout the entire new invite
	 * handling process. See ast_sip_create_dialog_uas_locked for more details but,
	 * basically we do this to make sure a transport failure does not destroy the dialog
	 * and/or transaction out from underneath us between pjsip calls. Alternatively, we
	 * could probably release the lock if we needed to, but then we'd have to re-lock and
	 * check the dialog and transaction prior to every pjsip call.
	 *
	 * That means any off nominal/failure paths in this function must remove the associated
	 * dialog reference added at dialog creation, and remove the lock. As well the
	 * referenced pjsip invite session must be "cleaned up", which should also then
	 * remove its reference to the dialog at that time.
	 *
	 * Nominally we'll unlock the dialog, and release the reference when all new invite
	 * process handling has successfully completed.
	 */

	session = ast_sip_session_alloc(endpoint, NULL, inv_session, rdata);
	if (!session) {
		/* Dialog's lock and reference are removed in new_invite_initial_answer */
		if (!new_invite_initial_answer(inv_session, rdata, 500, 500, PJ_FALSE)) {
			/* Terminate the session if it wasn't done in the answer */
			pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		}
		SCOPE_EXIT_RTN("Couldn't create session\n");
	}
	session->call_direction = AST_SIP_SESSION_INCOMING_CALL;

	/*
	 * The current thread is supposed be the session serializer to prevent
	 * any initial INVITE retransmissions from trying to setup the same
	 * call again.
	 */
	ast_assert(ast_taskprocessor_is_task(session->serializer));

	invite.session = session;
	invite.rdata = rdata;
	new_invite(&invite);

	/*
	 * The dialog lock and reference added at dialog creation time must be
	 * maintained throughout the new invite process. Since we're pretty much
	 * done at this point with things it's safe to go ahead and remove the lock
	 * and the reference here. See ast_sip_create_dialog_uas_locked for more info.
	 *
	 * Note, any future functionality added that does work using the dialog must
	 * be done before this.
	 */
	pjsip_dlg_dec_lock(inv_session->dlg);

	SCOPE_EXIT("Request: %s Session: %s\n", req_uri, ast_sip_session_get_name(session));
	ao2_ref(session, -1);
}

static pj_bool_t does_method_match(const pj_str_t *message_method, const char *supplement_method)
{
	pj_str_t method;

	if (ast_strlen_zero(supplement_method)) {
		return PJ_TRUE;
	}

	pj_cstr(&method, supplement_method);

	return pj_stristr(&method, message_method) ? PJ_TRUE : PJ_FALSE;
}

static pj_bool_t has_supplement(const struct ast_sip_session *session, const pjsip_rx_data *rdata)
{
	struct ast_sip_session_supplement *supplement;
	struct pjsip_method *method = &rdata->msg_info.msg->line.req.method;

	if (!session) {
		return PJ_FALSE;
	}

	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (does_method_match(&method->name, supplement->method)) {
			return PJ_TRUE;
		}
	}
	return PJ_FALSE;
}

/*!
 * \internal
 * Added for debugging purposes
 */
static void session_on_tsx_state(pjsip_transaction *tsx, pjsip_event *e)
{

	pjsip_dialog *dlg = pjsip_tsx_get_dlg(tsx);
	pjsip_inv_session *inv_session = (dlg ? pjsip_dlg_get_inv_session(dlg) : NULL);
	struct ast_sip_session *session = (inv_session ? inv_session->mod_data[session_module.id] : NULL);
	SCOPE_ENTER(1, "%s TSX State: %s  Inv State: %s\n", ast_sip_session_get_name(session),
			pjsip_tsx_state_str(tsx->state), inv_session ? pjsip_inv_state_name(inv_session->state) : "unknown");

	if (session) {
		ast_trace(2, "Topology: Pending: %s  Active: %s\n",
			ast_str_tmp(256, ast_stream_topology_to_str(session->pending_media_state->topology, &STR_TMP)),
			ast_str_tmp(256, ast_stream_topology_to_str(session->active_media_state->topology, &STR_TMP)));
	}

	SCOPE_EXIT_RTN();
}

/*!
 * \internal
 * Added for debugging purposes
 */
static pj_bool_t session_on_rx_response(pjsip_rx_data *rdata)
{

	struct pjsip_status_line status = rdata->msg_info.msg->line.status;
	pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
	pjsip_inv_session *inv_session = dlg ? pjsip_dlg_get_inv_session(dlg) : NULL;
	struct ast_sip_session *session = (inv_session ? inv_session->mod_data[session_module.id] : NULL);
	SCOPE_ENTER(1, "%s Method: %.*s Status: %d\n", ast_sip_session_get_name(session),
		(int)rdata->msg_info.cseq->method.name.slen, rdata->msg_info.cseq->method.name.ptr, status.code);

	SCOPE_EXIT_RTN_VALUE(PJ_FALSE);
}

/*!
 * \brief Called when a new SIP request comes into PJSIP
 *
 * This function is called under two circumstances
 * 1) An out-of-dialog request is received by PJSIP
 * 2) An in-dialog request that the inv_session layer does not
 *    handle is received (such as an in-dialog INFO)
 *
 * Except for INVITEs, there is very little we actually do in this function
 * 1) For requests we don't handle, we return PJ_FALSE
 * 2) For new INVITEs, handle them now to prevent retransmissions from
 *    trying to setup the same call again.
 * 3) For in-dialog requests we handle, we process them in the
 *    .on_state_changed = session_inv_on_state_changed or
 *    .on_tsx_state_changed = session_inv_on_tsx_state_changed
 *    callbacks instead.
 */
static pj_bool_t session_on_rx_request(pjsip_rx_data *rdata)
{
	pj_status_t handled = PJ_FALSE;
	struct pjsip_request_line req = rdata->msg_info.msg->line.req;
	pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
	pjsip_inv_session *inv_session = (dlg ? pjsip_dlg_get_inv_session(dlg) : NULL);
	struct ast_sip_session *session = (inv_session ? inv_session->mod_data[session_module.id] : NULL);
	char *req_uri = TRACE_ATLEAST(1) ? ast_alloca(256) : "";
	int res = TRACE_ATLEAST(1) ? pjsip_uri_print(PJSIP_URI_IN_REQ_URI, rdata->msg_info.msg->line.req.uri, req_uri, 256) : 0;
	SCOPE_ENTER(1, "%s Request: %.*s %s\n", ast_sip_session_get_name(session),
		(int) pj_strlen(&req.method.name), pj_strbuf(&req.method.name), res ? req_uri : "");

	switch (req.method.id) {
	case PJSIP_INVITE_METHOD:
		if (dlg) {
			ast_log(LOG_WARNING, "on_rx_request called for INVITE in mid-dialog?\n");
			break;
		}
		handled = PJ_TRUE;
		handle_new_invite_request(rdata);
		break;
	default:
		/* Handle other in-dialog methods if their supplements have been registered */
		handled = dlg && (inv_session = pjsip_dlg_get_inv_session(dlg)) &&
			has_supplement(inv_session->mod_data[session_module.id], rdata);
		break;
	}

	SCOPE_EXIT_RTN_VALUE(handled, "%s Handled request %.*s %s ? %s\n", ast_sip_session_get_name(session),
		(int) pj_strlen(&req.method.name), pj_strbuf(&req.method.name), req_uri,
		handled == PJ_TRUE ? "yes" : "no");
}

static void resend_reinvite(pj_timer_heap_t *timer, pj_timer_entry *entry)
{
	struct ast_sip_session *session = entry->user_data;

	ast_debug(3, "%s: re-INVITE collision timer expired.\n",
		ast_sip_session_get_name(session));

	if (AST_LIST_EMPTY(&session->delayed_requests)) {
		/* No delayed request pending, so just return */
		ao2_ref(session, -1);
		return;
	}
	if (ast_sip_push_task(session->serializer, invite_collision_timeout, session)) {
		/*
		 * Uh oh.  We now have nothing in the foreseeable future
		 * to trigger sending the delayed requests.
		 */
		ao2_ref(session, -1);
	}
}

static void reschedule_reinvite(struct ast_sip_session *session, ast_sip_session_response_cb on_response)
{
	pjsip_inv_session *inv = session->inv_session;
	pj_time_val tv;
	struct ast_sip_session_media_state *pending_media_state = NULL;
	struct ast_sip_session_media_state *active_media_state = NULL;
	const char *session_name = ast_sip_session_get_name(session);
	int use_pending = 0;
	int use_active = 0;

	SCOPE_ENTER(3, "%s\n", session_name);

	/*
	 * If the two media state topologies are the same this means that the session refresh request
	 * did not specify a desired topology, so it does not care. If that is the case we don't even
	 * pass one in here resulting in the current topology being used.  It's possible though that
	 * either one of the topologies could be NULL so we have to test for that before we check for
	 * equality.
	 */

	/* We only want to clone a media state if its topology is not null */
	use_pending = session->pending_media_state->topology != NULL;
	use_active = session->active_media_state->topology != NULL;

	/*
	 * If both media states have topologies, we can test for equality.  If they're equal we're not going to
	 * clone either states.
	 */
	if (use_pending && use_active && ast_stream_topology_equal(session->active_media_state->topology, session->pending_media_state->topology)) {
		use_pending = 0;
		use_active = 0;
	}

	if (use_pending) {
		pending_media_state = ast_sip_session_media_state_clone(session->pending_media_state);
		if (!pending_media_state) {
			SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Failed to clone pending media state\n", session_name);
		}
	}

	if (use_active) {
		active_media_state = ast_sip_session_media_state_clone(session->active_media_state);
		if (!active_media_state) {
			ast_sip_session_media_state_free(pending_media_state);
			SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Failed to clone active media state\n", session_name);
		}
	}

	if (delay_request(session, NULL, NULL, on_response, 1, DELAYED_METHOD_INVITE, pending_media_state,
		active_media_state, 1)) {
		ast_sip_session_media_state_free(pending_media_state);
		ast_sip_session_media_state_free(active_media_state);
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Failed to add delayed request\n", session_name);
	}

	if (pj_timer_entry_running(&session->rescheduled_reinvite)) {
		/* Timer already running.  Something weird is going on. */
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: re-INVITE collision while timer running!!!\n", session_name);
	}

	tv.sec = 0;
	if (inv->role == PJSIP_ROLE_UAC) {
		tv.msec = 2100 + ast_random() % 2000;
	} else {
		tv.msec = ast_random() % 2000;
	}
	pj_timer_entry_init(&session->rescheduled_reinvite, 0, session, resend_reinvite);

	ao2_ref(session, +1);
	if (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(),
		&session->rescheduled_reinvite, &tv) != PJ_SUCCESS) {
		ao2_ref(session, -1);
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Couldn't schedule timer\n", session_name);
	}

	SCOPE_EXIT_RTN();
}

static void __print_debug_details(const char *function, pjsip_inv_session *inv, pjsip_transaction *tsx, pjsip_event *e)
{
	int id = session_module.id;
	struct ast_sip_session *session = NULL;

	if (!DEBUG_ATLEAST(5)) {
		/* Debug not spamy enough */
		return;
	}

	ast_log(LOG_DEBUG, "Function %s called on event %s\n",
		function, pjsip_event_str(e->type));
	if (!inv) {
		ast_log(LOG_DEBUG, "Transaction %p does not belong to an inv_session?\n", tsx);
		ast_log(LOG_DEBUG, "The transaction state is %s\n",
			pjsip_tsx_state_str(tsx->state));
		return;
	}
	if (id > -1) {
		session = inv->mod_data[session_module.id];
	}
	if (!session) {
		ast_log(LOG_DEBUG, "inv_session %p has no ast session\n", inv);
	} else {
		ast_log(LOG_DEBUG, "The state change pertains to the endpoint '%s(%s)'\n",
			ast_sorcery_object_get_id(session->endpoint),
			session->channel ? ast_channel_name(session->channel) : "");
	}
	if (inv->invite_tsx) {
		ast_log(LOG_DEBUG, "The inv session still has an invite_tsx (%p)\n",
			inv->invite_tsx);
	} else {
		ast_log(LOG_DEBUG, "The inv session does NOT have an invite_tsx\n");
	}
	if (tsx) {
		ast_log(LOG_DEBUG, "The %s %.*s transaction involved in this state change is %p\n",
			pjsip_role_name(tsx->role),
			(int) pj_strlen(&tsx->method.name), pj_strbuf(&tsx->method.name),
			tsx);
		ast_log(LOG_DEBUG, "The current transaction state is %s\n",
			pjsip_tsx_state_str(tsx->state));
		ast_log(LOG_DEBUG, "The transaction state change event is %s\n",
			pjsip_event_str(e->body.tsx_state.type));
	} else {
		ast_log(LOG_DEBUG, "There is no transaction involved in this state change\n");
	}
	ast_log(LOG_DEBUG, "The current inv state is %s\n", pjsip_inv_state_name(inv->state));
}

#define print_debug_details(inv, tsx, e) __print_debug_details(__PRETTY_FUNCTION__, (inv), (tsx), (e))

static void handle_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	struct ast_sip_session_supplement *supplement;
	struct pjsip_request_line req = rdata->msg_info.msg->line.req;
	SCOPE_ENTER(3, "%s: Method is %.*s\n", ast_sip_session_get_name(session), (int) pj_strlen(&req.method.name), pj_strbuf(&req.method.name));

	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (supplement->incoming_request && does_method_match(&req.method.name, supplement->method)) {
			if (supplement->incoming_request(session, rdata)) {
				break;
			}
		}
	}

	SCOPE_EXIT("%s\n", ast_sip_session_get_name(session));
}

static void handle_session_begin(struct ast_sip_session *session)
{
	struct ast_sip_session_supplement *iter;

	AST_LIST_TRAVERSE(&session->supplements, iter, next) {
		if (iter->session_begin) {
			iter->session_begin(session);
		}
	}
}

static void handle_session_destroy(struct ast_sip_session *session)
{
	struct ast_sip_session_supplement *iter;

	AST_LIST_TRAVERSE(&session->supplements, iter, next) {
		if (iter->session_destroy) {
			iter->session_destroy(session);
		}
	}
}

static void handle_session_end(struct ast_sip_session *session)
{
	struct ast_sip_session_supplement *iter;

	/* Session is dead.  Notify the supplements. */
	AST_LIST_TRAVERSE(&session->supplements, iter, next) {
		if (iter->session_end) {
			iter->session_end(session);
		}
	}
}

static void handle_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata,
		enum ast_sip_session_response_priority response_priority)
{
	struct ast_sip_session_supplement *supplement;
	struct pjsip_status_line status = rdata->msg_info.msg->line.status;
	SCOPE_ENTER(3, "%s: Response is %d %.*s\n", ast_sip_session_get_name(session),
		status.code, (int) pj_strlen(&status.reason), pj_strbuf(&status.reason));

	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (!(supplement->response_priority & response_priority)) {
			continue;
		}
		if (supplement->incoming_response && does_method_match(&rdata->msg_info.cseq->method.name, supplement->method)) {
			supplement->incoming_response(session, rdata);
		}
	}

	SCOPE_EXIT("%s\n", ast_sip_session_get_name(session));
}

static int handle_incoming(struct ast_sip_session *session, pjsip_rx_data *rdata,
		enum ast_sip_session_response_priority response_priority)
{
	if (rdata->msg_info.msg->type == PJSIP_REQUEST_MSG) {
		handle_incoming_request(session, rdata);
	} else {
		handle_incoming_response(session, rdata, response_priority);
	}

	return 0;
}

static void handle_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	struct ast_sip_session_supplement *supplement;
	struct pjsip_request_line req = tdata->msg->line.req;
	SCOPE_ENTER(3, "%s: Method is %.*s\n", ast_sip_session_get_name(session),
		(int) pj_strlen(&req.method.name), pj_strbuf(&req.method.name));

	ast_sip_message_apply_transport(session->endpoint->transport, tdata);

	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (supplement->outgoing_request && does_method_match(&req.method.name, supplement->method)) {
			supplement->outgoing_request(session, tdata);
		}
	}
	SCOPE_EXIT("%s\n", ast_sip_session_get_name(session));
}

static void handle_outgoing_response(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	struct ast_sip_session_supplement *supplement;
	struct pjsip_status_line status = tdata->msg->line.status;
	pjsip_cseq_hdr *cseq = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL);
	SCOPE_ENTER(3, "%s: Method is %.*s, Response is %d %.*s\n", ast_sip_session_get_name(session),
		(int) pj_strlen(&cseq->method.name),
		pj_strbuf(&cseq->method.name), status.code, (int) pj_strlen(&status.reason),
		pj_strbuf(&status.reason));


	if (!cseq) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Cannot send response due to missing sequence header",
			ast_sip_session_get_name(session));
	}

	ast_sip_message_apply_transport(session->endpoint->transport, tdata);

	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (supplement->outgoing_response && does_method_match(&cseq->method.name, supplement->method)) {
			supplement->outgoing_response(session, tdata);
		}
	}

	SCOPE_EXIT("%s\n", ast_sip_session_get_name(session));
}

static int session_end(void *vsession)
{
	struct ast_sip_session *session = vsession;

	/* Stop the scheduled termination */
	sip_session_defer_termination_stop_timer(session);

	/* Session is dead.  Notify the supplements. */
	handle_session_end(session);

	return 0;
}

/*!
 * \internal
 * \brief Complete ending session activities.
 * \since 13.5.0
 *
 * \param vsession Which session to complete stopping.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int session_end_completion(void *vsession)
{
	struct ast_sip_session *session = vsession;

	ast_sip_dialog_set_serializer(session->inv_session->dlg, NULL);
	ast_sip_dialog_set_endpoint(session->inv_session->dlg, NULL);

	/* Now we can release the ref that was held by session->inv_session */
	ao2_cleanup(session);
	return 0;
}

static int check_request_status(pjsip_inv_session *inv, pjsip_event *e)
{
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	pjsip_transaction *tsx = e->body.tsx_state.tsx;

	if (tsx->status_code != 503 && tsx->status_code != 408) {
		return 0;
	}

	if (!ast_sip_failover_request(tsx->last_tx)) {
		return 0;
	}

	pjsip_inv_uac_restart(inv, PJ_FALSE);
	/*
	 * Bump the ref since it will be on a new transaction and
	 * we don't want it to go away along with the old transaction.
	 */
	pjsip_tx_data_add_ref(tsx->last_tx);
	ast_sip_session_send_request(session, tsx->last_tx);
	return 1;
}

static void handle_incoming_before_media(pjsip_inv_session *inv,
	struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	pjsip_msg *msg;
	ast_debug(3, "%s: Received %s\n", ast_sip_session_get_name(session), rdata->msg_info.msg->type == PJSIP_REQUEST_MSG ?
			"request" : "response");


	handle_incoming(session, rdata, AST_SIP_SESSION_BEFORE_MEDIA);
	msg = rdata->msg_info.msg;
	if (msg->type == PJSIP_REQUEST_MSG
		&& msg->line.req.method.id == PJSIP_ACK_METHOD
		&& pjmedia_sdp_neg_get_state(inv->neg) != PJMEDIA_SDP_NEG_STATE_DONE) {
		pjsip_tx_data *tdata;

		/*
		 * SDP negotiation failed on an incoming call that delayed
		 * negotiation and then gave us an invalid SDP answer.  We
		 * need to send a BYE to end the call because of the invalid
		 * SDP answer.
		 */
		ast_debug(1,
			"%s: Ending session due to incomplete SDP negotiation.  %s\n",
			ast_sip_session_get_name(session),
			pjsip_rx_data_get_info(rdata));
		if (pjsip_inv_end_session(inv, 400, NULL, &tdata) == PJ_SUCCESS
			&& tdata) {
			ast_sip_session_send_request(session, tdata);
		}
	}
}

static void session_inv_on_state_changed(pjsip_inv_session *inv, pjsip_event *e)
{
	pjsip_event_id_e type;
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	SCOPE_ENTER(1, "%s Event: %s  Inv State: %s\n", ast_sip_session_get_name(session),
		pjsip_event_str(e->type), pjsip_inv_state_name(inv->state));

	if (ast_shutdown_final()) {
		SCOPE_EXIT_RTN("Shutting down\n");
	}

	if (e) {
		print_debug_details(inv, NULL, e);
		type = e->type;
	} else {
		type = PJSIP_EVENT_UNKNOWN;
	}

	session = inv->mod_data[session_module.id];
	if (!session) {
		SCOPE_EXIT_RTN("No session\n");
	}

	switch(type) {
	case PJSIP_EVENT_TX_MSG:
		break;
	case PJSIP_EVENT_RX_MSG:
		handle_incoming_before_media(inv, session, e->body.rx_msg.rdata);
		break;
	case PJSIP_EVENT_TSX_STATE:
		ast_debug(3, "%s: Source of transaction state change is %s\n", ast_sip_session_get_name(session),
			pjsip_event_str(e->body.tsx_state.type));
		/* Transaction state changes are prompted by some other underlying event. */
		switch(e->body.tsx_state.type) {
		case PJSIP_EVENT_TX_MSG:
			break;
		case PJSIP_EVENT_RX_MSG:
			if (!check_request_status(inv, e)) {
				handle_incoming_before_media(inv, session, e->body.tsx_state.src.rdata);
			}
			break;
		case PJSIP_EVENT_TRANSPORT_ERROR:
		case PJSIP_EVENT_TIMER:
			/*
			 * Check the request status on transport error or timeout. A transport
			 * error can occur when a TCP socket closes and that can be the result
			 * of a 503. Also we may need to failover on a timeout (408).
			 */
			check_request_status(inv, e);
			break;
		case PJSIP_EVENT_USER:
		case PJSIP_EVENT_UNKNOWN:
		case PJSIP_EVENT_TSX_STATE:
			/* Inception? */
			break;
		}
		break;
	case PJSIP_EVENT_TRANSPORT_ERROR:
	case PJSIP_EVENT_TIMER:
	case PJSIP_EVENT_UNKNOWN:
	case PJSIP_EVENT_USER:
	default:
		break;
	}

	if (inv->state == PJSIP_INV_STATE_DISCONNECTED) {
		if (session->defer_end) {
			ast_debug(3, "%s: Deferring session end\n", ast_sip_session_get_name(session));
			session->ended_while_deferred = 1;
			SCOPE_EXIT_RTN("Deferring\n");
		}

		if (ast_sip_push_task(session->serializer, session_end, session)) {
			/* Do it anyway even though this is not the right thread. */
			session_end(session);
		}
	}

	SCOPE_EXIT_RTN();
}

static void session_inv_on_new_session(pjsip_inv_session *inv, pjsip_event *e)
{
	/* XXX STUB */
}

static int session_end_if_disconnected(int id, pjsip_inv_session *inv)
{
	struct ast_sip_session *session;

	if (inv->state != PJSIP_INV_STATE_DISCONNECTED) {
		return 0;
	}

	/*
	 * We are locking because ast_sip_dialog_get_session() needs
	 * the dialog locked to get the session by other threads.
	 */
	pjsip_dlg_inc_lock(inv->dlg);
	session = inv->mod_data[id];
	inv->mod_data[id] = NULL;
	pjsip_dlg_dec_lock(inv->dlg);

	/*
	 * Pass the session ref held by session->inv_session to
	 * session_end_completion().
	 */
	if (session
		&& ast_sip_push_task(session->serializer, session_end_completion, session)) {
		/* Do it anyway even though this is not the right thread. */
		session_end_completion(session);
	}

	return 1;
}

static void session_inv_on_tsx_state_changed(pjsip_inv_session *inv, pjsip_transaction *tsx, pjsip_event *e)
{
	ast_sip_session_response_cb cb;
	int id = session_module.id;
	pjsip_tx_data *tdata;
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	SCOPE_ENTER(1, "%s TSX State: %s  Inv State: %s\n", ast_sip_session_get_name(session),
		pjsip_tsx_state_str(tsx->state), pjsip_inv_state_name(inv->state));

	if (ast_shutdown_final()) {
		SCOPE_EXIT_RTN("Shutting down\n");
	}

	session = inv->mod_data[id];

	print_debug_details(inv, tsx, e);
	if (!session) {
		/* The session has ended.  Ignore the transaction change. */
		SCOPE_EXIT_RTN("Session ended\n");
	}

	/*
	 * If the session is disconnected really nothing else to do unless currently transacting
	 * a BYE. If a BYE then hold off destruction until the transaction timeout occurs. This
	 * has to be done for BYEs because sometimes the dialog can be in a disconnected
	 * state but the BYE request transaction has not yet completed.
	 */
	if (tsx->method.id != PJSIP_BYE_METHOD && session_end_if_disconnected(id, inv)) {
		SCOPE_EXIT_RTN("Disconnected\n");
	}

	switch (e->body.tsx_state.type) {
	case PJSIP_EVENT_TX_MSG:
		/* When we create an outgoing request, we do not have access to the transaction that
		 * is created. Instead, We have to place transaction-specific data in the tdata. Here,
		 * we transfer the data into the transaction. This way, when we receive a response, we
		 * can dig this data out again
		 */
		tsx->mod_data[id] = e->body.tsx_state.src.tdata->mod_data[id];
		break;
	case PJSIP_EVENT_RX_MSG:
		cb = ast_sip_mod_data_get(tsx->mod_data, id, MOD_DATA_ON_RESPONSE);
		/* As the PJSIP invite session implementation responds with a 200 OK before we have a
		 * chance to be invoked session supplements for BYE requests actually end up executing
		 * in the invite session state callback as well. To prevent session supplements from
		 * running on the BYE request again we explicitly squash invocation of them here.
		 */
		if ((e->body.tsx_state.src.rdata->msg_info.msg->type != PJSIP_REQUEST_MSG) ||
			(tsx->method.id != PJSIP_BYE_METHOD)) {
			handle_incoming(session, e->body.tsx_state.src.rdata,
				AST_SIP_SESSION_AFTER_MEDIA);
		}
		if (tsx->method.id == PJSIP_INVITE_METHOD) {
			if (tsx->role == PJSIP_ROLE_UAC) {
				if (tsx->state == PJSIP_TSX_STATE_COMPLETED) {
					/* This means we got a non 2XX final response to our outgoing INVITE */
					if (tsx->status_code == PJSIP_SC_REQUEST_PENDING) {
						reschedule_reinvite(session, cb);
						SCOPE_EXIT_RTN("Non 2XX final response\n");
					}
					if (inv->state == PJSIP_INV_STATE_CONFIRMED) {
						ast_debug(1, "%s: reINVITE received final response code %d\n",
							ast_sip_session_get_name(session),
							tsx->status_code);
						if ((tsx->status_code == 401 || tsx->status_code == 407)
							&& ++session->authentication_challenge_count < MAX_RX_CHALLENGES
							&& !ast_sip_create_request_with_auth(
								&session->endpoint->outbound_auths,
								e->body.tsx_state.src.rdata, tsx->last_tx, &tdata)) {
							/* Send authed reINVITE */
							ast_sip_session_send_request_with_cb(session, tdata, cb);
							SCOPE_EXIT_RTN("Sending authed reinvite\n");
						}
						/* Per RFC3261 14.1 a response to a re-INVITE should only terminate
						 * the dialog if a 481 or 408 occurs. All other responses should leave
						 * the dialog untouched.
						 */
						if (tsx->status_code == 481 || tsx->status_code == 408) {
							if (pjsip_inv_end_session(inv, 500, NULL, &tdata) == PJ_SUCCESS
								&& tdata) {
								ast_sip_session_send_request(session, tdata);
							}
						}
					}
				} else if (tsx->state == PJSIP_TSX_STATE_TERMINATED) {
					if (!inv->cancelling
						&& inv->role == PJSIP_ROLE_UAC
						&& inv->state == PJSIP_INV_STATE_CONFIRMED
						&& pjmedia_sdp_neg_was_answer_remote(inv->neg)
						&& pjmedia_sdp_neg_get_state(inv->neg) == PJMEDIA_SDP_NEG_STATE_DONE
						&& (session->channel && ast_channel_hangupcause(session->channel) == AST_CAUSE_BEARERCAPABILITY_NOTAVAIL)
						) {
						/*
						 * We didn't send a CANCEL but the UAS sent us the 200 OK with an invalid or unacceptable codec SDP.
						 * In this case the SDP negotiation is incomplete and PJPROJECT has already sent the ACK.
						 * So, we send the BYE with 503 status code here. And the actual hangup cause code is already set
						 * to AST_CAUSE_BEARERCAPABILITY_NOTAVAIL by the session_inv_on_media_update(), setting the 503
						 * status code doesn't affect to hangup cause code.
						 */
						ast_debug(1, "Endpoint '%s(%s)': Ending session due to 200 OK with incomplete SDP negotiation.  %s\n",
							ast_sorcery_object_get_id(session->endpoint),
							session->channel ? ast_channel_name(session->channel) : "",
							pjsip_rx_data_get_info(e->body.tsx_state.src.rdata));
						pjsip_inv_end_session(session->inv_session, 503, NULL, &tdata);
						SCOPE_EXIT_RTN("Incomplete SDP negotiation\n");
					}

					if (inv->cancelling && tsx->status_code == PJSIP_SC_OK) {
						int sdp_negotiation_done =
							pjmedia_sdp_neg_get_state(inv->neg) == PJMEDIA_SDP_NEG_STATE_DONE;

						/*
						 * We can get here for the following reasons.
						 *
						 * 1) The race condition detailed in RFC5407 section 3.1.2.
						 * We sent a CANCEL at the same time that the UAS sent us a
						 * 200 OK with a valid SDP for the original INVITE.  As a
						 * result, we have now received a 200 OK for a cancelled
						 * call and the SDP negotiation is complete.  We need to
						 * immediately send a BYE to end the dialog.
						 *
						 * 2) We sent a CANCEL and hit the race condition but the
						 * UAS sent us an invalid SDP with the 200 OK.  In this case
						 * the SDP negotiation is incomplete and PJPROJECT has
						 * already sent the BYE for us because of the invalid SDP.
						 */
						ast_test_suite_event_notify("PJSIP_SESSION_CANCELED",
							"Endpoint: %s\r\n"
							"Channel: %s\r\n"
							"Message: %s\r\n"
							"SDP: %s",
							ast_sorcery_object_get_id(session->endpoint),
							session->channel ? ast_channel_name(session->channel) : "",
							pjsip_rx_data_get_info(e->body.tsx_state.src.rdata),
							sdp_negotiation_done ? "complete" : "incomplete");
						if (!sdp_negotiation_done) {
							ast_debug(1, "%s: Incomplete SDP negotiation cancelled session.  %s\n",
								ast_sip_session_get_name(session),
								pjsip_rx_data_get_info(e->body.tsx_state.src.rdata));
						} else if (pjsip_inv_end_session(inv, 500, NULL, &tdata) == PJ_SUCCESS
							&& tdata) {
							ast_debug(1, "%s: Ending session due to RFC5407 race condition.  %s\n",
								ast_sip_session_get_name(session),
								pjsip_rx_data_get_info(e->body.tsx_state.src.rdata));
							ast_sip_session_send_request(session, tdata);
						}
					}
				}
			}
		} else {
			/* All other methods */
			if (tsx->role == PJSIP_ROLE_UAC) {
				if (tsx->state == PJSIP_TSX_STATE_COMPLETED) {
					/* This means we got a final response to our outgoing method */
					ast_debug(1, "%s: %.*s received final response code %d\n",
						ast_sip_session_get_name(session),
						(int) pj_strlen(&tsx->method.name), pj_strbuf(&tsx->method.name),
						tsx->status_code);
					if ((tsx->status_code == 401 || tsx->status_code == 407)
						&& ++session->authentication_challenge_count < MAX_RX_CHALLENGES
						&& !ast_sip_create_request_with_auth(
							&session->endpoint->outbound_auths,
							e->body.tsx_state.src.rdata, tsx->last_tx, &tdata)) {
						/* Send authed version of the method */
						ast_sip_session_send_request_with_cb(session, tdata, cb);
						SCOPE_EXIT_RTN("Sending authed %.*s\n",
							(int) pj_strlen(&tsx->method.name), pj_strbuf(&tsx->method.name));
					}
				}
			}
		}
		if (cb) {
			cb(session, e->body.tsx_state.src.rdata);
		}
		break;
	case PJSIP_EVENT_TRANSPORT_ERROR:
	case PJSIP_EVENT_TIMER:
		/*
		 * The timer event is run by the pjsip monitor thread and not
		 * by the session serializer.
		 */
		if (session_end_if_disconnected(id, inv)) {
			SCOPE_EXIT_RTN("Disconnected\n");
		}
		break;
	case PJSIP_EVENT_USER:
	case PJSIP_EVENT_UNKNOWN:
	case PJSIP_EVENT_TSX_STATE:
		/* Inception? */
		break;
	}

	if (AST_LIST_EMPTY(&session->delayed_requests)) {
		/* No delayed request pending, so just return */
		SCOPE_EXIT_RTN("Nothing delayed\n");
	}

	if (tsx->method.id == PJSIP_INVITE_METHOD) {
		if (tsx->state == PJSIP_TSX_STATE_PROCEEDING) {
			ast_debug(3, "%s: INVITE delay check. tsx-state:%s\n",
				ast_sip_session_get_name(session),
				pjsip_tsx_state_str(tsx->state));
			check_delayed_requests(session, invite_proceeding);
		} else if (tsx->state == PJSIP_TSX_STATE_TERMINATED) {
			/*
			 * Terminated INVITE transactions always should result in
			 * queuing delayed requests, no matter what event caused
			 * the transaction to terminate.
			 */
			ast_debug(3, "%s: INVITE delay check. tsx-state:%s\n",
				ast_sip_session_get_name(session),
				pjsip_tsx_state_str(tsx->state));
			check_delayed_requests(session, invite_terminated);
		}
	} else if (tsx->role == PJSIP_ROLE_UAC
		&& tsx->state == PJSIP_TSX_STATE_COMPLETED
		&& !pj_strcmp2(&tsx->method.name, "UPDATE")) {
		ast_debug(3, "%s: UPDATE delay check. tsx-state:%s\n",
			ast_sip_session_get_name(session),
			pjsip_tsx_state_str(tsx->state));
		check_delayed_requests(session, update_completed);
	}

	SCOPE_EXIT_RTN();
}

static int add_sdp_streams(struct ast_sip_session_media *session_media,
	struct ast_sip_session *session, pjmedia_sdp_session *answer,
	const struct pjmedia_sdp_session *remote,
	struct ast_stream *stream)
{
	struct ast_sip_session_sdp_handler *handler = session_media->handler;
	RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);
	int res = 0;
	SCOPE_ENTER(1, "%s Stream: %s\n", ast_sip_session_get_name(session),
		ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));

	if (handler) {
		/* if an already assigned handler reports a catastrophic error, fail */
		res = handler->create_outgoing_sdp_stream(session, session_media, answer, remote, stream);
		if (res < 0) {
			SCOPE_EXIT_RTN_VALUE(-1, "Coudn't create sdp stream\n");
		}
		SCOPE_EXIT_RTN_VALUE(0, "Had handler\n");
	}

	handler_list = ao2_find(sdp_handlers, ast_codec_media_type2str(session_media->type), OBJ_KEY);
	if (!handler_list) {
		SCOPE_EXIT_RTN_VALUE(0, "No handlers\n");
	}

	/* no handler for this stream type and we have a list to search */
	AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
		if (handler == session_media->handler) {
			continue;
		}
		res = handler->create_outgoing_sdp_stream(session, session_media, answer, remote, stream);
		if (res < 0) {
			/* catastrophic error */
			SCOPE_EXIT_RTN_VALUE(-1, "Coudn't create sdp stream\n");
		}
		if (res > 0) {
			/* Handled by this handler. Move to the next stream */
			session_media_set_handler(session_media, handler);
			SCOPE_EXIT_RTN_VALUE(0, "Handled\n");
		}
	}

	/* streams that weren't handled won't be included in generated outbound SDP */
	SCOPE_EXIT_RTN_VALUE(0, "Not handled\n");
}

/*! \brief Bundle group building structure */
struct sip_session_media_bundle_group {
	/*! \brief The media identifiers in this bundle group */
	char *mids[PJMEDIA_MAX_SDP_MEDIA];
	/*! \brief SDP attribute string */
	struct ast_str *attr_string;
};

static int add_bundle_groups(struct ast_sip_session *session, pj_pool_t *pool, pjmedia_sdp_session *answer)
{
	pj_str_t stmp;
	pjmedia_sdp_attr *attr;
	struct sip_session_media_bundle_group bundle_groups[PJMEDIA_MAX_SDP_MEDIA];
	int index, mid_id;
	struct sip_session_media_bundle_group *bundle_group;

	if (session->endpoint->media.webrtc) {
		attr = pjmedia_sdp_attr_create(pool, "msid-semantic", pj_cstr(&stmp, "WMS *"));
		pjmedia_sdp_attr_add(&answer->attr_count, answer->attr, attr);
	}

	if (!session->endpoint->media.bundle) {
		return 0;
	}

	memset(bundle_groups, 0, sizeof(bundle_groups));

	/* Build the bundle group layout so we can then add it to the SDP */
	for (index = 0; index < AST_VECTOR_SIZE(&session->pending_media_state->sessions); ++index) {
		struct ast_sip_session_media *session_media = AST_VECTOR_GET(&session->pending_media_state->sessions, index);

		/* If this stream is not part of a bundle group we can't add it */
		if (session_media->bundle_group == -1) {
			continue;
		}

		bundle_group = &bundle_groups[session_media->bundle_group];

		/* If this is the first mid then we need to allocate the attribute string and place BUNDLE in front */
		if (!bundle_group->mids[0]) {
			bundle_group->mids[0] = session_media->mid;
			bundle_group->attr_string = ast_str_create(64);
			if (!bundle_group->attr_string) {
				continue;
			}

			ast_str_set(&bundle_group->attr_string, 0, "BUNDLE %s", session_media->mid);
			continue;
		}

		for (mid_id = 1; mid_id < PJMEDIA_MAX_SDP_MEDIA; ++mid_id) {
			if (!bundle_group->mids[mid_id]) {
				bundle_group->mids[mid_id] = session_media->mid;
				ast_str_append(&bundle_group->attr_string, 0, " %s", session_media->mid);
				break;
			} else if (!strcmp(bundle_group->mids[mid_id], session_media->mid)) {
				break;
			}
		}
	}

	/* Add all bundle groups that have mids to the SDP */
	for (index = 0; index < PJMEDIA_MAX_SDP_MEDIA; ++index) {
		bundle_group = &bundle_groups[index];

		if (!bundle_group->attr_string) {
			continue;
		}

		attr = pjmedia_sdp_attr_create(pool, "group", pj_cstr(&stmp, ast_str_buffer(bundle_group->attr_string)));
		pjmedia_sdp_attr_add(&answer->attr_count, answer->attr, attr);

		ast_free(bundle_group->attr_string);
	}

	return 0;
}

static struct pjmedia_sdp_session *create_local_sdp(pjsip_inv_session *inv, struct ast_sip_session *session, const pjmedia_sdp_session *offer)
{
	static const pj_str_t STR_IN = { "IN", 2 };
	static const pj_str_t STR_IP4 = { "IP4", 3 };
	static const pj_str_t STR_IP6 = { "IP6", 3 };
	pjmedia_sdp_session *local;
	int i;
	int stream;
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	if (inv->state == PJSIP_INV_STATE_DISCONNECTED) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Failed to create session SDP. Session has been already disconnected\n",
			ast_sip_session_get_name(session));
	}

	if (!inv->pool_prov || !(local = PJ_POOL_ZALLOC_T(inv->pool_prov, pjmedia_sdp_session))) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Pool allocation failure\n", ast_sip_session_get_name(session));
	}

	if (!offer) {
		local->origin.version = local->origin.id = (pj_uint32_t)(ast_random());
	} else {
		local->origin.version = offer->origin.version + 1;
		local->origin.id = offer->origin.id;
	}

	pj_strdup2(inv->pool_prov, &local->origin.user, session->endpoint->media.sdpowner);
	pj_strdup2(inv->pool_prov, &local->name, session->endpoint->media.sdpsession);

	if (!session->pending_media_state->topology || !ast_stream_topology_get_count(session->pending_media_state->topology)) {
		/* We've encountered a situation where we have been told to create a local SDP but noone has given us any indication
		 * of what kind of stream topology they would like. We try to not alter the current state of the SDP negotiation
		 * by using what is currently negotiated. If this is unavailable we fall back to what is configured on the endpoint.
		 */
		ast_stream_topology_free(session->pending_media_state->topology);
		if (session->active_media_state->topology) {
			session->pending_media_state->topology = ast_stream_topology_clone(session->active_media_state->topology);
		} else {
			session->pending_media_state->topology = ast_stream_topology_clone(session->endpoint->media.topology);
		}
		if (!session->pending_media_state->topology) {
			SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: No pending media state topology\n", ast_sip_session_get_name(session));
		}
	}

	ast_trace(-1, "%s: Processing streams\n", ast_sip_session_get_name(session));

	for (i = 0; i < ast_stream_topology_get_count(session->pending_media_state->topology); ++i) {
		struct ast_sip_session_media *session_media;
		struct ast_stream *stream = ast_stream_topology_get_stream(session->pending_media_state->topology, i);
		unsigned int streams = local->media_count;
		SCOPE_ENTER(4, "%s: Processing stream %s\n", ast_sip_session_get_name(session),
			ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));

		/* This code does not enforce any maximum stream count limitations as that is done on either
		 * the handling of an incoming SDP offer or on the handling of a session refresh.
		 */

		session_media = ast_sip_session_media_state_add(session, session->pending_media_state, ast_stream_get_type(stream), i);
		if (!session_media) {
			local = NULL;
			SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Couldn't alloc/add session media for stream %s\n",
				ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
		}

		if (add_sdp_streams(session_media, session, local, offer, stream)) {
			local = NULL;
			SCOPE_EXIT_LOG_EXPR(goto end, LOG_ERROR, "%s: Couldn't add sdp streams for stream %s\n",
				ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));
		}

		/* If a stream was actually added then add any additional details */
		if (streams != local->media_count) {
			pjmedia_sdp_media *media = local->media[streams];
			pj_str_t stmp;
			pjmedia_sdp_attr *attr;

			/* Add the media identifier if present */
			if (!ast_strlen_zero(session_media->mid)) {
				attr = pjmedia_sdp_attr_create(inv->pool_prov, "mid", pj_cstr(&stmp, session_media->mid));
				pjmedia_sdp_attr_add(&media->attr_count, media->attr, attr);
			}

			ast_trace(-1, "%s: Stream %s added%s%s\n", ast_sip_session_get_name(session),
				ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)),
				S_COR(!ast_strlen_zero(session_media->mid), " with mid ", ""), S_OR(session_media->mid, ""));

		}

		/* Ensure that we never exceed the maximum number of streams PJMEDIA will allow. */
		if (local->media_count == PJMEDIA_MAX_SDP_MEDIA) {
			SCOPE_EXIT_EXPR(break, "%s: Stream %s exceeded max pjmedia count of %d\n",
				ast_sip_session_get_name(session), ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)),
				PJMEDIA_MAX_SDP_MEDIA);
		}

		SCOPE_EXIT("%s: Done with %s\n", ast_sip_session_get_name(session),
			ast_str_tmp(128, ast_stream_to_str(stream, &STR_TMP)));

	}

	/* Add any bundle groups that are present on the media state */
	ast_trace(-1, "%s: Adding bundle groups (if available)\n", ast_sip_session_get_name(session));
	if (add_bundle_groups(session, inv->pool_prov, local)) {
		SCOPE_EXIT_LOG_RTN_VALUE(NULL, LOG_ERROR, "%s: Couldn't add bundle groups\n", ast_sip_session_get_name(session));
	}

	/* Use the connection details of an available media if possible for SDP level */
	ast_trace(-1, "%s: Copying connection details\n", ast_sip_session_get_name(session));

	for (stream = 0; stream < local->media_count; stream++) {
		SCOPE_ENTER(4, "%s: Processing media %d\n", ast_sip_session_get_name(session), stream);
		if (!local->media[stream]->conn) {
			SCOPE_EXIT_EXPR(continue, "%s: Media %d has no connection info\n", ast_sip_session_get_name(session), stream);
		}

		if (local->conn) {
			if (!pj_strcmp(&local->conn->net_type, &local->media[stream]->conn->net_type) &&
				!pj_strcmp(&local->conn->addr_type, &local->media[stream]->conn->addr_type) &&
				!pj_strcmp(&local->conn->addr, &local->media[stream]->conn->addr)) {
				local->media[stream]->conn = NULL;
			}
			SCOPE_EXIT_EXPR(continue, "%s: Media %d has good existing connection info\n", ast_sip_session_get_name(session), stream);
		}

		/* This stream's connection info will serve as the connection details for SDP level */
		local->conn = local->media[stream]->conn;
		local->media[stream]->conn = NULL;

		SCOPE_EXIT_EXPR(continue, "%s: Media %d reset\n", ast_sip_session_get_name(session), stream);
	}

	/* If no SDP level connection details are present then create some */
	if (!local->conn) {
		ast_trace(-1, "%s: Creating connection details\n", ast_sip_session_get_name(session));

		local->conn = pj_pool_zalloc(inv->pool_prov, sizeof(struct pjmedia_sdp_conn));
		local->conn->net_type = STR_IN;
		local->conn->addr_type = session->endpoint->media.rtp.ipv6 ? STR_IP6 : STR_IP4;

		if (!ast_strlen_zero(session->endpoint->media.address)) {
			pj_strdup2(inv->pool_prov, &local->conn->addr, session->endpoint->media.address);
		} else {
			pj_strdup2(inv->pool_prov, &local->conn->addr, ast_sip_get_host_ip_string(session->endpoint->media.rtp.ipv6 ? pj_AF_INET6() : pj_AF_INET()));
		}
	}

	pj_strassign(&local->origin.net_type, &local->conn->net_type);
	pj_strassign(&local->origin.addr_type, &local->conn->addr_type);
	pj_strassign(&local->origin.addr, &local->conn->addr);

end:
	SCOPE_EXIT_RTN_VALUE(local, "%s\n", ast_sip_session_get_name(session));
}

static void session_inv_on_rx_offer(pjsip_inv_session *inv, const pjmedia_sdp_session *offer)
{
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	pjmedia_sdp_session *answer;
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	if (ast_shutdown_final()) {
		SCOPE_EXIT_RTN("%s: Shutdown in progress\n", ast_sip_session_get_name(session));
	}

	session = inv->mod_data[session_module.id];
	if (handle_incoming_sdp(session, offer)) {
		ast_sip_session_media_state_reset(session->pending_media_state);
		SCOPE_EXIT_RTN("%s: handle_incoming_sdp failed\n", ast_sip_session_get_name(session));
	}

	if ((answer = create_local_sdp(inv, session, offer))) {
		pjsip_inv_set_sdp_answer(inv, answer);
		SCOPE_EXIT_RTN("%s: Set SDP answer\n", ast_sip_session_get_name(session));
	}
	SCOPE_EXIT_RTN("%s: create_local_sdp failed\n", ast_sip_session_get_name(session));
}

static void session_inv_on_create_offer(pjsip_inv_session *inv, pjmedia_sdp_session **p_offer)
{
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	const pjmedia_sdp_session *previous_sdp = NULL;
	pjmedia_sdp_session *offer;
	int i;

	/* We allow PJSIP to produce an SDP if no channel is present. This may result
	 * in an incorrect SDP occurring, but if no channel is present then we are in
	 * the midst of a BYE and are hanging up. This ensures that all the code to
	 * produce an SDP doesn't need to worry about a channel being present or not,
	 * just in case.
	 */
	if (!session->channel) {
		return;
	}

	if (inv->neg) {
		if (pjmedia_sdp_neg_was_answer_remote(inv->neg)) {
			pjmedia_sdp_neg_get_active_remote(inv->neg, &previous_sdp);
		} else {
			pjmedia_sdp_neg_get_active_local(inv->neg, &previous_sdp);
		}
	}

	offer = create_local_sdp(inv, session, previous_sdp);
	if (!offer) {
		return;
	}

	ast_queue_unhold(session->channel);

	/*
	 * Some devices indicate hold with deferred SDP reinvites (i.e. no SDP in the reinvite).
	 * When hold is initially indicated, we
	 * - Receive an INVITE with no SDP
	 * - Send a 200 OK with SDP, indicating sendrecv in the media streams
	 * - Receive an ACK with SDP, indicating sendonly in the media streams
	 *
	 * At this point, the pjmedia negotiator saves the state of the media direction so that
	 * if we are to send any offers, we'll offer recvonly in the media streams. This is
	 * problematic if the device is attempting to unhold, though. If the device unholds
	 * by sending a reinvite with no SDP, then we will respond with a 200 OK with recvonly.
	 * According to RFC 3264, if an offerer offers recvonly, then the answerer MUST respond
	 * with sendonly or inactive. The result of this is that the stream is not off hold.
	 *
	 * Therefore, in this case, when we receive a reinvite while the stream is on hold, we
	 * need to be sure to offer sendrecv. This way, the answerer can respond with sendrecv
	 * in order to get the stream off hold. If this is actually a different purpose reinvite
	 * (like a session timer refresh), then the answerer can respond to our sendrecv with
	 * sendonly, keeping the stream on hold.
	 */
	for (i = 0; i < offer->media_count; ++i) {
		pjmedia_sdp_media *m = offer->media[i];
		pjmedia_sdp_attr *recvonly;
		pjmedia_sdp_attr *inactive;
		pjmedia_sdp_attr *sendonly;

		recvonly = pjmedia_sdp_attr_find2(m->attr_count, m->attr, "recvonly", NULL);
		inactive = pjmedia_sdp_attr_find2(m->attr_count, m->attr, "inactive", NULL);
		sendonly = pjmedia_sdp_attr_find2(m->attr_count, m->attr, "sendonly", NULL);
		if (recvonly || inactive || sendonly) {
			pjmedia_sdp_attr *to_remove = recvonly ?: inactive ?: sendonly;
			pjmedia_sdp_attr *sendrecv;

			pjmedia_sdp_attr_remove(&m->attr_count, m->attr, to_remove);

			sendrecv = pjmedia_sdp_attr_create(session->inv_session->pool, "sendrecv", NULL);
			pjmedia_sdp_media_add_attr(m, sendrecv);
		}
	}

	*p_offer = offer;
}

static void session_inv_on_media_update(pjsip_inv_session *inv, pj_status_t status)
{
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	const pjmedia_sdp_session *local, *remote;
	SCOPE_ENTER(3, "%s\n", ast_sip_session_get_name(session));

	if (ast_shutdown_final()) {
		SCOPE_EXIT_RTN("%s: Shutdown in progress\n", ast_sip_session_get_name(session));
	}

	session = inv->mod_data[session_module.id];
	if (!session || !session->channel) {
		/*
		 * If we don't have a session or channel then we really
		 * don't care about media updates.
		 * Just ignore
		 */
		SCOPE_EXIT_RTN("%s: No channel or session\n", ast_sip_session_get_name(session));
	}

	if (session->endpoint) {
		int bail = 0;

		/*
		 * If following_fork is set, then this is probably the result of a
		 * forked INVITE and SDP asnwers coming from the different fork UAS
		 * destinations.  In this case updated_sdp_answer will also be set.
		 *
		 * If only updated_sdp_answer is set, then this is the non-forking
		 * scenario where the same UAS just needs to change something like
		 * the media port.
		 */

		if (inv->following_fork) {
			if (session->endpoint->media.rtp.follow_early_media_fork) {
				ast_trace(-1, "%s: Following early media fork with different To tags\n", ast_sip_session_get_name(session));
			} else {
				ast_trace(-1, "%s: Not following early media fork with different To tags\n", ast_sip_session_get_name(session));
				bail = 1;
			}
		}
#ifdef HAVE_PJSIP_INV_ACCEPT_MULTIPLE_SDP_ANSWERS
		else if (inv->updated_sdp_answer) {
			if (session->endpoint->media.rtp.accept_multiple_sdp_answers) {
				ast_trace(-1, "%s: Accepting updated SDP with same To tag\n", ast_sip_session_get_name(session));
			} else {
				ast_trace(-1, "%s: Ignoring updated SDP answer with same To tag\n", ast_sip_session_get_name(session));
				bail = 1;
			}
		}
#endif
		if (bail) {
			SCOPE_EXIT_RTN("%s: Bailing\n", ast_sip_session_get_name(session));
		}
	}

	if ((status != PJ_SUCCESS) || (pjmedia_sdp_neg_get_active_local(inv->neg, &local) != PJ_SUCCESS) ||
		(pjmedia_sdp_neg_get_active_remote(inv->neg, &remote) != PJ_SUCCESS)) {
		ast_channel_hangupcause_set(session->channel, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
		ast_set_hangupsource(session->channel, ast_channel_name(session->channel), 0);
		ast_queue_hangup(session->channel);
		SCOPE_EXIT_RTN("%s: Couldn't get active or local or remote negotiator.  Hanging up\n", ast_sip_session_get_name(session));
	}

	if (handle_negotiated_sdp(session, local, remote)) {
		ast_sip_session_media_state_reset(session->pending_media_state);
		SCOPE_EXIT_RTN("%s: handle_negotiated_sdp failed.  Resetting pending media state\n", ast_sip_session_get_name(session));
	}
	SCOPE_EXIT_RTN("%s\n", ast_sip_session_get_name(session));
}

static pjsip_redirect_op session_inv_on_redirected(pjsip_inv_session *inv, const pjsip_uri *target, const pjsip_event *e)
{
	struct ast_sip_session *session;
	const pjsip_sip_uri *uri;

	if (ast_shutdown_final()) {
		return PJSIP_REDIRECT_STOP;
	}

	session = inv->mod_data[session_module.id];
	if (!session || !session->channel) {
		return PJSIP_REDIRECT_STOP;
	}

	if (session->endpoint->redirect_method == AST_SIP_REDIRECT_URI_PJSIP) {
		return PJSIP_REDIRECT_ACCEPT;
	}

	if (!PJSIP_URI_SCHEME_IS_SIP(target) && !PJSIP_URI_SCHEME_IS_SIPS(target)) {
		return PJSIP_REDIRECT_STOP;
	}

	handle_incoming(session, e->body.rx_msg.rdata, AST_SIP_SESSION_BEFORE_REDIRECTING);

	uri = pjsip_uri_get_uri(target);

	if (session->endpoint->redirect_method == AST_SIP_REDIRECT_USER) {
		char exten[AST_MAX_EXTENSION];

		ast_copy_pj_str(exten, &uri->user, sizeof(exten));

		/*
		 * We may want to match in the dialplan without any user
		 * options getting in the way.
		 */
		AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(exten);

		ast_channel_call_forward_set(session->channel, exten);
	} else if (session->endpoint->redirect_method == AST_SIP_REDIRECT_URI_CORE) {
		char target_uri[PJSIP_MAX_URL_SIZE];
		/* PJSIP/ + endpoint length + / + max URL size */
		char forward[8 + strlen(ast_sorcery_object_get_id(session->endpoint)) + PJSIP_MAX_URL_SIZE];

		pjsip_uri_print(PJSIP_URI_IN_REQ_URI, uri, target_uri, sizeof(target_uri));
		sprintf(forward, "PJSIP/%s/%s", ast_sorcery_object_get_id(session->endpoint), target_uri);
		ast_channel_call_forward_set(session->channel, forward);
	}

	return PJSIP_REDIRECT_STOP;
}

static pjsip_inv_callback inv_callback = {
	.on_state_changed = session_inv_on_state_changed,
	.on_new_session = session_inv_on_new_session,
	.on_tsx_state_changed = session_inv_on_tsx_state_changed,
	.on_rx_offer = session_inv_on_rx_offer,
	.on_create_offer = session_inv_on_create_offer,
	.on_media_update = session_inv_on_media_update,
	.on_redirected = session_inv_on_redirected,
};

/*! \brief Hook for modifying outgoing messages with SDP to contain the proper address information */
static void session_outgoing_nat_hook(pjsip_tx_data *tdata, struct ast_sip_transport *transport)
{
	RAII_VAR(struct ast_sip_transport_state *, transport_state, ast_sip_get_transport_state(ast_sorcery_object_get_id(transport)), ao2_cleanup);
	struct ast_sip_nat_hook *hook = ast_sip_mod_data_get(
		tdata->mod_data, session_module.id, MOD_DATA_NAT_HOOK);
	pjsip_sdp_info *sdp_info;
	pjmedia_sdp_session *sdp;
	pjsip_dialog *dlg = pjsip_tdata_get_dlg(tdata);
	RAII_VAR(struct ast_sip_session *, session, dlg ? ast_sip_dialog_get_session(dlg) : NULL, ao2_cleanup);
	int stream;

	/*
	 * If there's no transport_state or body, or the hook
	 * has already been run, just return.
	 */
	if (ast_strlen_zero(transport->external_media_address) || !transport_state || hook || !tdata->msg->body) {
		return;
	}

	sdp_info = pjsip_get_sdp_info(tdata->pool, tdata->msg->body, NULL, &pjsip_media_type_application_sdp);
	if (sdp_info->sdp_err != PJ_SUCCESS || !sdp_info->sdp) {
		return;
	}
	sdp = sdp_info->sdp;

	if (sdp->conn) {
		char host[NI_MAXHOST];
		struct ast_sockaddr our_sdp_addr = { { 0, } };

		ast_copy_pj_str(host, &sdp->conn->addr, sizeof(host));
		ast_sockaddr_parse(&our_sdp_addr, host, PARSE_PORT_FORBID);

		/* Reversed check here. We don't check the remote
		 * endpoint being in our local net, but whether our
		 * outgoing session IP is local. If it is, we'll do
		 * rewriting. No localnet configured? Always rewrite. */
		if (ast_sip_transport_is_local(transport_state, &our_sdp_addr) || !transport_state->localnet) {
			ast_debug(5, "%s: Setting external media address to %s\n", ast_sip_session_get_name(session),
				ast_sockaddr_stringify_host(&transport_state->external_media_address));
			pj_strdup2(tdata->pool, &sdp->conn->addr, ast_sockaddr_stringify_host(&transport_state->external_media_address));
			pj_strassign(&sdp->origin.addr, &sdp->conn->addr);
		}
	}

	for (stream = 0; stream < sdp->media_count; ++stream) {
		/* See if there are registered handlers for this media stream type */
		char media[20];
		struct ast_sip_session_sdp_handler *handler;
		RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);

		/* We need a null-terminated version of the media string */
		ast_copy_pj_str(media, &sdp->media[stream]->desc.media, sizeof(media));

		handler_list = ao2_find(sdp_handlers, media, OBJ_KEY);
		if (!handler_list) {
			ast_debug(4, "%s: No registered SDP handlers for media type '%s'\n",  ast_sip_session_get_name(session),
				media);
			continue;
		}
		AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
			if (handler->change_outgoing_sdp_stream_media_address) {
				handler->change_outgoing_sdp_stream_media_address(tdata, sdp->media[stream], transport);
			}
		}
	}

	/* We purposely do this so that the hook will not be invoked multiple times, ie: if a retransmit occurs */
	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, session_module.id, MOD_DATA_NAT_HOOK, nat_hook);
}

#ifdef TEST_FRAMEWORK

static struct ast_stream *test_stream_alloc(const char *name, enum ast_media_type type, enum ast_stream_state state)
{
	struct ast_stream *stream;

	stream = ast_stream_alloc(name, type);
	if (!stream) {
		return NULL;
	}
	ast_stream_set_state(stream, state);

	return stream;
}

static struct ast_sip_session_media *test_media_add(
	struct ast_sip_session_media_state *media_state, const char *name, enum ast_media_type type,
	enum ast_stream_state state, int position)
{
	struct ast_sip_session_media *session_media = NULL;
	struct ast_stream *stream = NULL;

	stream = test_stream_alloc(name, type, state);
	if (!stream) {
		return NULL;
	}

	if (position >= 0 && position < ast_stream_topology_get_count(media_state->topology)) {
		ast_stream_topology_set_stream(media_state->topology, position, stream);
	} else {
		position = ast_stream_topology_append_stream(media_state->topology, stream);
	}

	session_media = ao2_alloc_options(sizeof(*session_media), session_media_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!session_media) {
		return NULL;
	}

	session_media->keepalive_sched_id = -1;
	session_media->timeout_sched_id = -1;
	session_media->type = type;
	session_media->stream_num = position;
	session_media->bundle_group = -1;
	strcpy(session_media->label, name);

	if (AST_VECTOR_REPLACE(&media_state->sessions, position, session_media)) {
		ao2_ref(session_media, -1);

		return NULL;
	}

	/* If this stream will be active in some way and it is the first of this type then consider this the default media session to match */
	if (!media_state->default_session[type] && ast_stream_get_state(ast_stream_topology_get_stream(media_state->topology, position)) != AST_STREAM_STATE_REMOVED) {
		media_state->default_session[type] = session_media;
	}

	return session_media;
}

static int test_is_media_session_equal(struct ast_sip_session_media *left, struct ast_sip_session_media *right)
{
	if (left == right) {
		return 1;
	}

	if (!left) {
		return 1;
	}

	if (!right) {
		return 0;
	}
	return memcmp(left, right, sizeof(*left)) == 0;
}

static int test_is_media_state_equal(struct ast_sip_session_media_state *left, struct ast_sip_session_media_state *right,
	int assert_on_failure)
{
	int i;
	SCOPE_ENTER(2);

	if (left == right) {
		SCOPE_EXIT_RTN_VALUE(1, "equal\n");
	}

	if (!(left && right)) {
		ast_assert(!assert_on_failure);
		SCOPE_EXIT_RTN_VALUE(0, "one is null: left: %p  right: %p\n", left, right);
	}

	if (!ast_stream_topology_equal(left->topology, right->topology)) {
		ast_assert(!assert_on_failure);
		SCOPE_EXIT_RTN_VALUE(0, "topologies differ\n");
	}
	if (AST_VECTOR_SIZE(&left->sessions) != AST_VECTOR_SIZE(&right->sessions)) {
		ast_assert(!assert_on_failure);
		SCOPE_EXIT_RTN_VALUE(0, "session vector sizes different: left %zu != right %zu\n",
			AST_VECTOR_SIZE(&left->sessions),
			AST_VECTOR_SIZE(&right->sessions));
	}
	if (AST_VECTOR_SIZE(&left->read_callbacks) != AST_VECTOR_SIZE(&right->read_callbacks)) {
		ast_assert(!assert_on_failure);
		SCOPE_EXIT_RTN_VALUE(0, "read_callback vector sizes different: left %zu != right %zu\n",
			AST_VECTOR_SIZE(&left->read_callbacks),
			AST_VECTOR_SIZE(&right->read_callbacks));
	}

	for (i = 0; i < AST_VECTOR_SIZE(&left->sessions) ; i++) {
		if (!test_is_media_session_equal(AST_VECTOR_GET(&left->sessions, i), AST_VECTOR_GET(&right->sessions, i))) {
			ast_assert(!assert_on_failure);
			SCOPE_EXIT_RTN_VALUE(0, "Media session %d different\n", i);
		}
	}

	for (i = 0; i < AST_VECTOR_SIZE(&left->read_callbacks) ; i++) {
		if (memcmp(AST_VECTOR_GET_ADDR(&left->read_callbacks, i),
				AST_VECTOR_GET_ADDR(&right->read_callbacks, i),
				sizeof(struct ast_sip_session_media_read_callback_state)) != 0) {
			ast_assert(!assert_on_failure);
			SCOPE_EXIT_RTN_VALUE(0, "read_callback %d different\n", i);
		}
	}

	for (i = 0; i < AST_MEDIA_TYPE_END; i++) {
		if (!(left->default_session[i] && right->default_session[i])) {
			continue;
		}
		if (!left->default_session[i] || !right->default_session[i]
			|| left->default_session[i]->stream_num != right->default_session[i]->stream_num) {
			ast_assert(!assert_on_failure);
			SCOPE_EXIT_RTN_VALUE(0, "Default media session %d different.  Left: %s  Right: %s\n", i,
				left->default_session[i] ? left->default_session[i]->label : "null",
				right->default_session[i] ? right->default_session[i]->label : "null");
		}
	}

	SCOPE_EXIT_RTN_VALUE(1, "equal\n");
}

AST_TEST_DEFINE(test_resolve_refresh_media_states)
{
#define FREE_STATE() \
({ \
	ast_sip_session_media_state_free(new_pending_state); \
	new_pending_state = NULL; \
	ast_sip_session_media_state_free(delayed_pending_state); \
	delayed_pending_state = NULL; \
	ast_sip_session_media_state_free(delayed_active_state); \
	delayed_active_state = NULL; \
	ast_sip_session_media_state_free(current_active_state); \
	current_active_state = NULL; \
	ast_sip_session_media_state_free(expected_pending_state); \
	expected_pending_state = NULL; \
})

#define RESET_STATE(__num) \
({ \
	testnum=__num; \
	ast_trace(-1, "Test %d\n", testnum); \
	test_failed = 0; \
	delayed_pending_state = ast_sip_session_media_state_alloc(); \
	delayed_pending_state->topology = ast_stream_topology_alloc(); \
	delayed_active_state = ast_sip_session_media_state_alloc(); \
	delayed_active_state->topology = ast_stream_topology_alloc(); \
	current_active_state = ast_sip_session_media_state_alloc(); \
	current_active_state->topology = ast_stream_topology_alloc(); \
	expected_pending_state = ast_sip_session_media_state_alloc(); \
	expected_pending_state->topology = ast_stream_topology_alloc(); \
})

#define CHECKER() \
({ \
	new_pending_state = resolve_refresh_media_states("unittest", delayed_pending_state, delayed_active_state, current_active_state, 1); \
	if (!test_is_media_state_equal(new_pending_state, expected_pending_state, 0)) { \
		res = AST_TEST_FAIL; \
		test_failed = 1; \
		ast_test_status_update(test, "da: %s\n", ast_str_tmp(256, ast_stream_topology_to_str(delayed_active_state->topology, &STR_TMP))); \
		ast_test_status_update(test, "dp: %s\n", ast_str_tmp(256, ast_stream_topology_to_str(delayed_pending_state->topology, &STR_TMP))); \
		ast_test_status_update(test, "ca: %s\n", ast_str_tmp(256, ast_stream_topology_to_str(current_active_state->topology, &STR_TMP))); \
		ast_test_status_update(test, "ep: %s\n", ast_str_tmp(256, ast_stream_topology_to_str(expected_pending_state->topology, &STR_TMP))); \
		ast_test_status_update(test, "np: %s\n", ast_str_tmp(256, ast_stream_topology_to_str(new_pending_state->topology, &STR_TMP))); \
	} \
	ast_test_status_update(test, "Test %d %s\n", testnum, test_failed ? "FAILED" : "passed"); \
	ast_trace(-1, "Test %d %s\n", testnum, test_failed ? "FAILED" : "passed"); \
	test_failed = 0; \
	FREE_STATE(); \
})


	struct ast_sip_session_media_state * delayed_pending_state = NULL;
	struct ast_sip_session_media_state * delayed_active_state = NULL;
	struct ast_sip_session_media_state * current_active_state = NULL;
	struct ast_sip_session_media_state * new_pending_state = NULL;
	struct ast_sip_session_media_state * expected_pending_state = NULL;
	enum ast_test_result_state res = AST_TEST_PASS;
	int test_failed = 0;
	int testnum = 0;
	SCOPE_ENTER(1);

	switch (cmd) {
	case TEST_INIT:
		info->name = "merge_refresh_topologies";
		info->category = "/res/res_pjsip_session/";
		info->summary = "Test merging of delayed request topologies";
		info->description = "Test merging of delayed request topologies";
		SCOPE_EXIT_RTN_VALUE(AST_TEST_NOT_RUN);
	case TEST_EXECUTE:
		break;
	}

	RESET_STATE(1);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(2);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(3);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(4);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(5);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);
	CHECKER();

	RESET_STATE(6);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);
	test_media_add(current_active_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(7);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo6", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo6", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(8);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(9);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(10);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);
	test_media_add(expected_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_REMOVED, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(11);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "myvideo3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(12);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "292-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "296-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "292-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "296-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "297-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "294-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "292-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "296-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "290-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "297-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "292-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "296-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "290-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "297-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "294-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(13);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "293-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "292-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "294-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "295-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "296-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "293-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "292-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "294-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "295-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "296-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "298-7", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "293-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "292-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "294-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "295-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "296-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "290-6", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "293-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "292-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "294-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "295-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "296-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "290-6", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "298-7", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(14);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "298-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "297-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "298-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "294-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "295-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "298-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "297-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "291-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "294-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "298-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "297-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "291-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "294-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "295-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	RESET_STATE(15);
	test_media_add(delayed_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "298-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_active_state, "297-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(delayed_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "298-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDONLY, -1);
	test_media_add(delayed_pending_state, "294-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(delayed_pending_state, "295-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(current_active_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "297-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "291-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "294-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(current_active_state, "298-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);

	test_media_add(expected_pending_state, "audio", AST_MEDIA_TYPE_AUDIO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "297-2", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "291-3", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "294-4", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	test_media_add(expected_pending_state, "298-1", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDONLY, -1);
	test_media_add(expected_pending_state, "295-5", AST_MEDIA_TYPE_VIDEO, AST_STREAM_STATE_SENDRECV, -1);
	CHECKER();

	SCOPE_EXIT_RTN_VALUE(res);
}
#endif /* TEST_FRAMEWORK */

static int load_module(void)
{
	pjsip_endpoint *endpt;

	if (!ast_sip_get_sorcery() || !ast_sip_get_pjsip_endpoint()) {
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(nat_hook = ast_sorcery_alloc(ast_sip_get_sorcery(), "nat_hook", NULL))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	nat_hook->outgoing_external_message = session_outgoing_nat_hook;
	ast_sorcery_create(ast_sip_get_sorcery(), nat_hook);
	sdp_handlers = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		SDP_HANDLER_BUCKETS, sdp_handler_list_hash, NULL, sdp_handler_list_cmp);
	if (!sdp_handlers) {
		return AST_MODULE_LOAD_DECLINE;
	}
	endpt = ast_sip_get_pjsip_endpoint();
	pjsip_inv_usage_init(endpt, &inv_callback);
	pjsip_100rel_init_module(endpt);
	pjsip_timer_init_module(endpt);
	if (ast_sip_register_service(&session_module)) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_sip_register_service(&session_reinvite_module);
	ast_sip_register_service(&outbound_invite_auth_module);

	ast_module_shutdown_ref(ast_module_info->self);
#ifdef TEST_FRAMEWORK
	AST_TEST_REGISTER(test_resolve_refresh_media_states);
#endif
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
#ifdef TEST_FRAMEWORK
	AST_TEST_UNREGISTER(test_resolve_refresh_media_states);
#endif
	ast_sip_unregister_service(&outbound_invite_auth_module);
	ast_sip_unregister_service(&session_reinvite_module);
	ast_sip_unregister_service(&session_module);
	ast_sorcery_delete(ast_sip_get_sorcery(), nat_hook);
	ao2_cleanup(nat_hook);
	ao2_cleanup(sdp_handlers);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP Session resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip",
);
