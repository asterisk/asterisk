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

#include "asterisk/astobj2.h"
#include "asterisk/utils.h"
#include "asterisk/uuid.h"
#include "asterisk/res_aeap.h"
#include "asterisk/res_aeap_message.h"

enum AST_AEAP_DATA_TYPE ast_aeap_message_serial_type(const struct ast_aeap_message_type *type)
{
	ast_assert(type != NULL);

	return type->serial_type;
}

static void message_destructor(void *obj)
{
	struct ast_aeap_message *msg = obj;

	if (msg->type->destruct) {
		msg->type->destruct(msg);
	}
}

static struct ast_aeap_message *message_create(const struct ast_aeap_message_type *type)
{
	struct ast_aeap_message *msg;

	msg = ao2_t_alloc_options(type->type_size, message_destructor,
		AO2_ALLOC_OPT_LOCK_NOLOCK, type->type_name);
	if (!msg) {
		ast_log(LOG_ERROR, "AEAP message %s: unable to create\n", type->type_name);
		return NULL;
	}

	msg->type = type;

	return msg;
}

struct ast_aeap_message *ast_aeap_message_create1(const struct ast_aeap_message_type *type,
	const void *params)
{
	struct ast_aeap_message *msg;

	ast_assert(type != NULL);
	ast_assert(type->construct1 != NULL);

	msg = message_create(type);
	if (!msg) {
		return NULL;
	}

	if (type->construct1(msg, params)) {
		ast_log(LOG_ERROR, "AEAP message %s: unable to construct1\n", type->type_name);
		ao2_ref(msg, -1);
		return NULL;
	}

	return msg;
}

struct ast_aeap_message *ast_aeap_message_create2(const struct ast_aeap_message_type *type,
	const char *msg_type, const char *name, const char *id, const void *params)
{
	struct ast_aeap_message *msg;

	ast_assert(type != NULL);
	ast_assert(type->construct2 != NULL);
	ast_assert(msg_type != NULL);
	ast_assert(name != NULL);

	msg = message_create(type);
	if (!msg) {
		return NULL;
	}

	if (type->construct2(msg, msg_type, name, id, params)) {
		ast_log(LOG_ERROR, "AEAP message %s: unable to construct2\n", type->type_name);
		ao2_ref(msg, -1);
		return NULL;
	}

	return msg;
}

struct ast_aeap_message *ast_aeap_message_create_request(const struct ast_aeap_message_type *type,
	const char *name, const char *id, const void *params)
{
	struct ast_aeap_message *msg;

	msg = ast_aeap_message_create2(type, "request", name, id, params);
	if (!msg) {
		return NULL;
	}

	if (!id && !ast_aeap_message_id_generate(msg)) {
		ao2_ref(msg, -1);
		return NULL;
	}

	return msg;
}

struct ast_aeap_message *ast_aeap_message_create_response(const struct ast_aeap_message_type *type,
	const char *name, const char *id, const void *params)
{
	return ast_aeap_message_create2(type, "response", name, id, params);
}

struct ast_aeap_message *ast_aeap_message_create_error(const struct ast_aeap_message_type *type,
	const char *name, const char *id, const char *error_msg)
{
	struct ast_aeap_message *msg;

	msg = ast_aeap_message_create_response(type, name, id, NULL);
	if (!msg) {
		return NULL;
	}

	if (ast_aeap_message_error_msg_set(msg, error_msg)) {
		ao2_ref(msg, -1);
		return NULL;
	}

	return msg;
}

struct ast_aeap_message *ast_aeap_message_deserialize(const struct ast_aeap_message_type *type,
	const void *buf, intmax_t size)
{
	struct ast_aeap_message *msg;

	ast_assert(type != NULL);
	ast_assert(type->deserialize != NULL);

	msg = ast_aeap_message_create1(type, NULL);
	if (!msg) {
		return NULL;
	}

	if (type->deserialize(msg, buf, size)) {
		ao2_ref(msg, -1);
		return NULL;
	}

	return msg;
}

int ast_aeap_message_serialize(const struct ast_aeap_message *message,
	void **buf, intmax_t *size)
{
	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	return message->type->serialize ? message->type->serialize(message, buf, size) : 0;
}

const char *ast_aeap_message_id(const struct ast_aeap_message *message)
{
	const char *id = NULL;

	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	if (message->type->id) {
		id = message->type->id(message);
	}

	return id ? id : "";
}

int ast_aeap_message_id_set(struct ast_aeap_message *message, const char *id)
{
	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	return message->type->id_set ? message->type->id_set(message, id) : 0;
}

const char *ast_aeap_message_id_generate(struct ast_aeap_message *message)
{
	char uuid_str[AST_UUID_STR_LEN];

	ast_uuid_generate_str(uuid_str, sizeof(uuid_str));
	if (strlen(uuid_str) != (AST_UUID_STR_LEN - 1)) {
		ast_log(LOG_ERROR, "AEAP message %s failed to generate UUID for message '%s'",
			message->type->type_name, ast_aeap_message_name(message));
		return NULL;
	}

	return ast_aeap_message_id_set(message, uuid_str) ? NULL : ast_aeap_message_id(message);
}

const char *ast_aeap_message_name(const struct ast_aeap_message *message)
{
	const char *name = NULL;

	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	if (message->type->name) {
		name = message->type->name(message);
	}

	return name ? name : "";
}

int ast_aeap_message_is_named(const struct ast_aeap_message *message, const char *name)
{
	return name ? !strcasecmp(ast_aeap_message_name(message), name) : 0;
}

void *ast_aeap_message_data(struct ast_aeap_message *message)
{
	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	return message->type->data ? message->type->data(message) : NULL;
}

int ast_aeap_message_is_request(const struct ast_aeap_message *message)
{
	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	return message->type->is_request ? message->type->is_request(message) : 0;
}

int ast_aeap_message_is_response(const struct ast_aeap_message *message)
{
	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	return message->type->is_response ? message->type->is_response(message) : 0;
}

const char *ast_aeap_message_error_msg(const struct ast_aeap_message *message)
{
	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	return message->type->error_msg ? message->type->error_msg(message) : NULL;
}

int ast_aeap_message_error_msg_set(struct ast_aeap_message *message, const char *error_msg)
{
	ast_assert(message != NULL);
	ast_assert(message->type != NULL);

	return message->type->error_msg_set ? message->type->error_msg_set(message, error_msg) : 0;
}
