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
#if defined(AST_DEVMODE)
#include "ari_model_validators.h"
#endif
#include "asterisk/app.h"
#include "asterisk/ari.h"
#include "asterisk/astobj2.h"
#include "asterisk/http_websocket.h"
#include "asterisk/module.h"
#include "asterisk/stasis_app.h"


/*! \file
 *
 * \brief WebSocket support for RESTful API's.
 * \author David M. Lee, II <dlee@digium.com>
 */

/*! Number of buckets for the event session registry. Remember to keep it a prime number! */
#define ARI_WS_SESSION_NUM_BUCKETS 23

/*! Number of buckets for a websocket apps container. Remember to keep it a prime number! */
#define APPS_NUM_BUCKETS 7

/*! Initial size of a message queue. */
#define MESSAGES_INIT_SIZE 23


/*! \brief Local registry for created \ref event_session objects. */
static struct ao2_container *ari_ws_session_registry;

struct ast_websocket_server *ast_ws_server;

#define MAX_VALS 128

/*!
 * \brief Validator that always succeeds.
 */
static int null_validator(struct ast_json *json)
{
	return 1;
}

#define VALIDATION_FAILED				\
	"{"						\
	"  \"error\": \"InvalidMessage\","		\
	"  \"message\": \"Message validation failed\""	\
	"}"

static int ari_ws_session_write(
	struct ari_ws_session *ari_ws_session,
	struct ast_json *message)
{
	RAII_VAR(char *, str, NULL, ast_json_free);

#ifdef AST_DEVMODE
	if (!ari_ws_session->validator(message)) {
		ast_log(LOG_ERROR, "Outgoing message failed validation\n");
		return ast_websocket_write_string(ari_ws_session->ast_ws_session, VALIDATION_FAILED);
	}
#endif

	str = ast_json_dump_string_format(message, ast_ari_json_format());

	if (str == NULL) {
		ast_log(LOG_ERROR, "Failed to encode JSON object\n");
		return -1;
	}

	if (ast_websocket_write_string(ari_ws_session->ast_ws_session, str)) {
		ast_log(LOG_NOTICE, "Problem occurred during websocket write to %s, websocket closed\n",
			ast_sockaddr_stringify(ast_websocket_remote_address(ari_ws_session->ast_ws_session)));
		return -1;
	}
	return 0;
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
static int ari_ws_session_update(
	struct ari_ws_session *ari_ws_session,
	struct ast_websocket *ast_ws_session)
{
	RAII_VAR(struct ast_ari_conf *, config, ast_ari_config_get(), ao2_cleanup);
	int i;

	if (ast_ws_session == NULL) {
		return -1;
	}

	if (config == NULL || config->general == NULL) {
		return -1;
	}

	if (ast_websocket_set_nonblock(ast_ws_session) != 0) {
		ast_log(LOG_ERROR,
			"ARI web socket failed to set nonblock; closing: %s\n",
			strerror(errno));
		return -1;
	}

	if (ast_websocket_set_timeout(ast_ws_session, config->general->write_timeout)) {
		ast_log(LOG_WARNING, "Failed to set write timeout %d on ARI web socket\n",
			config->general->write_timeout);
	}

	ao2_ref(ast_ws_session, +1);
	ari_ws_session->ast_ws_session = ast_ws_session;
	ao2_lock(ari_ws_session);
	for (i = 0; i < AST_VECTOR_SIZE(&ari_ws_session->message_queue); i++) {
		struct ast_json *msg = AST_VECTOR_GET(&ari_ws_session->message_queue, i);
		ari_ws_session_write(ari_ws_session, msg);
		ast_json_unref(msg);
	}

	AST_VECTOR_RESET(&ari_ws_session->message_queue, AST_VECTOR_ELEM_CLEANUP_NOOP);
	ao2_unlock(ari_ws_session);

	return 0;
}

static struct ast_json *ari_ws_session_read(
	struct ari_ws_session *ari_ws_session)
{
	RAII_VAR(struct ast_json *, message, NULL, ast_json_unref);

	if (ast_websocket_fd(ari_ws_session->ast_ws_session) < 0) {
		return NULL;
	}

	while (!message) {
		int res;
		char *payload;
		uint64_t payload_len;
		enum ast_websocket_opcode opcode;
		int fragmented;

		res = ast_wait_for_input(
			ast_websocket_fd(ari_ws_session->ast_ws_session), -1);

		if (res <= 0) {
			ast_log(LOG_WARNING, "WebSocket poll error: %s\n",
				strerror(errno));
			return NULL;
		}

		res = ast_websocket_read(ari_ws_session->ast_ws_session, &payload,
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
				ari_websocket_send_event(ari_ws_session, ari_ws_session->app_name,
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
void ari_websocket_send_event(struct ari_ws_session *ari_ws_session,
	const char *app_name, struct ast_json *message, int debug_app)
{
	char *remote_addr = ast_sockaddr_stringify(
			ast_websocket_remote_address(ari_ws_session->ast_ws_session));
	const char *msg_type, *msg_application, *msg_timestamp, *msg_ast_id;
	SCOPE_ENTER(4, "%s: Dispatching message from Stasis app '%s'\n", remote_addr, app_name);

	ast_assert(ari_ws_session != NULL);

	ao2_lock(ari_ws_session);

	msg_type = S_OR(ast_json_string_get(ast_json_object_get(message, "type")), "");
	msg_application = S_OR(
		ast_json_string_get(ast_json_object_get(message, "application")), "");

	/* If we've been replaced, remove the application from our local
	   websocket_apps container */
	if (strcmp(msg_type, "ApplicationReplaced") == 0 &&
		strcmp(msg_application, app_name) == 0) {
		ao2_find(ari_ws_session->websocket_apps, msg_application,
			OBJ_UNLINK | OBJ_NODATA);
	}

	msg_timestamp = S_OR(
		ast_json_string_get(ast_json_object_get(message, "timestamp")), "");
	if (ast_strlen_zero(msg_timestamp)) {
		if (ast_json_object_set(message, "timestamp", ast_json_timeval(ast_tvnow(), NULL))) {
			ao2_unlock(ari_ws_session);
			SCOPE_EXIT_LOG_RTN(LOG_WARNING,
				"%s: Failed to dispatch '%s' message from Stasis app '%s'; could not update message\n",
				remote_addr, msg_type, msg_application);
		}
	}

	msg_ast_id = S_OR(
		ast_json_string_get(ast_json_object_get(message, "asterisk_id")), "");
	if (ast_strlen_zero(msg_ast_id)) {
		char eid[20];

		if (ast_json_object_set(message, "asterisk_id",
			ast_json_string_create(ast_eid_to_str(eid, sizeof(eid), &ast_eid_default)))) {
			ao2_unlock(ari_ws_session);
			SCOPE_EXIT_LOG_RTN(LOG_WARNING,
				"%s: Failed to dispatch '%s' message from Stasis app '%s'; could not update message\n",
				remote_addr, msg_type, msg_application);
		}
	}

	/* Now, we need to determine our state to see how we will handle the message */
	if (ast_json_object_set(message, "application", ast_json_string_create(app_name))) {
		ao2_unlock(ari_ws_session);
		SCOPE_EXIT_LOG_RTN(LOG_WARNING,
			"%s: Failed to dispatch '%s' message from Stasis app '%s'; could not update message\n",
			remote_addr, msg_type, msg_application);
	}

	if (!ari_ws_session) {
		/* If the websocket is NULL, the message goes to the queue */
		if (!AST_VECTOR_APPEND(&ari_ws_session->message_queue, message)) {
			ast_json_ref(message);
		}
		ast_log(LOG_WARNING,
				"%s: Queued '%s' message for Stasis app '%s'; websocket is not ready\n",
				remote_addr,
				msg_type,
				msg_application);
	} else if (stasis_app_event_allowed(app_name, message)) {

		if (TRACE_ATLEAST(4) || debug_app) {
			char *str = ast_json_dump_string_format(message, AST_JSON_PRETTY);

			ast_verbose("<--- Sending ARI event to %s --->\n%s\n",
				remote_addr,
				str);
			ast_json_free(str);
		}

		ari_ws_session_write(ari_ws_session, message);
	}

	ao2_unlock(ari_ws_session);
	SCOPE_EXIT("%s: Dispatched '%s' message from Stasis app '%s'\n",
		remote_addr, msg_type, app_name);
}

static void stasis_app_message_handler(void *data, const char *app_name,
	struct ast_json *message)
{
	int debug_app = stasis_app_get_debug_by_name(app_name);
	struct ari_ws_session *ari_ws_session = data;
	ast_assert(ari_ws_session != NULL);
	ari_websocket_send_event(ari_ws_session, app_name, message, debug_app);
}

static int parse_app_args(struct ast_variable *get_params,
	struct ast_ari_response * response,
	struct ast_ari_events_event_websocket_args *args)
{
	struct ast_variable *i;
	RAII_VAR(char *, app_parse, NULL, ast_free);

	for (i = get_params; i; i = i->next) {
		if (strcmp(i->name, "app") == 0) {
			/* Parse comma separated list */
			char *vals[MAX_VALS];
			size_t j;

			app_parse = ast_strdup(i->value);
			if (!app_parse) {
				ast_ari_response_alloc_failed(response);
				return -1;
			}

			if (strlen(app_parse) == 0) {
				/* ast_app_separate_args can't handle "" */
				args->app_count = 1;
				vals[0] = app_parse;
			} else {
				args->app_count = ast_app_separate_args(
					app_parse, ',', vals,
					ARRAY_LEN(vals));
			}

			if (args->app_count == 0) {
				ast_ari_response_alloc_failed(response);
				return -1;
			}

			if (args->app_count >= MAX_VALS) {
				ast_ari_response_error(response, 400,
					"Bad Request",
					"Too many values for app");
				return -1;
			}

			args->app = ast_malloc(sizeof(*args->app) * args->app_count);
			if (!args->app) {
				ast_ari_response_alloc_failed(response);
				return -1;
			}

			for (j = 0; j < args->app_count; ++j) {
				args->app[j] = (vals[j]);
			}
		} else if (strcmp(i->name, "subscribeAll") == 0) {
			args->subscribe_all = ast_true(i->value);
		}
	}

	args->app_parse = app_parse;
	app_parse = NULL;

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
 */
static void ari_ws_session_reset(struct ari_ws_session *ari_ws_session)
{
	struct ao2_iterator i;
	char *app;
	int j;
	SCOPED_AO2LOCK(lock, ari_ws_session);

	/* Clean up the websocket_apps container */
	if (ari_ws_session->websocket_apps) {
		i = ao2_iterator_init(ari_ws_session->websocket_apps, 0);
		while ((app = ao2_iterator_next(&i))) {
			stasis_app_unregister(app);
			ao2_cleanup(app);
		}
		ao2_iterator_destroy(&i);
		ao2_cleanup(ari_ws_session->websocket_apps);
		ari_ws_session->websocket_apps = NULL;
	}

	/* Clean up the message_queue container */
	for (j = 0; j < AST_VECTOR_SIZE(&ari_ws_session->message_queue); j++) {
		struct ast_json *msg = AST_VECTOR_GET(&ari_ws_session->message_queue, j);
		ast_json_unref(msg);
	}
	AST_VECTOR_FREE(&ari_ws_session->message_queue);
}

/*!
 * \internal
 * \brief RAII_VAR and container ari_ws_session cleanup function.
 * This unlinks the ari_ws_session from the registry and cleans up the
 * decrements the reference count.
 */
static void ari_ws_session_cleanup(struct ari_ws_session *ari_ws_session)
{
	if (!ari_ws_session) {
		return;
	}

	ari_ws_session_reset(ari_ws_session);
	if (ari_ws_session_registry) {
		ao2_unlink(ari_ws_session_registry, ari_ws_session);
	}
	ao2_ref(ari_ws_session, -1);
}

/*!
 * \internal
 * \brief The ao2 destructor.
 * This cleans up the reference to the parent ast_websocket.
 */
static void ari_ws_session_dtor(void *obj)
{
	struct ari_ws_session *ari_ws_session = obj;

	ast_free(ari_ws_session->app_name);
	if (!ari_ws_session->ast_ws_session) {
		return;
	}
	ast_websocket_unref(ari_ws_session->ast_ws_session);
	ari_ws_session->ast_ws_session = NULL;
}

static int ari_ws_session_create(
	int (*validator)(struct ast_json *),
	struct ast_tcptls_session_instance *ser,
	struct ast_ari_events_event_websocket_args *args,
	const char *session_id)
{
	RAII_VAR(struct ari_ws_session *, ari_ws_session, NULL, ao2_cleanup);
	int (* register_handler)(const char *, stasis_app_cb handler, void *data);
	size_t size, i;

	if (validator == NULL) {
		validator = null_validator;
	}

	size = sizeof(*ari_ws_session) + strlen(session_id) + 1;

	ari_ws_session = ao2_alloc(size, ari_ws_session_dtor);
	if (!ari_ws_session) {
		return -1;
	}

	ari_ws_session->app_name = ast_strdup(args->app_parse);
	if (!ari_ws_session->app_name) {
		ast_http_error(ser, 500, "Internal Server Error",
			"Allocation failed");
		return -1;
	}

	strcpy(ari_ws_session->session_id, session_id); /* Safe */

	/* Instantiate the hash table for Stasis apps */
	ari_ws_session->websocket_apps =
		ast_str_container_alloc(APPS_NUM_BUCKETS);
	if (!ari_ws_session->websocket_apps) {
		ast_http_error(ser, 500, "Internal Server Error",
			"Allocation failed");
		return -1;
	}

	/* Instantiate the message queue */
	if (AST_VECTOR_INIT(&ari_ws_session->message_queue, MESSAGES_INIT_SIZE)) {
		ast_http_error(ser, 500, "Internal Server Error",
			"Allocation failed");
		ao2_cleanup(ari_ws_session->websocket_apps);
		return -1;
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
			ast_http_error(ser, 400, "Bad Request",
				"Invalid application provided in param [app].");
			ari_ws_session_reset(ari_ws_session);
			return -1;
		}

		if (ast_str_container_add(ari_ws_session->websocket_apps, app)) {
			ast_http_error(ser, 500, "Internal Server Error",
				"Allocation failed");
			ari_ws_session_reset(ari_ws_session);
			return -1;
		}

		if (register_handler(app, stasis_app_message_handler, ari_ws_session)) {
			ast_log(LOG_WARNING, "Stasis registration failed for application: '%s'\n", app);
			ast_http_error(ser, 500, "Internal Server Error",
				"Stasis registration failed");
			ari_ws_session_reset(ari_ws_session);
			return -1;
		}
	}

	ari_ws_session->validator = validator;

	/*
	 * Add the event session to the session registry.
	 * When this functions returns, the registry will have
	 * the only reference to the session.
	 */
	if (!ao2_link(ari_ws_session_registry, ari_ws_session)) {
		ast_http_error(ser, 500, "Internal Server Error",
			"Allocation failed");
		ari_ws_session_reset(ari_ws_session);
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief This function gets called before the upgrade process is completed.
 * HTTP is still in effect.
 */
static int websocket_attempted_cb(struct ast_tcptls_session_instance *ser,
	struct ast_variable *get_params, struct ast_variable *headers,
	const char *session_id)
{
	struct ast_ari_events_event_websocket_args args = {};
	int res = 0;
	RAII_VAR(struct ast_ari_response *, response, NULL, ast_free);
	char *remote_addr = ast_sockaddr_stringify(&ser->remote_address);

	response = ast_calloc(1, sizeof(*response));
	if (!response) {
		ast_log(LOG_ERROR, "Failed to create response.\n");
		ast_http_error(ser, 500, "Server Error", "Memory allocation error");
		return -1;
	}

	res = parse_app_args(get_params, response, &args);
	if (res != 0) {
		/* Param parsing failure */
		RAII_VAR(char *, msg, NULL, ast_json_free);
		if (response->message) {
			msg = ast_json_dump_string(response->message);
		} else {
			ast_log(LOG_ERROR, "Missing response message\n");
		}

		if (msg) {
			ast_http_error(ser, response->response_code, response->response_text, msg);
			return -1;
		}
	}

	if (args.app_count == 0) {
		ast_http_error(ser, 400, "Bad Request",
			"HTTP request is missing param: [app]");
		return -1;
	}

#if defined(AST_DEVMODE)
	res = ari_ws_session_create(ast_ari_validate_message_fn(),
		ser, &args, session_id);
#else
	res = ari_ws_session_create(NULL, ser, &args, session_id);
#endif
	if (res != 0) {
		ast_log(LOG_ERROR,
			"%s: Failed to create ARI ari_session\n", remote_addr);
	}

	ast_free(args.app_parse);
	ast_free(args.app);
	return res;
}

/*!
 * \internal
 * \brief This function gets called after the upgrade process is completed.
 * The websocket is now in effect.
 */
static void websocket_established_cb(struct ast_websocket *ast_ws_session,
	struct ast_variable *get_params, struct ast_variable *upgrade_headers)
{
	RAII_VAR(struct ast_ari_response *, response, NULL, ast_free);
	/*
	 * ast_ws_session is passed in with it's refcount bumped so
	 * we need to unref it when we're done.  The refcount will
	 * be bumped again when we add it to the ari_ws_session.
	 */
	RAII_VAR(struct ast_websocket *, s, ast_ws_session, ast_websocket_unref);
	RAII_VAR(struct ari_ws_session *, ari_ws_session, NULL, ari_ws_session_cleanup);
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

	response = ast_calloc(1, sizeof(*response));
	if (!response) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR,
			"%s: Failed to create response\n", remote_addr);
	}

	/* Find the event_session and update its websocket  */
	ari_ws_session = ao2_find(ari_ws_session_registry, session_id, OBJ_SEARCH_KEY);
	if (ari_ws_session) {
		ao2_unlink(ari_ws_session_registry, ari_ws_session);
		ari_ws_session_update(ari_ws_session, ast_ws_session);
	} else {
		 SCOPE_EXIT_LOG_RTN(LOG_ERROR,
			"%s: Failed to locate an event session for the websocket session\n",
			remote_addr);
	}

	ast_trace(-1, "%s: Waiting for messages\n", remote_addr);
	while ((msg = ari_ws_session_read(ari_ws_session))) {
		ari_websocket_process_request(ari_ws_session, remote_addr,
			upgrade_headers, ari_ws_session->app_name, msg);
		ast_json_unref(msg);
	}

	SCOPE_EXIT("%s: Websocket closed\n", remote_addr);
}

static int ari_ws_session_shutdown_cb(void *ari_ws_session, void *arg, int flags)
{
	ari_ws_session_cleanup(ari_ws_session);

	return 0;
}

static void ari_ws_session_registry_dtor(void)
{
	if (!ari_ws_session_registry) {
		return;
	}

	ao2_callback(ari_ws_session_registry, OBJ_MULTIPLE | OBJ_NODATA,
		ari_ws_session_shutdown_cb, NULL);

	ao2_cleanup(ari_ws_session_registry);
	ari_ws_session_registry = NULL;
}

int ari_websocket_unload_module(void)
{
	ari_ws_session_registry_dtor();
	ao2_cleanup(ast_ws_server);
	ast_ws_server = NULL;
	return 0;
}

AO2_STRING_FIELD_CMP_FN(ari_ws_session, session_id);
AO2_STRING_FIELD_HASH_FN(ari_ws_session, session_id);

int ari_websocket_load_module(void)
{
	int res = 0;
	struct ast_websocket_protocol *protocol;

	ari_ws_session_registry = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		ARI_WS_SESSION_NUM_BUCKETS, ari_ws_session_hash_fn,
		NULL, ari_ws_session_cmp_fn);
	if (!ari_ws_session_registry) {
		ast_log(LOG_WARNING,
			    "Failed to allocate the local registry for websocket applications\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_ws_server = ast_websocket_server_create();
	if (!ast_ws_server) {
		ari_ws_session_registry_dtor();
		return AST_MODULE_LOAD_DECLINE;
	}

	protocol = ast_websocket_sub_protocol_alloc("ari");
	if (!protocol) {
		ao2_ref(ast_ws_server, -1);
		ast_ws_server = NULL;
		ari_ws_session_registry_dtor();
		return AST_MODULE_LOAD_DECLINE;
	}
	protocol->session_attempted = websocket_attempted_cb;
	protocol->session_established = websocket_established_cb;
	res = ast_websocket_server_add_protocol2(ast_ws_server, protocol);

	return res == 0 ? AST_MODULE_LOAD_SUCCESS : AST_MODULE_LOAD_DECLINE;
}

