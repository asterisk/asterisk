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
 * \brief HTTP binding for the Stasis API
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<depend type="module">res_stasis</depend>
	<depend type="module">res_http_websocket</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/http_websocket.h"
#include "asterisk/json.h"
#include "asterisk/module.h"
#include "asterisk/stasis_app.h"
#include "asterisk/strings.h"
#include "asterisk/utils.h"

/*! WebSocket protocol for Stasis */
static const char * const ws_protocol = "stasis";

/*! Message to send when out of memory */
static struct ast_json *oom_json;

/*! Number of buckets for the Stasis application hash table. Remember to keep it
 *  a prime number!
 */
#define APPS_NUM_BUCKETS 7

/*!
 * \internal
 * \brief Helper to write a JSON object to a WebSocket.
 * \param session WebSocket session.
 * \param message JSON message.
 * \return 0 on success.
 * \return -1 on error.
 */
static int websocket_write_json(struct ast_websocket *session,
				struct ast_json *message)
{
	RAII_VAR(char *, str, ast_json_dump_string(message), ast_free);

	if (str == NULL) {
		ast_log(LOG_ERROR, "Failed to encode JSON object\n");
		return -1;
	}

	return ast_websocket_write(session, AST_WEBSOCKET_OPCODE_TEXT, str,
				   strlen(str));
}

struct stasis_ws_session_info {
	struct ast_websocket *ws_session;
	struct ao2_container *websocket_apps;
};

static void session_dtor(void *obj)
{
#ifdef AST_DEVMODE /* Avoid unused variable warning */
	struct stasis_ws_session_info *session = obj;
#endif

	/* session_shutdown should have been called before */
	ast_assert(session->ws_session == NULL);
	ast_assert(session->websocket_apps == NULL);
}

static struct stasis_ws_session_info *session_create(
	struct ast_websocket *ws_session)
{
	RAII_VAR(struct stasis_ws_session_info *, session, NULL, ao2_cleanup);

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
 * \brief Explicitly shutdown a session.
 *
 * An explicit shutdown is necessary, since stasis-app has a reference to this
 * session. We also need to be sure to null out the \c ws_session field, since
 * the websocket is about to go away.
 *
 * \param session Session info struct.
 */
static void session_shutdown(struct stasis_ws_session_info *session)
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

/*!
 * \brief Callback handler for Stasis application messages.
 */
static void app_handler(void *data, const char *app_name,
			struct ast_json *message)
{
	struct stasis_ws_session_info *session = data;
	int res;

	res = ast_json_object_set(message, "application",
				  ast_json_string_create(app_name));
	if(res != 0) {
		return;
	}

	ao2_lock(session);
	if (session->ws_session) {
		websocket_write_json(session->ws_session, message);
	}
	ao2_unlock(session);
}

/*!
 * \brief Register for all of the apps given.
 * \param session Session info struct.
 * \param app_list Comma seperated list of app names to register.
 */
static int session_register_apps(struct stasis_ws_session_info *session,
				 const char *app_list)
{
	RAII_VAR(char *, to_free, NULL, ast_free);
	char *apps, *app_name;
	SCOPED_AO2LOCK(lock, session);

	ast_assert(session->ws_session != NULL);
	ast_assert(session->websocket_apps != NULL);

	to_free = apps = ast_strdup(app_list);
	if (!apps) {
		websocket_write_json(session->ws_session, oom_json);
		return -1;
	}
	while ((app_name = strsep(&apps, ","))) {
		if (ast_str_container_add(session->websocket_apps, app_name)) {
			websocket_write_json(session->ws_session, oom_json);
			return -1;
		}

		stasis_app_register(app_name, app_handler, session);
	}
	return 0;
}

static void websocket_callback(struct ast_websocket *ws_session,
			       struct ast_variable *parameters,
			       struct ast_variable *headers)
{
	RAII_VAR(struct stasis_ws_session_info *, stasis_session, NULL, ao2_cleanup);
	struct ast_variable *param = NULL;
	int res;

	ast_debug(3, "Stasis web socket connection\n");

	if (ast_websocket_set_nonblock(ws_session) != 0) {
		ast_log(LOG_ERROR,
			"Stasis web socket failed to set nonblock; closing\n");
		goto end;
	}

	stasis_session = session_create(ws_session);

	if (!stasis_session) {
		websocket_write_json(ws_session, oom_json);
		goto end;
	}

	for (param = parameters; param; param = param->next) {
		if (strcmp(param->name, "app") == 0) {
			int ret = session_register_apps(
				stasis_session, param->value);
			if (ret != 0) {
				goto end;
			}
		}
	}

	if (ao2_container_count(stasis_session->websocket_apps) == 0) {
		RAII_VAR(struct ast_json *, msg, NULL, ast_json_unref);

		msg = ast_json_pack("{s: s, s: [s]}",
				    "error", "MissingParams",
				    "params", "app");
		if (msg) {
			websocket_write_json(ws_session, msg);
		}

		goto end;
	}

	while ((res = ast_wait_for_input(ast_websocket_fd(ws_session), -1)) > 0) {
		char *payload;
		uint64_t payload_len;
		enum ast_websocket_opcode opcode;
		int fragmented;
		int read = ast_websocket_read(ws_session, &payload, &payload_len,
					      &opcode, &fragmented);

		if (read) {
			ast_log(LOG_ERROR,
				"Stasis WebSocket read error; closing\n");
			break;
		}

		if (opcode == AST_WEBSOCKET_OPCODE_CLOSE) {
			break;
		}
	}

end:
	session_shutdown(stasis_session);
	ast_websocket_unref(ws_session);
}

static int load_module(void)
{
	int r = 0;

	stasis_app_ref();
	oom_json = ast_json_pack("{s: s}",
				 "error", "OutOfMemory");
	if (!oom_json) {
		/* ironic */
		return AST_MODULE_LOAD_FAILURE;
	}
	r |= ast_websocket_add_protocol(ws_protocol, websocket_callback);
	return r;
}

static int unload_module(void)
{
	int r = 0;

	stasis_app_unref();
	ast_json_unref(oom_json);
	oom_json = NULL;
	r |= ast_websocket_remove_protocol(ws_protocol, websocket_callback);
	return r;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER,
	"Stasis HTTP bindings",
	.load = load_module,
	.unload = unload_module,
	.nonoptreq = "res_stasis,res_http_websocket",
	.load_pri = AST_MODPRI_APP_DEPEND,
        );
