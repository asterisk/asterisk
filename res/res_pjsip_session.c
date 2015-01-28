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

#define SDP_HANDLER_BUCKETS 11

#define MOD_DATA_ON_RESPONSE "on_response"
#define MOD_DATA_NAT_HOOK "nat_hook"

/* Some forward declarations */
static void handle_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata, pjsip_event_id_e type);
static void handle_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata, pjsip_event_id_e type,
		enum ast_sip_session_response_priority response_priority);
static int handle_incoming(struct ast_sip_session *session, pjsip_rx_data *rdata, pjsip_event_id_e type,
		enum ast_sip_session_response_priority response_priority);
static void handle_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata);
static void handle_outgoing_response(struct ast_sip_session *session, pjsip_tx_data *tdata);
static void handle_outgoing(struct ast_sip_session *session, pjsip_tx_data *tdata);

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

AST_RWLIST_HEAD_STATIC(session_supplements, ast_sip_session_supplement);

int ast_sip_session_register_supplement(struct ast_sip_session_supplement *supplement)
{
	struct ast_sip_session_supplement *iter;
	int inserted = 0;
	SCOPED_LOCK(lock, &session_supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	if (!supplement->response_priority) {
		supplement->response_priority = AST_SIP_SESSION_BEFORE_MEDIA;
	}

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&session_supplements, iter, next) {
		if (iter->priority > supplement->priority) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(supplement, next);
			inserted = 1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (!inserted) {
		AST_RWLIST_INSERT_TAIL(&session_supplements, supplement, next);
	}
	ast_module_ref(ast_module_info->self);
	return 0;
}

void ast_sip_session_unregister_supplement(struct ast_sip_session_supplement *supplement)
{
	struct ast_sip_session_supplement *iter;
	SCOPED_LOCK(lock, &session_supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&session_supplements, iter, next) {
		if (supplement == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			ast_module_unref(ast_module_info->self);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static struct ast_sip_session_supplement *supplement_dup(const struct ast_sip_session_supplement *src)
{
	struct ast_sip_session_supplement *dst = ast_calloc(1, sizeof(*dst));
	if (!dst) {
		return NULL;
	}
	/* Will need to revisit if shallow copy becomes an issue */
	*dst = *src;
	return dst;
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

/*!
 * \brief Structure used for sending delayed requests
 *
 * Requests are typically delayed because the current transaction
 * state of an INVITE. Once the pending INVITE transaction terminates,
 * the delayed request will be sent
 */
struct ast_sip_session_delayed_request {
	/*! Method of the request */
	char method[15];
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

static struct ast_sip_session_delayed_request *delayed_request_alloc(const char *method,
		ast_sip_session_request_creation_cb on_request_creation,
		ast_sip_session_sdp_creation_cb on_sdp_creation,
		ast_sip_session_response_cb on_response,
		int generate_new_sdp)
{
	struct ast_sip_session_delayed_request *delay = ast_calloc(1, sizeof(*delay));
	if (!delay) {
		return NULL;
	}
	ast_copy_string(delay->method, method, sizeof(delay->method));
	delay->on_request_creation = on_request_creation;
	delay->on_sdp_creation = on_sdp_creation;
	delay->on_response = on_response;
	delay->generate_new_sdp = generate_new_sdp;
	return delay;
}

static int send_delayed_request(struct ast_sip_session *session, struct ast_sip_session_delayed_request *delay)
{
	ast_debug(3, "Sending delayed %s request to %s\n", delay->method, ast_sorcery_object_get_id(session->endpoint));

	if (!strcmp(delay->method, "INVITE")) {
		ast_sip_session_refresh(session, delay->on_request_creation,
				delay->on_sdp_creation, delay->on_response, AST_SIP_SESSION_REFRESH_METHOD_INVITE, delay->generate_new_sdp);
	} else if (!strcmp(delay->method, "UPDATE")) {
		ast_sip_session_refresh(session, delay->on_request_creation,
				delay->on_sdp_creation, delay->on_response, AST_SIP_SESSION_REFRESH_METHOD_UPDATE, delay->generate_new_sdp);
	} else {
		ast_log(LOG_WARNING, "Unexpected delayed %s request with no existing request structure\n", delay->method);
		return -1;
	}
	return 0;
}

static int queued_delayed_request_send(void *data)
{
	RAII_VAR(struct ast_sip_session *, session, data, ao2_cleanup);
	RAII_VAR(struct ast_sip_session_delayed_request *, delay, NULL, ast_free_ptr);

	delay = AST_LIST_REMOVE_HEAD(&session->delayed_requests, next);
	if (!delay) {
		return 0;
	}

	return send_delayed_request(session, delay);
}

static void queue_delayed_request(struct ast_sip_session *session)
{
	if (AST_LIST_EMPTY(&session->delayed_requests)) {
		/* No delayed request to send, so just return */
		return;
	}

	ast_debug(3, "Queuing delayed request to run for %s\n",
			ast_sorcery_object_get_id(session->endpoint));

	ao2_ref(session, +1);
	ast_sip_push_task(session->serializer, queued_delayed_request_send, session);
}

static int delay_request(struct ast_sip_session *session, ast_sip_session_request_creation_cb on_request,
		ast_sip_session_sdp_creation_cb on_sdp_creation, ast_sip_session_response_cb on_response,
		int generate_new_sdp, const char *method)
{
	struct ast_sip_session_delayed_request *delay = delayed_request_alloc(method,
			on_request, on_sdp_creation, on_response, generate_new_sdp);

	if (!delay) {
		return -1;
	}

	AST_LIST_INSERT_TAIL(&session->delayed_requests, delay, next);
	return 0;
}

static pjmedia_sdp_session *generate_session_refresh_sdp(struct ast_sip_session *session)
{
	pjsip_inv_session *inv_session = session->inv_session;
	const pjmedia_sdp_session *previous_sdp;

	if (pjmedia_sdp_neg_was_answer_remote(inv_session->neg)) {
		pjmedia_sdp_neg_get_active_remote(inv_session->neg, &previous_sdp);
	} else {
		pjmedia_sdp_neg_get_active_local(inv_session->neg, &previous_sdp);
	}
	return create_local_sdp(inv_session, session, previous_sdp);
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
		ast_debug(3, "Delaying sending request to %s because dialog has not been established...\n",
			ast_sorcery_object_get_id(session->endpoint));
		return delay_request(session, on_request_creation, on_sdp_creation, on_response, generate_new_sdp,
			method == AST_SIP_SESSION_REFRESH_METHOD_INVITE ? "INVITE" : "UPDATE");
	}

	if (method == AST_SIP_SESSION_REFRESH_METHOD_INVITE) {
		if (inv_session->invite_tsx) {
			/* We can't send a reinvite yet, so delay it */
			ast_debug(3, "Delaying sending reinvite to %s because of outstanding transaction...\n",
					ast_sorcery_object_get_id(session->endpoint));
			return delay_request(session, on_request_creation, on_sdp_creation, on_response,
				generate_new_sdp, "INVITE");
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
		if (pjmedia_sdp_neg_get_state(inv_session->neg) != PJMEDIA_SDP_NEG_STATE_DONE) {
			ast_debug(3, "Delaying session refresh with new SDP to %s because SDP negotiation is not yet done...\n",
				ast_sorcery_object_get_id(session->endpoint));
			return delay_request(session, on_request_creation, on_sdp_creation, on_response, generate_new_sdp,
				method == AST_SIP_SESSION_REFRESH_METHOD_INVITE ? "INVITE" : "UPDATE");
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
		!(session = ast_sip_dialog_get_session(dlg))) {
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
			pjsip_endpt_send_response2(ast_sip_get_pjsip_endpoint(), rdata, tdata, NULL, NULL);
		}

		return PJ_TRUE;
	}

	if (!(sdp_info = pjsip_rdata_get_sdp_info(rdata)) ||
		(sdp_info->sdp_err != PJ_SUCCESS)) {
		return PJ_FALSE;
	}

	if (!sdp_info->sdp) {
		ast_queue_unhold(session->channel);
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

	pjsip_endpt_process_rx_data(ast_sip_get_pjsip_endpoint(), session->deferred_reinvite, NULL, NULL);
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

	if (inv_session->state == PJSIP_INV_STATE_DISCONNECTED) {
		/* Don't try to do anything with a hung-up call */
		return;
	}

	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, session_module.id,
			     MOD_DATA_ON_RESPONSE, on_response);

	if (!ast_strlen_zero(session->endpoint->fromuser) ||
		!ast_strlen_zero(session->endpoint->fromdomain)) {
		pjsip_fromto_hdr *from = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_FROM, tdata->msg->hdr.next);
		pjsip_sip_uri *uri = pjsip_uri_get_uri(from->uri);

		if (!ast_strlen_zero(session->endpoint->fromuser)) {
			pj_strdup2(tdata->pool, &uri->user, session->endpoint->fromuser);
		}
		if (!ast_strlen_zero(session->endpoint->fromdomain)) {
			pj_strdup2(tdata->pool, &uri->host, session->endpoint->fromdomain);
		}
	}

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

	ast_debug(3, "Destroying SIP session with endpoint %s\n",
			ast_sorcery_object_get_id(session->endpoint));

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
	ao2_cleanup(session->contact);
	ao2_cleanup(session->req_caps);
	ao2_cleanup(session->direct_media_cap);

	if (session->dsp) {
		ast_dsp_free(session->dsp);
	}

	if (session->inv_session) {
		pjsip_dlg_dec_session(session->inv_session->dlg, &session_module);
	}
}

static int add_supplements(struct ast_sip_session *session)
{
	struct ast_sip_session_supplement *iter;
	SCOPED_LOCK(lock, &session_supplements, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE(&session_supplements, iter, next) {
		struct ast_sip_session_supplement *copy = supplement_dup(iter);
		if (!copy) {
			return -1;
		}
		AST_LIST_INSERT_TAIL(&session->supplements, copy, next);
	}
	return 0;
}

static int add_session_media(void *obj, void *arg, int flags)
{
	struct sdp_handler_list *handler_list = obj;
	struct ast_sip_session * session = arg;
	RAII_VAR(struct ast_sip_session_media *, session_media, NULL, ao2_cleanup);
	session_media = ao2_alloc(sizeof(*session_media) + strlen(handler_list->stream_type), session_media_dtor);
	if (!session_media) {
		return CMP_STOP;
	}
	session_media->encryption = session->endpoint->media.rtp.encryption;
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
	struct ast_sip_contact *contact, pjsip_inv_session *inv_session)
{
	RAII_VAR(struct ast_sip_session *, session, ao2_alloc(sizeof(*session), session_destructor), ao2_cleanup);
	struct ast_sip_session_supplement *iter;
	int dsp_features = 0;
	if (!session) {
		return NULL;
	}
	AST_LIST_HEAD_INIT(&session->supplements);
	session->datastores = ao2_container_alloc(DATASTORE_BUCKETS, datastore_hash, datastore_cmp);
	if (!session->datastores) {
		return NULL;
	}

	session->endpoint = ao2_bump(endpoint);

	session->media = ao2_container_alloc(MEDIA_BUCKETS, session_media_hash, session_media_cmp);
	if (!session->media) {
		return NULL;
	}
	/* fill session->media with available types */
	ao2_callback(sdp_handlers, OBJ_NODATA, add_session_media, session);

	session->serializer = ast_sip_create_serializer();
	if (!session->serializer) {
		return NULL;
	}
	ast_sip_dialog_set_serializer(inv_session->dlg, session->serializer);
	ast_sip_dialog_set_endpoint(inv_session->dlg, endpoint);
	pjsip_dlg_inc_session(inv_session->dlg, &session_module);
	inv_session->mod_data[session_module.id] = ao2_bump(session);
	session->contact = ao2_bump(contact);
	session->inv_session = inv_session;
	session->req_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	if (endpoint->dtmf == AST_SIP_DTMF_INBAND) {
		dsp_features |= DSP_FEATURE_DIGIT_DETECT;
	}

	if (endpoint->faxdetect) {
		dsp_features |= DSP_FEATURE_FAX_DETECT;
	}

	if (dsp_features) {
		if (!(session->dsp = ast_dsp_new())) {
			ao2_ref(session, -1);
			return NULL;
		}

		ast_dsp_set_features(session->dsp, dsp_features);
	}

	if (add_supplements(session)) {
		ao2_ref(session, -1);
		return NULL;
	}
	AST_LIST_TRAVERSE(&session->supplements, iter, next) {
		if (iter->session_begin) {
			iter->session_begin(session);
		}
	}
	session->direct_media_cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	AST_LIST_HEAD_INIT_NOLOCK(&session->delayed_requests);
	ast_party_id_init(&session->id);
	ao2_ref(session, +1);
	return session;
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

	/* Wait for the the serializer suspension to be completed. */
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
}

static int session_outbound_auth(pjsip_dialog *dlg, pjsip_tx_data *tdata, void *user_data)
{
	pjsip_inv_session *inv = pjsip_dlg_get_inv_session(dlg);
	struct ast_sip_session *session = inv->mod_data[session_module.id];

	if (inv->state < PJSIP_INV_STATE_CONFIRMED && tdata->msg->line.req.method.id == PJSIP_INVITE_METHOD) {
		pjsip_inv_uac_restart(inv, PJ_FALSE);
	}
	ast_sip_session_send_request(session, tdata);
	return 0;
}

struct ast_sip_session *ast_sip_session_create_outgoing(struct ast_sip_endpoint *endpoint,
	struct ast_sip_contact *contact, const char *location, const char *request_user,
	struct ast_format_cap *req_caps)
{
	const char *uri = NULL;
	RAII_VAR(struct ast_sip_contact *, found_contact, NULL, ao2_cleanup);
	pjsip_timer_setting timer;
	pjsip_dialog *dlg;
	struct pjsip_inv_session *inv_session;
	RAII_VAR(struct ast_sip_session *, session, NULL, ao2_cleanup);

	/* If no location has been provided use the AOR list from the endpoint itself */
	if (location || !contact) {
		location = S_OR(location, endpoint->aors);

		found_contact = ast_sip_location_retrieve_contact_from_aor_list(location);
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
		return NULL;
	}

	if (!(dlg = ast_sip_create_dialog_uac(endpoint, uri, request_user))) {
		return NULL;
	}

	if (ast_sip_dialog_setup_outbound_authentication(dlg, endpoint, session_outbound_auth, NULL)) {
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

	if (!(session = ast_sip_session_alloc(endpoint, found_contact ? found_contact : contact, inv_session))) {
		pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		return NULL;
	}
	ast_party_id_copy(&session->id, &endpoint->id.self);

	if (ast_format_cap_count(req_caps)) {
		/* get joint caps between req_caps and endpoint caps */
		struct ast_format_cap *joint_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		ast_format_cap_get_compatible(req_caps, session->endpoint->media.codecs, joint_caps);

		/* if joint caps */
		if (ast_format_cap_count(joint_caps)) {
			/* copy endpoint caps into session->req_caps */
			ast_format_cap_append_from_cap(session->req_caps, session->endpoint->media.codecs, AST_MEDIA_TYPE_UNKNOWN);
			/* replace instances of joint caps equivalents in session->req_caps */
			ast_format_cap_replace_from_cap(session->req_caps, joint_caps, AST_MEDIA_TYPE_UNKNOWN);
		}
		ao2_cleanup(joint_caps);
	}

	if ((pjsip_dlg_add_usage(dlg, &session_module, NULL) != PJ_SUCCESS)) {
		pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		/* Since we are not notifying ourselves that the INVITE session is being terminated
		 * we need to manually drop its reference to session
		 */
		ao2_ref(session, -1);
		return NULL;
	}

	ao2_ref(session, +1);
	return session;
}

static int session_termination_task(void *data)
{
	RAII_VAR(struct ast_sip_session *, session, data, ao2_cleanup);
	pjsip_tx_data *packet = NULL;

	if (!session->inv_session) {
		return 0;
	}

	if (pjsip_inv_end_session(session->inv_session, 603, NULL, &packet) == PJ_SUCCESS) {
		ast_sip_session_send_request(session, packet);
	}

	return 0;
}

static void session_termination_cb(pj_timer_heap_t *timer_heap, struct pj_timer_entry *entry)
{
	struct ast_sip_session *session = entry->user_data;

	if (ast_sip_push_task(session->serializer, session_termination_task, session)) {
		ao2_cleanup(session);
	}
}

void ast_sip_session_defer_termination(struct ast_sip_session *session)
{
	pj_time_val delay = { .sec = 60, };

	session->defer_terminate = 1;

	session->scheduled_termination.id = 0;
	ao2_ref(session, +1);
	session->scheduled_termination.user_data = session;
	session->scheduled_termination.cb = session_termination_cb;

	if (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(), &session->scheduled_termination, &delay) != PJ_SUCCESS) {
		ao2_ref(session, -1);
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
		return SIP_GET_DEST_EXTEN_FOUND;
	}
	/* XXX In reality, we'll likely have further options so that partial matches
	 * can be indicated here, but for getting something up and running, we're going
	 * to return a "not exists" error here.
	 */
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
			pjsip_endpt_send_response2(ast_sip_get_pjsip_endpoint(), rdata, tdata, NULL, NULL);
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

static void new_invite_destroy(void *obj)
{
	struct new_invite *invite = obj;

	ao2_cleanup(invite->session);

	if (invite->rdata) {
		pjsip_rx_data_free_cloned(invite->rdata);
	}
}

static struct new_invite *new_invite_alloc(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	struct new_invite *invite = ao2_alloc(sizeof(*invite), new_invite_destroy);

	if (!invite) {
		return NULL;
	}

	ao2_ref(session, +1);
	invite->session = session;

	if (pjsip_rx_data_clone(rdata, 0, &invite->rdata) != PJ_SUCCESS) {
		ao2_ref(invite, -1);
		return NULL;
	}

	return invite;
}

static int new_invite(void *data)
{
	RAII_VAR(struct new_invite *, invite, data, ao2_cleanup);
	pjsip_tx_data *tdata = NULL;
	pjsip_timer_setting timer;
	pjsip_rdata_sdp_info *sdp_info;
	pjmedia_sdp_session *local = NULL;

	/* From this point on, any calls to pjsip_inv_terminate have the last argument as PJ_TRUE
	 * so that we will be notified so we can destroy the session properly
	 */

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
		return 0;
	case SIP_GET_DEST_EXTEN_NOT_FOUND:
	case SIP_GET_DEST_EXTEN_PARTIAL:
	default:
		ast_log(LOG_NOTICE, "Call from '%s' (%s:%s:%d) to extension '%s' rejected because extension not found in context '%s'.\n",
			ast_sorcery_object_get_id(invite->session->endpoint), invite->rdata->tp_info.transport->type_name, invite->rdata->pkt_info.src_name,
			invite->rdata->pkt_info.src_port, invite->session->exten, invite->session->endpoint->context);

		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 404, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 404, PJ_TRUE);
		}
		return 0;
	};

	if ((sdp_info = pjsip_rdata_get_sdp_info(invite->rdata)) && (sdp_info->sdp_err == PJ_SUCCESS) && sdp_info->sdp) {
		if (handle_incoming_sdp(invite->session, sdp_info->sdp)) {
			if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 488, NULL, NULL, &tdata) == PJ_SUCCESS) {
				ast_sip_session_send_response(invite->session, tdata);
			} else  {
				pjsip_inv_terminate(invite->session->inv_session, 488, PJ_TRUE);
			}
			return 0;
		}
		/* We are creating a local SDP which is an answer to their offer */
		local = create_local_sdp(invite->session->inv_session, invite->session, sdp_info->sdp);
	} else {
		/* We are creating a local SDP which is an offer */
		local = create_local_sdp(invite->session->inv_session, invite->session, NULL);
	}

	/* If we were unable to create a local SDP terminate the session early, it won't go anywhere */
	if (!local) {
		if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 500, NULL, NULL, &tdata) == PJ_SUCCESS) {
			ast_sip_session_send_response(invite->session, tdata);
		} else  {
			pjsip_inv_terminate(invite->session->inv_session, 500, PJ_TRUE);
		}
		return 0;
	} else {
		pjsip_inv_set_local_sdp(invite->session->inv_session, local);
		pjmedia_sdp_neg_set_prefer_remote_codec_order(invite->session->inv_session->neg, PJ_FALSE);
#ifdef PJMEDIA_SDP_NEG_ANSWER_MULTIPLE_CODECS
		pjmedia_sdp_neg_set_answer_multiple_codecs(invite->session->inv_session->neg, PJ_TRUE);
#endif
	}

	pjsip_timer_setting_default(&timer);
	timer.min_se = invite->session->endpoint->extensions.timer.min_se;
	timer.sess_expires = invite->session->endpoint->extensions.timer.sess_expires;
	pjsip_timer_init_session(invite->session->inv_session, &timer);

	/* At this point, we've verified what we can, so let's go ahead and send a 100 Trying out */
	if (pjsip_inv_initial_answer(invite->session->inv_session, invite->rdata, 100, NULL, NULL, &tdata) != PJ_SUCCESS) {
		pjsip_inv_terminate(invite->session->inv_session, 500, PJ_TRUE);
		return 0;
	}
	ast_sip_session_send_response(invite->session, tdata);

	handle_incoming_request(invite->session, invite->rdata, PJSIP_EVENT_RX_MSG);

	return 0;
}

static void handle_new_invite_request(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint,
			ast_pjsip_rdata_get_endpoint(rdata), ao2_cleanup);
	pjsip_tx_data *tdata = NULL;
	pjsip_inv_session *inv_session = NULL;
	RAII_VAR(struct ast_sip_session *, session, NULL, ao2_cleanup);
	struct new_invite *invite;

	ast_assert(endpoint != NULL);

	inv_session = pre_session_setup(rdata, endpoint);
	if (!inv_session) {
		/* pre_session_setup() returns a response on failure */
		return;
	}

	session = ast_sip_session_alloc(endpoint, NULL, inv_session);
	if (!session) {
		if (pjsip_inv_initial_answer(inv_session, rdata, 500, NULL, NULL, &tdata) == PJ_SUCCESS) {
			pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		} else {
			pjsip_inv_send_msg(inv_session, tdata);
		}
		return;
	}

	invite = new_invite_alloc(session, rdata);
	if (!invite || ast_sip_push_task(session->serializer, new_invite, invite)) {
		if (pjsip_inv_initial_answer(inv_session, rdata, 500, NULL, NULL, &tdata) == PJ_SUCCESS) {
			pjsip_inv_terminate(inv_session, 500, PJ_FALSE);
		} else {
			pjsip_inv_send_msg(inv_session, tdata);
		}
		ao2_ref(session, -1);
		ao2_cleanup(invite);
		return;
	}
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
 * In all cases, there is very little we actually do in this function
 * 1) For requests we don't handle, we return PJ_FALSE
 * 2) For new INVITEs, throw the work into the SIP threadpool to be done
 *    there to free up the thread(s) handling incoming requests
 * 3) For in-dialog requests we handle, we defer handling them until the
 *    on_inv_state_change() callback instead (where we will end up putting
 *    them into the threadpool).
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

struct reschedule_reinvite_data {
	struct ast_sip_session *session;
	struct ast_sip_session_delayed_request *delay;
};

static struct reschedule_reinvite_data *reschedule_reinvite_data_alloc(
		struct ast_sip_session *session, struct ast_sip_session_delayed_request *delay)
{
	struct reschedule_reinvite_data *rrd = ast_malloc(sizeof(*rrd));
	if (!rrd) {
		return NULL;
	}
	ao2_ref(session, +1);
	rrd->session = session;
	rrd->delay = delay;
	return rrd;
}

static void reschedule_reinvite_data_destroy(struct reschedule_reinvite_data *rrd)
{
	ao2_cleanup(rrd->session);
	ast_free(rrd->delay);
	ast_free(rrd);
}

static int really_resend_reinvite(void *data)
{
	RAII_VAR(struct reschedule_reinvite_data *, rrd, data, reschedule_reinvite_data_destroy);

	return send_delayed_request(rrd->session, rrd->delay);
}

static void resend_reinvite(pj_timer_heap_t *timer, pj_timer_entry *entry)
{
	struct reschedule_reinvite_data *rrd = entry->user_data;

	ast_sip_push_task(rrd->session->serializer, really_resend_reinvite, entry->user_data);
}

static void reschedule_reinvite(struct ast_sip_session *session, ast_sip_session_response_cb on_response)
{
	struct ast_sip_session_delayed_request *delay = delayed_request_alloc("INVITE",
			NULL, NULL, on_response, 1);
	pjsip_inv_session *inv = session->inv_session;
	struct reschedule_reinvite_data *rrd = reschedule_reinvite_data_alloc(session, delay);
	pj_time_val tv;

	if (!rrd || !delay) {
		return;
	}

	tv.sec = 0;
	if (inv->role == PJSIP_ROLE_UAC) {
		tv.msec = 2100 + ast_random() % 2000;
	} else {
		tv.msec = ast_random() % 2000;
	}

	pj_timer_entry_init(&session->rescheduled_reinvite, 0, rrd, resend_reinvite);

	pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(), &session->rescheduled_reinvite, &tv);
}

static void __print_debug_details(const char *function, pjsip_inv_session *inv, pjsip_transaction *tsx, pjsip_event *e)
{
	struct ast_sip_session *session;
	ast_debug(5, "Function %s called on event %s\n", function, pjsip_event_str(e->type));
	if (!inv) {
		ast_debug(5, "Transaction %p does not belong to an inv_session?\n", tsx);
		ast_debug(5, "The transaction state is %s\n", pjsip_tsx_state_str(tsx->state));
		return;
	}
	session = inv->mod_data[session_module.id];
	if (!session) {
		ast_debug(5, "inv_session %p has no ast session\n", inv);
	} else {
		ast_debug(5, "The state change pertains to the session with %s\n",
				ast_sorcery_object_get_id(session->endpoint));
	}
	if (inv->invite_tsx) {
		ast_debug(5, "The inv session still has an invite_tsx (%p)\n", inv->invite_tsx);
	} else {
		ast_debug(5, "The inv session does NOT have an invite_tsx\n");
	}
	if (tsx) {
		ast_debug(5, "The transaction involved in this state change is %p\n", tsx);
		ast_debug(5, "The current transaction state is %s\n", pjsip_tsx_state_str(tsx->state));
		ast_debug(5, "The transaction state change event is %s\n", pjsip_event_str(e->body.tsx_state.type));
	} else {
		ast_debug(5, "There is no transaction involved in this state change\n");
	}
	ast_debug(5, "The current inv state is %s\n", pjsip_inv_state_name(inv->state));
}

#define print_debug_details(inv, tsx, e) __print_debug_details(__PRETTY_FUNCTION__, (inv), (tsx), (e))

static void handle_incoming_request(struct ast_sip_session *session, pjsip_rx_data *rdata, pjsip_event_id_e type)
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

static void handle_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata, pjsip_event_id_e type,
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

static int handle_incoming(struct ast_sip_session *session, pjsip_rx_data *rdata, pjsip_event_id_e type,
		enum ast_sip_session_response_priority response_priority)
{
	ast_debug(3, "Received %s\n", rdata->msg_info.msg->type == PJSIP_REQUEST_MSG ?
			"request" : "response");

	if (rdata->msg_info.msg->type == PJSIP_REQUEST_MSG) {
		handle_incoming_request(session, rdata, type);
	} else {
		handle_incoming_response(session, rdata, type, response_priority);
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
	ast_debug(3, "Method is %.*s, Response is %d %.*s\n", (int) pj_strlen(&cseq->method.name),
		pj_strbuf(&cseq->method.name), status.code, (int) pj_strlen(&status.reason),
		pj_strbuf(&status.reason));

	AST_LIST_TRAVERSE(&session->supplements, supplement, next) {
		if (supplement->outgoing_response && does_method_match(&cseq->method.name, supplement->method)) {
			supplement->outgoing_response(session, tdata);
		}
	}
}

static void handle_outgoing(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	ast_debug(3, "Sending %s\n", tdata->msg->type == PJSIP_REQUEST_MSG ?
			"request" : "response");
	if (tdata->msg->type == PJSIP_REQUEST_MSG) {
		handle_outgoing_request(session, tdata);
	} else {
		handle_outgoing_response(session, tdata);
	}
}

static int session_end(struct ast_sip_session *session)
{
	struct ast_sip_session_supplement *iter;

	/* Stop the scheduled termination */
	if (pj_timer_heap_cancel(pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()), &session->scheduled_termination)) {
		ao2_ref(session, -1);
	}

	/* Session is dead. Let's get rid of the reference to the session */
	AST_LIST_TRAVERSE(&session->supplements, iter, next) {
		if (iter->session_end) {
			iter->session_end(session);
		}
	}

	session->inv_session->mod_data[session_module.id] = NULL;
	ast_sip_dialog_set_serializer(session->inv_session->dlg, NULL);
	ast_sip_dialog_set_endpoint(session->inv_session->dlg, NULL);
	ao2_cleanup(session);
	return 0;
}

static void session_inv_on_state_changed(pjsip_inv_session *inv, pjsip_event *e)
{
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	pjsip_event_id_e type;

	if (e) {
		print_debug_details(inv, NULL, e);
		type = e->type;
	} else {
		type = PJSIP_EVENT_UNKNOWN;
	}

	if (!session) {
		return;
	}

	switch(type) {
	case PJSIP_EVENT_TX_MSG:
		handle_outgoing(session, e->body.tx_msg.tdata);
		break;
	case PJSIP_EVENT_RX_MSG:
		handle_incoming(session, e->body.rx_msg.rdata, type,
				AST_SIP_SESSION_BEFORE_MEDIA);
		break;
	case PJSIP_EVENT_TSX_STATE:
		ast_debug(3, "Source of transaction state change is %s\n", pjsip_event_str(e->body.tsx_state.type));
		/* Transaction state changes are prompted by some other underlying event. */
		switch(e->body.tsx_state.type) {
		case PJSIP_EVENT_TX_MSG:
			handle_outgoing(session, e->body.tsx_state.src.tdata);
			break;
		case PJSIP_EVENT_RX_MSG:
			handle_incoming(session, e->body.tsx_state.src.rdata, type,
					AST_SIP_SESSION_BEFORE_MEDIA);
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
		session_end(session);
	}
}

static void session_inv_on_new_session(pjsip_inv_session *inv, pjsip_event *e)
{
	/* XXX STUB */
}

static void session_inv_on_tsx_state_changed(pjsip_inv_session *inv, pjsip_transaction *tsx, pjsip_event *e)
{
	ast_sip_session_response_cb cb;
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	print_debug_details(inv, tsx, e);
	if (!session) {
		/* Transaction likely timed out after the call was hung up. Just
		 * ignore such transaction changes
		 */
		return;
	}
	switch (e->body.tsx_state.type) {
	case PJSIP_EVENT_TX_MSG:
		handle_outgoing(session, e->body.tsx_state.src.tdata);
		/* When we create an outgoing request, we do not have access to the transaction that
		 * is created. Instead, We have to place transaction-specific data in the tdata. Here,
		 * we transfer the data into the transaction. This way, when we receive a response, we
		 * can dig this data out again
		 */
		tsx->mod_data[session_module.id] = e->body.tsx_state.src.tdata->mod_data[session_module.id];
		break;
	case PJSIP_EVENT_RX_MSG:
		cb = ast_sip_mod_data_get(tsx->mod_data, session_module.id, MOD_DATA_ON_RESPONSE);
		handle_incoming(session, e->body.tsx_state.src.rdata, e->type,
				AST_SIP_SESSION_AFTER_MEDIA);
		if (tsx->method.id == PJSIP_INVITE_METHOD) {
			if (tsx->role == PJSIP_ROLE_UAC) {
				if (tsx->state == PJSIP_TSX_STATE_COMPLETED) {
					/* This means we got a non 2XX final response to our outgoing INVITE */
					if (tsx->status_code == PJSIP_SC_REQUEST_PENDING) {
						reschedule_reinvite(session, cb);
						return;
					} else if (inv->state == PJSIP_INV_STATE_CONFIRMED &&
						   tsx->status_code != 488) {
						/* Other reinvite failures (except 488) result in destroying the session. */
						pjsip_tx_data *tdata;
						if (pjsip_inv_end_session(inv, 500, NULL, &tdata) == PJ_SUCCESS) {
							ast_sip_session_send_request(session, tdata);
						}
					}
				} else if (tsx->state == PJSIP_TSX_STATE_TERMINATED) {
					if (inv->cancelling && tsx->status_code == PJSIP_SC_OK) {
						/* This is a race condition detailed in RFC 5407 section 3.1.2.
						 * We sent a CANCEL at the same time that the UAS sent us a 200 OK for
						 * the original INVITE. As a result, we have now received a 200 OK for
						 * a cancelled call. Our role is to immediately send a BYE to end the
						 * dialog.
						 */
						pjsip_tx_data *tdata;

						if (pjsip_inv_end_session(inv, 500, NULL, &tdata) == PJ_SUCCESS) {
							ast_sip_session_send_request(session, tdata);
						}
					}
				}
			}
		}
		if (cb) {
			cb(session, e->body.tsx_state.src.rdata);
		}
	case PJSIP_EVENT_TRANSPORT_ERROR:
	case PJSIP_EVENT_TIMER:
	case PJSIP_EVENT_USER:
	case PJSIP_EVENT_UNKNOWN:
	case PJSIP_EVENT_TSX_STATE:
		/* Inception? */
		break;
	}

	/* Terminated INVITE transactions always should result in queuing delayed requests,
	 * no matter what event caused the transaction to terminate
	 */
	if (tsx->method.id == PJSIP_INVITE_METHOD &&
		((tsx->state == PJSIP_TSX_STATE_TERMINATED) ||
		(tsx->state == PJSIP_TSX_STATE_PROCEEDING))) {
		queue_delayed_request(session);
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
		/* if an already assigned handler does not handle the session_media or reports a catastrophic error, fail */
		res = handler->create_outgoing_sdp_stream(session, session_media, answer);
		if (res <= 0) {
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

	if (!(local = PJ_POOL_ZALLOC_T(inv->pool_prov, pjmedia_sdp_session))) {
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
			pj_sockaddr localaddr;
			char our_ip[PJ_INET6_ADDRSTRLEN];

			pj_gethostip(session->endpoint->media.rtp.ipv6 ? pj_AF_INET6() : pj_AF_INET(), &localaddr);
			pj_sockaddr_print(&localaddr, our_ip, sizeof(our_ip), 0);
			pj_strdup2(inv->pool_prov, &local->origin.addr, our_ip);
		}
	}

	return local;
}

static void session_inv_on_rx_offer(pjsip_inv_session *inv, const pjmedia_sdp_session *offer)
{
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	pjmedia_sdp_session *answer;

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
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	const pjmedia_sdp_session *local, *remote;

	if (!session->channel) {
		/* If we don't have a channel. We really don't care about media updates.
		 * Just ignore
		 */
		return;
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
	struct ast_sip_session *session = inv->mod_data[session_module.id];
	const pjsip_sip_uri *uri;

	if (session->endpoint->redirect_method == AST_SIP_REDIRECT_URI_PJSIP) {
		return PJSIP_REDIRECT_ACCEPT;
	}

	if (!PJSIP_URI_SCHEME_IS_SIP(target) && !PJSIP_URI_SCHEME_IS_SIPS(target)) {
		return PJSIP_REDIRECT_STOP;
	}

	handle_incoming(session, e->body.rx_msg.rdata, PJSIP_EVENT_RX_MSG,
			AST_SIP_SESSION_BEFORE_REDIRECTING);

	uri = pjsip_uri_get_uri(target);

	if (session->endpoint->redirect_method == AST_SIP_REDIRECT_USER) {
		char exten[AST_MAX_EXTENSION];

		ast_copy_pj_str(exten, &uri->user, sizeof(exten));
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
	struct ast_sip_nat_hook *hook = ast_sip_mod_data_get(
		tdata->mod_data, session_module.id, MOD_DATA_NAT_HOOK);
	struct pjmedia_sdp_session *sdp;
	int stream;

	/* SDP produced by us directly will never be multipart */
	if (hook || !tdata->msg->body || pj_stricmp2(&tdata->msg->body->content_type.type, "application") ||
		pj_stricmp2(&tdata->msg->body->content_type.subtype, "sdp") || ast_strlen_zero(transport->external_media_address)) {
		return;
	}

	sdp = tdata->msg->body->data;

	if (sdp->conn) {
		char host[NI_MAXHOST];
		struct ast_sockaddr addr = { { 0, } };

		ast_copy_pj_str(host, &sdp->conn->addr, sizeof(host));
		ast_sockaddr_parse(&addr, host, PARSE_PORT_FORBID);

		if (ast_apply_ha(transport->localnet, &addr) != AST_SENSE_ALLOW) {
			pj_strdup2(tdata->pool, &sdp->conn->addr, transport->external_media_address);
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

	ast_module_ref(ast_module_info->self);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	/* This will never get called as this module can't be unloaded */
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "PJSIP Session resource",
		.support_level = AST_MODULE_SUPPORT_CORE,
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
