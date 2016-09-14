/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <signal.h>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"

/*! \brief Number of buckets for keepalive transports */
#define TRANSPORTS_BUCKETS 53

#define IDLE_TIMEOUT (pjsip_cfg()->tsx.td)

/*! \brief The keep alive packet to send */
static const pj_str_t keepalive_packet = { "\r\n\r\n", 4 };

/*! \brief Global container of active transports */
static AO2_GLOBAL_OBJ_STATIC(monitored_transports);

/*! \brief Scheduler context for timing out connections with no data received */
static struct ast_sched_context *sched;

/*! \brief Thread keeping things alive */
static pthread_t keepalive_thread = AST_PTHREADT_NULL;

/*! \brief The global interval at which to send keepalives */
static unsigned int keepalive_interval;

/*! \brief Existing transport manager callback that we need to invoke */
static pjsip_tp_state_callback tpmgr_state_callback;

/*! \brief Structure for transport to be monitored */
struct monitored_transport {
	/*! \brief The underlying PJSIP transport */
	pjsip_transport *transport;
	/*! \brief Non-zero if a PJSIP request was received */
	int sip_received;
};

/*! \brief Callback function to send keepalive */
static int keepalive_transport_cb(void *obj, void *arg, int flags)
{
	struct monitored_transport *monitored = obj;
	pjsip_tpselector selector = {
		.type = PJSIP_TPSELECTOR_TRANSPORT,
		.u.transport = monitored->transport,
	};

	pjsip_tpmgr_send_raw(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()),
		monitored->transport->key.type, &selector, NULL, keepalive_packet.ptr, keepalive_packet.slen,
		&monitored->transport->key.rem_addr, pj_sockaddr_get_len(&monitored->transport->key.rem_addr),
		NULL, NULL);

	return 0;
}

/*! \brief Thread which sends keepalives to all active connection-oriented transports */
static void *keepalive_transport_thread(void *data)
{
	struct ao2_container *transports;
	pj_thread_desc desc;
	pj_thread_t *thread;

	if (pj_thread_register("Asterisk Keepalive Thread", desc, &thread) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Could not register keepalive thread with PJLIB, keepalives will not occur.\n");
		return NULL;
	}

	transports = ao2_global_obj_ref(monitored_transports);
	if (!transports) {
		return NULL;
	}

	/* Once loaded this module just keeps on going as it is unsafe to stop and change the underlying
	 * callback for the transport manager.
	 */
	while (keepalive_interval) {
		sleep(keepalive_interval);
		ao2_callback(transports, OBJ_NODATA, keepalive_transport_cb, NULL);
	}

	ao2_ref(transports, -1);
	return NULL;
}

AST_THREADSTORAGE(desc_storage);

static int idle_sched_cb(const void *data)
{
	struct monitored_transport *keepalive = (struct monitored_transport *) data;

	if (!pj_thread_is_registered()) {
		pj_thread_t *thread;
		pj_thread_desc *desc;

		desc = ast_threadstorage_get(&desc_storage, sizeof(pj_thread_desc));
		if (!desc) {
			ast_log(LOG_ERROR, "Could not get thread desc from thread-local storage.\n");
			ao2_ref(keepalive, -1);
			return 0;
		}

		pj_bzero(*desc, sizeof(*desc));

		pj_thread_register("Transport Monitor", *desc, &thread);
	}

	if (!keepalive->sip_received) {
		ast_log(LOG_NOTICE, "Shutting down transport '%s' since no request was received in %d seconds\n",
				keepalive->transport->info, IDLE_TIMEOUT / 1000);
		pjsip_transport_shutdown(keepalive->transport);
	}

	ao2_ref(keepalive, -1);
	return 0;
}

/*! \brief Destructor for keepalive transport */
static void monitored_transport_destroy(void *obj)
{
	struct monitored_transport *keepalive = obj;

	pjsip_transport_dec_ref(keepalive->transport);
}

/*! \brief Callback invoked when transport changes occur */
static void monitored_transport_state_callback(pjsip_transport *transport, pjsip_transport_state state,
	const pjsip_transport_state_info *info)
{
	struct ao2_container *transports;

	/* We only care about reliable transports */
	if (PJSIP_TRANSPORT_IS_RELIABLE(transport)
		&& (transport->dir == PJSIP_TP_DIR_INCOMING || keepalive_interval)
		&& (transports = ao2_global_obj_ref(monitored_transports))) {
		struct monitored_transport *monitored;

		switch (state) {
		case PJSIP_TP_STATE_CONNECTED:
			monitored = ao2_alloc_options(sizeof(*monitored),
				monitored_transport_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
			if (!monitored) {
				break;
			}
			monitored->transport = transport;
			pjsip_transport_add_ref(monitored->transport);

			ao2_link(transports, monitored);

			if (transport->dir == PJSIP_TP_DIR_INCOMING) {
				/* Let the scheduler inherit the reference from allocation */
				if (ast_sched_add_variable(sched, IDLE_TIMEOUT, idle_sched_cb, monitored, 1) < 0) {
					/* Uh Oh.  Could not schedule the idle check.  Kill the transport. */
					ao2_unlink(transports, monitored);
					ao2_ref(monitored, -1);
					pjsip_transport_shutdown(transport);
				}
			} else {
				/* No scheduled task, so get rid of the allocation reference */
				ao2_ref(monitored, -1);
			}
			break;
		case PJSIP_TP_STATE_SHUTDOWN:
		case PJSIP_TP_STATE_DISCONNECTED:
			ao2_find(transports, transport->obj_name, OBJ_SEARCH_KEY | OBJ_NODATA | OBJ_UNLINK);
			break;
		default:
			break;
		}

		ao2_ref(transports, -1);
	}

	/* Forward to the old state callback if present */
	if (tpmgr_state_callback) {
		tpmgr_state_callback(transport, state, info);
	}
}

/*! \brief Hashing function for monitored transport */
static int monitored_transport_hash_fn(const void *obj, int flags)
{
	const struct monitored_transport *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->transport->obj_name;
		break;
	default:
		/* Hash can only work on something with a full key. */
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief Comparison function for monitored transport */
static int monitored_transport_cmp_fn(void *obj, void *arg, int flags)
{
	const struct monitored_transport *object_left = obj;
	const struct monitored_transport *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->transport->obj_name;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->transport->obj_name, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/*
		 * We could also use a partial key struct containing a length
		 * so strlen() does not get called for every comparison instead.
		 */
		cmp = strncmp(object_left->transport->obj_name, right_key, strlen(right_key));
		break;
	default:
		/*
		 * What arg points to is specific to this traversal callback
		 * and has no special meaning to astobj2.
		 */
		cmp = 0;
		break;
	}

	return !cmp ? CMP_MATCH : 0;
}

static void keepalive_global_loaded(const char *object_type)
{
	unsigned int new_interval = ast_sip_get_keep_alive_interval();

	if (new_interval) {
		keepalive_interval = new_interval;
	} else if (keepalive_interval) {
		ast_log(LOG_NOTICE, "Keepalive support can not be disabled once activated.\n");
		return;
	} else {
		/* This will occur if no keepalive interval has been specified at initial start */
		return;
	}

	if (keepalive_thread != AST_PTHREADT_NULL) {
		return;
	}

	if (ast_pthread_create(&keepalive_thread, NULL, keepalive_transport_thread, NULL)) {
		ast_log(LOG_ERROR, "Could not create thread for sending keepalive messages.\n");
		keepalive_thread = AST_PTHREADT_NULL;
		keepalive_interval = 0;
	}
}

/*! \brief Observer which is used to update our interval when the global setting changes */
static struct ast_sorcery_observer keepalive_global_observer = {
	.loaded = keepalive_global_loaded,
};

/*!
 * \brief
 * On incoming TCP connections, when we receive a SIP request, we mark that we have
 * received a valid SIP request. This way, we will not shut the transport down for
 * idleness
 */
static pj_bool_t idle_monitor_on_rx_request(pjsip_rx_data *rdata)
{
	struct ao2_container *transports;
	struct monitored_transport *idle_trans;

	transports = ao2_global_obj_ref(monitored_transports);
	if (!transports) {
		return PJ_FALSE;
	}

	idle_trans = ao2_find(transports, rdata->tp_info.transport->obj_name, OBJ_SEARCH_KEY);
	ao2_ref(transports, -1);
	if (!idle_trans) {
		return PJ_FALSE;
	}

	idle_trans->sip_received = 1;
	ao2_ref(idle_trans, -1);

	return PJ_FALSE;
}

static pjsip_module idle_monitor_module = {
	.name = {"idle monitor module", 19},
	.priority = PJSIP_MOD_PRIORITY_TRANSPORT_LAYER + 3,
	.on_rx_request = idle_monitor_on_rx_request,
};

static int load_module(void)
{
	struct ao2_container *transports;
	pjsip_tpmgr *tpmgr;

	CHECK_PJSIP_MODULE_LOADED();

	tpmgr = pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint());
	if (!tpmgr) {
		ast_log(LOG_ERROR, "No transport manager to attach keepalive functionality to.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	transports = ao2_container_alloc(TRANSPORTS_BUCKETS, monitored_transport_hash_fn,
		monitored_transport_cmp_fn);
	if (!transports) {
		ast_log(LOG_ERROR, "Could not create container for transports to perform keepalive on.\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	ao2_global_obj_replace_unref(monitored_transports, transports);
	ao2_ref(transports, -1);

	sched = ast_sched_context_create();
	if (!sched) {
		ast_log(LOG_ERROR, "Failed to create keepalive scheduler context.\n");
		ao2_global_obj_release(monitored_transports);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sched_start_thread(sched)) {
		ast_log(LOG_ERROR, "Failed to start keepalive scheduler thread\n");
		ast_sched_context_destroy(sched);
		sched = NULL;
		ao2_global_obj_release(monitored_transports);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sip_register_service(&idle_monitor_module);

	tpmgr_state_callback = pjsip_tpmgr_get_state_cb(tpmgr);
	pjsip_tpmgr_set_state_cb(tpmgr, &monitored_transport_state_callback);

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &keepalive_global_observer);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");

	ast_module_shutdown_ref(ast_module_info->self);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	pjsip_tpmgr *tpmgr;

	if (keepalive_interval) {
		keepalive_interval = 0;
		if (keepalive_thread != AST_PTHREADT_NULL) {
			pthread_kill(keepalive_thread, SIGURG);
			pthread_join(keepalive_thread, NULL);
			keepalive_thread = AST_PTHREADT_NULL;
		}
	}

	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "global", &keepalive_global_observer);

	tpmgr = pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint());
	if (tpmgr) {
		pjsip_tpmgr_set_state_cb(tpmgr, tpmgr_state_callback);
	}

	ast_sip_unregister_service(&idle_monitor_module);

	ast_sched_context_destroy(sched);
	sched = NULL;

	ao2_global_obj_release(monitored_transports);

	return 0;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Reliable Transport Management",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.reload = reload_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 4,
);
