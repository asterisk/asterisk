/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/time.h"
#include "include/res_pjsip_private.h"

#define DEFAULT_LANGUAGE "en"
#define DEFAULT_ENCODING "text/plain"
#define QUALIFIED_BUCKETS 211

static int qualify_contact(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact);

/*!
 * \internal
 * \brief Create a ast_sip_contact_status object.
 */
static void *contact_status_alloc(const char *name)
{
	struct ast_sip_contact_status *status = ast_sorcery_generic_alloc(sizeof(*status), NULL);

	if (!status) {
		ast_log(LOG_ERROR, "Unable to allocate ast_sip_contact_status\n");
		return NULL;
	}

	status->status = UNAVAILABLE;

	return status;
}

/*!
 * \internal
 * \brief Retrieve a ast_sip_contact_status object from sorcery creating
 *        one if not found.
 */
static struct ast_sip_contact_status *find_or_create_contact_status(const struct ast_sip_contact *contact)
{
	struct ast_sip_contact_status *status = ast_sorcery_retrieve_by_id(
		ast_sip_get_sorcery(), CONTACT_STATUS,
		ast_sorcery_object_get_id(contact));

	if (status) {
		return status;
	}

	if (!(status = ast_sorcery_alloc(
		      ast_sip_get_sorcery(), CONTACT_STATUS,
		      ast_sorcery_object_get_id(contact)))) {

		ast_log(LOG_ERROR, "Unable to create ast_sip_contact_status for contact %s\n",
			contact->uri);
		return NULL;
	}

	if (ast_sorcery_create(ast_sip_get_sorcery(), status)) {
		ast_log(LOG_ERROR, "Unable to persist ast_sip_contact_status for contact %s\n",
			contact->uri);
		return NULL;
	}

	return status;
}

/*!
 * \internal
 * \brief Update an ast_sip_contact_status's elements.
 */
static void update_contact_status(const struct ast_sip_contact *contact,
				  enum ast_sip_contact_status_type value)
{
	RAII_VAR(struct ast_sip_contact_status *, status,
		 find_or_create_contact_status(contact), ao2_cleanup);

	RAII_VAR(struct ast_sip_contact_status *, update, ast_sorcery_alloc(
		      ast_sip_get_sorcery(), CONTACT_STATUS,
		      ast_sorcery_object_get_id(status)), ao2_cleanup);

	if (!update) {
		ast_log(LOG_ERROR, "Unable to create update ast_sip_contact_status for contact %s\n",
			contact->uri);
		return;
	}

	update->status = value;

	/* if the contact is available calculate the rtt as
	   the diff between the last start time and "now" */
	update->rtt = update->status ?
		ast_tvdiff_us(ast_tvnow(), status->rtt_start) : 0;

	update->rtt_start = ast_tv(0, 0);

	if (ast_sorcery_update(ast_sip_get_sorcery(), update)) {
		ast_log(LOG_ERROR, "Unable to update ast_sip_contact_status for contact %s\n",
			contact->uri);
	}
}

/*!
 * \internal
 * \brief Initialize the start time on a contact status so the round
 *        trip time can be calculated upon a valid response.
 */
static void init_start_time(const struct ast_sip_contact *contact)
{
	RAII_VAR(struct ast_sip_contact_status *, status,
		 find_or_create_contact_status(contact), ao2_cleanup);

	RAII_VAR(struct ast_sip_contact_status *, update, ast_sorcery_alloc(
		      ast_sip_get_sorcery(), CONTACT_STATUS,
		      ast_sorcery_object_get_id(status)), ao2_cleanup);

	if (!update) {
		ast_log(LOG_ERROR, "Unable to create update ast_sip_contact_status for contact %s\n",
			contact->uri);
		return;
	}

	update->rtt_start = ast_tvnow();

	if (ast_sorcery_update(ast_sip_get_sorcery(), update)) {
		ast_log(LOG_ERROR, "Unable to update ast_sip_contact_status for contact %s\n",
			contact->uri);
	}
}

/*!
 * \internal
 * \brief For an endpoint try to match on a given contact.
 */
static int on_endpoint(void *obj, void *arg, int flags)
{
	struct ast_sip_endpoint *endpoint = obj;
	char *aor_name, *aors;

	if (!arg || ast_strlen_zero(endpoint->aors)) {
		return 0;
	}

	aors = ast_strdupa(endpoint->aors);

	while ((aor_name = strsep(&aors, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor,
			 ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);

		if (!aor || !(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
			continue;
		}

		if (ao2_find(contacts, arg, OBJ_NODATA | OBJ_POINTER)) {
			return CMP_MATCH;
		}
	}

	return 0;
}

/*!
 * \internal
 * \brief Find endpoints associated with the given contact.
 */
static struct ao2_iterator *find_endpoints(struct ast_sip_contact *contact)
{
	RAII_VAR(struct ao2_container *, endpoints,
		 ast_sip_get_endpoints(), ao2_cleanup);

	return ao2_callback(endpoints, OBJ_MULTIPLE, on_endpoint, contact);
}

/*!
 * \internal
 * \brief Receive an response to the qualify contact request.
 */
static void qualify_contact_cb(void *token, pjsip_event *e)
{
	RAII_VAR(struct ast_sip_contact *, contact, token, ao2_cleanup);

	switch(e->body.tsx_state.type) {
	case PJSIP_EVENT_TRANSPORT_ERROR:
	case PJSIP_EVENT_TIMER:
		update_contact_status(contact, UNAVAILABLE);
		break;
	default:
		update_contact_status(contact, AVAILABLE);
		break;
	}
}

/*!
 * \internal
 * \brief Attempt to qualify the contact
 *
 * \detail Sends a SIP OPTIONS request to the given contact in order to make
 *         sure that contact is available.
 */
static int qualify_contact(struct ast_sip_endpoint *endpoint, struct ast_sip_contact *contact)
{
	pjsip_tx_data *tdata;
	RAII_VAR(struct ast_sip_endpoint *, endpoint_local, ao2_bump(endpoint), ao2_cleanup);


	if (!endpoint_local) {
		struct ao2_iterator *endpoint_iterator = find_endpoints(contact);

		/* try to find endpoints that are associated with the contact */
		if (endpoint_iterator) {
			/* find "first" endpoint in order to authenticate - actually any
			   endpoint should do that matched on the contact */
			endpoint_local = ao2_iterator_next(endpoint_iterator);
			ao2_iterator_destroy(endpoint_iterator);
		}
	}

	if (ast_sip_create_request("OPTIONS", NULL, NULL, NULL, contact, &tdata)) {
		ast_log(LOG_ERROR, "Unable to create request to qualify contact %s\n",
			contact->uri);
		return -1;
	}

	/* If an outbound proxy is specified set it on this request */
	if (!ast_strlen_zero(contact->outbound_proxy) &&
		ast_sip_set_outbound_proxy(tdata, contact->outbound_proxy)) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "Unable to apply outbound proxy on request to qualify contact %s\n",
			contact->uri);
		return -1;
	}

	init_start_time(contact);

	ao2_ref(contact, +1);
	if (ast_sip_send_request(tdata, NULL, endpoint_local, contact,
		qualify_contact_cb) != PJ_SUCCESS) {
		/* The callback will be called so we don't need to drop the contact ref*/
		ast_log(LOG_ERROR, "Unable to send request to qualify contact %s\n",
			contact->uri);
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Scheduling context for sending QUALIFY request at specified intervals.
 */
static struct ast_sched_context *sched;

/*!
 * \internal
 * \brief Container to hold all actively scheduled qualifies.
 */
static struct ao2_container *sched_qualifies;

/*!
 * \internal
 * \brief Structure to hold qualify contact scheduling information.
 */
struct sched_data {
	/*! The scheduling id */
	int id;
	/*! The the contact being checked */
	struct ast_sip_contact *contact;
};

/*!
 * \internal
 * \brief Destroy the scheduled data and remove from scheduler.
 */
static void sched_data_destructor(void *obj)
{
	struct sched_data *data = obj;
	ao2_cleanup(data->contact);
}
/*!
 * \internal
 * \brief Create the scheduling data object.
 */
static struct sched_data *sched_data_create(struct ast_sip_contact *contact)
{
	struct sched_data *data = ao2_alloc(sizeof(*data), sched_data_destructor);

	if (!data) {
		ast_log(LOG_ERROR, "Unable to create schedule qualify data\n");
		return NULL;
	}

	data->contact = contact;
	ao2_ref(data->contact, +1);

	return data;
}

/*!
 * \internal
 * \brief Send a qualify contact request within a threaded task.
 */
static int qualify_contact_task(void *obj)
{
	RAII_VAR(struct ast_sip_contact *, contact, obj, ao2_cleanup);
	return qualify_contact(NULL, contact);
}

/*!
 * \internal
 * \brief Send a scheduled qualify contact request.
 */
static int qualify_contact_sched(const void *obj)
{
	struct sched_data *data = (struct sched_data *)obj;

	ao2_ref(data->contact, +1);
	if (ast_sip_push_task(NULL, qualify_contact_task, data->contact)) {
		ao2_ref(data->contact, -1);
		ao2_cleanup(data);
		return 0;
	}

	return data->contact->qualify_frequency * 1000;
}

/*!
 * \internal
 * \brief Set up a scheduled qualify contact check.
 */
static void schedule_qualify(struct ast_sip_contact *contact)
{
	RAII_VAR(struct sched_data *, data, sched_data_create(contact), ao2_cleanup);

	if (!data) {
		return;
	}

	ao2_ref(data, +1);
	if ((data->id = ast_sched_add_variable(
		    sched, contact->qualify_frequency * 1000,
		    qualify_contact_sched, data, 1)) < 0) {

		ao2_ref(data, -1);
		ast_log(LOG_ERROR, "Unable to schedule qualify for contact %s\n",
			contact->uri);
		return;
	}

	ao2_link(sched_qualifies, data);
}

/*!
 * \internal
 * \brief Remove the contact from the scheduler.
 */
static void unschedule_qualify(struct ast_sip_contact *contact)
{
	RAII_VAR(struct sched_data *, data, ao2_find(
			 sched_qualifies, contact, OBJ_UNLINK), ao2_cleanup);

	if (!data) {
		return;
	}

	AST_SCHED_DEL_UNREF(sched, data->id, ao2_cleanup(data));
}

/*!
 * \internal
 * \brief Qualify the given contact and set up scheduling if configured.
 */
static void qualify_and_schedule(struct ast_sip_contact *contact)
{
	unschedule_qualify(contact);

	if (contact->qualify_frequency) {
		ao2_ref(contact, +1);
		ast_sip_push_task(NULL, qualify_contact_task, contact);

		schedule_qualify(contact);
	}
}

/*!
 * \internal
 * \brief A new contact has been created make sure it is available.
 */
static void contact_created(const void *obj)
{
	qualify_and_schedule((struct ast_sip_contact *)obj);
}

/*!
 * \internal
 * \brief A contact has been deleted remove status tracking.
 */
static void contact_deleted(const void *obj)
{
	struct ast_sip_contact *contact = (struct ast_sip_contact *)obj;
	RAII_VAR(struct ast_sip_contact_status *, status, NULL, ao2_cleanup);

	unschedule_qualify(contact);

	if (!(status = ast_sorcery_retrieve_by_id(
		      ast_sip_get_sorcery(), CONTACT_STATUS,
		      ast_sorcery_object_get_id(contact)))) {
		return;
	}

	if (ast_sorcery_delete(ast_sip_get_sorcery(), status)) {
		ast_log(LOG_ERROR, "Unable to delete ast_sip_contact_status for contact %s\n",
			contact->uri);
	}
}

static const struct ast_sorcery_observer contact_observer = {
	.created = contact_created,
	.deleted = contact_deleted
};

static pj_bool_t options_start(void)
{
	if (!(sched = ast_sched_context_create()) ||
	    ast_sched_start_thread(sched)) {
		return -1;
	}

	return PJ_SUCCESS;
}

static pj_bool_t options_stop(void)
{
	ast_sorcery_observer_remove(ast_sip_get_sorcery(), "contact", &contact_observer);

	ao2_t_ref(sched_qualifies, -1, "Remove scheduled qualifies on module stop");

	if (sched) {
		ast_sched_context_destroy(sched);
	}

	return PJ_SUCCESS;
}

static pj_status_t send_options_response(pjsip_rx_data *rdata, int code)
{
	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();
	pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
	pjsip_transaction *trans = pjsip_rdata_get_tsx(rdata);
	pjsip_tx_data *tdata;
	const pjsip_hdr *hdr;
	pjsip_response_addr res_addr;
	pj_status_t status;

	/* Make the response object */
	if ((status = ast_sip_create_response(rdata, code, NULL, &tdata) != PJ_SUCCESS)) {
		ast_log(LOG_ERROR, "Unable to create response (%d)\n", status);
		return status;
	}

	/* Add appropriate headers */
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_ACCEPT, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_ALLOW, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}
	if ((hdr = pjsip_endpt_get_capability(endpt, PJSIP_H_SUPPORTED, NULL))) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr*)pjsip_hdr_clone(tdata->pool, hdr));
	}

	/*
	 * XXX TODO: pjsip doesn't care a lot about either of these headers -
	 * while it provides specific methods to create them, they are defined
	 * to be the standard string header creation. We never did add them
	 * in chan_sip, although RFC 3261 says they SHOULD. Hard coded here.
	 */
	ast_sip_add_header(tdata, "Accept-Encoding", DEFAULT_ENCODING);
	ast_sip_add_header(tdata, "Accept-Language", DEFAULT_LANGUAGE);

	if (dlg && trans) {
		status = pjsip_dlg_send_response(dlg, trans, tdata);
	} else {
		/* Get where to send request. */
		if ((status = pjsip_get_response_addr(
			     tdata->pool, rdata, &res_addr)) != PJ_SUCCESS) {
			ast_log(LOG_ERROR, "Unable to get response address (%d)\n",
				status);

			pjsip_tx_data_dec_ref(tdata);
			return status;
		}
		status = ast_sip_send_response(&res_addr, tdata,
						   ast_pjsip_rdata_get_endpoint(rdata));
	}

	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to send response (%d)\n", status);
	}

	return status;
}

static pj_bool_t options_on_rx_request(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	pjsip_uri *ruri;
	pjsip_sip_uri *sip_ruri;
	char exten[AST_MAX_EXTENSION];

	if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method,
			     &pjsip_options_method)) {
		return PJ_FALSE;
	}

	if (!(endpoint = ast_pjsip_rdata_get_endpoint(rdata))) {
		return PJ_FALSE;
	}

	ruri = rdata->msg_info.msg->line.req.uri;
	if (!PJSIP_URI_SCHEME_IS_SIP(ruri) && !PJSIP_URI_SCHEME_IS_SIPS(ruri)) {
		send_options_response(rdata, 416);
		return -1;
	}

	sip_ruri = pjsip_uri_get_uri(ruri);
	ast_copy_pj_str(exten, &sip_ruri->user, sizeof(exten));

	if (ast_shutting_down()) {
		send_options_response(rdata, 503);
	} else if (!ast_exists_extension(NULL, endpoint->context, exten, 1, NULL)) {
		send_options_response(rdata, 404);
	} else {
		send_options_response(rdata, 200);
	}
	return PJ_TRUE;
}

static pjsip_module options_module = {
	.name = {"Options Module", 14},
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.start = options_start,
	.stop = options_stop,
	.on_rx_request = options_on_rx_request,
};

/*!
 * \internal
 * \brief Send qualify request to the given contact.
 */
static int cli_on_contact(void *obj, void *arg, void *data, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct ast_sip_endpoint *endpoint = data;
	int *cli_fd = arg;
	ast_cli(*cli_fd, " contact %s\n", contact->uri);
	qualify_contact(endpoint, contact);
	return 0;
}

/*!
 * \brief Data pushed to threadpool to qualify endpoints from the CLI
 */
struct qualify_data {
	/*! Endpoint that is being qualified */
	struct ast_sip_endpoint *endpoint;
	/*! CLI File descriptor for printing messages */
	int cli_fd;
};

static struct qualify_data *qualify_data_alloc(struct ast_sip_endpoint *endpoint, int cli_fd)
{
	struct qualify_data *qual_data;

	qual_data = ast_malloc(sizeof(*qual_data));
	if (!qual_data) {
		return NULL;
	}

	qual_data->endpoint = ao2_bump(endpoint);
	qual_data->cli_fd = cli_fd;
	return qual_data;
}

static void qualify_data_destroy(struct qualify_data *qual_data)
{
	ao2_cleanup(qual_data->endpoint);
	ast_free(qual_data);
}

/*!
 * \internal
 * \brief For an endpoint iterate over and qualify all aors/contacts
 */
static int cli_qualify_contacts(void *data)
{
	char *aor_name, *aors;
	RAII_VAR(struct qualify_data *, qual_data, data, qualify_data_destroy);
	struct ast_sip_endpoint *endpoint = qual_data->endpoint;
	int cli_fd = qual_data->cli_fd;
	const char *endpoint_name = ast_sorcery_object_get_id(endpoint);

	if (ast_strlen_zero(endpoint->aors)) {
		ast_cli(cli_fd, "Endpoint %s has no AoR's configured\n",
			endpoint_name);
		return 0;
	}

	aors = ast_strdupa(endpoint->aors);

	while ((aor_name = strsep(&aors, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor,
			 ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);

		if (!aor || !(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
			continue;
		}

		ast_cli(cli_fd, "Sending qualify to endpoint %s\n", endpoint_name);
		ao2_callback_data(contacts, OBJ_NODATA, cli_on_contact, &cli_fd, endpoint);
	}
	return 0;
}

static char *cli_qualify(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	const char *endpoint_name;
	struct qualify_data *qual_data;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip qualify";
		e->usage =
			"Usage: pjsip qualify <endpoint>\n"
			"       Send a SIP OPTIONS request to all contacts on the endpoint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	endpoint_name = a->argv[2];

	if (!(endpoint = ast_sorcery_retrieve_by_id(
		      ast_sip_get_sorcery(), "endpoint", endpoint_name))) {
		ast_cli(a->fd, "Unable to retrieve endpoint %s\n", endpoint_name);
		return CLI_FAILURE;
	}

	qual_data = qualify_data_alloc(endpoint, a->fd);
	if (!qual_data) {
		return CLI_FAILURE;
	}

	if (ast_sip_push_task(NULL, cli_qualify_contacts, qual_data)) {
		qualify_data_destroy(qual_data);
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

/*!
 * \internal
 * \brief Send qualify request to the given contact.
 */
static int ami_contact_cb(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	ao2_ref(contact, +1);
	if (ast_sip_push_task(NULL, qualify_contact_task, contact)) {
		ao2_cleanup(contact);
	}
	return 0;
}

static int ami_sip_qualify(struct mansession *s, const struct message *m)
{
	const char *endpoint_name = astman_get_header(m, "Endpoint");
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	char *aor_name, *aors;

	if (ast_strlen_zero(endpoint_name)) {
		astman_send_error(s, m, "Endpoint parameter missing.");
		return 0;
	}

	endpoint = ast_sorcery_retrieve_by_id(
		ast_sip_get_sorcery(),
		"endpoint",
		endpoint_name);
	if (!endpoint) {
		astman_send_error(s, m, "Unable to retrieve endpoint\n");
		return 0;
	}

	/* send a qualify for all contacts registered with the endpoint */
	if (ast_strlen_zero(endpoint->aors)) {
		astman_send_error(s, m, "No AoRs configured for endpoint\n");
		return 0;
	}

	aors = ast_strdupa(endpoint->aors);

	while ((aor_name = strsep(&aors, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor,
			 ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);
		RAII_VAR(struct ao2_container *, contacts, NULL, ao2_cleanup);

		if (!aor || !(contacts = ast_sip_location_retrieve_aor_contacts(aor))) {
			continue;
		}

		ao2_callback(contacts, OBJ_NODATA, ami_contact_cb, NULL);
	}

	astman_send_ack(s, m, "Endpoint found, will qualify");
	return 0;
}

static struct ast_cli_entry cli_options[] = {
	AST_CLI_DEFINE(cli_qualify, "Send an OPTIONS request to a PJSIP endpoint")
};

static int sched_qualifies_hash_fn(const void *obj, int flags)
{
	const struct sched_data *data = obj;

	return ast_str_hash(ast_sorcery_object_get_id(data->contact));
}

static int sched_qualifies_cmp_fn(void *obj, void *arg, int flags)
{
	struct sched_data *data = obj;

	return !strcmp(ast_sorcery_object_get_id(data->contact),
		       ast_sorcery_object_get_id(arg));
}

int ast_sip_initialize_sorcery_qualify(struct ast_sorcery *sorcery)
{
	/* initialize sorcery ast_sip_contact_status resource */
	ast_sorcery_apply_default(sorcery, CONTACT_STATUS, "memory", NULL);

	if (ast_sorcery_internal_object_register(sorcery, CONTACT_STATUS,
					contact_status_alloc, NULL, NULL)) {
		ast_log(LOG_ERROR, "Unable to register ast_sip_contact_status in sorcery\n");
		return -1;
	}

	ast_sorcery_object_field_register_nodoc(sorcery, CONTACT_STATUS, "rtt", "0", OPT_UINT_T,
					  1, FLDSET(struct ast_sip_contact_status, status));
	ast_sorcery_object_field_register_nodoc(sorcery, CONTACT_STATUS, "rtt", "0", OPT_UINT_T,
					  1, FLDSET(struct ast_sip_contact_status, rtt));

	return 0;
}

static int qualify_and_schedule_cb(void *obj, void *arg, int flags)
{
	struct ast_sip_contact *contact = obj;
	struct ast_sip_aor *aor = arg;

	contact->qualify_frequency = aor->qualify_frequency;

	qualify_and_schedule(contact);

	return 0;
}

/*!
 * \internal
 * \brief Qualify and schedule an endpoint's permanent contacts
 *
 * \detail For the given endpoint retrieve its list of aors, qualify all
 *         permanent contacts, and schedule for checks if configured.
 */
static int qualify_and_schedule_permanent_cb(void *obj, void *arg, int flags)
{
	struct ast_sip_endpoint *endpoint = obj;
	char *aor_name, *aors;

	if (ast_strlen_zero(endpoint->aors)) {
		return 0;
	}

	aors = ast_strdupa(endpoint->aors);

	while ((aor_name = strsep(&aors, ","))) {
		RAII_VAR(struct ast_sip_aor *, aor,
			 ast_sip_location_retrieve_aor(aor_name), ao2_cleanup);

		if (!aor || !aor->permanent_contacts) {
			continue;
		}
		ao2_callback(aor->permanent_contacts, OBJ_NODATA, qualify_and_schedule_cb, aor);
	}

	return 0;
}

static void qualify_and_schedule_permanent(void)
{
	RAII_VAR(struct ao2_container *, endpoints,
		 ast_sip_get_endpoints(), ao2_cleanup);

	ao2_callback(endpoints, OBJ_NODATA,
		     qualify_and_schedule_permanent_cb, NULL);
}

int ast_res_pjsip_init_options_handling(int reload)
{
	const pj_str_t STR_OPTIONS = { "OPTIONS", 7 };

	if (sched_qualifies) {
		ao2_t_ref(sched_qualifies, -1, "Remove old scheduled qualifies");
	}

	if (!(sched_qualifies = ao2_t_container_alloc(
		QUALIFIED_BUCKETS, sched_qualifies_hash_fn, sched_qualifies_cmp_fn,
		"Create container for scheduled qualifies"))) {

		return -1;
	}

	if (reload) {
		qualify_and_schedule_permanent();
		return 0;
	}

	if (pjsip_endpt_register_module(ast_sip_get_pjsip_endpoint(), &options_module) != PJ_SUCCESS) {
		options_stop();
		return -1;
	}

	if (pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), NULL, PJSIP_H_ALLOW, NULL, 1, &STR_OPTIONS) != PJ_SUCCESS) {
		pjsip_endpt_unregister_module(ast_sip_get_pjsip_endpoint(), &options_module);
		return -1;
	}

	if (ast_sorcery_observer_add(ast_sip_get_sorcery(), "contact", &contact_observer)) {
		ast_log(LOG_WARNING, "Unable to add contact observer\n");
		return -1;
	}

	qualify_and_schedule_permanent();
	ast_cli_register_multiple(cli_options, ARRAY_LEN(cli_options));
	ast_manager_register2("PJSIPQualify", EVENT_FLAG_SYSTEM | EVENT_FLAG_REPORTING, ami_sip_qualify, NULL, NULL, NULL);

	return 0;
}

void ast_res_pjsip_cleanup_options_handling(void)
{
	ast_cli_unregister_multiple(cli_options, ARRAY_LEN(cli_options));
	ast_manager_unregister("PJSIPQualify");
}
