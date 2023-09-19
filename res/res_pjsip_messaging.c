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
	<depend>res_pjsip_session</depend>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<info name="MessageDestinationInfo" language="en_US" tech="PJSIP">
		<para>The <literal>destination</literal> parameter is used to construct
		the Request URI for an outgoing message.  It can be in one of the following
		formats, all prefixed with the <literal>pjsip:</literal> message tech.</para>
		<para>
		</para>
		<enumlist>
			<enum name="endpoint">
				<para>Request URI comes from the endpoint's default aor and contact.</para>
			</enum>
			<enum name="endpoint/aor">
				<para>Request URI comes from the specific aor/contact.</para>
			</enum>
			<enum name="endpoint@domain">
				<para>Request URI from the endpoint's default aor and contact.  The domain is discarded.</para>
			</enum>
		</enumlist>
		<para>
		</para>
		<para>These all use the endpoint to send the message with the specified URI:</para>
		<para>
		</para>
		<enumlist>
 			<enum name="endpoint/&lt;sip[s]:host&gt;>"/>
 			<enum name="endpoint/&lt;sip[s]:user@host&gt;"/>
 			<enum name="endpoint/&quot;display name&quot; &lt;sip[s]:host&gt;"/>
 			<enum name="endpoint/&quot;display name&quot; &lt;sip[s]:user@host&gt;"/>
 			<enum name="endpoint/sip[s]:host"/>
 			<enum name="endpoint/sip[s]:user@host"/>
 			<enum name="endpoint/host"/>
 			<enum name="endpoint/user@host"/>
 		</enumlist>
		<para>
		</para>
		<para>These all use the default endpoint to send the message with the specified URI:</para>
		<para>
		</para>
 	 	<enumlist>
 			<enum name="&lt;sip[s]:host&gt;"/>
 			<enum name="&lt;sip[s]:user@host&gt;"/>
 			<enum name="&quot;display name&quot; &lt;sip[s]:host&gt;"/>
 			<enum name="&quot;display name&quot; &lt;sip[s]:user@host&gt;"/>
 			<enum name="sip[s]:host"/>
 			<enum name="sip[s]:user@host"/>
 	 	</enumlist>
		<para>
		</para>
 	 	<para>These use the default endpoint to send the message with the specified host:</para>
		<para>
		</para>
 	 	<enumlist>
 			<enum name="host"/>
 			<enum name="user@host"/>
 	 	</enumlist>
 		<para>
 		</para>
 		<para>This form is similar to a dialstring:</para>
		<para>
		</para>
		<enumlist>
			<enum name="PJSIP/user@endpoint"/>
		</enumlist>
		<para>
		</para>
		<para>You still need to prefix the destination with
		the <literal>pjsip:</literal> message technology prefix.  For example:
		<literal>pjsip:PJSIP/8005551212@myprovider</literal>.
		The endpoint contact's URI will have the <literal>user</literal> inserted
		into it and will become the Request URI.  If the contact URI already has
		a user specified, it will be replaced.
		</para>
		<para>
		</para>
	</info>
	<info name="MessageFromInfo" language="en_US" tech="PJSIP">
		<para>The <literal>from</literal> parameter is used to specity the <literal>From:</literal>
		header in the outgoing SIP MESSAGE.  It will override the value specified in
		MESSAGE(from) which itself will override any <literal>from</literal> value from
		an incoming SIP MESSAGE.
		</para>
 		<para>
 		</para>
	</info>
	<info name="MessageToInfo" language="en_US" tech="PJSIP">
		<para>The <literal>to</literal> parameter is used to specity the <literal>To:</literal>
		header in the outgoing SIP MESSAGE.  It will override the value specified in
		MESSAGE(to) which itself will override any <literal>to</literal> value from
		an incoming SIP MESSAGE.
		</para>
 		<para>
 		</para>
	</info>
 ***/
#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/message.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/test.h"
#include "asterisk/uri.h"

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

	if (!(rdata->msg_info.msg->body && rdata->msg_info.msg->body->len > 0)) {
		return res;
	}

	/* We'll accept any text/ or application/ content type */
	if (pj_stricmp(&rdata->msg_info.msg->body->content_type.type, &text) == 0
			|| pj_stricmp(&rdata->msg_info.msg->body->content_type.type, &application) == 0) {
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
 * \brief Update the display name in the To uri in the tdata with the one from the supplied uri
 *
 * \param tdata the outbound message data structure
 * \param to uri containing the display name to replace in the the To uri
 *
 * \return 0: success, -1: failure
 */
static int update_to_display_name(pjsip_tx_data *tdata, char *to)
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
		return 0;
	}

	return -1;
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

	/* Don't block the Max-Forwards header because the user can override it */
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
		"Content-Type",
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
	pjsip_name_addr *name_addr;
	char buf[MAX_BODY_SIZE];
	const char *field;
	const char *context;
	char exten[AST_MAX_EXTENSION];
	int res = 0;
	int size;

	if (!ast_sip_is_allowed_uri(ruri)) {
		return PJSIP_SC_UNSUPPORTED_URI_SCHEME;
	}

	ast_copy_pj_str(exten, ast_sip_pjsip_uri_get_username(ruri), AST_MAX_EXTENSION);

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
	char *destination;
	char *from;
};

static void msg_data_destroy(void *obj)
{
	struct msg_data *mdata = obj;

	ast_free(mdata->from);
	ast_free(mdata->destination);

	ast_msg_destroy(mdata->msg);
}

static struct msg_data *msg_data_create(const struct ast_msg *msg, const char *destination, const char *from)
{
	char *uri_params;
	struct msg_data *mdata = ao2_alloc(sizeof(*mdata), msg_data_destroy);

	if (!mdata) {
		return NULL;
	}

	/* typecast to suppress const warning */
	mdata->msg = ast_msg_ref((struct ast_msg *) msg);

	/* To starts with 'pjsip:' which needs to be removed. */
	if (!(destination = strchr(destination, ':'))) {
		ao2_ref(mdata, -1);
		return NULL;
	}
	++destination;/* Now skip the ':' */

	mdata->destination = ast_strdup(destination);
	mdata->from = ast_strdup(from);

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

static void update_content_type(pjsip_tx_data *tdata, struct ast_msg *msg, struct ast_sip_body *body)
{
	static const pj_str_t CONTENT_TYPE = { "Content-Type", sizeof("Content-Type") - 1 };

	const char *content_type = ast_msg_get_var(msg, pj_strbuf(&CONTENT_TYPE));
	if (content_type) {
		pj_str_t type, subtype;
		pjsip_ctype_hdr *parsed;

		/* Let pjsip do the parsing for us */
		parsed = pjsip_parse_hdr(tdata->pool, &CONTENT_TYPE,
			ast_strdupa(content_type), strlen(content_type),
			NULL);

		if (!parsed) {
			ast_log(LOG_WARNING, "Failed to parse '%s' as a content type. Using text/plain\n",
				content_type);
			return;
		}

		/* We need to turn type and subtype into zero-terminated strings */
		pj_strdup_with_null(tdata->pool, &type, &parsed->media.type);
		pj_strdup_with_null(tdata->pool, &subtype, &parsed->media.subtype);

		body->type = pj_strbuf(&type);
		body->subtype = pj_strbuf(&subtype);
	}
}

/*!
 * \internal
 * \brief Send a MESSAGE
 *
 * \param data The outbound message data structure
 *
 * \return 0: success, -1: failure
 *
 * mdata contains the To and From specified in the call to the MessageSend
 * dialplan app.  It also contains the ast_msg object that contains the
 * message body and may contain the To and From from the channel datastore,
 * usually set with the MESSAGE or MESSAGE_DATA dialplan functions but
 * could also come from an incoming sip MESSAGE.
 *
 * The mdata->to is always used as the basis for the Request URI
 * while the mdata->msg->to is used for the To header.  If
 * mdata->msg->to isn't available, mdata->to is used for the To header.
 *
 */
static int msg_send(void *data)
{
	struct msg_data *mdata = data; /* The caller holds a reference */

	struct ast_sip_body body = {
		.type = "text",
		.subtype = "plain",
		.body_text = ast_msg_get_body(mdata->msg)
	};

	pjsip_tx_data *tdata;
	RAII_VAR(char *, uri, NULL, ast_free);
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);

	ast_debug(3, "mdata From: %s msg From: %s mdata Destination: %s msg To: %s\n",
		mdata->from, ast_msg_get_from(mdata->msg), mdata->destination, ast_msg_get_to(mdata->msg));

	endpoint = ast_sip_get_endpoint(mdata->destination, 1, &uri);
	if (!endpoint) {
		ast_log(LOG_ERROR,
			"PJSIP MESSAGE - Could not find endpoint '%s' and no default outbound endpoint configured\n",
			mdata->destination);

		ast_test_suite_event_notify("MSG_ENDPOINT_URI_FAIL",
			"MdataFrom: %s\r\n"
			"MsgFrom: %s\r\n"
			"MdataDestination: %s\r\n"
			"MsgTo: %s\r\n",
			mdata->from,
			ast_msg_get_from(mdata->msg),
			mdata->destination,
			ast_msg_get_to(mdata->msg));

		return -1;
	}

	ast_debug(3, "Request URI: %s\n", uri);

	if (ast_sip_create_request("MESSAGE", NULL, endpoint, uri, NULL, &tdata)) {
		ast_log(LOG_WARNING, "PJSIP MESSAGE - Could not create request\n");
		return -1;
	}

	/* If there was a To in the actual message, */
	if (!ast_strlen_zero(ast_msg_get_to(mdata->msg))) {
		char *msg_to = ast_strdupa(ast_msg_get_to(mdata->msg));

		/*
		 * It's possible that the message To was copied from
		 * an incoming MESSAGE in which case it'll have the
		 * pjsip: tech prepended to it.  We need to remove it.
		 */
		if (ast_begins_with(msg_to, "pjsip:")) {
			msg_to += 6;
		}
		ast_sip_update_to_uri(tdata, msg_to);
	} else {
		/*
		 * If there was no To in the message, it's still possible
		 * that there is a display name in the mdata To.  If so,
		 * we'll copy the URI display name to the tdata To.
		 */
		update_to_display_name(tdata, uri);
	}

	if (!ast_strlen_zero(mdata->from)) {
		ast_sip_update_from(tdata, mdata->from);
	} else if (!ast_strlen_zero(ast_msg_get_from(mdata->msg))) {
		ast_sip_update_from(tdata, (char *)ast_msg_get_from(mdata->msg));
	}

#ifdef TEST_FRAMEWORK
	{
		pjsip_name_addr *tdata_name_addr;
		pjsip_sip_uri *tdata_sip_uri;
		char touri[128];
		char fromuri[128];

		tdata_name_addr = (pjsip_name_addr *) PJSIP_MSG_TO_HDR(tdata->msg)->uri;
		tdata_sip_uri = pjsip_uri_get_uri(tdata_name_addr->uri);
		pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, tdata_sip_uri, touri, sizeof(touri));
		tdata_name_addr = (pjsip_name_addr *) PJSIP_MSG_FROM_HDR(tdata->msg)->uri;
		tdata_sip_uri = pjsip_uri_get_uri(tdata_name_addr->uri);
		pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, tdata_sip_uri, fromuri, sizeof(fromuri));

		ast_test_suite_event_notify("MSG_FROMTO_URI",
			"MdataFrom: %s\r\n"
			"MsgFrom: %s\r\n"
			"MdataDestination: %s\r\n"
			"MsgTo: %s\r\n"
			"Endpoint: %s\r\n"
			"RequestURI: %s\r\n"
			"ToURI: %s\r\n"
			"FromURI: %s\r\n",
			mdata->from,
			ast_msg_get_from(mdata->msg),
			mdata->destination,
			ast_msg_get_to(mdata->msg),
			ast_sorcery_object_get_id(endpoint),
			uri,
			touri,
			fromuri
			);
	}
#endif

	update_content_type(tdata, mdata->msg, &body);

	if (ast_sip_add_body(tdata, &body)) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "PJSIP MESSAGE - Could not add body to request\n");
		return -1;
	}

	/*
	 * This copies any headers set with MESSAGE_DATA() to the
	 * tdata.
	 */
	vars_to_headers(mdata->msg, tdata);

	ast_debug(1, "Sending message to '%s' (via endpoint %s) from '%s'\n",
		uri, ast_sorcery_object_get_id(endpoint), mdata->from);

	if (ast_sip_send_request(tdata, NULL, endpoint, NULL, NULL)) {
		ast_log(LOG_ERROR, "PJSIP MESSAGE - Could not send request\n");
		return -1;
	}

	return 0;
}

static int sip_msg_send(const struct ast_msg *msg, const char *destination, const char *from)
{
	struct msg_data *mdata;
	int res;

	if (ast_strlen_zero(destination)) {
		ast_log(LOG_ERROR, "SIP MESSAGE - a 'To' URI  must be specified\n");
		return -1;
	}

	mdata = msg_data_create(msg, destination, from);
	if (!mdata) {
		return -1;
	}

	res = ast_sip_push_task_wait_serializer(message_serializer, msg_send, mdata);
	ao2_ref(mdata, -1);

	return res;
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
