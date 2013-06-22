/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Kevin Harwell <kharwell@digium.com>
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
	<depend>res_sip_session</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include "pjsua-lib/pjsua.h"

#include "asterisk/message.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/res_sip.h"
#include "asterisk/res_sip_session.h"

const pjsip_method pjsip_message_method = {PJSIP_OTHER_METHOD, {"MESSAGE", 7} };

#define MAX_HDR_SIZE 512
#define MAX_BODY_SIZE 1024
#define MAX_EXTEN_SIZE 256
#define MAX_USER_SIZE 128

/*!
 * \internal
 * \brief Determine where in the dialplan a call should go
 *
 * \details This uses the username in the request URI to try to match
 * an extension in an endpoint's context in order to route the call.
 *
 * \param rdata The SIP request
 * \param context The context to use
 * \param exten The extension to use
 */
static enum pjsip_status_code get_destination(const pjsip_rx_data *rdata, const char *context, char *exten)
{
	pjsip_uri *ruri = rdata->msg_info.msg->line.req.uri;
	pjsip_sip_uri *sip_ruri;

	if (!PJSIP_URI_SCHEME_IS_SIP(ruri) && !PJSIP_URI_SCHEME_IS_SIPS(ruri)) {
		return PJSIP_SC_UNSUPPORTED_URI_SCHEME;
	}

	sip_ruri = pjsip_uri_get_uri(ruri);
	ast_copy_pj_str(exten, &sip_ruri->user, MAX_EXTEN_SIZE);

	if (ast_exists_extension(NULL, context, exten, 1, NULL)) {
		return PJSIP_SC_OK;
	}
	return PJSIP_SC_NOT_FOUND;
}

/*!
 * \internal
 * \brief Checks to make sure the request has the correct content type.
 *
 * \details This module supports the following media types: "text/plain".
 * Return unsupported otherwise.
 *
 * \param rdata The SIP request
 */
static enum pjsip_status_code check_content_type(const pjsip_rx_data *rdata)
{
	if (ast_sip_is_content_type(&rdata->msg_info.msg->body->content_type,
				    "text",
				    "plain")) {
		return PJSIP_SC_OK;
	} else {
		return PJSIP_SC_UNSUPPORTED_MEDIA_TYPE;
	}
}

/*!
 * \internal
 * \brief Puts pointer past 'sip[s]:' string that should be at the
 * front of the given 'fromto' parameter
 *
 * \param fromto 'From' or 'To' field containing 'sip:'
 */
static const char* skip_sip(const char *fromto)
{
	const char *p;

	/* need to be one past 'sip:' or 'sips:' */
	if (!(p = strstr(fromto, "sip"))) {
		return fromto;
	}

	p += 3;
	if (*p == 's') {
		++p;
	}
	return ++p;
}

/*!
 * \internal
 * \brief Retrieves an endpoint if specified in the given 'fromto'
 *
 * Expects the given 'fromto' to be in one of the following formats:
 *      sip[s]:endpoint[/aor]
 *      sip[s]:endpoint[/uri]
 *
 * If an optional aor is given it will try to find an associated uri
 * to return.  If an optional uri is given then that will be returned,
 * otherwise uri will be NULL.
 *
 * \param fromto 'From' or 'To' field with possible endpoint
 * \param uri Optional uri to return
 */
static struct ast_sip_endpoint* get_endpoint(const char *fromto, char **uri)
{
	const char *name = skip_sip(fromto);
	struct ast_sip_endpoint* endpoint;
	struct ast_sip_aor *aor;

	if ((*uri = strchr(name, '/'))) {
		*(*uri)++ = '\0';
	}

	/* endpoint is required */
	if (ast_strlen_zero(name)) {
		return NULL;
	}

	if (!(endpoint = ast_sorcery_retrieve_by_id(
		      ast_sip_get_sorcery(), "endpoint", name))) {
		return NULL;
	}

	if (*uri && (aor = ast_sip_location_retrieve_aor(*uri))) {
		*uri = (char*)ast_sip_location_retrieve_first_aor_contact(aor)->uri;
	}

	return endpoint;
}

/*!
 * \internal
 * \brief Updates fields in an outgoing 'From' header.
 *
 * \param tdata The outgoing message data structure
 * \param from Info to potentially copy into the 'From' header
 */
static void update_from(pjsip_tx_data *tdata, const char *from)
{
	/* static const pj_str_t hname = { "From", 4 }; */
	pjsip_name_addr *from_name_addr;
	pjsip_sip_uri *from_uri;
	pjsip_uri *parsed;
	char *uri;

	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);

	if (ast_strlen_zero(from)) {
		return;
	}

	if (!(endpoint = get_endpoint(from, &uri))) {
		return;
	}

	if (ast_strlen_zero(uri)) {
		/* if no aor/uri was specified get one from the endpoint */
		uri = (char*)ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors)->uri;
	}

	/* get current 'from' hdr & uri - going to overwrite some fields */
	from_name_addr = (pjsip_name_addr *)PJSIP_MSG_FROM_HDR(tdata->msg)->uri;
	from_uri = pjsip_uri_get_uri(from_name_addr);

	/* check to see if uri is in 'name <sip:user@domain>' format */
	if ((parsed = pjsip_parse_uri(tdata->pool, uri, strlen(uri), PJSIP_PARSE_URI_AS_NAMEADDR))) {
		pjsip_name_addr *name_addr = (pjsip_name_addr *)parsed;
		pjsip_sip_uri *sip_uri = pjsip_uri_get_uri(name_addr->uri);

		pj_strdup(tdata->pool, &from_name_addr->display, &name_addr->display);
		pj_strdup(tdata->pool, &from_uri->user, &sip_uri->user);
		pj_strdup(tdata->pool, &from_uri->host, &sip_uri->host);
		from_uri->port = sip_uri->port;
	} else {
		/* assume it is 'user[@domain]' format */
		char *domain = strchr(uri, '@');
		if (domain) {
			*domain++ = '\0';
			pj_strdup2(tdata->pool, &from_uri->host, domain);
		}
		pj_strdup2(tdata->pool, &from_uri->user, uri);
	}
}

/*!
 * \internal
 * \brief Checks if the given msg var name should be blocked.
 *
 * \details Some headers are not allowed to be overriden by the user.
 *  Determine if the given var header name from the user is blocked for
 *  an outgoing MESSAGE.
 *
 * \param name name of header to see if it is blocked.
 *
 * \retval TRUE if the given header is blocked.
 */
static int is_msg_var_blocked(const char *name)
{
	int i;

	/*
	 * Don't block Content-Type or Max-Forwards headers because the
	 * user can override them.
	 */
	static const char *hdr[] = {
		"To",
		"From",
		"Via",
		"Route",
		"Contact",
		"Call-ID",
		"CSeq",
		"Allow",
		"Content-Length",
		"Request-URI",
	};

	for (i = 0; i < ARRAY_LEN(hdr); ++i) {
		if (!strcasecmp(name, hdr[i])) {
			/* Block addition of this header. */
			return 1;
		}
	}
	return 0;
}

/*!
 * \internal
 * \brief Copies any other msg vars over to the request headers.
 *
 * \param msg The msg structure to copy headers from
 * \param tdata The SIP transmission data
 */
static enum pjsip_status_code vars_to_headers(const struct ast_msg *msg, pjsip_tx_data *tdata)
{
	const char *name;
	const char *value;
	int max_forwards;

	RAII_VAR(struct ast_msg_var_iterator *, i, ast_msg_var_iterator_init(msg), ast_msg_var_iterator_destroy);
	while (ast_msg_var_iterator_next(msg, i, &name, &value)) {
		if (!strcasecmp(name, "Max-Forwards")) {
			/* Decrement Max-Forwards for SIP loop prevention. */
			if (sscanf(value, "%30d", &max_forwards) != 1 || --max_forwards == 0) {
				ast_log(LOG_NOTICE, "MESSAGE(Max-Forwards) reached zero.  MESSAGE not sent.\n");
				return -1;
			}
			sprintf((char*)value, "%d", max_forwards);
			ast_sip_add_header(tdata, name, value);
		}
		else if (!is_msg_var_blocked(name)) {
			ast_sip_add_header(tdata, name, value);
		}
		ast_msg_var_unref_current(i);
	}
	return PJSIP_SC_OK;
}

/*!
 * \internal  
 * \brief Copies any other request header data over to ast_msg structure.
 *
 * \param rdata The SIP request
 * \param msg The msg structure to copy headers into
 */
static int headers_to_vars(const pjsip_rx_data *rdata, struct ast_msg *msg)
{
	char *c;
	char buf[MAX_HDR_SIZE];
	int res = 0;
	pjsip_hdr *h = rdata->msg_info.msg->hdr.next;
	pjsip_hdr *end= &rdata->msg_info.msg->hdr;

	while (h != end) {
		if ((res = pjsip_hdr_print_on(h, buf, sizeof(buf)-1)) > 0) {
			buf[res] = '\0';
			if ((c = strchr(buf, ':'))) {
				ast_copy_string(buf, ast_skip_blanks(c + 1), sizeof(buf)-(c-buf));
			}

			if ((res = ast_msg_set_var(msg, pj_strbuf(&h->name), buf)) != 0) {
				break;
			}
		}
		h = h->next;
	}
	return 0;
}

/*!
 * \internal
 * \brief Prints the message body into the given char buffer.
 *
 * \details Copies body content from the received data into the given
 * character buffer removing any extra carriage return/line feeds.
 *
 * \param rdata The SIP request
 * \param buf Buffer to fill
 * \param len The length of the buffer
 */
static int print_body(pjsip_rx_data *rdata, char *buf, int len)
{
	int res = rdata->msg_info.msg->body->print_body(
		rdata->msg_info.msg->body, buf, len);

	if (res < 0) {
		return res;
	}

	/* remove any trailing carriage return/line feeds */
	while (res > 0 && ((buf[--res] == '\r') || (buf[res] == '\n')));

	buf[++res] = '\0';

	return res;
}

/*!
 * \internal
 * \brief Converts a pjsip_rx_data structure to an ast_msg structure.
 *
 * \details Attempts to fill in as much information as possible into the given
 * msg structure copied from the given request data.
 *
 * \param rdata The SIP request
 * \param msg The asterisk message structure to fill in.
 */
static enum pjsip_status_code rx_data_to_ast_msg(pjsip_rx_data *rdata, struct ast_msg *msg)
{

#define CHECK_RES(z_) do { if (z_) { ast_msg_destroy(msg); \
		return PJSIP_SC_INTERNAL_SERVER_ERROR; } } while (0)

	int size;
	char buf[MAX_BODY_SIZE];
	pjsip_name_addr *name_addr;
	const char *field;
	pjsip_status_code code;
	struct ast_sip_endpoint *endpt = ast_pjsip_rdata_get_endpoint(rdata);

	/* make sure there is an appropriate context and extension*/
	if ((code = get_destination(rdata, endpt->context, buf)) != PJSIP_SC_OK) {
		return code;
	}

	CHECK_RES(ast_msg_set_context(msg, "%s", endpt->context));
	CHECK_RES(ast_msg_set_exten(msg, "%s", buf));

	/* to header */
	name_addr = (pjsip_name_addr *)rdata->msg_info.to->uri;
	if ((size = pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, name_addr, buf, sizeof(buf)-1)) > 0) {
		buf[size] = '\0';
		CHECK_RES(ast_msg_set_to(msg, "%s", buf));
	}

	/* from header */
	name_addr = (pjsip_name_addr *)rdata->msg_info.from->uri;
	if ((size = pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, name_addr, buf, sizeof(buf)-1)) > 0) {
		buf[size] = '\0';
		CHECK_RES(ast_msg_set_from(msg, "%s", buf));
	}

	/* contact header */
	if ((size = pjsip_hdr_print_on(pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL), buf, sizeof(buf)-1)) > 0) {
		buf[size] = '\0';
		CHECK_RES(ast_msg_set_var(msg, "SIP_FULLCONTACT", buf));
	}

	/* receive address */
	field = pj_sockaddr_print(&rdata->pkt_info.src_addr, buf, sizeof(buf)-1, 1);
	CHECK_RES(ast_msg_set_var(msg, "SIP_RECVADDR", field));

	/* body */
	if (print_body(rdata, buf, sizeof(buf) - 1) > 0) {
		CHECK_RES(ast_msg_set_body(msg, "%s", buf));
	}

	/* endpoint name */
	if (endpt->id.name.valid) {
		CHECK_RES(ast_msg_set_var(msg, "SIP_PEERNAME", endpt->id.name.str));
	}

	CHECK_RES(headers_to_vars(rdata, msg));

	return PJSIP_SC_OK;
}

struct msg_data {
	struct ast_msg *msg;
        char *to;
	char *from;
};

static void msg_data_destroy(void *obj)
{
	struct msg_data *mdata = obj;

	ast_free(mdata->from);
	ast_free(mdata->to);

	ast_msg_destroy(mdata->msg);
}

static struct msg_data* msg_data_create(const struct ast_msg *msg, const char *to, const char *from)
{
	char *tag;
	struct msg_data *mdata = ao2_alloc(sizeof(*mdata), msg_data_destroy);

	if (!mdata) {
		return NULL;
	}

	/* typecast to suppress const warning */
	mdata->msg = ast_msg_ref((struct ast_msg*)msg);

	mdata->to = ast_strdup(to);
	mdata->from = ast_strdup(from);

	/* sometimes from can still contain the tag at this point, so remove it */
	if ((tag = strchr(mdata->from, ';'))) {
		*tag = '\0';
	}

	return mdata;
}

static int msg_send(void *data)
{
	RAII_VAR(struct msg_data *, mdata, data, ao2_cleanup);

	const struct ast_sip_body body = {
		.type = "text",
		.subtype = "plain",
		.body_text = ast_msg_get_body(mdata->msg)
	};

	pjsip_tx_data *tdata;
	char *uri;

	RAII_VAR(struct ast_sip_endpoint *, endpoint, get_endpoint(
			 mdata->to, &uri), ao2_cleanup);
	if (!endpoint) {
		ast_log(LOG_ERROR, "SIP MESSAGE - Endpoint not found in %s\n", mdata->to);
		return -1;
	}

	if (ast_sip_create_request("MESSAGE", NULL, endpoint, uri, &tdata)) {
		ast_log(LOG_ERROR, "SIP MESSAGE - Could not create request\n");
		return -1;
	}

	if (ast_sip_add_body(tdata, &body)) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "SIP MESSAGE - Could not add body to request\n");
		return -1;
	}

	update_from(tdata, mdata->from);
	vars_to_headers(mdata->msg, tdata);
	if (ast_sip_send_request(tdata, NULL, endpoint)) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "SIP MESSAGE - Could not send request\n");
		return -1;
	}

	return PJ_SUCCESS;
}

static int sip_msg_send(const struct ast_msg *msg, const char *to, const char *from)
{
	struct msg_data *mdata;

	if (ast_strlen_zero(to)) {
		ast_log(LOG_ERROR, "SIP MESSAGE - a 'To' URI  must be specified\n");
		return -1;
	}

	if (!(mdata = msg_data_create(msg, to, from)) ||
	    ast_sip_push_task(NULL, msg_send, mdata)) {
		ao2_ref(mdata, -1);
		return -1;
	}
	return 0;
}

static const struct ast_msg_tech msg_tech = {
	.name = "sip",
	.msg_send = sip_msg_send,
};

static pj_status_t send_response(pjsip_rx_data *rdata, enum pjsip_status_code code,
				 pjsip_dialog *dlg, pjsip_transaction *tsx)
{
	pjsip_tx_data *tdata;
	pj_status_t status;
	pjsip_response_addr res_addr;

	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();

	status = pjsip_endpt_create_response(endpt, rdata, code, NULL, &tdata);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to create response (%d)\n", status);
		return status;
	}

	if (dlg && tsx) {
		status = pjsip_dlg_send_response(dlg, tsx, tdata);
	} else {
		/* Get where to send request. */
		status = pjsip_get_response_addr(tdata->pool, rdata, &res_addr);
		if (status != PJ_SUCCESS) {
			ast_log(LOG_ERROR, "Unable to get response address (%d)\n", status);
			return status;
		}
		status = pjsip_endpt_send_response(endpt, &res_addr, tdata, NULL, NULL);
	}

	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to send response (%d)\n", status);
	}

	return status;
}

static pj_bool_t module_on_rx_request(pjsip_rx_data *rdata)
{
	enum pjsip_status_code code;
	struct ast_msg *msg;

	/* if not a MESSAGE, don't handle */
	if (pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_message_method)) {
		return PJ_FALSE;
	}

	msg = ast_msg_alloc();
	if (!msg) {
		send_response(rdata, PJSIP_SC_INTERNAL_SERVER_ERROR, NULL, NULL);
		return PJ_TRUE;
	}

	if ((code = check_content_type(rdata)) != PJSIP_SC_OK) {
		send_response(rdata, code, NULL, NULL);
		return PJ_TRUE;
	}

	if ((code = rx_data_to_ast_msg(rdata, msg)) == PJSIP_SC_OK) {
		/* send it to the dialplan */
		ast_msg_queue(msg);
		code = PJSIP_SC_ACCEPTED;
	}

	send_response(rdata, code, NULL, NULL);
	return PJ_TRUE;
}

static int incoming_in_dialog_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	char buf[MAX_BODY_SIZE];
	enum pjsip_status_code code;
	struct ast_frame f;

	pjsip_dialog *dlg = session->inv_session->dlg;
	pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);

	if ((code = check_content_type(rdata)) != PJSIP_SC_OK) {
		send_response(rdata, code, dlg, tsx);
		return 0;
	}

	if (print_body(rdata, buf, sizeof(buf)-1) < 1) {
		/* invalid body size */
		return 0;
	}

	memset(&f, 0, sizeof(f));
	f.frametype = AST_FRAME_TEXT;
	f.subclass.integer = 0;
	f.offset = 0;
	f.data.ptr = buf;
	f.datalen = strlen(buf) + 1;
	ast_queue_frame(session->channel, &f);

	send_response(rdata, PJSIP_SC_ACCEPTED, dlg, tsx);
	return 0;
}

static struct ast_sip_session_supplement messaging_supplement = {
	.method = "MESSAGE",
	.incoming_request = incoming_in_dialog_request
};

static pjsip_module messaging_module = {
	.name = {"Messaging Module", 16},
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = module_on_rx_request,
};

static int load_module(void)
{
	if (ast_sip_register_service(&messaging_module) != PJ_SUCCESS) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pjsip_endpt_add_capability(ast_sip_get_pjsip_endpoint(),
				       NULL, PJSIP_H_ALLOW, NULL, 1,
				       &pjsip_message_method.name) != PJ_SUCCESS) {

		ast_sip_unregister_service(&messaging_module);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_msg_tech_register(&msg_tech)) {
		ast_sip_unregister_service(&messaging_module);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sip_session_register_supplement(&messaging_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_supplement(&messaging_supplement);
	ast_msg_tech_unregister(&msg_tech);
	ast_sip_unregister_service(&messaging_module);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "SIP Messaging Support",
		.load = load_module,
		.unload = unload_module,
		.load_pri = AST_MODPRI_APP_DEPEND,
	       );
