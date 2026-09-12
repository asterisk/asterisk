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
#include "asterisk/res_pjsip_redirect.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/test.h"
#include "asterisk/uri.h"

const pjsip_method pjsip_message_method = {PJSIP_OTHER_METHOD, {"MESSAGE", 7} };

#define MAX_HDR_SIZE 512
#define MAX_BODY_SIZE 1024
#define MAX_USER_SIZE 128

static struct ast_taskprocessor *message_serializer;
static struct ao2_container *in_dialog_message_deliveries;

static pj_bool_t module_on_rx_request(pjsip_rx_data *rdata);

static pjsip_module messaging_module = {
	.name = {"Messaging Module", 16},
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION,
	.on_rx_request = module_on_rx_request,
};

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
	pjsip_sip_uri *suri;
	char *display_name;
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

	/*
	 * We need to sanitize the from header's display name
	 * by replacing any control characters, including NULLs,
	 * with spaces.  Since the display name is a pj_str_t, we
	 * can't modify it in place, so we need to copy it to a
	 * temporary buffer first. The good news is that we can't
	 * accidentally run over the end of the buffer, even if
	 * there's a NULL in the middle, because the display name
	 * is a pj_str_t and we know its length.
	 */
	name_addr = (pjsip_name_addr *)rdata->msg_info.from->uri;
	suri = pjsip_uri_get_uri((pjsip_uri *)name_addr);
	if (name_addr->display.slen > 0) {
		int i = 0;
		char *temp_name = ast_alloca(name_addr->display.slen + 1);
		for (i = 0; i < name_addr->display.slen; i++) {
			if (name_addr->display.ptr[i] < 32) {
				temp_name[i] = ' ';
			} else {
				temp_name[i] = name_addr->display.ptr[i];
			}
		}
		temp_name[name_addr->display.slen] = '\0';
		/*
		 * We need space for each double quote, the display name,
		 * the trailing space and the NULL terminator.
		 */
		display_name = ast_alloca(name_addr->display.slen + 5);
		size = sprintf(display_name, "\"%s\" ", temp_name); /* Safe */
	} else {
		display_name = "";
	}

	/*
	 * In the end, the string should look like...
	 * "display name" <scheme:user@host>
	 * If there's no display name, it and its double quotes
	 * will be suppressed.
	 * Note that the port is not included.
	 */
	res |= ast_msg_set_from(msg, "%s<" PJSTR_PRINTF_SPEC ":" PJSTR_PRINTF_SPEC "@" PJSTR_PRINTF_SPEC ">",
			display_name,
			PJSTR_PRINTF_VAR(*pjsip_uri_get_scheme(suri)),
			PJSTR_PRINTF_VAR(*ast_sip_pjsip_uri_get_username((pjsip_uri *)name_addr)),
			PJSTR_PRINTF_VAR(*ast_sip_pjsip_uri_get_hostname((pjsip_uri *)name_addr)));

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

	return mdata;
}

/*!
 * \internal
 * \brief Callback data for MESSAGE response handling
 */
struct message_response_data {
	char *from;
	char *to;
	char *body;
	char *content_type;
	struct ast_sip_redirect_state *redirect_state;
};

static void message_response_data_destroy(void *obj)
{
	struct message_response_data *resp_data = obj;

	ast_free(resp_data->from);
	ast_free(resp_data->to);
	ast_free(resp_data->body);
	ast_free(resp_data->content_type);

	if (resp_data->redirect_state) {
		ast_sip_redirect_state_destroy(resp_data->redirect_state);
	}
}

static struct message_response_data *message_response_data_create(
	struct ast_sip_endpoint *endpoint,
	const char *from,
	const char *to,
	const char *body,
	const char *content_type,
	const char *initial_uri)
{
	struct message_response_data *resp_data;

	resp_data = ao2_alloc_options(sizeof(*resp_data), message_response_data_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!resp_data) {
		return NULL;
	}

	resp_data->from = ast_strdup(from);
	resp_data->to = ast_strdup(to);
	resp_data->body = ast_strdup(body);
	resp_data->content_type = ast_strdup(content_type);

	if (!resp_data->from || !resp_data->to || !resp_data->body || !resp_data->content_type) {
		ast_log(LOG_ERROR, "Failed to allocate memory for response data strings on endpoint '%s'.\n",
			ast_sorcery_object_get_id(endpoint));
		ao2_ref(resp_data, -1);
		return NULL;
	}

	resp_data->redirect_state = ast_sip_redirect_state_create(endpoint, initial_uri);
	if (!resp_data->redirect_state) {
		ast_log(LOG_ERROR, "Failed to create redirect state for endpoint '%s'.\n", ast_sorcery_object_get_id(endpoint));
		ao2_ref(resp_data, -1);
		return NULL;
	}

	return resp_data;
}

struct in_dialog_message_delivery {
	struct ast_sip_session *session;
	struct ast_channel *channel;
	/*!
	 * Outgoing tdata currently holding our pointer in
	 * tdata->mod_data[messaging_module.id]. When non-NULL the delivery
	 * holds one reference on tdata via pjsip_tx_data_add_ref and the
	 * slot holds one reference on the delivery; attach/detach helpers
	 * keep both directions in lock-step.
	 */
	pjsip_tx_data *tdata;
	unsigned int awaiting_auth_retry;
	unsigned int auth_retry_sent;
	unsigned int complete;
	int challenge_code;
	char *challenge_reason;
	char *request_id;
	char *to;
	char *from;
	char *body;
	char *content_type;
};

static void in_dialog_message_delivery_destroy(void *obj)
{
	struct in_dialog_message_delivery *delivery = obj;

	/*
	 * The slot owns a reference on the delivery, so refcount can only
	 * reach zero once the binding is gone. Be defensive in case something
	 * leaked: clearing tdata->mod_data avoids a dangling pointer.
	 */
	if (delivery->tdata) {
		if (delivery->tdata->mod_data[messaging_module.id] == delivery) {
			delivery->tdata->mod_data[messaging_module.id] = NULL;
		}
		pjsip_tx_data_dec_ref(delivery->tdata);
		delivery->tdata = NULL;
	}
	ao2_cleanup(delivery->session);
	ao2_cleanup(delivery->channel);
	ast_free(delivery->challenge_reason);
	ast_free(delivery->request_id);
	ast_free(delivery->to);
	ast_free(delivery->from);
	ast_free(delivery->body);
	ast_free(delivery->content_type);
}

/*!
 * \brief Bind \a delivery to \a tdata.
 *
 * Writes \a delivery into tdata->mod_data[messaging_module.id], adds a
 * reference on the tdata, and bumps the delivery refcount for the slot.
 * Caller must ensure \a delivery is currently unbound.
 */
static void attach_delivery_to_tdata(struct in_dialog_message_delivery *delivery,
	pjsip_tx_data *tdata)
{
	ast_assert(delivery->tdata == NULL);
	pjsip_tx_data_add_ref(tdata);
	ao2_ref(delivery, +1);
	delivery->tdata = tdata;
	tdata->mod_data[messaging_module.id] = delivery;
}

/*!
 * \brief Release the tdata binding on \a delivery.
 *
 * Clears tdata->mod_data[id] when it still points at \a delivery, drops
 * the delivery's reference on the tdata and the slot's reference on the
 * delivery. Idempotent.
 */
static void detach_delivery_from_tdata(struct in_dialog_message_delivery *delivery)
{
	pjsip_tx_data *tdata = delivery->tdata;

	if (!tdata) {
		return;
	}
	if (tdata->mod_data[messaging_module.id] == delivery) {
		tdata->mod_data[messaging_module.id] = NULL;
	}
	delivery->tdata = NULL;
	pjsip_tx_data_dec_ref(tdata);
	ao2_ref(delivery, -1);
}

static struct in_dialog_message_delivery *in_dialog_message_delivery_create(
	struct ast_sip_session *session, struct ast_msg_data *msg)
{
	struct in_dialog_message_delivery *delivery;
	const char *request_id = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_REQUEST_ID);
	const char *to = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_TO);
	const char *from = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_FROM);
	const char *body = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_BODY);
	const char *content_type = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_CONTENT_TYPE);

	delivery = ao2_alloc(sizeof(*delivery), in_dialog_message_delivery_destroy);
	if (!delivery) {
		return NULL;
	}

	delivery->session = ao2_bump(session);
	if (!session->channel) {
		ao2_ref(delivery, -1);
		return NULL;
	}
	delivery->channel = ast_channel_ref(session->channel);
	delivery->request_id = ast_strdup(request_id);
	delivery->to = ast_strdup(to);
	delivery->from = ast_strdup(from);
	delivery->body = ast_strdup(body);
	delivery->content_type = ast_strdup(content_type);

	if ((request_id[0] && !delivery->request_id)
		|| (to[0] && !delivery->to)
		|| (from[0] && !delivery->from)
		|| (body[0] && !delivery->body)
		|| (content_type[0] && !delivery->content_type)) {
		ao2_ref(delivery, -1);
		return NULL;
	}

	return delivery;
}

static int is_in_dialog_auth_challenge(int status_code)
{
	return status_code == 401 || status_code == 407 || status_code == 494;
}

static int has_auth_credentials(pjsip_tx_data *tdata)
{
	return pjsip_msg_find_hdr(tdata->msg, PJSIP_H_AUTHORIZATION, NULL)
		|| pjsip_msg_find_hdr(tdata->msg, PJSIP_H_PROXY_AUTHORIZATION, NULL);
}

static void publish_in_dialog_message_delivery_status(
	struct in_dialog_message_delivery *delivery, const char *status,
	int sip_status_code, const char *reason)
{
	RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);

	if (ast_strlen_zero(delivery->request_id)) {
		return;
	}

	if (!delivery->channel) {
		return;
	}

	blob = ast_json_pack("{s: s, s: s}",
		"request_id", S_OR(delivery->request_id, ""),
		"status", status);
	if (!blob) {
		return;
	}

	if (!ast_strlen_zero(delivery->to)) {
		ast_json_object_set(blob, "to", ast_json_string_create(delivery->to));
	}
	if (!ast_strlen_zero(delivery->from)) {
		ast_json_object_set(blob, "from", ast_json_string_create(delivery->from));
	}
	if (!ast_strlen_zero(delivery->body)) {
		ast_json_object_set(blob, "body", ast_json_string_create(delivery->body));
	}
	if (!ast_strlen_zero(delivery->content_type)) {
		ast_json_object_set(blob, "content_type", ast_json_string_create(delivery->content_type));
	}
	if (sip_status_code > 0) {
		ast_json_object_set(blob, "sip_status_code", ast_json_integer_create(sip_status_code));
	}
	if (!ast_strlen_zero(reason)) {
		ast_json_object_set(blob, "reason", ast_json_string_create(reason));
	}

	ast_channel_publish_cached_blob(delivery->channel,
		ast_channel_message_delivery_status_type(), blob);
}

static void complete_in_dialog_message_delivery(
	struct in_dialog_message_delivery *delivery, const char *status,
	int sip_status_code, const char *reason)
{
	pjsip_dialog *dlg;
	int slot_cleared = 0;

	ao2_lock(delivery);
	if (delivery->complete) {
		ao2_unlock(delivery);
		return;
	}

	delivery->complete = 1;
	ao2_unlock(delivery);

	dlg = (delivery->session && delivery->session->inv_session)
		? delivery->session->inv_session->dlg : NULL;
	if (dlg) {
		pjsip_dlg_inc_lock(dlg);
		if (dlg->mod_data[messaging_module.id] == delivery) {
			dlg->mod_data[messaging_module.id] = NULL;
			slot_cleared = 1;
		}
		pjsip_dlg_dec_lock(dlg);
	}
	if (slot_cleared) {
		ao2_ref(delivery, -1);
	}

	/*
	 * Sever the tdata binding so any late or duplicate response that still
	 * holds a reference to the originating tdata resolves to NULL via
	 * tdata->mod_data[messaging_module.id] and is silently ignored.
	 */
	detach_delivery_from_tdata(delivery);

	if (in_dialog_message_deliveries) {
		ao2_unlink(in_dialog_message_deliveries, delivery);
	}
	publish_in_dialog_message_delivery_status(delivery, status, sip_status_code, reason);
}

static void complete_in_dialog_auth_retry_failed_if_pending(
	struct in_dialog_message_delivery *delivery)
{
	int should_complete;
	int challenge_code;
	char challenge_reason[256];

	ao2_lock(delivery);
	should_complete = !delivery->complete && delivery->awaiting_auth_retry
		&& !delivery->auth_retry_sent;
	challenge_code = delivery->challenge_code;
	ast_copy_string(challenge_reason,
		S_OR(delivery->challenge_reason,
			"Authentication challenge could not be retried"),
		sizeof(challenge_reason));
	ao2_unlock(delivery);

	if (should_complete) {
		complete_in_dialog_message_delivery(delivery, "failed",
			challenge_code, challenge_reason);
	}
}

static int check_in_dialog_auth_retry(void *obj)
{
	struct in_dialog_message_delivery *delivery = obj;

	complete_in_dialog_auth_retry_failed_if_pending(delivery);
	ao2_ref(delivery, -1);

	return 0;
}

static void schedule_in_dialog_auth_retry_check(struct ast_sip_session *session,
	struct in_dialog_message_delivery *delivery)
{
	struct in_dialog_message_delivery *task_data;

	if (!session->serializer || !ast_taskprocessor_is_task(session->serializer)) {
		return;
	}

	task_data = ao2_bump(delivery);
	if (!task_data) {
		return;
	}

	if (ast_sip_push_task(session->serializer, check_in_dialog_auth_retry, task_data)) {
		ast_log(LOG_WARNING, "Unable to schedule in-dialog MESSAGE auth retry check\n");
		ao2_ref(task_data, -1);
	}
}

static void fail_pending_in_dialog_deliveries_for_session(struct ast_sip_session *session,
	const char *status, const char *reason)
{
	struct ao2_iterator it;
	struct in_dialog_message_delivery *entry;

	if (!in_dialog_message_deliveries) {
		return;
	}

	it = ao2_iterator_init(in_dialog_message_deliveries, 0);
	while ((entry = ao2_iterator_next(&it))) {
		if (entry->session == session) {
			complete_in_dialog_message_delivery(entry, status, 0, reason);
		}
		ao2_ref(entry, -1);
	}
	ao2_iterator_destroy(&it);
}

static void fail_all_pending_in_dialog_deliveries(const char *reason)
{
	struct ao2_iterator it;
	struct in_dialog_message_delivery *entry;

	if (!in_dialog_message_deliveries) {
		return;
	}

	it = ao2_iterator_init(in_dialog_message_deliveries, 0);
	while ((entry = ao2_iterator_next(&it))) {
		complete_in_dialog_message_delivery(entry, "failed", 0, reason);
		ao2_ref(entry, -1);
	}
	ao2_iterator_destroy(&it);
}

static void mark_in_dialog_delivery_auth_challenge(
	struct in_dialog_message_delivery *delivery, pjsip_rx_data *rdata)
{
	pjsip_status_line status_line = rdata->msg_info.msg->line.status;
	char reason[256];

	ast_copy_pj_str(reason, &status_line.reason, sizeof(reason));

	ao2_lock(delivery);
	if (!delivery->complete) {
		delivery->awaiting_auth_retry = 1;
		delivery->auth_retry_sent = 0;
		delivery->challenge_code = status_line.code;
		ast_free(delivery->challenge_reason);
		delivery->challenge_reason = ast_strdup(reason);
	}
	ao2_unlock(delivery);
}

static void handle_in_dialog_message_final_response(
	struct in_dialog_message_delivery *delivery, pjsip_rx_data *rdata);

static void handle_in_dialog_message_response(struct ast_sip_session *session,
	pjsip_rx_data *rdata)
{
	RAII_VAR(struct in_dialog_message_delivery *, delivery, NULL, ao2_cleanup);
	RAII_VAR(struct in_dialog_message_delivery *, evicted, NULL, ao2_cleanup);
	pjsip_status_line status_line;
	pjsip_transaction *tsx;
	struct in_dialog_message_delivery *bound;
	pjsip_dialog *dlg;

	if (!session || !rdata || !rdata->msg_info.msg
		|| rdata->msg_info.msg->type != PJSIP_RESPONSE_MSG) {
		return;
	}

	status_line = rdata->msg_info.msg->line.status;
	if (status_line.code < 200) {
		return;
	}

	/*
	 * The UAC transaction's last_tx is the exact pjsip_tx_data we wrote our
	 * pointer into at send time, so a single deref gives us the originating
	 * delivery with no CSeq, branch, or container lookup involved.
	 */
	tsx = pjsip_rdata_get_tsx(rdata);
	if (!tsx || !tsx->last_tx) {
		return;
	}
	bound = tsx->last_tx->mod_data[messaging_module.id];
	if (!bound) {
		return;
	}
	delivery = ao2_bump(bound);

	if (!is_in_dialog_auth_challenge(status_line.code)) {
		handle_in_dialog_message_final_response(delivery, rdata);
		return;
	}

	mark_in_dialog_delivery_auth_challenge(delivery, rdata);
	if (!session->endpoint || !AST_VECTOR_SIZE(&session->endpoint->outbound_auths)) {
		complete_in_dialog_auth_retry_failed_if_pending(delivery);
		return;
	}

	/*
	 * Park the delivery in the dialog mod_data slot so messaging_outgoing_request
	 * can pick it up when it sees the authed retry. If the slot already holds an
	 * older delivery whose retry never landed, evict and fail it.
	 */
	dlg = session->inv_session ? session->inv_session->dlg : NULL;
	if (dlg) {
		pjsip_dlg_inc_lock(dlg);
		evicted = dlg->mod_data[messaging_module.id];
		dlg->mod_data[messaging_module.id] = ao2_bump(delivery);
		pjsip_dlg_dec_lock(dlg);
	}
	if (evicted && evicted != delivery) {
		complete_in_dialog_auth_retry_failed_if_pending(evicted);
	}

	/*
	 * res_pjsip_session attempts the auth retry after incoming_response
	 * supplements run. Queue behind the current serializer task so failure
	 * is published if no authed retry is actually sent.
	 */
	schedule_in_dialog_auth_retry_check(session, delivery);
}

static void handle_in_dialog_message_final_response(
	struct in_dialog_message_delivery *delivery, pjsip_rx_data *rdata)
{
	pjsip_status_line status_line;
	char reason[256];

	if (!delivery || !rdata || !rdata->msg_info.msg
		|| rdata->msg_info.msg->type != PJSIP_RESPONSE_MSG) {
		return;
	}

	status_line = rdata->msg_info.msg->line.status;
	if (status_line.code < 200 || is_in_dialog_auth_challenge(status_line.code)) {
		return;
	}

	ast_copy_pj_str(reason, &status_line.reason, sizeof(reason));
	complete_in_dialog_message_delivery(delivery,
		(status_line.code >= 200 && status_line.code < 300) ? "delivered" : "failed",
		status_line.code, reason);
}

static void messaging_incoming_response(struct ast_sip_session *session, pjsip_rx_data *rdata)
{
	handle_in_dialog_message_response(session, rdata);
}

static void messaging_outgoing_request(struct ast_sip_session *session, pjsip_tx_data *tdata)
{
	RAII_VAR(struct in_dialog_message_delivery *, delivery, NULL, ao2_cleanup);
	pjsip_dialog *dlg;
	int proceed;

	/*
	 * The initial outgoing MESSAGE has its delivery bound to its tdata by
	 * track_in_dialog_message_request, so only the authed retry needs work
	 * here: rebind the delivery onto the brand-new tdata produced by
	 * ast_sip_create_request_with_auth.
	 */
	if (!has_auth_credentials(tdata)) {
		return;
	}

	dlg = session->inv_session ? session->inv_session->dlg : NULL;
	if (!dlg) {
		return;
	}

	pjsip_dlg_inc_lock(dlg);
	delivery = dlg->mod_data[messaging_module.id];
	dlg->mod_data[messaging_module.id] = NULL;
	pjsip_dlg_dec_lock(dlg);

	if (!delivery) {
		return;
	}

	ao2_lock(delivery);
	proceed = !delivery->complete && delivery->awaiting_auth_retry;
	if (proceed) {
		delivery->awaiting_auth_retry = 0;
		delivery->auth_retry_sent = 1;
	}
	ao2_unlock(delivery);

	if (proceed) {
		detach_delivery_from_tdata(delivery);
		attach_delivery_to_tdata(delivery, tdata);
	}
}

static void messaging_session_end(struct ast_sip_session *session)
{
	fail_pending_in_dialog_deliveries_for_session(session, "transport_error",
		"Session ended before a final SIP response was received");
}

static void messaging_session_destroy(struct ast_sip_session *session)
{
	fail_pending_in_dialog_deliveries_for_session(session, "transport_error",
		"Session ended before a final SIP response was received");
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

/* Forward declaration for callback */
static void msg_response_callback(void *token, pjsip_event *e);

/*!
 * \internal
 * \brief Send a MESSAGE to a redirect target
 *
 * \param resp_data Response data containing redirect state and message info
 * \param target_uri The URI to send the message to
 *
 * \return 0: success, -1: failure
 */
static int send_message_to_uri(struct message_response_data *resp_data, const char *target_uri)
{
	pjsip_tx_data *tdata;
	struct message_response_data *new_resp_data;
	struct ast_sip_endpoint *endpoint;
	struct ast_sip_body body = {
		.type = "text",
		.subtype = "plain",
		.body_text = resp_data->body
	};

	if (!resp_data->redirect_state) {
		ast_log(LOG_ERROR, "No redirect state available for sending a redirect message.\n");
		return -1;
	}

	endpoint = ast_sip_redirect_get_endpoint(resp_data->redirect_state);

	ast_debug(1, "Sending redirected MESSAGE to '%s' (hop %d) on endpoint '%s'\n",
		target_uri, ast_sip_redirect_get_hop_count(resp_data->redirect_state), ast_sorcery_object_get_id(endpoint));

	if (ast_sip_create_request("MESSAGE", NULL, endpoint, target_uri, NULL, &tdata)) {
		ast_log(LOG_WARNING, "Could not create redirect request for endpoint '%s'.\n",
			ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	/* Update To header if we have one */
	if (!ast_strlen_zero(resp_data->to)) {
		ast_sip_update_to_uri(tdata, resp_data->to);
	}

	/* Update From header if we have one */
	if (!ast_strlen_zero(resp_data->from)) {
		ast_sip_update_from(tdata, resp_data->from);
	}

	/* Parse and set content type if provided */
	if (!ast_strlen_zero(resp_data->content_type)) {
		char *type_copy = ast_strdupa(resp_data->content_type);
		char *subtype = strchr(type_copy, '/');
		if (subtype) {
			*subtype = '\0';
			subtype++;
			body.type = type_copy;
			body.subtype = subtype;
		}
	}
	ast_sip_add_body(tdata, &body);
	if (!tdata->msg->body) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "Could not add body to redirect request on endpoint '%s'.\n", ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	/* Create new callback data - the redirect state is passed along */
	new_resp_data = ao2_alloc(sizeof(*new_resp_data), message_response_data_destroy);
	if (!new_resp_data) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "Could not allocate redirect callback data for endpoint '%s'.\n", ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	/* Copy message-specific data */
	new_resp_data->from = ast_strdup(resp_data->from);
	new_resp_data->to = ast_strdup(resp_data->to);
	new_resp_data->body = ast_strdup(resp_data->body);
	new_resp_data->content_type = ast_strdup(resp_data->content_type);

	/* Check for allocation failures */
	if (!new_resp_data->from || !new_resp_data->to || !new_resp_data->body || !new_resp_data->content_type) {
		pjsip_tx_data_dec_ref(tdata);
		ao2_ref(new_resp_data, -1);
		ast_log(LOG_ERROR, "Failed to allocate memory for redirect callback strings for endpoint '%s'.\n",
			ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	/* Transfer the redirect state to the new response data */
	new_resp_data->redirect_state = resp_data->redirect_state;
	resp_data->redirect_state = NULL;

	/* Send with callback for potential further redirects */
	if (ast_sip_send_request(tdata, NULL, endpoint, new_resp_data, msg_response_callback)) {
		ao2_ref(new_resp_data, -1);
		ast_log(LOG_ERROR, "Failed to send redirect request to '%s' on endpoint '%s'.\n",
			new_resp_data->to, ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Handle a 3xx redirect response to a MESSAGE
 *
 * \param resp_data Response callback data
 * \param rdata The redirect response data
 */
static void handle_message_redirect(struct message_response_data *resp_data, pjsip_rx_data *rdata)
{
	char *uri = NULL;
	int hop_count;

	if (!resp_data->redirect_state) {
		ast_log(LOG_ERROR, "MESSAGE redirect: no redirect state available\n");
		return;
	}

	/* Parse the redirect response and extract contacts */
	if (ast_sip_redirect_parse_3xx(rdata, resp_data->redirect_state)) {
		ast_debug(1, "MESSAGE redirect on endpoint '%s': not following redirect (parse failed or conditions not met)\n",
			ast_sorcery_object_get_id(ast_sip_redirect_get_endpoint(resp_data->redirect_state)));
		return;
	}

	/* Get the first URI to try */
	if (ast_sip_redirect_get_next_uri(resp_data->redirect_state, &uri)) {
		ast_log(LOG_WARNING, "MESSAGE redirect on endpoint '%s': no valid URIs to try\n",
			ast_sorcery_object_get_id(ast_sip_redirect_get_endpoint(resp_data->redirect_state)));
		return;
	}

	hop_count = ast_sip_redirect_get_hop_count(resp_data->redirect_state);
	ast_log(LOG_NOTICE, "MESSAGE redirect on endpoint '%s': Following redirect to '%s' (hop %d/%d)\n",
		ast_sorcery_object_get_id(ast_sip_redirect_get_endpoint(resp_data->redirect_state)), uri, hop_count, AST_SIP_MAX_REDIRECT_HOPS);

	/* Try the first contact */
	send_message_to_uri(resp_data, uri);
	ast_free(uri);
}

/*!
 * \internal
 * \brief Callback for MESSAGE responses
 *
 * \param token Callback data
 * \param e The pjsip event
 */
static void msg_response_callback(void *token, pjsip_event *e)
{
	struct message_response_data *resp_data = token;
	struct ast_sip_redirect_state *state = resp_data->redirect_state;
	struct ast_sip_endpoint *endpoint = ast_sip_redirect_get_endpoint(state);
	pjsip_rx_data *rdata;
	int status_code;
	char *next_uri = NULL;

	/* Check event type */
	if (e->body.tsx_state.type == PJSIP_EVENT_TIMER) {
		ast_debug(1, "MESSAGE request on endpoint '%s' timed out\n", ast_sorcery_object_get_id(endpoint));
		/* Try next pending contact if available */
		if (state && !ast_sip_redirect_get_next_uri(state, &next_uri)) {
			ast_log(LOG_NOTICE, "MESSAGE timed out on endpoint '%s', trying next Contact: '%s'\n",
				ast_sorcery_object_get_id(endpoint), next_uri);
			send_message_to_uri(resp_data, next_uri);
			ast_free(next_uri);
		}
		ao2_ref(resp_data, -1);
		return;
	}

	if (e->body.tsx_state.type != PJSIP_EVENT_RX_MSG) {
		ast_debug(3, "MESSAGE response event type %d (not RX_MSG) on endpoint '%s'.\n",
			e->body.tsx_state.type, ast_sorcery_object_get_id(endpoint));
		ao2_ref(resp_data, -1);
		return;
	}

	rdata = e->body.tsx_state.src.rdata;
	status_code = e->body.tsx_state.tsx->status_code;

	ast_debug(3, "Received MESSAGE response %d on endpoint '%s'.\n", status_code, ast_sorcery_object_get_id(endpoint));

	/* Handle 3xx redirects */
	if (PJSIP_IS_STATUS_IN_CLASS(status_code, 300)) {
		handle_message_redirect(resp_data, rdata);
	}
	/* If non-2xx response and we have pending contacts, try the next one */
	else if (status_code >= 400 && state && !ast_sip_redirect_get_next_uri(state, &next_uri)) {
		ast_log(LOG_NOTICE, "MESSAGE to redirect target failed (%d) on endpoint '%s', trying next Contact: '%s'\n",
			status_code, ast_sorcery_object_get_id(endpoint), next_uri);
		send_message_to_uri(resp_data, next_uri);
		ast_free(next_uri);
	}
	/* Success (2xx) - don't try other contacts */
	else if (PJSIP_IS_STATUS_IN_CLASS(status_code, 200)) {
		ast_debug(1, "MESSAGE successfully delivered (%d) for endpoint '%s'.\n", status_code, ast_sorcery_object_get_id(endpoint));
	}

	ao2_ref(resp_data, -1);
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
	/* Callback data for redirect handling */
	struct message_response_data *resp_data;
	const char *from_uri;
	const char *to_uri;

	struct ast_sip_body body = {
		.type = "text",
		.subtype = "plain",
		.body_text = ast_msg_get_body(mdata->msg)
	};

	pjsip_hdr *contact;
	pjsip_tx_data *tdata;
	RAII_VAR(char *, uri, NULL, ast_free);
	RAII_VAR(struct ast_sip_endpoint *, endpoint, NULL, ao2_cleanup);
	RAII_VAR(struct ast_str *, content_type_buf , ast_str_create(128), ast_free);

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
		/*
		 * Only attempt to update the To URI if it's actually a SIP/SIPS URI.
		 * When sending via ARI, the To field is also used as destination
		 * (MessageDestinationInfo) and therefore might not contain a SIP URI.
		 * ast_sip_create_request still sets the correct To header.
		 */
		if (ast_begins_with(msg_to, "sip:") || ast_begins_with(msg_to, "sips:")) {
			ast_sip_update_to_uri(tdata, msg_to);
		}
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

	ast_sip_add_body(tdata, &body);
	if (!tdata->msg->body) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "PJSIP MESSAGE - Could not add body to request\n");
		return -1;
	}

	/*
	 * This copies any headers set with MESSAGE_DATA() to the
	 * tdata.
	 */
	vars_to_headers(mdata->msg, tdata);
	/* Remove Contact header fields as per RFC 3428*/
	contact = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CONTACT, NULL);
	if (contact) {
		pj_list_erase(contact);
	}
	ast_debug(1, "Sending message to '%s' (via endpoint %s) from '%s'\n",
		uri, ast_sorcery_object_get_id(endpoint), mdata->from);

	/* Determine From URI */
	if (!ast_strlen_zero(mdata->from)) {
		from_uri = mdata->from;
	} else if (!ast_strlen_zero(ast_msg_get_from(mdata->msg))) {
		from_uri = ast_msg_get_from(mdata->msg);
	} else {
		from_uri = "";
	}

	/* Determine To URI */
	if (!ast_strlen_zero(ast_msg_get_to(mdata->msg))) {
		to_uri = ast_msg_get_to(mdata->msg);
	} else {
		to_uri = "";
	}

	/* Build content type string */
	ast_str_set(&content_type_buf, 0, "%s/%s", body.type, body.subtype);

	/* Create callback data */
	resp_data = message_response_data_create(endpoint, from_uri, to_uri,
		body.body_text, ast_str_buffer(content_type_buf), uri);

	if (!resp_data) {
		pjsip_tx_data_dec_ref(tdata);
		ast_log(LOG_ERROR, "PJSIP MESSAGE - Could not allocate callback data for endpoint '%s'\n",
			ast_sorcery_object_get_id(endpoint));
		return -1;
	}

	/* Send with callback for redirect handling */
	if (ast_sip_send_request(tdata, NULL, endpoint, resp_data, msg_response_callback)) {
		ao2_ref(resp_data, -1);
		ast_log(LOG_ERROR, "PJSIP MESSAGE - Could not send request on endpoint '%s'\n", ast_sorcery_object_get_id(endpoint));
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

static int track_in_dialog_message_request(struct ast_sip_session *session,
	struct ast_msg_data *msg, pjsip_tx_data *tdata)
{
	RAII_VAR(struct in_dialog_message_delivery *, delivery, NULL, ao2_cleanup);
	const char *request_id = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_REQUEST_ID);

	if (!ast_strlen_zero(request_id)) {
		delivery = in_dialog_message_delivery_create(session, msg);
		if (!delivery) {
			return -1;
		}

		if (!ao2_link(in_dialog_message_deliveries, delivery)) {
			publish_in_dialog_message_delivery_status(delivery, "transport_error", 0,
				"Failed to register MESSAGE delivery tracking entry");
			return -1;
		}

		/*
		 * Bind the delivery to this tdata so messaging_incoming_response
		 * can resolve back to it via pjsip_rdata_get_tsx(rdata)->last_tx.
		 */
		attach_delivery_to_tdata(delivery, tdata);
	}

	return 0;
}

static void track_in_dialog_message_send_failed(struct ast_sip_session *session,
	struct ast_msg_data *msg, int sip_status_code, const char *reason)
{
	RAII_VAR(struct in_dialog_message_delivery *, delivery, NULL, ao2_cleanup);
	const char *request_id = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_REQUEST_ID);

	if (ast_strlen_zero(request_id)) {
		return;
	}

	delivery = in_dialog_message_delivery_create(session, msg);
	if (!delivery) {
		return;
	}

	publish_in_dialog_message_delivery_status(delivery, "transport_error",
		sip_status_code, reason);
}

static struct ast_sip_session_message_tracker in_dialog_message_tracker = {
	.request_prepared = track_in_dialog_message_request,
	.send_failed = track_in_dialog_message_send_failed,
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
	/* pjsip body->data is not null-terminated; use memcpy to avoid reading
	 * uninitialised bytes */
	memcpy(attrs[pos].value, rdata->msg_info.msg->body->data, rdata->msg_info.msg->body->len);
	((char *)attrs[pos].value)[rdata->msg_info.msg->body->len] = '\0';
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
	if (rc != 0) {
		send_response(rdata, PJSIP_SC_INTERNAL_SERVER_ERROR, dlg, tsx);
	} else if (!send_response(rdata, PJSIP_SC_ACCEPTED, dlg, tsx)) {
		RAII_VAR(struct ast_json *, blob, NULL, ast_json_unref);
		blob = ast_json_pack("{s: s, s: s}",
			"body", ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_BODY),
			"content_type", ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_CONTENT_TYPE));
		if (blob) {
			const char *from = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_FROM);
			const char *to = ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_TO);
			if (!ast_strlen_zero(from)) {
				ast_json_object_set(blob, "from", ast_json_string_create(from));
			}
			if (!ast_strlen_zero(to)) {
				ast_json_object_set(blob, "to", ast_json_string_create(to));
			}
			ast_channel_publish_cached_blob(session->channel,
				ast_channel_message_received_type(), blob);
		}
	}
	ast_free(attrs[body_pos].value);
	ast_free(msg);

	return 0;
}

static struct ast_sip_session_supplement messaging_supplement = {
	.method = "MESSAGE",
	.incoming_request = incoming_in_dialog_request,
	.incoming_response = messaging_incoming_response,
	.outgoing_request = messaging_outgoing_request,
	.session_end = messaging_session_end,
	.session_destroy = messaging_session_destroy,
	.response_priority = AST_SIP_SESSION_BEFORE_MEDIA | AST_SIP_SESSION_AFTER_MEDIA,
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

	in_dialog_message_deliveries = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_RWLOCK,
		AO2_CONTAINER_ALLOC_OPT_DUPS_ALLOW, NULL, NULL);
	if (!in_dialog_message_deliveries) {
		ast_sip_unregister_service(&messaging_module);
		ast_msg_tech_unregister(&msg_tech);
		return AST_MODULE_LOAD_DECLINE;
	}

	message_serializer = ast_sip_create_serializer("pjsip/messaging");
	if (!message_serializer) {
		ao2_cleanup(in_dialog_message_deliveries);
		in_dialog_message_deliveries = NULL;
		ast_sip_unregister_service(&messaging_module);
		ast_msg_tech_unregister(&msg_tech);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_sip_session_register_message_tracker(&in_dialog_message_tracker)) {
		ao2_cleanup(in_dialog_message_deliveries);
		in_dialog_message_deliveries = NULL;
		ast_taskprocessor_unreference(message_serializer);
		message_serializer = NULL;
		ast_sip_unregister_service(&messaging_module);
		ast_msg_tech_unregister(&msg_tech);
		return AST_MODULE_LOAD_DECLINE;
	}

	ast_sip_session_register_supplement(&messaging_supplement);
	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_sip_session_unregister_message_tracker(&in_dialog_message_tracker);
	ast_sip_session_unregister_supplement(&messaging_supplement);
	fail_all_pending_in_dialog_deliveries("PJSIP messaging module unloaded");
	ao2_cleanup(in_dialog_message_deliveries);
	in_dialog_message_deliveries = NULL;
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
