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

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_sip</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_sip.h"
#include "asterisk/module.h"
#include "asterisk/taskprocessor.h"

/*** DOCUMENTATION
	<configInfo name="res_sip_outbound_registration" language="en_US">
		<synopsis>SIP resource for outbound registrations</synopsis>
		<description><para>
			<emphasis>Outbound Registration</emphasis>
			</para>
			<para>This module allows <literal>res_sip</literal> to register to other SIP servers.</para>
		</description>
		<configFile name="res_sip.conf">
			<configObject name="registration">
				<synopsis>The configuration for outbound registration</synopsis>
				<description><para>
					Registration is <emphasis>COMPLETELY</emphasis> separate from the rest of
					<literal>res_sip.conf</literal>. A minimal configuration consists of
					setting a <literal>server_uri</literal>	and a <literal>client_uri</literal>.
				</para></description>
				<configOption name="auth_rejection_permanent" default="yes">
					<synopsis>Determines whether failed authentication challenges are treated
					as permanent failures.</synopsis>
					<description><para>If this option is enabled and an authentication challenge fails,
					registration will not be attempted again until the configuration is reloaded.</para></description>
				</configOption>
				<configOption name="client_uri">
					<synopsis>Client SIP URI used when attemping outbound registration</synopsis>
				</configOption>
				<configOption name="contact_user">
					<synopsis>Contact User to use in request</synopsis>
				</configOption>
				<configOption name="expiration" default="3600">
					<synopsis>Expiration time for registrations in seconds</synopsis>
				</configOption>
				<configOption name="max_retries" default="10">
					<synopsis>Maximum number of registration attempts.</synopsis>
				</configOption>
				<configOption name="outbound_auth" default="">
					<synopsis>Authentication object to be used for outbound registrations.</synopsis>
				</configOption>
				<configOption name="outbound_proxy" default="">
					<synopsis>Outbound Proxy used to send registrations</synopsis>
				</configOption>
				<configOption name="retry_interval" default="60">
					<synopsis>Interval in seconds between retries if outbound registration is unsuccessful</synopsis>
				</configOption>
				<configOption name="server_uri">
					<synopsis>SIP URI of the server to register against</synopsis>
				</configOption>
				<configOption name="transport">
					<synopsis>Transport used for outbound authentication</synopsis>
					<description>
						<note><para>A <replaceable>transport</replaceable> configured in
						<literal>res_sip.conf</literal>. As with other <literal>res_sip</literal> modules, this will use the first available transport of the appropriate type if unconfigured.</para></note>
					</description>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'registration'.</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
 ***/

/*! \brief Amount of buffer time (in seconds) before expiration that we re-register at */
#define REREGISTER_BUFFER_TIME 10

/*! \brief Various states that an outbound registration may be in */
enum sip_outbound_registration_status {
	/*! \brief Currently unregistered */
	SIP_REGISTRATION_UNREGISTERED = 0,
	/*! \brief Registered, yay! */
	SIP_REGISTRATION_REGISTERED,
	/*! \brief Registration was rejected, but response was temporal */
	SIP_REGISTRATION_REJECTED_TEMPORARY,
	/*! \brief Registration was rejected, permanently */
	SIP_REGISTRATION_REJECTED_PERMANENT,
	/*! \brief Registration has been stopped */
	SIP_REGISTRATION_STOPPED,
};

/*! \brief Outbound registration client state information (persists for lifetime of regc) */
struct sip_outbound_registration_client_state {
	/*! \brief Current status of this registration */
	enum sip_outbound_registration_status status;
	/*! \brief Outbound registration client */
	pjsip_regc *client;
	/*! \brief Timer entry for retrying on temporal responses */
	pj_timer_entry timer;
	/*! \brief Current number of retries */
	unsigned int retries;
	/*! \brief Maximum number of retries permitted */
	unsigned int max_retries;
	/*! \brief Interval at which retries should occur for temporal responses */
	unsigned int retry_interval;
	/*! \brief Treat authentication challenges that we cannot handle as permanent failures */
	unsigned int auth_rejection_permanent;
	/*! \brief Serializer for stuff and things */
	struct ast_taskprocessor *serializer;
	/*! \brief Configured authentication credentials */
	const char **sip_outbound_auths;
	/*! \brief Number of configured auths */
	size_t num_outbound_auths;
	/*! \brief Registration should be destroyed after completion of transaction */
	unsigned int destroy:1;
};

/*! \brief Outbound registration state information (persists for lifetime that registration should exist) */
struct sip_outbound_registration_state {
	/*! \brief Client state information */
	struct sip_outbound_registration_client_state *client_state;
};

/*! \brief Outbound registration information */
struct sip_outbound_registration {
	/*! \brief Sorcery object details */
	SORCERY_OBJECT(details);
	/*! \brief Stringfields */
	AST_DECLARE_STRING_FIELDS(
		/*! \brief URI for the registrar */
		AST_STRING_FIELD(server_uri);
		/*! \brief URI for the AOR */
		AST_STRING_FIELD(client_uri);
		/*! \brief Optional user for contact header */
		AST_STRING_FIELD(contact_user);
		/*! \brief Explicit transport to use for registration */
		AST_STRING_FIELD(transport);
		/*! \brief Outbound proxy to use */
		AST_STRING_FIELD(outbound_proxy);
	);
	/*! \brief Requested expiration time */
	unsigned int expiration;
	/*! \brief Interval at which retries should occur for temporal responses */
	unsigned int retry_interval;
	/*! \brief Treat authentication challenges that we cannot handle as permanent failures */
	unsigned int auth_rejection_permanent;
	/*! \brief Maximum number of retries permitted */
	unsigned int max_retries;
	/*! \brief Outbound registration state */
	struct sip_outbound_registration_state *state;
	/*! \brief Configured authentication credentials */
	const char **sip_outbound_auths;
	/*! \brief Number of configured auths */
	size_t num_outbound_auths;
};

static void destroy_auths(const char **auths, size_t num_auths)
{
	int i;
	for (i = 0; i < num_auths; ++i) {
		ast_free((char *) auths[i]);
	}
	ast_free(auths);
}

/*! \brief Helper function which cancels the timer on a client */
static void cancel_registration(struct sip_outbound_registration_client_state *client_state)
{
	if (pj_timer_heap_cancel(pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()), &client_state->timer)) {
		/* The timer was successfully cancelled, drop the refcount of client_state */
		ao2_ref(client_state, -1);
	}
}

/*! \brief Callback function for registering */
static int handle_client_registration(void *data)
{
	RAII_VAR(struct sip_outbound_registration_client_state *, client_state, data, ao2_cleanup);
	pjsip_tx_data *tdata;

	cancel_registration(client_state);

	if ((client_state->status == SIP_REGISTRATION_STOPPED) ||
		(pjsip_regc_register(client_state->client, PJ_FALSE, &tdata) != PJ_SUCCESS)) {
		return 0;
	}

	/* Due to the registration the callback may now get called, so bump the ref count */
	ao2_ref(client_state, +1);
	if (pjsip_regc_send(client_state->client, tdata) != PJ_SUCCESS) {
		ao2_ref(client_state, -1);
		pjsip_tx_data_dec_ref(tdata);
	}

	return 0;
}

/*! \brief Timer callback function, used just for registrations */
static void sip_outbound_registration_timer_cb(pj_timer_heap_t *timer_heap, struct pj_timer_entry *entry)
{
	RAII_VAR(struct sip_outbound_registration_client_state *, client_state, entry->user_data, ao2_cleanup);

	ao2_ref(client_state, +1);
	if (ast_sip_push_task(client_state->serializer, handle_client_registration, client_state)) {
		ast_log(LOG_WARNING, "Failed to pass outbound registration to threadpool\n");
		ao2_ref(client_state, -1);
	}

	entry->id = 0;
}

/*! \brief Helper function which sets up the timer to re-register in a specific amount of time */
static void schedule_registration(struct sip_outbound_registration_client_state *client_state, unsigned int seconds)
{
	pj_time_val delay = { .sec = seconds, };

	cancel_registration(client_state);

	ao2_ref(client_state, +1);
	if (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(), &client_state->timer, &delay) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to pass timed registration to scheduler\n");
		ao2_ref(client_state, -1);
	}
}

/*! \brief Callback function for unregistering (potentially) and destroying state */
static int handle_client_state_destruction(void *data)
{
	RAII_VAR(struct sip_outbound_registration_client_state *, client_state, data, ao2_cleanup);
	pjsip_regc_info info;

	cancel_registration(client_state);

	pjsip_regc_get_info(client_state->client, &info);

	if (info.is_busy == PJ_TRUE) {
		/* If a client transaction is in progress we defer until it is complete */
		client_state->destroy = 1;
		return 0;
	}

	if (client_state->status != SIP_REGISTRATION_UNREGISTERED && client_state->status != SIP_REGISTRATION_REJECTED_PERMANENT) {
		pjsip_tx_data *tdata;

		if (pjsip_regc_unregister(client_state->client, &tdata) == PJ_SUCCESS) {
			pjsip_regc_send(client_state->client, tdata);
		}
	}

	pjsip_regc_destroy(client_state->client);

	client_state->status = SIP_REGISTRATION_STOPPED;
	destroy_auths(client_state->sip_outbound_auths, client_state->num_outbound_auths);

	return 0;
}

/*! \brief Structure for registration response */
struct registration_response {
	/*! \brief Response code for the registration attempt */
	int code;
	/*! \brief Expiration time for registration */
	int expiration;
	/*! \brief Retry-After value */
	int retry_after;
	/*! \brief Outbound registration client state */
	struct sip_outbound_registration_client_state *client_state;
	/*! \brief The response message */
	pjsip_rx_data *rdata;
	/*! \brief The response transaction */
	pjsip_transaction *tsx;
};

/*! \brief Registration response structure destructor */
static void registration_response_destroy(void *obj)
{
	struct registration_response *response = obj;

	pjsip_rx_data_free_cloned(response->rdata);
	ao2_cleanup(response->client_state);
}

/* \brief Helper funtion which determines if a response code is temporal or not */
static int sip_outbound_registration_is_temporal(unsigned int code,
		struct sip_outbound_registration_client_state *client_state)
{
	/* Shamelessly taken from pjsua */
	if (code == PJSIP_SC_REQUEST_TIMEOUT ||
		code == PJSIP_SC_INTERNAL_SERVER_ERROR ||
		code == PJSIP_SC_BAD_GATEWAY ||
		code == PJSIP_SC_SERVICE_UNAVAILABLE ||
		code == PJSIP_SC_SERVER_TIMEOUT ||
		((code == PJSIP_SC_UNAUTHORIZED ||
		  code == PJSIP_SC_PROXY_AUTHENTICATION_REQUIRED) &&
		 !client_state->auth_rejection_permanent) ||
		PJSIP_IS_STATUS_IN_CLASS(code, 600)) {
		return 1;
	} else {
		return 0;
	}
}

/*! \brief Callback function for handling a response to a registration attempt */
static int handle_registration_response(void *data)
{
	RAII_VAR(struct registration_response *, response, data, ao2_cleanup);
	pjsip_regc_info info;
	char server_uri[PJSIP_MAX_URL_SIZE], client_uri[PJSIP_MAX_URL_SIZE];

	if (response->client_state->status == SIP_REGISTRATION_STOPPED) {
		return 0;
	}

	pjsip_regc_get_info(response->client_state->client, &info);
	ast_copy_pj_str(server_uri, &info.server_uri, sizeof(server_uri));
	ast_copy_pj_str(client_uri, &info.client_uri, sizeof(client_uri));

	if (response->code == 401 || response->code == 407) {
		pjsip_tx_data *tdata;
		if (!ast_sip_create_request_with_auth(response->client_state->sip_outbound_auths, response->client_state->num_outbound_auths,
				response->rdata, response->tsx, &tdata)) {
			pjsip_regc_send(response->client_state->client, tdata);
			return 0;
		}
		/* Otherwise, fall through so the failure is processed appropriately */
	}

	if (PJSIP_IS_STATUS_IN_CLASS(response->code, 200)) {
		/* If the registration went fine simply reschedule registration for the future */
		response->client_state->status = SIP_REGISTRATION_REGISTERED;
		response->client_state->retries = 0;
		schedule_registration(response->client_state, response->expiration - REREGISTER_BUFFER_TIME);
	} else if (response->retry_after) {
		/* If we have been instructed to retry after a period of time, schedule it as such */
		response->client_state->status = SIP_REGISTRATION_REJECTED_TEMPORARY;
		schedule_registration(response->client_state, response->retry_after);
		ast_log(LOG_WARNING, "Temporal response '%d' received from '%s' on registration attempt to '%s', instructed to retry in '%d'\n",
			response->code, server_uri, client_uri, response->retry_after);
	} else if (response->client_state->retry_interval && sip_outbound_registration_is_temporal(response->code, response->client_state)) {
		if (response->client_state->retries == response->client_state->max_retries) {
			/* If we received enough temporal responses to exceed our maximum give up permanently */
			response->client_state->status = SIP_REGISTRATION_REJECTED_PERMANENT;
			ast_log(LOG_WARNING, "Maximum retries reached when attempting outbound registration to '%s' with client '%s', stopping registration attempt\n",
				server_uri, client_uri);
		} else {
			/* On the other hand if we can still try some more do so */
			response->client_state->status = SIP_REGISTRATION_REJECTED_TEMPORARY;
			response->client_state->retries++;
			schedule_registration(response->client_state, response->client_state->retry_interval);
			ast_log(LOG_WARNING, "Temporal response '%d' received from '%s' on registration attempt to '%s', retrying in '%d' seconds\n",
				response->code, server_uri, client_uri, response->client_state->retry_interval);
		}
	} else {
		/* Finally if there's no hope of registering give up */
		response->client_state->status = SIP_REGISTRATION_REJECTED_PERMANENT;
		ast_log(LOG_WARNING, "Fatal response '%d' received from '%s' on registration attempt to '%s', stopping outbound registration\n",
			response->code, server_uri, client_uri);
	}

	/* If deferred destruction is in use see if we need to destroy now */
	if (response->client_state->destroy) {
		handle_client_state_destruction(response->client_state);
	}

	return 0;
}

/*! \brief Callback function for outbound registration client */
static void sip_outbound_registration_response_cb(struct pjsip_regc_cbparam *param)
{
	RAII_VAR(struct sip_outbound_registration_client_state *, client_state, param->token, ao2_cleanup);
	struct registration_response *response = ao2_alloc(sizeof(*response), registration_response_destroy);
	struct pjsip_retry_after_hdr *retry_after = pjsip_msg_find_hdr(param->rdata->msg_info.msg, PJSIP_H_RETRY_AFTER, NULL);

	response->code = param->code;
	response->expiration = param->expiration;
	response->retry_after = retry_after ? retry_after->ivalue : 0;
	response->client_state = client_state;
	response->tsx = pjsip_rdata_get_tsx(param->rdata);
	pjsip_rx_data_clone(param->rdata, 0, &response->rdata);
	ao2_ref(response->client_state, +1);

	if (ast_sip_push_task(client_state->serializer, handle_registration_response, response)) {
		ast_log(LOG_WARNING, "Failed to pass incoming registration response to threadpool\n");
		ao2_cleanup(response);
	}
}

/*! \brief Destructor function for registration state */
static void sip_outbound_registration_state_destroy(void *obj)
{
	struct sip_outbound_registration_state *state = obj;

	if (!state->client_state) {
		return;
	}

	if (state->client_state->serializer && ast_sip_push_task(state->client_state->serializer, handle_client_state_destruction, state->client_state)) {
		ast_log(LOG_WARNING, "Failed to pass outbound registration client destruction to threadpool\n");
		ao2_ref(state->client_state, -1);
	}
}

/*! \brief Destructor function for client registration state */
static void sip_outbound_registration_client_state_destroy(void *obj)
{
	struct sip_outbound_registration_client_state *client_state = obj;

	ast_taskprocessor_unreference(client_state->serializer);
}

/*! \brief Allocator function for registration state */
static struct sip_outbound_registration_state *sip_outbound_registration_state_alloc(void)
{
	struct sip_outbound_registration_state *state = ao2_alloc(sizeof(*state), sip_outbound_registration_state_destroy);

	if (!state || !(state->client_state = ao2_alloc(sizeof(*state->client_state), sip_outbound_registration_client_state_destroy))) {
		ao2_cleanup(state);
		return NULL;
	}

	if ((pjsip_regc_create(ast_sip_get_pjsip_endpoint(), state->client_state, sip_outbound_registration_response_cb, &state->client_state->client) != PJ_SUCCESS) ||
		!(state->client_state->serializer = ast_sip_create_serializer())) {
		/* This is on purpose, normal operation will have it be deallocated within the serializer */
		pjsip_regc_destroy(state->client_state->client);
		ao2_cleanup(state->client_state);
		ao2_cleanup(state);
		return NULL;
	}

	state->client_state->status = SIP_REGISTRATION_UNREGISTERED;
	state->client_state->timer.user_data = state->client_state;
	state->client_state->timer.cb = sip_outbound_registration_timer_cb;

	return state;
}

/*! \brief Destructor function for registration information */
static void sip_outbound_registration_destroy(void *obj)
{
	struct sip_outbound_registration *registration = obj;

	ao2_cleanup(registration->state);
	destroy_auths(registration->sip_outbound_auths, registration->num_outbound_auths);

	ast_string_field_free_memory(registration);
}

/*! \brief Allocator function for registration information */
static void *sip_outbound_registration_alloc(const char *name)
{
	struct sip_outbound_registration *registration = ao2_alloc(sizeof(*registration), sip_outbound_registration_destroy);

	if (!registration || ast_string_field_init(registration, 256)) {
		ao2_cleanup(registration);
		return NULL;
	}

	return registration;
}

/*! \brief Helper function which populates a pj_str_t with a contact header */
static int sip_dialog_create_contact(pj_pool_t *pool, pj_str_t *contact, const char *user, const pj_str_t *target, pjsip_tpselector *selector)
{
	pj_str_t tmp, local_addr;
	pjsip_uri *uri;
	pjsip_sip_uri *sip_uri;
	pjsip_transport_type_e type = PJSIP_TRANSPORT_UNSPECIFIED;
	int local_port;

	pj_strdup_with_null(pool, &tmp, target);

	if (!(uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0)) ||
	    (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		return -1;
	}

	sip_uri = pjsip_uri_get_uri(uri);

	if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri)) {
		type = PJSIP_TRANSPORT_TLS;
	} else if (!sip_uri->transport_param.slen) {
		type = PJSIP_TRANSPORT_UDP;
	} else {
		type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
	}

	if (type == PJSIP_TRANSPORT_UNSPECIFIED) {
		return -1;
	}

	if (pj_strchr(&sip_uri->host, ':')) {
		type = (pjsip_transport_type_e)(((int)type) + PJSIP_TRANSPORT_IPV6);
	}

	if (pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()), pool, type, selector,
							      &local_addr, &local_port) != PJ_SUCCESS) {
		return -1;
	}

	if (!pj_strchr(&sip_uri->host, ':') && pj_strchr(&local_addr, ':')) {
		type = (pjsip_transport_type_e)(((int)type) + PJSIP_TRANSPORT_IPV6);
	}

	contact->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
	contact->slen = pj_ansi_snprintf(contact->ptr, PJSIP_MAX_URL_SIZE,
				      "<%s:%s@%s%.*s%s:%d%s%s>",
				      (pjsip_transport_get_flag_from_type(type) & PJSIP_TRANSPORT_SECURE) ? "sips" : "sip",
				      user,
				      (type & PJSIP_TRANSPORT_IPV6) ? "[" : "",
				      (int)local_addr.slen,
				      local_addr.ptr,
				      (type & PJSIP_TRANSPORT_IPV6) ? "]" : "",
				      local_port,
				      (type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
				      (type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");

	return 0;
}

/*!
 * \internal
 * \brief Check if a registration can be reused
 *
 * This checks if the existing outbound registration's configuration differs from a newly-applied
 * outbound registration to see if the applied one.
 *
 * \param existing The pre-existing outbound registration
 * \param applied The newly-created registration
 */
static int can_reuse_registration(struct sip_outbound_registration *existing, struct sip_outbound_registration *applied)
{
	int i;

	if (strcmp(existing->server_uri, applied->server_uri) || strcmp(existing->client_uri, applied->client_uri) ||
		strcmp(existing->transport, applied->transport) || strcmp(existing->contact_user, applied->contact_user) ||
		strcmp(existing->outbound_proxy, applied->outbound_proxy) || existing->num_outbound_auths != applied->num_outbound_auths ||
		existing->auth_rejection_permanent != applied->auth_rejection_permanent) {
		return 0;
	}

	for (i = 0; i < existing->num_outbound_auths; ++i) {
		if (strcmp(existing->sip_outbound_auths[i], applied->sip_outbound_auths[i])) {
			return 0;
		}
	}

	return 1;
}

/*! \brief Apply function which finds or allocates a state structure */
static int sip_outbound_registration_apply(const struct ast_sorcery *sorcery, void *obj)
{
	RAII_VAR(struct sip_outbound_registration *, existing, ast_sorcery_retrieve_by_id(sorcery, "registration", ast_sorcery_object_get_id(obj)), ao2_cleanup);
	struct sip_outbound_registration *applied = obj;
	pj_str_t server_uri, client_uri, contact_uri;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };

	if (!existing) {
		/* If no existing registration exists we can just start fresh easily */
		applied->state = sip_outbound_registration_state_alloc();
	} else {
		/* If there is an existing registration things are more complicated, we can immediately reuse this state if most stuff remains unchanged */
		if (can_reuse_registration(existing, applied)) {
			applied->state = existing->state;
			ao2_ref(applied->state, +1);
			return 0;
		}
		applied->state = sip_outbound_registration_state_alloc();
	}

	if (!applied->state) {
		return -1;
	}

	if (!ast_strlen_zero(applied->transport)) {
		RAII_VAR(struct ast_sip_transport *, transport, ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", applied->transport), ao2_cleanup);

		if (!transport || !transport->state) {
			return -1;
		}

		if (transport->type == AST_TRANSPORT_UDP) {
			selector.type = PJSIP_TPSELECTOR_TRANSPORT;
			selector.u.transport = transport->state->transport;
		} else if (transport->type == AST_TRANSPORT_TCP || transport->type == AST_TRANSPORT_TLS) {
			selector.type = PJSIP_TPSELECTOR_LISTENER;
			selector.u.listener = transport->state->factory;
		} else {
			return -1;
		}
	}

	pjsip_regc_set_transport(applied->state->client_state->client, &selector);

	if (!ast_strlen_zero(applied->outbound_proxy)) {
		pjsip_route_hdr route_set, *route;
		static const pj_str_t ROUTE_HNAME = { "Route", 5 };
		pj_str_t tmp;

		pj_list_init(&route_set);

		pj_strdup2_with_null(pjsip_regc_get_pool(applied->state->client_state->client), &tmp, applied->outbound_proxy);
		if (!(route = pjsip_parse_hdr(pjsip_regc_get_pool(applied->state->client_state->client), &ROUTE_HNAME, tmp.ptr, tmp.slen, NULL))) {
			return -1;
		}
		pj_list_push_back(&route_set, route);

		pjsip_regc_set_route_set(applied->state->client_state->client, &route_set);
	}

	pj_cstr(&server_uri, applied->server_uri);

	if (sip_dialog_create_contact(pjsip_regc_get_pool(applied->state->client_state->client), &contact_uri, S_OR(applied->contact_user, "s"), &server_uri, &selector)) {
		return -1;
	}

	pj_cstr(&client_uri, applied->client_uri);

	if (pjsip_regc_init(applied->state->client_state->client, &server_uri, &client_uri, &client_uri, 1, &contact_uri, applied->expiration) != PJ_SUCCESS) {
		return -1;
	}

	return 0;
}

/*! \brief Helper function which performs a single registration */
static int sip_outbound_registration_perform(void *obj, void *arg, int flags)
{
	struct sip_outbound_registration *registration = obj;
	size_t i;

	/* Just in case the client state is being reused for this registration, free the auth information */
	destroy_auths(registration->state->client_state->sip_outbound_auths,
			registration->state->client_state->num_outbound_auths);
	registration->state->client_state->num_outbound_auths = 0;

	registration->state->client_state->sip_outbound_auths = ast_calloc(registration->num_outbound_auths, sizeof(char *));
	for (i = 0; i < registration->num_outbound_auths; ++i) {
		registration->state->client_state->sip_outbound_auths[i] = ast_strdup(registration->sip_outbound_auths[i]);
	}
	registration->state->client_state->num_outbound_auths = registration->num_outbound_auths;
	registration->state->client_state->retry_interval = registration->retry_interval;
	registration->state->client_state->max_retries = registration->max_retries;
	registration->state->client_state->retries = 0;

	pjsip_regc_update_expires(registration->state->client_state->client, registration->expiration);

	schedule_registration(registration->state->client_state, (ast_random() % 10) + 1);

	return 0;
}

/*! \brief Helper function which performs all registrations */
static void sip_outbound_registration_perform_all(void)
{
	RAII_VAR(struct ao2_container *, registrations, ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "registration", AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL), ao2_cleanup);

	if (!registrations) {
		return;
	}

	ao2_callback(registrations, OBJ_NODATA, sip_outbound_registration_perform, NULL);
}

#define AUTH_INCREMENT 4

static const char **auth_alloc(const char *value, size_t *num_auths)
{
	char *auths = ast_strdupa(value);
	char *val;
	int num_alloced = 0;
	const char **alloced_auths = NULL;

	while ((val = strsep(&auths, ","))) {
		if (*num_auths >= num_alloced) {
			size_t size;
			num_alloced += AUTH_INCREMENT;
			size = num_alloced * sizeof(char *);
			alloced_auths = ast_realloc(alloced_auths, size);
			if (!alloced_auths) {
				goto failure;
			}
		}
		alloced_auths[*num_auths] = ast_strdup(val);
		if (!alloced_auths[*num_auths]) {
			goto failure;
		}
		++(*num_auths);
	}
	return alloced_auths;

failure:
	destroy_auths(alloced_auths, *num_auths);
	return NULL;
}

static int outbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct sip_outbound_registration *registration = obj;

	registration->sip_outbound_auths = auth_alloc(var->value, &registration->num_outbound_auths);
	if (!registration->sip_outbound_auths) {
		return -1;
	}
	return 0;
}

static int load_module(void)
{
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "registration", "config", "res_sip.conf,criteria=type=registration");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "registration", sip_outbound_registration_alloc, NULL, sip_outbound_registration_apply)) {
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "type", "", OPT_NOOP_T, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "server_uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sip_outbound_registration, server_uri));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "client_uri", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sip_outbound_registration, client_uri));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "contact_user", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sip_outbound_registration, contact_user));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "transport", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sip_outbound_registration, transport));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "outbound_proxy", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sip_outbound_registration, outbound_proxy));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "expiration", "3600", OPT_UINT_T, 0, FLDSET(struct sip_outbound_registration, expiration));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "retry_interval", "60", OPT_UINT_T, 0, FLDSET(struct sip_outbound_registration, retry_interval));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "max_retries", "10", OPT_UINT_T, 0, FLDSET(struct sip_outbound_registration, max_retries));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "auth_rejection_permanent", "yes", OPT_BOOL_T, 1, FLDSET(struct sip_outbound_registration, auth_rejection_permanent));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "registration", "outbound_auth", "", outbound_auth_handler, NULL, 0, 0);
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "registration");
	sip_outbound_registration_perform_all();

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "registration");
	sip_outbound_registration_perform_all();
	return 0;
}

static int unload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SIP Outbound Registration Support",
		.load = load_module,
		.reload = reload_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
