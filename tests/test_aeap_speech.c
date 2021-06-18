/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Sangoma Technologies Corporation
 *
 * Kevin Harwell <kharwell@sangoma.com>
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
	<depend>TEST_FRAMEWORK</depend>
	<depend>res_aeap</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/test.h"
#include "asterisk/module.h"
#include "asterisk/file.h"
#include "asterisk/format_cap.h"
#include "asterisk/http.h"
#include "asterisk/http_websocket.h"
#include "asterisk/json.h"
#include "asterisk/speech.h"

#include "asterisk/res_aeap.h"
#include "asterisk/res_aeap_message.h"

#define ADDR "127.0.0.1:8088"

static int speech_test_server_setup(struct ast_json *req, struct ast_json *resp)
{
	struct ast_json *params;

	if (ast_json_object_set(resp, "codecs", ast_json_ref(ast_json_object_get(req, "codecs")))) {
		return -1;
	}

	params = ast_json_object_get(req, "params"); /* Optional */
	if (params && ast_json_object_set(resp, "params", ast_json_ref(params))) {
		return -1;
	}

	return 0;
}

#define TEST_SPEECH_RESULTS_TEXT "foo"
#define TEST_SPEECH_RESULTS_SCORE 7
#define TEST_SPEECH_RESULTS_GRAMMAR "bar"
#define TEST_SPEECH_RESULTS_BEST 1

static int speech_test_server_get(struct ast_json *req, struct ast_json *resp)
{
	const char *param;
	struct ast_json *json = NULL;

	param = ast_json_string_get(ast_json_array_get(ast_json_object_get(req, "params"), 0));
	if (!param) {
		return -1;
	}

	if (!strcmp(param, "results")) {
		json = ast_json_pack("{s:[{s:s,s:i,s:s,s:i}]}",
			param,
			"text", TEST_SPEECH_RESULTS_TEXT,
			"score", TEST_SPEECH_RESULTS_SCORE,
			"grammar", TEST_SPEECH_RESULTS_GRAMMAR,
			"best", TEST_SPEECH_RESULTS_BEST);
	} else {
		/* Assume setting */
		json = ast_json_pack("{s:s}", param, "bar");
	}

	if (!json || ast_json_object_set(resp, "params", json)) {
		return -1;
	}

	return 0;
}

static int speech_test_server_set(struct ast_json *req, struct ast_json *resp)
{
	if (ast_json_object_set(resp, "params", ast_json_ref(ast_json_object_get(req, "params")))) {
		return -1;
	}

	return 0;
}

static int speech_test_server_handle_request(struct ast_websocket *ws, const void *buf, uint64_t size)
{
	struct ast_json *req;
	struct ast_json *resp;
	const char *name;
	char *resp_buf;
	int res = 0;

	req = ast_json_load_buf(buf, size, NULL);
	if (!req) {
		ast_log(LOG_ERROR, "speech test handle request: unable to load json\n");
		return -1;
	}

	name = ast_json_object_string_get(req, "request");
	if (!name) {
		ast_log(LOG_ERROR, "speech test handle request: no name\n");
		ast_json_unref(req);
		return -1;
	}

	resp = ast_json_pack("{s:s, s:s}", "response", name,
		"id", ast_json_object_string_get(req, "id"));
	if (!resp) {
		ast_log(LOG_ERROR, "speech test handle request: unable to create response '%s'\n", name);
		ast_json_unref(req);
		return -1;
	}

	if (!strcmp(name, "setup")) {
		res = speech_test_server_setup(req, resp);
	} else if (!strcmp(name, "get")) {
		res = speech_test_server_get(req, resp);
	} else if (!strcmp(name, "set")) {
		res = speech_test_server_set(req, resp);
	} else {
		ast_log(LOG_ERROR, "speech test handle request: unsupported request '%s'\n", name);
		return -1;
	}

	if (res) {
		ast_log(LOG_ERROR, "speech test handle request: unable to build response '%s'\n", name);
		ast_json_unref(resp);
		ast_json_unref(req);
		return -1;
	}

	resp_buf = ast_json_dump_string(resp);
	ast_json_unref(resp);

	if (!resp_buf) {
		ast_log(LOG_ERROR, "speech test handle request: unable to dump response '%s'\n", name);
		ast_json_unref(req);
		return -1;
	}

	res = ast_websocket_write_string(ws, resp_buf);
	if (res) {
		ast_log(LOG_ERROR, "speech test handle request: unable to write response '%s'\n", name);
	}

	ast_json_unref(req);
	ast_free(resp_buf);

	return res;
}

static void speech_test_server_cb(struct ast_websocket *ws, struct ast_variable *parameters,
	struct ast_variable *headers)
{
	int res;

	if (ast_fd_set_flags(ast_websocket_fd(ws), O_NONBLOCK)) {
		ast_websocket_unref(ws);
		return;
	}

	while ((res = ast_websocket_wait_for_input(ws, -1)) > 0) {
		char *payload;
		uint64_t payload_len;
		enum ast_websocket_opcode opcode;
		int fragmented;

		if (ast_websocket_read(ws, &payload, &payload_len, &opcode, &fragmented)) {
			ast_log(LOG_ERROR, "speech test: Read failure in server loop\n");
			break;
		}

		switch (opcode) {
			case AST_WEBSOCKET_OPCODE_CLOSE:
				ast_websocket_unref(ws);
				return;
			case AST_WEBSOCKET_OPCODE_BINARY:
				ast_websocket_write(ws, opcode, payload, payload_len);
				break;
			case AST_WEBSOCKET_OPCODE_TEXT:
				ast_debug(3, "payload=%.*s\n", (int)payload_len, payload);
				if (speech_test_server_handle_request(ws, payload, payload_len)) {
					ast_websocket_unref(ws);
					return;
				}
				break;
			default:
				break;
		}
	}
	ast_websocket_unref(ws);
}

AST_TEST_DEFINE(res_speech_aeap_test)
{
	RAII_VAR(struct ast_format_cap *, cap, NULL, ao2_cleanup);
	RAII_VAR(struct ast_speech_result *, results, NULL, ast_speech_results_free);
	struct ast_speech *speech = NULL;
	enum ast_test_result_state res = AST_TEST_PASS;
	char buf[8] = "";

	switch (cmd) {
	case TEST_INIT:
		info->name = __func__;
		info->explicit_only = 0;
		info->category = "/res/aeap/speech/";
		info->summary = "test the speech AEAP interface";
		info->description = info->summary;
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_test_validate(test, !ast_websocket_add_protocol("_aeap_test_speech_", speech_test_server_cb));

	ast_test_validate(test, (cap = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT)));
	ast_test_validate(test, !ast_format_cap_update_by_allow_disallow(cap, "ulaw", 1));

	ast_test_validate_cleanup(test, (speech = ast_speech_new("_aeap_test_speech_", cap)), res, cleanup);
	ast_speech_start(speech);
	ast_test_validate_cleanup(test, !ast_speech_dtmf(speech, "1"), res, cleanup);
	ast_test_validate_cleanup(test, !ast_speech_change(speech, "foo", "bar"), res, cleanup);
	ast_test_validate_cleanup(test, !ast_speech_change_results_type(
		speech, AST_SPEECH_RESULTS_TYPE_NBEST), res, cleanup);

	ast_test_validate_cleanup(test, !ast_speech_get_setting(
		speech, "foo", buf, sizeof(buf)), res, cleanup);
	ast_test_validate_cleanup(test, !strcmp(buf, "bar"), res, cleanup);

	ast_test_validate_cleanup(test, (results = ast_speech_results_get(speech)), res, cleanup);
	ast_test_validate_cleanup(test, !strcmp(results->text, TEST_SPEECH_RESULTS_TEXT), res, cleanup);
	ast_test_validate_cleanup(test, results->score == TEST_SPEECH_RESULTS_SCORE, res, cleanup);
	ast_test_validate_cleanup(test, !strcmp(results->grammar, TEST_SPEECH_RESULTS_GRAMMAR), res, cleanup);
	ast_test_validate_cleanup(test, results->nbest_num == TEST_SPEECH_RESULTS_BEST, res, cleanup);

cleanup:
	if (speech) {
		ast_speech_destroy(speech);
	}
	ast_websocket_remove_protocol("_aeap_test_speech_", speech_test_server_cb);

	return res;
}

static struct ast_http_server *http_server;

static int load_module(void)
{
	if (!(http_server = ast_http_test_server_get("aeap transport http server", NULL))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	AST_TEST_REGISTER(res_speech_aeap_test);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(res_speech_aeap_test);

	ast_http_test_server_discard(http_server);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Asterisk External Application Protocol Speech test(s)",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.requires = "res_speech_aeap",
);
