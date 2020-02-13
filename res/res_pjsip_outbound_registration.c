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
	<depend>res_pjsip</depend>
	<use type="module">res_statsd</use>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/module.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/cli.h"
#include "asterisk/stasis_system.h"
#include "asterisk/threadstorage.h"
#include "asterisk/threadpool.h"
#include "asterisk/statsd.h"
#include "res_pjsip/include/res_pjsip_private.h"

/*** DOCUMENTATION
	<configInfo name="res_pjsip_outbound_registration" language="en_US">
		<synopsis>SIP resource for outbound registrations</synopsis>
		<description><para>
			<emphasis>Outbound Registration</emphasis>
			</para>
			<para>This module allows <literal>res_pjsip</literal> to register to other SIP servers.</para>
		</description>
		<configFile name="pjsip.conf">
			<configObject name="registration">
				<synopsis>The configuration for outbound registration</synopsis>
				<description><para>
					Registration is <emphasis>COMPLETELY</emphasis> separate from the rest of
					<literal>pjsip.conf</literal>. A minimal configuration consists of
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
					<description><para>
						This is the address-of-record for the outbound registration (i.e. the URI in
						the To header of the REGISTER).</para>
						<para>For registration with an ITSP, the client SIP URI may need to consist of
						an account name or number and the provider's hostname for their registrar, e.g.
						client_uri=1234567890@example.com. This may differ between providers.</para>
						<para>For registration to generic registrars, the client SIP URI will depend
						on networking specifics and configuration of the registrar.
					</para></description>
				</configOption>
				<configOption name="contact_user">
					<synopsis>Contact User to use in request</synopsis>
				</configOption>
				<configOption name="expiration" default="3600">
					<synopsis>Expiration time for registrations in seconds</synopsis>
				</configOption>
				<configOption name="max_retries" default="10">
					<synopsis>Maximum number of registration attempts.</synopsis>
					<description><para>
						This sets the maximum number of registration attempts that are made before
						stopping any further attempts. If set to 0 then upon failure no further attempts
						are made.
					</para></description>
				</configOption>
				<configOption name="outbound_auth" default="">
					<synopsis>Authentication object(s) to be used for outbound registrations.</synopsis>
					<description><para>
						This is a comma-delimited list of <replaceable>auth</replaceable>
						sections defined in <filename>pjsip.conf</filename> used to respond
						to outbound authentication challenges.</para>
						<note><para>
						Using the same auth section for inbound and outbound
						authentication is not recommended.  There is a difference in
						meaning for an empty realm setting between inbound and outbound
						authentication uses.  See the auth realm description for details.
						</para></note>
					</description>
				</configOption>
				<configOption name="outbound_proxy" default="">
					<synopsis>Full SIP URI of the outbound proxy used to send registrations</synopsis>
				</configOption>
				<configOption name="retry_interval" default="60">
					<synopsis>Interval in seconds between retries if outbound registration is unsuccessful</synopsis>
				</configOption>
				<configOption name="forbidden_retry_interval" default="0">
					<synopsis>Interval used when receiving a 403 Forbidden response.</synopsis>
					<description><para>
						If a 403 Forbidden is received, chan_pjsip will wait
						<replaceable>forbidden_retry_interval</replaceable> seconds before
						attempting registration again. If 0 is specified, chan_pjsip will not
						retry after receiving a 403 Forbidden response. Setting this to a non-zero
						value goes against a "SHOULD NOT" in RFC3261, but can be used to work around
						buggy registrars.
					</para></description>
				</configOption>
				<configOption name="fatal_retry_interval" default="0">
					<synopsis>Interval used when receiving a Fatal response.</synopsis>
					<description><para>
						If a fatal response is received, chan_pjsip will wait
						<replaceable>fatal_retry_interval</replaceable> seconds before
						attempting registration again. If 0 is specified, chan_pjsip will not
						retry after receiving a fatal (non-temporary 4xx, 5xx, 6xx) response.
						Setting this to a non-zero value may go against a "SHOULD NOT" in RFC3261,
						but can be used to work around buggy registrars.</para>
						<note><para>if also set the <replaceable>forbidden_retry_interval</replaceable>
						takes precedence over this one when a 403 is received.
						Also, if <replaceable>auth_rejection_permanent</replaceable> equals 'yes' then
						a 401 and 407 become subject to this retry interval.</para></note>
					</description>
				</configOption>
				<configOption name="server_uri">
					<synopsis>SIP URI of the server to register against</synopsis>
					<description><para>
						This is the URI at which to find the registrar to send the outbound REGISTER. This URI
						is used as the request URI of the outbound REGISTER request from Asterisk.</para>
						<para>For registration with an ITSP, the setting may often be just the domain of
						the registrar, e.g. sip:sip.example.com.
					</para></description>
				</configOption>
				<configOption name="transport">
					<synopsis>Transport used for outbound authentication</synopsis>
					<description>
						<note><para>A <replaceable>transport</replaceable> configured in
						<literal>pjsip.conf</literal>. As with other <literal>res_pjsip</literal> modules, this will use the first available transport of the appropriate type if unconfigured.</para></note>
					</description>
				</configOption>
				<configOption name="line">
					<synopsis>Whether to add a 'line' parameter to the Contact for inbound call matching</synopsis>
					<description><para>
						When enabled this option will cause a 'line' parameter to be added to the Contact
						header placed into the outgoing registration request. If the remote server sends a call
						this line parameter will be used to establish a relationship to the outbound registration,
						ultimately causing the configured endpoint to be used.
					</para></description>
				</configOption>
				<configOption name="endpoint">
					<synopsis>Endpoint to use for incoming related calls</synopsis>
					<description><para>
						When line support is enabled this configured endpoint name is used for incoming calls
						that are related to the outbound registration.
					</para></description>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'registration'.</synopsis>
				</configOption>
				<configOption name="support_path">
					<synopsis>Enables Path support for outbound REGISTER requests.</synopsis>
					<description><para>
						When this option is enabled, outbound REGISTER requests will advertise
						support for Path headers so that intervening proxies can add to the Path
						header as necessary.
					</para></description>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
	<manager name="PJSIPUnregister" language="en_US">
		<synopsis>
			Unregister an outbound registration.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Registration" required="true">
				<para>The outbound registration to unregister or '*all' to unregister them all.</para>
			</parameter>
		</syntax>
		<description>
			<para>
			Unregisters the specified (or all) outbound registration(s) and stops future registration attempts.
			Call PJSIPRegister to start registration and schedule re-registrations according to configuration.
            </para>
		</description>
	</manager>
	<manager name="PJSIPRegister" language="en_US">
		<synopsis>
			Register an outbound registration.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Registration" required="true">
				<para>The outbound registration to register or '*all' to register them all.</para>
			</parameter>
		</syntax>
		<description>
			<para>
			Unregisters the specified (or all) outbound registration(s) then starts registration and schedules re-registrations
			according to configuration.
            </para>
		</description>
	</manager>
	<manager name="PJSIPShowRegistrationsOutbound" language="en_US">
		<synopsis>
			Lists PJSIP outbound registrations.
		</synopsis>
		<syntax />
		<description>
			<para>
			In response <literal>OutboundRegistrationDetail</literal> events showing configuration and status
			information are raised for each outbound registration object. <literal>AuthDetail</literal>
			events are raised for each associated auth object as well.  Once all events are completed an
			<literal>OutboundRegistrationDetailComplete</literal> is issued.
                        </para>
		</description>
	</manager>
 ***/

/*! \brief Some thread local storage used to determine if the running thread invoked the callback */
AST_THREADSTORAGE(register_callback_invoked);

/*! \brief Amount of buffer time (in seconds) before expiration that we re-register at */
#define REREGISTER_BUFFER_TIME 10

/*! \brief Size of the buffer for creating a unique string for the line */
#define LINE_PARAMETER_SIZE 8

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
	/*! \brief Registration is stopping. */
	SIP_REGISTRATION_STOPPING,
	/*! \brief Registration has been stopped */
	SIP_REGISTRATION_STOPPED,
};

/*!
 * \internal
 * \brief Convert the internal registration state to an external status string.
 * \since 13.5.0
 *
 * \param state Current outbound registration state.
 *
 * \return External registration status string.
 */
static const char *sip_outbound_registration_status_str(enum sip_outbound_registration_status state)
{
	const char *str;

	str = "Unregistered";
	switch (state) {
	case SIP_REGISTRATION_STOPPING:
	case SIP_REGISTRATION_STOPPED:
	case SIP_REGISTRATION_UNREGISTERED:
		break;
	case SIP_REGISTRATION_REGISTERED:
		str = "Registered";
		break;
	case SIP_REGISTRATION_REJECTED_TEMPORARY:
	case SIP_REGISTRATION_REJECTED_PERMANENT:
		str = "Rejected";
		break;
	}
	return str;
}

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
		/*! \brief Endpoint to use for related incoming calls */
		AST_STRING_FIELD(endpoint);
	);
	/*! \brief Requested expiration time */
	unsigned int expiration;
	/*! \brief Interval at which retries should occur for temporal responses */
	unsigned int retry_interval;
	/*! \brief Interval at which retries should occur for permanent responses */
	unsigned int forbidden_retry_interval;
	/*! \brief Interval at which retries should occur for all permanent responses */
	unsigned int fatal_retry_interval;
	/*! \brief Treat authentication challenges that we cannot handle as permanent failures */
	unsigned int auth_rejection_permanent;
	/*! \brief Maximum number of retries permitted */
	unsigned int max_retries;
	/*! \brief Whether to add a line parameter to the outbound Contact or not */
	unsigned int line;
	/*! \brief Configured authentication credentials */
	struct ast_sip_auth_vector outbound_auths;
	/*! \brief Whether Path support is enabled */
	unsigned int support_path;
};

/*! \brief Outbound registration client state information (persists for lifetime of regc) */
struct sip_outbound_registration_client_state {
	/*! \brief Current state of this registration */
	enum sip_outbound_registration_status status;
	/*!
	 * \brief Outbound registration client
	 * \note May only be accessed within the serializer thread
	 * because it might get destroyed and set to NULL for
	 * module unload.
	 */
	pjsip_regc *client;
	/*!
	 * \brief Last tdata sent
	 * We need the original tdata to resend a request on auth failure
	 * or timeout.  On an auth failure, we use the original tdata
	 * to initialize the new tdata for the authorized response.  On a timeout
	 * we need it to skip failed SRV entries if any.
	 */
	pjsip_tx_data *last_tdata;
	/*! \brief Timer entry for retrying on temporal responses */
	pj_timer_entry timer;
	/*! \brief Optional line parameter placed into Contact */
	char line[LINE_PARAMETER_SIZE];
	/*! \brief Current number of retries */
	unsigned int retries;
	/*! \brief Maximum number of retries permitted */
	unsigned int max_retries;
	/*! \brief Interval at which retries should occur for temporal responses */
	unsigned int retry_interval;
	/*! \brief Interval at which retries should occur for permanent responses */
	unsigned int forbidden_retry_interval;
	/*! \brief Interval at which retries should occur for all permanent responses */
	unsigned int fatal_retry_interval;
	/*! \brief Treat authentication challenges that we cannot handle as permanent failures */
	unsigned int auth_rejection_permanent;
	/*! \brief Determines whether SIP Path support should be advertised */
	unsigned int support_path;
	/*! CSeq number of last sent auth request. */
	unsigned int auth_cseq;
	/*! \brief Serializer for stuff and things */
	struct ast_taskprocessor *serializer;
	/*! \brief Configured authentication credentials */
	struct ast_sip_auth_vector outbound_auths;
	/*! \brief Registration should be destroyed after completion of transaction */
	unsigned int destroy:1;
	/*! \brief Non-zero if we have attempted sending a REGISTER with authentication */
	unsigned int auth_attempted:1;
	/*! \brief The name of the transport to be used for the registration */
	char *transport_name;
	/*! \brief The name of the registration sorcery object */
	char *registration_name;
};

/*! \brief Outbound registration state information (persists for lifetime that registration should exist) */
struct sip_outbound_registration_state {
	/*! \brief Outbound registration configuration object */
	struct sip_outbound_registration *registration;
	/*! \brief Client state information */
	struct sip_outbound_registration_client_state *client_state;
};

/*! Time needs to be long enough for a transaction to timeout if nothing replies. */
#define MAX_UNLOAD_TIMEOUT_TIME		35	/* Seconds */

/*! Shutdown group to monitor sip_outbound_registration_client_state serializers. */
static struct ast_serializer_shutdown_group *shutdown_group;

/*! \brief Default number of state container buckets */
#define DEFAULT_STATE_BUCKETS 53
static AO2_GLOBAL_OBJ_STATIC(current_states);

/*! subscription id for network change events */
static struct stasis_subscription *network_change_sub;

/*! \brief hashing function for state objects */
static int registration_state_hash(const void *obj, const int flags)
{
	const struct sip_outbound_registration_state *object;
	const char *key;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_KEY:
		key = obj;
		break;
	case OBJ_SEARCH_OBJECT:
		object = obj;
		key = ast_sorcery_object_get_id(object->registration);
		break;
	default:
		ast_assert(0);
		return 0;
	}
	return ast_str_hash(key);
}

/*! \brief comparator function for state objects */
static int registration_state_cmp(void *obj, void *arg, int flags)
{
	const struct sip_outbound_registration_state *object_left = obj;
	const struct sip_outbound_registration_state *object_right = arg;
	const char *right_key = arg;
	int cmp;

	switch (flags & OBJ_SEARCH_MASK) {
	case OBJ_SEARCH_OBJECT:
		right_key = ast_sorcery_object_get_id(object_right->registration);
		/* Fall through */
	case OBJ_SEARCH_KEY:
		cmp = strcmp(ast_sorcery_object_get_id(object_left->registration), right_key);
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

static struct sip_outbound_registration_state *get_state(const char *id)
{
	struct sip_outbound_registration_state *state = NULL;
	struct ao2_container *states;

	states = ao2_global_obj_ref(current_states);
	if (states) {
		state = ao2_find(states, id, OBJ_SEARCH_KEY);
		ao2_ref(states, -1);
	}
	return state;
}

static struct ao2_container *get_registrations(void)
{
	struct ao2_container *registrations = ast_sorcery_retrieve_by_fields(
		ast_sip_get_sorcery(), "registration",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);

	return registrations;
}

/*! \brief Callback function for matching an outbound registration based on line */
static int line_identify_relationship(void *obj, void *arg, int flags)
{
	struct sip_outbound_registration_state *state = obj;
	pjsip_param *line = arg;

	return !pj_strcmp2(&line->value, state->client_state->line) ? CMP_MATCH : 0;
}

static struct pjsip_param *get_uri_option_line(const void *uri)
{
	pjsip_sip_uri *pjuri;
	static const pj_str_t LINE_STR = { "line", 4 };

	if (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri)) {
		return NULL;
	}
	pjuri = pjsip_uri_get_uri(uri);
	return pjsip_param_find(&pjuri->other_param, &LINE_STR);
}

/*! \brief Endpoint identifier which uses the 'line' parameter to establish a relationship to an outgoing registration */
static struct ast_sip_endpoint *line_identify(pjsip_rx_data *rdata)
{
	pjsip_param *line;
	RAII_VAR(struct ao2_container *, states, NULL, ao2_cleanup);
	RAII_VAR(struct sip_outbound_registration_state *, state, NULL, ao2_cleanup);

	if (!(line = get_uri_option_line(rdata->msg_info.to->uri))
		&& !(line = get_uri_option_line(rdata->msg_info.msg->line.req.uri))) {
		return NULL;
	}

	states = ao2_global_obj_ref(current_states);
	if (!states) {
		return NULL;
	}

	state = ao2_callback(states, 0, line_identify_relationship, line);
	if (!state || ast_strlen_zero(state->registration->endpoint)) {
		return NULL;
	}

	ast_debug(3, "Determined relationship to outbound registration '%s' based on line '%s', using configured endpoint '%s'\n",
		ast_sorcery_object_get_id(state->registration), state->client_state->line, state->registration->endpoint);

	return ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", state->registration->endpoint);
}

static struct ast_sip_endpoint_identifier line_identifier = {
	.identify_endpoint = line_identify,
};

/*! \brief Helper function which cancels the timer on a client */
static void cancel_registration(struct sip_outbound_registration_client_state *client_state)
{
	if (pj_timer_heap_cancel_if_active(pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()),
		&client_state->timer, client_state->timer.id)) {
		/* The timer was successfully cancelled, drop the refcount of client_state */
		ao2_ref(client_state, -1);
	}
}

static pj_str_t PATH_NAME = { "path", 4 };

/*! \brief Helper function which sends a message and cleans up, if needed, on failure */
static pj_status_t registration_client_send(struct sip_outbound_registration_client_state *client_state,
	pjsip_tx_data *tdata)
{
	pj_status_t status;
	int *callback_invoked;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };

	callback_invoked = ast_threadstorage_get(&register_callback_invoked, sizeof(int));
	if (!callback_invoked) {
		pjsip_tx_data_dec_ref(tdata);
		return PJ_ENOMEM;
	}
	*callback_invoked = 0;

	/* Due to the message going out the callback may now be invoked, so bump the count */
	ao2_ref(client_state, +1);
	/*
	 * We also bump tdata in expectation of saving it to client_state->last_tdata.
	 * We have to do it BEFORE pjsip_regc_send because if it succeeds, it decrements
	 * the ref count on its own.
	 */
	pjsip_tx_data_add_ref(tdata);

	/*
	 * Set the transport in case transports were reloaded.
	 * When pjproject removes the extraneous error messages produced,
	 * we can check status and only set the transport and resend if there was an error
	 */
	ast_sip_set_tpselector_from_transport_name(client_state->transport_name, &selector);
	pjsip_regc_set_transport(client_state->client, &selector);
	status = pjsip_regc_send(client_state->client, tdata);

	/*
	 * If the attempt to send the message failed and the callback was not invoked we need to
	 * drop the references we just added
	 */
	if ((status != PJ_SUCCESS) && !(*callback_invoked)) {
		pjsip_tx_data_dec_ref(tdata);
		ao2_ref(client_state, -1);
		return status;
	}

	/*
	 * Decref the old last_data before replacing it.
	 * BTW, it's quite possible that last_data == tdata
	 * if we're trying successive servers in an SRV set.
	 */
	if (client_state->last_tdata) {
		pjsip_tx_data_dec_ref(client_state->last_tdata);
	}
	client_state->last_tdata = tdata;

	return status;
}

/*! \brief Callback function for registering */
static int handle_client_registration(void *data)
{
	RAII_VAR(struct sip_outbound_registration_client_state *, client_state, data, ao2_cleanup);
	pjsip_tx_data *tdata;

	if (client_state->status == SIP_REGISTRATION_STOPPED
		|| pjsip_regc_register(client_state->client, PJ_FALSE, &tdata) != PJ_SUCCESS) {
		return 0;
	}

	if (DEBUG_ATLEAST(1)) {
		pjsip_regc_info info;

		pjsip_regc_get_info(client_state->client, &info);
		ast_log(LOG_DEBUG, "Outbound REGISTER attempt %u to '%.*s' with client '%.*s'\n",
			client_state->retries + 1,
			(int) info.server_uri.slen, info.server_uri.ptr,
			(int) info.client_uri.slen, info.client_uri.ptr);
	}

	if (client_state->support_path) {
		pjsip_supported_hdr *hdr;

		hdr = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_SUPPORTED, NULL);
		if (!hdr) {
			/* insert a new Supported header */
			hdr = pjsip_supported_hdr_create(tdata->pool);
			if (!hdr) {
				pjsip_tx_data_dec_ref(tdata);
				return -1;
			}

			pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *)hdr);
		}

		/* add on to the existing Supported header */
		pj_strassign(&hdr->values[hdr->count++], &PATH_NAME);
	}

	registration_client_send(client_state, tdata);

	return 0;
}

/*! \brief Timer callback function, used just for registrations */
static void sip_outbound_registration_timer_cb(pj_timer_heap_t *timer_heap, struct pj_timer_entry *entry)
{
	struct sip_outbound_registration_client_state *client_state = entry->user_data;

	entry->id = 0;

	/*
	 * Transfer client_state reference to serializer task so the
	 * nominal path will not dec the client_state ref in this
	 * pjproject callback thread.
	 */
	if (ast_sip_push_task(client_state->serializer, handle_client_registration, client_state)) {
		ast_log(LOG_WARNING, "Scheduled outbound registration could not be executed.\n");
		ao2_ref(client_state, -1);
	}
}

/*! \brief Helper function which sets up the timer to re-register in a specific amount of time */
static void schedule_registration(struct sip_outbound_registration_client_state *client_state, unsigned int seconds)
{
	pj_time_val delay = { .sec = seconds, };
	pjsip_regc_info info;

	cancel_registration(client_state);

	pjsip_regc_get_info(client_state->client, &info);
	ast_debug(1, "Scheduling outbound registration to server '%.*s' from client '%.*s' in %d seconds\n",
			(int) info.server_uri.slen, info.server_uri.ptr,
			(int) info.client_uri.slen, info.client_uri.ptr,
			seconds);

	ao2_ref(client_state, +1);
	if (pjsip_endpt_schedule_timer(ast_sip_get_pjsip_endpoint(), &client_state->timer, &delay) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to schedule registration to server '%.*s' from client '%.*s'\n",
				(int) info.server_uri.slen, info.server_uri.ptr,
				(int) info.client_uri.slen, info.client_uri.ptr);
		ao2_ref(client_state, -1);
	}
}

static void update_client_state_status(struct sip_outbound_registration_client_state *client_state, enum sip_outbound_registration_status status)
{
	const char *status_old;
	const char *status_new;

	if (client_state->status == status) {
		/* Status state did not change at all. */
		return;
	}

	status_old = sip_outbound_registration_status_str(client_state->status);
	status_new = sip_outbound_registration_status_str(status);
	client_state->status = status;

	if (!strcmp(status_old, status_new)) {
		/*
		 * The internal status state may have changed but the status
		 * state we tell the world did not change at all.
		 */
		return;
	}

	ast_statsd_log_string_va("PJSIP.registrations.state.%s", AST_STATSD_GAUGE, "-1", 1.0,
		status_old);
	ast_statsd_log_string_va("PJSIP.registrations.state.%s", AST_STATSD_GAUGE, "+1", 1.0,
		status_new);
}

/*! \brief Callback function for unregistering (potentially) and destroying state */
static int handle_client_state_destruction(void *data)
{
	struct sip_outbound_registration_client_state *client_state = data;

	cancel_registration(client_state);

	if (client_state->client) {
		pjsip_regc_info info;
		pjsip_tx_data *tdata;

		pjsip_regc_get_info(client_state->client, &info);

		if (info.is_busy == PJ_TRUE) {
			/* If a client transaction is in progress we defer until it is complete */
			ast_debug(1,
				"Registration transaction is busy with server '%.*s' from client '%.*s'.\n",
				(int) info.server_uri.slen, info.server_uri.ptr,
				(int) info.client_uri.slen, info.client_uri.ptr);
			client_state->destroy = 1;
			ao2_ref(client_state, -1);
			return 0;
		}

		switch (client_state->status) {
		case SIP_REGISTRATION_UNREGISTERED:
			break;
		case SIP_REGISTRATION_REGISTERED:
			ast_debug(1,
				"Trying to unregister with server '%.*s' from client '%.*s' before destruction.\n",
				(int) info.server_uri.slen, info.server_uri.ptr,
				(int) info.client_uri.slen, info.client_uri.ptr);

			update_client_state_status(client_state, SIP_REGISTRATION_STOPPING);
			client_state->destroy = 1;
			if (pjsip_regc_unregister(client_state->client, &tdata) == PJ_SUCCESS
				&& registration_client_send(client_state, tdata) == PJ_SUCCESS) {
				ao2_ref(client_state, -1);
				return 0;
			}
			break;
		case SIP_REGISTRATION_REJECTED_TEMPORARY:
		case SIP_REGISTRATION_REJECTED_PERMANENT:
		case SIP_REGISTRATION_STOPPING:
		case SIP_REGISTRATION_STOPPED:
			break;
		}

		pjsip_regc_destroy(client_state->client);
		client_state->client = NULL;
	}

	update_client_state_status(client_state, SIP_REGISTRATION_STOPPED);
	ast_sip_auth_vector_destroy(&client_state->outbound_auths);
	ao2_ref(client_state, -1);

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
	/*! \brief Request for which the response was received */
	pjsip_tx_data *old_request;
};

/*! \brief Registration response structure destructor */
static void registration_response_destroy(void *obj)
{
	struct registration_response *response = obj;

	if (response->rdata) {
		pjsip_rx_data_free_cloned(response->rdata);
	}

	if (response->old_request) {
		pjsip_tx_data_dec_ref(response->old_request);
	}

	ao2_cleanup(response->client_state);
}

/*! \brief Helper function which determines if a response code is temporal or not */
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

static void schedule_retry(struct registration_response *response, unsigned int interval,
			   const char *server_uri, const char *client_uri)
{
	update_client_state_status(response->client_state, SIP_REGISTRATION_REJECTED_TEMPORARY);
	schedule_registration(response->client_state, interval);

	if (response->rdata) {
		ast_log(LOG_WARNING, "Temporal response '%d' received from '%s' on "
			"registration attempt to '%s', retrying in '%u'\n",
			response->code, server_uri, client_uri, interval);
	} else {
		ast_log(LOG_WARNING, "No response received from '%s' on "
			"registration attempt to '%s', retrying in '%u'\n",
			server_uri, client_uri, interval);
	}
}

static int reregister_immediately_cb(void *obj)
{
	struct sip_outbound_registration_state *state = obj;

	if (state->client_state->status != SIP_REGISTRATION_REGISTERED) {
		ao2_ref(state, -1);
		return 0;
	}

	if (DEBUG_ATLEAST(1)) {
		pjsip_regc_info info;

		pjsip_regc_get_info(state->client_state->client, &info);
		ast_log(LOG_DEBUG,
			"Outbound registration transport to server '%.*s' from client '%.*s' shutdown\n",
			(int) info.server_uri.slen, info.server_uri.ptr,
			(int) info.client_uri.slen, info.client_uri.ptr);
	}

	cancel_registration(state->client_state);

	ao2_ref(state->client_state, +1);
	handle_client_registration(state->client_state);

	ao2_ref(state, -1);
	return 0;
}

/*!
 * \internal
 * \brief The reliable transport we registered using has shutdown.
 * \since 13.18.0
 *
 * \param obj What is needed to initiate a reregister attempt.
 *
 * \note Normally executed by the pjsip monitor thread.
 *
 * \return Nothing
 */
static void registration_transport_shutdown_cb(void *obj)
{
	const char *registration_name = obj;
	struct sip_outbound_registration_state *state;

	state = get_state(registration_name);
	if (!state) {
		/* Registration no longer exists or shutting down. */
		return;
	}
	if (ast_sip_push_task(state->client_state->serializer, reregister_immediately_cb, state)) {
		ao2_ref(state, -1);
	}
}

static int monitor_matcher(void *a, void *b)
{
	char *ma = a;
	char *mb = b;

	return strcmp(ma, mb) == 0;
}

static void registration_transport_monitor_setup(pjsip_transport *transport, const char *registration_name)
{
	char *monitor;

	if (!PJSIP_TRANSPORT_IS_RELIABLE(transport)) {
		return;
	}
	monitor = ao2_alloc_options(strlen(registration_name) + 1, NULL,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!monitor) {
		return;
	}
	strcpy(monitor, registration_name);/* Safe */

	/*
	 * We'll ignore if the transport has already been shutdown before we
	 * register the monitor.  We might get into a message spamming infinite
	 * loop of registration, shutdown, reregistration...
	 */
	ast_sip_transport_monitor_register(transport, registration_transport_shutdown_cb,
		monitor);
	ao2_ref(monitor, -1);
}

/*! \brief Callback function for handling a response to a registration attempt */
static int handle_registration_response(void *data)
{
	struct registration_response *response = data;
	pjsip_regc_info info;
	char server_uri[PJSIP_MAX_URL_SIZE];
	char client_uri[PJSIP_MAX_URL_SIZE];

	if (response->client_state->status == SIP_REGISTRATION_STOPPED) {
		ao2_ref(response, -1);
		return 0;
	}

	pjsip_regc_get_info(response->client_state->client, &info);
	ast_copy_pj_str(server_uri, &info.server_uri, sizeof(server_uri));
	ast_copy_pj_str(client_uri, &info.client_uri, sizeof(client_uri));

	ast_debug(1, "Processing REGISTER response %d from server '%s' for client '%s'\n",
			response->code, server_uri, client_uri);

	if (response->code == 408 || response->code == 503) {
		if ((ast_sip_failover_request(response->old_request))) {
			int res = registration_client_send(response->client_state, response->old_request);
			/* The tdata ref was stolen */
			response->old_request = NULL;
			if (res == PJ_SUCCESS) {
				ao2_ref(response, -1);
				return 0;
			}
		}
	} else if ((response->code == 401 || response->code == 407)
		&& (!response->client_state->auth_attempted
			|| response->rdata->msg_info.cseq->cseq != response->client_state->auth_cseq)) {
		int res;
		pjsip_cseq_hdr *cseq_hdr;
		pjsip_tx_data *tdata;

		if (!ast_sip_create_request_with_auth(&response->client_state->outbound_auths,
				response->rdata, response->old_request, &tdata)) {
			response->client_state->auth_attempted = 1;
			ast_debug(1, "Sending authenticated REGISTER to server '%s' from client '%s'\n",
					server_uri, client_uri);
			pjsip_tx_data_add_ref(tdata);
			res = registration_client_send(response->client_state, tdata);

			/* Save the cseq that actually got sent. */
			cseq_hdr = (pjsip_cseq_hdr *) pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ,
				NULL);
			response->client_state->auth_cseq = cseq_hdr->cseq;
			pjsip_tx_data_dec_ref(tdata);
			if (res == PJ_SUCCESS) {
				ao2_ref(response, -1);
				return 0;
			}
		} else {
			ast_log(LOG_WARNING, "Failed to create authenticated REGISTER request to server '%s' from client '%s'\n",
					server_uri, client_uri);
		}
		/* Otherwise, fall through so the failure is processed appropriately */
	}

	response->client_state->auth_attempted = 0;

	if (PJSIP_IS_STATUS_IN_CLASS(response->code, 200)) {
		/* Check if this is in regards to registering or unregistering */
		if (response->expiration) {
			int next_registration_round;

			/* If the registration went fine simply reschedule registration for the future */
			ast_debug(1, "Outbound registration to '%s' with client '%s' successful\n", server_uri, client_uri);
			update_client_state_status(response->client_state, SIP_REGISTRATION_REGISTERED);
			response->client_state->retries = 0;
			next_registration_round = response->expiration - REREGISTER_BUFFER_TIME;
			if (next_registration_round < 0) {
				/* Re-register immediately. */
				next_registration_round = 0;
			}
			schedule_registration(response->client_state, next_registration_round);

			/* See if we should monitor for transport shutdown */
			registration_transport_monitor_setup(response->rdata->tp_info.transport,
				response->client_state->registration_name);
		} else {
			ast_debug(1, "Outbound unregistration to '%s' with client '%s' successful\n", server_uri, client_uri);
			update_client_state_status(response->client_state, SIP_REGISTRATION_UNREGISTERED);
			ast_sip_transport_monitor_unregister(response->rdata->tp_info.transport,
				registration_transport_shutdown_cb, response->client_state->registration_name,
				monitor_matcher);
		}
	} else if (response->client_state->destroy) {
		/* We need to deal with the pending destruction instead. */
	} else if (response->retry_after) {
		/* If we have been instructed to retry after a period of time, schedule it as such */
		schedule_retry(response, response->retry_after, server_uri, client_uri);
	} else if (response->client_state->retry_interval
		&& sip_outbound_registration_is_temporal(response->code, response->client_state)) {
		if (response->client_state->retries == response->client_state->max_retries) {
			/* If we received enough temporal responses to exceed our maximum give up permanently */
			update_client_state_status(response->client_state, SIP_REGISTRATION_REJECTED_PERMANENT);
			ast_log(LOG_WARNING, "Maximum retries reached when attempting outbound registration to '%s' with client '%s', stopping registration attempt\n",
				server_uri, client_uri);
		} else {
			/* On the other hand if we can still try some more do so */
			response->client_state->retries++;
			schedule_retry(response, response->client_state->retry_interval, server_uri, client_uri);
		}
	} else {
		if (response->code == 403
			&& response->client_state->forbidden_retry_interval
			&& response->client_state->retries < response->client_state->max_retries) {
			/* A forbidden response retry interval is configured and there are retries remaining */
			update_client_state_status(response->client_state, SIP_REGISTRATION_REJECTED_TEMPORARY);
			response->client_state->retries++;
			schedule_registration(response->client_state, response->client_state->forbidden_retry_interval);
			ast_log(LOG_WARNING, "403 Forbidden fatal response received from '%s' on registration attempt to '%s', retrying in '%u' seconds\n",
				server_uri, client_uri, response->client_state->forbidden_retry_interval);
		} else if (response->client_state->fatal_retry_interval
			   && response->client_state->retries < response->client_state->max_retries) {
			/* Some kind of fatal failure response received, so retry according to configured interval */
			update_client_state_status(response->client_state, SIP_REGISTRATION_REJECTED_TEMPORARY);
			response->client_state->retries++;
			schedule_registration(response->client_state, response->client_state->fatal_retry_interval);
			ast_log(LOG_WARNING, "'%d' fatal response received from '%s' on registration attempt to '%s', retrying in '%u' seconds\n",
				response->code, server_uri, client_uri, response->client_state->fatal_retry_interval);
		} else {
			/* Finally if there's no hope of registering give up */
			update_client_state_status(response->client_state, SIP_REGISTRATION_REJECTED_PERMANENT);
			if (response->rdata) {
				ast_log(LOG_WARNING, "Fatal response '%d' received from '%s' on registration attempt to '%s', stopping outbound registration\n",
					response->code, server_uri, client_uri);
			} else {
				ast_log(LOG_WARNING, "Fatal registration attempt to '%s', stopping outbound registration\n", client_uri);
			}
		}
	}

	ast_system_publish_registry("PJSIP", client_uri, server_uri,
		sip_outbound_registration_status_str(response->client_state->status), NULL);

	if (response->client_state->destroy) {
		/* We have a pending deferred destruction to complete now. */
		ao2_ref(response->client_state, +1);
		handle_client_state_destruction(response->client_state);
	}

	ao2_ref(response, -1);
	return 0;
}

/*! \brief Callback function for outbound registration client */
static void sip_outbound_registration_response_cb(struct pjsip_regc_cbparam *param)
{
	struct sip_outbound_registration_client_state *client_state = param->token;
	struct registration_response *response;
	int *callback_invoked;

	callback_invoked = ast_threadstorage_get(&register_callback_invoked, sizeof(int));

	ast_assert(callback_invoked != NULL);
	ast_assert(client_state != NULL);

	*callback_invoked = 1;

	response = ao2_alloc(sizeof(*response), registration_response_destroy);
	if (!response) {
		ao2_ref(client_state, -1);
		return;
	}
	response->code = param->code;
	response->expiration = param->expiration;
	/*
	 * Transfer client_state reference to response so the
	 * nominal path will not dec the client_state ref in this
	 * pjproject callback thread.
	 */
	response->client_state = client_state;

	ast_debug(1, "Received REGISTER response %d(%.*s)\n",
		param->code, (int) param->reason.slen, param->reason.ptr);

	if (param->rdata) {
		struct pjsip_retry_after_hdr *retry_after;
		pjsip_transaction *tsx;

		retry_after = pjsip_msg_find_hdr(param->rdata->msg_info.msg, PJSIP_H_RETRY_AFTER,
			NULL);
		response->retry_after = retry_after ? retry_after->ivalue : 0;

		/*
		 * If we got a response from the server, we have to use the tdata
		 * from the transaction, not the tdata saved when we sent the
		 * request.  If we use the saved tdata, we won't process responses
		 * like 423 Interval Too Brief correctly and we'll wind up sending
		 * the bad Expires value again.
		 */
		pjsip_tx_data_dec_ref(client_state->last_tdata);

		tsx = pjsip_rdata_get_tsx(param->rdata);
		response->old_request = tsx->last_tx;
		pjsip_tx_data_add_ref(response->old_request);
		pjsip_rx_data_clone(param->rdata, 0, &response->rdata);
	} else {
		/* old_request steals the reference */
		response->old_request = client_state->last_tdata;
	}
	client_state->last_tdata = NULL;

	/*
	 * Transfer response reference to serializer task so the
	 * nominal path will not dec the response ref in this
	 * pjproject callback thread.
	 */
	if (ast_sip_push_task(client_state->serializer, handle_registration_response, response)) {
		ast_log(LOG_WARNING, "Failed to pass incoming registration response to threadpool\n");
		ao2_cleanup(response);
	}
}

/*! \brief Destructor function for registration state */
static void sip_outbound_registration_state_destroy(void *obj)
{
	struct sip_outbound_registration_state *state = obj;

	ast_debug(3, "Destroying registration state for registration to server '%s' from client '%s'\n",
		state->registration ? state->registration->server_uri : "",
		state->registration ? state->registration->client_uri : "");
	ao2_cleanup(state->registration);

	if (!state->client_state) {
		/* Nothing to do */
	} else if (!state->client_state->serializer) {
		ao2_ref(state->client_state, -1);
	} else if (ast_sip_push_task(state->client_state->serializer,
		handle_client_state_destruction, state->client_state)) {
		ast_log(LOG_WARNING, "Failed to pass outbound registration client destruction to threadpool\n");
		ao2_ref(state->client_state, -1);
	}
}

/*! \brief Destructor function for client registration state */
static void sip_outbound_registration_client_state_destroy(void *obj)
{
	struct sip_outbound_registration_client_state *client_state = obj;

	ast_statsd_log_string("PJSIP.registrations.count", AST_STATSD_GAUGE, "-1", 1.0);
	ast_statsd_log_string_va("PJSIP.registrations.state.%s", AST_STATSD_GAUGE, "-1", 1.0,
		sip_outbound_registration_status_str(client_state->status));

	ast_taskprocessor_unreference(client_state->serializer);
	ast_free(client_state->transport_name);
	ast_free(client_state->registration_name);
	if (client_state->last_tdata) {
		pjsip_tx_data_dec_ref(client_state->last_tdata);
	}
}

/*! \brief Allocator function for registration state */
static struct sip_outbound_registration_state *sip_outbound_registration_state_alloc(struct sip_outbound_registration *registration)
{
	struct sip_outbound_registration_state *state;
	char tps_name[AST_TASKPROCESSOR_MAX_NAME + 1];

	state = ao2_alloc(sizeof(*state), sip_outbound_registration_state_destroy);
	if (!state) {
		return NULL;
	}
	state->client_state = ao2_alloc(sizeof(*state->client_state),
		sip_outbound_registration_client_state_destroy);
	if (!state->client_state) {
		ao2_cleanup(state);
		return NULL;
	}

	state->client_state->status = SIP_REGISTRATION_UNREGISTERED;
	pj_timer_entry_init(&state->client_state->timer, 0, state->client_state,
		sip_outbound_registration_timer_cb);
	state->client_state->transport_name = ast_strdup(registration->transport);
	state->client_state->registration_name =
		ast_strdup(ast_sorcery_object_get_id(registration));

	ast_statsd_log_string("PJSIP.registrations.count", AST_STATSD_GAUGE, "+1", 1.0);
	ast_statsd_log_string_va("PJSIP.registrations.state.%s", AST_STATSD_GAUGE, "+1", 1.0,
		sip_outbound_registration_status_str(state->client_state->status));

	if (!state->client_state->transport_name
		|| !state->client_state->registration_name) {
		ao2_cleanup(state);
		return NULL;
	}

	/* Create name with seq number appended. */
	ast_taskprocessor_build_name(tps_name, sizeof(tps_name), "pjsip/outreg/%s",
		ast_sorcery_object_get_id(registration));

	state->client_state->serializer = ast_sip_create_serializer_group(tps_name,
		shutdown_group);
	if (!state->client_state->serializer) {
		ao2_cleanup(state);
		return NULL;
	}

	state->registration = ao2_bump(registration);
	return state;
}

/*! \brief Destructor function for registration information */
static void sip_outbound_registration_destroy(void *obj)
{
	struct sip_outbound_registration *registration = obj;

	ast_sip_auth_vector_destroy(&registration->outbound_auths);

	ast_string_field_free_memory(registration);
}

/*! \brief Allocator function for registration information */
static void *sip_outbound_registration_alloc(const char *name)
{
	struct sip_outbound_registration *registration;

	registration = ast_sorcery_generic_alloc(sizeof(*registration),
		sip_outbound_registration_destroy);
	if (!registration || ast_string_field_init(registration, 256)) {
		ao2_cleanup(registration);
		return NULL;
	}

	return registration;
}

/*! \brief Helper function which populates a pj_str_t with a contact header */
static int sip_dialog_create_contact(pj_pool_t *pool, pj_str_t *contact, const char *user,
	const pj_str_t *target, pjsip_tpselector *selector, const char *line)
{
	pj_str_t tmp, local_addr;
	pjsip_uri *uri;
	pjsip_sip_uri *sip_uri;
	pjsip_transport_type_e type;
	int local_port;

	pj_strdup_with_null(pool, &tmp, target);

	if (!(uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0)) ||
	    (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		return -1;
	}

	sip_uri = pjsip_uri_get_uri(uri);

	type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
	if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri)) {
		if (type == PJSIP_TRANSPORT_UNSPECIFIED
			|| !(pjsip_transport_get_flag_from_type(type) & PJSIP_TRANSPORT_SECURE)) {
			type = PJSIP_TRANSPORT_TLS;
		}
	} else if (!sip_uri->transport_param.slen) {
		type = PJSIP_TRANSPORT_UDP;
	} else if (type == PJSIP_TRANSPORT_UNSPECIFIED) {
		return -1;
	}

	if (pj_strchr(&sip_uri->host, ':')) {
		type |= PJSIP_TRANSPORT_IPV6;
	}

	if (pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()),
		pool, type, selector, &local_addr, &local_port) != PJ_SUCCESS) {
		return -1;
	}

	if (!pj_strchr(&sip_uri->host, ':') && pj_strchr(&local_addr, ':')) {
		type |= PJSIP_TRANSPORT_IPV6;
	}

	contact->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
	contact->slen = pj_ansi_snprintf(contact->ptr, PJSIP_MAX_URL_SIZE,
		"<%s:%s@%s%.*s%s:%d%s%s%s%s>",
		((pjsip_transport_get_flag_from_type(type) & PJSIP_TRANSPORT_SECURE) && PJSIP_URI_SCHEME_IS_SIPS(uri)) ? "sips" : "sip",
		user,
		(type & PJSIP_TRANSPORT_IPV6) ? "[" : "",
		(int)local_addr.slen,
		local_addr.ptr,
		(type & PJSIP_TRANSPORT_IPV6) ? "]" : "",
		local_port,
		(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
		(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "",
		!ast_strlen_zero(line) ? ";line=" : "",
		S_OR(line, ""));

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
static int can_reuse_registration(struct sip_outbound_registration *existing,
	struct sip_outbound_registration *applied)
{
	int rc = 1;
	struct ast_sorcery *sorcery = ast_sip_get_sorcery();
	struct ast_variable *ve = ast_sorcery_objectset_create(sorcery, existing);
	struct ast_variable *va = ast_sorcery_objectset_create(sorcery, applied);
	struct ast_variable *vc = NULL;

	if (ast_sorcery_changeset_create(ve, va, &vc) || vc != NULL) {
		rc = 0;
		ast_debug(4, "Registration '%s' changed.  Can't re-use.\n", ast_sorcery_object_get_id(existing));
	} else {
		ast_debug(4, "Registration '%s' didn't change.  Can re-use\n", ast_sorcery_object_get_id(existing));
	}

	ast_variables_destroy(ve);
	ast_variables_destroy(va);
	ast_variables_destroy(vc);

	return rc;
}

/*! \brief Helper function that allocates a pjsip registration client and configures it */
static int sip_outbound_registration_regc_alloc(void *data)
{
	struct sip_outbound_registration_state *state = data;
	RAII_VAR(struct sip_outbound_registration *, registration,
		 ao2_bump(state->registration), ao2_cleanup);
	pj_pool_t *pool;
	pj_str_t tmp;
	pjsip_uri *uri;
	pj_str_t server_uri, client_uri, contact_uri;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "URI Validation", 256, 256);
	if (!pool) {
		ast_log(LOG_ERROR, "Could not create pool for URI validation on outbound registration '%s'\n",
			ast_sorcery_object_get_id(registration));
		return -1;
	}

	pj_strdup2_with_null(pool, &tmp, registration->server_uri);
	uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0);
	if (!uri) {
		ast_log(LOG_ERROR, "Invalid server URI '%s' specified on outbound registration '%s'\n",
			registration->server_uri, ast_sorcery_object_get_id(registration));
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	pj_strdup2_with_null(pool, &tmp, registration->client_uri);
	uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0);
	if (!uri) {
		ast_log(LOG_ERROR, "Invalid client URI '%s' specified on outbound registration '%s'\n",
			registration->client_uri, ast_sorcery_object_get_id(registration));
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	if (!ast_strlen_zero(registration->outbound_proxy)) {
		pj_strdup2_with_null(pool, &tmp, registration->outbound_proxy);
		uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0);
		if (!uri) {
			ast_log(LOG_ERROR, "Invalid outbound proxy URI '%s' specified on outbound registration '%s'\n",
				registration->outbound_proxy, ast_sorcery_object_get_id(registration));
			pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
			return -1;
		}
	}

	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);


	ast_assert(state->client_state->client == NULL);
	if (pjsip_regc_create(ast_sip_get_pjsip_endpoint(), state->client_state,
			sip_outbound_registration_response_cb,
			&state->client_state->client) != PJ_SUCCESS) {
		return -1;
	}

	ast_sip_set_tpselector_from_transport_name(registration->transport, &selector);
	pjsip_regc_set_transport(state->client_state->client, &selector);

	if (!ast_strlen_zero(registration->outbound_proxy)) {
		pjsip_route_hdr route_set, *route;
		static const pj_str_t ROUTE_HNAME = { "Route", 5 };
		pj_str_t tmp;

		pj_list_init(&route_set);

		pj_strdup2_with_null(pjsip_regc_get_pool(state->client_state->client), &tmp,
			registration->outbound_proxy);
		route = pjsip_parse_hdr(pjsip_regc_get_pool(state->client_state->client),
			&ROUTE_HNAME, tmp.ptr, tmp.slen, NULL);
		if (!route) {
			return -1;
		}
		pj_list_insert_nodes_before(&route_set, route);

		pjsip_regc_set_route_set(state->client_state->client, &route_set);
	}

	if (state->registration->line) {
		ast_generate_random_string(state->client_state->line, sizeof(state->client_state->line));
	}

	pj_cstr(&server_uri, registration->server_uri);


	if (sip_dialog_create_contact(pjsip_regc_get_pool(state->client_state->client),
		&contact_uri, S_OR(registration->contact_user, "s"), &server_uri, &selector,
		state->client_state->line)) {
		return -1;
	}

	pj_cstr(&client_uri, registration->client_uri);
	if (pjsip_regc_init(state->client_state->client, &server_uri, &client_uri,
		&client_uri, 1, &contact_uri, registration->expiration) != PJ_SUCCESS) {
		return -1;
	}

	return 0;
}

/*! \brief Helper function which performs a single registration */
static int sip_outbound_registration_perform(void *data)
{
	struct sip_outbound_registration_state *state = data;
	struct sip_outbound_registration *registration = ao2_bump(state->registration);
	size_t i;

	/* Just in case the client state is being reused for this registration, free the auth information */
	ast_sip_auth_vector_destroy(&state->client_state->outbound_auths);

	AST_VECTOR_INIT(&state->client_state->outbound_auths, AST_VECTOR_SIZE(&registration->outbound_auths));
	for (i = 0; i < AST_VECTOR_SIZE(&registration->outbound_auths); ++i) {
		char *name = ast_strdup(AST_VECTOR_GET(&registration->outbound_auths, i));

		if (name && AST_VECTOR_APPEND(&state->client_state->outbound_auths, name)) {
			ast_free(name);
		}
	}
	state->client_state->retry_interval = registration->retry_interval;
	state->client_state->forbidden_retry_interval = registration->forbidden_retry_interval;
	state->client_state->fatal_retry_interval = registration->fatal_retry_interval;
	state->client_state->max_retries = registration->max_retries;
	state->client_state->retries = 0;
	state->client_state->support_path = registration->support_path;
	state->client_state->auth_rejection_permanent = registration->auth_rejection_permanent;

	pjsip_regc_update_expires(state->client_state->client, registration->expiration);

	schedule_registration(state->client_state, (ast_random() % 10) + 1);

	ao2_ref(registration, -1);
	ao2_ref(state, -1);
	return 0;
}

/*! \brief Apply function which finds or allocates a state structure */
static int sip_outbound_registration_apply(const struct ast_sorcery *sorcery, void *obj)
{
	RAII_VAR(struct ao2_container *, states, ao2_global_obj_ref(current_states), ao2_cleanup);
	RAII_VAR(struct sip_outbound_registration_state *, state, NULL, ao2_cleanup);
	RAII_VAR(struct sip_outbound_registration_state *, new_state, NULL, ao2_cleanup);
	struct sip_outbound_registration *applied = obj;

	if (!states) {
		/* Global container has gone.  Likely shutting down. */
		return -1;
	}
	state = ao2_find(states, ast_sorcery_object_get_id(applied), OBJ_SEARCH_KEY);

	ast_debug(4, "Applying configuration to outbound registration '%s'\n", ast_sorcery_object_get_id(applied));

	if (ast_strlen_zero(applied->server_uri)) {
		ast_log(LOG_ERROR, "No server URI specified on outbound registration '%s'\n",
			ast_sorcery_object_get_id(applied));
		return -1;
	} else if (ast_sip_validate_uri_length(applied->server_uri)) {
			ast_log(LOG_ERROR, "Server URI or hostname length exceeds pjproject limit or is not a sip(s) uri: '%s'\n",
				ast_sorcery_object_get_id(applied));
			return -1;
	} else if (ast_strlen_zero(applied->client_uri)) {
		ast_log(LOG_ERROR, "No client URI specified on outbound registration '%s'\n",
			ast_sorcery_object_get_id(applied));
		return -1;
	} else if (ast_sip_validate_uri_length(applied->client_uri)) {
			ast_log(LOG_ERROR, "Client URI or hostname length exceeds pjproject limit or is not a sip(s) uri: '%s'\n",
				ast_sorcery_object_get_id(applied));
			return -1;
	} else if (applied->line && ast_strlen_zero(applied->endpoint)) {
		ast_log(LOG_ERROR, "Line support has been enabled on outbound registration '%s' without providing an endpoint\n",
			ast_sorcery_object_get_id(applied));
		return -1;
	} else if (!ast_strlen_zero(applied->endpoint) && !applied->line) {
		ast_log(LOG_ERROR, "An endpoint has been specified on outbound registration '%s' without enabling line support\n",
			ast_sorcery_object_get_id(applied));
		return -1;
	}

	if (state && can_reuse_registration(state->registration, applied)) {
		ast_debug(4,
			"No change between old configuration and new configuration on outbound registration '%s'. Using previous state\n",
			ast_sorcery_object_get_id(applied));

		/*
		 * This is OK to replace without relinking the state in the
		 * current_states container since state->registration and
		 * applied have the same key.
		 */
		ao2_lock(states);
		ao2_replace(state->registration, applied);
		ao2_unlock(states);
		return 0;
	}

	if (!(new_state = sip_outbound_registration_state_alloc(applied))) {
		return -1;
	}

	if (ast_sip_push_task_wait_serializer(new_state->client_state->serializer,
		sip_outbound_registration_regc_alloc, new_state)) {
		return -1;
	}

	if (ast_sip_push_task(new_state->client_state->serializer,
			      sip_outbound_registration_perform, ao2_bump(new_state))) {
		ast_log(LOG_ERROR, "Failed to perform outbound registration on '%s'\n",
			ast_sorcery_object_get_id(new_state->registration));
		ao2_ref(new_state, -1);
		return -1;
	}

	ao2_lock(states);
	if (state) {
		ao2_unlink(states, state);
	}
	ao2_link(states, new_state);
	ao2_unlock(states);

	return 0;
}

static int outbound_auth_handler(const struct aco_option *opt, struct ast_variable *var, void *obj)
{
	struct sip_outbound_registration *registration = obj;

	return ast_sip_auth_vector_init(&registration->outbound_auths, var->value);
}

static int outbound_auths_to_str(const void *obj, const intptr_t *args, char **buf)
{
	const struct sip_outbound_registration *registration = obj;

	return ast_sip_auths_to_str(&registration->outbound_auths, buf);
}

static int outbound_auths_to_var_list(const void *obj, struct ast_variable **fields)
{
	const struct sip_outbound_registration *registration = obj;
	int i;
	struct ast_variable *head = NULL;

	for (i = 0; i < AST_VECTOR_SIZE(&registration->outbound_auths) ; i++) {
		ast_variable_list_append(&head, ast_variable_new("outbound_auth",
			AST_VECTOR_GET(&registration->outbound_auths, i), ""));
	}

	if (head) {
		*fields = head;
	}

	return 0;
}

static int unregister_task(void *obj)
{
	struct sip_outbound_registration_state *state = obj;
	struct pjsip_regc *client = state->client_state->client;
	pjsip_tx_data *tdata;
	pjsip_regc_info info;

	pjsip_regc_get_info(client, &info);
	ast_debug(1, "Unregistering contacts with server '%s' from client '%s'\n",
		state->registration->server_uri, state->registration->client_uri);

	cancel_registration(state->client_state);

	if (pjsip_regc_unregister(client, &tdata) == PJ_SUCCESS) {
		registration_client_send(state->client_state, tdata);
	}

	ao2_ref(state, -1);
	return 0;
}

static int queue_unregister(struct sip_outbound_registration_state *state)
{
	ao2_ref(state, +1);
	if (ast_sip_push_task(state->client_state->serializer, unregister_task, state)) {
		ao2_ref(state, -1);
		return -1;
	}

	return 0;
}

static int queue_register(struct sip_outbound_registration_state *state)
{
	ao2_ref(state, +1);
	if (ast_sip_push_task(state->client_state->serializer, sip_outbound_registration_perform, state)) {
		ao2_ref(state, -1);
		return -1;
	}

	return 0;
}

static void unregister_all(void)
{
	struct ao2_container *states;

	states = ao2_global_obj_ref(current_states);
	if (!states) {
		return;
	}

	/* Clean out all the states and let sorcery handle recreating the registrations */
	ao2_callback(states, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, NULL, NULL);
	ao2_ref(states, -1);
}

static void reregister_all(void)
{
	unregister_all();
	ast_sorcery_load_object(ast_sip_get_sorcery(), "registration");
}

static char *cli_complete_registration(const char *line, const char *word,
				       int pos, int state)
{
	char *result = NULL;
	int wordlen;
	int which = 0;
	struct sip_outbound_registration *registration;
	struct ao2_container *registrations;
	struct ao2_iterator i;

	if (pos != 3) {
		return NULL;
	}

	wordlen = strlen(word);
	if (wordlen == 0 && ++which > state) {
		return ast_strdup("*all");
	}

	registrations = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "registration",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!registrations) {
		return NULL;
	}

	i = ao2_iterator_init(registrations, 0);
	while ((registration = ao2_iterator_next(&i))) {
		const char *name = ast_sorcery_object_get_id(registration);

		if (!strncasecmp(word, name, wordlen) && ++which > state) {
			result = ast_strdup(name);
		}

		ao2_ref(registration, -1);
		if (result) {
			break;
		}
	}
	ao2_iterator_destroy(&i);

	ao2_ref(registrations, -1);
	return result;
}

static char *cli_unregister(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sip_outbound_registration_state *state;
	const char *registration_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip send unregister";
		e->usage =
			"Usage: pjsip send unregister <registration> | *all\n"
			"       Unregisters the specified (or all) outbound registration(s) "
			"and stops future registration attempts.\n";
		return NULL;
	case CLI_GENERATE:
		return cli_complete_registration(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	registration_name = a->argv[3];

	if (strcmp(registration_name, "*all") == 0) {
		unregister_all();
		ast_cli(a->fd, "Unregister all queued\n");
		return CLI_SUCCESS;
	}

	state = get_state(registration_name);
	if (!state) {
		ast_cli(a->fd, "Unable to retrieve registration %s\n", registration_name);
		return CLI_FAILURE;
	}

	if (queue_unregister(state)) {
		ast_cli(a->fd, "Failed to queue unregistration\n");
	}

	ao2_ref(state, -1);
	return CLI_SUCCESS;
}

static char *cli_register(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct sip_outbound_registration_state *state;
	const char *registration_name;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip send register";
		e->usage =
			"Usage: pjsip send register <registration> | *all \n"
			"       Unregisters the specified (or all) outbound "
			"registration(s) then starts registration(s) and schedules re-registrations.\n";
		return NULL;
	case CLI_GENERATE:
		return cli_complete_registration(a->line, a->word, a->pos, a->n);
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	registration_name = a->argv[3];

	if (strcmp(registration_name, "*all") == 0) {
		reregister_all();
		ast_cli(a->fd, "Re-register all queued\n");
		return CLI_SUCCESS;
	}

	state = get_state(registration_name);
	if (!state) {
		ast_cli(a->fd, "Unable to retrieve registration %s\n", registration_name);
		return CLI_FAILURE;
	}

	/* We need to serialize the unregister and register so they need
	 * to be queued as separate tasks.
	 */
	if (queue_unregister(state)) {
		ast_cli(a->fd, "Failed to queue unregistration\n");
	} else if (queue_register(state)) {
		ast_cli(a->fd, "Failed to queue registration\n");
	}

	ao2_ref(state, -1);
	return CLI_SUCCESS;
}

static int ami_unregister(struct mansession *s, const struct message *m)
{
	const char *registration_name = astman_get_header(m, "Registration");
	struct sip_outbound_registration_state *state;

	if (ast_strlen_zero(registration_name)) {
		astman_send_error(s, m, "Registration parameter missing.");
		return 0;
	}

	if (strcmp(registration_name, "*all") == 0) {
		unregister_all();
		astman_send_ack(s, m, "Unregistrations queued.");
		return 0;
	}

	state = get_state(registration_name);
	if (!state) {
		astman_send_error(s, m, "Unable to retrieve registration entry\n");
		return 0;
	}

	if (queue_unregister(state)) {
		astman_send_ack(s, m, "Failed to queue unregistration");
	} else {
		astman_send_ack(s, m, "Unregistration sent");
	}

	ao2_ref(state, -1);
	return 0;
}

static int ami_register(struct mansession *s, const struct message *m)
{
	const char *registration_name = astman_get_header(m, "Registration");
	struct sip_outbound_registration_state *state;

	if (ast_strlen_zero(registration_name)) {
		astman_send_error(s, m, "Registration parameter missing.");
		return 0;
	}

	if (strcmp(registration_name, "*all") == 0) {
		reregister_all();
		astman_send_ack(s, m, "Reregistrations queued.");
		return 0;
	}

	state = get_state(registration_name);
	if (!state) {
		astman_send_error(s, m, "Unable to retrieve registration entry\n");
		return 0;
	}

	/* We need to serialize the unregister and register so they need
	 * to be queued as separate tasks.
	 */
	if (queue_unregister(state)) {
		astman_send_ack(s, m, "Failed to queue unregistration");
	} else if (queue_register(state)) {
		astman_send_ack(s, m, "Failed to queue unregistration");
	} else {
		astman_send_ack(s, m, "Reregistration sent");
	}

	ao2_ref(state, -1);
	return 0;
}

struct sip_ami_outbound {
	struct ast_sip_ami *ami;
	int registered;
	int not_registered;
	struct sip_outbound_registration *registration;
};

static int ami_outbound_registration_task(void *obj)
{
	struct sip_ami_outbound *ami = obj;
	struct ast_str *buf;
	struct sip_outbound_registration_state *state;

	buf = ast_sip_create_ami_event("OutboundRegistrationDetail", ami->ami);
	if (!buf) {
		return -1;
	}

	ast_sip_sorcery_object_to_ami(ami->registration, &buf);

	if ((state = get_state(ast_sorcery_object_get_id(ami->registration)))) {
		pjsip_regc_info info;

		if (state->client_state->status == SIP_REGISTRATION_REGISTERED) {
			++ami->registered;
		} else {
			++ami->not_registered;
		}

		ast_str_append(&buf, 0, "Status: %s\r\n",
			sip_outbound_registration_status_str(state->client_state->status));

		pjsip_regc_get_info(state->client_state->client, &info);
		ast_str_append(&buf, 0, "NextReg: %d\r\n", info.next_reg);
		ao2_ref(state, -1);
	}

	astman_append(ami->ami->s, "%s\r\n", ast_str_buffer(buf));
	ast_free(buf);

	return ast_sip_format_auths_ami(&ami->registration->outbound_auths, ami->ami);
}

static int ami_outbound_registration_detail(void *obj, void *arg, int flags)
{
	struct sip_ami_outbound *ami = arg;

	ami->registration = obj;
	return ast_sip_push_task_wait_servant(NULL, ami_outbound_registration_task, ami);
}

static int ami_show_outbound_registrations(struct mansession *s,
					   const struct message *m)
{
	struct ast_sip_ami ami = { .s = s, .m = m, .action_id = astman_get_header(m, "ActionID"), };
	struct sip_ami_outbound ami_outbound = { .ami = &ami };
	struct ao2_container *regs;

	regs = get_registrations();
	if (!regs) {
		astman_send_error(s, m, "Unable to retrieve "
				  "outbound registrations\n");
		return -1;
	}

	astman_send_listack(s, m, "Following are Events for each Outbound registration",
		"start");

	ao2_callback(regs, OBJ_NODATA, ami_outbound_registration_detail, &ami_outbound);

	astman_send_list_complete_start(s, m, "OutboundRegistrationDetailComplete",
		ami_outbound.registered + ami_outbound.not_registered);
	astman_append(s,
		"Registered: %d\r\n"
		"NotRegistered: %d\r\n",
		ami_outbound.registered,
		ami_outbound.not_registered);
	astman_send_list_complete_end(s);

	ao2_ref(regs, -1);
	return 0;
}

static struct ao2_container *cli_get_container(const char *regex)
{
	RAII_VAR(struct ao2_container *, container, NULL, ao2_cleanup);
	struct ao2_container *s_container;

	container = ast_sorcery_retrieve_by_regex(ast_sip_get_sorcery(), "registration", regex);
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

static int cli_iterator(void *container, ao2_callback_fn callback, void *args)
{
	ao2_callback(container, OBJ_NODATA, callback, args);

	return 0;
}

static void *cli_retrieve_by_id(const char *id)
{
	struct ao2_container *states;
	void *obj = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "registration", id);

	if (!obj) {
		/* if the object no longer exists then remove its state  */
		states = ao2_global_obj_ref(current_states);
		if (states) {
			ao2_find(states, id, OBJ_SEARCH_KEY | OBJ_UNLINK | OBJ_NODATA);
			ao2_ref(states, -1);
		}
	}

	return obj;
}

static int cli_print_header(void *obj, void *arg, int flags)
{
	struct ast_sip_cli_context *context = arg;

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0,
		" <Registration/ServerURI..............................>  <Auth..........>  <Status.......>\n");

	return 0;
}

static int cli_print_body(void *obj, void *arg, int flags)
{
	struct sip_outbound_registration *registration = obj;
	struct ast_sip_cli_context *context = arg;
	const char *id = ast_sorcery_object_get_id(registration);
	struct sip_outbound_registration_state *state = get_state(id);
#define REGISTRATION_URI_FIELD_LEN	53

	ast_assert(context->output_buffer != NULL);

	ast_str_append(&context->output_buffer, 0, " %-s/%-*.*s  %-16s  %-16s\n",
		id,
		(int) (REGISTRATION_URI_FIELD_LEN - strlen(id)),
		(int) (REGISTRATION_URI_FIELD_LEN - strlen(id)),
		registration->server_uri,
		AST_VECTOR_SIZE(&registration->outbound_auths)
			? AST_VECTOR_GET(&registration->outbound_auths, 0)
			: "n/a",
		(state ? sip_outbound_registration_status_str(state->client_state->status) : "Unregistered"));
	ao2_cleanup(state);

	if (context->show_details
		|| (context->show_details_only_level_0 && context->indent_level == 0)) {
		ast_str_append(&context->output_buffer, 0, "\n");
		ast_sip_cli_print_sorcery_objectset(registration, context, 0);
	}

	return 0;
}

/*
 * A function pointer to callback needs to be within the
 * module in order to avoid problems with an undefined
 * symbol when the module is loaded.
 */
static char *my_cli_traverse_objects(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	return ast_sip_cli_traverse_objects(e, cmd, a);
}

static struct ast_cli_entry cli_outbound_registration[] = {
	AST_CLI_DEFINE(cli_unregister, "Unregisters outbound registration target"),
	AST_CLI_DEFINE(cli_register, "Registers an outbound registration target"),
	AST_CLI_DEFINE(my_cli_traverse_objects, "List PJSIP Registrations",
		.command = "pjsip list registrations",
		.usage = "Usage: pjsip list registrations [ like <pattern> ]\n"
				"       List the configured PJSIP Registrations\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(my_cli_traverse_objects, "Show PJSIP Registrations",
		.command = "pjsip show registrations",
		.usage = "Usage: pjsip show registrations [ like <pattern> ]\n"
				"       Show the configured PJSIP Registrations\n"
				"       Optional regular expression pattern is used to filter the list.\n"),
	AST_CLI_DEFINE(my_cli_traverse_objects, "Show PJSIP Registration",
		.command = "pjsip show registration",
		.usage = "Usage: pjsip show registration <id>\n"
				 "       Show the configured PJSIP Registration\n"),
};

static struct ast_sip_cli_formatter_entry *cli_formatter;

static void auth_observer(const char *type)
{
	struct sip_outbound_registration *registration;
	struct sip_outbound_registration_state *state;
	struct ao2_container *regs;
	const char *registration_id;
	struct ao2_iterator i;

	ast_debug(4, "Auths updated. Checking for any outbound registrations that are in permanent rejected state so they can be retried\n");

	regs = ast_sorcery_retrieve_by_fields(ast_sip_get_sorcery(), "registration",
		AST_RETRIEVE_FLAG_MULTIPLE | AST_RETRIEVE_FLAG_ALL, NULL);
	if (!regs || ao2_container_count(regs) == 0) {
		ao2_cleanup(regs);
		return;
	}

	i = ao2_iterator_init(regs, 0);
	for (; (registration = ao2_iterator_next(&i)); ao2_ref(registration, -1)) {
		registration_id = ast_sorcery_object_get_id(registration);
		state = get_state(registration_id);
		if (state && state->client_state->status == SIP_REGISTRATION_REJECTED_PERMANENT) {
			ast_debug(4, "Trying outbound registration '%s' again\n", registration_id);

			if (ast_sip_push_task(state->client_state->serializer,
					      sip_outbound_registration_perform, ao2_bump(state))) {
				ast_log(LOG_ERROR, "Failed to perform outbound registration on '%s'\n", registration_id);
				ao2_ref(state, -1);
			}
		}
		ao2_cleanup(state);
	}
	ao2_iterator_destroy(&i);
	ao2_cleanup(regs);
}

static const struct ast_sorcery_observer observer_callbacks_auth = {
	.loaded = auth_observer,
};

static int check_state(void *obj, void *arg, int flags)
{
	struct sip_outbound_registration_state *state = obj;
	struct sip_outbound_registration *registration;

	registration = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "registration",
		ast_sorcery_object_get_id(state->registration));
	if (!registration) {
		/* This is a dead registration */
		return CMP_MATCH;
	}

	ao2_ref(registration, -1);
	return 0;
}

/*!
 * \internal
 * \brief Observer to purge dead registration states.
 *
 * \param name Module name owning the sorcery instance.
 * \param sorcery Instance being observed.
 * \param object_type Name of object being observed.
 * \param reloaded Non-zero if the object is being reloaded.
 *
 * \return Nothing
 */
static void registration_loaded_observer(const char *name, const struct ast_sorcery *sorcery, const char *object_type, int reloaded)
{
	struct ao2_container *states;

	if (strcmp(object_type, "registration")) {
		/* Not interested */
		return;
	}

	states = ao2_global_obj_ref(current_states);
	if (!states) {
		/* Global container has gone.  Likely shutting down. */
		return;
	}

	/*
	 * Refresh the current configured registrations. We don't need to hold
	 * onto the objects, as the apply handler will cause their states to
	 * be created appropriately.
	 */
	ao2_cleanup(get_registrations());

	/* Now to purge dead registrations. */
	ao2_callback(states, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE, check_state, NULL);
	ao2_ref(states, -1);
}

static const struct ast_sorcery_instance_observer observer_callbacks_registrations = {
	.object_type_loaded = registration_loaded_observer,
};

static void registration_deleted_observer(const void *obj)
{
	const struct sip_outbound_registration *registration = obj;
	struct ao2_container *states;

	states = ao2_global_obj_ref(current_states);
	if (!states) {
		/* Global container has gone.  Likely shutting down. */
		return;
	}

	ao2_find(states, ast_sorcery_object_get_id(registration), OBJ_UNLINK | OBJ_NODATA | OBJ_SEARCH_KEY);

	ao2_ref(states, -1);
}

static const struct ast_sorcery_observer registration_observer = {
	.deleted = registration_deleted_observer,
};

static void network_change_stasis_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	/* This callback is only concerned with network change messages from the system topic. */
	if (stasis_message_type(message) != ast_network_change_type()) {
		return;
	}
	ast_debug(3, "Received network change event\n");

	reregister_all();
}

static int unload_module(void)
{
	int remaining;

	network_change_sub = stasis_unsubscribe_and_join(network_change_sub);

	ast_manager_unregister("PJSIPShowRegistrationsOutbound");
	ast_manager_unregister("PJSIPUnregister");
	ast_manager_unregister("PJSIPRegister");

	ast_cli_unregister_multiple(cli_outbound_registration, ARRAY_LEN(cli_outbound_registration));
	ast_sip_unregister_cli_formatter(cli_formatter);
	cli_formatter = NULL;

	ast_sip_unregister_endpoint_identifier(&line_identifier);

	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "auth", &observer_callbacks_auth);
	ast_sorcery_instance_observer_remove(ast_sip_get_sorcery(), &observer_callbacks_registrations);

	ast_sorcery_object_unregister(ast_sip_get_sorcery(), "registration");

	ao2_global_obj_release(current_states);

	ast_sip_transport_monitor_unregister_all(registration_transport_shutdown_cb, NULL, NULL);

	/* Wait for registration serializers to get destroyed. */
	ast_debug(2, "Waiting for registration transactions to complete for unload.\n");
	remaining = ast_serializer_shutdown_group_join(shutdown_group, MAX_UNLOAD_TIMEOUT_TIME);
	if (remaining) {
		/*
		 * NOTE: We probably have a sip_outbound_registration_client_state
		 * ref leak if the remaining count cannot reach zero after a few
		 * minutes of trying to unload.
		 */
		ast_log(LOG_WARNING, "Unload incomplete.  Could not stop %d outbound registrations.  Try again later.\n",
			remaining);
		return -1;
	}

	ast_debug(2, "Successful shutdown.\n");

	ao2_cleanup(shutdown_group);
	shutdown_group = NULL;

	return 0;
}

static int load_module(void)
{
	struct ao2_container *new_states;

	shutdown_group = ast_serializer_shutdown_group_alloc();
	if (!shutdown_group) {
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Create outbound registration states container. */
	new_states = ao2_container_alloc_hash(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		DEFAULT_STATE_BUCKETS, registration_state_hash, NULL, registration_state_cmp);
	if (!new_states) {
		ast_log(LOG_ERROR, "Unable to allocate registration states container\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	ao2_global_obj_replace_unref(current_states, new_states);
	ao2_ref(new_states, -1);

	/*
	 * Register sorcery object descriptions.
	 */
	ast_sorcery_apply_config(ast_sip_get_sorcery(), "res_pjsip_outbound_registration");
	ast_sorcery_apply_default(ast_sip_get_sorcery(), "registration", "config", "pjsip.conf,criteria=type=registration");

	if (ast_sorcery_object_register(ast_sip_get_sorcery(), "registration", sip_outbound_registration_alloc, NULL, sip_outbound_registration_apply)) {
		unload_module();
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
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "forbidden_retry_interval", "0", OPT_UINT_T, 0, FLDSET(struct sip_outbound_registration, forbidden_retry_interval));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "fatal_retry_interval", "0", OPT_UINT_T, 0, FLDSET(struct sip_outbound_registration, fatal_retry_interval));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "max_retries", "10", OPT_UINT_T, 0, FLDSET(struct sip_outbound_registration, max_retries));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "auth_rejection_permanent", "yes", OPT_BOOL_T, 1, FLDSET(struct sip_outbound_registration, auth_rejection_permanent));
	ast_sorcery_object_field_register_custom(ast_sip_get_sorcery(), "registration", "outbound_auth", "", outbound_auth_handler, outbound_auths_to_str, outbound_auths_to_var_list, 0, 0);
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "support_path", "no", OPT_BOOL_T, 1, FLDSET(struct sip_outbound_registration, support_path));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "line", "no", OPT_BOOL_T, 1, FLDSET(struct sip_outbound_registration, line));
	ast_sorcery_object_field_register(ast_sip_get_sorcery(), "registration", "endpoint", "", OPT_STRINGFIELD_T, 0, STRFLDSET(struct sip_outbound_registration, endpoint));

	/*
	 * Register sorcery observers.
	 */
	if (ast_sorcery_instance_observer_add(ast_sip_get_sorcery(),
		&observer_callbacks_registrations)
		|| ast_sorcery_observer_add(ast_sip_get_sorcery(), "auth",
			&observer_callbacks_auth)
		|| ast_sorcery_observer_add(ast_sip_get_sorcery(), "registration",
			&registration_observer)) {
		ast_log(LOG_ERROR, "Unable to register observers.\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}

	/* Register how this module identifies endpoints. */
	ast_sip_register_endpoint_identifier(&line_identifier);

	/* Register CLI commands. */
	cli_formatter = ao2_alloc(sizeof(struct ast_sip_cli_formatter_entry), NULL);
	if (!cli_formatter) {
		ast_log(LOG_ERROR, "Unable to allocate memory for cli formatter\n");
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	cli_formatter->name = "registration";
	cli_formatter->print_header = cli_print_header;
	cli_formatter->print_body = cli_print_body;
	cli_formatter->get_container = cli_get_container;
	cli_formatter->iterate = cli_iterator;
	cli_formatter->get_id = ast_sorcery_object_get_id;
	cli_formatter->retrieve_by_id = cli_retrieve_by_id;
	ast_sip_register_cli_formatter(cli_formatter);
	ast_cli_register_multiple(cli_outbound_registration, ARRAY_LEN(cli_outbound_registration));

	/* Register AMI actions. */
	ast_manager_register_xml("PJSIPUnregister", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, ami_unregister);
	ast_manager_register_xml("PJSIPRegister", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, ami_register);
	ast_manager_register_xml("PJSIPShowRegistrationsOutbound", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, ami_show_outbound_registrations);

	/* Clear any previous statsd gauges in case we weren't shutdown cleanly */
	ast_statsd_log("PJSIP.registrations.count", AST_STATSD_GAUGE, 0);
	ast_statsd_log("PJSIP.registrations.state.Registered", AST_STATSD_GAUGE, 0);
	ast_statsd_log("PJSIP.registrations.state.Unregistered", AST_STATSD_GAUGE, 0);
	ast_statsd_log("PJSIP.registrations.state.Rejected", AST_STATSD_GAUGE, 0);

	/* Load configuration objects */
	ast_sorcery_load_object(ast_sip_get_sorcery(), "registration");

	network_change_sub = stasis_subscribe(ast_system_topic(),
		network_change_stasis_cb, NULL);
	stasis_subscription_accept_message_type(network_change_sub, ast_network_change_type());
	stasis_subscription_set_filter(network_change_sub, STASIS_SUBSCRIPTION_FILTER_SELECTIVE);

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	ast_sorcery_reload_object(ast_sip_get_sorcery(), "registration");
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Outbound Registration Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.reload = reload_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip",
	.optional_modules = "res_statsd",
);
