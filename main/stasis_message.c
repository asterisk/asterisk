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

/*! \file
 *
 * \brief Stasis Message API.
 *
 * \author David M. Lee, II <dlee@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"

/*! \internal */
struct stasis_message_type {
	struct stasis_message_vtable *vtable;
	char *name;
	struct ast_module_lib *lib;
};

static struct stasis_message_vtable null_vtable = {};

static void message_type_dtor(void *obj)
{
	struct stasis_message_type *type = obj;

	ast_free(type->name);
	type->name = NULL;
	ao2_cleanup(type->lib);
}

int __stasis_message_type_create(const char *name,
	struct stasis_message_vtable *vtable,
	struct stasis_message_type **result,
	struct ast_module *module)
{
	struct stasis_message_type *type;

	/* Check for declination */
	if (name && stasis_message_type_declined(name)) {
		return STASIS_MESSAGE_TYPE_DECLINED;
	}

	type = ao2_t_alloc(sizeof(*type), message_type_dtor, name);
	if (!type) {
		return STASIS_MESSAGE_TYPE_ERROR;
	}
	if (!vtable) {
		/* Null object pattern, FTW! */
		vtable = &null_vtable;
	}

	type->name = ast_strdup(name);
	if (!type->name) {
		ao2_cleanup(type);
		return STASIS_MESSAGE_TYPE_ERROR;
	}
	type->vtable = vtable;
	*result = type;

	if (module) {
		type->lib = ast_module_get_lib_running(module);
		ast_assert(!!type->lib);
	}

	return STASIS_MESSAGE_TYPE_SUCCESS;
}

const char *stasis_message_type_name(const struct stasis_message_type *type)
{
	return type->name;
}

/*! \internal */
struct stasis_message {
	/*! Time the message was created */
	struct timeval timestamp;
	/*! Type of the message */
	struct stasis_message_type *type;
	/*! Where this message originated.  NULL if aggregate message. */
	const struct ast_eid *eid_ptr;
	/*! Message content */
	void *data;
	/*! Where this message originated. */
	struct ast_eid eid;
};

static void stasis_message_dtor(void *obj)
{
	struct stasis_message *message = obj;
	ao2_cleanup(message->type);
	ao2_cleanup(message->data);
}

struct stasis_message *stasis_message_create_full(struct stasis_message_type *type, void *data, const struct ast_eid *eid)
{
	struct stasis_message *message;

	if (type == NULL || data == NULL) {
		return NULL;
	}

	message = ao2_t_alloc(sizeof(*message), stasis_message_dtor, type->name);
	if (message == NULL) {
		return NULL;
	}

	message->timestamp = ast_tvnow();
	ao2_ref(type, +1);
	message->type = type;
	ao2_ref(data, +1);
	message->data = data;
	if (eid) {
		message->eid_ptr = &message->eid;
		message->eid = *eid;
	}

	return message;
}

struct stasis_message *stasis_message_create(struct stasis_message_type *type, void *data)
{
	return stasis_message_create_full(type, data, &ast_eid_default);
}

const struct ast_eid *stasis_message_eid(const struct stasis_message *msg)
{
	if (msg == NULL) {
		return NULL;
	}
	return msg->eid_ptr;
}

struct stasis_message_type *stasis_message_type(const struct stasis_message *msg)
{
	if (msg == NULL) {
		return NULL;
	}
	return msg->type;
}

void *stasis_message_data(const struct stasis_message *msg)
{
	if (msg == NULL) {
		return NULL;
	}
	return msg->data;
}

const struct timeval *stasis_message_timestamp(const struct stasis_message *msg)
{
	if (msg == NULL) {
		return NULL;
	}
	return &msg->timestamp;
}

#define INVOKE_VIRTUAL(fn, ...)				\
	({						\
		if (msg == NULL) {			\
			return NULL;			\
		}					\
		ast_assert(msg->type != NULL);		\
		ast_assert(msg->type->vtable != NULL);	\
		if (msg->type->vtable->fn == NULL) {	\
			return NULL;			\
		}					\
		msg->type->vtable->fn(__VA_ARGS__);	\
	})

struct ast_manager_event_blob *stasis_message_to_ami(struct stasis_message *msg)
{
	return INVOKE_VIRTUAL(to_ami, msg);
}

struct ast_json *stasis_message_to_json(
	struct stasis_message *msg,
	struct stasis_message_sanitizer *sanitize)
{
	return INVOKE_VIRTUAL(to_json, msg, sanitize);
}

struct ast_event *stasis_message_to_event(struct stasis_message *msg)
{
	return INVOKE_VIRTUAL(to_event, msg);
}
