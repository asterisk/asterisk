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

#include "asterisk.h"

#include "asterisk/res_odbc.h"
#include "asterisk/res_odbc_transaction.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"

/*** MODULEINFO
	<depend>generic_odbc</depend>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<function name="ODBC" language="en_US">
		<synopsis>
			Controls ODBC transaction properties.
		</synopsis>
		<syntax>
			<parameter name="property" required="true">
				<enumlist>
					<enum name="transaction">
						<para>Gets or sets the active transaction ID.  If set, and the transaction ID does not
						exist and a <replaceable>database name</replaceable> is specified as an argument, it will be created.</para>
					</enum>
					<enum name="forcecommit">
						<para>Controls whether a transaction will be automatically committed when the channel
						hangs up.  Defaults to false.  If a <replaceable>transaction ID</replaceable> is specified in the optional argument,
						the property will be applied to that ID, otherwise to the current active ID.</para>
					</enum>
					<enum name="isolation">
						<para>Controls the data isolation on uncommitted transactions.  May be one of the
						following: <literal>read_committed</literal>, <literal>read_uncommitted</literal>,
						<literal>repeatable_read</literal>, or <literal>serializable</literal>.  Defaults to the
						database setting in <filename>res_odbc.conf</filename> or <literal>read_committed</literal>
						if not specified.  If a <replaceable>transaction ID</replaceable> is specified as an optional argument, it will be
						applied to that ID, otherwise the current active ID.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="argument" required="false" />
		</syntax>
		<description>
			<para>The ODBC() function allows setting several properties to influence how a connected
			database processes transactions.</para>
		</description>
	</function>
	<application name="ODBC_Commit" language="en_US">
		<synopsis>
			Commits a currently open database transaction.
		</synopsis>
		<syntax>
			<parameter name="transaction ID" required="no" />
		</syntax>
		<description>
			<para>Commits the database transaction specified by <replaceable>transaction ID</replaceable>
			or the current active transaction, if not specified.</para>
		</description>
	</application>
	<application name="ODBC_Rollback" language="en_US">
		<synopsis>
			Rollback a currently open database transaction.
		</synopsis>
		<syntax>
			<parameter name="transaction ID" required="no" />
		</syntax>
		<description>
			<para>Rolls back the database transaction specified by <replaceable>transaction ID</replaceable>
			or the current active transaction, if not specified.</para>
		</description>
	</application>
 ***/

struct odbc_txn_frame {
	AST_LIST_ENTRY(odbc_txn_frame) list;
	struct odbc_obj *obj;        /*!< Database handle within which transacted statements are run */
	/*!\brief Is this record the current active transaction within the channel?
	 * Note that the active flag is really only necessary for statements which
	 * are triggered from the dialplan, as there isn't a direct correlation
	 * between multiple statements.  Applications wishing to use transactions
	 * may simply perform each statement on the same odbc_obj, which keeps the
	 * transaction persistent.
	 */
	unsigned int active:1;
	unsigned int forcecommit:1;     /*!< Should uncommitted transactions be auto-committed on handle release? */
	unsigned int isolation;         /*!< Flags for how the DB should deal with data in other, uncommitted transactions */
	char name[0];                   /*!< Name of this transaction ID */
};

static struct odbc_txn_frame *release_transaction(struct odbc_txn_frame *tx);

static void odbc_txn_free(void *vdata)
{
	struct odbc_txn_frame *tx;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist = vdata;

	ast_debug(2, "odbc_txn_free(%p) called\n", vdata);

	AST_LIST_LOCK(oldlist);
	while ((tx = AST_LIST_REMOVE_HEAD(oldlist, list))) {
		release_transaction(tx);
	}
	AST_LIST_UNLOCK(oldlist);
	AST_LIST_HEAD_DESTROY(oldlist);
	ast_free(oldlist);
}

static const struct ast_datastore_info txn_info = {
	.type = "ODBC_Transaction",
	.destroy = odbc_txn_free,
};

static struct odbc_txn_frame *create_transaction(struct ast_channel *chan, const char *name, const char *dsn)
{
	struct ast_datastore *txn_store;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist;
	struct odbc_txn_frame *txn = NULL;
	struct odbc_txn_frame *otxn;

	if (ast_strlen_zero(dsn)) {
		return NULL;
	}

	ast_channel_lock(chan);
	if ((txn_store = ast_channel_datastore_find(chan, &txn_info, NULL))) {
		oldlist = txn_store->data;
	} else {
		if (!(txn_store = ast_datastore_alloc(&txn_info, NULL))) {
			ast_log(LOG_ERROR, "Unable to allocate a new datastore.  Cannot create a new transaction.\n");
			ast_channel_unlock(chan);
			return NULL;
		}

		if (!(oldlist = ast_calloc(1, sizeof(*oldlist)))) {
			ast_log(LOG_ERROR, "Unable to allocate datastore list head.  Cannot create a new transaction.\n");
			ast_datastore_free(txn_store);
			ast_channel_unlock(chan);
			return NULL;
		}

		txn_store->data = oldlist;
		AST_LIST_HEAD_INIT(oldlist);
		ast_channel_datastore_add(chan, txn_store);
	}
	ast_channel_unlock(chan);

	txn = ast_calloc(1, sizeof(*txn) + strlen(name) + 1);
	if (!txn) {
		return NULL;
	}

	strcpy(txn->name, name); /* SAFE */
	txn->obj = ast_odbc_request_obj(dsn, 0);
	if (!txn->obj) {
		ast_free(txn);
		return NULL;
	}
	txn->isolation = ast_odbc_class_get_isolation(txn->obj->parent);
	txn->forcecommit = ast_odbc_class_get_isolation(txn->obj->parent);
	txn->active = 1;

	if (SQLSetConnectAttr(txn->obj->con, SQL_ATTR_AUTOCOMMIT, (void *)SQL_AUTOCOMMIT_OFF, 0) == SQL_ERROR) {
		ast_odbc_print_errors(SQL_HANDLE_DBC, txn->obj->con, "SetConnectAttr (Autocommit)");
		ast_odbc_release_obj(txn->obj);
		ast_free(txn);
		return NULL;
	}

	/* Set the isolation property */
	if (SQLSetConnectAttr(txn->obj->con, SQL_ATTR_TXN_ISOLATION, (void *)(long)txn->isolation, 0) == SQL_ERROR) {
		ast_odbc_print_errors(SQL_HANDLE_DBC, txn->obj->con, "SetConnectAttr");
		ast_odbc_release_obj(txn->obj);
		ast_free(txn);
		return NULL;
	}

	/* On creation, the txn becomes active, and all others inactive */
	AST_LIST_LOCK(oldlist);
	AST_LIST_TRAVERSE(oldlist, otxn, list) {
		otxn->active = 0;
	}
	AST_LIST_INSERT_TAIL(oldlist, txn, list);
	AST_LIST_UNLOCK(oldlist);

	return txn;
}

static struct odbc_txn_frame *find_transaction(struct ast_channel *chan, const char *name, int active)
{
	struct ast_datastore *txn_store;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist;
	struct odbc_txn_frame *txn = NULL;

	if (!chan || (!active && !name)) {
		return NULL;
	}

	ast_channel_lock(chan);
	txn_store = ast_channel_datastore_find(chan, &txn_info, NULL);
	ast_channel_unlock(chan);

	if (!txn_store) {
		/* No datastore? Definitely no transaction then */
		return NULL;
	}

	oldlist = txn_store->data;
	AST_LIST_LOCK(oldlist);

	AST_LIST_TRAVERSE(oldlist, txn, list) {
		if (active) {
			if (txn->active) {
				break;
			}
		} else if (!strcasecmp(txn->name, name)) {
			break;
		}
	}
	AST_LIST_UNLOCK(oldlist);

	return txn;
}

static struct odbc_txn_frame *release_transaction(struct odbc_txn_frame *tx)
{
	if (!tx) {
		return NULL;
	}

	ast_debug(2, "release_transaction(%p) called (tx->obj = %p\n", tx, tx->obj);

	ast_debug(1, "called on a transactional handle with %s\n", tx->forcecommit ? "COMMIT" : "ROLLBACK");
	if (SQLEndTran(SQL_HANDLE_DBC, tx->obj->con, tx->forcecommit ? SQL_COMMIT : SQL_ROLLBACK) == SQL_ERROR) {
		ast_odbc_print_errors(SQL_HANDLE_DBC, tx->obj->con, "SQLEndTran");
	}

	/* Transaction is done, reset autocommit
	 *
	 * XXX I'm unsure if this is actually necessary, since we're releasing
	 * the connection back to unixODBC. However, if unixODBC pooling is enabled,
	 * it can't hurt to do just in case.
	 */
	if (SQLSetConnectAttr(tx->obj->con, SQL_ATTR_AUTOCOMMIT, (void *)SQL_AUTOCOMMIT_ON, 0) == SQL_ERROR) {
		ast_odbc_print_errors(SQL_HANDLE_DBC, tx->obj->con, "SQLSetAttr");
	}

	ast_odbc_release_obj(tx->obj);
	ast_free(tx);
	return NULL;
}

static int commit_exec(struct ast_channel *chan, const char *data)
{
	struct odbc_txn_frame *tx;

	if (ast_strlen_zero(data)) {
		tx = find_transaction(chan, NULL, 1);
	} else {
		tx = find_transaction(chan, data, 0);
	}

	/* XXX COMMIT_RESULT is set to OK even if no transaction was found. Very misleading */
	pbx_builtin_setvar_helper(chan, "COMMIT_RESULT", "OK");

	if (tx) {
		if (SQLEndTran(SQL_HANDLE_DBC, tx->obj->con, SQL_COMMIT) == SQL_ERROR) {
			struct ast_str *errors = ast_odbc_print_errors(SQL_HANDLE_DBC, tx->obj->con, "SQLEndTran");
			pbx_builtin_setvar_helper(chan, "COMMIT_RESULT", ast_str_buffer(errors));
		}
	}
	return 0;
}

static int rollback_exec(struct ast_channel *chan, const char *data)
{
	struct odbc_txn_frame *tx;

	if (ast_strlen_zero(data)) {
		tx = find_transaction(chan, NULL, 1);
	} else {
		tx = find_transaction(chan, data, 0);
	}

	/* XXX ROLLBACK_RESULT is set to OK even if no transaction was found. Very misleading */
	pbx_builtin_setvar_helper(chan, "ROLLBACK_RESULT", "OK");

	if (tx) {
		if (SQLEndTran(SQL_HANDLE_DBC, tx->obj->con, SQL_ROLLBACK) == SQL_ERROR) {
			struct ast_str *errors = ast_odbc_print_errors(SQL_HANDLE_DBC, tx->obj->con, "SQLEndTran");
			pbx_builtin_setvar_helper(chan, "ROLLBACK_RESULT", ast_str_buffer(errors));
		}
	}
	return 0;
}

static int acf_transaction_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(property);
		AST_APP_ARG(opt);
	);
	struct odbc_txn_frame *tx;

	AST_STANDARD_APP_ARGS(args, data);
	if (strcasecmp(args.property, "transaction") == 0) {
		if ((tx = find_transaction(chan, NULL, 1))) {
			ast_copy_string(buf, tx->name, len);
			return 0;
		}
	} else if (strcasecmp(args.property, "isolation") == 0) {
		if (!ast_strlen_zero(args.opt)) {
			tx = find_transaction(chan, args.opt, 0);
		} else {
			tx = find_transaction(chan, NULL, 1);
		}
		if (tx) {
			ast_copy_string(buf, ast_odbc_isolation2text(tx->isolation), len);
			return 0;
		}
	} else if (strcasecmp(args.property, "forcecommit") == 0) {
		if (!ast_strlen_zero(args.opt)) {
			tx = find_transaction(chan, args.opt, 0);
		} else {
			tx = find_transaction(chan, NULL, 1);
		}
		if (tx) {
			ast_copy_string(buf, tx->forcecommit ? "1" : "0", len);
			return 0;
		}
	}
	return -1;
}

/* XXX The idea of "active" transactions is silly and makes things
 * more prone to error. It would be much better if the transaction
 * always had to be specified by name so that no implicit behavior
 * occurred.
 */
static int mark_transaction_active(struct ast_channel *chan, struct odbc_txn_frame *tx)
{
	struct ast_datastore *txn_store;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist;
	struct odbc_txn_frame *active = NULL, *txn;

	if (!chan) {
		return -1;
	}

	ast_channel_lock(chan);
	if (!(txn_store = ast_channel_datastore_find(chan, &txn_info, NULL))) {
		ast_channel_unlock(chan);
		return -1;
	}

	oldlist = txn_store->data;
	AST_LIST_LOCK(oldlist);
	AST_LIST_TRAVERSE(oldlist, txn, list) {
		if (txn == tx) {
			txn->active = 1;
			active = txn;
		} else {
			txn->active = 0;
		}
	}
	AST_LIST_UNLOCK(oldlist);
	ast_channel_unlock(chan);
	return active ? 0 : -1;
}

static int acf_transaction_write(struct ast_channel *chan, const char *cmd, char *s, const char *value)
{
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(property);
		AST_APP_ARG(opt);
	);
	struct odbc_txn_frame *tx;

	AST_STANDARD_APP_ARGS(args, s);
	if (strcasecmp(args.property, "transaction") == 0) {
		/* Set active transaction */
		if ((tx = find_transaction(chan, value, 0))) {
			mark_transaction_active(chan, tx);
		} else if (!create_transaction(chan, value, args.opt)) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "FAILED_TO_CREATE");
			return -1;
		}
		pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "OK");
		return 0;
	} else if (strcasecmp(args.property, "forcecommit") == 0) {
		/* Set what happens when an uncommitted transaction ends without explicit Commit or Rollback */
		if (ast_strlen_zero(args.opt)) {
			tx = find_transaction(chan, NULL, 1);
		} else {
			tx = find_transaction(chan, args.opt, 0);
		}
		if (!tx) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "FAILED_TO_CREATE");
			return -1;
		}
		if (ast_true(value)) {
			tx->forcecommit = 1;
		} else if (ast_false(value)) {
			tx->forcecommit = 0;
		} else {
			ast_log(LOG_ERROR, "Invalid value for forcecommit: '%s'\n", S_OR(value, ""));
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "INVALID_VALUE");
			return -1;
		}

		pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "OK");
		return 0;
	} else if (strcasecmp(args.property, "isolation") == 0) {
		/* How do uncommitted transactions affect reads? */
		/* XXX This is completely useless. The problem is that setting the isolation here
		 * does not actually alter the connection. The only time the isolation gets set is
		 * when the transaction is created. The only way to set isolation is to set it on
		 * the ODBC class's configuration in res_odbc.conf.
		 */
		int isolation = ast_odbc_text2isolation(value);
		if (ast_strlen_zero(args.opt)) {
			tx = find_transaction(chan, NULL, 1);
		} else {
			tx = find_transaction(chan, args.opt, 0);
		}
		if (!tx) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "FAILED_TO_CREATE");
			return -1;
		}
		if (isolation == 0) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "INVALID_VALUE");
			ast_log(LOG_ERROR, "Invalid isolation specification: '%s'\n", S_OR(value, ""));
		} else if (SQLSetConnectAttr(tx->obj->con, SQL_ATTR_TXN_ISOLATION, (void *)(long)isolation, 0) == SQL_ERROR) {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "SQL_ERROR");
			ast_odbc_print_errors(SQL_HANDLE_DBC, tx->obj->con, "SetConnectAttr (Txn isolation)");
		} else {
			pbx_builtin_setvar_helper(chan, "ODBC_RESULT", "OK");
			tx->isolation = isolation;
		}
		return 0;
	} else {
		ast_log(LOG_ERROR, "Unknown property: '%s'\n", args.property);
		return -1;
	}
}

struct odbc_obj *ast_odbc_retrieve_transaction_obj(struct ast_channel *chan, const char *objname)
{
	struct ast_datastore *txn_store;
	AST_LIST_HEAD(, odbc_txn_frame) *oldlist;
	struct odbc_txn_frame *txn = NULL;

	if (!chan || !objname) {
		/* No channel == no transaction */
		return NULL;
	}

	ast_channel_lock(chan);
	if ((txn_store = ast_channel_datastore_find(chan, &txn_info, NULL))) {
		oldlist = txn_store->data;
	} else {
		ast_channel_unlock(chan);
		return NULL;
	}

	AST_LIST_LOCK(oldlist);
	ast_channel_unlock(chan);

	AST_LIST_TRAVERSE(oldlist, txn, list) {
		if (txn->obj && txn->obj->parent && !strcmp(ast_odbc_class_get_name(txn->obj->parent), objname)) {
			AST_LIST_UNLOCK(oldlist);
			return txn->obj;
		}
	}
	AST_LIST_UNLOCK(oldlist);
	return NULL;
}

static struct ast_custom_function odbc_function = {
	.name = "ODBC",
	.read = acf_transaction_read,
	.write = acf_transaction_write,
};

static const char * const app_commit = "ODBC_Commit";
static const char * const app_rollback = "ODBC_Rollback";

/* XXX res_odbc takes the path of disallowing unloads from happening.
 * It's not a great precedent, but since trying to deal with unloading the module
 * while transactions are active seems like a huge pain to deal with, we'll go
 * the same way here.
 */
static int unload_module(void)
{
	return -1;
}

static int load_module(void)
{
	ast_register_application_xml(app_commit, commit_exec);
	ast_register_application_xml(app_rollback, rollback_exec);
	ast_custom_function_register(&odbc_function);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "ODBC transaction resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_REALTIME_DEPEND,
);
