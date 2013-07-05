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

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/astobj2.h"
#include "asterisk/stasis_http.h"

/*! \file
 *
 * \brief WebSocket support for RESTful API's.
 * \author David M. Lee, II <dlee@digium.com>
 */

struct ari_websocket_session {
	struct ast_websocket *ws_session;
	int (*validator)(struct ast_json *);
};

static void websocket_session_dtor(void *obj)
{
	struct ari_websocket_session *session = obj;

	ast_websocket_unref(session->ws_session);
	session->ws_session = NULL;
}

/*!
 * \brief Validator that always succeeds.
 */
static int null_validator(struct ast_json *json)
{
	return 1;
}

struct ari_websocket_session *ari_websocket_session_create(
	struct ast_websocket *ws_session, int (*validator)(struct ast_json *))
{
	RAII_VAR(struct ari_websocket_session *, session, NULL, ao2_cleanup);

	if (ws_session == NULL) {
		return NULL;
	}

	if (validator == NULL) {
		validator = null_validator;
	}

	if (ast_websocket_set_nonblock(ws_session) != 0) {
		ast_log(LOG_ERROR,
			"ARI web socket failed to set nonblock; closing: %s\n",
			strerror(errno));
		return NULL;
	}

	session = ao2_alloc(sizeof(*session), websocket_session_dtor);
	if (!session) {
		return NULL;
	}

	ao2_ref(ws_session, +1);
	session->ws_session = ws_session;
	session->validator = validator;

	ao2_ref(session, +1);
	return session;
}

struct ast_json *ari_websocket_session_read(
	struct ari_websocket_session *session)
{
	RAII_VAR(struct ast_json *, message, NULL, ast_json_unref);

	while (!message) {
		int res;
		char *payload;
		uint64_t payload_len;
		enum ast_websocket_opcode opcode;
		int fragmented;

		res = ast_wait_for_input(
			ast_websocket_fd(session->ws_session), -1);

		if (res <= 0) {
			return NULL;
		}

		res = ast_websocket_read(session->ws_session, &payload,
			&payload_len, &opcode, &fragmented);

		if (res != 0) {
			return NULL;
		}

		switch (opcode) {
		case AST_WEBSOCKET_OPCODE_CLOSE:
			return NULL;
		case AST_WEBSOCKET_OPCODE_TEXT:
			message = ast_json_load_buf(payload, payload_len, NULL);
			break;
		default:
			/* Ignore all other message types */
			break;
		}
	}

	return ast_json_ref(message);
}

#define VALIDATION_FAILED					\
	"{ \"error\": \"Outgoing message failed validation\" }"

int ari_websocket_session_write(struct ari_websocket_session *session,
	struct ast_json *message)
{
	RAII_VAR(char *, str, NULL, ast_free);

#ifdef AST_DEVMODE
	if (!session->validator(message)) {
		ast_log(LOG_ERROR, "Outgoing message failed validation\n");
		return ast_websocket_write(session->ws_session,
			AST_WEBSOCKET_OPCODE_TEXT, VALIDATION_FAILED,
			strlen(VALIDATION_FAILED));
	}
#endif

	str = ast_json_dump_string_format(message, stasis_http_json_format());

	if (str == NULL) {
		ast_log(LOG_ERROR, "Failed to encode JSON object\n");
		return -1;
	}

	return ast_websocket_write(session->ws_session,
		AST_WEBSOCKET_OPCODE_TEXT, str,	strlen(str));
}
