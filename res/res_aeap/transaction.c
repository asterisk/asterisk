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
#include "asterisk/sched.h"
#include "asterisk/utils.h"

#include "asterisk/res_aeap.h"
#include "asterisk/res_aeap_message.h"

#include "general.h"
#include "logger.h"
#include "transaction.h"

struct aeap_transaction {
	/*! Pointer back to owner object */
	struct ast_aeap *aeap;
	/*! The container this transaction is in */
	struct ao2_container *container;
	/*! Scheduler ID message timeout */
	int sched_id;
	/*! Whether or not the handler has been executed */
	int handled;
	/*! Used to sync matching received messages */
	ast_cond_t handled_cond;
	/*! The result of this transaction */
	int result;
	/*! The timeout data */
	struct ast_aeap_tsx_params params;
	/*! The transaction identifier */
	char id[0];
};

/*! \brief Number of transaction buckets */
#define AEAP_TRANSACTION_BUCKETS 11

AO2_STRING_FIELD_HASH_FN(aeap_transaction, id);
AO2_STRING_FIELD_CMP_FN(aeap_transaction, id);

int aeap_transaction_cancel_timer(struct aeap_transaction *tsx)
{
	if (tsx && tsx->sched_id != -1) {
		AST_SCHED_DEL_UNREF(aeap_sched_context(), tsx->sched_id, ao2_ref(tsx, -1));
		return tsx->sched_id != -1;
	}

	return 0;
}

void aeap_transaction_params_cleanup(struct ast_aeap_tsx_params *params)
{
	ao2_cleanup(params->msg);

	if (params->obj_cleanup) {
		params->obj_cleanup(params->obj);
	}
}

static void transaction_destructor(void *obj)
{
	struct aeap_transaction *tsx = obj;

	/* Ensure timer is canceled */
	aeap_transaction_cancel_timer(tsx);

	aeap_transaction_params_cleanup(&tsx->params);

	ast_cond_destroy(&tsx->handled_cond);
}

static struct aeap_transaction *transaction_create(const char *id,
	struct ast_aeap_tsx_params *params, struct ast_aeap *aeap)
{
	struct aeap_transaction *tsx;

	if (!id) {
		aeap_error(aeap, "transaction", "missing transaction id");
		aeap_transaction_params_cleanup(params);
		return NULL;
	}

	tsx = ao2_alloc(sizeof(*tsx) + strlen(id) + 1, transaction_destructor);
	if (!tsx) {
		aeap_error(aeap, "transaction", "unable to create for '%s'", id);
		aeap_transaction_params_cleanup(params);
		return NULL;
	}

	strcpy(tsx->id, id); /* safe */
	tsx->sched_id = -1;

	ast_cond_init(&tsx->handled_cond, NULL);

	/*
	 * Currently, transactions, and their lifetimes are fully managed by the given 'aeap'
	 * object, so do not bump its reference here as we want the 'aeap' object to stop
	 * transactions and not transactions potentially stopping the 'aeap' object.
	 */
	tsx->aeap = aeap;
	tsx->params = *params;

	return tsx;
}

static void transaction_end(struct aeap_transaction *tsx, int timed_out, int result)
{
	if (!tsx) {
		return;
	}

	ao2_lock(tsx);

	tsx->result = result;

	if (tsx->container) {
		ao2_unlink(tsx->container, tsx);
		tsx->container = NULL;
	}

	if (!timed_out) {
		aeap_transaction_cancel_timer(tsx);
	} else if (tsx->sched_id != -1) {
		tsx->sched_id = -1;
	}

	if (!tsx->handled) {
		if (timed_out) {
			if (tsx->params.on_timeout) {
				tsx->params.on_timeout(tsx->aeap, tsx->params.msg, tsx->params.obj);
			} else {
				aeap_error(tsx->aeap, "transaction", "message '%s' timed out",
					ast_aeap_message_name(tsx->params.msg));
			}
		}

		tsx->handled = 1;
		ast_cond_signal(&tsx->handled_cond);
	}

	ao2_unlock(tsx);

	ao2_ref(tsx, -1);
}

static int transaction_raise_timeout(const void *data)
{
	/* Ref added added at timer creation removed in end call */
	transaction_end((struct aeap_transaction *)data, 1, -1);

	return 0;
}

static int transaction_sched_timer(struct aeap_transaction *tsx)
{
	if (tsx->params.timeout <= 0 || tsx->sched_id != -1) {
		return 0;
	}

	tsx->sched_id = ast_sched_add(aeap_sched_context(), tsx->params.timeout,
			transaction_raise_timeout, ao2_bump(tsx));
	if (tsx->sched_id == -1) {
		aeap_error(tsx->aeap, "transaction", "unable to schedule timeout for '%s'", tsx->id);
		ao2_ref(tsx, -1);
		return -1;
	}

	return 0;
}

static void transaction_wait(struct aeap_transaction *tsx)
{
	ao2_lock(tsx);

	while (!tsx->handled) {
		ast_cond_wait(&tsx->handled_cond, ao2_object_get_lockaddr(tsx));
	}

	ao2_unlock(tsx);
}

int aeap_transaction_start(struct aeap_transaction *tsx)
{
	if (transaction_sched_timer(tsx)) {
		return -1;
	}

	if (tsx->params.wait) {
		/* Wait until transaction completes, or times out */
		transaction_wait(tsx);
	}

	return 0;
}

struct aeap_transaction *aeap_transaction_get(struct ao2_container *transactions, const char *id)
{
	return ao2_find(transactions, id, OBJ_SEARCH_KEY);
}

void aeap_transaction_end(struct aeap_transaction *tsx, int result)
{
	transaction_end(tsx, 0, result);
}

int aeap_transaction_result(struct aeap_transaction *tsx)
{
	return tsx->result;
}

void *aeap_transaction_user_obj(struct aeap_transaction *tsx)
{
	return tsx->params.obj;
}

struct aeap_transaction *aeap_transaction_create_and_add(struct ao2_container *transactions,
	const char *id, struct ast_aeap_tsx_params *params, struct ast_aeap *aeap)
{
	struct aeap_transaction *tsx;

	tsx = transaction_create(id, params, aeap);
	if (!tsx) {
		return NULL;
	}

	if (!ao2_link(transactions, tsx)) {
		aeap_error(tsx->aeap, "transaction", "unable to add '%s' to container", id);
		ao2_ref(tsx, -1);
		return NULL;
	}

	/*
	 * Yes, this creates a circular reference. This reference is removed though
	 * upon transaction end. It's assumed here that the given transactions container
	 * takes "ownership", and ultimate responsibility of its contained transactions.
	 * Thus when the given container needs to be unref'ed/freed it must call
	 * aeap_transaction_end for each transaction prior to doing so.
	 */
	/* tsx->container = ao2_bump(transactions); */

	/*
	 * The transaction needs to know what container manages it, so it can remove
	 * itself from the given container under certain conditions (e.g. transaction
	 * timeout).
	 *
	 * It's expected that the given container will out live any contained transaction
	 * (i.e. the container will not itself be destroyed before ensuring all contained
	 * transactions are ended, and removed). Thus there is no reason to bump the given
	 * container's reference here.
	 */
	tsx->container = transactions;

	return tsx;
}

struct ao2_container *aeap_transactions_create(void)
{
	struct ao2_container *transactions;

	transactions = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, AEAP_TRANSACTION_BUCKETS,
		aeap_transaction_hash_fn, NULL, aeap_transaction_cmp_fn);
	if (!transactions) {
		ast_log(LOG_ERROR, "AEAP transaction: unable to create container\n");
		return NULL;
	}

	return transactions;
}
