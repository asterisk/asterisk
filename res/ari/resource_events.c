/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2012 - 2013, Digium, Inc.
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

/*! \file
 *
 * \brief /api-docs/events.{format} implementation- WebSocket resource
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "resource_events.h"
#include "asterisk/astobj2.h"
#include "asterisk/http_websocket.h"
#include "asterisk/stasis_app.h"
#include "asterisk/vector.h"

/*! Number of buckets for the event session registry. Remember to keep it a prime number! */
#define EVENT_SESSION_NUM_BUCKETS 23

/*! Number of buckets for a websocket apps container. Remember to keep it a prime number! */
#define APPS_NUM_BUCKETS 7

/*! Initial size of a message queue. */
#define MESSAGES_INIT_SIZE 23


/*! \brief A wrapper for the /ref ast_ari_websocket_session. */
struct event_session {
	struct ast_ari_websocket_session *ws_session;   /*!< Handle to the websocket session. */
	struct ao2_container *websocket_apps;           /*!< List of Stasis apps registered to
	                                                     the websocket session. */
	AST_VECTOR(, struct ast_json *) message_queue;  /*!< Container for holding delayed messages. */
	char session_id[];                              /*!< The id for the websocket session. */
};

/*! \brief \ref event_session error types. */
enum event_session_error_type {
	ERROR_TYPE_STASIS_REGISTRATION = 1,  /*!< Stasis failed to register the application. */
	ERROR_TYPE_OOM = 2,                  /*!< Insufficient memory to create the event
	                                          session. */
	ERROR_TYPE_MISSING_APP_PARAM = 3,    /*!< HTTP request was missing an [app] parameter. */
	ERROR_TYPE_INVALID_APP_PARAM = 4,    /*!< HTTP request contained an invalid [app]
	                                          parameter. */
};

/*! \brief Local registry for created \ref event_session objects. */
static struct ao2_container *event_session_registry;

/*!
 * \brief Callback handler for Stasis application messages.
 *
 * \internal
 *
 * \param data      Void pointer to the event session (\ref event_session).
 * \param app_name  Name of the Stasis application that dispatched the message.
 * \param message   The dispatched message.
 */
static void stasis_app_message_handler(
		void *data, const char *app_name, struct ast_json *message)
{
	struct event_session *session = data;
	const char *msg_type, *msg_application;
	int app_debug_enabled;

	ast_assert(session != NULL);

	/*
	 * We need to get the debug flag before locking session
	 * to help prevent a deadlock with the apps_registry container.
	 */
	app_debug_enabled = stasis_app_get_debug_by_name(app_name);

	ao2_lock(session);

	msg_type = S_OR(ast_json_string_get(ast_json_object_get(message, "type")), "");
	msg_application = S_OR(
		ast_json_string_get(ast_json_object_get(message, "application")), "");

	/* If we've been replaced, remove the application from our local
	   websocket_apps container */
	if (strcmp(msg_type, "ApplicationReplaced") == 0 &&
		strcmp(msg_application, app_name) == 0) {
		ao2_find(session->websocket_apps, msg_application,
			OBJ_UNLINK | OBJ_NODATA);
	}

	/* Now, we need to determine our state to see how we will handle the message */
	if (ast_json_object_set(message, "application", ast_json_string_create(app_name))) {
		/* We failed to add an application element to our json message */
		ast_log(LOG_WARNING,
		        "Failed to dispatch '%s' message from Stasis app '%s'; could not update message\n",
		        msg_type,
		        msg_application);
	} else if (!session->ws_session) {
		/* If the websocket is NULL, the message goes to the queue */
		if (!AST_VECTOR_APPEND(&session->message_queue, message)) {
			ast_json_ref(message);
		}
		ast_log(LOG_WARNING,
				"Queued '%s' message for Stasis app '%s'; websocket is not ready\n",
				msg_type,
				msg_application);
	} else if (stasis_app_event_allowed(app_name, message)) {
		if (app_debug_enabled) {
			char *str = ast_json_dump_string_format(message, ast_ari_json_format());

			ast_verbose("<--- Sending ARI event to %s --->\n%s\n",
				ast_sockaddr_stringify(ast_ari_websocket_session_get_remote_addr(session->ws_session)),
				str);
			ast_json_free(str);
		}

		/* We are ready to publish the message */
		ast_ari_websocket_session_write(session->ws_session, message);
	}

	ao2_unlock(session);
}

/*!
 * \brief AO2 comparison function for \ref event_session objects.
 *
 * \internal
 *
 * \param obj    Void pointer to the \ref event_session container.
 * \param arg    Void pointer to the \ref event_session object.
 * \param flags  The \ref search_flags to use when creating the hash key.
 *
 * \retval 0          The objects are not equal.
 * \retval CMP_MATCH  The objects are equal.
 */
static int event_session_compare(void *obj, void *arg, int flags)
{
	const struct event_session *object_left = obj;
	const struct event_session *object_right = arg;
	const char *right_key = arg;
	int cmp = 0;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->session_id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->session_id, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		cmp = strncmp(object_left->session_id, right_key, strlen(right_key));
		break;
	default:
		break;
	}

	return cmp ? 0 : CMP_MATCH;
}

/*!
 * \brief AO2 hash function for \ref event_session objects.
 *
 * \details Computes hash value for the given \ref event_session, with respect to the
 *          provided search flags.
 *
 * \internal
 *
 * \param obj    Void pointer to the \ref event_session object.
 * \param flags  The \ref search_flags to use when creating the hash key.
 *
 * \retval > 0  on success
 * \retval   0  on failure
 */
static int event_session_hash(const void *obj, const int flags)
{
	const struct event_session *session;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		session = obj;
		key = session->session_id;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*!
 * \brief Explicitly shutdown a session.
 *
 * \details An explicit shutdown is necessary, since the \ref stasis_app has a reference
 *          to this session. We also need to be sure to null out the \c ws_session field,
 *          since the websocket is about to go away.
 *
 * \internal
 *
 * \param session  Event session object (\ref event_session).
 */
static void event_session_shutdown(struct event_session *session)
{
	struct ao2_iterator i;
	char *app;
	int j;
	SCOPED_AO2LOCK(lock, session);

	/* Clean up the websocket_apps container */
	if (session->websocket_apps) {
		i = ao2_iterator_init(session->websocket_apps, 0);
		while ((app = ao2_iterator_next(&i))) {
			stasis_app_unregister(app);
			ao2_cleanup(app);
		}
		ao2_iterator_destroy(&i);
		ao2_cleanup(session->websocket_apps);
		session->websocket_apps = NULL;
	}

	/* Clean up the message_queue container */
	for (j = 0; j < AST_VECTOR_SIZE(&session->message_queue); j++) {
		struct ast_json *msg = AST_VECTOR_GET(&session->message_queue, j);
		ast_json_unref(msg);
	}
	AST_VECTOR_FREE(&session->message_queue);

	/* Remove the handle to the underlying websocket session */
	session->ws_session = NULL;
}

/*!
 * \brief Updates the websocket session for an \ref event_session.
 *
 * \details The websocket for the given \ref event_session will be updated to the value
 *          of the \c ws_session argument.
 *
 *          If the value of the \c ws_session is not \c NULL and there are messages in the
 *          event session's \c message_queue, the messages are dispatched and removed from
 *          the queue.
 *
 * \internal
 *
 * \param session     The event session object to update (\ref event_session).
 * \param ws_session  Handle to the underlying websocket session
 *                    (\ref ast_ari_websocket_session).
 */
static void event_session_update_websocket(
		struct event_session *session, struct ast_ari_websocket_session *ws_session)
{
	int i;

	ast_assert(session != NULL);

	ao2_lock(session);

	session->ws_session = ws_session;

	for (i = 0; i < AST_VECTOR_SIZE(&session->message_queue); i++) {
		struct ast_json *msg = AST_VECTOR_GET(&session->message_queue, i);
		ast_ari_websocket_session_write(session->ws_session, msg);
		ast_json_unref(msg);
	}

	AST_VECTOR_RESET(&session->message_queue, AST_VECTOR_ELEM_CLEANUP_NOOP);
	ao2_unlock(session);
}

/*!
 * \brief Processes cleanup actions for a \ref event_session object.
 *
 * \internal
 *
 * \param session  The event session object to cleanup (\ref event_session).
 */
static void event_session_cleanup(struct event_session *session)
{
	if (!session) {
		return;
	}

	event_session_shutdown(session);
	if (event_session_registry) {
		ao2_unlink(event_session_registry, session);
	}
}

/*!
 * \brief Event session object destructor (\ref event_session).
 *
 * \internal
 *
 * \param obj  Void pointer to the \ref event_session object.
 */
static void event_session_dtor(void *obj)
{
#ifdef AST_DEVMODE /* Avoid unused variable warning */
	struct event_session *session = obj;
#endif

	/* event_session_shutdown should have been called before now */
	ast_assert(session->ws_session == NULL);
	ast_assert(session->websocket_apps == NULL);
	ast_assert(AST_VECTOR_SIZE(&session->message_queue) == 0);
}

/*!
 * \brief Handles \ref event_session error processing.
 *
 * \internal
 *
 * \param session  The \ref event_session object.
 * \param error    The \ref event_session_error_type to handle.
 * \param ser      HTTP TCP/TLS Server Session (\ref ast_tcptls_session_instance).
 *
 * \retval  -1  Always returns -1.
 */
static int event_session_allocation_error_handler(
		struct event_session *session, enum event_session_error_type error,
		struct ast_tcptls_session_instance *ser)
{
	/* Notify the client */
	switch (error) {
	case ERROR_TYPE_STASIS_REGISTRATION:
		ast_http_error(ser, 500, "Internal Server Error",
			"Stasis registration failed");
		break;

	case ERROR_TYPE_OOM:
		ast_http_error(ser, 500, "Internal Server Error",
			"Allocation failed");
		break;

	case ERROR_TYPE_MISSING_APP_PARAM:
		ast_http_error(ser, 400, "Bad Request",
			"HTTP request is missing param: [app]");
		break;

	case ERROR_TYPE_INVALID_APP_PARAM:
		ast_http_error(ser, 400, "Bad Request",
			"Invalid application provided in param [app].");
		break;

	default:
		break;
	}

	/* Cleanup the session */
	event_session_cleanup(session);
	return -1;
}

/*!
 * \brief Creates an \ref event_session object and registers its apps with Stasis.
 *
 * \internal
 *
 * \param ser         HTTP TCP/TLS Server Session (\ref ast_tcptls_session_instance).
 * \param args        The Stasis [app] parameters as parsed from the HTTP request
 *                    (\ref ast_ari_events_event_websocket_args).
 * \param session_id  The id for the websocket session that will be created for this
 *                    event session.
 *
 * \retval  0  on success
 * \retval -1  on failure
 */
static int event_session_alloc(struct ast_tcptls_session_instance *ser,
		struct ast_ari_events_event_websocket_args *args, const char *session_id)
{
	RAII_VAR(struct event_session *, session, NULL, ao2_cleanup);
	int (* register_handler)(const char *, stasis_app_cb handler, void *data);
	size_t size, i;

	/* The request must have at least one [app] parameter */
	if (args->app_count == 0) {
		return event_session_allocation_error_handler(
			session, ERROR_TYPE_MISSING_APP_PARAM, ser);
	}

	size = sizeof(*session) + strlen(session_id) + 1;

	/* Instantiate the event session */
	session = ao2_alloc(size, event_session_dtor);
	if (!session) {
		return event_session_allocation_error_handler(session, ERROR_TYPE_OOM, ser);
	}

	strncpy(session->session_id, session_id, size - sizeof(*session));

	/* Instantiate the hash table for Stasis apps */
	session->websocket_apps =
		ast_str_container_alloc(APPS_NUM_BUCKETS);

	if (!session->websocket_apps) {
		return event_session_allocation_error_handler(session, ERROR_TYPE_OOM, ser);
	}

	/* Instantiate the message queue */
	if (AST_VECTOR_INIT(&session->message_queue, MESSAGES_INIT_SIZE)) {
		return event_session_allocation_error_handler(session, ERROR_TYPE_OOM, ser);
	}

	/* Register the apps with Stasis */
	if (args->subscribe_all) {
		register_handler = &stasis_app_register_all;
	} else {
		register_handler = &stasis_app_register;
	}

	for (i = 0; i < args->app_count; ++i) {
		const char *app = args->app[i];

		if (ast_strlen_zero(app)) {
			return event_session_allocation_error_handler(
				session, ERROR_TYPE_INVALID_APP_PARAM, ser);
		}

		if (ast_str_container_add(session->websocket_apps, app)) {
			return event_session_allocation_error_handler(session, ERROR_TYPE_OOM, ser);
		}

		if (register_handler(app, stasis_app_message_handler, session)) {
			ast_log(LOG_WARNING, "Stasis registration failed for application: '%s'\n", app);
			return event_session_allocation_error_handler(
				session, ERROR_TYPE_STASIS_REGISTRATION, ser);
		}
	}

	/* Add the event session to the local registry */
	if (!ao2_link(event_session_registry, session)) {
		return event_session_allocation_error_handler(session, ERROR_TYPE_OOM, ser);
	}

	return 0;
}

static int event_session_shutdown_cb(void *session, void *arg, int flags)
{
	event_session_cleanup(session);

	return 0;
}

void ast_ari_websocket_events_event_websocket_dtor(void)
{
	ao2_callback(event_session_registry, OBJ_MULTIPLE | OBJ_NODATA, event_session_shutdown_cb, NULL);

	ao2_cleanup(event_session_registry);
	event_session_registry = NULL;
}

int ast_ari_websocket_events_event_websocket_init(void)
{
	/* Try to instantiate the registry */
	event_session_registry = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		EVENT_SESSION_NUM_BUCKETS, event_session_hash, NULL, event_session_compare);
	if (!event_session_registry) {
		/* This is bad, bad. */
		ast_log(LOG_WARNING,
			    "Failed to allocate the local registry for websocket applications\n");
		return -1;
	}

	return 0;
}

int ast_ari_websocket_events_event_websocket_attempted(
		struct ast_tcptls_session_instance *ser, struct ast_variable *headers,
		struct ast_ari_events_event_websocket_args *args, const char *session_id)
{
	ast_debug(3, "/events WebSocket attempted\n");

	/* Create the event session */
	return event_session_alloc(ser, args, session_id);
}

void ast_ari_websocket_events_event_websocket_established(
		struct ast_ari_websocket_session *ws_session, struct ast_variable *headers,
		struct ast_ari_events_event_websocket_args *args)
{
	struct event_session *session;

	struct ast_json *msg;
	const char *session_id;

	ast_debug(3, "/events WebSocket established\n");

	ast_assert(ws_session != NULL);

	session_id = ast_ari_websocket_session_id(ws_session);

	/* Find the event_session and update its websocket  */
	session = ao2_find(event_session_registry, session_id, OBJ_SEARCH_KEY);
	if (session) {
		ao2_unlink(event_session_registry, session);
		event_session_update_websocket(session, ws_session);
	} else {
		ast_log(LOG_WARNING,
			"Failed to locate an event session for the provided websocket session\n");
	}

	/* We don't process any input, but we'll consume it waiting for EOF */
	while ((msg = ast_ari_websocket_session_read(ws_session))) {
		ast_json_unref(msg);
	}

	event_session_cleanup(session);
	ao2_ref(session, -1);
}

void ast_ari_events_user_event(struct ast_variable *headers,
	struct ast_ari_events_user_event_args *args,
	struct ast_ari_response *response)
{
	enum stasis_app_user_event_res res;
	struct ast_json *json_variables = NULL;

	if (args->variables) {
		ast_ari_events_user_event_parse_body(args->variables, args);
		json_variables = ast_json_object_get(args->variables, "variables");
	}

	if (ast_strlen_zero(args->application)) {
		ast_ari_response_error(response, 400, "Bad Request",
			"Missing parameter application");
		return;
	}

	res = stasis_app_user_event(args->application,
		args->event_name,
		args->source, args->source_count,
		json_variables);

	switch (res) {
	case STASIS_APP_USER_OK:
		ast_ari_response_no_content(response);
		break;

	case STASIS_APP_USER_APP_NOT_FOUND:
		ast_ari_response_error(response, 404, "Not Found",
			"Application not found");
		break;

	case STASIS_APP_USER_EVENT_SOURCE_NOT_FOUND:
		ast_ari_response_error(response, 422, "Unprocessable Entity",
			"Event source was not found");
		break;

	case STASIS_APP_USER_EVENT_SOURCE_BAD_SCHEME:
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid event source URI scheme");
		break;

	case STASIS_APP_USER_USEREVENT_INVALID:
		ast_ari_response_error(response, 400, "Bad Request",
			"Invalid userevent data");
		break;

	case STASIS_APP_USER_INTERNAL_ERROR:
	default:
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Error processing request");
	}
}
