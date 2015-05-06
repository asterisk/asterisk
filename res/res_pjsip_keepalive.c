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

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"

/*! \brief Number of buckets for keepalive transports */
#define KEEPALIVE_TRANSPORTS_BUCKETS 53

/*! \brief The keep alive packet to send */
static const pj_str_t keepalive_packet = { "\r\n\r\n", 4 };

/*! \brief Global container of active transports */
static struct ao2_container *transports;

/*! \brief Thread keeping things alive */
static pthread_t keepalive_thread = AST_PTHREADT_NULL;

/*! \brief The global interval at which to send keepalives */
static unsigned int keepalive_interval;

/*! \brief Existing transport manager callback that we need to invoke */
static pjsip_tp_state_callback tpmgr_state_callback;

/*! \brief Structure for transport to be kept alive */
struct keepalive_transport {
	/*! \brief The underlying PJSIP transport */
	pjsip_transport *transport;
};

/*! \brief Callback function to send keepalive */
static int keepalive_transport_cb(void *obj, void *arg, int flags)
{
	struct keepalive_transport *keepalive = obj;
	pjsip_tpselector selector = {
		.type = PJSIP_TPSELECTOR_TRANSPORT,
		.u.transport = keepalive->transport,
	};

	pjsip_tpmgr_send_raw(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()),
		keepalive->transport->key.type, &selector, NULL, keepalive_packet.ptr, keepalive_packet.slen,
		&keepalive->transport->key.rem_addr, pj_sockaddr_get_len(&keepalive->transport->key.rem_addr),
		NULL, NULL);

	return 0;
}

/*! \brief Thread which sends keepalives to all active connection-oriented transports */
static void *keepalive_transport_thread(void *data)
{
	pj_thread_desc desc;
	pj_thread_t *thread;

	if (pj_thread_register("Asterisk Keepalive Thread", desc, &thread) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Could not register keepalive thread with PJLIB, keepalives will not occur.\n");
		return NULL;
	}

	/* Once loaded this module just keeps on going as it is unsafe to stop and change the underlying
	 * callback for the transport manager.
	 */
	while (1) {
		sleep(keepalive_interval);
		ao2_callback(transports, OBJ_NODATA, keepalive_transport_cb, NULL);
	}

	return NULL;
}

/*! \brief Destructor for keepalive transport */
static void keepalive_transport_destroy(void *obj)
{
	struct keepalive_transport *keepalive = obj;

	pjsip_transport_dec_ref(keepalive->transport);
}

/*! \brief Callback invoked when transport changes occur */
static void keepalive_transport_state_callback(pjsip_transport *transport, pjsip_transport_state state,
	const pjsip_transport_state_info *info)
{
	/* We only care about connection-oriented transports */
	if (transport->flag & PJSIP_TRANSPORT_RELIABLE) {
		struct keepalive_transport *keepalive;

		switch (state) {
		case PJSIP_TP_STATE_CONNECTED:
			keepalive = ao2_alloc(sizeof(*keepalive), keepalive_transport_destroy);
			if (keepalive) {
				keepalive->transport = transport;
				pjsip_transport_add_ref(keepalive->transport);
				ao2_link(transports, keepalive);
				ao2_ref(keepalive, -1);
			}
			break;
		case PJSIP_TP_STATE_DISCONNECTED:
			ao2_find(transports, transport->obj_name, OBJ_SEARCH_KEY | OBJ_NODATA | OBJ_UNLINK);
			break;
		default:
			break;
		}
	}

	/* Forward to the old state callback if present */
	if (tpmgr_state_callback) {
		tpmgr_state_callback(transport, state, info);
	}
}

/*! \brief Hashing function for keepalive transport */
static int keepalive_transport_hash_fn(const void *obj, int flags)
{
	const struct keepalive_transport *object;
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

/*! \brief Comparison function for keepalive transport */
static int keepalive_transport_cmp_fn(void *obj, void *arg, int flags)
{
	const struct keepalive_transport *object_left = obj;
	const struct keepalive_transport *object_right = arg;
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

	return !cmp ? CMP_MATCH | CMP_STOP : 0;
}

static void keepalive_global_loaded(const char *object_type)
{
	unsigned int new_interval = ast_sip_get_keep_alive_interval();
	pjsip_tpmgr *tpmgr;

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

	transports = ao2_container_alloc(KEEPALIVE_TRANSPORTS_BUCKETS, keepalive_transport_hash_fn,
		keepalive_transport_cmp_fn);
	if (!transports) {
		ast_log(LOG_ERROR, "Could not create container for transports to perform keepalive on.\n");
		return;
	}

	tpmgr = pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint());
	if (!tpmgr) {
		ast_log(LOG_ERROR, "No transport manager to attach keepalive functionality to.\n");
		ao2_ref(transports, -1);
		return;
	}

	if (ast_pthread_create(&keepalive_thread, NULL, keepalive_transport_thread, NULL)) {
		ast_log(LOG_ERROR, "Could not create thread for sending keepalive messages.\n");
		ao2_ref(transports, -1);
		return;
	}

	tpmgr_state_callback = pjsip_tpmgr_get_state_cb(tpmgr);
	pjsip_tpmgr_set_state_cb(tpmgr, &keepalive_transport_state_callback);
}

/*! \brief Observer which is used to update our interval when the global setting changes */
static struct ast_sorcery_observer keepalive_global_observer = {
	.loaded = keepalive_global_loaded,
};

static int load_module(void)
{
	CHECK_PJSIP_MODULE_LOADED();

	ast_sorcery_observer_add(ast_sip_get_sorcery(), "global", &keepalive_global_observer);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");
	ast_module_shutdown_ref(ast_module_info->self);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	/* This will never get called */
	return 0;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "global");
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Stateful Connection Keepalive Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.reload = reload_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 4,
);
