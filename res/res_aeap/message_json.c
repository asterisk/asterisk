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

#include "asterisk.h"

#include "asterisk/json.h"

#include "asterisk/res_aeap_message.h"

#define JSON_MSG(lvalue, rvalue) struct message_json *lvalue = \
		((struct message_json *)rvalue)

/*!
 * \brief Asterisk external application JSON message type
 */
struct message_json {
	/*! The base message type (must be first) */
	struct ast_aeap_message base;
	/*! Underlying JSON data structure */
	struct ast_json *json;
};

static int message_json_construct1(struct ast_aeap_message *self, const void *params)
{
	JSON_MSG(msg, self);

	msg->json = ast_json_ref((struct ast_json *)params) ?: ast_json_object_create();

	return msg->json ? 0 : -1;
}

static int message_json_construct2(struct ast_aeap_message *self, const char *msg_type,
	const char *name, const char *id, const void *params)
{
	struct ast_json *msg_data;
	int res;

	msg_data = ast_json_pack("{s:s,s:s*}", msg_type, name, "id", id);

	if (!msg_data) {
		ast_log(LOG_ERROR, "AEAP message json: failed to create data for '%s: %s'", msg_type, name);
		return -1;
	}

	if (params && ast_json_object_update(msg_data, (struct ast_json *)params)) {
		ast_log(LOG_ERROR, "AEAP message json: failed to update data for '%s: %s'", msg_type, name);
		ast_json_unref(msg_data);
		return -1;
	}

	res = message_json_construct1(self, msg_data);
	ast_json_unref(msg_data);
	return res;
}

static void message_json_destruct(struct ast_aeap_message *self)
{
	JSON_MSG(msg, self);

	ast_json_unref(msg->json);
}

static int message_json_deserialize(struct ast_aeap_message *self, const void *buf, intmax_t size)
{
	JSON_MSG(msg, self);

	msg->json = ast_json_load_buf(buf, size, NULL);

	return msg->json ? 0 : -1;
}

static int message_json_serialize(const struct ast_aeap_message *self, void **buf, intmax_t *size)
{
	const JSON_MSG(msg, self);

	*buf = ast_json_dump_string(msg->json);
	if (!*buf) {
		*size = 0;
		return -1;
	}

	*size = strlen(*buf);

	return 0;
}

static const char *message_json_id(const struct ast_aeap_message *self)
{
	const JSON_MSG(msg, self);

	return ast_json_object_string_get(msg->json, "id");
}

static int message_json_id_set(struct ast_aeap_message *self, const char *id)
{
	JSON_MSG(msg, self);

	if (ast_json_object_set(msg->json, "id", ast_json_string_create(id))) {
		return -1;
	}

	return 0;
}

static const char *message_json_name(const struct ast_aeap_message *self)
{
	const JSON_MSG(msg, self);
	struct ast_json_iter *iter;

	iter = ast_json_object_iter_at(msg->json, "response");
	if (!iter) {
		iter = ast_json_object_iter_at(msg->json, "request");
	}

	return iter ? ast_json_string_get(ast_json_object_iter_value(iter)) : "";
}

static void *message_json_data(struct ast_aeap_message *self)
{
	JSON_MSG(msg, self);

	return msg->json;
}

static int message_json_is_request(const struct ast_aeap_message *self)
{
	const JSON_MSG(msg, self);

	return ast_json_object_iter_at(msg->json, "request") != NULL;
}

static int message_json_is_response(const struct ast_aeap_message *self)
{
	const JSON_MSG(msg, self);

	return ast_json_object_iter_at(msg->json, "response") != NULL;
}

static const char *message_json_error_msg(const struct ast_aeap_message *self)
{
	const JSON_MSG(msg, self);

	return ast_json_object_string_get(msg->json, "error_msg");
}

static int message_json_error_msg_set(struct ast_aeap_message *self, const char *error_msg)
{
	JSON_MSG(msg, self);

	if (ast_json_object_set(msg->json, "error_msg", ast_json_string_create(error_msg))) {
		return -1;
	}

	return 0;
}

static const struct ast_aeap_message_type message_type_json = {
	.type_size = sizeof(struct message_json),
	.type_name = "json",
	.serial_type = AST_AEAP_DATA_TYPE_STRING,
	.construct1 = message_json_construct1,
	.construct2 = message_json_construct2,
	.destruct = message_json_destruct,
	.deserialize = message_json_deserialize,
	.serialize = message_json_serialize,
	.id = message_json_id,
	.id_set = message_json_id_set,
	.name = message_json_name,
	.data = message_json_data,
	.is_request = message_json_is_request,
	.is_response = message_json_is_response,
	.error_msg = message_json_error_msg,
	.error_msg_set = message_json_error_msg_set,
};

const struct ast_aeap_message_type *ast_aeap_message_type_json = &message_type_json;
