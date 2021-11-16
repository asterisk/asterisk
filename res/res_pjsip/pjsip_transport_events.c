/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2017, Digium Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Manages the global transport event notification callbacks.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 * 	See Also:
 *
 * \arg \ref AstCREDITS
 */


#include "asterisk.h"

#include "asterisk/res_pjsip.h"
#include "include/res_pjsip_private.h"
#include "asterisk/linkedlists.h"
#include "asterisk/vector.h"

/* ------------------------------------------------------------------- */

/*! \brief Number of buckets for monitored active transports */
#define ACTIVE_TRANSPORTS_BUCKETS 127

/*! Who to notify when transport shuts down. */
struct transport_monitor_notifier {
	/*! Who to call when transport shuts down. */
	ast_transport_monitor_shutdown_cb cb;
	/*! ao2 data object to pass to callback. */
	void *data;
};

/*! \brief Structure for transport to be monitored */
struct transport_monitor {
	/*! \brief The underlying PJSIP transport */
	pjsip_transport *transport;
	/*! Who is interested in when this transport shuts down. */
	AST_VECTOR(, struct transport_monitor_notifier) monitors;
};

/*! \brief Global container of active reliable transports */
static AO2_GLOBAL_OBJ_STATIC(active_transports);

/*! \brief Existing transport events callback that we need to invoke */
static pjsip_tp_state_callback tpmgr_state_callback;

/*! List of registered transport state callbacks. */
static AST_RWLIST_HEAD(, ast_sip_tpmgr_state_callback) transport_state_list;


/*! \brief Hashing function for struct transport_monitor */
AO2_STRING_FIELD_HASH_FN(transport_monitor, transport->obj_name);

/*! \brief Comparison function for struct transport_monitor */
AO2_STRING_FIELD_CMP_FN(transport_monitor, transport->obj_name);

static const char *transport_state2str(pjsip_transport_state state)
{
	const char *name;

	switch (state) {
	case PJSIP_TP_STATE_CONNECTED:
		name = "CONNECTED";
		break;
	case PJSIP_TP_STATE_DISCONNECTED:
		name = "DISCONNECTED";
		break;
	case PJSIP_TP_STATE_SHUTDOWN:
		name = "SHUTDOWN";
		break;
	case PJSIP_TP_STATE_DESTROY:
		name = "DESTROY";
		break;
	default:
		/*
		 * We have to have a default case because the enum is
		 * defined by a third-party library.
		 */
		ast_assert(0);
		name = "<unknown>";
		break;
	}
	return name;
}

static void transport_monitor_dtor(void *vdoomed)
{
	struct transport_monitor *monitored = vdoomed;
	int idx;

	for (idx = AST_VECTOR_SIZE(&monitored->monitors); idx--;) {
		struct transport_monitor_notifier *notifier;

		notifier = AST_VECTOR_GET_ADDR(&monitored->monitors, idx);
		ao2_cleanup(notifier->data);
	}
	AST_VECTOR_FREE(&monitored->monitors);
}

/*!
 * \internal
 * \brief Do registered callbacks for the transport.
 * \since 13.21.0
 *
 * \param transports Active transports container
 * \param transport Which transport to do callbacks for.
 */
static void transport_state_do_reg_callbacks(struct ao2_container *transports, pjsip_transport *transport)
{
	struct transport_monitor *monitored;

	monitored = ao2_find(transports, transport->obj_name, OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (monitored) {
		int idx;

		for (idx = AST_VECTOR_SIZE(&monitored->monitors); idx--;) {
			struct transport_monitor_notifier *notifier;

			notifier = AST_VECTOR_GET_ADDR(&monitored->monitors, idx);
			ast_debug(3, "running callback %p(%p) for transport %s\n",
				notifier->cb, notifier->data, transport->obj_name);
			notifier->cb(notifier->data);
		}
		ao2_ref(monitored, -1);
	}
}

/*! \brief Callback invoked when transport state changes occur */
static void transport_state_callback(pjsip_transport *transport,
	pjsip_transport_state state, const pjsip_transport_state_info *info)
{
	struct ao2_container *transports;

	/* We only care about monitoring reliable transports */
	if (PJSIP_TRANSPORT_IS_RELIABLE(transport)
		&& (transports = ao2_global_obj_ref(active_transports))) {
		struct transport_monitor *monitored;

		ast_debug(3, "Reliable transport '%s' state:%s\n",
			transport->obj_name, transport_state2str(state));
		switch (state) {
		case PJSIP_TP_STATE_CONNECTED:
			monitored = ao2_alloc_options(sizeof(*monitored),
				transport_monitor_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
			if (!monitored) {
				break;
			}
			monitored->transport = transport;
			if (AST_VECTOR_INIT(&monitored->monitors, 5)) {
				ao2_ref(monitored, -1);
				break;
			}

			ao2_link(transports, monitored);
			ao2_ref(monitored, -1);
			break;
		case PJSIP_TP_STATE_DISCONNECTED:
			if (!transport->is_shutdown) {
				pjsip_transport_shutdown(transport);
			}
			transport_state_do_reg_callbacks(transports, transport);
			break;
		case PJSIP_TP_STATE_SHUTDOWN:
			/*
			 * Set shutdown flag early so we can force a new transport to be
			 * created if a monitor callback needs to reestablish a link.
			 * PJPROJECT sets the flag after this routine returns even though
			 * it has already called the transport's shutdown routine.
			 */
			transport->is_shutdown = PJ_TRUE;

			transport_state_do_reg_callbacks(transports, transport);
			break;
		case PJSIP_TP_STATE_DESTROY:
			transport_state_do_reg_callbacks(transports, transport);
			break;
		default:
			/*
			 * We have to have a default case because the enum is
			 * defined by a third-party library.
			 */
			ast_assert(0);
			break;
		}

		ao2_ref(transports, -1);
	}

	/* Loop over other transport state callbacks registered with us. */
	if (!AST_LIST_EMPTY(&transport_state_list)) {
		struct ast_sip_tpmgr_state_callback *tpmgr_notifier;

		AST_RWLIST_RDLOCK(&transport_state_list);
		AST_LIST_TRAVERSE(&transport_state_list, tpmgr_notifier, node) {
			tpmgr_notifier->cb(transport, state, info);
		}
		AST_RWLIST_UNLOCK(&transport_state_list);
	}

	/* Forward to the old state callback if present */
	if (tpmgr_state_callback) {
		tpmgr_state_callback(transport, state, info);
	}
}

struct callback_data {
	ast_transport_monitor_shutdown_cb cb;
	void *data;
	ast_transport_monitor_data_matcher matches;
};

static int transport_monitor_unregister_cb(void *obj, void *arg, int flags)
{
	struct transport_monitor *monitored = obj;
	struct callback_data *cb_data = arg;
	int idx;

	for (idx = AST_VECTOR_SIZE(&monitored->monitors); idx--;) {
		struct transport_monitor_notifier *notifier;

		notifier = AST_VECTOR_GET_ADDR(&monitored->monitors, idx);
		if (notifier->cb == cb_data->cb && (!cb_data->data
			|| cb_data->matches(cb_data->data, notifier->data))) {
			ao2_cleanup(notifier->data);
			AST_VECTOR_REMOVE_UNORDERED(&monitored->monitors, idx);
			ast_debug(3, "Unregistered monitor %p(%p) from transport %s\n",
				notifier->cb, notifier->data, monitored->transport->obj_name);
		}
	}
	return 0;
}

static int ptr_matcher(void *a, void *b)
{
	return a == b;
}

void ast_sip_transport_monitor_unregister_all(ast_transport_monitor_shutdown_cb cb,
	void *data, ast_transport_monitor_data_matcher matches)
{
	struct ao2_container *transports;
	struct callback_data cb_data = {
		.cb = cb,
		.data = data,
		.matches = matches ?: ptr_matcher,
	};

	ast_assert(cb != NULL);

	transports = ao2_global_obj_ref(active_transports);
	if (!transports) {
		return;
	}
	ao2_callback(transports, OBJ_MULTIPLE | OBJ_NODATA, transport_monitor_unregister_cb, &cb_data);
	ao2_ref(transports, -1);
}

void ast_sip_transport_monitor_unregister(pjsip_transport *transport,
	ast_transport_monitor_shutdown_cb cb, void *data, ast_transport_monitor_data_matcher matches)
{
	struct ao2_container *transports;
	struct transport_monitor *monitored;

	ast_assert(transport != NULL && cb != NULL);

	transports = ao2_global_obj_ref(active_transports);
	if (!transports) {
		return;
	}

	ao2_lock(transports);
	monitored = ao2_find(transports, transport->obj_name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (monitored) {
		struct callback_data cb_data = {
			.cb = cb,
			.data = data,
			.matches = matches ?: ptr_matcher,
		};

		transport_monitor_unregister_cb(monitored, &cb_data, 0);
		ao2_ref(monitored, -1);
	}
	ao2_unlock(transports);
	ao2_ref(transports, -1);
}

enum ast_transport_monitor_reg ast_sip_transport_monitor_register(pjsip_transport *transport,
	ast_transport_monitor_shutdown_cb cb, void *ao2_data)
{
	return ast_sip_transport_monitor_register_replace(transport, cb, ao2_data, NULL);
}

enum ast_transport_monitor_reg ast_sip_transport_monitor_register_replace(pjsip_transport *transport,
	ast_transport_monitor_shutdown_cb cb, void *ao2_data, ast_transport_monitor_data_matcher matches)
{
	struct ao2_container *transports;
	struct transport_monitor *monitored;
	enum ast_transport_monitor_reg res = AST_TRANSPORT_MONITOR_REG_NOT_FOUND;

	ast_assert(transport != NULL && cb != NULL);

	transports = ao2_global_obj_ref(active_transports);
	if (!transports) {
		return res;
	}

	ao2_lock(transports);
	monitored = ao2_find(transports, transport->obj_name, OBJ_SEARCH_KEY | OBJ_NOLOCK);
	if (monitored) {
		struct transport_monitor_notifier new_monitor;
		struct callback_data cb_data = {
			.cb = cb,
			.data = ao2_data,
			.matches = matches ?: ptr_matcher,
		};

		transport_monitor_unregister_cb(monitored, &cb_data, 0);

		/* Add new monitor to vector */
		new_monitor.cb = cb;
		new_monitor.data = ao2_bump(ao2_data);
		if (AST_VECTOR_APPEND(&monitored->monitors, new_monitor)) {
			ao2_cleanup(ao2_data);
			res = AST_TRANSPORT_MONITOR_REG_FAILED;
			ast_debug(3, "Register monitor %p(%p) to transport %s FAILED\n",
				cb, ao2_data, transport->obj_name);
		} else {
			res = AST_TRANSPORT_MONITOR_REG_SUCCESS;
			ast_debug(3, "Registered monitor %p(%p) to transport %s\n",
				cb, ao2_data, transport->obj_name);
		}

		ao2_ref(monitored, -1);
	}
	ao2_unlock(transports);
	ao2_ref(transports, -1);
	return res;
}

void ast_sip_transport_state_unregister(struct ast_sip_tpmgr_state_callback *element)
{
	AST_RWLIST_WRLOCK(&transport_state_list);
	AST_LIST_REMOVE(&transport_state_list, element, node);
	AST_RWLIST_UNLOCK(&transport_state_list);
}

void ast_sip_transport_state_register(struct ast_sip_tpmgr_state_callback *element)
{
	struct ast_sip_tpmgr_state_callback *tpmgr_notifier;

	AST_RWLIST_WRLOCK(&transport_state_list);
	AST_LIST_TRAVERSE(&transport_state_list, tpmgr_notifier, node) {
		if (element == tpmgr_notifier) {
			/* Already registered. */
			AST_RWLIST_UNLOCK(&transport_state_list);
			return;
		}
	}
	AST_LIST_INSERT_HEAD(&transport_state_list, element, node);
	AST_RWLIST_UNLOCK(&transport_state_list);
}

void ast_sip_destroy_transport_events(void)
{
	pjsip_tpmgr *tpmgr;

	tpmgr = pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint());
	if (tpmgr) {
		pjsip_tpmgr_set_state_cb(tpmgr, tpmgr_state_callback);
	}

	ao2_global_obj_release(active_transports);
}

int ast_sip_initialize_transport_events(void)
{
	pjsip_tpmgr *tpmgr;
	struct ao2_container *transports;

	tpmgr = pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint());
	if (!tpmgr) {
		return -1;
	}

	transports = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		ACTIVE_TRANSPORTS_BUCKETS, transport_monitor_hash_fn, NULL,
		transport_monitor_cmp_fn);
	if (!transports) {
		return -1;
	}
	ao2_global_obj_replace_unref(active_transports, transports);
	ao2_ref(transports, -1);

	tpmgr_state_callback = pjsip_tpmgr_get_state_cb(tpmgr);
	pjsip_tpmgr_set_state_cb(tpmgr, &transport_state_callback);

	return 0;
}
