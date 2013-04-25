/*
 * sip_options.c
 *
 *  Created on: Jan 25, 2013
 *      Author: mjordan
 */

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjlib.h>

#include "asterisk/res_sip.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "include/res_sip_private.h"

#define DEFAULT_LANGUAGE "en"
#define DEFAULT_ENCODING "text/plain"
#define QUALIFIED_BUCKETS 211

/*! \brief Scheduling context for qualifies */
static struct ast_sched_context *sched; /* XXX move this to registrar */

struct ao2_container *scheduled_qualifies;

struct qualify_info {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(endpoint_id);
	);
	char *scheduler_data;
	int scheduler_id;
};

static pj_bool_t options_module_start(void);
static pj_bool_t options_module_stop(void);
static pj_bool_t options_module_on_rx_request(pjsip_rx_data *rdata);
static pj_bool_t options_module_on_rx_response(pjsip_rx_data *rdata);

static pjsip_module options_module = {
	.name = {"Options Module", 14},
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.start = options_module_start,
	.stop = options_module_stop,
	.on_rx_request = options_module_on_rx_request,
	.on_rx_response = options_module_on_rx_response,
};

static pj_bool_t options_module_start(void)
{
	if (!(sched = ast_sched_context_create()) ||
	    ast_sched_start_thread(sched)) {
		return -1;
	}

	return PJ_SUCCESS;
}

static pj_bool_t options_module_stop(void)
{
	ao2_t_ref(scheduled_qualifies, -1, "Remove scheduled qualifies on module stop");

	if (sched) {
		ast_sched_context_destroy(sched);
	}

	return PJ_SUCCESS;
}

static pj_status_t send_options_response(pjsip_rx_data *rdata, pjsip_dialog *pj_dlg, int code)
{
	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();
	pjsip_transaction *pj_trans = pjsip_rdata_get_tsx(rdata);
	pjsip_tx_data *tdata;
	const pjsip_hdr *hdr;
	pjsip_response_addr res_addr;
	pj_status_t status;

	/* Make the response object */
	status = pjsip_endpt_create_response(endpt, rdata, code, NULL, &tdata);
	if (status != PJ_SUCCESS) {
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

	if (pj_dlg && pj_trans) {
		status = pjsip_dlg_send_response(pj_dlg, pj_trans, tdata);
	} else {
		/* Get where to send request. */
		status = pjsip_get_response_addr(tdata->pool, rdata, &res_addr);
		if (status != PJ_SUCCESS) {
			pjsip_tx_data_dec_ref(tdata);
			return status;
		}
		status = pjsip_endpt_send_response(endpt, &res_addr, tdata, NULL, NULL);
	}

	return status;
}

static pj_bool_t options_module_on_rx_request(pjsip_rx_data *rdata)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	pjsip_dialog *dlg = pjsip_rdata_get_dlg(rdata);
	pjsip_uri *ruri;
	pjsip_sip_uri *sip_ruri;
	char exten[AST_MAX_EXTENSION];

	if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_options_method)) {
		return PJ_FALSE;
	}
	endpoint = ast_pjsip_rdata_get_endpoint(rdata);
	ast_assert(endpoint != NULL);

	ruri = rdata->msg_info.msg->line.req.uri;
	if (!PJSIP_URI_SCHEME_IS_SIP(ruri) && !PJSIP_URI_SCHEME_IS_SIPS(ruri)) {
		send_options_response(rdata, dlg, 416);
		return -1;
	}
	
	sip_ruri = pjsip_uri_get_uri(ruri);
	ast_copy_pj_str(exten, &sip_ruri->user, sizeof(exten));

	if (ast_shutting_down()) {
		send_options_response(rdata, dlg, 503);
	} else if (!ast_exists_extension(NULL, endpoint->context, exten, 1, NULL)) {
		send_options_response(rdata, dlg, 404);
	} else {
		send_options_response(rdata, dlg, 200);
	}
	return PJ_TRUE;
}

static pj_bool_t options_module_on_rx_response(pjsip_rx_data *rdata)
{

	return PJ_SUCCESS;
}

static int qualify_info_hash_fn(const void *obj, int flags)
{
	const struct qualify_info *info = obj;
	const char *endpoint_id = flags & OBJ_KEY ? obj : info->endpoint_id;

	return ast_str_hash(endpoint_id);
}

static int qualify_info_cmp_fn(void *obj, void *arg, int flags)
{
	struct qualify_info *left = obj;
	struct qualify_info *right = arg;
	const char *right_endpoint_id = flags & OBJ_KEY ? arg : right->endpoint_id;

	return strcmp(left->endpoint_id, right_endpoint_id) ? 0 : CMP_MATCH | CMP_STOP;
}


static void qualify_info_destructor(void *obj)
{
	struct qualify_info *info = obj;
	if (!info) {
		return;
	}
	ast_string_field_free_memory(info);
	/* Cancel the qualify */
	if (!AST_SCHED_DEL(sched, info->scheduler_id)) {
		/* If we successfully deleted the qualify, we got it before it
		 * fired. We can safely delete the data that was passed to it.
		 * Otherwise, we're getting deleted while this is firing - don't
		 * touch that memory!
		 */
		ast_free(info->scheduler_data);
	}
}

static struct qualify_info *create_qualify_info(struct ast_sip_endpoint *endpoint)
{
	struct qualify_info *info;

	info = ao2_alloc(sizeof(*info), qualify_info_destructor);
	if (!info) {
		return NULL;
	}

	if (ast_string_field_init(info, 64)) {
		ao2_ref(info, -1);
		return NULL;
	}
	ast_string_field_set(info, endpoint_id, ast_sorcery_object_get_id(endpoint));

	return info;
}

static int send_qualify_request(void *data)
{
	struct ast_sip_endpoint *endpoint = data;
	pjsip_tx_data *tdata;
	/* YAY! Send an OPTIONS request. */

	ast_sip_create_request("OPTIONS", NULL, endpoint, NULL, &tdata);
	ast_sip_send_request(tdata, NULL, endpoint);

	ao2_cleanup(endpoint);
	return 0;
}

static int qualify_endpoint_scheduler_cb(const void *data)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	struct ast_sorcery *sorcery;
	char *endpoint_id = (char *)data;

	sorcery = ast_sip_get_sorcery();
	if (!sorcery) {
		ast_free(endpoint_id);
		return 0;
	}

	endpoint = ast_sorcery_retrieve_by_id(sorcery, "endpoint", endpoint_id);
	if (!endpoint) {
		/* Whoops, endpoint went away */
		ast_free(endpoint_id);
		return 0;
	}

	ast_sip_push_task(NULL, send_qualify_request, endpoint);

	return 1;
}

static void schedule_qualifies(void)
{
	RAII_VAR(struct ao2_container *, endpoints, NULL, ao2_cleanup);
	struct ao2_iterator it_endpoints;
	struct ast_sip_endpoint *endpoint;
	struct qualify_info *info;
	char *endpoint_id;

	endpoints = ast_res_sip_get_endpoints();
	if (!endpoints) {
		return;
	}

	it_endpoints = ao2_iterator_init(endpoints, 0);
	while ((endpoint = ao2_iterator_next(&it_endpoints))) {
		if (endpoint->qualify_frequency) {
			/* XXX TODO: This really should only qualify registered peers,
			 * which means we need a registrar. We should check the
			 * registrar to see if this endpoint has registered and, if
			 * not, pass on it.
			 *
			 * Actually, all of this should just get moved into the registrar.
			 * Otherwise, the registar will have to kick this off when a
			 * new endpoint registers, so it just makes sense to have it
			 * all live there.
			 */
			info = create_qualify_info(endpoint);
			if (!info) {
				ao2_ref(endpoint, -1);
				break;
			}
			endpoint_id = ast_strdup(info->endpoint_id);
			if (!endpoint_id) {
				ao2_t_ref(info, -1, "Dispose of info on off nominal");
				ao2_ref(endpoint, -1);
				break;
			}
			info->scheduler_data = endpoint_id;
			info->scheduler_id = ast_sched_add_variable(sched, endpoint->qualify_frequency * 1000, qualify_endpoint_scheduler_cb, endpoint_id, 1);
			ao2_t_link(scheduled_qualifies, info, "Link scheduled qualify information into container");
			ao2_t_ref(info, -1, "Dispose of creation ref");
		}
		ao2_t_ref(endpoint, -1, "Dispose of iterator ref");
	}
	ao2_iterator_destroy(&it_endpoints);
}

static char *send_options(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	const char *endpoint_name;
	pjsip_tx_data *tdata;

	switch (cmd) {
	case CLI_INIT:
		e->command = "sip send options";
		e->usage =
			"Usage: sip send options <endpoint>\n"
			"       Send a SIP OPTIONS request to the specified endpoint.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	endpoint_name = a->argv[3];

	endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint", endpoint_name);
	if (!endpoint) {
		ast_log(LOG_ERROR, "Unable to retrieve endpoint %s\n", endpoint_name);
		return CLI_FAILURE;
	}

	if (ast_sip_create_request("OPTIONS", NULL, endpoint, NULL, &tdata)) {
		ast_log(LOG_ERROR, "Unable to create OPTIONS request to endpoint %s\n", endpoint_name);
		return CLI_FAILURE;
	}

	if (ast_sip_send_request(tdata, NULL, endpoint)) {
		ast_log(LOG_ERROR, "Unable to send OPTIONS request to endpoint %s\n", endpoint_name);
		return CLI_FAILURE;
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_options[] = {
	AST_CLI_DEFINE(send_options, "Send an OPTIONS requst to an arbitrary SIP URI"),
};

int ast_res_sip_init_options_handling(int reload)
{
	const pj_str_t STR_OPTIONS = { "OPTIONS", 7 };

	if (scheduled_qualifies) {
		ao2_t_ref(scheduled_qualifies, -1, "Remove old scheduled qualifies");
	}
	scheduled_qualifies = ao2_t_container_alloc(QUALIFIED_BUCKETS, qualify_info_hash_fn, qualify_info_cmp_fn, "Create container for scheduled qualifies");
	if (!scheduled_qualifies) {
		return -1;
	}

	if (reload) {
		return 0;
	}

	if (pjsip_endpt_register_module(ast_sip_get_pjsip_endpoint(), &options_module) != PJ_SUCCESS) {
		options_module_stop();
		return -1;
	}

	if (pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(), NULL, PJSIP_H_ALLOW, NULL, 1, &STR_OPTIONS) != PJ_SUCCESS) {
		pjsip_endpt_unregister_module(ast_sip_get_pjsip_endpoint(), &options_module);
		return -1;
	}

	ast_cli_register_multiple(cli_options, ARRAY_LEN(cli_options));

	schedule_qualifies();

	return 0;
}
