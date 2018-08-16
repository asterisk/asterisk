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

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
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

#define SDP_HANDLER_BUCKETS 11

#define MOD_DATA_ON_RESPONSE "on_response"
#define MOD_DATA_NAT_HOOK "nat_hook"

/* Some forward declarations */
static void handle_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata);
static void handle_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata,
		enum ast_sip_session_response_priority response_priority);
static int handle_incoming(struct ast_sip_session *session, pjsip_rx_data *rdata,
		enum ast_sip_session_response_priority response_priority);
static void handle_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata);
static void handle_outgoing_response(struct ast_sip_session *session, pjsip_tx_data *tdata);

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

static int sdp_handler_list_cmp(void *obj, void *arg, int flags)
{
	struct sdp_handler_list *handler_list1 = obj;
	struct sdp_handler_list *handler_list2 = arg;
	const char *stream_type2 = flags & OBJ_KEY ? arg : handler_list2->stream_type;

	return strcmp(handler_list1->stream_type, stream_type2) ? 0 : CMP_MATCH | CMP_STOP;
}

static int session_media_hash(const void *obj, int flags)
{
	const struct ast_sip_session_media *session_media = obj;
	const char *stream_type = flags & OBJ_KEY ? obj : session_media->stream_type;

	return ast_str_hash(stream_type);
}

static int session_media_cmp(void *obj, void *arg, int flags)
{
	struct ast_sip_session_media *session_media1 = obj;
	struct ast_sip_session_media *session_media2 = arg;
	const char *stream_type2 = flags & OBJ_KEY ? arg : session_media2->stream_type;

	return strcmp(session_media1->stream_type, stream_type2) ? 0 : CMP_MATCH | CMP_STOP;
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
		ast_module_ref(ast_module_info->self);
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
	ast_module_ref(ast_module_info->self);
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
			ast_module_unref(ast_module_info->self);
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

static int handle_incoming_sdp(struct ast_sip_session *session, const pjmedia_sdp_session *sdp)
{
	int i;
	int handled = 0;

	if (session->inv_session && session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Failed to handle incoming SDP. Session has been already disconnected\n");
		return -1;
	}

	for (i = 0; i < sdp->media_count; ++i) {
		/* See if there are registered handlers for this media stream type */
		char media[20];
		struct ast_sip_session_sdp_handler *handler;
		RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);
		RAII_VAR(struct ast_sip_session_media *, session_media, NULL, ao2_cleanup);
		int res;

		/* We need a null-terminated version of the media string */
		ast_copy_pj_str(media, &sdp->media[i]->desc.media, sizeof(media));

		session_media = ao2_find(session->media, media, OBJ_KEY);
		if (!session_media) {
			/* if the session_media doesn't exist, there weren't
			 * any handlers at the time of its creation */
			continue;
		}

		if (session_media->handler) {
			handler = session_media->handler;
			ast_debug(1, "Negotiating incoming SDP media stream '%s' using %s SDP handler\n",
				session_media->stream_type,
				session_media->handler->id);
			res = handler->negotiate_incoming_sdp_stream(session, session_media, sdp,
				sdp->media[i]);
			if (res < 0) {
				/* Catastrophic failure. Abort! */
				return -1;
			} else if (res > 0) {
				ast_debug(1, "Media stream '%s' handled by %s\n",
					session_media->stream_type,
					session_media->handler->id);
				/* Handled by this handler. Move to the next stream */
				handled = 1;
				continue;
			}
		}

		handler_list = ao2_find(sdp_handlers, media, OBJ_KEY);
		if (!handler_list) {
			ast_debug(1, "No registered SDP handlers for media type '%s'\n", media);
			continue;
		}
		AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
			if (handler == session_media->handler) {
				continue;
			}
			ast_debug(1, "Negotiating incoming SDP media stream '%s' using %s SDP handler\n",
				session_media->stream_type,
				handler->id);
			res = handler->negotiate_incoming_sdp_stream(session, session_media, sdp,
				sdp->media[i]);
			if (res < 0) {
				/* Catastrophic failure. Abort! */
				return -1;
			}
			if (res > 0) {
				ast_debug(1, "Media stream '%s' handled by %s\n",
					session_media->stream_type,
					handler->id);
				/* Handled by this handler. Move to the next stream */
				session_media_set_handler(session_media, handler);
				handled = 1;
				break;
			}
		}
	}
	if (!handled) {
		return -1;
	}
	return 0;
}

struct handle_negotiated_sdp_cb {
	struct ast_sip_session *session;
	const pjmedia_sdp_session *local;
	const pjmedia_sdp_session *remote;
};

static int handle_negotiated_sdp_session_media(void *obj, void *arg, int flags)
{
	struct ast_sip_session_media *session_media = obj;
	struct handle_negotiated_sdp_cb *callback_data = arg;
	struct ast_sip_session *session = callback_data->session;
	const pjmedia_sdp_session *local = callback_data->local;
	const pjmedia_sdp_session *remote = callback_data->remote;
	int i;

	for (i = 0; i < local->media_count; ++i) {
		/* See if there are registered handlers for this media stream type */
		char media[20];
		struct ast_sip_session_sdp_handler *handler;
		RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);
		int res;

		if (!remote->media[i]) {
			continue;
		}

		/* We need a null-terminated version of the media string */
		ast_copy_pj_str(media, &local->media[i]->desc.media, sizeof(media));

		/* stream type doesn't match the one we're looking to fill */
		if (strcasecmp(session_media->stream_type, media)) {
			continue;
		}

		handler = session_media->handler;
		if (handler) {
			ast_debug(1, "Applying negotiated SDP media stream '%s' using %s SDP handler\n",
				session_media->stream_type,
				handler->id);
			res = handler->apply_negotiated_sdp_stream(session, session_media, local,
				local->media[i], remote, remote->media[i]);
			if (res >= 0) {
				ast_debug(1, "Applied negotiated SDP media stream '%s' using %s SDP handler\n",
					session_media->stream_type,
					handler->id);
				return CMP_MATCH;
			}
			return 0;
		}

		handler_list = ao2_find(sdp_handlers, media, OBJ_KEY);
		if (!handler_list) {
			ast_debug(1, "No registered SDP handlers for media type '%s'\n", media);
			continue;
		}
		AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
			if (handler == session_media->handler) {
				continue;
			}
			ast_debug(1, "Applying negotiated SDP media stream '%s' using %s SDP handler\n",
				session_media->stream_type,
				handler->id);
			res = handler->apply_negotiated_sdp_stream(session, session_media, local,
				local->media[i], remote, remote->media[i]);
			if (res < 0) {
				/* Catastrophic failure. Abort! */
				return 0;
			}
			if (res > 0) {
				ast_debug(1, "Applied negotiated SDP media stream '%s' using %s SDP handler\n",
					session_media->stream_type,
					handler->id);
				/* Handled by this handler. Move to the next stream */
				session_media_set_handler(session_media, handler);
				return CMP_MATCH;
			}
		}
	}

	if (session_media->handler && session_media->handler->stream_stop) {
		ast_debug(1, "Stopping SDP media stream '%s' as it is not currently negotiated\n",
			session_media->stream_type);
		session_media->handler->stream_stop(session_media);
	}

	return CMP_MATCH;
}

static int handle_negotiated_sdp(struct ast_sip_session *session, const pjmedia_sdp_session *local, const pjmedia_sdp_session *remote)
{
	RAII_VAR(struct ao2_iterator *, successful, NULL, ao2_iterator_cleanup);
	struct handle_negotiated_sdp_cb callback_data = {
		.session = session,
		.local = local,
		.remote = remote,
	};

	successful = ao2_callback(session->media, OBJ_MULTIPLE, handle_negotiated_sdp_session_media, &callback_data);
	if (successful && ao2_iterator_count(successful) == ao2_container_count(session->media)) {
		/* Nothing experienced a catastrophic failure */
		ast_queue_frame(session->channel, &ast_null_frame);
		return 0;
	}
	return -1;
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
	AST_LIST_ENTRY(ast_sip_session_delayed_request) next;
};

static struct ast_sip_session_delayed_request *delayed_request_alloc(
	enum delayed_method method,
	ast_sip_session_request_creation_cb on_request_creation,
	ast_sip_session_sdp_creation_cb on_sdp_creation,
	ast_sip_session_response_cb on_response,
	int generate_new_sdp)
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
	return delay;
}

static int send_delayed_request(struct ast_sip_session *session, struct ast_sip_session_delayed_request *delay)
{
	ast_debug(3, "Endpoint '%s(%s)' sending delayed %s request.\n",
		ast_sorcery_object_get_id(session->endpoint),
		session->channel ? ast_channel_name(session->channel) : "",
		delayed_method2str(delay->method));

	switch (delay->method) {
	case DELAYED_METHOD_INVITE:
		ast_sip_session_refresh(session, delay->on_request_creation,
			delay->on_sdp_creation, delay->on_response,
			AST_SIP_SESSION_REFRESH_METHOD_INVITE, delay->generate_new_sdp);
		return 0;
	case DELAYED_METHOD_UPDATE:
		ast_sip_session_refresh(session, delay->on_request_creation,
			delay->on_sdp_creation, delay->on_response,
			AST_SIP_SESSION_REFRESH_METHOD_UPDATE, delay->generate_new_sdp);
		return 0;
	case DELAYED_METHOD_BYE:
		ast_sip_session_terminate(session, 0);
		return 0;
	}
	ast_log(LOG_WARNING, "Don't know how to send delayed %s(%d) request.\n",
		delayed_method2str(delay->method), delay->method);
	return -1;
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

	AST_LIST_TRAVERSE_SAFE_BEGIN(&session->delayed_requests, delay, next) {
		switch (delay->method) {
		case DELAYED_METHOD_INVITE:
			break;
		case DELAYED_METHOD_UPDATE:
			AST_LIST_REMOVE_CURRENT(next);
			res = send_delayed_request(session, delay);
			ast_free(delay);
			found = 1;
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
	return res;
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
			res = send_delayed_request(session, delay);
			ast_free(delay);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	ao2_ref(session, -1);
	return res;
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

	return res;
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
	enum delayed_method method)
{
	struct ast_sip_session_delayed_request *delay = delayed_request_alloc(method,
			on_request, on_sdp_creation, on_response, generate_new_sdp);

	if (!delay) {
		return -1;
	}

	if (method == DELAYED_METHOD_BYE) {
		/* Send BYE as early as possible */
		AST_LIST_INSERT_HEAD(&session->delayed_requests, delay, next);
	} else {
		AST_LIST_INSERT_TAIL(&session->delayed_requests, delay, next);
	}
	return 0;
}

static pjmedia_sdp_session *generate_session_refresh_sdp(struct ast_sip_session *session)
{
	pjsip_inv_session *inv_session = session->inv_session;
	const pjmedia_sdp_session *previous_sdp = NULL;

	if (inv_session->neg) {
		if (pjmedia_sdp_neg_was_answer_remote(inv_session->neg)) {
			pjmedia_sdp_neg_get_active_remote(inv_session->neg, &previous_sdp);
		} else {
			pjmedia_sdp_neg_get_active_local(inv_session->neg, &previous_sdp);
		}
	}
	return create_local_sdp(inv_session, session, previous_sdp);
}

static void set_from_header(struct ast_sip_session *session)
{
	struct ast_party_id effective_id;
	struct ast_party_id connected_id;
	pj_pool_t *dlg_pool;
	pjsip_fromto_hdr *dlg_info;
	pjsip_name_addr *dlg_info_name_addr;
	pjsip_sip_uri *dlg_info_uri;
	int restricted;

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
	dlg_info_name_addr = (pjsip_name_addr *) dlg_info->uri;
	dlg_info_uri = pjsip_uri_get_uri(dlg_info_name_addr);

	if (session->endpoint->id.trust_outbound || !restricted) {
		ast_sip_modify_id_header(dlg_pool, dlg_info, &connected_id);
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

		if (ast_strlen_zero(session->endpoint->fromdomain)) {
			pj_strdup2(dlg_pool, &dlg_info_uri->host, "anonymous.invalid");
		}
	} else {
		ast_sip_add_usereqphone(session->endpoint, dlg_pool, dlg_info->uri);
    }
}

int ast_sip_session_refresh(struct ast_sip_session *session,
		ast_sip_session_request_creation_cb on_request_creation,
		ast_sip_session_sdp_creation_cb on_sdp_creation,
		ast_sip_session_response_cb on_response,
		enum ast_sip_session_refresh_method method, int generate_new_sdp)
{
	pjsip_inv_session *inv_session = session->inv_session;
	pjmedia_sdp_session *new_sdp = NULL;
	pjsip_tx_data *tdata;

	if (inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		/* Don't try to do anything with a hung-up call */
		ast_debug(3, "Not sending reinvite to %s because of disconnected state...\n",
				ast_sorcery_object_get_id(session->endpoint));
		return 0;
	}

	/* If the dialog has not yet been established we have to defer until it has */
	if (inv_session->dlg->state != PJSIP_DIALOG_STATE_ESTABLISHED) {
		ast_debug(3, "Delay sending request to %s because dialog has not been established...\n",
			ast_sorcery_object_get_id(session->endpoint));
		return delay_request(session, on_request_creation, on_sdp_creation, on_response,
			generate_new_sdp,
			method == AST_SIP_SESSION_REFRESH_METHOD_INVITE
				? DELAYED_METHOD_INVITE : DELAYED_METHOD_UPDATE);
	}

	if (method == AST_SIP_SESSION_REFRESH_METHOD_INVITE) {
		if (inv_session->invite_tsx) {
			/* We can't send a reinvite yet, so delay it */
			ast_debug(3, "Delay sending reinvite to %s because of outstanding transaction...\n",
					ast_sorcery_object_get_id(session->endpoint));
			return delay_request(session, on_request_creation, on_sdp_creation,
				on_response, generate_new_sdp, DELAYED_METHOD_INVITE);
		} else if (inv_session->state != PJSIP_INV_STATE_CONFIRMED) {
			/* Initial INVITE transaction failed to progress us to a confirmed state
			 * which means re-invites are not possible
			 */
			ast_debug(3, "Not sending reinvite to %s because not in confirmed state...\n",
					ast_sorcery_object_get_id(session->endpoint));
			return 0;
		}
	}

	if (generate_new_sdp) {
		/* SDP can only be generated if current negotiation has already completed */
		if (inv_session->neg
			&& pjmedia_sdp_neg_get_state(inv_session->neg)
				!= PJMEDIA_SDP_NEG_STATE_DONE) {
			ast_debug(3, "Delay session refresh with new SDP to %s because SDP negotiation is not yet done...\n",
				ast_sorcery_object_get_id(session->endpoint));
			return delay_request(session, on_request_creation, on_sdp_creation,
				on_response, generate_new_sdp,
				method == AST_SIP_SESSION_REFRESH_METHOD_INVITE
					? DELAYED_METHOD_INVITE : DELAYED_METHOD_UPDATE);
		}

		new_sdp = generate_session_refresh_sdp(session);
		if (!new_sdp) {
			ast_log(LOG_ERROR, "Failed to generate session refresh SDP. Not sending session refresh\n");
			return -1;
		}
		if (on_sdp_creation) {
			if (on_sdp_creation(session, new_sdp)) {
				return -1;
			}
		}
	}


	if (method == AST_SIP_SESSION_REFRESH_METHOD_INVITE) {
		if (pjsip_inv_reinvite(inv_session, NULL, new_sdp, &tdata)) {
			ast_log(LOG_WARNING, "Failed to create reinvite properly.\n");
			return -1;
		}
	} else if (pjsip_inv_update(inv_session, NULL, new_sdp, &tdata)) {
		ast_log(LOG_WARNING, "Failed to create UPDATE properly.\n");
		return -1;
	}
	if (on_request_creation) {
		if (on_request_creation(session, tdata)) {
			return -1;
		}
	}
	ast_debug(3, "Sending session refresh SDP via %s to %s\n",
		method == AST_SIP_SESSION_REFRESH_METHOD_INVITE ? "re-INVITE" : "UPDATE",
		ast_sorcery_object_get_id(session->endpoint));
	ast_sip_session_send_request_with_cb(session, tdata, on_response);
	return 0;
}

int ast_sip_session_regenerate_answer(struct ast_sip_session *session,
		ast_sip_session_sdp_creation_cb on_sdp_creation)
{
	pjsip_inv_session *inv_session = session->inv_session;
	pjmedia_sdp_session *new_answer = NULL;
	const pjmedia_sdp_session *previous_offer = NULL;

	/* The SDP answer can only be regenerated if it is still pending to be sent */
	if (!inv_session->neg || (pjmedia_sdp_neg_get_state(inv_session->neg) != PJMEDIA_SDP_NEG_STATE_REMOTE_OFFER &&
		pjmedia_sdp_neg_get_state(inv_session->neg) != PJMEDIA_SDP_NEG_STATE_WAIT_NEGO)) {
		ast_log(LOG_WARNING, "Requested to regenerate local SDP answer for channel '%s' but negotiation in state '%s'\n",
			ast_channel_name(session->channel), pjmedia_sdp_neg_state_str(pjmedia_sdp_neg_get_state(inv_session->neg)));
		return -1;
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
		return -1;
	}

	if (on_sdp_creation) {
		if (on_sdp_creation(session, new_answer)) {
			return -1;
		}
	}

	pjsip_inv_set_sdp_answer(inv_session, new_answer);

	return 0;
}

void ast_sip_session_send_response(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	handle_outgoing_response(session, tdata);
	pjsip_inv_send_msg(session->inv_session, tdata);
	return;
}

static pj_bool_t session_on_rx_request(pjsip_rx_data *rdata);

static pjsip_module session_module = {
	.name = {"Session Module", 14},
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = session_on_rx_request,
};

/*! \brief Determine whether the SDP provided requires deferral of negotiating or not
 *
 * \retval 1 re-invite should be deferred and resumed later
 * \retval 0 re-invite should not be deferred
 */
static int sdp_requires_deferral(struct ast_sip_session *session, const pjmedia_sdp_session *sdp)
{
	int i;

	for (i = 0; i < sdp->media_count; ++i) {
		/* See if there are registered handlers for this media stream type */
		char media[20];
		struct ast_sip_session_sdp_handler *handler;
		RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);
		RAII_VAR(struct ast_sip_session_media *, session_media, NULL, ao2_cleanup);
		enum ast_sip_session_sdp_stream_defer res;

		/* We need a null-terminated version of the media string */
		ast_copy_pj_str(media, &sdp->media[i]->desc.media, sizeof(media));

		session_media = ao2_find(session->media, media, OBJ_KEY);
		if (!session_media) {
			/* if the session_media doesn't exist, there weren't
			 * any handlers at the time of its creation */
			continue;
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
			ast_debug(1, "No registered SDP handlers for media type '%s'\n", media);
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

	if (rdata->msg_info.msg->line.req.method.id != PJSIP_INVITE_METHOD ||
		!(dlg = pjsip_ua_find_dialog(&rdata->msg_info.cid->id, &rdata->msg_info.to->tag, &rdata->msg_info.from->tag, PJ_FALSE)) ||
		!(session = ast_sip_dialog_get_session(dlg)) ||
		!session->channel) {
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
		const pjmedia_sdp_session *local;
		int i;

		ast_queue_unhold(session->channel);

		pjmedia_sdp_neg_get_active_local(session->inv_session->neg, &local);
		if (!local) {
			return PJ_FALSE;
		}

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
		for (i = 0; i < local->media_count; ++i) {
			pjmedia_sdp_media *m = local->media[i];
			pjmedia_sdp_attr *recvonly;
			pjmedia_sdp_attr *inactive;

			recvonly = pjmedia_sdp_attr_find2(m->attr_count, m->attr, "recvonly", NULL);
			inactive = pjmedia_sdp_attr_find2(m->attr_count, m->attr, "inactive", NULL);
			if (recvonly || inactive) {
				pjmedia_sdp_attr *to_remove = recvonly ?: inactive;
				pjmedia_sdp_attr *sendrecv;

				pjmedia_sdp_attr_remove(&m->attr_count, m->attr, to_remove);

				sendrecv = pjmedia_sdp_attr_create(session->inv_session->pool, "sendrecv", NULL);
				pjmedia_sdp_media_add_attr(m, sendrecv);
			}
		}

		return PJ_FALSE;
	}

	if (!sdp_requires_deferral(session, sdp_info->sdp)) {
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

	if (!(offer = create_local_sdp(session->inv_session, session, NULL))) {
		pjsip_inv_terminate(session->inv_session, 500, PJ_FALSE);
		return -1;
	}

	pjsip_inv_set_local_sdp(session->inv_session, offer);
	pjmedia_sdp_neg_set_prefer_remote_codec_order(session->inv_session->neg, PJ_FALSE);
#ifdef PJMEDIA_SDP_NEG_ANSWER_MULTIPLE_CODECS
	pjmedia_sdp_neg_set_answer_multiple_codecs(session->inv_session->neg, PJ_TRUE);
#endif

	/*
	 * We MUST call set_from_header() before pjsip_inv_invite.  If we don't, the
	 * From in the initial INVITE will be wrong but the rest of the messages will be OK.
	 */
	set_from_header(session);

	if (pjsip_inv_invite(session->inv_session, tdata) != PJ_SUCCESS) {
		return -1;
	}

	return 0;
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

static void session_media_dtor(void *obj)
{
	struct ast_sip_session_media *session_media = obj;
	struct sdp_handler_list *handler_list;
	/* It is possible for SDP handlers to allocate memory on a session_media but
	 * not end up getting set as the handler for this session_media. This traversal
	 * ensures that all memory allocated by SDP handlers on the session_media is
	 * cleared (as well as file descriptors, etc.).
	 */
	handler_list = ao2_find(sdp_handlers, session_media->stream_type, OBJ_KEY);
	if (handler_list) {
		struct ast_sip_session_sdp_handler *handler;

		AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
			handler->stream_destroy(session_media);
		}
	}
	ao2_cleanup(handler_list);
	if (session_media->srtp) {
		ast_sdp_srtp_destroy(session_media->srtp);
	}
}

static void session_destructor(void *obj)
{
	struct ast_sip_session *session = obj;
	struct ast_sip_session_supplement *supplement;
	struct ast_sip_session_delayed_request *delay;
	const char *endpoint_name = session->endpoint ?
		ast_sorcery_object_get_id(session->endpoint) : "<none>";

	ast_debug(3, "Destroying SIP session with endpoint %s\n", endpoint_name);

	ast_test_suite_event_notify("SESSION_DESTROYING",
		"Endpoint: %s\r\n"
		"AOR: %s\r\n"
		"Contact: %s"
		, endpoint_name
		, session->aor ? ast_sorcery_object_get_id(session->aor) : "<none>"
		, session->contact ? ast_sorcery_object_get_id(session->contact) : "<none>"
		);

	while ((supplement = AST_LIST_REMOVE_HEAD(&session->supplements, next))) {
		if (supplement->session_destroy) {
			supplement->session_destroy(session);
		}
		ast_free(supplement);
	}

	ast_taskprocessor_unreference(session->serializer);
	ao2_cleanup(session->datastores);
	ao2_cleanup(session->media);

	AST_LIST_HEAD_DESTROY(&session->supplements);
	while ((delay = AST_LIST_REMOVE_HEAD(&session->delayed_requests, next))) {
		ast_free(delay);
	}
	ast_party_id_free(&session->id);
	ao2_cleanup(session->endpoint);
	ao2_cleanup(session->aor);
	ao2_cleanup(session->contact);
	ao2_cleanup(session->req_caps);
	ao2_cleanup(session->direct_media_cap);

	ast_dsp_free(session->dsp);

	if (session->inv_session) {
		pjsip_dlg_dec_session(session->inv_session->dlg, &session_module);
	}

	ast_test_suite_event_notify("SESSION_DESTROYED", "Endpoint: %s", endpoint_name);
}

static int add_session_media(void *obj, void *arg, int flags)
{
	struct sdp_handler_list *handler_list = obj;
	struct ast_sip_session *session = arg;
	RAII_VAR(struct ast_sip_session_media *, session_media, NULL, ao2_cleanup);

	session_media = ao2_alloc(sizeof(*session_media) + strlen(handler_list->stream_type), session_media_dtor);
	if (!session_media) {
		return CMP_STOP;
	}
	session_media->encryption = session->endpoint->media.rtp.encryption;
	session_media->remote_ice = session->endpoint->media.rtp.ice_support;
	session_media->remote_rtcp_mux = session->endpoint->rtcp_mux;
	session_media->keepalive_sched_id = -1;
	session_media->timeout_sched_id = -1;
	/* Safe use of strcpy */
	strcpy(session_media->stream_type, handler_list->stream_type);
	ao2_link(session->media, session_media);
	return 0;
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
	struct ast_sip_session_supplement *iter;
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
	session->req_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!session->req_caps) {
		return NULL;
	}
	session->datastores = ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp);
	if (!session->datastores) {
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

	session->media = ao2_container_alloc(MEDIA_BUCKETS, session_media_hash, session_media_cmp);
	if (!session->media) {
		return NULL;
	}
	/* fill session->media with available types */
	ao2_callback(sdp_handlers, OBJ_NODATA, add_session_media, session);

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

		session->serializer = ast_sip_create_serializer_named(tps_name);
	}
	if (!session->serializer) {
		return NULL;
	}
	ast_sip_dialog_set_serializer(inv_session->dlg, session->serializer);
	ast_sip_dialog_set_endpoint(inv_session->dlg, endpoint);
	pjsip_dlg_inc_session(inv_session->dlg, &session_module);
	inv_session->mod_data[session_module.id] = ao2_bump(session);
	session->contact = ao2_bump(contact);
	session->inv_session = inv_session;

	session->dtmf = endpoint->dtmf;

	if (ast_sip_session_add_supplements(session)) {
		/* Release the ref held by session->inv_session */
		ao2_ref(session, -1);
		return NULL;
	}
	AST_LIST_TRAVERSE(&session->supplements, iter, next) {
		if (iter->session_begin) {
			iter->session_begin(session);
		}
	}

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
	if (PJSIP_INV_STATE_CONFIRMED <= inv->state) {
		/*
		 * We cannot handle reINVITE authentication at this
		 * time because the reINVITE transaction is still in
		 * progress.
		 */
		ast_debug(1, "A reINVITE is being challenged.\n");
		return PJ_FALSE;
	}
	ast_debug(1, "Initial INVITE is being challenged.\n");

	session = inv->mod_data[session_module.id];

	if (ast_sip_create_request_with_auth(&session->endpoint->outbound_auths, rdata, tsx,
		&tdata)) {
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
	struct ast_format_cap *req_caps)
{
	const char *uri = NULL;
	RAII_VAR(struct ast_sip_aor *, found_aor, NULL, ao2_cleanup);
	RAII_VAR(struct ast_sip_contact *, found_contact, NULL, ao2_cleanup);
	pjsip_timer_setting timer;
	pjsip_dialog *dlg;
	struct pjsip_inv_session *inv_session;
	RAII_VAR(struct ast_sip_session *, session, NULL, ao2_cleanup);
	struct ast_sip_session *ret_session;

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
		return NULL;
	}

	if (!(dlg = ast_sip_create_dialog_uac(endpoint, uri, request_user))) {
		return NULL;
	}

	if (setup_outbound_invite_auth(dlg)) {
		pjsip_dlg_terminate(dlg);
		return NULL;
	}

	if (pjsip_inv_create_uac(dlg, NULL, endpoint->extensions.flags, &inv_session) != PJ_SUCCESS) {
		pjsip_dlg_terminate(dlg);
		return NULL;
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
	ast_party_id_copy(&session->id, &endpoint->id.self);

	if (ast_format_cap_count(req_caps)) {
		/* get joint caps between req_caps and endpoint caps */
		struct ast_format_cap *joint_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

		ast_format_cap_get_compatible(req_caps, endpoint->media.codecs, joint_caps);

		/* if joint caps */
		if (ast_format_cap_count(joint_caps)) {
			/* copy endpoint caps into session->req_caps */
			ast_format_cap_append_from_cap(session->req_caps,
				endpoint->media.codecs, AST_MEDIA_TYPE_UNKNOWN);
			/* replace instances of joint caps equivalents in session->req_caps */
			ast_format_cap_replace_from_cap(session->req_caps, joint_caps,
				AST_MEDIA_TYPE_UNKNOWN);
		}
		ao2_cleanup(joint_caps);
	}

	if (pjsip_dlg_add_usage(dlg, &session_module, NULL) != PJ_SUCCESS) {
		pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		/* Since we are not notifying ourselves that the INVITE session is being terminated
		 * we need to manually drop its reference to session
		 */
		ao2_ref(session, -1);
		return NULL;
	}

	/* Avoid unnecessary ref manipulation to return a session */
	ret_session = session;
	session = NULL;
	return ret_session;
}

static int session_end(void *vsession);
static int session_end_completion(void *vsession);

void ast_sip_session_terminate(struct ast_sip_session *session, int response)
{
	pj_status_t status;
	pjsip_tx_data *packet = NULL;

	if (session->defer_terminate) {
		session->terminate_while_deferred = 1;
		return;
	}

	if (!response) {
		response = 603;
	}

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
			ast_debug(3, "Delay sending BYE to %s because of outstanding transaction...\n",
					ast_sorcery_object_get_id(session->endpoint));
			/* If this is delayed the only thing that will happen is a BYE request so we don't
			 * actually need to store the response code for when it happens.
			 */
			delay_request(session, NULL, NULL, NULL, 0, DELAYED_METHOD_BYE);
			break;
		}
		/* Fall through */
	default:
		status = pjsip_inv_end_session(session->inv_session, response, NULL, &packet);
		if (status == PJ_SUCCESS && packet) {
			struct ast_sip_session_delayed_request *delay;

			/* Flush any delayed requests so they cannot overlap this transaction. */
			while ((delay = AST_LIST_REMOVE_HEAD(&session->delayed_requests, next))) {
				ast_free(delay);
			}

			if (packet->msg->type == PJSIP_RESPONSE_MSG) {
				ast_sip_session_send_response(session, packet);
			} else {
				ast_sip_session_send_request(session, packet);
			}
		}
		break;
	}
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
 *
 * \return Nothing
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
		ast_debug(3, "Ending session (%p) after being deferred\n", session);
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

	pickup_cfg = ast_get_chan_features_pickup_config(session->channel);
	if (!pickup_cfg) {
		ast_log(LOG_ERROR, "Unable to retrieve pickup configuration options. Unable to detect call pickup extension\n");
		pickupexten = "";
	} else {
		pickupexten = ast_strdupa(pickup_cfg->pickupexten);
		ao2_ref(pickup_cfg, -1);
	}

	if (!strcmp(session->exten, pickupexten) ||
		ast_exists_extension(NULL, session->endpoint->context, session->exten, 1, NULL)) {
		size_t size = pj_strlen(&sip_ruri->host) + 1;
		char *domain = ast_alloca(size);

		ast_copy_pj_str(domain, &sip_ruri->host, size);
		pbx_builtin_setvar_helper(session->channel, "SIPDOMAIN", domain);

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

static pjsip_inv_session *pre_session_setup(pjsip_rx_data *rdata, const struct ast_sip_endpoint *endpoint)
{
	pjsip_tx_data *tdata;
	pjsip_dialog *dlg;
	pjsip_inv_session *inv_session;
	unsigned int options = endpoint->extensions.flags;
	pj_status_t dlg_status;

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
	dlg = ast_sip_create_dialog_uas(endpoint, rdata, &dlg_status);
	if (!dlg) {
		if (dlg_status != PJ_EEXISTS) {
			pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		}
		return NULL;
	}
	if (pjsip_inv_create_uas(dlg, rdata, NULL, options, &inv_session) != PJ_SUCCESS) {
		pjsip_endpt_respond_stateless(ast_sip_get_pjsip_endpoint(), rdata, 500, NULL, NULL, NULL);
		pjsip_dlg_terminate(dlg);
		return NULL;
	}

#if defined(HAVE_PJSIP_REPLACE_MEDIA_STREAM) || defined(PJMEDIA_SDP_NEG_ALLOW_MEDIA_CHANGE)
	inv_session->sdp_neg_flags = PJMEDIA_SDP_NEG_ALLOW_MEDIA_CHANGE;
#endif
	if (pjsip_dlg_add_usage(dlg, &session_module, NULL) != PJ_SUCCESS) {
		if (pjsip_inv_initial_answer(inv_session, rdata, 500, NULL, NULL, &tdata) != PJ_SUCCESS) {
			pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		}
		pjsip_inv_send_msg(inv_session, tdata);
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

static int new_invite(struct new_invite *invite)
{
	pjsip_tx_data *tdata = NULL;
	pjsip_timer_setting timer;
	pjsip_rdata_sdp_info *sdp_info;
	pjmedia_sdp_session *local = NULL;

	/* From this point on, any calls to pjsip_inv_terminate have the last argument as PJ_TRUE
	 * so that we will be notified so we can destroy the session properly
	 */

	if (invite->session->inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Session already DISCONNECTED [reason=%d (%s)]\n",
			invite->session->inv_session->cause,
			pjsip_get_status_text(invite->session->inv_session->cause)->ptr);
#ifdef HAVE_PJSIP_INV_SESSION_REF
		pjsip_inv_dec_ref(invite->session->inv_session);
#endif
		return -1;
	}

	switch (get_destination(invite->session, invite->rdata)) {
	case SIP_GET_DEST_EXTEN_FOUND:
		/* Things worked. Keep going */
		break;
	case SIP_GET_DEST_UNSUPPORTED_URI:
		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 416, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 416, PJ_TRUE);
		}
		goto end;
	case SIP_GET_DEST_EXTEN_PARTIAL:
		ast_debug(1, "Call from '%s' (%s:%s:%d) to extension '%s' - partial match\n", ast_sorcery_object_get_id(invite->session->endpoint),
			invite->rdata->tp_info.transport->type_name, invite->rdata->pkt_info.src_name, invite->rdata->pkt_info.src_port, invite->session->exten);

		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 484, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 484, PJ_TRUE);
		}
		goto end;
	case SIP_GET_DEST_EXTEN_NOT_FOUND:
	default:
		ast_log(LOG_NOTICE, "Call from '%s' (%s:%s:%d) to extension '%s' rejected because extension not found in context '%s'.\n",
			ast_sorcery_object_get_id(invite->session->endpoint), invite->rdata->tp_info.transport->type_name, invite->rdata->pkt_info.src_name,
			invite->rdata->pkt_info.src_port, invite->session->exten, invite->session->endpoint->context);

		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 404, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 404, PJ_TRUE);
		}
		goto end;
	};

	pjsip_timer_setting_default(&timer);
	timer.min_se = invite->session->endpoint->extensions.timer.min_se;
	timer.sess_expires = invite->session->endpoint->extensions.timer.sess_expires;
	pjsip_timer_init_session(invite->session->inv_session, &timer);

	/*
	 * At this point, we've verified what we can that won't take awhile,
	 * so let's go ahead and send a 100 Trying out to stop any
	 * retransmissions.
	 */
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
	pjmedia_sdp_neg_set_answer_multiple_codecs(invite->session->inv_session->neg, PJ_TRUE);
#endif

	handle_incoming_request(invite->session, invite->rdata);

end:
#ifdef HAVE_PJSIP_INV_SESSION_REF
	pjsip_inv_dec_ref(invite->session->inv_session);
#endif
	return 0;
}

static void handle_new_invite_request(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint,
			ast_pjsip_rdata_get_endpoint(rdata), ao2_cleanup);
	pjsip_tx_data *tdata = NULL;
	pjsip_inv_session *inv_session = NULL;
	struct ast_sip_session *session;
	struct new_invite invite;

	ast_assert(endpoint != NULL);

	inv_session = pre_session_setup(rdata, endpoint);
	if (!inv_session) {
		/* pre_session_setup() returns a response on failure */
		return;
	}

#ifdef HAVE_PJSIP_INV_SESSION_REF
	if (pjsip_inv_add_ref(inv_session) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Can't increase the session reference counter\n");
		if (inv_session->state != PJSIP_INV_STATE_DISCONNECTED) {
			if (pjsip_inv_initial_answer(inv_session, rdata, 500, NULL, NULL, &tdata) == PJ_SUCCESS) {
				pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
			} else {
				pjsip_inv_send_msg(inv_session, tdata);
			}
		}
		return;
	}
#endif

	session = ast_sip_session_alloc(endpoint, NULL, inv_session, rdata);
	if (!session) {
		if (pjsip_inv_initial_answer(inv_session, rdata, 500, NULL, NULL, &tdata) == PJ_SUCCESS) {
			pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		} else {
			pjsip_inv_send_msg(inv_session, tdata);
		}
#ifdef HAVE_PJSIP_INV_SESSION_REF
		pjsip_inv_dec_ref(inv_session);
#endif
		return;
	}

	/*
	 * The current thread is supposed be the session serializer to prevent
	 * any initial INVITE retransmissions from trying to setup the same
	 * call again.
	 */
	ast_assert(ast_taskprocessor_is_task(session->serializer));

	invite.session = session;
	invite.rdata = rdata;
	new_invite(&invite);

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
	pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
	pjsip_inv_session *inv_session;

	switch (rdata->msg_info.msg->line.req.method.id) {
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

	return handled;
}

static void resend_reinvite(pj_timer_heap_t *timer, pj_timer_entry *entry)
{
	struct ast_sip_session *session = entry->user_data;

	ast_debug(3, "Endpoint '%s(%s)' re-INVITE collision timer expired.\n",
		ast_sorcery_object_get_id(session->endpoint),
		session->channel ? ast_channel_name(session->channel) : "");

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

	ast_debug(3, "Endpoint '%s(%s)' re-INVITE collision.\n",
		ast_sorcery_object_get_id(session->endpoint),
		session->channel ? ast_channel_name(session->channel) : "");
	if (delay_request(session, NULL, NULL, on_response, 1, DELAYED_METHOD_INVITE)) {
		return;
	}
	if (pj_timer_entry_running(&session->rescheduled_reinvite)) {
		/* Timer already running.  Something weird is going on. */
		ast_debug(1, "Endpoint '%s(%s)' re-INVITE collision while timer running!!!\n",
			ast_sorcery_object_get_id(session->endpoint),
			session->channel ? ast_channel_name(session->channel) : "");
		return;
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
	}
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

	ast_debug(3, "Method is %.*s\n", (int) pj_strlen(&req.method.name), pj_strbuf(&req.method.name));
	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (supplement->incoming_request && does_method_match(&req.method.name, supplement->method)) {
			if (supplement->incoming_request(session, rdata)) {
				break;
			}
		}
	}
}

static void handle_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata,
		enum ast_sip_session_response_priority response_priority)
{
	struct ast_sip_session_supplement *supplement;
	struct pjsip_status_line status = rdata->msg_info.msg->line.status;

	ast_debug(3, "Response is %d %.*s\n", status.code, (int) pj_strlen(&status.reason),
			pj_strbuf(&status.reason));

	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (!(supplement->response_priority & response_priority)) {
			continue;
		}
		if (supplement->incoming_response && does_method_match(&rdata->msg_info.cseq->method.name, supplement->method)) {
			supplement->incoming_response(session, rdata);
		}
	}
}

static int handle_incoming(struct ast_sip_session *session, pjsip_rx_data *rdata,
		enum ast_sip_session_response_priority response_priority)
{
	ast_debug(3, "Received %s\n", rdata->msg_info.msg->type == PJSIP_REQUEST_MSG ?
			"request" : "response");

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

	ast_debug(3, "Method is %.*s\n", (int) pj_strlen(&req.method.name), pj_strbuf(&req.method.name));
	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (supplement->outgoing_request && does_method_match(&req.method.name, supplement->method)) {
			supplement->outgoing_request(session, tdata);
		}
	}
}

static void handle_outgoing_response(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	struct ast_sip_session_supplement *supplement;
	struct pjsip_status_line status = tdata->msg->line.status;
	pjsip_cseq_hdr *cseq = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL);

	if (!cseq) {
		ast_log(LOG_ERROR, "Cannot send response due to missing sequence header");
		return;
	}

	ast_debug(3, "Method is %.*s, Response is %d %.*s\n", (int) pj_strlen(&cseq->method.name),
		pj_strbuf(&cseq->method.name), status.code, (int) pj_strlen(&status.reason),
		pj_strbuf(&status.reason));

	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (supplement->outgoing_response && does_method_match(&cseq->method.name, supplement->method)) {
			supplement->outgoing_response(session, tdata);
		}
	}
}

static int session_end(void *vsession)
{
	struct ast_sip_session *session = vsession;
	struct ast_sip_session_supplement *iter;

	/* Stop the scheduled termination */
	sip_session_defer_termination_stop_timer(session);

	/* Session is dead.  Notify the supplements. */
	AST_LIST_TRAVERSE(&session->supplements, iter, next) {
		if (iter->session_end) {
			iter->session_end(session);
		}
	}

	/* Release any media resources. */
	ao2_cleanup(session->media);
	session->media = NULL;

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

static void handle_incoming_before_media(pjsip_inv_session *inv,
	struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	pjsip_msg *msg;

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
			"Endpoint '%s(%s)': Ending session due to incomplete SDP negotiation.  %s\n",
			ast_sorcery_object_get_id(session->endpoint),
			session->channel ? ast_channel_name(session->channel) : "",
			pjsip_rx_data_get_info(rdata));
		if (pjsip_inv_end_session(inv, 400, NULL, &tdata) == PJ_SUCCESS
			&& tdata) {
			ast_sip_session_send_request(session, tdata);
		}
	}
}

static void session_inv_on_state_changed(pjsip_inv_session *inv, pjsip_event *e)
{
	struct ast_sip_session *session;
	pjsip_event_id_e type;

	if (ast_shutdown_final()) {
		return;
	}

	if (e) {
		print_debug_details(inv, NULL, e);
		type = e->type;
	} else {
		type = PJSIP_EVENT_UNKNOWN;
	}

	session = inv->mod_data[session_module.id];
	if (!session) {
		return;
	}

	switch(type) {
	case PJSIP_EVENT_TX_MSG:
		break;
	case PJSIP_EVENT_RX_MSG:
		handle_incoming_before_media(inv, session, e->body.rx_msg.rdata);
		break;
	case PJSIP_EVENT_TSX_STATE:
		ast_debug(3, "Source of transaction state change is %s\n", pjsip_event_str(e->body.tsx_state.type));
		/* Transaction state changes are prompted by some other underlying event. */
		switch(e->body.tsx_state.type) {
		case PJSIP_EVENT_TX_MSG:
			break;
		case PJSIP_EVENT_RX_MSG:
			handle_incoming_before_media(inv, session, e->body.tsx_state.src.rdata);
			break;
		case PJSIP_EVENT_TRANSPORT_ERROR:
		case PJSIP_EVENT_TIMER:
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
			ast_debug(3, "Deferring session (%p) end\n", session);
			session->ended_while_deferred = 1;
			return;
		}

		if (ast_sip_push_task(session->serializer, session_end, session)) {
			/* Do it anyway even though this is not the right thread. */
			session_end(session);
		}
	}
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
	struct ast_sip_session *session;
	pjsip_tx_data *tdata;

	if (ast_shutdown_final()) {
		return;
	}

	session = inv->mod_data[id];

	print_debug_details(inv, tsx, e);
	if (!session) {
		/* The session has ended.  Ignore the transaction change. */
		return;
	}

	/*
	 * If the session is disconnected really nothing else to do unless currently transacting
	 * a BYE. If a BYE then hold off destruction until the transaction timeout occurs. This
	 * has to be done for BYEs because sometimes the dialog can be in a disconnected
	 * state but the BYE request transaction has not yet completed.
	 */
	if (tsx->method.id != PJSIP_BYE_METHOD && session_end_if_disconnected(id, inv)) {
		return;
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
						return;
					}
					if (inv->state == PJSIP_INV_STATE_CONFIRMED) {
						ast_debug(1, "reINVITE received final response code %d\n",
							tsx->status_code);
						if ((tsx->status_code == 401 || tsx->status_code == 407)
							&& !ast_sip_create_request_with_auth(
								&session->endpoint->outbound_auths,
								e->body.tsx_state.src.rdata, tsx, &tdata)) {
							/* Send authed reINVITE */
							ast_sip_session_send_request_with_cb(session, tdata, cb);
							return;
						}
						if (tsx->status_code != 488) {
							/* Other reinvite failures (except 488) result in destroying the session. */
							if (pjsip_inv_end_session(inv, 500, NULL, &tdata) == PJ_SUCCESS
								&& tdata) {
								ast_sip_session_send_request(session, tdata);
							}
						}
					}
				} else if (tsx->state == PJSIP_TSX_STATE_TERMINATED) {
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
						 *
						 * 3) We didn't send a CANCEL but the UAS sent us an invalid
						 * SDP with the 200 OK.  In this case the SDP negotiation is
						 * incomplete and PJPROJECT has already sent the BYE for us
						 * because of the invalid SDP.
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
							ast_debug(1, "Endpoint '%s(%s)': Incomplete SDP negotiation cancelled session.  %s\n",
								ast_sorcery_object_get_id(session->endpoint),
								session->channel ? ast_channel_name(session->channel) : "",
								pjsip_rx_data_get_info(e->body.tsx_state.src.rdata));
						} else if (pjsip_inv_end_session(inv, 500, NULL, &tdata) == PJ_SUCCESS
							&& tdata) {
							ast_debug(1, "Endpoint '%s(%s)': Ending session due to RFC5407 race condition.  %s\n",
								ast_sorcery_object_get_id(session->endpoint),
								session->channel ? ast_channel_name(session->channel) : "",
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
					ast_debug(1, "%.*s received final response code %d\n",
						(int) pj_strlen(&tsx->method.name), pj_strbuf(&tsx->method.name),
						tsx->status_code);
					if ((tsx->status_code == 401 || tsx->status_code == 407)
						&& !ast_sip_create_request_with_auth(
							&session->endpoint->outbound_auths,
							e->body.tsx_state.src.rdata, tsx, &tdata)) {
						/* Send authed version of the method */
						ast_sip_session_send_request_with_cb(session, tdata, cb);
						return;
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
			return;
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
		return;
	}

	if (tsx->method.id == PJSIP_INVITE_METHOD) {
		if (tsx->state == PJSIP_TSX_STATE_PROCEEDING) {
			ast_debug(3, "Endpoint '%s(%s)' INVITE delay check. tsx-state:%s\n",
				ast_sorcery_object_get_id(session->endpoint),
				session->channel ? ast_channel_name(session->channel) : "",
				pjsip_tsx_state_str(tsx->state));
			check_delayed_requests(session, invite_proceeding);
		} else if (tsx->state == PJSIP_TSX_STATE_TERMINATED) {
			/*
			 * Terminated INVITE transactions always should result in
			 * queuing delayed requests, no matter what event caused
			 * the transaction to terminate.
			 */
			ast_debug(3, "Endpoint '%s(%s)' INVITE delay check. tsx-state:%s\n",
				ast_sorcery_object_get_id(session->endpoint),
				session->channel ? ast_channel_name(session->channel) : "",
				pjsip_tsx_state_str(tsx->state));
			check_delayed_requests(session, invite_terminated);
		}
	} else if (tsx->role == PJSIP_ROLE_UAC
		&& tsx->state == PJSIP_TSX_STATE_COMPLETED
		&& !pj_strcmp2(&tsx->method.name, "UPDATE")) {
		ast_debug(3, "Endpoint '%s(%s)' UPDATE delay check. tsx-state:%s\n",
			ast_sorcery_object_get_id(session->endpoint),
			session->channel ? ast_channel_name(session->channel) : "",
			pjsip_tsx_state_str(tsx->state));
		check_delayed_requests(session, update_completed);
	}
}

static int add_sdp_streams(void *obj, void *arg, void *data, int flags)
{
	struct ast_sip_session_media *session_media = obj;
	pjmedia_sdp_session *answer = arg;
	struct ast_sip_session *session = data;
	struct ast_sip_session_sdp_handler *handler = session_media->handler;
	RAII_VAR(struct sdp_handler_list *, handler_list, NULL, ao2_cleanup);
	int res;

	if (handler) {
		/* if an already assigned handler reports a catastrophic error, fail */
		res = handler->create_outgoing_sdp_stream(session, session_media, answer);
		if (res < 0) {
			return 0;
		}
		return CMP_MATCH;
	}

	handler_list = ao2_find(sdp_handlers, session_media->stream_type, OBJ_KEY);
	if (!handler_list) {
		return CMP_MATCH;
	}

	/* no handler for this stream type and we have a list to search */
	AST_LIST_TRAVERSE(&handler_list->list, handler, next) {
		if (handler == session_media->handler) {
			continue;
		}
		res = handler->create_outgoing_sdp_stream(session, session_media, answer);
		if (res < 0) {
			/* catastrophic error */
			return 0;
		}
		if (res > 0) {
			/* Handled by this handler. Move to the next stream */
			session_media_set_handler(session_media, handler);
			return CMP_MATCH;
		}
	}

	/* streams that weren't handled won't be included in generated outbound SDP */
	return CMP_MATCH;
}

static struct pjmedia_sdp_session *create_local_sdp(pjsip_inv_session *inv, struct ast_sip_session *session, const pjmedia_sdp_session *offer)
{
	RAII_VAR(struct ao2_iterator *, successful, NULL, ao2_iterator_cleanup);
	static const pj_str_t STR_IN = { "IN", 2 };
	static const pj_str_t STR_IP4 = { "IP4", 3 };
	static const pj_str_t STR_IP6 = { "IP6", 3 };
	pjmedia_sdp_session *local;

	if (inv->state == PJSIP_INV_STATE_DISCONNECTED) {
		ast_log(LOG_ERROR, "Failed to create session SDP. Session has been already disconnected\n");
		return NULL;
	}

	if (!inv->pool_prov || !(local = PJ_POOL_ZALLOC_T(inv->pool_prov, pjmedia_sdp_session))) {
		return NULL;
	}

	if (!offer) {
		local->origin.version = local->origin.id = (pj_uint32_t)(ast_random());
	} else {
		local->origin.version = offer->origin.version + 1;
		local->origin.id = offer->origin.id;
	}

	pj_strdup2(inv->pool_prov, &local->origin.user, session->endpoint->media.sdpowner);
	pj_strdup2(inv->pool_prov, &local->name, session->endpoint->media.sdpsession);

	/* Now let the handlers add streams of various types, pjmedia will automatically reorder the media streams for us */
	successful = ao2_callback_data(session->media, OBJ_MULTIPLE, add_sdp_streams, local, session);
	if (!successful || ao2_iterator_count(successful) != ao2_container_count(session->media)) {
		/* Something experienced a catastrophic failure */
		return NULL;
	}

	/* Use the connection details of the first media stream if possible for SDP level */
	if (local->media_count) {
		int stream;

		/* Since we are using the first media stream as the SDP level we can get rid of it
		 * from the stream itself
		 */
		local->conn = local->media[0]->conn;
		local->media[0]->conn = NULL;
		pj_strassign(&local->origin.net_type, &local->conn->net_type);
		pj_strassign(&local->origin.addr_type, &local->conn->addr_type);
		pj_strassign(&local->origin.addr, &local->conn->addr);

		/* Go through each media stream seeing if the connection details actually differ,
		 * if not just use SDP level and reduce the SDP size
		 */
		for (stream = 1; stream < local->media_count; stream++) {
			if (!pj_strcmp(&local->conn->net_type, &local->media[stream]->conn->net_type) &&
				!pj_strcmp(&local->conn->addr_type, &local->media[stream]->conn->addr_type) &&
				!pj_strcmp(&local->conn->addr, &local->media[stream]->conn->addr)) {
				local->media[stream]->conn = NULL;
			}
		}
	} else {
		local->origin.net_type = STR_IN;
		local->origin.addr_type = session->endpoint->media.rtp.ipv6 ? STR_IP6 : STR_IP4;

		if (!ast_strlen_zero(session->endpoint->media.address)) {
			pj_strdup2(inv->pool_prov, &local->origin.addr, session->endpoint->media.address);
		} else {
			pj_strdup2(inv->pool_prov, &local->origin.addr, ast_sip_get_host_ip_string(session->endpoint->media.rtp.ipv6 ? pj_AF_INET6() : pj_AF_INET()));
		}
	}

	return local;
}

static void session_inv_on_rx_offer(pjsip_inv_session *inv, const pjmedia_sdp_session *offer)
{
	struct ast_sip_session *session;
	pjmedia_sdp_session *answer;

	if (ast_shutdown_final()) {
		return;
	}

	session = inv->mod_data[session_module.id];
	if (handle_incoming_sdp(session, offer)) {
		return;
	}

	if ((answer = create_local_sdp(inv, session, offer))) {
		pjsip_inv_set_sdp_answer(inv, answer);
	}
}

#if 0
static void session_inv_on_create_offer(pjsip_inv_session *inv, pjmedia_sdp_session **p_offer)
{
	/* XXX STUB */
}
#endif

static void session_inv_on_media_update(pjsip_inv_session *inv, pj_status_t status)
{
	struct ast_sip_session *session;
	const pjmedia_sdp_session *local, *remote;

	if (ast_shutdown_final()) {
		return;
	}

	session = inv->mod_data[session_module.id];
	if (!session || !session->channel) {
		/*
		 * If we don't have a session or channel then we really
		 * don't care about media updates.
		 * Just ignore
		 */
		return;
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
			if (session->endpoint->follow_early_media_fork) {
				ast_debug(3, "Following early media fork with different To tags\n");
			} else {
				ast_debug(3, "Not following early media fork with different To tags\n");
				bail = 1;
			}
		}
#ifdef HAVE_PJSIP_INV_ACCEPT_MULTIPLE_SDP_ANSWERS
		else if (inv->updated_sdp_answer) {
			if (session->endpoint->accept_multiple_sdp_answers) {
				ast_debug(3, "Accepting updated SDP with same To tag\n");
			} else {
				ast_debug(3, "Ignoring updated SDP answer with same To tag\n");
				bail = 1;
			}
		}
#endif
		if (bail) {
			return;
		}
	}

	if ((status != PJ_SUCCESS) || (pjmedia_sdp_neg_get_active_local(inv->neg, &local) != PJ_SUCCESS) ||
		(pjmedia_sdp_neg_get_active_remote(inv->neg, &remote) != PJ_SUCCESS)) {
		ast_channel_hangupcause_set(session->channel, AST_CAUSE_BEARERCAPABILITY_NOTAVAIL);
		ast_set_hangupsource(session->channel, ast_channel_name(session->channel), 0);
		ast_queue_hangup(session->channel);
		return;
	}

	handle_negotiated_sdp(session, local, remote);
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
	.on_media_update = session_inv_on_media_update,
	.on_redirected = session_inv_on_redirected,
};

/*! \brief Hook for modifying outgoing messages with SDP to contain the proper address information */
static void session_outgoing_nat_hook(pjsip_tx_data *tdata, struct ast_sip_transport *transport)
{
	RAII_VAR(struct ast_sip_transport_state *, transport_state, ast_sip_get_transport_state(ast_sorcery_object_get_id(transport)), ao2_cleanup);
	struct ast_sip_nat_hook *hook = ast_sip_mod_data_get(
		tdata->mod_data, session_module.id, MOD_DATA_NAT_HOOK);
	struct pjmedia_sdp_session *sdp;
	int stream;

	/* SDP produced by us directly will never be multipart */
	if (!transport_state || hook || !tdata->msg->body ||
		!ast_sip_is_content_type(&tdata->msg->body->content_type, "application", "sdp") ||
		ast_strlen_zero(transport->external_media_address)) {
		return;
	}

	sdp = tdata->msg->body->data;

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
			ast_debug(5, "Setting external media address to %s\n", ast_sockaddr_stringify_host(&transport_state->external_media_address));
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
			ast_debug(1, "No registered SDP handlers for media type '%s'\n", media);
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

static int load_module(void)
{
	pjsip_endpoint *endpt;

	CHECK_PJSIP_MODULE_LOADED();

	if (!ast_sip_get_sorcery() || !ast_sip_get_pjsip_endpoint()) {
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(nat_hook = ast_sorcery_alloc(ast_sip_get_sorcery(), "nat_hook", NULL))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	nat_hook->outgoing_external_message = session_outgoing_nat_hook;
	ast_sorcery_create(ast_sip_get_sorcery(), nat_hook);
	sdp_handlers = ao2_container_alloc(SDP_HANDLER_BUCKETS,
			sdp_handler_list_hash, sdp_handler_list_cmp);
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

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
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
	       );
