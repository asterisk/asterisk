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

/*! \file
 *
 * \brief Asterisk External Application Speech Engine
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/config.h"
#include "asterisk/format.h"
#include "asterisk/format_cap.h"
#include "asterisk/json.h"
#include "asterisk/module.h"
#include "asterisk/speech.h"
#include "asterisk/sorcery.h"

#include "asterisk/res_aeap.h"
#include "asterisk/res_aeap_message.h"

#define SPEECH_AEAP_VERSION "0.1.0"
#define SPEECH_PROTOCOL "speech_to_text"

#define CONNECTION_TIMEOUT 2000

#define log_error(obj, fmt, ...) \
	ast_log(LOG_ERROR, "AEAP speech (%p): " fmt "\n", obj, ##__VA_ARGS__)

static struct ast_json *custom_fields_to_params(const struct ast_variable *variables)
{
	const struct ast_variable *i;
	struct ast_json *obj;

	if (!variables) {
		return NULL;
	}

	obj = ast_json_object_create();
	if (!obj) {
		return NULL;
	}

	for (i = variables; i; i = i->next) {
		if (i->name[0] == '@' && i->name[1]) {
			ast_json_object_set(obj, i->name + 1, ast_json_string_create(i->value));
		}
	}

	return obj;
}

/*!
 * \internal
 * \brief Create, and send a request to the external application
 *
 * Create, then sends a request to an Asterisk external application, and then blocks
 * until a response is received or a time out occurs. Since this method waits until
 * receiving a response the returned result is guaranteed to be pass/fail based upon
 * a response handler's result.
 *
 * \param aeap Pointer to an Asterisk external application object
 * \param name The name of the request to send
 * \param json The core json request data
 * \param data Optional user data to associate with request/response
 *
 * \returns 0 on success, -1 on error
 */
static int speech_aeap_send_request(struct ast_aeap *aeap, const char *name,
	struct ast_json *json, void *data)
{
	/*
	 * Wait for a response. Also since we're blocking,
	 * data is expected to be on the stack so no cleanup required.
	 */
	struct ast_aeap_tsx_params tsx_params = {
		.timeout = 1000,
		.wait = 1,
		.obj = data,
	};

	/* "steals" the json ref */
	tsx_params.msg = ast_aeap_message_create_request(
		ast_aeap_message_type_json, name, NULL, json);
	if (!tsx_params.msg) {
		return -1;
	}

	/* Send "steals" the json msg ref */
	return ast_aeap_send_msg_tsx(aeap, &tsx_params);
}

/*!
 * \internal
 * \brief Create, and send a "get" request to an external application
 *
 * Basic structure of the JSON message to send:
 *
 \verbatim
 { param: [<param>, ...] }
 \endverbatim
 *
 * \param speech The speech engine
 * \param param The name of the parameter to retrieve
 * \param data User data passed to the response handler
 *
 * \returns 0 on success, -1 on error
 */
static int speech_aeap_get(struct ast_speech *speech, const char *param, void *data)
{
	if (!param) {
		return -1;
	}

	/* send_request handles json ref */
	return speech_aeap_send_request(speech->data,
		"get", ast_json_pack("{s:[s]}", "params", param), data);
}

struct speech_param {
	const char *name;
	const char *value;
};

/*!
 * \internal
 * \brief Create, and send a "set" request to an external application
 *
 * Basic structure of the JSON message to send:
 *
 \verbatim
 { params: { <name> : <value> }  }
 \endverbatim
 *
 * \param speech The speech engine
 * \param name The name of the parameter to set
 * \param value The value of the parameter to set
 *
 * \returns 0 on success, -1 on error
 */
static int speech_aeap_set(struct ast_speech *speech, const char *name, const char *value)
{
	if (!name) {
		return -1;
	}

	/* send_request handles json ref */
	return speech_aeap_send_request(speech->data,
		"set", ast_json_pack("{s:{s:s}}", "params", name, value), NULL);
}

static int handle_response_set(struct ast_aeap *aeap, struct ast_aeap_message *message, void *data)
{
	return 0;
}

struct speech_setting {
	const char *param;
	size_t len;
	char *buf;
};

static int handle_setting(struct ast_aeap *aeap, struct ast_json_iter *iter,
	struct speech_setting *setting)
{
	const char *value;

	if (strcmp(ast_json_object_iter_key(iter), setting->param)) {
		log_error(aeap, "Unable to 'get' speech setting for '%s'", setting->param);
		return -1;
	}

	value = ast_json_string_get(ast_json_object_iter_value(iter));
	if (!value) {
		log_error(aeap, "No value for speech setting '%s'", setting->param);
		return -1;
	}

	ast_copy_string(setting->buf, value, setting->len);
	return 0;
}

static int handle_results(struct ast_aeap *aeap, struct ast_json_iter *iter,
	struct ast_speech_result **speech_results)
{
	struct ast_speech_result *result = NULL;
	struct ast_json *json_results;
	struct ast_json *json_result;
	size_t i;

	json_results = ast_json_object_iter_value(iter);
	if (!json_results || !speech_results) {
		log_error(aeap, "Unable to 'get' speech results");
		return -1;
	}

	for (i = 0; i < ast_json_array_size(json_results); ++i) {
		if (!(result = ast_calloc(1, sizeof(*result)))) {
			continue;
		}

		json_result = ast_json_array_get(json_results, i);

		result->text = ast_strdup(ast_json_object_string_get(json_result, "text"));
		result->score = ast_json_object_integer_get(json_result, "score");
		result->grammar = ast_strdup(ast_json_object_string_get(json_result, "grammar"));
		result->nbest_num = ast_json_object_integer_get(json_result, "best");
		if (*speech_results) {
			AST_LIST_NEXT(result, list) = *speech_results;
			*speech_results = result;
		} else {
			*speech_results = result;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief Handle a "get" response from an external application
 *
 * Basic structure of the expected JSON message to received:
 *
 \verbatim
 {
   response: "get"
   "params" : { <name>: <value> | [ <results> ] }
 }
 \endverbatim
 *
 * \param aeap Pointer to an Asterisk external application object
 * \param message The received message
 * \param data User data passed to the response handler
 *
 * \returns 0 on success, -1 on error
 */
static int handle_response_get(struct ast_aeap *aeap, struct ast_aeap_message *message, void *data)
{
	struct ast_json_iter *iter;

	iter = ast_json_object_iter(ast_json_object_get(ast_aeap_message_data(message), "params"));
	if (!iter) {
		log_error(aeap, "no 'get' parameters returned");
		return -1;
	}

	if (!strcmp(ast_json_object_iter_key(iter), "results")) {
		return handle_results(aeap, iter, data);
	}

	return handle_setting(aeap, iter, data);
}

static int handle_response_setup(struct ast_aeap *aeap, struct ast_aeap_message *message, void *data)
{
	struct ast_format *format = data;
	struct ast_json *json = ast_aeap_message_data(message);
	const char *codec_name;

	if (!format) {
		log_error(aeap, "no 'format' set");
		return -1;
	}

	if (!json) {
		log_error(aeap, "no 'setup' object returned");
		return -1;
	}

	json = ast_json_object_get(json, "codecs");
	if (!json || ast_json_array_size(json) == 0) {
		log_error(aeap, "no 'setup' codecs available");
		return -1;
	}

	codec_name = ast_json_object_string_get(ast_json_array_get(json, 0), "name");
	if (!codec_name || strcmp(codec_name, ast_format_get_codec_name(format))) {
		log_error(aeap, "setup  codec '%s' unsupported", ast_format_get_codec_name(format));
		return -1;
	}

	return 0;
}

static const struct ast_aeap_message_handler response_handlers[] = {
	{ "setup", handle_response_setup },
	{ "get", handle_response_get },
	{ "set", handle_response_set },
};

static int handle_request_set(struct ast_aeap *aeap, struct ast_aeap_message *message, void *data)
{
	struct ast_json_iter *iter;
	const char *error_msg = NULL;

	iter = ast_json_object_iter(ast_json_object_get(ast_aeap_message_data(message), "params"));
	if (!iter) {
		error_msg = "no parameter(s) requested";
	} else if (!strcmp(ast_json_object_iter_key(iter), "results")) {
		struct ast_speech *speech = ast_aeap_user_data_object_by_id(aeap, "speech");

		if (!speech) {
			error_msg = "no associated speech object";
		} else if (handle_results(aeap, iter, &speech->results)) {
			error_msg = "unable to handle results";
		} else {
			ast_speech_change_state(speech, AST_SPEECH_STATE_DONE);
		}
	} else {
		error_msg = "can only set 'results'";
	}

	if (error_msg) {
		log_error(aeap, "set - %s", error_msg);
		message = ast_aeap_message_create_error(ast_aeap_message_type_json,
			ast_aeap_message_name(message), ast_aeap_message_id(message), error_msg);
	} else {
		message = ast_aeap_message_create_response(ast_aeap_message_type_json,
			ast_aeap_message_name(message), ast_aeap_message_id(message), NULL);
	}

	ast_aeap_send_msg(aeap, message);

	return 0;
}

static const struct ast_aeap_message_handler request_handlers[] = {
	{ "set", handle_request_set },
};

static struct ast_aeap_params speech_aeap_params = {
	.response_handlers = response_handlers,
	.response_handlers_size = ARRAY_LEN(response_handlers),
	.request_handlers = request_handlers,
	.request_handlers_size = ARRAY_LEN(request_handlers),
};

/*!
 * \internal
 * \brief Create, and connect to an external application and send initial setup
 *
 * Basic structure of the JSON message to send:
 *
 \verbatim
 {
   "request": "setup"
   "codecs": [
       {
           "name": <name>,
           "attributes": { <name>: <value>, ..., }
       },
       ...,
   ],
   "params": { <name>: <value>, ..., }
 }
 \endverbatim
 *
 * \param speech The speech engine
 * \param format The format codec to use
 *
 * \returns 0 on success, -1 on error
 */
static int speech_aeap_engine_create(struct ast_speech *speech, struct ast_format *format)
{
	struct ast_aeap *aeap;
	struct ast_variable *vars;
	struct ast_json *json;

	aeap = ast_aeap_create_and_connect_by_id(
		speech->engine->name, &speech_aeap_params, CONNECTION_TIMEOUT);
	if (!aeap) {
		return -1;
	}

	speech->data = aeap;

	/* Don't allow unloading of this module while an external application is in use */
	ast_module_ref(ast_module_info->self);

	vars = ast_aeap_custom_fields_get(speech->engine->name);

	/* While the protocol allows sending of codec attributes, for now don't */
	json = ast_json_pack("{s:s,s:[{s:s}],s:o*}", "version", SPEECH_AEAP_VERSION, "codecs",
		"name", ast_format_get_codec_name(format), "params", custom_fields_to_params(vars));

	ast_variables_destroy(vars);

	if (ast_aeap_user_data_register(aeap, "speech", speech, NULL)) {
		ast_module_unref(ast_module_info->self);
		return -1;
	}

	/* send_request handles json ref */
	if (speech_aeap_send_request(speech->data, "setup", json, format)) {
		ast_module_unref(ast_module_info->self);
		return -1;
	}

	/*
	 * Add a reference to the engine here, so if it happens to get unregistered
	 * while executing it won't disappear.
	 */
	ao2_ref(speech->engine, 1);

	return 0;
}

static int speech_aeap_engine_destroy(struct ast_speech *speech)
{
	ao2_ref(speech->engine, -1);
	ao2_cleanup(speech->data);

	ast_module_unref(ast_module_info->self);

	return 0;
}

static int speech_aeap_engine_write(struct ast_speech *speech, void *data, int len)
{
	return ast_aeap_send_binary(speech->data, data, len);
}

static int speech_aeap_engine_dtmf(struct ast_speech *speech, const char *dtmf)
{
	return speech_aeap_set(speech, "dtmf", dtmf);
}

static int speech_aeap_engine_start(struct ast_speech *speech)
{
	ast_speech_change_state(speech, AST_SPEECH_STATE_READY);

	return 0;
}

static int speech_aeap_engine_change(struct ast_speech *speech, const char *name, const char *value)
{
	return speech_aeap_set(speech, name, value);
}

static int speech_aeap_engine_get_setting(struct ast_speech *speech, const char *name,
	char *buf, size_t len)
{
	struct speech_setting setting = {
		.param = name,
		.len = len,
		.buf = buf,
	};

	return speech_aeap_get(speech, name, &setting);
}

static int speech_aeap_engine_change_results_type(struct ast_speech *speech,
	enum ast_speech_results_type results_type)
{
	return speech_aeap_set(speech, "results_type",
		ast_speech_results_type_to_string(results_type));
}

static struct ast_speech_result *speech_aeap_engine_get(struct ast_speech *speech)
{
	struct ast_speech_result *results = NULL;

	if (speech->results) {
		return speech->results;
	}

	if (speech_aeap_get(speech, "results", &results)) {
		return NULL;
	}

	return results;
}

static void speech_engine_destroy(void *obj)
{
	struct ast_speech_engine *engine = obj;

	ao2_cleanup(engine->formats);
	ast_free(engine->name);
}

static struct ast_speech_engine *speech_engine_alloc(const char *name)
{
	struct ast_speech_engine *engine;

	engine = ao2_t_alloc_options(sizeof(*engine), speech_engine_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK, name);
	if (!engine) {
		ast_log(LOG_ERROR, "AEAP speech: unable create engine '%s'\n", name);
		return NULL;
	}

	engine->name = ast_strdup(name);
	if (!engine->name) {
		ao2_ref(engine, -1);
		return NULL;
	}

	engine->create = speech_aeap_engine_create;
	engine->destroy = speech_aeap_engine_destroy;
	engine->write = speech_aeap_engine_write;
	engine->dtmf = speech_aeap_engine_dtmf;
	engine->start = speech_aeap_engine_start;
	engine->change = speech_aeap_engine_change;
	engine->get_setting = speech_aeap_engine_get_setting;
	engine->change_results_type = speech_aeap_engine_change_results_type;
	engine->get = speech_aeap_engine_get;

	engine->formats = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);

	return engine;
}

static void speech_engine_alloc_and_register(const char *name, const struct ast_format_cap *formats)
{
	struct ast_speech_engine *engine;

	engine = speech_engine_alloc(name);
	if (!engine) {
		return;
	}

	if (formats && ast_format_cap_append_from_cap(engine->formats,
			formats, AST_MEDIA_TYPE_AUDIO)) {
		ast_log(LOG_WARNING, "AEAP speech: Unable to add engine '%s' formats\n", name);
		ao2_ref(engine, -1);
		return;
	}

	if (ast_speech_register(engine)) {
		ast_log(LOG_WARNING, "AEAP speech: Unable to register engine '%s'\n", name);
		ao2_ref(engine, -1);
	}
}

#ifdef TEST_FRAMEWORK

static void speech_engine_alloc_and_register2(const char *name, const char *codec_names)
{
	struct ast_speech_engine *engine;

	engine = speech_engine_alloc(name);
	if (!engine) {
		return;
	}

	if (codec_names && ast_format_cap_update_by_allow_disallow(engine->formats, codec_names, 1)) {
		ast_log(LOG_WARNING, "AEAP speech: Unable to add engine '%s' codecs\n", name);
		ao2_ref(engine, -1);
		return;
	}

	if (ast_speech_register(engine)) {
		ast_log(LOG_WARNING, "AEAP speech: Unable to register engine '%s'\n", name);
		ao2_ref(engine, -1);
	}
}

#endif

static int unload_engine(void *obj, void *arg, int flags)
{
	if (ast_aeap_client_config_has_protocol(obj, SPEECH_PROTOCOL)) {
		ao2_cleanup(ast_speech_unregister2(ast_sorcery_object_get_id(obj)));
	}

	return 0;
}

static int load_engine(void *obj, void *arg, int flags)
{
	const char *id;
	const struct ast_format_cap *formats;
	const struct ast_speech_engine *engine;

	if (!ast_aeap_client_config_has_protocol(obj, SPEECH_PROTOCOL)) {
		return 0;
	}

	id = ast_sorcery_object_get_id(obj);
	formats = ast_aeap_client_config_codecs(obj);
	if (!formats) {
		formats = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!formats) {
			ast_log(LOG_ERROR, "AEAP speech: unable to allocate default engine format for '%s'\n", id);
			return 0;
		}
	}

	engine = ast_speech_find_engine(id);
	if (!engine) {
		speech_engine_alloc_and_register(id, formats);
		return 0;
	}

	if (ast_format_cap_identical(formats, engine->formats)) {
		/* Same name, same formats then nothing changed */
		return 0;
	}

	ao2_ref(ast_speech_unregister2(engine->name), -1);
	speech_engine_alloc_and_register(id, formats);

	return 0;
}

static int matches_engine(void *obj, void *arg, int flags)
{
	const struct ast_speech_engine *engine = arg;

	return strcmp(ast_sorcery_object_get_id(obj), engine->name) ? 0 : CMP_MATCH;
}

static int should_unregister(const struct ast_speech_engine *engine, void *data)
{
	void *obj;

	if (engine->create != speech_aeap_engine_create) {
		/* Only want to potentially unregister AEAP speech engines */
		return 0;
	}

#ifdef TEST_FRAMEWORK
	if (!strcmp("_aeap_test_speech_", engine->name)) {
		/* Don't remove the test engine */
		return 0;
	}
#endif

	obj = ao2_callback(data, 0, matches_engine, (void*)engine);

	if (obj) {
		ao2_ref(obj, -1);
		return 0;
	}

	/* If no match in given container then unregister engine */
	return 1;
}

static void speech_observer_loaded(const char *object_type)
{
	struct ao2_container *container;

	if (strcmp(object_type, AEAP_CONFIG_CLIENT)) {
		return;
	}

	container = ast_aeap_client_configs_get(SPEECH_PROTOCOL);
	if (!container) {
		return;
	}

	/*
	 * An AEAP module reload has occurred. First
	 * remove all engines that no longer exist.
	 */
	ast_speech_unregister_engines(should_unregister, container, __ao2_cleanup);

	/* Now add or update engines */
	ao2_callback(container, 0, load_engine, NULL);
	ao2_ref(container, -1);
}

/*! \brief Observer for AEAP reloads */
static const struct ast_sorcery_observer speech_observer = {
	.loaded = speech_observer_loaded,
};

static int unload_module(void)
{
	struct ao2_container *container;

#ifdef TEST_FRAMEWORK
	ao2_cleanup(ast_speech_unregister2("_aeap_test_speech_"));
#endif

	ast_sorcery_observer_remove(ast_aeap_sorcery(), AEAP_CONFIG_CLIENT, &speech_observer);

	container = ast_aeap_client_configs_get(SPEECH_PROTOCOL);
	if (container) {
		ao2_callback(container, 0, unload_engine, NULL);
		ao2_ref(container, -1);
	}

	return 0;
}

static int load_module(void)
{
	struct ao2_container *container;

	speech_aeap_params.msg_type = ast_aeap_message_type_json;

	container = ast_aeap_client_configs_get(SPEECH_PROTOCOL);
	if (container) {
		ao2_callback(container, 0, load_engine, NULL);
		ao2_ref(container, -1);
	}

	/*
	 * Add an observer since a named speech server must be created,
	 * registered, and eventually removed for all AEAP client
	 * configuration matching the "speech_to_text" protocol.
	*/
	if (ast_sorcery_observer_add(ast_aeap_sorcery(), AEAP_CONFIG_CLIENT, &speech_observer)) {
		return AST_MODULE_LOAD_DECLINE;
	}

#ifdef TEST_FRAMEWORK
	speech_engine_alloc_and_register2("_aeap_test_speech_", "ulaw");
#endif

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Asterisk External Application Speech Engine",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
	.requires = "res_speech,res_aeap",
);
