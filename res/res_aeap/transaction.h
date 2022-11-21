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

#ifndef RES_AEAP_TRANSACTION_H
#define RES_AEAP_TRANSACTION_H

#include "asterisk/res_aeap.h"

struct ao2_container;
struct ast_aeap_tsx_params;
struct aeap_transaction;

/*!
 * \brief Create an Asterisk external application transactions container
 *
 * \returns A transaction object, or NULL on error
 */
struct ao2_container *aeap_transactions_create(void);

/*!
 * \brief Create a transaction object, and add it to the given container
 *
 * \param transactions A transactions container
 * \param id An id to use for the transaction
 * \param params Transaction parameters
 * \param aeap The aeap object that "owns" this transaction
 *
 * \returns 0 if successfully create and added, -1 on error
 */
struct aeap_transaction *aeap_transaction_create_and_add(struct ao2_container *transactions,
	const char *id, struct ast_aeap_tsx_params *params, struct ast_aeap *aeap);

/*!
 * \brief Clean up parameter references, and possibly call optional user object cleanup
 *
 * \param params Transaction parameters
 */
void aeap_transaction_params_cleanup(struct ast_aeap_tsx_params *params);

/*!
 * \brief Retrieve a transaction for the id from the container
 *
 * \param transactions A transactions container
 * \param id A transaction id
 *
 * \returns an AEAP transaction object, NULL if no transaction is found
 */
struct aeap_transaction *aeap_transaction_get(struct ao2_container *transactions,
	const char *id);

/*!
 * \brief Start the transaction
 *
 * \param tsx The transaction to initiate
 *
 * \returns 0 if successfully raised, and handled. Otherwise non zero.
 */
int aeap_transaction_start(struct aeap_transaction *tsx);

/*!
 * \brief End a transaction, and remove it from the given container
 *
 * The "result" parameter is a value representing the state (success/failure,
 * perhaps even something else) of transactional processing upon ending.
 *
 * \param tsx A transaction to end
 * \param result A result to give to the transaction
 */
void aeap_transaction_end(struct aeap_transaction *tsx, int result);

/*!
 * \brief Get a transaction's result
 *
 * A transaction's result is a value that represents the relative success (0), or
 * failure (-1) of a transaction. For example, a timeout is considered a failure
 * and will elicit a -1.
 *
 * This value though is also dependent upon the result of the message handler
 * associated with the transaction. Meaning if an associated message is handled,
 * then its result is stored as the transaction result and returned here.
 *
 * \param tsx A transaction object
 *
 * \returns The transaction result
 */
int aeap_transaction_result(struct aeap_transaction *tsx);

/*!
 * \brief Cancel the transaction timer
 *
 * Stops the transaction timer, but does not end/stop the transaction itself
 *
 * \param transaction A transaction to cancel the timer on
 *
 * \returns 0 if canceled, non zero otherwise
 */
int aeap_transaction_cancel_timer(struct aeap_transaction *tsx);

/*!
 * \brief Retrieve the user object associated with the transaction
 *
 * \param transaction A transaction object
 *
 * \returns A user object, or NULL if non associated
 */
void *aeap_transaction_user_obj(struct aeap_transaction *tsx);

#endif /* RES_AEAP_TRANSACTION_H */
