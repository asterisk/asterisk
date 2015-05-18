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

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/stasis_app.h"
#include "resource_events.h"

/*! Number of buckets for the Stasis application hash table. Remember to keep it
 *  a prime number!
 */
#define APPS_NUM_BUCKETS 7

/*! \brief A connection to the event WebSocket */
struct event_session {
	struct ast_ari_websocket_session *ws_session;
	struct ao2_container *websocket_apps;
};

/*!
 * \brief Explicitly shutdown a session.
 *
 * An explicit shutdown is necessary, since stasis-app has a reference to this
 * session. We also need to be sure to null out the \c ws_session field, since
 * the websocket is about to go away.
 *
 * \param session Session info struct.
 */
static void session_shutdown(struct event_session *session)
{
        struct ao2_iterator i;
	char *app;
	SCOPED_AO2LOCK(lock, session);

	i = ao2_iterator_init(session->websocket_apps, 0);
	while ((app = ao2_iterator_next(&i))) {
		stasis_app_unregister(app);
		ao2_cleanup(app);
	}
	ao2_iterator_destroy(&i);
	ao2_cleanup(session->websocket_apps);

	session->websocket_apps = NULL;
	session->ws_session = NULL;
}

static void session_dtor(void *obj)
{
#ifdef AST_DEVMODE /* Avoid unused variable warning */
	struct event_session *session = obj;
#endif

	/* session_shutdown should have been called before */
	ast_assert(session->ws_session == NULL);
	ast_assert(session->websocket_apps == NULL);
}

static void session_cleanup(struct event_session *session)
{
	session_shutdown(session);
	ao2_cleanup(session);
}

static struct event_session *session_create(
	struct ast_ari_websocket_session *ws_session)
{
	RAII_VAR(struct event_session *, session, NULL, ao2_cleanup);

	session = ao2_alloc(sizeof(*session), session_dtor);

	session->ws_session = ws_session;
	session->websocket_apps =
		ast_str_container_alloc(APPS_NUM_BUCKETS);

	if (!session->websocket_apps) {
		return NULL;
	}

	ao2_ref(session, +1);
	return session;
}

/*!
 * \brief Callback handler for Stasis application messages.
 */
static void app_handler(void *data, const char *app_name,
			struct ast_json *message)
{
	struct event_session *session = data;
	int res;
	const char *msg_type = S_OR(
		ast_json_string_get(ast_json_object_get(message, "type")),
		"");
	const char *msg_application = S_OR(
		ast_json_string_get(ast_json_object_get(message, "application")),
		"");

	if (!session) {
		return;
	}
 
	/* Determine if we've been replaced */
	if (strcmp(msg_type, "ApplicationReplaced") == 0 &&
		strcmp(msg_application, app_name) == 0) {
		ao2_find(session->websocket_apps, msg_application,
			OBJ_UNLINK | OBJ_NODATA);
	}

	res = ast_json_object_set(message, "application",
				  ast_json_string_create(app_name));
	if(res != 0) {
		return;
	}

	ao2_lock(session);
	if (session->ws_session) {
		ast_ari_websocket_session_write(session->ws_session, message);
	}
	ao2_unlock(session);
}

/*!
 * \brief Register for all of the apps given.
 * \param session Session info struct.
 * \param app_name Name of application to register.
 */
static int session_register_app(struct event_session *session,
				 const char *app_name)
{
	SCOPED_AO2LOCK(lock, session);

	ast_assert(session->ws_session != NULL);
	ast_assert(session->websocket_apps != NULL);

	if (ast_strlen_zero(app_name)) {
		return -1;
	}

	if (ast_str_container_add(session->websocket_apps, app_name)) {
		ast_ari_websocket_session_write(session->ws_session,
			ast_ari_oom_json());
		return -1;
	}

	stasis_app_register(app_name, app_handler, session);

	return 0;
}

int ast_ari_websocket_events_event_websocket_attempted(struct ast_tcptls_session_instance *ser,
	struct ast_variable *headers,
	struct ast_ari_events_event_websocket_args *args)
{
	int res = 0;
	size_t i, j;

	ast_debug(3, "/events WebSocket attempted\n");

	if (args->app_count == 0) {
		ast_http_error(ser, 400, "Bad Request", "Missing param 'app'");
		return -1;
	}

	for (i = 0; i < args->app_count; ++i) {
		if (ast_strlen_zero(args->app[i])) {
			res = -1;
			break;
		}

		res |= stasis_app_register(args->app[i], app_handler, NULL);
	}

	if (res) {
		for (j = 0; j < i; ++j) {
			stasis_app_unregister(args->app[j]);
		}
		ast_http_error(ser, 400, "Bad Request", "Invalid application provided in param 'app'.");
	}

	return res;
}

void ast_ari_websocket_events_event_websocket_established(struct ast_ari_websocket_session *ws_session,
	struct ast_variable *headers,
	struct ast_ari_events_event_websocket_args *args)
{
	RAII_VAR(struct event_session *, session, NULL, session_cleanup);
	struct ast_json *msg;
	int res;
	size_t i;

	ast_debug(3, "/events WebSocket connection\n");

	session = session_create(ws_session);
	if (!session) {
		ast_ari_websocket_session_write(ws_session, ast_ari_oom_json());
		return;
	}

	res = 0;
	for (i = 0; i < args->app_count; ++i) {
		if (ast_strlen_zero(args->app[i])) {
			continue;
		}
		res |= session_register_app(session, args->app[i]);
	}

	if (ao2_container_count(session->websocket_apps) == 0) {
		RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);

		msg = ast_json_pack("{s: s, s: [s]}",
			"type", "MissingParams",
			"params", "app");
		if (!msg) {
			msg = ast_json_ref(ast_ari_oom_json());
		}

		ast_ari_websocket_session_write(session->ws_session, msg);
		return;
	}

	if (res != 0) {
		ast_ari_websocket_session_write(ws_session, ast_ari_oom_json());
		return;
	}

	/* We don't process any input, but we'll consume it waiting for EOF */
	while ((msg = ast_ari_websocket_session_read(ws_session))) {
		ast_json_unref(msg);
	}
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
			"Invalid userevnet data");
		break;

	case STASIS_APP_USER_INTERNAL_ERROR:
	default:
		ast_ari_response_error(response, 500, "Internal Server Error",
			"Error processing request");
	}
}

