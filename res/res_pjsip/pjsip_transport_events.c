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
#include "asterisk/res_pjsip_cli.h"
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
	/*! \brief Key <ipaddr>:<port> */
	char key[IP6ADDR_COLON_PORT_BUFLEN];
	/*! \brief The underlying PJSIP transport */
	pjsip_transport *transport;
	/*! For debugging purposes, we save the obj_name
	 * in case the transport goes away.
	 */
	char *transport_obj_name;
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
AO2_STRING_FIELD_HASH_FN(transport_monitor, key);

/*! \brief Comparison function for struct transport_monitor */
AO2_STRING_FIELD_CMP_FN(transport_monitor, key);

/*! \brief Sort function for struct transport_monitor */
AO2_STRING_FIELD_SORT_FN(transport_monitor, key);

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
	ast_debug(3, "Transport %s(%s,%s) RefCnt: %ld : state:MONITOR_DESTROYED\n",
		monitored->key, monitored->transport->obj_name,
		monitored->transport->type_name,pj_atomic_get(monitored->transport->ref_cnt));
	ast_free(monitored->transport_obj_name);
	pjsip_transport_dec_ref(monitored->transport);
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
	char key[IP6ADDR_COLON_PORT_BUFLEN];

	AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR(transport, key);

	monitored = ao2_find(transports, key, OBJ_SEARCH_KEY | OBJ_UNLINK);
	if (monitored) {
		int idx;

		for (idx = AST_VECTOR_SIZE(&monitored->monitors); idx--;) {
			struct transport_monitor_notifier *notifier;

			notifier = AST_VECTOR_GET_ADDR(&monitored->monitors, idx);
			ast_debug(3, "Transport %s(%s,%s) RefCnt: %ld : running callback %p(%p)\n",
				monitored->key, monitored->transport->obj_name,
				monitored->transport->type_name,
				pj_atomic_get(monitored->transport->ref_cnt), notifier->cb, notifier->data);
			notifier->cb(notifier->data);
		}
		ao2_ref(monitored, -1);
	}
}

static void verify_log_result(int log_level, const pjsip_transport *transport,
	pj_uint32_t verify_status)
{
	const char *status[32];
	unsigned int count;
	unsigned int i;

	count = ARRAY_LEN(status);

	if (pj_ssl_cert_get_verify_status_strings(verify_status, status, &count) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Error retrieving certificate verification result(s)\n");
		return;
	}

	for (i = 0; i < count; ++i) {
		ast_log(log_level, _A_, "Transport '%s' to remote '%.*s' - %s\n", transport->factory->info,
			(int)pj_strlen(&transport->remote_name.host), pj_strbuf(&transport->remote_name.host),
			status[i]);
	}
}

static int verify_cert_name(const pj_str_t *local, const pj_str_t *remote)
{
	const char *p;
	pj_ssize_t size;

	ast_debug(3, "Verify certificate name: local = %.*s, remote = %.*s\n",
		(unsigned int)pj_strlen(local), pj_strbuf(local),
		(unsigned int)pj_strlen(remote), pj_strbuf(remote));

	if (!pj_stricmp(remote, local)) {
		return 1;
	}

	if (pj_strnicmp2(remote, "*.", 2)) {
		return 0;
	}

	p = pj_strchr(local, '.');
	if (!p) {
		return 0;
	}

	size = pj_strbuf(local) + pj_strlen(local) - ++p;

	return size == pj_strlen(remote) - 2 ?
		!pj_memcmp(pj_strbuf(remote) + 2,  p, size) : 0;
}

static int verify_cert_names(const pj_str_t *host, const pj_ssl_cert_info *remote)
{
	unsigned int i;

	for (i = 0; i < remote->subj_alt_name.cnt; ++i) {
		/*
		 * DNS is the only type we're matching wildcards against,
		 * so only recheck those.
		 */
		if (remote->subj_alt_name.entry[i].type == PJ_SSL_CERT_NAME_DNS
			&& verify_cert_name(host, &remote->subj_alt_name.entry[i].name)) {
			return 1;
		}
	}

	return verify_cert_name(host, &remote->subject.cn);
}

static int transport_tls_verify(const pjsip_transport *transport,
	const pjsip_tls_state_info *state_info)
{
	pj_uint32_t verify_status;
	const struct ast_sip_transport_state *state;

	if (transport->dir == PJSIP_TP_DIR_INCOMING) {
		return 1;
	}

	/* transport_id should always be in factory info (see config_transport) */
	ast_assert(!ast_strlen_zero(transport->factory->info));

	state = ast_sip_get_transport_state(transport->factory->info);
	if (!state) {
		/*
		 * There should always be an associated state, but if for some
		 * reason there is not then fail verification
		 */
		ast_log(LOG_ERROR, "Transport state not found for '%s'\n", transport->factory->info);
		return 0;
	}

	verify_status = state_info->ssl_sock_info->verify_status;

	/*
	 * By this point pjsip has already completed its verification process. If
	 * there was a name matching error it could be because they disallow wildcards.
	 * If this transport has been configured to allow wildcards then we'll need
	 * to re-check the name(s) for such.
	 */
	if (state->allow_wildcard_certs &&
			(verify_status & PJ_SSL_CERT_EIDENTITY_NOT_MATCH)) {
		if (verify_cert_names(&transport->remote_name.host,
			state_info->ssl_sock_info->remote_cert_info)) {
			/* A name matched a wildcard, so clear the error */
			verify_status &= ~PJ_SSL_CERT_EIDENTITY_NOT_MATCH;
		}
	}

	if (state->verify_server && verify_status != PJ_SSL_CERT_ESUCCESS) {
		verify_log_result(__LOG_ERROR, transport, verify_status);
		return 0;
	}

	verify_log_result(__LOG_NOTICE, transport, verify_status);
	return 1;
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

		ast_debug(3, "Transport " PJSTR_PRINTF_SPEC ":%d(%s,%s): RefCnt: %ld state:%s\n",
			PJSTR_PRINTF_VAR(transport->remote_name.host),
			transport->remote_name.port, transport->obj_name,
			transport->type_name,
			pj_atomic_get(transport->ref_cnt), transport_state2str(state));
		switch (state) {
		case PJSIP_TP_STATE_CONNECTED:
			if (PJSIP_TRANSPORT_IS_SECURE(transport) &&
				!transport_tls_verify(transport, info->ext_info)) {
				pjsip_transport_shutdown(transport);
				return;
			}

			monitored = ao2_alloc_options(sizeof(*monitored),
				transport_monitor_dtor, AO2_ALLOC_OPT_LOCK_NOLOCK);
			if (!monitored) {
				break;
			}
			monitored->transport = transport;
			AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR(transport, monitored->key);
			monitored->transport_obj_name = ast_strdup(transport->obj_name);

			if (AST_VECTOR_INIT(&monitored->monitors, 5)) {
				ao2_ref(monitored, -1);
				break;
			}
			pjsip_transport_add_ref(monitored->transport);
			ast_debug(3, "Transport %s(%s,%s): RefCnt: %ld state:MONITOR_CREATED\n",
				monitored->key,	monitored->transport_obj_name,
				monitored->transport->type_name,
				pj_atomic_get(monitored->transport->ref_cnt));

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
			ast_debug(3, "Transport %s(%s,%s) RefCnt: %ld : Unregistered monitor %p(%p)\n",
				monitored->key, monitored->transport_obj_name,
				monitored->transport->type_name,
				pj_atomic_get(monitored->transport->ref_cnt), notifier->cb, notifier->data);
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
	char key[IP6ADDR_COLON_PORT_BUFLEN];
	AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR(transport, key);
	ast_sip_transport_monitor_unregister_key(key, cb, data, matches);
}

void ast_sip_transport_monitor_unregister_key(const char *transport_key,
	ast_transport_monitor_shutdown_cb cb, void *data, ast_transport_monitor_data_matcher matches)
{
	struct ao2_container *transports;
	struct transport_monitor *monitored;

	ast_assert(transport_key != NULL && cb != NULL);

	transports = ao2_global_obj_ref(active_transports);
	if (!transports) {
		return;
	}

	ao2_lock(transports);
	monitored = ao2_find(transports, transport_key, OBJ_SEARCH_KEY | OBJ_NOLOCK);
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
	char key[IP6ADDR_COLON_PORT_BUFLEN];
	AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR(transport, key);

	return ast_sip_transport_monitor_register_replace_key(key, cb, ao2_data, NULL);
}

enum ast_transport_monitor_reg ast_sip_transport_monitor_register_key(const char *transport_key,
	ast_transport_monitor_shutdown_cb cb, void *ao2_data)
{
	return ast_sip_transport_monitor_register_replace_key(transport_key, cb, ao2_data, NULL);
}

enum ast_transport_monitor_reg ast_sip_transport_monitor_register_replace(pjsip_transport *transport,
	ast_transport_monitor_shutdown_cb cb, void *ao2_data, ast_transport_monitor_data_matcher matches)
{
	char key[IP6ADDR_COLON_PORT_BUFLEN];

	AST_SIP_MAKE_REMOTE_IPADDR_PORT_STR(transport, key);
	return ast_sip_transport_monitor_register_replace_key(key, cb, ao2_data, NULL);
}

enum ast_transport_monitor_reg ast_sip_transport_monitor_register_replace_key(const char *transport_key,
	ast_transport_monitor_shutdown_cb cb, void *ao2_data, ast_transport_monitor_data_matcher matches)
{
	struct ao2_container *transports;
	struct transport_monitor *monitored;
	enum ast_transport_monitor_reg res = AST_TRANSPORT_MONITOR_REG_NOT_FOUND;

	ast_assert(transport_key != NULL && cb != NULL);

	transports = ao2_global_obj_ref(active_transports);
	if (!transports) {
		return res;
	}

	ao2_lock(transports);
	monitored = ao2_find(transports, transport_key, OBJ_SEARCH_KEY | OBJ_NOLOCK);
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
			ast_debug(3, "Transport %s(%s) RefCnt: %ld : Monitor registration failed %p(%p)\n",
				monitored->key, monitored->transport_obj_name,
				pj_atomic_get(monitored->transport->ref_cnt), cb, ao2_data);
		} else {
			res = AST_TRANSPORT_MONITOR_REG_SUCCESS;
			ast_debug(3, "Transport %s(%s,%s) RefCnt: %ld : Registered monitor %p(%p)\n",
				monitored->key, monitored->transport_obj_name,
				monitored->transport->type_name,
				pj_atomic_get(monitored->transport->ref_cnt), cb, ao2_data);
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

static char *cli_show_monitors(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	char *cli_rc = CLI_FAILURE;
	int rc = 0;
	int using_regex = 0;
	regex_t regex = { 0, };
	int container_count;
	struct ao2_iterator iter;
	struct ao2_container *sorted_monitors = NULL;
	struct ao2_container *transports;
	struct transport_monitor *monitored;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show transport-monitors";
		e->usage = "Usage: pjsip show transport-monitors [ like <pattern> ]\n"
		            "      Show pjsip transport monitors\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3 && a->argc != 5) {
		return CLI_SHOWUSAGE;
	}

	if (a->argc == 5) {
		int regrc;
		if (strcasecmp(a->argv[3], "like")) {
			return CLI_SHOWUSAGE;
		}
		regrc = regcomp(&regex, a->argv[4], REG_EXTENDED | REG_ICASE | REG_NOSUB);
		if (regrc) {
			char err[256];
			regerror(regrc, &regex, err, 256);
			ast_cli(a->fd, "PJSIP Transport Monitor: Error: %s\n", err);
			return CLI_FAILURE;
		}
		using_regex = 1;
	}

	/* Get a sorted snapshot of the scheduled tasks */
	sorted_monitors = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		transport_monitor_sort_fn, NULL);
	if (!sorted_monitors) {
		ast_cli(a->fd, "PJSIP Transport Monitor: Unable to allocate temporary container\n");
		goto error;
	}

	transports = ao2_global_obj_ref(active_transports);
	if (!transports) {
		ast_cli(a->fd, "PJSIP Transport Monitor: Unable to get transports\n");
		goto error;
	}

	ao2_lock(transports);
	rc = ao2_container_dup(sorted_monitors, transports, 0);
	ao2_unlock(transports);
	ao2_ref(transports, -1);
	if (rc != 0) {
		ast_cli(a->fd, "PJSIP Transport Monitors: Unable to sort temporary container\n");
		goto error;
	}
	container_count = ao2_container_count(sorted_monitors);

	ast_cli(a->fd, "PJSIP Transport Monitors:\n\n");

	ast_cli(a->fd,
		"<Remote Host...................................> <State.....> <Direction> <RefCnt> <Monitors> <ObjName............>\n");

	iter = ao2_iterator_init(sorted_monitors, AO2_ITERATOR_UNLINK);
	for (; (monitored = ao2_iterator_next(&iter)); ao2_ref(monitored, -1)) {
		char *state;

		if (using_regex && regexec(&regex, monitored->key, 0, NULL, 0) == REG_NOMATCH) {
			continue;
		}

		if (monitored->transport->is_destroying) {
			state = "DESTROYING";
		} else if (monitored->transport->is_shutdown) {
			state = "SHUTDOWN";
		} else {
			state = "ACTIVE";
		}

		ast_cli(a->fd, " %-46.46s   %-10s   %-9s   %6ld   %8" PRIu64 "   %s\n",
			monitored->key, state,
			monitored->transport->dir == PJSIP_TP_DIR_OUTGOING ? "Outgoing" : "Incoming",
			pj_atomic_get(monitored->transport->ref_cnt),
			AST_VECTOR_SIZE(&monitored->monitors), monitored->transport->obj_name);
	}
	ao2_iterator_destroy(&iter);
	ast_cli(a->fd, "\nTotal Transport Monitors: %d\n\n", container_count);
	cli_rc = CLI_SUCCESS;
error:
	if (using_regex) {
		regfree(&regex);
	}
	ao2_cleanup(sorted_monitors);

	return cli_rc;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(cli_show_monitors, "Show pjsip transport monitors"),
};

void ast_sip_destroy_transport_events(void)
{
	pjsip_tpmgr *tpmgr;

	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));

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
		ACTIVE_TRANSPORTS_BUCKETS, transport_monitor_hash_fn, transport_monitor_sort_fn,
		transport_monitor_cmp_fn);
	if (!transports) {
		return -1;
	}
	ao2_global_obj_replace_unref(active_transports, transports);
	ao2_ref(transports, -1);

	tpmgr_state_callback = pjsip_tpmgr_get_state_cb(tpmgr);
	pjsip_tpmgr_set_state_cb(tpmgr, &transport_state_callback);

	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));


	return 0;
}
