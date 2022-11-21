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

#include <pthread.h>

#include "asterisk/astobj2.h"
#include "asterisk/strings.h"

#include "asterisk/res_aeap.h"
#include "asterisk/res_aeap_message.h"

#include "logger.h"
#include "transaction.h"
#include "transport.h"

#define AEAP_RECV_SIZE 32768

struct aeap_user_data {
	/*! The user data object */
	void *obj;
	/*! A user data identifier */
	char id[0];
};

AO2_STRING_FIELD_HASH_FN(aeap_user_data, id);
AO2_STRING_FIELD_CMP_FN(aeap_user_data, id);

#define USER_DATA_BUCKETS 11

struct ast_aeap {
	/*! This object's configuration parameters */
	const struct ast_aeap_params *params;
	/*! Container for registered user data objects */
	struct ao2_container *user_data;
	/*! Transactions container */
	struct ao2_container *transactions;
	/*! Transport layer communicator */
	struct aeap_transport *transport;
	/*! Id of thread that reads data from the transport */
	pthread_t read_thread_id;
};

static int tsx_end(void *obj, void *arg, int flags)
{
	aeap_transaction_end(obj, -1);

	return 0;
}

static void aeap_destructor(void *obj)
{
	struct ast_aeap *aeap = obj;

	/* Disconnect things first, which keeps transactions from further executing */
	ast_aeap_disconnect(aeap);

	aeap_transport_destroy(aeap->transport);

	/*
	 * Each contained transaction holds a pointer back to this transactions container,
	 * which is removed upon transaction end. Thus by explicitly ending each transaction
	 * here we can ensure all references to the transactions container are removed.
	 */
	ao2_callback(aeap->transactions, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE,
			tsx_end, NULL);
	ao2_cleanup(aeap->transactions);

	ao2_cleanup(aeap->user_data);
}

struct ast_aeap *ast_aeap_create(const char *transport_type,
	const struct ast_aeap_params *params)
{
	struct ast_aeap *aeap;

	aeap = ao2_alloc(sizeof(*aeap), aeap_destructor);
	if (!aeap) {
		ast_log(LOG_ERROR, "AEAP: unable to create");
		return NULL;
	}

	aeap->params = params;
	aeap->read_thread_id = AST_PTHREADT_NULL;

	aeap->user_data = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, USER_DATA_BUCKETS,
		aeap_user_data_hash_fn, NULL, aeap_user_data_cmp_fn);
	if (!aeap->user_data) {
		aeap_error(aeap, NULL, "unable to create user data container");
		ao2_ref(aeap, -1);
		return NULL;
	}

	aeap->transactions = aeap_transactions_create();
	if (!aeap->transactions) {
		aeap_error(aeap, NULL, "unable to create transactions container");
		ao2_ref(aeap, -1);
		return NULL;
	}

	aeap->transport = aeap_transport_create(transport_type);
	if (!aeap->transport) {
		aeap_error(aeap, NULL, "unable to create transport");
		ao2_ref(aeap, -1);
		return NULL;
	}

	return aeap;
}

static struct aeap_user_data *aeap_user_data_create(const char *id, void *obj,
	ast_aeap_user_obj_cleanup cleanup)
{
	struct aeap_user_data *data;

	ast_assert(id != NULL);

	data = ao2_t_alloc_options(sizeof(*data) + strlen(id) + 1, cleanup,
		AO2_ALLOC_OPT_LOCK_NOLOCK, "");
	if (!data) {
		if (cleanup) {
			cleanup(obj);
		}

		return NULL;
	}

	strcpy(data->id, id); /* safe */
	data->obj = obj;

	return data;
}

int ast_aeap_user_data_register(struct ast_aeap *aeap, const char *id, void *obj,
	ast_aeap_user_obj_cleanup cleanup)
{
	struct aeap_user_data *data;

	data = aeap_user_data_create(id, obj, cleanup);
	if (!data) {
		return -1;
	}

	if (!ao2_link(aeap->user_data, data)) {
		ao2_ref(data, -1);
		return -1;
	}

	ao2_ref(data, -1);
	return 0;
}

void ast_aeap_user_data_unregister(struct ast_aeap *aeap, const char *id)
{
	ao2_find(aeap->user_data, id, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
}

void *ast_aeap_user_data_object_by_id(struct ast_aeap *aeap, const char *id)
{
	struct aeap_user_data *data;
	void *obj;

	data = ao2_find(aeap->user_data, id, OBJ_SEARCH_KEY);
	if (!data) {
		return NULL;
	}

	obj = data->obj;
	ao2_ref(data, -1);

	/*
	 * Returned object's lifetime is based on how it was registered.
	 * See public function docs for more info
	 */
	return obj;
}

static int raise_msg_handler(struct ast_aeap *aeap,	const struct ast_aeap_message_handler *handlers,
	size_t size, struct ast_aeap_message *msg, void *data)
{
	ast_aeap_on_message on_message = NULL;
	size_t i;

	if (!aeap->params->emit_error) {
		const char *error_msg = ast_aeap_message_error_msg(msg);

		if (error_msg) {
			aeap_error(aeap, NULL, "%s", error_msg);
			return -1;
		}

		/* If no error_msg then it's assumed this is not an error message */
	}

	for (i = 0; i < size; ++i) {
		if (ast_strlen_zero(handlers[i].name)) {
			/* A default handler is specified. Use it if no other match is found */
			on_message = handlers[i].on_message;
			continue;
		}

		if (ast_aeap_message_is_named(msg, handlers[i].name)) {
			on_message = handlers[i].on_message;
			break;
		}
	}

	if (on_message) {
		return on_message(aeap, msg, data);
	}

	/* Respond with un-handled error */
	ast_aeap_send_msg(aeap, ast_aeap_message_create_error(aeap->params->msg_type,
		ast_aeap_message_name(msg), ast_aeap_message_id(msg),
		"Unsupported and/or un-handled message"));

	return 0;
}

static void raise_msg(struct ast_aeap *aeap, const void *buf, intmax_t size,
	enum AST_AEAP_DATA_TYPE serial_type)
{
	struct ast_aeap_message *msg;
	struct aeap_transaction *tsx;
	int res = 0;

	if (!aeap->params || !aeap->params->msg_type ||
		ast_aeap_message_serial_type(aeap->params->msg_type) != serial_type ||
		!(msg = ast_aeap_message_deserialize(aeap->params->msg_type, buf, size))) {
		return;
	}

	/* See if this msg is involved in a transaction */
	tsx = aeap_transaction_get(aeap->transactions, ast_aeap_message_id(msg));

	/* If so go ahead and cancel the timeout timer */
	aeap_transaction_cancel_timer(tsx);

	if (aeap->params->request_handlers && ast_aeap_message_is_request(msg)) {
		res = raise_msg_handler(aeap, aeap->params->request_handlers, aeap->params->request_handlers_size,
			msg, tsx ? aeap_transaction_user_obj(tsx) : NULL);
	} else if (aeap->params->response_handlers && ast_aeap_message_is_response(msg)) {
		res = raise_msg_handler(aeap, aeap->params->response_handlers, aeap->params->response_handlers_size,
			msg, tsx ? aeap_transaction_user_obj(tsx) : NULL);
	}

	/* Complete transaction (Note, removes tsx ref) */
	aeap_transaction_end(tsx, res);

	ao2_ref(msg, -1);
}

static void *aeap_receive(void *data)
{
	struct ast_aeap *aeap = data;
	void *buf;

	buf = ast_calloc(1, AEAP_RECV_SIZE);
	if (!buf) {
		aeap_error(aeap, NULL, "unable to create read buffer");
		goto aeap_receive_error;
	}

	while (aeap_transport_is_connected(aeap->transport)) {
		enum AST_AEAP_DATA_TYPE rtype;
		intmax_t size;

		size = aeap_transport_read(aeap->transport, buf, AEAP_RECV_SIZE, &rtype);
		if (size < 0) {
			goto aeap_receive_error;
		}

		if (!size) {
			continue;
		}

		switch (rtype) {
		case AST_AEAP_DATA_TYPE_BINARY:
			if (aeap->params && aeap->params->on_binary) {
				aeap->params->on_binary(aeap, buf, size);
			}
			break;
		case AST_AEAP_DATA_TYPE_STRING:
			ast_debug(3, "AEAP: received message: %s\n", (char *)buf);
			if (aeap->params && aeap->params->on_string) {
				aeap->params->on_string(aeap, (const char *)buf, size - 1);
			}
			break;
		default:
			break;
		}

		raise_msg(aeap, buf, size, rtype);
	};

	ast_free(buf);
	return NULL;

aeap_receive_error:
	/*
	 * An unrecoverable error occurred so ensure the aeap and transport reset
	 * to a disconnected state. We don't want this thread to "join" itself so set
	 * its id to NULL prior to disconnecting.
	 */
	aeap_error(aeap, NULL, "unrecoverable read error, disconnecting");

	ao2_lock(aeap);
	aeap->read_thread_id = AST_PTHREADT_NULL;
	ao2_unlock(aeap);

	ast_aeap_disconnect(aeap);

	ast_free(buf);

	if (aeap->params && aeap->params->on_error) {
		aeap->params->on_error(aeap);
	}

	return NULL;
}

int ast_aeap_connect(struct ast_aeap *aeap, const char *url, const char *protocol, int timeout)
{
	SCOPED_AO2LOCK(lock, aeap);

	if (aeap_transport_is_connected(aeap->transport)) {
		/* Should already be connected, so nothing to do */
		return 0;
	}

	if (aeap_transport_connect(aeap->transport, url, protocol, timeout)) {
		aeap_error(aeap, NULL, "unable to connect transport");
		return -1;
	}

	if (ast_pthread_create_background(&aeap->read_thread_id, NULL,
			aeap_receive, aeap)) {
		aeap_error(aeap, NULL, "unable to start read thread: %s",
			strerror(errno));
		ast_aeap_disconnect(aeap);
		return -1;
	}

	return 0;
}

struct ast_aeap *ast_aeap_create_and_connect(const char *type,
	const struct ast_aeap_params *params, const char *url, const char *protocol, int timeout)
{
	struct ast_aeap *aeap;

	aeap = ast_aeap_create(type, params);
	if (!aeap) {
		return NULL;
	}

	if (ast_aeap_connect(aeap, url, protocol, timeout)) {
		ao2_ref(aeap, -1);
		return NULL;
	}

	return aeap;
}

int ast_aeap_disconnect(struct ast_aeap *aeap)
{
	ao2_lock(aeap);

	aeap_transport_disconnect(aeap->transport);

	if (aeap->read_thread_id != AST_PTHREADT_NULL) {
		/*
		 * The read thread calls disconnect if an error occurs, so
		 * unlock the aeap before "joining" to avoid a deadlock.
		 */
		ao2_unlock(aeap);
		pthread_join(aeap->read_thread_id, NULL);
		ao2_lock(aeap);

		aeap->read_thread_id = AST_PTHREADT_NULL;
	}

	ao2_unlock(aeap);

	return 0;
}

static int aeap_send(struct ast_aeap *aeap, const void *buf, uintmax_t size,
	enum AST_AEAP_DATA_TYPE type)
{
	intmax_t num;

	num = aeap_transport_write(aeap->transport, buf, size, type);

	if (num == 0) {
		/* Nothing written, could be disconnected */
		return 0;
	}

	if (num < 0) {
		aeap_error(aeap, NULL, "error sending data");
		return -1;
	}

	if (num < size) {
		aeap_error(aeap, NULL, "not all data sent");
		return -1;
	}

	if (num > size) {
		aeap_error(aeap, NULL, "sent data truncated");
		return -1;
	}

	return 0;
}

int ast_aeap_send_binary(struct ast_aeap *aeap, const void *buf, uintmax_t size)
{
	return aeap_send(aeap, buf, size, AST_AEAP_DATA_TYPE_BINARY);
}

int ast_aeap_send_msg(struct ast_aeap *aeap, struct ast_aeap_message *msg)
{
	void *buf;
	intmax_t size;
	int res;

	if (!msg) {
		aeap_error(aeap, NULL, "no message to send");
		return -1;
	}

	if (ast_aeap_message_serialize(msg, &buf, &size)) {
		aeap_error(aeap, NULL, "unable to serialize outgoing message");
		ao2_ref(msg, -1);
		return -1;
	}

	res = aeap_send(aeap, buf, size, msg->type->serial_type);

	ast_free(buf);
	ao2_ref(msg, -1);

	return res;
}

int ast_aeap_send_msg_tsx(struct ast_aeap *aeap, struct ast_aeap_tsx_params *params)
{
	struct aeap_transaction *tsx = NULL;
	int res = 0;

	if (!params) {
		return -1;
	}

	if (!params->msg) {
		aeap_transaction_params_cleanup(params);
		aeap_error(aeap, NULL, "no message to send");
		return -1;
	}

	/* The transaction will take over params cleanup, which includes the msg reference */
	tsx = aeap_transaction_create_and_add(aeap->transactions,
		ast_aeap_message_id(params->msg), params, aeap);
	if (!tsx) {
		return -1;
	}

	if (ast_aeap_send_msg(aeap, ao2_bump(params->msg))) {
		aeap_transaction_end(tsx, -1); /* Removes container, and tsx ref */
		return -1;
	}

	if (aeap_transaction_start(tsx)) {
		aeap_transaction_end(tsx, -1); /* Removes container, and tsx ref */
		return -1;
	}

	res = aeap_transaction_result(tsx);

	ao2_ref(tsx, -1);

	return res;
}
