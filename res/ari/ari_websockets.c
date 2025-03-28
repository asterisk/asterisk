/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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

#include "resource_events.h"
#include "ari_websockets.h"
#include "internal.h"
#include "ari_model_validators.h"
#include "asterisk/app.h"
#include "asterisk/ari.h"
#include "asterisk/astobj2.h"
#include "asterisk/http_websocket.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_app.h"
#include "asterisk/time.h"
#include "asterisk/uuid.h"
#include "asterisk/vector.h"
#include "asterisk/websocket_client.h"


/*! \file
 *
 * \brief WebSocket support for RESTful API's.
 * \author David M. Lee, II <dlee@digium.com>
 */

/*! Number of buckets for the ari_ws_session registry. Remember to keep it a prime number! */
#define SESSION_REGISTRY_NUM_BUCKETS 23

/*! Initial size of websocket session apps vector */
#define APPS_INIT_SIZE 7

/*! Initial size of the websocket session message queue. */
#define MESSAGES_INIT_SIZE 23

#define ARI_CONTEXT_REGISTRAR "res_ari"

/*! \brief Local registry for created \ref ari_ws_session objects. */
static struct ao2_container *session_registry;

struct ast_websocket_server *ast_ws_server;

#if defined(AST_DEVMODE)
	ari_validator ari_validate_message_fn = ast_ari_validate_message;
#else
	/*!
	 * \brief Validator that always succeeds.
	 */
	static int null_validator(struct ast_json *json)
	{
		return 1;
	}

	ari_validator ari_validate_message_fn = null_validator;
#endif


#define VALIDATION_FAILED				\
	"{"						\
	"  \"error\": \"InvalidMessage\","		\
	"  \"message\": \"Message validation failed\""	\
	"}"

static int session_write(struct ari_ws_session *session, struct ast_json *message)
{
	RAII_VAR(char *, str, NULL, ast_json_free);

	if (!session || !session->ast_ws_session || !message) {
		return -1;
	}

#ifdef AST_DEVMODE
	if (!session->validator(message)) {
		ast_log(LOG_ERROR, "Outgoing message failed validation\n");
		return ast_websocket_write_string(session->ast_ws_session, VALIDATION_FAILED);
	}
#endif

	str = ast_json_dump_string_format(message, ast_ari_json_format());

	if (str == NULL) {
		ast_log(LOG_ERROR, "Failed to encode JSON object\n");
		return -1;
	}

	if (ast_websocket_write_string(session->ast_ws_session, str)) {
		ast_log(LOG_NOTICE, "Problem occurred during websocket write to %s, websocket closed\n",
			ast_sockaddr_stringify(ast_websocket_remote_address(session->ast_ws_session)));
		return -1;
	}
	return 0;
}

static void session_send_or_queue(struct ari_ws_session *session,
	struct ast_json *message, const char *msg_type, const char *app_name,
	int debug_app)
{
	const char *msg_timestamp, *msg_ast_id;

	msg_timestamp = S_OR(
		ast_json_string_get(ast_json_object_get(message, "timestamp")), "");
	if (ast_strlen_zero(msg_timestamp)) {
		if (ast_json_object_set(message, "timestamp", ast_json_timeval(ast_tvnow(), NULL))) {
			ast_log(LOG_ERROR,
				"%s: Failed to dispatch '%s' message from Stasis app '%s'; could not update message\n",
				session->remote_addr, msg_type, app_name);
			return;
		}
	}

	msg_ast_id = S_OR(
		ast_json_string_get(ast_json_object_get(message, "asterisk_id")), "");
	if (ast_strlen_zero(msg_ast_id)) {
		char eid[20];

		if (ast_json_object_set(message, "asterisk_id",
			ast_json_string_create(ast_eid_to_str(eid, sizeof(eid), &ast_eid_default)))) {
			ao2_unlock(session);
			ast_log(LOG_ERROR,
				"%s: Failed to dispatch '%s' message from Stasis app '%s'; could not update message\n",
				session->remote_addr, msg_type, app_name);
		}
	}

	if (!session->ast_ws_session) {
		/* If the websocket is NULL, the message goes to the queue */
		if (AST_VECTOR_APPEND(&session->message_queue, message) == 0) {
			ast_json_ref(message);
		}
		/*
		 * If the msg_type one of the Application* types, the websocket
		 * might not be there yet so don't log.
		 */
		if (!ast_begins_with(msg_type, "Application")) {
			ast_log(LOG_WARNING,
					"%s: Queued '%s' message for Stasis app '%s'; websocket is not ready\n",
					session->remote_addr,
					msg_type,
					app_name);
		}
	} else {

		if (DEBUG_ATLEAST(4) || debug_app) {
			char *str = ast_json_dump_string_format(message, AST_JSON_PRETTY);

			ast_verbose("<--- Sending ARI event to %s --->\n%s\n",
				session->remote_addr,
				str);
			ast_json_free(str);
		}
		session_write(session, message);
	}
}

static void session_send_app_event(struct ari_ws_session *session,
	const char *event_type, const char *app_name)
{
	char eid[20];
	int debug_app = stasis_app_get_debug_by_name(app_name);
	struct ast_json *msg = ast_json_pack("{s:s, s:o?, s:s, s:s }",
		"type", event_type,
		"timestamp", ast_json_timeval(ast_tvnow(), NULL),
		"application", app_name,
		"asterisk_id", ast_eid_to_str(eid, sizeof(eid), &ast_eid_default));

	if (!msg) {
		return;
	}
	ast_debug(3, "%s: Sending '%s' event to app '%s'\n", session->session_id,
		event_type, app_name);
	/*
	 * We don't want to use ari_websocket_send_event() here because
	 * the app may be unregistered which will cause stasis_app_event_allowed
	 * to return false.
	 */
	session_send_or_queue(session, msg, event_type, app_name, debug_app);
	ast_json_unref(msg);
}

static struct ast_json *session_read(struct ari_ws_session *session)
{
	RAII_VAR(struct ast_json *, message, NULL, ast_json_unref);

	if (!session || !session->ast_ws_session) {
		return NULL;
	}
	if (ast_websocket_fd(session->ast_ws_session) < 0) {
		return NULL;
	}

	while (!message) {
		int res;
		char *payload;
		uint64_t payload_len;
		enum ast_websocket_opcode opcode;
		int fragmented;

		res = ast_wait_for_input(
			ast_websocket_fd(session->ast_ws_session), -1);

		if (res <= 0) {
			ast_log(LOG_WARNING, "WebSocket poll error: %s\n",
				strerror(errno));
			return NULL;
		}

		res = ast_websocket_read(session->ast_ws_session, &payload,
			&payload_len, &opcode, &fragmented);

		if (res != 0) {
			ast_log(LOG_WARNING, "WebSocket read error: %s\n",
				strerror(errno));
			return NULL;
		}

		switch (opcode) {
		case AST_WEBSOCKET_OPCODE_CLOSE:
			ast_debug(1, "WebSocket closed\n");
			return NULL;
		case AST_WEBSOCKET_OPCODE_TEXT:
			message = ast_json_load_buf(payload, payload_len, NULL);
			if (message == NULL) {
				struct ast_json *error = ast_json_pack(
					"{s:s, s:s, s:s, s:i, s:s, s:s }",
					"type", "RESTResponse",
					"transaction_id", "",
					"request_id", "",
					"status_code", 400,
					"reason_phrase", "Failed to parse request message JSON",
					"uri", ""
					);
				ari_websocket_send_event(session, session->app_name,
					error, 0);
				ast_json_unref(error);
				ast_log(LOG_WARNING,
					"WebSocket input failed to parse\n");

			}

			break;
		default:
			/* Ignore all other message types */
			break;
		}
	}

	return ast_json_ref(message);
}

void ari_handle_websocket(
	struct ast_tcptls_session_instance *ser, const char *uri,
	enum ast_http_method method, struct ast_variable *get_params,
	struct ast_variable *headers)
{
	struct ast_http_uri fake_urih = {
		.data = ast_ws_server,
	};

	ast_websocket_uri_cb(ser, &fake_urih, uri, method, get_params,
		headers);
}

/*!
 * \brief Callback handler for Stasis application messages.
 *
 * \internal
 *
 * \param data      Void pointer to the event session (\ref event_session).
 * \param app_name  Name of the Stasis application that dispatched the message.
 * \param message   The dispatched message.
 * \param debug_app Debug flag for the application.
 */
void ari_websocket_send_event(struct ari_ws_session *session,
	const char *app_name, struct ast_json *message, int debug_app)
{
	char *remote_addr = session->ast_ws_session ? ast_sockaddr_stringify(
			ast_websocket_remote_address(session->ast_ws_session)) : "";
	const char *msg_type, *msg_application;
	SCOPE_ENTER(4, "%s: Dispatching message from Stasis app '%s'\n", remote_addr, app_name);

	ast_assert(session != NULL);

	ao2_lock(session);

	msg_type = S_OR(ast_json_string_get(ast_json_object_get(message, "type")), "");
	msg_application = S_OR(
		ast_json_string_get(ast_json_object_get(message, "application")), app_name);

	/* If we've been replaced, remove the application from our local
	   websocket_apps container */
	if (session->type == AST_WS_TYPE_INBOUND
		&& strcmp(msg_type, "ApplicationReplaced") == 0 &&
		strcmp(msg_application, app_name) == 0) {
		AST_VECTOR_REMOVE_CMP_ORDERED(&session->websocket_apps,
			app_name, ast_strings_equal, ast_free_ptr);
	}

	/* Now, we need to determine our state to see how we will handle the message */
	if (ast_json_object_set(message, "application", ast_json_string_create(app_name))) {
		ao2_unlock(session);
		SCOPE_EXIT_LOG_RTN(LOG_WARNING,
			"%s: Failed to dispatch '%s' message from Stasis app '%s'; could not update message\n",
			remote_addr, msg_type, msg_application);
	}

	if (stasis_app_event_allowed(app_name, message)) {
		session_send_or_queue(session, message, msg_type,
			app_name, debug_app);
	}

	if (session->type == AST_WS_TYPE_CLIENT_PER_CALL
		&& !ast_strlen_zero(session->channel_id)
		&& ast_strings_equal(msg_type, "StasisEnd")) {
		struct ast_json *chan = ast_json_object_get(message, "channel");
		struct ast_json *id_obj = ast_json_object_get(chan, "id");
		const char *id = ast_json_string_get(id_obj);
		if (!ast_strlen_zero(id)
			&& ast_strings_equal(id, session->channel_id)) {
			ast_debug(3, "%s: StasisEnd message sent for channel '%s'\n",
				remote_addr, id);
			session->stasis_end_sent = 1;
		}
	}
	ao2_unlock(session);
	SCOPE_EXIT("%s: Dispatched '%s' message from Stasis app '%s'\n",
		remote_addr, msg_type, app_name);
}

static void stasis_app_message_handler(void *data, const char *app_name,
	struct ast_json *message)
{
	int debug_app = stasis_app_get_debug_by_name(app_name);
	struct ari_ws_session *session = data;

	if (!session) {
		ast_debug(3, "Stasis app '%s' message handler called with NULL session.  OK for per_call_config websocket.\n",
			app_name);
		return;
	}

	ari_websocket_send_event(session, app_name, message, debug_app);
}

static void session_unref(struct ari_ws_session *session)
{
	if (!session) {
		return;
	}
	ast_debug(4, "%s: Unreffing ARI websocket session\n", session->session_id);
	ao2_ref(session, -1);
}

static void session_unregister_app_cb(char *app_name, struct ari_ws_session *session)
{
	ast_debug(3, "%s: Trying to unregister app '%s'\n",
		session->session_id, app_name);
	if (session->type == AST_WS_TYPE_CLIENT_PER_CALL_CONFIG) {
		char context_name[AST_MAX_CONTEXT + 1];
		sprintf(context_name, "%s%s", STASIS_CONTEXT_PREFIX, app_name);
		ast_debug(3, "%s: Unregistering context '%s' for app '%s'\n",
			session->session_id, context_name, app_name);
		ast_context_destroy_by_name(context_name, ARI_CONTEXT_REGISTRAR);
	} else {
		ast_debug(3, "%s: Unregistering stasis app '%s' and unsubscribing from all events.\n",
			session->session_id, app_name);
		stasis_app_unregister(app_name);
	}

	/*
	 * We don't send ApplicationUnregistered events for outbound per-call
	 * configs because there's no websocket to send them via or to
	 * inbound websockets because the websocket is probably closed already.
	 */
	if (!(session->type
		& (AST_WS_TYPE_CLIENT_PER_CALL_CONFIG | AST_WS_TYPE_INBOUND))) {
		session_send_app_event(session, "ApplicationUnregistered", app_name);
	}
}

static void session_unregister_apps(struct ari_ws_session *session)
{
	int app_count = (int)AST_VECTOR_SIZE(&session->websocket_apps);

	if (app_count == 0) {
		return;
	}
	ast_debug(3, "%s: Unregistering stasis apps.\n", session->session_id);

	AST_VECTOR_CALLBACK_VOID(&session->websocket_apps, session_unregister_app_cb,
		session);
	AST_VECTOR_RESET(&session->websocket_apps, ast_free_ptr);

	return;
}

static int session_register_apps(struct ari_ws_session *session,
	const char *_apps, int subscribe_all)
{
	char *apps = ast_strdupa(_apps);
	char *app_name;
	int app_counter = 0;

	ast_debug(3, "%s: Registering apps '%s'.  Subscribe all: %s\n",
		session->session_id, apps, subscribe_all ? "yes" : "no");

	while ((app_name = ast_strsep(&apps, ',', AST_STRSEP_STRIP))) {

		if (ast_strlen_zero(app_name)) {
			ast_log(LOG_WARNING, "%s: Invalid application name\n", session->session_id);
			return -1;
		}

		if (strlen(app_name) > ARI_MAX_APP_NAME_LEN) {
			ast_log(LOG_WARNING, "%s: Websocket app '%s' > %d characters\n",
				session->session_id, app_name, (int)ARI_MAX_APP_NAME_LEN);
			return -1;
		}

		if (session->type == AST_WS_TYPE_CLIENT_PER_CALL_CONFIG) {
			/*
			 * Outbound per-call configs only create a dialplan context.
			 * If they registered stasis apps there'd be no way for the
			 * Stasis dialplan app to know that it needs to start a
			 * per-call websocket connection.
			 */
			char context_name[AST_MAX_CONTEXT + 1];

			sprintf(context_name, "%s%s", STASIS_CONTEXT_PREFIX, app_name);
			if (!ast_context_find(context_name)) {
				if (!ast_context_find_or_create(NULL, NULL, context_name,
					ARI_CONTEXT_REGISTRAR)) {
					ast_log(LOG_WARNING, "%s: Could not create context '%s'\n",
						session->session_id, context_name);
					return -1;
				} else {
					ast_add_extension(context_name, 0, "_.", 1, NULL, NULL,
						"Stasis", ast_strdup(app_name), ast_free_ptr,
						ARI_CONTEXT_REGISTRAR);
					ast_add_extension(context_name, 0, "h", 1, NULL, NULL,
						"NoOp", NULL, NULL, ARI_CONTEXT_REGISTRAR);
				}
			} else {
				ast_debug(3, "%s: Context '%s' already exists\n", session->session_id,
					context_name);
			}
		} else {
			int already_registered = stasis_app_is_registered(app_name);
			int res = 0;

			if (subscribe_all) {
				res = stasis_app_register_all(app_name, stasis_app_message_handler,
					session);
			} else {
				res = stasis_app_register(app_name, stasis_app_message_handler,
					session);
			}

			if (res != 0) {
				return -1;
			}

			/*
			 * If there was an existing app by the same name, the register handler
			 * will have sent an ApplicationReplaced event.  If it's a new app, we
			 * send an ApplicationRegistered event.
			 *
			 * Except... There's no websocket to send it on for outbound per-call
			 * configs and inbound websockets don't need them because they aready
			 * know what apps they've registered for.
			 */
			if (!already_registered
				&& !(session->type & (AST_WS_TYPE_INBOUND | AST_WS_TYPE_CLIENT_PER_CALL_CONFIG))) {
				session_send_app_event(session, "ApplicationRegistered",
					app_name);
			}
		}

		if (AST_VECTOR_ADD_SORTED(&session->websocket_apps, ast_strdup(app_name), strcmp)) {
			ast_log(LOG_WARNING, "%s: Unable to add app '%s' to apps container\n",
				session->session_id, app_name);
			return -1;
		}

		app_counter++;
		if (app_counter == 1) {
			ast_free(session->app_name);
			session->app_name = ast_strdup(app_name);
			if (!session->app_name) {
				ast_log(LOG_WARNING, "%s: Unable to duplicate app name\n",
					session->session_id);
				return -1;
			}
		}
	}

	return 0;
}

/*
 * Websocket session cleanup is a bit complicated because it can be
 * in different states, it may or may not be in the registry container,
 * and stasis may be sending asynchronous events to it and some
 * stages of cleanup need to lock it.
 *
 * That's why there are 3 different cleanup functions.
 */

/*!
 * \internal
 * \brief Reset the ari_ws_session without destroying it.
 * It can't be reused and will be cleaned up by the caller.
 * This should only be called by session_create()
 * and session_cleanup().
 */
static void session_reset(struct ari_ws_session *session)
{
	SCOPED_AO2LOCK(lock, session);

	ast_debug(3, "%s: Resetting ARI websocket session\n",
		session->session_id);

	/* Clean up the websocket_apps container */
	if (AST_VECTOR_SIZE(&session->websocket_apps) > 0) {
		session_unregister_apps(session);
	}
	AST_VECTOR_RESET(&session->websocket_apps, ast_free_ptr);
	AST_VECTOR_FREE(&session->websocket_apps);

	AST_VECTOR_RESET(&session->message_queue, ast_json_unref);
	AST_VECTOR_FREE(&session->message_queue);
}

/*!
 * \internal
 * \brief RAII_VAR and container ari_ws_session cleanup function.
 * This unlinks the ari_ws_session from the registry and cleans up the
 * decrements the reference count.
 */
static void session_cleanup(struct ari_ws_session *session)
{
	if (!session) {
		return;
	}
	ast_debug(3, "%s: Cleaning up ARI websocket session RC: %d\n",
		session->session_id, (int)ao2_ref(session, 0));

	session_reset(session);

	if (session_registry) {
		ast_debug(3, "%s: Unlinking websocket session from registry RC: %d\n",
			session->session_id, (int)ao2_ref(session, 0));
		ao2_unlink(session_registry, session);
	}

	/*
	 * If this is a per-call config then its only reference
	 * was held by the registry container so we don't need
	 * to unref it here.
	 */
	if (session->type != AST_WS_TYPE_CLIENT_PER_CALL_CONFIG) {
		session_unref(session);
	}
}

/*!
 * \internal
 * \brief The ao2 destructor.
 * This cleans up the reference to the parent ast_websocket and the
 * outbound connection websocket if any.
 */
static void session_dtor(void *obj)
{
	struct ari_ws_session *session = obj;

	ast_debug(3, "%s: Destroying ARI websocket session\n",
		session->session_id);

	ast_free(session->app_name);
	ast_free(session->remote_addr);
	ast_free(session->channel_id);
	ast_free(session->channel_name);
	ao2_cleanup(session->owc);
	session->owc = NULL;
	if (!session->ast_ws_session) {
		return;
	}
	ast_websocket_unref(session->ast_ws_session);
	session->ast_ws_session = NULL;
}

#define handle_create_error(ser, code, msg, reason) \
({ \
	if (ser) { \
		ast_http_error(ser, code, msg, reason); \
	} \
	ast_log(LOG_WARNING, "Failed to create ARI websocket session: %d %s %s\n", \
		code, msg, reason); \
})

static struct ari_ws_session *session_create(
	struct ast_tcptls_session_instance *ser,
	const char *apps,
	int subscribe_all,
	const char *session_id,
	struct ari_conf_outbound_websocket *ows,
	enum ast_websocket_type ws_type)
{
	RAII_VAR(struct ari_ws_session *, session, NULL, ao2_cleanup);
	size_t size;

	ast_debug(3, "%s: Creating ARI websocket session for apps '%s'\n",
		 session_id, apps);

	size = sizeof(*session) + strlen(session_id) + 1;

	session = ao2_alloc(size, session_dtor);
	if (!session) {
		return NULL;
	}

	session->type = ws_type;
	session->subscribe_all = subscribe_all;

	strcpy(session->session_id, session_id); /* Safe */

	/* Instantiate the hash table for Stasis apps */
	if (AST_VECTOR_INIT(&session->websocket_apps, APPS_INIT_SIZE)) {
		handle_create_error(ser, 500, "Internal Server Error",
			"Allocation failed");
		return NULL;
	}

	/* Instantiate the message queue */
	if (AST_VECTOR_INIT(&session->message_queue, MESSAGES_INIT_SIZE)) {
		handle_create_error(ser, 500, "Internal Server Error",
			"Allocation failed");
		AST_VECTOR_FREE(&session->websocket_apps);
		return NULL;
	}

	session->validator = ari_validate_message_fn;

	if (ows) {
		session->owc = ao2_bump(ows);
	}

	if (session_register_apps(session, apps, subscribe_all) < 0) {
		handle_create_error(ser, 500, "Internal Server Error",
			"Stasis app registration failed");
		session_reset(session);
		return NULL;
	}

	if (!ao2_link(session_registry, session)) {
		handle_create_error(ser, 500, "Internal Server Error",
			"Allocation failed");
		session_reset(session);
		return NULL;
	}

	return ao2_bump(session);
}

/*!
 * \internal
 * \brief Updates the websocket session.
 *
 * \details If the value of the \c ws_session is not \c NULL and there are messages in the
 *          event session's \c message_queue, the messages are dispatched and removed from
 *          the queue.
 *
 * \param ari_ws_session  The ARI websocket session
 * \param ast_ws_session  The Asterisk websocket session
 */
static int session_update(struct ari_ws_session *ari_ws_session,
	struct ast_websocket *ast_ws_session, int send_registered_events)
{
	RAII_VAR(struct ari_conf_general *, general, ari_conf_get_general(), ao2_cleanup);
	int i;

	if (ast_ws_session == NULL) {
		return -1;
	}

	if (!general) {
		return -1;
	}

	ari_ws_session->remote_addr = ast_strdup(ast_sockaddr_stringify(
		ast_websocket_remote_address(ast_ws_session)));
	if (!ari_ws_session->remote_addr) {
		ast_log(LOG_ERROR, "Failed to copy remote address\n");
		return -1;
	}

	if (ast_websocket_set_nonblock(ast_ws_session) != 0) {
		ast_log(LOG_ERROR,
			"ARI web socket failed to set nonblock; closing: %s\n",
			strerror(errno));
		return -1;
	}

	if (ast_websocket_set_timeout(ast_ws_session, general->write_timeout)) {
		ast_log(LOG_WARNING, "Failed to set write timeout %d on ARI web socket\n",
			general->write_timeout);
	}

	ao2_ref(ast_ws_session, +1);
	ari_ws_session->ast_ws_session = ast_ws_session;
	ao2_lock(ari_ws_session);
	for (i = 0; i < AST_VECTOR_SIZE(&ari_ws_session->message_queue); i++) {
		struct ast_json *msg = AST_VECTOR_GET(&ari_ws_session->message_queue, i);
		session_write(ari_ws_session, msg);
		ast_json_unref(msg);
	}

	AST_VECTOR_RESET(&ari_ws_session->message_queue, AST_VECTOR_ELEM_CLEANUP_NOOP);
	ao2_unlock(ari_ws_session);

	if (send_registered_events) {
		int i;
		char *app;

		for (i = 0; i < AST_VECTOR_SIZE(&ari_ws_session->websocket_apps); i++) {
			app = AST_VECTOR_GET(&ari_ws_session->websocket_apps, i);
			session_send_app_event(ari_ws_session,
				"ApplicationRegistered", app);
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief This function gets called for incoming websocket connections
 * before the upgrade process is completed.
 *
 * The point is to be able to report early errors via HTTP rather
 * than letting res_http_websocket create an ast_websocket session
 * then immediately close it if there's an error.
 */
static int websocket_attempted_cb(struct ast_tcptls_session_instance *ser,
	struct ast_variable *get_params, struct ast_variable *headers,
	const char *session_id)
{
	const char *subscribe_all = NULL;
	const char *apps = NULL;
	struct ari_ws_session *session = NULL;

	apps = ast_variable_find_in_list(get_params, "app");
	if (ast_strlen_zero(apps)) {
		handle_create_error(ser, 400, "Bad Request",
			"HTTP request is missing param: [app]");
		return -1;
	}

	subscribe_all = ast_variable_find_in_list(get_params, "subscribeAll");

	session = session_create(ser, apps, ast_true(subscribe_all),
		session_id, NULL, AST_WS_TYPE_INBOUND);
	if (!session) {
		handle_create_error(ser, 500, "Server Error",
			"Failed to create ARI websocket session");
		return -1;
	}
	/* It's in the session registry now so we can release our reference */
	session_unref(session);

	return 0;
}

/*!
 * \internal
 * \brief This function gets called for incoming websocket connections
 * after the upgrade process is completed.
 */
static void websocket_established_cb(struct ast_websocket *ast_ws_session,
	struct ast_variable *get_params, struct ast_variable *upgrade_headers)
{
	/*
	 * ast_ws_session is passed in with it's refcount bumped so
	 * we need to unref it when we're done.  The refcount will
	 * be bumped again when we add it to the ari_ws_session.
	 */
	RAII_VAR(struct ast_websocket *, s, ast_ws_session, ast_websocket_unref);
	RAII_VAR(struct ari_ws_session *, ari_ws_session, NULL, session_cleanup);
	struct ast_json *msg;
	struct ast_variable *v;
	char *remote_addr = ast_sockaddr_stringify(
		ast_websocket_remote_address(ast_ws_session));
	const char *session_id = ast_websocket_session_id(ast_ws_session);

	SCOPE_ENTER(2, "%s: WebSocket established\n", remote_addr);

	if (TRACE_ATLEAST(2)) {
		ast_trace(2, "%s: Websocket Upgrade Headers:\n", remote_addr);
		for (v = upgrade_headers; v; v = v->next) {
			ast_trace(3, "--> %s: %s\n", v->name, v->value);
		}
	}

	/*
	 * Find the ari_ws_session that was created by websocket_attempted_cb
	 * and update its ast_websocket.
	 */
	ari_ws_session = ao2_find(session_registry, session_id, OBJ_SEARCH_KEY);
	if (!ari_ws_session) {
		 SCOPE_EXIT_LOG_RTN(LOG_ERROR,
			"%s: Failed to locate an event session for the websocket session %s\n",
			remote_addr, session_id);
	}

	/*
	 * Since this is a new inbound websocket session,
	 * session_register_apps() will have already sent "ApplicationRegistered"
	 * events for the apps. We don't want to do it again.
	 */
	session_update(ari_ws_session, ast_ws_session, 0);

	ari_ws_session->connected = 1;
	ast_trace(-1, "%s: Waiting for messages\n", remote_addr);
	while ((msg = session_read(ari_ws_session))) {
		ari_websocket_process_request(ari_ws_session, remote_addr,
			upgrade_headers, ari_ws_session->app_name, msg);
		ast_json_unref(msg);
	}
	ari_ws_session->connected = 0;

	SCOPE_EXIT("%s: Websocket closed\n", remote_addr);
}

static int session_shutdown_cb(void *obj, void *arg, int flags)
{
	struct ari_ws_session *session = obj;

	/* Per-call configs have no actual websocket */
	if (session->type == AST_WS_TYPE_CLIENT_PER_CALL_CONFIG) {
		ast_log(LOG_NOTICE, "%s: Shutting down %s ARI websocket session\n",
			session->session_id,
			ari_websocket_type_to_str(session->type));
		session_cleanup(session);
		return 0;
	}
	if (session->type == AST_WS_TYPE_INBOUND) {
		ast_log(LOG_NOTICE, "%s: Shutting down inbound ARI websocket session from %s\n",
			session->session_id, session->remote_addr);
	} else {
		ast_log(LOG_NOTICE, "%s: Shutting down %s ARI websocket session to %s\n",
			session->session_id,
			ari_websocket_type_to_str(session->type),
			session->remote_addr);
	}

	/*
	 * We need to ensure the session is kept around after the cleanup
	 * so we can close the websocket.
	 */
	ao2_bump(session);
	session->closing = 1;
	session_cleanup(session);
	if (session->ast_ws_session) {
		ast_websocket_close(session->ast_ws_session, 1000);
	}

	return 0;
}


struct ari_ws_session * ari_websocket_get_session(const char *session_id)
{
	return ao2_find(session_registry, session_id, OBJ_SEARCH_KEY);
}

static struct ari_ws_session *session_find_by_app(const char *app_name,
	unsigned int ws_type)
{
	struct ari_ws_session *session = NULL;
	struct ao2_iterator i;

	if (ast_strlen_zero(app_name)) {
		return NULL;
	}

	i = ao2_iterator_init(session_registry, 0);
	while ((session = ao2_iterator_next(&i))) {
		char *app = NULL;
		if (!(session->type & ws_type)) {
			session_unref(session);
			continue;
		}

		app = AST_VECTOR_GET_CMP(&session->websocket_apps,
			app_name, ast_strings_equal);
		if (app) {
			break;
		}
		session_unref(session);
	}
	ao2_iterator_destroy(&i);
	return session;
}

/*!
 * \internal
 * \brief Connection and request handler thread for outbound websockets.
 *
 * This thread handles the connection and reconnection logic for outbound
 * websockets.  Once connected, it waits for incoming REST over Websocket
 * requests and dispatches them to ari_websocket_process_request()).
 */
static void *outbound_session_handler_thread(void *obj)
{
	RAII_VAR(struct ari_ws_session *, session, obj, session_cleanup);
	int already_sent_registers = 1;

	ast_debug(3, "%s: Starting outbound websocket thread RC: %d\n",
		session->session_id, (int)ao2_ref(session, 0));
	session->thread = pthread_self();
	session->connected = 0;

	while(1) {
		RAII_VAR(struct ast_websocket *, astws, NULL, ast_websocket_unref);
		RAII_VAR(struct ast_variable *, upgrade_headers, NULL, ast_variables_destroy);
		enum ast_websocket_result result;
		struct ast_json *msg;

		ast_debug(3, "%s: Attempting to connect to %s\n", session->session_id,
			session->owc->websocket_client->uri);

		astws = ast_websocket_client_connect(session->owc->websocket_client,
			NULL, session->session_id, &result);
		if (!astws || result != WS_OK) {
			if (session->type == AST_WS_TYPE_CLIENT_PER_CALL) {
				struct stasis_app_control *control =
					stasis_app_control_find_by_channel_id(session->channel_id);
				if (control) {
					ast_debug(3, "%s: Connection failed.  Returning to dialplan.\n",
						session->session_id);
					stasis_app_control_mark_failed(control);
					stasis_app_control_continue(control, NULL, NULL, -1);
					ao2_cleanup(control);
				} else {
					ast_debug(3, "%s: Connection failed.  No control object found.\n",
						session->session_id);
				}

				break;
			}
			usleep(session->owc->websocket_client->reconnect_interval * 1000);
			continue;
		}
		ast_log(LOG_NOTICE, "%s: Outbound websocket connected to %s\n",
			session->type == AST_WS_TYPE_CLIENT_PERSISTENT ? session->session_id : session->channel_name,
				session->owc->websocket_client->uri);

		/*
		 * We only want to send "ApplicationRegistered" events in the
		 * case of a reconnect.  The initial connection will have already sent
		 * the events when outbound_register_apps() was called.
		 */
		session_update(session, astws, !already_sent_registers);
		already_sent_registers = 0;

		/*
		 * This is the Authorization header that would normally be taken
		 * from the incoming HTTP request that is being upgraded to a websocket.
		 * Since this is an outbound websocket, we have to create it ourselves.
		 *
		 * This is NOT the same as the Authorization header that is used for
		 * authentication with the remote websocket server.
		 */
		upgrade_headers = ast_http_create_basic_auth_header(
			session->owc->local_ari_user, session->owc->local_ari_password);
		if (!upgrade_headers) {
			ast_log(LOG_WARNING, "%s: Failed to create upgrade header\n", session->session_id);
			session->thread = 0;
			ast_websocket_close(astws, 1000);
			return NULL;
		}

		session->connected = 1;
		ast_debug(3, "%s: Websocket connected\n", session->session_id);
		ast_debug(3, "%s: Waiting for messages RC: %d\n",
			session->session_id, (int)ao2_ref(session, 0));

		/*
		 * The websocket is connected.  Now we need to wait for messages
		 * from the server.
		 */
		while ((msg = session_read(session))) {
			ari_websocket_process_request(session, session->remote_addr,
				upgrade_headers, session->app_name, msg);
			ast_json_unref(msg);
		}

		session->connected = 0;
		ast_websocket_unref(session->ast_ws_session);
		session->ast_ws_session = NULL;
		if (session->closing) {
			ast_debug(3, "%s: Websocket closing RC: %d\n",
				session->session_id, (int)ao2_ref(session, 0));
			break;
		}

		ast_log(LOG_WARNING, "%s: Websocket disconnected.  Reconnecting\n",
			session->session_id);
	}

	ast_debug(3, "%s: Stopping outbound websocket thread RC: %d\n",
		session->session_id, (int)ao2_ref(session, 0));
	session->thread = 0;

	return NULL;
}

enum session_apply_result {
	SESSION_APPLY_NO_CHANGE,
	SESSION_APPLY_OK,
	SESSION_APPLY_RECONNECT_REQUIRED,
	SESSION_APPLY_FAILED,
};

static enum session_apply_result outbound_session_apply_config(
	struct ari_ws_session *session,
	struct ari_conf_outbound_websocket *new_owc)
{
	enum session_apply_result apply_result;
	enum ari_conf_owc_fields what_changed;
	const char *new_owc_id = ast_sorcery_object_get_id(new_owc);

	what_changed = ari_conf_owc_detect_changes(session->owc, new_owc);

	if (what_changed == ARI_OWC_FIELD_NONE) {
		ast_debug(2, "%s: No changes detected\n", new_owc_id);
		return SESSION_APPLY_NO_CHANGE;
	}
	ast_debug(2, "%s: Config change detected.  Checking details\n", new_owc_id);

	if (what_changed & ARI_OWC_NEEDS_REREGISTER) {
		ast_debug(2, "%s: Re-registering apps\n", new_owc_id);

		if (!(what_changed & ARI_OWC_FIELD_SUBSCRIBE_ALL)) {
			/*
			 * If subscribe_all didn't change, we don't have to
			 * unregister apps that are already registered and
			 * also in the new config.  We'll remove them from
			 * the session->websocket_apps container so that
			 * session_unregister_apps will only clean up
			 * the ones that are going away. session_register_apps
			 * will add them back in again and cause ApplicationReplaced
			 * messages to be sent.
			 *
			 * If subscribe_all did change, we have no choice but to
			 * unregister all apps and register all the ones in
			 * the new config even if they already existed.
			 */
			int i = 0;
			char *app;

			while(i < (int) AST_VECTOR_SIZE(&session->websocket_apps)) {
				app = AST_VECTOR_GET(&session->websocket_apps, i);
				if (ast_in_delimited_string(app, new_owc->apps, ',')) {
					AST_VECTOR_REMOVE_ORDERED(&session->websocket_apps, i);
					ast_debug(3, "%s: Unlinked app '%s' to keep it from being unregistered\n",
						new_owc_id, app);
					ast_free(app);
				} else {
					i++;
				}
			}
		}

		session_unregister_apps(session);

		/*
		 * Register the new apps.  This will also replace any
		 * existing apps that are in the new config sending
		 * ApplicationRegistered or ApplicationReplaced events
		 * as necessary.
		 */
		if (session_register_apps(session, new_owc->apps,
				new_owc->subscribe_all) < 0) {
			ast_log(LOG_WARNING, "%s: Failed to register apps '%s'\n",
				new_owc_id, new_owc->apps);
			/* Roll back. */
			session_unregister_apps(session);
			/* Re-register the original apps. */
			if (session_register_apps(session, session->owc->apps,
						session->owc->subscribe_all) < 0) {
				ast_log(LOG_WARNING, "%s: Failed to re-register apps '%s'\n",
					new_owc_id, session->owc->apps);
			}
			return SESSION_APPLY_FAILED;
		}
	}
	/*
	 * We need to update the session with the new config
	 * but it has to be done after re-registering apps and
	 * before we reconnect.
	 */
	ao2_replace(session->owc, new_owc);
	session->type = new_owc->websocket_client->connection_type;
	session->subscribe_all = new_owc->subscribe_all;

	apply_result = SESSION_APPLY_OK;

	if (what_changed & ARI_OWC_NEEDS_RECONNECT) {
		ast_debug(2, "%s: Reconnect required\n", new_owc_id);
		apply_result = SESSION_APPLY_RECONNECT_REQUIRED;
		if (session->ast_ws_session) {
			ast_debug(2, "%s: Closing websocket\n", new_owc_id);
			ast_websocket_close(session->ast_ws_session, 1000);
		}
	}

	return apply_result;
}

/*
 * This is the fail-safe timeout for the per-call websocket
 * connection.  To prevent a cleanup race condition, we wait
 * 3 times the timeout the thread will use to connect to the
 * websocket server.  This way we're sure the thread will be
 * done before we do final cleanup.  This timeout is only used
 * if the thread is cancelled somehow and can't indicate
 * whether it actually connected or not.
 */
#define PER_CALL_FAIL_SAFE_TIMEOUT(owc) \
	(int64_t)((owc->websocket_client->connect_timeout + owc->websocket_client->reconnect_interval) \
	* (owc->websocket_client->reconnect_attempts + 3))

/*!
 * \brief This function gets called by app_stasis when a call arrives
 * but a Stasis application isn't already registered.  We check to see
 * if a per-call config exists for the application and if so, we create a
 * per-call websocket connection and return a unique app id which app_stasis
 * can use to call stasis_app_exec() with.
 */
char *ast_ari_create_per_call_websocket(const char *app_name,
	struct ast_channel *chan)
{
	RAII_VAR(struct ari_ws_session *, session, NULL, session_unref);
	RAII_VAR(struct ari_conf_outbound_websocket *, owc, NULL, ao2_cleanup);
	RAII_VAR(char *, session_id, NULL, ast_free);
	RAII_VAR(char *, app_id, NULL, ast_free);
	enum ari_conf_owc_fields invalid_fields;
	const char *owc_id = NULL;
	char *app_id_rtn = NULL;
	struct timeval tv_start;
	int res = 0;

	owc = ari_conf_get_owc_for_app(app_name, AST_WS_TYPE_CLIENT_PER_CALL_CONFIG);
	if (!owc) {
		ast_log(LOG_WARNING, "%s: Failed to find outbound websocket per-call config for app '%s'\n",
			ast_channel_name(chan), app_name);
		return NULL;
	}
	owc_id = ast_sorcery_object_get_id(owc);
	invalid_fields = ari_conf_owc_get_invalid_fields(owc_id);

	if (invalid_fields) {
		ast_log(LOG_WARNING, "%s: Unable to create per-call websocket.  Outbound websocket config is invalid\n",
			owc_id);
		return NULL;
	}

	res = ast_asprintf(&session_id, "%s:%s", owc_id, ast_channel_name(chan));
	if (res < 0) {
		return NULL;
	}
	res = ast_asprintf(&app_id, "%s:%s", app_name, ast_channel_name(chan));
	if (res < 0) {
		ast_free(app_id);
		return NULL;
	}

	session = session_create(NULL, app_id, owc->subscribe_all,
		session_id, owc, AST_WS_TYPE_CLIENT_PER_CALL);
	if (!session) {
		ast_log(LOG_WARNING, "%s: Failed to create websocket session\n", session_id);
		return NULL;
	}

	session->channel_id = ast_strdup(ast_channel_uniqueid(chan));
	session->channel_name = ast_strdup(ast_channel_name(chan));

	/*
	 * We have to bump the session reference count here because
	 * we need to check that the session is connected before we return.
	 * If it didn't connect, then the thread will have cleaned up the
	 * session while we're in the loop checking for the connection
	 * which will result in a SEGV or FRACK.
	 * RAII will clean up this bump.
	 */
	ao2_bump(session);
	ast_debug(2, "%s: Starting thread RC: %d\n", session->session_id,
		(int)ao2_ref(session, 0));

	if (ast_pthread_create_detached_background(&session->thread, NULL,
		outbound_session_handler_thread, session)) {
		session_cleanup(session);
		ast_log(LOG_WARNING, "%s: Failed to create thread.\n", session->session_id);
		return NULL;
	}

	/*
	 * We need to make sure the session connected and is processing
	 * requests before we return but we don't want to block forever
	 * in case the thread never starts or gets cancelled so we have
	 * a fail-safe timeout.
	 */
	tv_start = ast_tvnow();
	while (session->thread > 0 && !session->connected) {
		struct timeval tv_now = ast_tvnow();
		if (ast_tvdiff_ms(tv_now, tv_start) > PER_CALL_FAIL_SAFE_TIMEOUT(owc)) {
			break;
		}
		/* Sleep for 500ms before checking again. */
		usleep(500 * 1000);
	}

	if (session->thread <= 0 || !session->connected) {
		ast_log(LOG_WARNING, "%s: Failed to create per call websocket thread\n",
			session_id);
		return NULL;
	}

	ast_debug(3, "%s: Created per call websocket for app '%s'\n",
		session_id, app_id);

	/*
	 * We now need to prevent RAII from freeing the app_id.
	 */
	app_id_rtn = app_id;
	app_id = NULL;
	return app_id_rtn;
}

#define STASIS_END_MAX_WAIT_MS 5000
#define STASIS_END_POST_WAIT_US (3000 * 1000)

/*
 * This thread is used to close the websocket after the StasisEnd
 * event has been sent and control has been returned to the dialplan.
 * We wait a few seconds to allow additional events to be sent
 * like ChannelVarset and ChannelDestroyed.
 */
static void *outbound_session_pc_close_thread(void *data)
{
	/*
	 * We're using RAII because we want to show a debug message
	 * after we run ast_websocket_close().
	 */
	RAII_VAR(struct ari_ws_session *, session, data, session_unref);

	/*
	 * We're going to wait 3 seconds to allow stasis to send additional
	 * events like ChannelVarset and ChannelDestroyed after the StasisEnd.
	 */
	ast_debug(3, "%s: Waiting for %dms before closing websocket RC: %d\n",
		session->session_id, (int)(STASIS_END_POST_WAIT_US / 1000),
		(int)ao2_ref(session, 0));
	usleep(STASIS_END_POST_WAIT_US);
	session->closing = 1;
	if (session->ast_ws_session) {
		ast_websocket_close(session->ast_ws_session, 1000);
	}
	ast_debug(3, "%s: Websocket closed RC: %d\n", session->session_id,
		(int)ao2_ref(session, 0));
	return NULL;
}

/*!
 * \brief This function is called by the app_stasis dialplan app
 * to close a per-call websocket after stasis_app_exec() returns.
 */
void ast_ari_close_per_call_websocket(char *app_name)
{
	struct ari_ws_session *session = NULL;
	pthread_t thread;
	struct timeval tv_start;

	session = session_find_by_app(app_name, AST_WS_TYPE_CLIENT_PER_CALL);
	if (!session) {
		ast_debug(3, "%s: Per call websocket not found\n", app_name);
		ast_free(app_name);
		return;
	}
	ast_free(app_name);

	/*
	 * When stasis_app_exec() returns, the StasisEnd event for the
	 * channel has been queued but since actually sending it is done
	 * in a separate thread, it probably won't have been sent yet.
	 * We need to wait for it to go out on the wire before we close the
	 * websocket.  ari_websocket_send_event will set a flag on the session
	 * when a StasisEnd event is sent for the channel that originally
	 * triggered the connection.  We'll wait for that but we don't want
	 * to wait forever so there's a fail-safe timeout in case a thread
	 * got cancelled or we missed the StasisEnd event somehow.
	 */
	ast_debug(3, "%s: Waiting for StasisEnd event to be sent RC: %d\n",
		session->session_id, (int)ao2_ref(session, 0));

	tv_start = ast_tvnow();
	while (session->thread > 0 && !session->stasis_end_sent) {
		struct timeval tv_now = ast_tvnow();
		int64_t diff = ast_tvdiff_ms(tv_now, tv_start);
		ast_debug(3, "%s: Waiting for StasisEnd event %lu %d %ld\n",
			session->session_id, (unsigned long)session->thread,
			session->stasis_end_sent, diff);
		if (diff > STASIS_END_MAX_WAIT_MS) {
			break;
		}
		/* Sleep for 500ms before checking again. */
		usleep(500 * 1000);
	}
	ast_debug(3, "%s: StasisEnd event sent.  Scheduling websocket close. RC: %d\n",
		session->session_id, (int)ao2_ref(session, 0));

	/*
	 * We can continue to send events like ChannelVarset and ChannelDestroyed
	 * to the websocket after the StasisEnd event but those events won't be
	 * generated until after the Stasis() dialplan app returns.  We don't want
	 * to hold up the dialplan while we wait so we'll create a thread that waits
	 * a few seconds more before closing the websocket.
	 *
	 * We transferring ownership of the session to the thread.
	 */
	if (ast_pthread_create_detached_background(&thread, NULL,
		outbound_session_pc_close_thread, session)) {
		ast_log(LOG_WARNING, "%s: Failed to create websocket close thread\n",
			session->session_id);
		session_unref(session);
	}
	ast_debug(3, "%s: Scheduled websocket close RC: %d\n",
		session->session_id, (int)ao2_ref(session, 0));

	return;
}

struct ao2_container* ari_websocket_get_sessions(void)
{
	return ao2_bump(session_registry);
}

static int outbound_session_create(void *obj, void *args, int flags)
{
	struct ari_conf_outbound_websocket *owc = obj;
	const char *owc_id = ast_sorcery_object_get_id(owc);
	struct ari_ws_session *session = NULL;
	enum session_apply_result apply_result;
	enum ari_conf_owc_fields invalid_fields = ari_conf_owc_get_invalid_fields(owc_id);

	session = ari_websocket_get_session(owc_id);
	if (session) {
		ast_debug(2, "%s: Found existing connection\n", owc_id);
		if (invalid_fields) {
			session_unref(session);
			ast_log(LOG_WARNING,
				"%s: Unable to update websocket session. Outbound websocket config is invalid\n",
				owc_id);
			return 0;
		}

		ao2_lock(session);
		apply_result = outbound_session_apply_config(session, owc);
		ao2_unlock(session);
		session_unref(session);
		if (apply_result == SESSION_APPLY_FAILED) {
			ast_log(LOG_WARNING,
				"%s: Failed to apply new configuration. Existing connection preserved.\n",
				owc_id);
		}
		return 0;
	}

	if (invalid_fields) {
		ast_log(LOG_WARNING,
			"%s: Unable to create websocket session. Outbound websocket config is invalid\n",
			owc_id);
		return 0;
	}

	session = session_create(NULL, owc->apps, owc->subscribe_all, owc_id,
		owc, owc->websocket_client->connection_type);
	if (!session) {
		ast_log(LOG_WARNING, "%s: Failed to create websocket session\n", owc_id);
		return 0;
	}

	if (owc->websocket_client->connection_type == AST_WS_TYPE_CLIENT_PER_CALL_CONFIG) {
		/* There's no thread to transfer the reference to */
		session_unref(session);
		return 0;
	}

	ast_debug(2, "%s: Starting thread RC: %d\n", session->session_id,
		(int)ao2_ref(session, 0));
	/* We're transferring the session reference to the thread. */
	if (ast_pthread_create_detached_background(&session->thread, NULL,
		outbound_session_handler_thread, session)) {
		session_cleanup(session);
		ast_log(LOG_WARNING, "%s: Failed to create thread.\n", session->session_id);
		return 0;
	}
	ast_debug(2, "%s: launched thread\n", session->session_id);

	return 0;
}

static void outbound_sessions_load(const char *name)
{
	RAII_VAR(struct ao2_container *, owcs, ari_conf_get_owcs(), ao2_cleanup);
	struct ao2_iterator i;
	struct ari_ws_session *session;

	ast_debug(2, "Reloading ARI websockets\n");

	ao2_callback(owcs, OBJ_NODATA, outbound_session_create, NULL);

	i = ao2_iterator_init(session_registry, 0);
	while ((session = ao2_iterator_next(&i))) {
		int cleanup = 1;
		if (session->owc
			&& (session->type &
			(AST_WS_TYPE_CLIENT_PERSISTENT | AST_WS_TYPE_CLIENT_PER_CALL_CONFIG))) {
			struct ari_conf_outbound_websocket *ows =
				ari_conf_get_owc(session->session_id);
			if (!ows) {
				ast_debug(3, "Cleaning up outbound websocket %s\n",
					session->session_id);
				session->closing = 1;
				session_cleanup(session);
				if (session->ast_ws_session) {
					ast_websocket_close(session->ast_ws_session, 1000);
				}

				if (session->type == AST_WS_TYPE_CLIENT_PERSISTENT) {
					/*
					 * If persistent, session_cleanup will cleanup
					 * this reference so we don't want to double clean it up.
					 * session_cleanup doesn't cleanup the reference
					 * for per-call configs so we need to do that ourselves.
					 */
					cleanup = 0;
				}
			}
			ao2_cleanup(ows);
		}
		/* We don't want to double cleanup if its been closed. */
		if (cleanup) {
			ao2_cleanup(session);
		}
	}
	ao2_iterator_destroy(&i);

	return;
}

int ari_outbound_websocket_start(struct ari_conf_outbound_websocket *owc)
{
	if (owc) {
		return outbound_session_create(owc, NULL, 0);
	}
	return -1;
}

void ari_websocket_shutdown(struct ari_ws_session *session)
{
	if (session) {
		session_shutdown_cb(session, NULL, 0);
	}
}

void ari_websocket_shutdown_all(void)
{
	if (session_registry) {
		ao2_callback(session_registry, OBJ_MULTIPLE | OBJ_NODATA,
			session_shutdown_cb, NULL);
	}
}

static void session_registry_dtor(void)
{
	if (session_registry) {
		ao2_callback(session_registry, OBJ_MULTIPLE | OBJ_NODATA,
			session_shutdown_cb, NULL);
		ao2_cleanup(session_registry);
		session_registry = NULL;
	}
}

static struct ast_sorcery_observer observer_callbacks = {
	.loaded = outbound_sessions_load,
};

int ari_websocket_unload_module(void)
{
	ari_sorcery_observer_remove("outbound_websocket", &observer_callbacks);
	session_registry_dtor();
	ao2_cleanup(ast_ws_server);
	ast_ws_server = NULL;
	return 0;
}

AO2_STRING_FIELD_CMP_FN(ari_ws_session, session_id)
AO2_STRING_FIELD_SORT_FN(ari_ws_session, session_id)

int ari_websocket_load_module(int is_enabled)
{
	int res = 0;
	struct ast_websocket_protocol *protocol;

	ast_debug(2, "Initializing ARI websockets.  Enabled: %s\n", is_enabled ? "yes" : "no");

	session_registry = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE,
		ari_ws_session_sort_fn, ari_ws_session_cmp_fn);
	if (!session_registry) {
		ast_log(LOG_WARNING,
			    "Failed to allocate the local registry for websocket applications\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	res = ari_sorcery_observer_add("outbound_websocket", &observer_callbacks);
	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to register ARI websocket observer\n");
		ari_websocket_unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	/*
	 * The global "enabled" flag only controls whether the REST and
	 * inbound websockets are enabled.  The outbound websocket
	 * configs are always enabled.
	if (!is_enabled) {
		return AST_MODULE_LOAD_SUCCESS;
	}
	 */

	ast_ws_server = ast_websocket_server_create();
	if (!ast_ws_server) {
		ari_websocket_unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	protocol = ast_websocket_sub_protocol_alloc("ari");
	if (!protocol) {
		ari_websocket_unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	protocol->session_attempted = websocket_attempted_cb;
	protocol->session_established = websocket_established_cb;
	res = ast_websocket_server_add_protocol2(ast_ws_server, protocol);

	return res == 0 ? AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

