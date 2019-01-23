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

#include "asterisk.h"

#include <signal.h>

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "include/res_pjsip_private.h"

/*! \brief Number of buckets for monitored transports */
#define TRANSPORTS_BUCKETS 127

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

/*! \brief Structure for transport to be monitored */
struct monitored_transport {
	/*! \brief The underlying PJSIP transport */
	pjsip_transport *transport;
	/*! \brief Non-zero if a PJSIP request was received */
	int sip_received;
};

static void keepalive_transport_send_keepalive(struct monitored_transport *monitored)
{
	pjsip_tpselector selector = {
		.type = PJSIP_TPSELECTOR_TRANSPORT,
		.u.transport = monitored->transport,
	};

	pjsip_tpmgr_send_raw(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()),
		monitored->transport->key.type,
		&selector,
		NULL,
		keepalive_packet.ptr,
		keepalive_packet.slen,
		&monitored->transport->key.rem_addr,
		pj_sockaddr_get_len(&monitored->transport->key.rem_addr),
		NULL, NULL);
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

	/*
	 * Once loaded this module just keeps on going as it is unsafe to stop
	 * and change the underlying callback for the transport manager.
	 */
	while (keepalive_interval) {
		struct ao2_iterator iter;
		struct monitored_transport *monitored;

		sleep(keepalive_interval);

		/*
		 * We must use the iterator to avoid deadlock between the container lock
		 * and the pjproject transport manager group lock when sending
		 * the keepalive packet.
		 */
		iter = ao2_iterator_init(transports, 0);
		for (; (monitored = ao2_iterator_next(&iter)); ao2_ref(monitored, -1)) {
			keepalive_transport_send_keepalive(monitored);
		}
		ao2_iterator_destroy(&iter);
	}

	ao2_ref(transports, -1);
	return NULL;
}

AST_THREADSTORAGE(desc_storage);

static int idle_sched_init_pj_thread(void)
{
	if (!pj_thread_is_registered()) {
		pj_thread_t *thread;
		pj_thread_desc *desc;

		desc = ast_threadstorage_get(&desc_storage, sizeof(pj_thread_desc));
		if (!desc) {
			ast_log(LOG_ERROR, "Could not get thread desc from thread-local storage.\n");
			return -1;
		}

		pj_bzero(*desc, sizeof(*desc));

		pj_thread_register("Transport Monitor", *desc, &thread);
	}

	return 0;
}

static struct monitored_transport *get_monitored_transport_by_name(const char *obj_name)
{
	struct ao2_container *transports;
	struct monitored_transport *monitored = NULL;

	transports = ao2_global_obj_ref(monitored_transports);
	if (transports) {
		monitored = ao2_find(transports, obj_name, OBJ_SEARCH_KEY);
	}
	ao2_cleanup(transports);

	/* Caller is responsible for cleaning up reference */
	return monitored;
}

static int idle_sched_cb(const void *data)
{
	char *obj_name = (char *) data;
	struct monitored_transport *monitored;

	if (idle_sched_init_pj_thread()) {
		ast_free(obj_name);
		return 0;
	}

	monitored = get_monitored_transport_by_name(obj_name);
	if (monitored) {
		if (!monitored->sip_received) {
			ast_log(LOG_NOTICE, "Shutting down transport '%s' since no request was received in %d seconds\n",
				monitored->transport->info, IDLE_TIMEOUT / 1000);
			pjsip_transport_shutdown(monitored->transport);
		}
		ao2_ref(monitored, -1);
	}

	ast_free(obj_name);
	return 0;
}

static int idle_sched_cleanup(const void *data)
{
	char *obj_name = (char *) data;
	struct monitored_transport *monitored;

	if (idle_sched_init_pj_thread()) {
		ast_free(obj_name);
		return 0;
	}

	monitored = get_monitored_transport_by_name(obj_name);
	if (monitored) {
		pjsip_transport_shutdown(monitored->transport);
		ao2_ref(monitored, -1);
	}

	ast_free(obj_name);
	return 0;
}

/*! \brief Destructor for keepalive transport */
static void monitored_transport_destroy(void *obj)
{
	struct monitored_transport *monitored = obj;

	pjsip_transport_dec_ref(monitored->transport);
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
				char *obj_name = ast_strdup(transport->obj_name);

				if (!obj_name
				   || ast_sched_add_variable(sched, IDLE_TIMEOUT, idle_sched_cb, obj_name, 1) < 0) {
					/* Shut down the transport if anything fails */
					pjsip_transport_shutdown(transport);
					ast_free(obj_name);
				}
			}
			ao2_ref(monitored, -1);
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
}

struct ast_sip_tpmgr_state_callback monitored_transport_reg = {
	monitored_transport_state_callback,
};

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
	struct monitored_transport *idle_trans;

	idle_trans = get_monitored_transport_by_name(rdata->tp_info.transport->obj_name);
	if (idle_trans) {
		idle_trans->sip_received = 1;
		ao2_ref(idle_trans, -1);
	}

	return PJ_FALSE;
}

static pjsip_module idle_monitor_module = {
	.name = {"idle monitor module", 19},
	.priority = PJSIP_MOD_PRIORITY_TRANSPORT_LAYER + 3,
	.on_rx_request = idle_monitor_on_rx_request,
};

int ast_sip_initialize_transport_management(void)
{
	struct ao2_container *transports;

	transports = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0, TRANSPORTS_BUCKETS,
		monitored_transport_hash_fn, NULL, monitored_transport_cmp_fn);
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

	ast_sip_transport_state_register(&monitored_transport_reg);

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &keepalive_global_observer);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");

	return AST_MODULE_LOAD_SUCCESS;
}

void ast_sip_destroy_transport_management(void)
{
	if (keepalive_interval) {
		keepalive_interval = 0;
		if (keepalive_thread != AST_PTHREADT_NULL) {
			pthread_kill(keepalive_thread, SIGURG);
			pthread_join(keepalive_thread, NULL);
			keepalive_thread = AST_PTHREADT_NULL;
		}
	}

	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "global", &keepalive_global_observer);

	ast_sip_transport_state_unregister(&monitored_transport_reg);

	ast_sip_unregister_service(&idle_monitor_module);

	ast_sched_clean_by_callback(sched, idle_sched_cb, idle_sched_cleanup);
	ast_sched_context_destroy(sched);
	sched = NULL;

	ao2_global_obj_release(monitored_transports);
}
