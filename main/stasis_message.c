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

#include "asterisk/astobj2.h"
#include "asterisk/stasis.h"
#include "asterisk/utils.h"
#include "asterisk/hashtab.h"

/*! \internal */
struct stasis_message_type {
	struct stasis_message_vtable *vtable;
	char *name;
	unsigned int hash;
	int id;
};

static struct stasis_message_vtable null_vtable = {};
static int message_type_id;

static void message_type_dtor(void *obj)
{
	struct stasis_message_type *type = obj;
	ast_free(type->name);
	type->name = NULL;
}

int stasis_message_type_create(const char *name,
	struct stasis_message_vtable *vtable,
	struct stasis_message_type **result)
{
	struct stasis_message_type *type;

	/* Check for declination */
	if (name && stasis_message_type_declined(name)) {
		return STASIS_MESSAGE_TYPE_DECLINED;
	}

	type = ao2_t_alloc_options(sizeof(*type), message_type_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK, name ?: "");
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
	type->hash = ast_hashtab_hash_string(name);
	type->vtable = vtable;
	type->id = ast_atomic_fetchadd_int(&message_type_id, +1);
	*result = type;

	return STASIS_MESSAGE_TYPE_SUCCESS;
}

const char *stasis_message_type_name(const struct stasis_message_type *type)
{
	return type->name;
}

unsigned int stasis_message_type_hash(const struct stasis_message_type *type)
{
	return type->hash;
}

int stasis_message_type_id(const struct stasis_message_type *type)
{
	return type->id;
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
	ao2_cleanup(message->data);
}

struct stasis_message *stasis_message_create_full(struct stasis_message_type *type, void *data, const struct ast_eid *eid)
{
	struct stasis_message *message;

	if (type == NULL || data == NULL) {
		return NULL;
	}

	message = ao2_t_alloc_options(sizeof(*message), stasis_message_dtor,
		AO2_ALLOC_OPT_LOCK_NOLOCK, type->name);
	if (message == NULL) {
		return NULL;
	}

	message->timestamp = ast_tvnow();
	/*
	 * XXX Normal ao2 ref counting rules says we should increment the message
	 * type ref here and decrement it in stasis_message_dtor().  However, the
	 * stasis message could be cached and legitimately cause the type ref count
	 * to hit the excessive ref count assertion.  Since the message type
	 * practically has to be a global object anyway, we can get away with not
	 * holding a ref in the stasis message.
	 */
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

#define INVOKE_VIRTUAL(fn, ...)					\
	({											\
		if (!msg) {								\
			return NULL;						\
		}										\
		ast_assert(msg->type != NULL);			\
		ast_assert(msg->type->vtable != NULL);	\
		if (!msg->type->vtable->fn) {			\
			return NULL;						\
		}										\
		msg->type->vtable->fn(__VA_ARGS__);		\
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

#define HAS_VIRTUAL(fn, msg)					\
	({											\
		if (!msg) {								\
			return 0;							\
		}										\
		ast_assert(msg->type != NULL);			\
		ast_assert(msg->type->vtable != NULL);	\
		!!msg->type->vtable->fn;				\
	})

int stasis_message_can_be_ami(struct stasis_message *msg)
{
	return HAS_VIRTUAL(to_ami, msg);
}
