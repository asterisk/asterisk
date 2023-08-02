/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
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

#include <math.h>
#include <pjsip.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/logger.h"
#include "asterisk/astobj2.h"
#include "asterisk/sorcery.h"
#include "asterisk/acl.h"
#include "asterisk/utils.h"
#include "include/res_pjsip_private.h"
/* We're only using a #define from http_websocket.h, no OPTIONAL_API symbols are used. */
#include "asterisk/http_websocket.h"

#define MAX_POINTER_STRING 33

/*! \brief Default number of state container buckets */
#define DEFAULT_STATE_BUCKETS 53
static struct ao2_container *transport_states;

struct internal_state {
	char *id;
	/*! Set if there was a change detected */
	int change_detected;
	/*! \brief Transport configuration object */
	struct ast_sip_transport *transport;
	/*! \brief Transport state information */
	struct ast_sip_transport_state *state;
};

static void temp_state_store_cleanup(void *data)
{
	struct ast_sip_transport_state **temp_state = data;

	ao2_cleanup(*temp_state);
	ast_free(data);
}

AST_THREADSTORAGE_CUSTOM(temp_state_store, NULL, temp_state_store_cleanup);

/*! \brief hashing function for state objects */
static int internal_state_hash(const void *obj, const int flags)
{
	const struct internal_state *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->id;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief comparator function for state objects */
static int internal_state_cmp(void *obj, void *arg, int flags)
{
	const struct internal_state *object_left = obj;
	const struct internal_state *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->id, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Not supported by container. */
		ast_assert(0);
		return 0;
	default:
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	return CMP_MATCH;
}

/*! \brief hashing function for state objects */
static int transport_state_hash(const void *obj, const int flags)
{
	const struct ast_sip_transport_state *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = object->id;
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief comparator function for state objects */
static int transport_state_cmp(void *obj, void *arg, int flags)
{
	const struct ast_sip_transport_state *object_left = obj;
	const struct ast_sip_transport_state *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = object_right->id;
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(object_left->id, right_key);
		break;
	case OBJ_SEARCH_PARTIAL_KEY:
		/* Not supported by container. */
		ast_assert(0);
		return 0;
	default:
		cmp = 0;
		break;
	}
	if (cmp) {
		return 0;
	}
	return CMP_MATCH;
}

static int sip_transport_to_ami(const struct ast_sip_transport *transport,
				struct ast_str **buf)
{
	return ast_sip_sorcery_object_to_ami(transport, buf);
}

static int format_ami_endpoint_transport(const struct ast_sip_endpoint *endpoint,
					 struct ast_sip_ami *ami)
{
	RAII_VAR(struct ast_str *, buf, NULL, ast_free);
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);

	if (ast_strlen_zero(endpoint->transport)) {
		return 0;
	}

	buf = ast_sip_create_ami_event("TransportDetail", ami);
	if (!buf) {
		return -1;
	}

	transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport",
		endpoint->transport);
	if (!transport) {
		astman_send_error_va(ami->s, ami->m, "Unable to retrieve "
				     "transport %s\n", endpoint->transport);
		return -1;
	}

	sip_transport_to_ami(transport, &buf);

	ast_str_append(&buf, 0, "EndpointName: %s\r\n",
		       ast_sorcery_object_get_id(endpoint));

	astman_append(ami->s, "%s\r\n", ast_str_buffer(buf));
	ami->count++;

	return 0;
}

struct ast_sip_endpoint_formatter endpoint_transport_formatter = {
	.format_ami = format_ami_endpoint_transport
};

int ast_sip_transport_state_set_transport(const char *transport_name, pjsip_transport *transport)
{
	struct ast_sip_transport_state *transport_state;

	/* To make it easier on callers we allow an empty transport name */
	if (ast_strlen_zero(transport_name)) {
		return 0;
	}

	transport_state = ast_sip_get_transport_state(transport_name);
	if (!transport_state) {
		return -1;
	}

	if (!transport_state->flow) {
		ao2_ref(transport_state, -1);
		return 0;
	}

	ao2_lock(transport_state);
	if (transport_state->transport != transport) {
		if (transport_state->transport) {
			pjsip_transport_dec_ref(transport_state->transport);
		}
		transport_state->transport = transport;
		if (transport_state->transport) {
			pjsip_transport_add_ref(transport_state->transport);
		}
	}
	ao2_unlock(transport_state);

	ao2_ref(transport_state, -1);

	return 0;
}

int ast_sip_transport_state_set_preferred_identity(const char *transport_name, const char *identity)
{
	struct ast_sip_transport_state *transport_state;

	if (ast_strlen_zero(transport_name)) {
		return 0;
	}

	transport_state = ast_sip_get_transport_state(transport_name);
	if (!transport_state) {
		return -1;
	}

	if (!transport_state->flow) {
		ao2_ref(transport_state, -1);
		return 0;
	}

	ao2_lock(transport_state);
	ast_free(transport_state->preferred_identity);
	transport_state->preferred_identity = ast_strdup(identity);
	ao2_unlock(transport_state);

	ao2_ref(transport_state, -1);

	return 0;
}

int ast_sip_transport_state_set_service_routes(const char *transport_name, struct ast_sip_service_route_vector *service_routes)
{
	struct ast_sip_transport_state *transport_state;

	if (ast_strlen_zero(transport_name)) {
		ast_sip_service_route_vector_destroy(service_routes);
		return 0;
	}

	transport_state = ast_sip_get_transport_state(transport_name);
	if (!transport_state) {
		ast_sip_service_route_vector_destroy(service_routes);
		return -1;
	}

	if (!transport_state->flow) {
		ao2_ref(transport_state, -1);
		ast_sip_service_route_vector_destroy(service_routes);
		return 0;
	}

	ao2_lock(transport_state);
	ast_sip_service_route_vector_destroy(transport_state->service_routes);
	transport_state->service_routes = service_routes;
	ao2_unlock(transport_state);

	ao2_ref(transport_state, -1);

	return 0;
}

void ast_sip_message_apply_transport(const char *transport_name, pjsip_tx_data *tdata)
{
	struct ast_sip_transport_state *transport_state;

	if (ast_strlen_zero(transport_name)) {
		return;
	}

	/* We only currently care about requests that are of the INVITE, CANCEL, or OPTIONS
	 * type but in the future we could support other messages.
	 */
	if (tdata->msg->type != PJSIP_REQUEST_MSG ||
		(pjsip_method_cmp(&tdata->msg->line.req.method, &pjsip_invite_method) &&
		pjsip_method_cmp(&tdata->msg->line.req.method, &pjsip_cancel_method) &&
		pjsip_method_cmp(&tdata->msg->line.req.method, &pjsip_options_method))) {
		return;
	}

	transport_state = ast_sip_get_transport_state(transport_name);
	if (!transport_state) {
		return;
	}

	if (!transport_state->flow) {
		ao2_ref(transport_state, -1);
		return;
	}

	ao2_lock(transport_state);

	/* If a Preferred Identity has been set then add it to the request */
	if (transport_state->preferred_identity) {
		ast_sip_add_header(tdata, "P-Preferred-Identity", transport_state->preferred_identity);
	}

	/* If Service Routes have been set then add them to the request */
	if (transport_state->service_routes) {
		int idx;

		for (idx = 0; idx < AST_VECTOR_SIZE(transport_state->service_routes); ++idx) {
			char *service_route = AST_VECTOR_GET(transport_state->service_routes, idx);

			ast_sip_add_header(tdata, "Route", service_route);
		}
	}

	ao2_unlock(transport_state);

	ao2_ref(transport_state, -1);
}

struct ast_sip_service_route_vector *ast_sip_service_route_vector_alloc(void)
{
	struct ast_sip_service_route_vector *service_routes;

	service_routes = ast_calloc(1, sizeof(*service_routes));
	if (!service_routes) {
		return NULL;
	}

	AST_VECTOR_INIT(service_routes, 0);

	return service_routes;
}

void ast_sip_service_route_vector_destroy(struct ast_sip_service_route_vector *service_routes)
{
	if (!service_routes) {
		return;
	}

	AST_VECTOR_CALLBACK_VOID(service_routes, ast_free);
	ast_free(service_routes);
}

static void set_qos(struct ast_sip_transport *transport, pj_qos_params *qos)
{
	int tos_as_dscp = transport->tos >> 2;

	if (transport->tos) {
		qos->flags |= PJ_QOS_PARAM_HAS_DSCP;
		qos->dscp_val = tos_as_dscp;
	}
	if (transport->cos) {
		qos->flags |= PJ_QOS_PARAM_HAS_SO_PRIO;
		qos->so_prio = transport->cos;
	}
}

/*! \brief Destructor for transport */
static void sip_transport_destroy(void *obj)
{
	struct ast_sip_transport *transport = obj;

	ast_string_field_free_memory(transport);
}

/*! \brief Allocator for transport */
static void *sip_transport_alloc(const char *name)
{
	struct ast_sip_transport *transport = ast_sorcery_generic_alloc(sizeof(*transport), sip_transport_destroy);

	if (!transport) {
		return NULL;
	}

	if (ast_string_field_init(transport, 256)) {
		ao2_cleanup(transport);
		return NULL;
	}

	return transport;
}

static int destroy_sip_transport_state(void *data)
{
	struct ast_sip_transport_state *transport_state = data;

	ast_free(transport_state->id);
	ast_free_ha(transport_state->localnet);

	if (transport_state->external_signaling_address_refresher) {
		ast_dnsmgr_release(transport_state->external_signaling_address_refresher);
	}
	if (transport_state->external_media_address_refresher) {
		ast_dnsmgr_release(transport_state->external_media_address_refresher);
	}
	if (transport_state->transport) {
		pjsip_transport_shutdown(transport_state->transport);
	}

	return 0;
}

/*! \brief Destructor for ast_sip_transport state information */
static void sip_transport_state_destroy(void *obj)
{
	struct ast_sip_transport_state *state = obj;

	ast_sip_push_task_wait_servant(NULL, destroy_sip_transport_state, state);
}

/*! \brief Destructor for ast_sip_transport state information */
static void internal_state_destroy(void *obj)
{
	struct internal_state *state = obj;

	ast_free(state->id);
	ao2_cleanup(state->transport);
	ao2_cleanup(state->state);
}

static struct internal_state *find_internal_state_by_transport(const struct ast_sip_transport *transport)
{
	const char *key = ast_sorcery_object_get_id(transport);

	return ao2_find(transport_states, key, OBJ_SEARCH_KEY | OBJ_NOLOCK);
}

static struct ast_sip_transport_state *find_state_by_transport(const struct ast_sip_transport *transport)
{
	struct internal_state *state;
	struct ast_sip_transport_state *trans_state;

	state = find_internal_state_by_transport(transport);
	if (!state) {
		return NULL;
	}
	trans_state = ao2_bump(state->state);
	ao2_ref(state, -1);

	return trans_state;
}

static int remove_temporary_state(void)
{
	struct ast_sip_transport_state **state;

	state = ast_threadstorage_get(&temp_state_store, sizeof(state));
	if (!state) {
		return -1;
	}

	ao2_cleanup(*state);
	*state = NULL;
	return 0;
}

static struct ast_sip_transport_state *find_temporary_state(struct ast_sip_transport *transport)
{
	struct ast_sip_transport_state **state;

	state = ast_threadstorage_get(&temp_state_store, sizeof(state));
	if (state && *state) {
		ao2_ref(*state, +1);
		return *state;
	}

	return NULL;
}

static struct internal_state *internal_state_alloc(struct ast_sip_transport *transport)
{
	struct internal_state *internal_state;

	internal_state = ao2_alloc(sizeof(*internal_state), internal_state_destroy);
	if (!internal_state) {
		return NULL;
	}

	internal_state->id = ast_strdup(ast_sorcery_object_get_id(transport));
	if (!internal_state->id) {
		ao2_cleanup(internal_state);
		return NULL;
	}

	/* We're transferring the reference from find_temporary_state */
	internal_state->state = find_temporary_state(transport);
	if (!internal_state->state) {
		ao2_cleanup(internal_state);
		return NULL;
	}
	internal_state->transport = ao2_bump(transport);
	internal_state->transport->state = internal_state->state;
	remove_temporary_state();

	return internal_state;
}

/*!
 * \internal
 * \brief Should only be called by the individual field handlers
 */
static struct ast_sip_transport_state *find_or_create_temporary_state(struct ast_sip_transport *transport)
{
	struct ast_sip_transport_state **state;
	struct ast_sip_transport_state *new_state;

	if ((new_state = find_temporary_state(transport))) {
		return new_state;
	}

	state = ast_threadstorage_get(&temp_state_store, sizeof(state));
	if (!state || *state) {
		return NULL;
	}

	new_state = ao2_alloc(sizeof(**state), sip_transport_state_destroy);
	if (!new_state) {
		return NULL;
	}
	new_state->id = ast_strdup(ast_sorcery_object_get_id(transport));
	new_state->type = transport->type;

	pjsip_tls_setting_default(&new_state->tls);
#ifdef HAVE_PJSIP_TLS_TRANSPORT_PROTO
	/* proto must be forced to 0 to enable all protocols otherwise only TLS will work */
	new_state->tls.proto = 0;
#endif
	new_state->tls.ciphers = new_state->ciphers;

	ao2_ref(new_state, +1);
	*state = new_state;

	return new_state;
}

static void copy_state_to_transport(struct ast_sip_transport *transport)
{
	ast_assert(transport && transport->state);

	memcpy(&transport->host, &transport->state->host, sizeof(transport->host));
	memcpy(&transport->tls, &transport->state->tls, sizeof(transport->tls));
	memcpy(&transport->ciphers, &transport->state->ciphers, sizeof(transport->ciphers));
	transport->localnet = transport->state->localnet;
	transport->external_address_refresher = transport->state->external_signaling_address_refresher;
	memcpy(&transport->external_address, &transport->state->external_signaling_address, sizeof(transport->external_signaling_address));
}

#ifdef HAVE_PJSIP_TLS_TRANSPORT_RESTART
static int file_stat_cmp(const struct stat *old_stat, const struct stat *new_stat)
{
	return old_stat->st_size != new_stat->st_size
		|| old_stat->st_mtime != new_stat->st_mtime
#if defined(HAVE_STRUCT_STAT_ST_MTIM)
		|| old_stat->st_mtim.tv_nsec != new_stat->st_mtim.tv_nsec
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
		|| old_stat->st_mtimensec != new_stat->st_mtimensec
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC)
		|| old_stat->st_mtimespec.tv_nsec != new_stat->st_mtimespec.tv_nsec
#endif
        ;
}
#endif

static int has_state_changed(struct ast_sip_transport_state *a, struct ast_sip_transport_state *b)
{
	if (a->type != b->type) {
		return -1;
	}

	if (pj_sockaddr_cmp(&a->host, &b->host)) {
		return -1;
	}

	if ((a->localnet || b->localnet)
		&& ((!a->localnet != !b->localnet)
		|| ast_sockaddr_cmp(&a->localnet->addr, &b->localnet->addr)
		|| ast_sockaddr_cmp(&a->localnet->netmask, &b->localnet->netmask)))
	{
		return -1;
	}

	if (ast_sockaddr_cmp(&a->external_signaling_address, &b->external_signaling_address)) {
		return -1;
	}

	if (ast_sockaddr_cmp(&a->external_media_address, &b->external_media_address)) {
		return -1;
	}

	if (a->tls.method != b->tls.method
		|| a->tls.ciphers_num != b->tls.ciphers_num
#ifdef HAVE_PJSIP_TLS_TRANSPORT_PROTO
		|| a->tls.proto != b->tls.proto
#endif
		|| a->tls.verify_client != b->tls.verify_client
		|| a->tls.verify_server != b->tls.verify_server
		|| a->tls.require_client_cert != b->tls.require_client_cert) {
		return -1;
	}

	if (memcmp(a->ciphers, b->ciphers, sizeof(pj_ssl_cipher) * fmax(a->tls.ciphers_num, b->tls.ciphers_num))) {
		return -1;
	}

#ifdef HAVE_PJSIP_TLS_TRANSPORT_RESTART
	if (file_stat_cmp(&a->cert_file_stat, &b->cert_file_stat) || file_stat_cmp(&a->privkey_file_stat, &b->privkey_file_stat)) {
		return -1;
	}
#endif

	return 0;
}

static void states_cleanup(void *states)
{
	if (states) {
		ao2_unlock(states);
	}
}

/*! \brief Apply handler for transports */
static int transport_apply(const struct ast_sorcery *sorcery, void *obj)
{
	struct ast_sip_transport *transport = obj;
	const char *transport_id = ast_sorcery_object_get_id(obj);
	RAII_VAR(struct ao2_container *, states, transport_states, states_cleanup);
	RAII_VAR(struct internal_state *, temp_state, NULL, ao2_cleanup);
	RAII_VAR(struct internal_state *, perm_state, NULL, ao2_cleanup);
	RAII_VAR(struct ast_variable *, changes, NULL, ast_variables_destroy);
	pj_status_t res = -1;
	int i;
#define BIND_TRIES 3
#define BIND_DELAY_US 100000

	if (!states) {
		return -1;
	}

	/*
	 * transport_apply gets called for EVERY retrieval of a transport when using realtime.
	 * We need to prevent multiple threads from trying to mess with underlying transports
	 * at the same time.  The container is the only thing we have to lock on.
	 */
	ao2_wrlock(states);

	temp_state = internal_state_alloc(transport);
	if (!temp_state) {
		ast_log(LOG_ERROR, "Transport '%s' failed to allocate memory\n", transport_id);
		return -1;
	}

	if (transport->async_operations != 1) {
		ast_log(LOG_WARNING, "The async_operations setting on transport '%s' has been set to '%d'. The setting can no longer be set and is always 1.\n",
			transport_id, transport->async_operations);
		transport->async_operations = 1;
	}

	perm_state = find_internal_state_by_transport(transport);
	if (perm_state) {
		ast_sorcery_diff(sorcery, perm_state->transport, transport, &changes);
		if (!changes && !has_state_changed(perm_state->state, temp_state->state)) {
			/* In case someone is using the deprecated fields, reset them */
			transport->state = perm_state->state;
			copy_state_to_transport(transport);
			ao2_replace(perm_state->transport, transport);
			return 0;
		}

		/* If we aren't allowed to reload then we copy values that can't be changed from perm_state */
		if (!transport->allow_reload) {
			memcpy(&temp_state->state->host, &perm_state->state->host, sizeof(temp_state->state->host));
			memcpy(&temp_state->state->tls, &perm_state->state->tls, sizeof(temp_state->state->tls));
			memcpy(&temp_state->state->ciphers, &perm_state->state->ciphers, sizeof(temp_state->state->ciphers));
		}
	}

	if (!transport->flow && (!perm_state || transport->allow_reload)) {
		if (temp_state->state->host.addr.sa_family != PJ_AF_INET && temp_state->state->host.addr.sa_family != PJ_AF_INET6) {
			ast_log(LOG_ERROR, "Transport '%s' could not be started as binding not specified\n", transport_id);
			return -1;
		}

		/* Set default port if not present */
		if (!pj_sockaddr_get_port(&temp_state->state->host)) {
			pj_sockaddr_set_port(&temp_state->state->host, (transport->type == AST_TRANSPORT_TLS) ? 5061 : 5060);
		}
	}

	/* Now that we know what address family we can set up a dnsmgr refresh for the external addresses if present */
	if (!ast_strlen_zero(transport->external_signaling_address)) {
		if (temp_state->state->host.addr.sa_family == pj_AF_INET()) {
			temp_state->state->external_signaling_address.ss.ss_family = AF_INET;
		} else if (temp_state->state->host.addr.sa_family == pj_AF_INET6()) {
			temp_state->state->external_signaling_address.ss.ss_family = AF_INET6;
		} else {
			ast_log(LOG_ERROR, "Unknown address family for transport '%s', could not get external signaling address\n",
					transport_id);
			return -1;
		}

		if (ast_dnsmgr_lookup(transport->external_signaling_address, &temp_state->state->external_signaling_address, &temp_state->state->external_signaling_address_refresher, NULL) < 0) {
			ast_log(LOG_ERROR, "Could not create dnsmgr for external signaling address on '%s'\n", transport_id);
			return -1;
		}
	}

	if (!ast_strlen_zero(transport->external_media_address)) {
		if (temp_state->state->host.addr.sa_family == pj_AF_INET()) {
			temp_state->state->external_media_address.ss.ss_family = AF_INET;
		} else if (temp_state->state->host.addr.sa_family == pj_AF_INET6()) {
			temp_state->state->external_media_address.ss.ss_family = AF_INET6;
		} else {
			ast_log(LOG_ERROR, "Unknown address family for transport '%s', could not get external media address\n",
					transport_id);
			return -1;
		}

		if (ast_dnsmgr_lookup(transport->external_media_address, &temp_state->state->external_media_address, &temp_state->state->external_media_address_refresher, NULL) < 0) {
			ast_log(LOG_ERROR, "Could not create dnsmgr for external media address on '%s'\n", transport_id);
			return -1;
		}
	}

	if (transport->flow) {
		pj_str_t address;

		ast_debug(1, "Ignoring any bind configuration on transport '%s' as it is a child of another\n",
			transport_id);
		pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&address, "0.0.0.0"), &temp_state->state->host);

		temp_state->state->flow = 1;
		res = PJ_SUCCESS;
	} else if (!transport->allow_reload && perm_state) {
		/* We inherit the transport from perm state, untouched */
#ifdef HAVE_PJSIP_TLS_TRANSPORT_RESTART
		ast_log(LOG_NOTICE, "Transport '%s' is not fully reloadable, not reloading: protocol, bind, TLS (everything but certificate and private key if filename is unchanged), TCP, ToS, or CoS options.\n", transport_id);
		/* If this is a TLS transport and the certificate or private key has changed, then restart the transport so it uses the new one */
		if (transport->type == AST_TRANSPORT_TLS) {
			if (strcmp(perm_state->transport->cert_file, temp_state->transport->cert_file) ||
				strcmp(perm_state->transport->privkey_file, temp_state->transport->privkey_file)) {
				ast_log(LOG_ERROR, "Unable to restart TLS transport '%s' as certificate or private key filename has changed\n",
					transport_id);
			} else if (file_stat_cmp(&perm_state->state->cert_file_stat, &temp_state->state->cert_file_stat) ||
				file_stat_cmp(&perm_state->state->privkey_file_stat, &temp_state->state->privkey_file_stat)) {
				if (pjsip_tls_transport_restart(perm_state->state->factory, &perm_state->state->host, NULL) != PJ_SUCCESS) {
					ast_log(LOG_ERROR, "Failed to restart TLS transport '%s'\n", transport_id);
				} else {
					sprintf(perm_state->state->factory->info, "%s", transport_id);
				}
			}
		}
#else
		ast_log(LOG_NOTICE, "Transport '%s' is not fully reloadable, not reloading: protocol, bind, TLS, TCP, ToS, or CoS options.\n", transport_id);
#endif
		temp_state->state->transport = perm_state->state->transport;
		perm_state->state->transport = NULL;
		temp_state->state->factory = perm_state->state->factory;
		perm_state->state->factory = NULL;

		res = PJ_SUCCESS;
	} else if (transport->type == AST_TRANSPORT_UDP) {

		for (i = 0; i < BIND_TRIES && res != PJ_SUCCESS; i++) {
			if (perm_state && perm_state->state && perm_state->state->transport) {
				pjsip_udp_transport_pause(perm_state->state->transport,
					PJSIP_UDP_TRANSPORT_DESTROY_SOCKET);
				usleep(BIND_DELAY_US);
			}

			if (temp_state->state->host.addr.sa_family == pj_AF_INET()) {
				res = pjsip_udp_transport_start(ast_sip_get_pjsip_endpoint(),
					&temp_state->state->host.ipv4, NULL, transport->async_operations,
					&temp_state->state->transport);
			} else if (temp_state->state->host.addr.sa_family == pj_AF_INET6()) {
				res = pjsip_udp_transport_start6(ast_sip_get_pjsip_endpoint(),
					&temp_state->state->host.ipv6, NULL, transport->async_operations,
					&temp_state->state->transport);
			}
		}

		if (res == PJ_SUCCESS) {
			temp_state->state->transport->info = pj_pool_alloc(temp_state->state->transport->pool,
				(AST_SIP_X_AST_TXP_LEN + strlen(transport_id) + 2));

			sprintf(temp_state->state->transport->info, "%s:%s", AST_SIP_X_AST_TXP, transport_id);

			if (transport->tos || transport->cos) {
				pj_sock_t sock;
				pj_qos_params qos_params;
				sock = pjsip_udp_transport_get_socket(temp_state->state->transport);
				pj_sock_get_qos_params(sock, &qos_params);
				set_qos(transport, &qos_params);
				pj_sock_set_qos_params(sock, &qos_params);
			}
		}
	} else if (transport->type == AST_TRANSPORT_TCP) {
		pjsip_tcp_transport_cfg cfg;
		static int option = 1;

		pjsip_tcp_transport_cfg_default(&cfg, temp_state->state->host.addr.sa_family);
		cfg.bind_addr = temp_state->state->host;
		cfg.async_cnt = transport->async_operations;
		set_qos(transport, &cfg.qos_params);
		/* sockopt_params.options is copied to each newly connected socket */
		cfg.sockopt_params.options[0].level = pj_SOL_TCP();
		cfg.sockopt_params.options[0].optname = pj_TCP_NODELAY();
		cfg.sockopt_params.options[0].optval = &option;
		cfg.sockopt_params.options[0].optlen = sizeof(option);
		cfg.sockopt_params.cnt = 1;

		for (i = 0; i < BIND_TRIES && res != PJ_SUCCESS; i++) {
			if (perm_state && perm_state->state && perm_state->state->factory
				&& perm_state->state->factory->destroy) {
				perm_state->state->factory->destroy(perm_state->state->factory);
				usleep(BIND_DELAY_US);
			}

			res = pjsip_tcp_transport_start3(ast_sip_get_pjsip_endpoint(), &cfg,
				&temp_state->state->factory);
		}
	} else if (transport->type == AST_TRANSPORT_TLS) {
#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
		static int option = 1;

		if (transport->async_operations > 1 && ast_compare_versions(pj_get_version(), "2.5.0") < 0) {
			ast_log(LOG_ERROR, "Transport: %s: When protocol=tls and pjproject version < 2.5.0, async_operations can't be > 1\n",
					ast_sorcery_object_get_id(obj));
			return -1;
		}

		temp_state->state->tls.password = pj_str((char*)transport->password);
		set_qos(transport, &temp_state->state->tls.qos_params);

		/* sockopt_params.options is copied to each newly connected socket */
		temp_state->state->tls.sockopt_params.options[0].level = pj_SOL_TCP();
		temp_state->state->tls.sockopt_params.options[0].optname = pj_TCP_NODELAY();
		temp_state->state->tls.sockopt_params.options[0].optval = &option;
		temp_state->state->tls.sockopt_params.options[0].optlen = sizeof(option);
		temp_state->state->tls.sockopt_params.cnt = 1;

		for (i = 0; i < BIND_TRIES && res != PJ_SUCCESS; i++) {
			if (perm_state && perm_state->state && perm_state->state->factory
				&& perm_state->state->factory->destroy) {
				perm_state->state->factory->destroy(perm_state->state->factory);
				usleep(BIND_DELAY_US);
			}

			res = pjsip_tls_transport_start2(ast_sip_get_pjsip_endpoint(), &temp_state->state->tls,
				&temp_state->state->host, NULL, transport->async_operations,
				&temp_state->state->factory);
		}

		if (res == PJ_SUCCESS) {
			/*
			 * PJSIP uses 100 bytes to store information, and during a restart will repopulate
			 * the field so ensure there is sufficient space - even though we'll revert it after.
			 */
			temp_state->state->factory->info = pj_pool_alloc(
				temp_state->state->factory->pool, (MAX(MAX_OBJECT_FIELD, 100) + 1));
			/*
			 * Store transport id on the factory instance so it can be used
			 * later to look up the transport state.
			 */
			sprintf(temp_state->state->factory->info, "%s", transport_id);
		}
#else
		ast_log(LOG_ERROR, "Transport: %s: PJSIP has not been compiled with TLS transport support, ensure OpenSSL development packages are installed\n",
			ast_sorcery_object_get_id(obj));
		return -1;
#endif
	} else if ((transport->type == AST_TRANSPORT_WS) || (transport->type == AST_TRANSPORT_WSS)) {
		if (transport->cos || transport->tos) {
			ast_log(LOG_WARNING, "TOS and COS values ignored for websocket transport\n");
		} else if (!ast_strlen_zero(transport->ca_list_file) || !ast_strlen_zero(transport->ca_list_path) ||
			!ast_strlen_zero(transport->cert_file) || !ast_strlen_zero(transport->privkey_file)) {
			ast_log(LOG_WARNING, "TLS certificate values ignored for websocket transport as they are configured in http.conf\n");
		}
		res = PJ_SUCCESS;
	}

	if (res != PJ_SUCCESS) {
		char msg[PJ_ERR_MSG_SIZE];

		pj_strerror(res, msg, sizeof(msg));
		ast_log(LOG_ERROR, "Transport '%s' could not be started: %s\n", ast_sorcery_object_get_id(obj), msg);
		return -1;
	}

	copy_state_to_transport(transport);
	if (perm_state) {
		ao2_unlink_flags(states, perm_state, OBJ_NOLOCK);
	}
	ao2_link_flags(states, temp_state, OBJ_NOLOCK);

	return 0;
}

/*! \brief Custom handler for type just makes sure the state is created */
static int transport_state_init(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	struct ast_sip_transport_state *state = find_or_create_temporary_state(transport);

	ao2_cleanup(state);

	return 0;
}

/*! \brief Custom handler for TLS method setting */
static int transport_tls_file_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_or_create_temporary_state(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	if (ast_strlen_zero(var->value)) {
		/* Ignore empty options */
		return 0;
	}

	if (!ast_file_is_readable(var->value)) {
		ast_log(LOG_ERROR, "Transport: %s: %s %s is either missing or not readable\n",
			ast_sorcery_object_get_id(obj), var->name, var->value);
		return -1;
	}

	if (!strcasecmp(var->name, "ca_list_file")) {
		state->tls.ca_list_file = pj_str((char*)var->value);
		ast_string_field_set(transport, ca_list_file, var->value);
	} else if (!strcasecmp(var->name, "ca_list_path")) {
#ifdef HAVE_PJ_SSL_CERT_LOAD_FROM_FILES2
		state->tls.ca_list_path = pj_str((char *)var->value);
		ast_string_field_set(transport, ca_list_path, var->value);
#else
		ast_log(LOG_WARNING, "Asterisk has been built against a version of pjproject that does not "
				"support the 'ca_list_path' option. Please upgrade to version 2.4 or later.\n");
#endif
	} else if (!strcasecmp(var->name, "cert_file")) {
		state->tls.cert_file = pj_str((char *)var->value);
		ast_string_field_set(transport, cert_file, var->value);
#ifdef HAVE_PJSIP_TLS_TRANSPORT_RESTART
		if (stat(var->value, &state->cert_file_stat)) {
			ast_log(LOG_ERROR, "Failed to stat certificate file '%s' for transport '%s' due to '%s'\n",
				var->value, ast_sorcery_object_get_id(obj), strerror(errno));
			return -1;
		}
		ast_sorcery_object_set_has_dynamic_contents(transport);
#endif
	} else if (!strcasecmp(var->name, "priv_key_file")) {
		state->tls.privkey_file = pj_str((char *)var->value);
		ast_string_field_set(transport, privkey_file, var->value);
#ifdef HAVE_PJSIP_TLS_TRANSPORT_RESTART
		if (stat(var->value, &state->privkey_file_stat)) {
			ast_log(LOG_ERROR, "Failed to stat private key file '%s' for transport '%s' due to '%s'\n",
				var->value, ast_sorcery_object_get_id(obj), strerror(errno));
			return -1;
		}
		ast_sorcery_object_set_has_dynamic_contents(transport);
#endif
	}

	return 0;
}

static int ca_list_file_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	*buf = ast_strdup(transport->ca_list_file);

	return 0;
}

static int ca_list_path_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	*buf = ast_strdup(transport->ca_list_path);

	return 0;
}

static int cert_file_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	*buf = ast_strdup(transport->cert_file);

	return 0;
}

static int privkey_file_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	*buf = ast_strdup(transport->privkey_file);

	return 0;
}

/*! \brief Custom handler for turning a string protocol into an enum */
static int transport_protocol_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_or_create_temporary_state(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	if (!strcasecmp(var->value, "flow")) {
		transport->flow = 1;
	} else {
		if (!strcasecmp(var->value, "udp")) {
			transport->type = AST_TRANSPORT_UDP;
		} else if (!strcasecmp(var->value, "tcp")) {
			transport->type = AST_TRANSPORT_TCP;
		} else if (!strcasecmp(var->value, "tls")) {
			transport->type = AST_TRANSPORT_TLS;
		} else if (!strcasecmp(var->value, "ws")) {
			transport->type = AST_TRANSPORT_WS;
		} else if (!strcasecmp(var->value, "wss")) {
			transport->type = AST_TRANSPORT_WSS;
		} else {
			return -1;
		}
		transport->flow = 0;
	}

	state->type = transport->type;

	return 0;
}

static const char *transport_types[] = {
	[AST_TRANSPORT_UDP] = "udp",
	[AST_TRANSPORT_TCP] = "tcp",
	[AST_TRANSPORT_TLS] = "tls",
	[AST_TRANSPORT_WS] = "ws",
	[AST_TRANSPORT_WSS] = "wss"
};

static int transport_protocol_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	if (transport->flow) {
		*buf = ast_strdup("flow");
	} else if (ARRAY_IN_BOUNDS(transport->type, transport_types)) {
		*buf = ast_strdup(transport_types[transport->type]);
	}

	return 0;
}

/*! \brief Custom handler for turning a string bind into a pj_sockaddr */
static int transport_bind_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	pj_str_t buf;
	int rc;
	RAII_VAR(struct ast_sip_transport_state *, state, find_or_create_temporary_state(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	rc = pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&buf, var->value), &state->host);

	return rc != PJ_SUCCESS ? -1 : 0;
}

static int transport_bind_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	if (!(*buf = ast_calloc(MAX_OBJECT_FIELD, sizeof(char)))) {
		return -1;
	}

	/* include port as well as brackets if IPv6 */
	pj_sockaddr_print(&state->host, *buf, MAX_OBJECT_FIELD, 1 | 2);

	return 0;
}

/*! \brief Custom handler for TLS boolean settings */
static int transport_tls_bool_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_or_create_temporary_state(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	if (!strcasecmp(var->name, "verify_server")) {
		state->verify_server = ast_true(var->value);
	} else if (!strcasecmp(var->name, "verify_client")) {
		state->tls.verify_client = ast_true(var->value) ? PJ_TRUE : PJ_FALSE;
	} else if (!strcasecmp(var->name, "require_client_cert")) {
		state->tls.require_client_cert = ast_true(var->value) ? PJ_TRUE : PJ_FALSE;
	} else if (!strcasecmp(var->name, "allow_wildcard_certs")) {
		state->allow_wildcard_certs = ast_true(var->value);
	} else {
		return -1;
	}

	return 0;
}

static int verify_server_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	*buf = ast_strdup(AST_YESNO(state->verify_server));

	return 0;
}

static int verify_client_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	*buf = ast_strdup(AST_YESNO(state->tls.verify_client));

	return 0;
}

static int require_client_cert_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	*buf = ast_strdup(AST_YESNO(state->tls.require_client_cert));

	return 0;
}

static int allow_wildcard_certs_to_str(const void *obj, const intptr_t *args, char **buf)
{
	struct ast_sip_transport_state *state = find_state_by_transport(obj);

	if (!state) {
		return -1;
	}

	*buf = ast_strdup(AST_YESNO(state->allow_wildcard_certs));
	ao2_ref(state, -1);

	return 0;
}

/*! \brief Custom handler for TLS method setting */
static int transport_tls_method_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_or_create_temporary_state(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	if (ast_strlen_zero(var->value) || !strcasecmp(var->value, "default")) {
		state->tls.method = PJSIP_SSL_DEFAULT_METHOD;
	} else if (!strcasecmp(var->value, "unspecified")) {
		state->tls.method = PJSIP_SSL_UNSPECIFIED_METHOD;
	} else if (!strcasecmp(var->value, "tlsv1")) {
		state->tls.method = PJSIP_TLSV1_METHOD;
#ifdef HAVE_PJSIP_TLS_1_1
	} else if (!strcasecmp(var->value, "tlsv1_1")) {
		state->tls.method = PJSIP_TLSV1_1_METHOD;
#endif
#ifdef HAVE_PJSIP_TLS_1_2
	} else if (!strcasecmp(var->value, "tlsv1_2")) {
		state->tls.method = PJSIP_TLSV1_2_METHOD;
#endif
#ifdef HAVE_PJSIP_TLS_1_3
	} else if (!strcasecmp(var->value, "tlsv1_3")) {
		state->tls.method = PJSIP_TLSV1_3_METHOD;
#endif
	} else if (!strcasecmp(var->value, "sslv2")) {
		state->tls.method = PJSIP_SSLV2_METHOD;
	} else if (!strcasecmp(var->value, "sslv3")) {
		state->tls.method = PJSIP_SSLV3_METHOD;
	} else if (!strcasecmp(var->value, "sslv23")) {
		state->tls.method = PJSIP_SSLV23_METHOD;
	} else {
		return -1;
	}

	return 0;
}

static const char *tls_method_map[] = {
	[PJSIP_SSL_UNSPECIFIED_METHOD] = "unspecified",
	[PJSIP_TLSV1_METHOD] = "tlsv1",
#ifdef HAVE_PJSIP_TLS_1_1
	[PJSIP_TLSV1_1_METHOD] = "tlsv1_1",
#endif
#ifdef HAVE_PJSIP_TLS_1_2
	[PJSIP_TLSV1_2_METHOD] = "tlsv1_2",
#endif
#ifdef HAVE_PJSIP_TLS_1_3
	[PJSIP_TLSV1_3_METHOD] = "tlsv1_3",
#endif
	[PJSIP_SSLV2_METHOD] = "sslv2",
	[PJSIP_SSLV3_METHOD] = "sslv3",
	[PJSIP_SSLV23_METHOD] = "sslv23",
};

static int tls_method_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	if (ARRAY_IN_BOUNDS(state->tls.method, tls_method_map)) {
		*buf = ast_strdup(tls_method_map[state->tls.method]);
	}

	return 0;
}

#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
/*! \brief Helper function which turns a cipher name into an identifier */
static pj_ssl_cipher cipher_name_to_id(const char *name)
{
	pj_ssl_cipher ciphers[PJ_SSL_SOCK_MAX_CIPHERS];
	unsigned int cipher_num = PJ_ARRAY_SIZE(ciphers);
	unsigned int pos;

	if (pj_ssl_cipher_get_availables(ciphers, &cipher_num)) {
		return 0;
	}

	for (pos = 0; pos < cipher_num; ++pos) {
		const char *pos_name = pj_ssl_cipher_name(ciphers[pos]);
		if (pos_name && !strcmp(pos_name, name)) {
			return ciphers[pos];
		}
	}

	return 0;
}
#endif

#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
/*!
 * \internal
 * \brief Add a new cipher to the transport's cipher list array.
 *
 * \param state Which transport to add the cipher to.
 * \param name Cipher identifier name.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int transport_cipher_add(struct ast_sip_transport_state *state, const char *name)
{
	pj_ssl_cipher cipher;
	int idx;

	cipher = cipher_name_to_id(name);
	if (!cipher) {
		/* TODO: Check this over/tweak - it's taken from pjsua for now */
		if (!strnicmp(name, "0x", 2)) {
			pj_str_t cipher_st = pj_str((char *) name + 2);
			cipher = pj_strtoul2(&cipher_st, NULL, 16);
		} else {
			cipher = atoi(name);
		}
	}

	if (pj_ssl_cipher_is_supported(cipher)) {
		for (idx = state->tls.ciphers_num; idx--;) {
			if (state->ciphers[idx] == cipher) {
				/* The cipher is already in the list. */
				return 0;
			}
		}
		state->ciphers[state->tls.ciphers_num++] = cipher;
		return 0;
	} else {
		ast_log(LOG_ERROR, "Cipher '%s' is unsupported\n", name);
		return -1;
	}
}
#endif

#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
/*! \brief Custom handler for TLS cipher setting */
static int transport_tls_cipher_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	char *parse;
	char *name;
	int res = 0;
	RAII_VAR(struct ast_sip_transport_state *, state, find_or_create_temporary_state(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	parse = ast_strdupa(S_OR(var->value, ""));
	while ((name = ast_strip(strsep(&parse, ",")))) {
		if (ast_strlen_zero(name)) {
			continue;
		}
		if (ARRAY_LEN(state->ciphers) <= state->tls.ciphers_num) {
			ast_log(LOG_ERROR, "Too many ciphers specified\n");
			res = -1;
			break;
		}
		res |= transport_cipher_add(state, name);
	}
	return res ? -1 : 0;
}
#endif

#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
static void cipher_to_str(char **buf, const pj_ssl_cipher *ciphers, unsigned int cipher_num)
{
	struct ast_str *str;
	unsigned int idx;

	str = ast_str_create(128);
	if (!str) {
		*buf = NULL;
		return;
	}

	for (idx = 0; idx < cipher_num; ++idx) {
		ast_str_append(&str, 0, "%s", pj_ssl_cipher_name(ciphers[idx]));
		if (idx < cipher_num - 1) {
			ast_str_append(&str, 0, ", ");
		}
	}

	*buf = ast_strdup(ast_str_buffer(str));
	ast_free(str);
}
#endif

#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
static int transport_tls_cipher_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	cipher_to_str(buf, state->ciphers, state->tls.ciphers_num);
	return *buf ? 0 : -1;
}
#endif

#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
static char *handle_pjsip_list_ciphers(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	pj_ssl_cipher ciphers[PJ_SSL_SOCK_MAX_CIPHERS];
	unsigned int cipher_num = PJ_ARRAY_SIZE(ciphers);
	char *buf;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip list ciphers";
		e->usage = "Usage: pjsip list ciphers\n"
			"       List available OpenSSL cipher names.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (pj_ssl_cipher_get_availables(ciphers, &cipher_num) || !cipher_num) {
		buf = NULL;
	} else {
		cipher_to_str(&buf, ciphers, cipher_num);
	}

	if (!ast_strlen_zero(buf)) {
		ast_cli(a->fd, "Available ciphers: '%s'\n", buf);
	} else {
		ast_cli(a->fd, "No available ciphers\n");
	}
	ast_free(buf);
	return CLI_SUCCESS;
}
#endif

/*! \brief Custom handler for localnet setting */
static int transport_localnet_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	int error = 0;
	RAII_VAR(struct ast_sip_transport_state *, state, find_or_create_temporary_state(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	if (ast_strlen_zero(var->value)) {
		ast_free_ha(state->localnet);
		state->localnet = NULL;
		return 0;
	}

	/* We use only the ast_apply_ha() which defaults to ALLOW
	 * ("permit"), so we add DENY rules. */
	if (!(state->localnet = ast_append_ha("deny", var->value, state->localnet, &error))) {
		return -1;
	}

	return error;
}

static void localnet_to_vl_append(struct ast_variable **head, struct ast_ha *ha)
{
	char str[MAX_OBJECT_FIELD];
	const char *addr = ast_strdupa(ast_sockaddr_stringify_addr(&ha->addr));
	snprintf(str, MAX_OBJECT_FIELD, "%s%s/%s", ha->sense == AST_SENSE_ALLOW ? "!" : "",
			 addr, ast_sockaddr_stringify_addr(&ha->netmask));

	ast_variable_list_append(head, ast_variable_new("local_net", str, ""));
}

static int localnet_to_vl(const void *obj, struct ast_variable **fields)
{
	const struct ast_sip_transport *transport = obj;
	struct ast_variable *head = NULL;
	struct ast_ha *ha;
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	for (ha = state->localnet; ha; ha = ha->next) {
		localnet_to_vl_append(&head, ha);
	}

	if (head) {
		*fields = head;
	}

	return 0;
}

static int localnet_to_str(const void *obj, const intptr_t *args, char **buf)
{
	RAII_VAR(struct ast_str *, str, ast_str_create(MAX_OBJECT_FIELD), ast_free);
	const struct ast_sip_transport *transport = obj;
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	ast_ha_join(state->localnet, &str);
	*buf = ast_strdup(ast_str_buffer(str));
	return 0;
}

/*! \brief Custom handler for TOS setting */
static int transport_tos_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct ast_sip_transport *transport = obj;
	unsigned int value;

	if (ast_str2tos(var->value, &value)) {
		ast_log(LOG_ERROR, "Error configuring transport '%s' - Could not "
			"interpret 'tos' value '%s'\n",
			ast_sorcery_object_get_id(transport), var->value);
		return -1;
	}

	if (value % 4) {
		value = value >> 2;
		value = value << 2;
		ast_log(LOG_WARNING,
			"transport '%s' - 'tos' value '%s' uses bits that are "
			"discarded when converted to DSCP. Using equivalent %u instead.\n",
			ast_sorcery_object_get_id(transport), var->value, value);
	}

	transport->tos = value;
	return 0;
}

static int tos_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct ast_sip_transport *transport = obj;

	if (ast_asprintf(buf, "%u", transport->tos) == -1) {
		return -1;
	}
	return 0;
}

static struct ao2_container *cli_get_container(const char *regex)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	struct ao2_container *s_container;

	container = ast_sorcery_retrieve_by_regex(ast_sip_get_sorcery(), "transport",
		regex);
	if (!container) {
		return NULL;
	}

	s_container = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_NOLOCK, 0,
		ast_sorcery_object_id_sort, ast_sorcery_object_id_compare);
	if (!s_container) {
		return NULL;
	}

	if (ao2_container_dup(s_container, container, 0)) {
		ao2_ref(s_container, -1);
		return NULL;
	}

	return s_container;
}

static int cli_iterate(void *container, ao2_callback_fn callback, void *args)
{
	const struct ast_sip_endpoint *endpoint = container;
	struct ast_sip_transport *transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(),
		"transport", endpoint->transport);

	if (!transport) {
		return -1;
	}

	return callback(transport, args, 0);
}

static void *cli_retrieve_by_id(const char *id)
{
	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", id);
}

static int cli_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;
	int indent = CLI_INDENT_TO_SPACES(context->indent_level);
	int filler = CLI_MAX_WIDTH - indent - 61;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		"%*s:  <TransportId........>  <Type>  <cos>  <tos>  <BindAddress%*.*s>\n",
		indent, "Transport", filler, filler, CLI_HEADER_FILLER);

	return 0;
}

static int cli_print_body(void *obj, void *arg, int flags)
{
	struct ast_sip_transport *transport = obj;
	struct ast_sip_cli_context *context = arg;
	char hoststr[PJ_INET6_ADDRSTRLEN];
	RAII_VAR(struct ast_sip_transport_state *, state, find_state_by_transport(transport), ao2_cleanup);

	if (!state) {
		return -1;
	}

	ast_assert(context->output_buffer != NULL);

	pj_sockaddr_print(&state->host, hoststr, sizeof(hoststr), 3);

	ast_str_append(&context->output_buffer, 0, "%*s:  %-21s  %6s  %5u  %5u  %s\n",
		CLI_INDENT_TO_SPACES(context->indent_level), "Transport",
		ast_sorcery_object_get_id(transport),
		ARRAY_IN_BOUNDS(transport->type, transport_types) ? transport_types[transport->type] : "Unknown",
		transport->cos, transport->tos, hoststr);

	if (context->show_details
		|| (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(transport, context, 0);
	}

	return 0;
}

static struct ast_cli_entry cli_commands[] = {
#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
	AST_CLI_DEFINE(handle_pjsip_list_ciphers, "List available OpenSSL cipher names"),
#endif
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "List PJSIP Transports",
		.command = "pjsip list transports",
		.usage = "Usage: pjsip list transports [ like <pattern> ]\n"
				"       List the configured PJSIP Transports\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Transports",
		.command = "pjsip show transports",
		.usage = "Usage: pjsip show transports [ like <pattern> ]\n"
				"       Show the configured PJSIP Transport\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(ast_sip_cli_traverse_objects, "Show PJSIP Transport",
		.command = "pjsip show transport",
		.usage = "Usage: pjsip show transport <id>\n"
				 "       Show the configured PJSIP Transport\n"),
};

static struct ast_sip_cli_formatter_entry *cli_formatter;

struct ast_sip_transport_state *ast_sip_get_transport_state(const char *transport_id)
{
	struct internal_state *state = NULL;
	struct ast_sip_transport_state *trans_state;

	if (!transport_states) {
		return NULL;
	}

	state = ao2_find(transport_states, transport_id, OBJ_SEARCH_KEY);
	if (!state) {
		return NULL;
	}

	trans_state = ao2_bump(state->state);
	ao2_ref(state, -1);

	/* If this is a child transport see if the transport is actually dead */
	if (trans_state->flow) {
		ao2_lock(trans_state);
		if (trans_state->transport && trans_state->transport->is_shutdown == PJ_TRUE) {
			pjsip_transport_dec_ref(trans_state->transport);
			trans_state->transport = NULL;
		}
		ao2_unlock(trans_state);
	}

	return trans_state;
}

static int populate_transport_states(void *obj, void *arg, int flags)
{
	struct internal_state *state = obj;
	struct ao2_container *container = arg;

	ao2_link(container, state->state);

	return CMP_MATCH;
}

struct ao2_container *ast_sip_get_transport_states(void)
{
	struct ao2_container *states = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		DEFAULT_STATE_BUCKETS, transport_state_hash, NULL, transport_state_cmp);

	if (!states) {
		return NULL;
	}

	ao2_callback(transport_states, OBJ_NODATA | OBJ_MULTIPLE, populate_transport_states, states);
	return states;
}

/*! \brief Initialize sorcery with transport support */
int ast_sip_initialize_sorcery_transport(void)
{
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();
	struct ao2_container *transports = NULL;

	/* Create outbound registration states container. */
	transport_states = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		DEFAULT_STATE_BUCKETS, internal_state_hash, NULL, internal_state_cmp);
	if (!transport_states) {
		ast_log(LOG_ERROR, "Unable to allocate transport states container\n");
		return -1;
	}

	ast_sorcery_apply_default(sorcery, "transport", "config", "pjsip.conf,criteria=type=transport");

	if (ast_sorcery_object_register(sorcery, "transport", sip_transport_alloc, NULL, transport_apply)) {
		return -1;
	}

	/* Normally type is a OPT_NOOP_T but we're using it to make sure that state is created */
	ast_sorcery_object_field_register_custom(sorcery, "transport", "type", "", transport_state_init, NULL, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "protocol", "udp", transport_protocol_handler, transport_protocol_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "bind", "", transport_bind_handler, transport_bind_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "transport", "async_operations", "1", OPT_UINT_T, 0, FLDSET(struct ast_sip_transport, async_operations));

	ast_sorcery_object_field_register_custom(sorcery, "transport", "ca_list_file", "", transport_tls_file_handler, ca_list_file_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "ca_list_path", "", transport_tls_file_handler, ca_list_path_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "cert_file", "", transport_tls_file_handler, cert_file_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "priv_key_file", "", transport_tls_file_handler, privkey_file_to_str, NULL, 0, 0);

	ast_sorcery_object_field_register(sorcery, "transport", "password", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, password));
	ast_sorcery_object_field_register(sorcery, "transport", "external_signaling_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, external_signaling_address));
	ast_sorcery_object_field_register(sorcery, "transport", "external_signaling_port", "0", OPT_UINT_T, PARSE_IN_RANGE, FLDSET(struct ast_sip_transport, external_signaling_port), 0, 65535);
	ast_sorcery_object_field_register(sorcery, "transport", "external_media_address", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, external_media_address));
	ast_sorcery_object_field_register(sorcery, "transport", "domain", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct ast_sip_transport, domain));
	ast_sorcery_object_field_register_custom(sorcery, "transport", "verify_server", "", transport_tls_bool_handler, verify_server_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "verify_client", "", transport_tls_bool_handler, verify_client_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "require_client_cert", "", transport_tls_bool_handler, require_client_cert_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "allow_wildcard_certs", "", transport_tls_bool_handler, allow_wildcard_certs_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "method", "", transport_tls_method_handler, tls_method_to_str, NULL, 0, 0);
#if defined(PJ_HAS_SSL_SOCK) && PJ_HAS_SSL_SOCK != 0
	ast_sorcery_object_field_register_custom(sorcery, "transport", "cipher", "", transport_tls_cipher_handler, transport_tls_cipher_to_str, NULL, 0, 0);
#endif
	ast_sorcery_object_field_register_custom(sorcery, "transport", "local_net", "", transport_localnet_handler, localnet_to_str, localnet_to_vl, 0, 0);
	ast_sorcery_object_field_register_custom(sorcery, "transport", "tos", "0", transport_tos_handler, tos_to_str, NULL, 0, 0);
	ast_sorcery_object_field_register(sorcery, "transport", "cos", "0", OPT_UINT_T, 0, FLDSET(struct ast_sip_transport, cos));
	ast_sorcery_object_field_register(sorcery, "transport", "websocket_write_timeout", AST_DEFAULT_WEBSOCKET_WRITE_TIMEOUT_STR, OPT_INT_T, PARSE_IN_RANGE, FLDSET(struct ast_sip_transport, write_timeout), 1, INT_MAX);
	ast_sorcery_object_field_register(sorcery, "transport", "allow_reload", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_transport, allow_reload));
	ast_sorcery_object_field_register(sorcery, "transport", "symmetric_transport", "no", OPT_BOOL_T, 1, FLDSET(struct ast_sip_transport, symmetric_transport));

	ast_sip_register_endpoint_formatter(&endpoint_transport_formatter);

	cli_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!cli_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for cli formatter\n");
		return -1;
	}
	cli_formatter->name = "transport";
	cli_formatter->print_header = cli_print_header;
	cli_formatter->print_body = cli_print_body;
	cli_formatter->get_container = cli_get_container;
	cli_formatter->iterate = cli_iterate;
	cli_formatter->get_id = ast_sorcery_object_get_id;
	cli_formatter->retrieve_by_id = cli_retrieve_by_id;

	ast_sip_register_cli_formatter(cli_formatter);
	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	/* trigger load of transports from realtime by trying to revrieve them all */
	transports = ast_sorcery_retrieve_by_fields(sorcery, "transport", AST_RETRIEVE_FLAG_ALL | AST_RETRIEVE_FLAG_MULTIPLE, NULL);
	ao2_cleanup(transports);

	return 0;
}

int ast_sip_destroy_sorcery_transport(void)
{
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));
	ast_sip_unregister_cli_formatter(cli_formatter);

	ast_sip_unregister_endpoint_formatter(&endpoint_transport_formatter);

	ao2_ref(transport_states, -1);
	transport_states = NULL;

	return 0;
}
