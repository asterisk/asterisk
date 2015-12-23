/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2016, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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

#ifndef RES_ODBC_TRANSACTION_H
#define RES_ODBC_TRANSACTION_H

/*!
 * \brief
 *
 * Retrieve an ODBC transaction connection with the given ODBC class name.
 *
 * \note The name passed here is *not* the name of the transaction but the name of the
 * ODBC class defined in res_odbc.conf.
 *
 * \note Do not call ast_odbc_release_obj() on the retrieved connection. Calling this function
 * does not make you the owner of the connection.
 *
 * XXX This function is majorly flawed because it ignores properties of transactions and simply
 * finds one that corresponds to the given DSN. The problem here is that transactions have names
 * and they maintain which transaction is "active" for operations like transaction creation,
 * commit, and rollback. However, when it comes to intermediary operations to be made on the
 * transactions, all that is ignored. It means that if a channel has created multiple transactions
 * for the same DSN, it's a crapshoot which of those transactions the operation will be performed
 * on. This can potentially lead to baffling errors under the right circumstances.
 *
 * XXX The semantics of this function make for writing some awkward code. If you use func_odbc as
 * an example, it has to first try to retrieve a transactional connection, then failing that, create
 * a non-transactional connection. The result is that it has to remember which type of connection it's
 * using and know whether to release the connection when completed or not. It would be much better
 * if callers did not have to jump through such hoops.
 *
 * \param chan Channel on which the ODBC transaction was created
 * \param objname The name of the ODBC class configured in res_odbc.conf
 * \retval NULL Transaction connection could not be found.
 * \retval non-NULL A transactional connection
 */
struct odbc_obj *ast_odbc_retrieve_transaction_obj(struct ast_channel *chan, const char *objname);

#endif /* RES_ODBC_TRANSACTION_H */
