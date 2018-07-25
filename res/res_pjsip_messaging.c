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
	<depend>res_pjsip</depend>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<info name="MessageFromInfo" language="en_US" tech="PJSIP">
		<para>The <literal>from</literal> parameter can be a configured endpoint
		or in the form of "display-name" &lt;URI&gt;.</para>
	</info>
	<info name="MessageToInfo" language="en_US" tech="PJSIP">
		<para>Specifying a prefix of <literal>pjsip:</literal> will send the
		message as a SIP MESSAGE request.</para>
	</info>
 ***/
#include "asterisk.h"

#include "pjsua-lib/pjsua.h"

#include "asterisk/message.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/taskprocessor.h"

const pjsip_method pjsip_message_method = {PJSIP_OTHER_METHOD, {"MESSAGE", 7} };

#define MAX_HDR_SIZE 512
#define MAX_BODY_SIZE 1024
#define MAX_USER_SIZE 128

static struct ast_taskprocessor *message_serializer;

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
	int res;
	if (rdata->msg_info.msg->body && rdata->msg_info.msg->body->len) {
		res = ast_sip_is_content_type(
			&rdata->msg_info.msg->body->content_type, "text", "plain");
	} else {
		res = rdata->msg_info.ctype &&
			ast_sip_is_content_type(
				&rdata->msg_info.ctype->media, "text", "plain");
	}

	return res ? PJSIP_SC_OK : PJSIP_SC_UNSUPPORTED_MEDIA_TYPE;
}

/*!
 * \internal
 * \brief Checks to make sure the request has the correct content type.
 *
 * \details This module supports the following media types: "text/\*", "application/\*".
 * Return unsupported otherwise.
 *
 * \param rdata The SIP request
 */
static enum pjsip_status_code check_content_type_in_dialog(const pjsip_rx_data *rdata)
{
	int res = PJSIP_SC_UNSUPPORTED_MEDIA_TYPE;
	static const pj_str_t text = { "text", 4};
	static const pj_str_t application = { "application", 11};

	/* We'll accept any text/ or application/ content type */
	if (rdata->msg_info.msg->body && rdata->msg_info.msg->body->len
		&& (pj_stricmp(&rdata->msg_info.msg->body->content_type.type, &text) == 0
			|| pj_stricmp(&rdata->msg_info.msg->body->content_type.type, &application) == 0)) {
		res = PJSIP_SC_OK;
	} else if (rdata->msg_info.ctype
		&& (pj_stricmp(&rdata->msg_info.ctype->media.type, &text) == 0
		|| pj_stricmp(&rdata->msg_info.ctype->media.type, &application) == 0)) {
		res = PJSIP_SC_OK;
	}

	return res;
}

/*!
 * \internal
 * \brief Puts pointer past 'sip[s]:' string that should be at the
 * front of the given 'fromto' parameter
 *
 * \param fromto 'From' or 'To' field containing 'sip:'
 */
static const char *skip_sip(const char *fromto)
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
 * \brief Retrieves an endpoint if specified in the given 'to'
 *
 * Expects the given 'to' to be in one of the following formats:
 *      sip[s]:endpoint[/aor]
 *      sip[s]:endpoint[/uri] - Where uri is: sip[s]:user@domain
 *      sip[s]:endpoint[@domain]
 *      sip[s]:unknown_user@domain <-- will use default outbound endpoint
 *
 * If an optional aor is given it will try to find an associated uri
 * to return.  If an optional uri is given then that will be returned,
 * otherwise uri will be NULL.
 *
 * \param to 'From' or 'To' field with possible endpoint
 * \param uri Optional uri to return
 */
static struct ast_sip_endpoint *get_outbound_endpoint(const char *to, char **uri)
{
	char *name;
	char *aor_uri;
	struct ast_sip_endpoint *endpoint;

	name = ast_strdupa(skip_sip(to));

	/* attempt to extract the endpoint name */
	if ((aor_uri = strchr(name, '/'))) {
		/* format was 'endpoint/(aor_name | uri)' */
		*aor_uri++ = '\0';
	} else if ((aor_uri = strchr(name, '@'))) {
		/* format was 'endpoint@domain' - discard the domain */
		*aor_uri = '\0';

		/*
		 * We may want to match without any user options getting
		 * in the way.
		 */
		AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(name);
	}

	/* at this point, if name is not empty then it
	   might be an endpoint, so try to retrieve it */
	if (ast_strlen_zero(name)
		|| !(endpoint = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "endpoint",
			name))) {
		/* an endpoint was not found, so assume sending directly
		   to a uri and use the default outbound endpoint */
		*uri = ast_strdup(to);
		return ast_sip_default_outbound_endpoint();
	}

	if (ast_strlen_zero(aor_uri)) {
		*uri = NULL;
	} else {
		struct ast_sip_aor *aor;
		struct ast_sip_contact *contact = NULL;
		char *end;

		/* Trim off any stray angle bracket that shouldn't be here */
		end = strchr(aor_uri, '>');
		if (end) {
			*end = '\0';
		}

		/*
		 * if what's in 'uri' is a retrievable aor use the uri on it
		 * instead, otherwise assume what's there is already a uri
		 */
		aor = ast_sip_location_retrieve_aor(aor_uri);
		if (aor && (contact = ast_sip_location_retrieve_first_aor_contact(aor))) {
			aor_uri = (char *) contact->uri;
		}
		/* need to copy because underlying uri goes away */
		*uri = ast_strdup(aor_uri);

		ao2_cleanup(contact);
		ao2_cleanup(aor);
	}

	return endpoint;
}

/*!
 * \internal
 * \brief Overwrite fields in the outbound 'To' header
 *
 * Updates display name in an outgoing To header.
 *
 * \param tdata the outbound message data structure
 * \param to info to copy into the header
 */
static void update_to(pjsip_tx_data *tdata, char *to)
{
	pjsip_name_addr *parsed_name_addr;

	parsed_name_addr = (pjsip_name_addr *) pjsip_parse_uri(tdata->pool, to, strlen(to),
		PJSIP_PARSE_URI_AS_NAMEADDR);
	if (parsed_name_addr) {
		if (pj_strlen(&parsed_name_addr->display)) {
			pjsip_name_addr *name_addr =
				(pjsip_name_addr *) PJSIP_MSG_TO_HDR(tdata->msg)->uri;

			pj_strdup(tdata->pool, &name_addr->display, &parsed_name_addr->display);
		}
	}
}

/*!
 * \internal
 * \brief Overwrite fields in the outbound 'From' header
 *
 * The outbound 'From' header is created/added in ast_sip_create_request with
 * default data.  If available that data may be info specified in the 'from_user'
 * and 'from_domain' options found on the endpoint.  That information will be
 * overwritten with data in the given 'from' parameter.
 *
 * \param tdata the outbound message data structure
 * \param from info to copy into the header
 */
static void update_from(pjsip_tx_data *tdata, char *from)
{
	pjsip_name_addr *name_addr;
	pjsip_sip_uri *uri;
	pjsip_name_addr *parsed_name_addr;

	if (ast_strlen_zero(from)) {
		return;
	}

	name_addr = (pjsip_name_addr *) PJSIP_MSG_FROM_HDR(tdata->msg)->uri;
	uri = pjsip_uri_get_uri(name_addr);

	parsed_name_addr = (pjsip_name_addr *) pjsip_parse_uri(tdata->pool, from,
		strlen(from), PJSIP_PARSE_URI_AS_NAMEADDR);
	if (parsed_name_addr) {
		pjsip_sip_uri *parsed_uri;

		if (!PJSIP_URI_SCHEME_IS_SIP(parsed_name_addr->uri)
				&& !PJSIP_URI_SCHEME_IS_SIPS(parsed_name_addr->uri)) {
			ast_log(LOG_WARNING, "From address '%s' is not a valid SIP/SIPS URI\n", from);
			return;
		}

		parsed_uri = pjsip_uri_get_uri(parsed_name_addr->uri);

		if (pj_strlen(&parsed_name_addr->display)) {
			pj_strdup(tdata->pool, &name_addr->display, &parsed_name_addr->display);
		}

		pj_strdup(tdata->pool, &uri->user, &parsed_uri->user);
		pj_strdup(tdata->pool, &uri->host, &parsed_uri->host);
		uri->port = parsed_uri->port;
	} else {
		/* assume it is 'user[@domain]' format */
		char *domain = strchr(from, '@');

		if (domain) {
			pj_str_t pj_from;

			pj_strset3(&pj_from, from, domain);
			pj_strdup(tdata->pool, &uri->user, &pj_from);

			pj_strdup2(tdata->pool, &uri->host, domain + 1);
		} else {
			pj_strdup2(tdata->pool, &uri->user, from);
		}
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
	struct ast_msg_var_iterator *iter;

	for (iter = ast_msg_var_iterator_init(msg);
		ast_msg_var_iterator_next(msg, iter, &name, &value);
		ast_msg_var_unref_current(iter)) {
		if (!strcasecmp(name, "Max-Forwards")) {
			/* Decrement Max-Forwards for SIP loop prevention. */
			if (sscanf(value, "%30d", &max_forwards) != 1 || --max_forwards == 0) {
				ast_msg_var_iterator_destroy(iter);
				ast_log(LOG_NOTICE, "MESSAGE(Max-Forwards) reached zero.  MESSAGE not sent.\n");
				return -1;
			}
			sprintf((char *) value, "%d", max_forwards);
			ast_sip_add_header(tdata, name, value);
		} else if (!is_msg_var_blocked(name)) {
			ast_sip_add_header(tdata, name, value);
		}
	}
	ast_msg_var_iterator_destroy(iter);

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
	char name[MAX_HDR_SIZE];
	char buf[MAX_HDR_SIZE];
	int res = 0;
	pjsip_hdr *h = rdata->msg_info.msg->hdr.next;
	pjsip_hdr *end= &rdata->msg_info.msg->hdr;

	while (h != end) {
		if ((res = pjsip_hdr_print_on(h, buf, sizeof(buf)-1)) > 0) {
			buf[res] = '\0';
			if ((c = strchr(buf, ':'))) {
				ast_copy_string(buf, ast_skip_blanks(c + 1), sizeof(buf));
			}

			ast_copy_pj_str(name, &h->name, sizeof(name));
			if ((res = ast_msg_set_var(msg, name, buf)) != 0) {
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
	int res;

	if (!rdata->msg_info.msg->body || !rdata->msg_info.msg->body->len) {
		return 0;
	}

	if ((res = rdata->msg_info.msg->body->print_body(
		     rdata->msg_info.msg->body, buf, len)) < 0) {
		return res;
	}

	/* remove any trailing carriage return/line feeds */
	while (res > 0 && ((buf[--res] == '\r') || (buf[res] == '\n')));

	buf[++res] = '\0';

	return res;
}

/*!
 * \internal
 * \brief Converts a 'sip:' uri to a 'pjsip:' so it can be found by
 * the message tech.
 *
 * \param buf uri to insert 'pjsip' into
 * \param size length of the uri in buf
 * \param capacity total size of buf
 */
static char *sip_to_pjsip(char *buf, int size, int capacity)
{
	int count;
	const char *scheme;
	char *res = buf;

	/* remove any wrapping brackets */
	if (*buf == '<') {
		++buf;
		--size;
	}

	scheme = strncmp(buf, "sip", 3) ? "pjsip:" : "pj";
	count = strlen(scheme);
	if (count + size >= capacity) {
		ast_log(LOG_WARNING, "Unable to handle MESSAGE- incoming uri "
			"too large for given buffer\n");
		return NULL;
	}

	memmove(res + count, buf, size);
	memcpy(res, scheme, count);

	buf += size - 1;
	if (*buf == '>') {
		*buf = '\0';
	}

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
	RAII_VAR(struct ast_sip_endpoint *, endpt, NULL, ao2_cleanup);
	pjsip_uri *ruri = rdata->msg_info.msg->line.req.uri;
	pjsip_sip_uri *sip_ruri;
	pjsip_name_addr *name_addr;
	char buf[MAX_BODY_SIZE];
	const char *field;
	const char *context;
	char exten[AST_MAX_EXTENSION];
	int res = 0;
	int size;

	if (!PJSIP_URI_SCHEME_IS_SIP(ruri) && !PJSIP_URI_SCHEME_IS_SIPS(ruri)) {
		return PJSIP_SC_UNSUPPORTED_URI_SCHEME;
	}

	sip_ruri = pjsip_uri_get_uri(ruri);
	ast_copy_pj_str(exten, &sip_ruri->user, AST_MAX_EXTENSION);

	/*
	 * We may want to match in the dialplan without any user
	 * options getting in the way.
	 */
	AST_SIP_USER_OPTIONS_TRUNCATE_CHECK(exten);

	endpt = ast_pjsip_rdata_get_endpoint(rdata);
	ast_assert(endpt != NULL);

	context = S_OR(endpt->message_context, endpt->context);
	res |= ast_msg_set_context(msg, "%s", context);
	res |= ast_msg_set_exten(msg, "%s", exten);

	/* to header */
	name_addr = (pjsip_name_addr *)rdata->msg_info.to->uri;
	size = pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, name_addr, buf, sizeof(buf) - 1);
	if (size <= 0) {
		return PJSIP_SC_INTERNAL_SERVER_ERROR;
	}
	buf[size] = '\0';
	res |= ast_msg_set_to(msg, "%s", sip_to_pjsip(buf, ++size, sizeof(buf) - 1));

	/* from header */
	name_addr = (pjsip_name_addr *)rdata->msg_info.from->uri;
	size = pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, name_addr, buf, sizeof(buf) - 1);
	if (size <= 0) {
		return PJSIP_SC_INTERNAL_SERVER_ERROR;
	}
	buf[size] = '\0';
	res |= ast_msg_set_from(msg, "%s", buf);

	field = pj_sockaddr_print(&rdata->pkt_info.src_addr, buf, sizeof(buf) - 1, 3);
	res |= ast_msg_set_var(msg, "PJSIP_RECVADDR", field);

	switch (rdata->tp_info.transport->key.type) {
	case PJSIP_TRANSPORT_UDP:
	case PJSIP_TRANSPORT_UDP6:
		field = "udp";
		break;
	case PJSIP_TRANSPORT_TCP:
	case PJSIP_TRANSPORT_TCP6:
		field = "tcp";
		break;
	case PJSIP_TRANSPORT_TLS:
	case PJSIP_TRANSPORT_TLS6:
		field = "tls";
		break;
	default:
		field = rdata->tp_info.transport->type_name;
	}
	ast_msg_set_var(msg, "PJSIP_TRANSPORT", field);

	if (print_body(rdata, buf, sizeof(buf) - 1) > 0) {
		res |= ast_msg_set_body(msg, "%s", buf);
	}

	/* endpoint name */
	res |= ast_msg_set_tech(msg, "%s", "PJSIP");
	res |= ast_msg_set_endpoint(msg, "%s", ast_sorcery_object_get_id(endpt));
	if (endpt->id.self.name.valid) {
		res |= ast_msg_set_var(msg, "PJSIP_ENDPOINT", endpt->id.self.name.str);
	}

	res |= headers_to_vars(rdata, msg);

	return !res ? PJSIP_SC_OK : PJSIP_SC_INTERNAL_SERVER_ERROR;
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

static struct msg_data *msg_data_create(const struct ast_msg *msg, const char *to, const char *from)
{
	char *uri_params;
	struct msg_data *mdata = ao2_alloc(sizeof(*mdata), msg_data_destroy);

	if (!mdata) {
		return NULL;
	}

	/* typecast to suppress const warning */
	mdata->msg = ast_msg_ref((struct ast_msg *) msg);

	/* To starts with 'pjsip:' which needs to be removed. */
	if (!(to = strchr(to, ':'))) {
		ao2_ref(mdata, -1);
		return NULL;
	}
	++to;/* Now skip the ':' */

	/* Make sure we start with sip: */
	mdata->to = ast_begins_with(to, "sip:") ? ast_strdup(to) : ast_strdup(to - 4);
	mdata->from = ast_strdup(from);
	if (!mdata->to || !mdata->from) {
		ao2_ref(mdata, -1);
		return NULL;
	}

	/*
	 * Sometimes from URI can contain URI parameters, so remove them.
	 *
	 * sip:user;user-options@domain;uri-parameters
	 */
	uri_params = strchr(mdata->from, '@');
	if (uri_params && (uri_params = strchr(mdata->from, ';'))) {
		*uri_params = '\0';
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
	RAII_VAR(char *, uri, NULL, ast_free);
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);

	endpoint = get_outbound_endpoint(mdata->to, &uri);
	if (!endpoint) {
		ast_log(LOG_ERROR,
			"PJSIP MESSAGE - Could not find endpoint '%s' and no default outbound endpoint configured\n",
			mdata->to);
		return -1;
	}

	if (ast_sip_create_request("MESSAGE", NULL, endpoint, uri, NULL, &tdata)) {
		ast_log(LOG_WARNING, "PJSIP MESSAGE - Could not create request\n");
		return -1;
	}

	update_to(tdata, mdata->to);
	update_from(tdata, mdata->from);

	if (ast_sip_add_body(tdata, &body)) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "PJSIP MESSAGE - Could not add body to request\n");
		return -1;
	}

	vars_to_headers(mdata->msg, tdata);

	ast_debug(1, "Sending message to '%s' (via endpoint %s) from '%s'\n",
		mdata->to, ast_sorcery_object_get_id(endpoint), mdata->from);

	if (ast_sip_send_request(tdata, NULL, endpoint, NULL, NULL)) {
		ast_log(LOG_ERROR, "PJSIP MESSAGE - Could not send request\n");
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
	    ast_sip_push_task(message_serializer, msg_send, mdata)) {
		ao2_cleanup(mdata);
		return -1;
	}
	return 0;
}

static const struct ast_msg_tech msg_tech = {
	.name = "pjsip",
	.msg_send = sip_msg_send,
};

static pj_status_t send_response(pjsip_rx_data *rdata, enum pjsip_status_code code,
				 pjsip_dialog *dlg, pjsip_transaction *tsx)
{
	pjsip_tx_data *tdata;
	pj_status_t status;

	status = ast_sip_create_response(rdata, code, NULL, &tdata);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to create response (%d)\n", status);
		return status;
	}

	if (dlg && tsx) {
		status = pjsip_dlg_send_response(dlg, tsx, tdata);
	} else {
		struct ast_sip_endpoint *endpoint;

		endpoint = ast_pjsip_rdata_get_endpoint(rdata);
		status = ast_sip_send_stateful_response(rdata, tdata, endpoint);
		ao2_cleanup(endpoint);
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

	code = check_content_type(rdata);
	if (code != PJSIP_SC_OK) {
		send_response(rdata, code, NULL, NULL);
		return PJ_TRUE;
	}

	msg = ast_msg_alloc();
	if (!msg) {
		send_response(rdata, PJSIP_SC_INTERNAL_SERVER_ERROR, NULL, NULL);
		return PJ_TRUE;
	}

	code = rx_data_to_ast_msg(rdata, msg);
	if (code != PJSIP_SC_OK) {
		send_response(rdata, code, NULL, NULL);
		ast_msg_destroy(msg);
		return PJ_TRUE;
	}

	if (!ast_msg_has_destination(msg)) {
		ast_debug(1, "MESSAGE request received, but no handler wanted it\n");
		send_response(rdata, PJSIP_SC_NOT_FOUND, NULL, NULL);
		ast_msg_destroy(msg);
		return PJ_TRUE;
	}

	/* Send it to the messaging core.
	 *
	 * If we are unable to send a response, the most likely reason is that we
	 * are handling a retransmission of an incoming MESSAGE and were unable to
	 * create a transaction due to a duplicate key. If we are unable to send
	 * a response, we should not queue the message to the dialplan
	 */
	if (!send_response(rdata, PJSIP_SC_ACCEPTED, NULL, NULL)) {
		ast_msg_queue(msg);
	}

	return PJ_TRUE;
}

static int incoming_in_dialog_request(struct ast_sip_session *session, struct pjsip_rx_data *rdata)
{
	enum pjsip_status_code code;
	int rc;
	pjsip_dialog *dlg = session->inv_session->dlg;
	pjsip_transaction *tsx = pjsip_rdata_get_tsx(rdata);
	struct ast_msg_data *msg;
	struct ast_party_caller *caller;
	pjsip_name_addr *name_addr;
	size_t from_len;
	size_t to_len;
	struct ast_msg_data_attribute attrs[4];
	int pos = 0;
	int body_pos;

	if (!session->channel) {
		send_response(rdata, PJSIP_SC_NOT_FOUND, dlg, tsx);
		return 0;
	}

	code = check_content_type_in_dialog(rdata);
	if (code != PJSIP_SC_OK) {
		send_response(rdata, code, dlg, tsx);
		return 0;
	}

	caller = ast_channel_caller(session->channel);

	name_addr = (pjsip_name_addr *) rdata->msg_info.from->uri;
	from_len = pj_strlen(&name_addr->display);
	if (from_len) {
		attrs[pos].type = AST_MSG_DATA_ATTR_FROM;
		from_len++;
		attrs[pos].value = ast_alloca(from_len);
		ast_copy_pj_str(attrs[pos].value, &name_addr->display, from_len);
		pos++;
	} else if (caller->id.name.valid && !ast_strlen_zero(caller->id.name.str)) {
		attrs[pos].type = AST_MSG_DATA_ATTR_FROM;
		attrs[pos].value = caller->id.name.str;
		pos++;
	}

	name_addr = (pjsip_name_addr *) rdata->msg_info.to->uri;
	to_len = pj_strlen(&name_addr->display);
	if (to_len) {
		attrs[pos].type = AST_MSG_DATA_ATTR_TO;
		to_len++;
		attrs[pos].value = ast_alloca(to_len);
		ast_copy_pj_str(attrs[pos].value, &name_addr->display, to_len);
		pos++;
	}

	attrs[pos].type = AST_MSG_DATA_ATTR_CONTENT_TYPE;
	attrs[pos].value = ast_alloca(rdata->msg_info.msg->body->content_type.type.slen
		+ rdata->msg_info.msg->body->content_type.subtype.slen + 2);
	sprintf(attrs[pos].value, "%.*s/%.*s",
		(int)rdata->msg_info.msg->body->content_type.type.slen,
		rdata->msg_info.msg->body->content_type.type.ptr,
		(int)rdata->msg_info.msg->body->content_type.subtype.slen,
		rdata->msg_info.msg->body->content_type.subtype.ptr);
	pos++;

	body_pos = pos;
	attrs[pos].type = AST_MSG_DATA_ATTR_BODY;
	attrs[pos].value = ast_malloc(rdata->msg_info.msg->body->len + 1);
	if (!attrs[pos].value) {
		send_response(rdata, PJSIP_SC_INTERNAL_SERVER_ERROR, dlg, tsx);
		return 0;
	}
	ast_copy_string(attrs[pos].value, rdata->msg_info.msg->body->data, rdata->msg_info.msg->body->len + 1);
	pos++;

	msg = ast_msg_data_alloc(AST_MSG_DATA_SOURCE_TYPE_IN_DIALOG, attrs, pos);
	if (!msg) {
		ast_free(attrs[body_pos].value);
		send_response(rdata, PJSIP_SC_INTERNAL_SERVER_ERROR, dlg, tsx);
		return 0;
	}

	ast_debug(1, "Received in-dialog MESSAGE from '%s:%s': %s %s\n",
		ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_FROM),
		ast_channel_name(session->channel),
		ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_TO),
		ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_BODY));

	rc = ast_msg_data_queue_frame(session->channel, msg);
	ast_free(attrs[body_pos].value);
	ast_free(msg);
	if (rc != 0) {
		send_response(rdata, PJSIP_SC_INTERNAL_SERVER_ERROR, dlg, tsx);
	} else {
		send_response(rdata, PJSIP_SC_ACCEPTED, dlg, tsx);
	}

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

	message_serializer = ast_sip_create_serializer("pjsip/messaging");
	if (!message_serializer) {
		ast_sip_unregister_service(&messaging_module);
		ast_msg_tech_unregister(&msg_tech);
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
	ast_taskprocessor_unreference(message_serializer);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Messaging Support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
