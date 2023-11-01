/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
 * José Lopes <jose.lopes@nfon.com>
 * Naveen Albert <asterisk@phreaknet.org>
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

#include "asterisk.h"

#include <pjsip.h>
#include <pjsip_ua.h>

#include "asterisk/res_pjsip.h"
#include "asterisk/res_pjsip_session.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/module.h"
#include "asterisk/utils.h"

/*** DOCUMENTATION
	<function name="PJSIP_HEADER" language="en_US">
		<synopsis>
			Gets headers from an inbound PJSIP channel. Adds, updates or removes the
			specified SIP header from an outbound PJSIP channel.
		</synopsis>
		<syntax>
			<parameter name="action" required="true">
				<enumlist>
					<enum name="read"><para>Returns instance <replaceable>number</replaceable>
					of header <replaceable>name</replaceable>. A <literal>*</literal>
					may be appended to <replaceable>name</replaceable> to iterate over all 
					headers <emphasis>beginning with</emphasis> <replaceable>name</replaceable>.
					</para></enum>

					<enum name="add"><para>Adds a new header <replaceable>name</replaceable>
					to this session.</para></enum>

					<enum name="update"><para>Updates instance <replaceable>number</replaceable>
					of header <replaceable>name</replaceable> to a new value.
					The header must already exist.</para></enum>

					<enum name="remove"><para>Removes all instances of previously added headers
					whose names match <replaceable>name</replaceable>. A <literal>*</literal>
					may be appended to <replaceable>name</replaceable> to remove all headers
					<emphasis>beginning with</emphasis> <replaceable>name</replaceable>.
					<replaceable>name</replaceable> may be set to a single <literal>*</literal>
					to clear <emphasis>all</emphasis> previously added headers. In all cases,
					the number of headers actually removed is returned.</para></enum>
				</enumlist>
			</parameter>

			<parameter name="name" required="true"><para>The name of the header.</para></parameter>

			<parameter name="number" required="false">
				<para>If there's more than 1 header with the same name, this specifies which header
				to read or update.  If not specified, defaults to <literal>1</literal> meaning
				the first matching header.  Not valid for <literal>add</literal> or
				<literal>remove</literal>.</para>
			</parameter>

		</syntax>
		<description>
			<para>PJSIP_HEADER allows you to read specific SIP headers from the inbound
			PJSIP channel as well as write(add, update, remove) headers on the outbound
			channel. One exception is that you can read headers that you have already
			added on the outbound channel.</para>
			<para>Examples:</para>
			<example title="Set somevar to the value of the From header">
			exten => 1,1,Set(somevar=${PJSIP_HEADER(read,From)})
			</example>
			<example title="Set via2 to the value of the 2nd Via header">
			exten => 1,1,Set(via2=${PJSIP_HEADER(read,Via,2)})
			</example>
			<example title="Set xhdr to the value of the 1st X-header">
			exten => 1,1,Set(xhdr=${PJSIP_HEADER(read,X-*,1)})
			</example>
			<example title="Add an X-Myheader header with the value of myvalue">
			exten => 1,1,Set(PJSIP_HEADER(add,X-MyHeader)=myvalue)
			</example>
			<example title="Add an X-Myheader header with an empty value">
			exten => 1,1,Set(PJSIP_HEADER(add,X-MyHeader)=)
			</example>
			<example title="Update the value of the header named X-Myheader to newvalue">
			; 'X-Myheader' must already exist or the call will fail.
			exten => 1,1,Set(PJSIP_HEADER(update,X-MyHeader)=newvalue)
			</example>
			<example title="Remove all headers whose names exactly match X-MyHeader">
			exten => 1,1,Set(PJSIP_HEADER(remove,X-MyHeader)=)
			</example>
			<example title="Remove all headers that begin with X-My">
			exten => 1,1,Set(PJSIP_HEADER(remove,X-My*)=)
			</example>
			<example title="Remove all previously added headers">
			exten => 1,1,Set(PJSIP_HEADER(remove,*)=)
			</example>
			<note><para>The <literal>remove</literal> action can be called by reading
			<emphasis>or</emphasis> writing PJSIP_HEADER.</para></note>
			<example title="Display the number of headers removed">
			exten => 1,1,Verbose( Removed ${PJSIP_HEADER(remove,X-MyHeader)} headers)
			</example>
			<example title="Set a variable to the number of headers removed">
			exten => 1,1,Set(count=${PJSIP_HEADER(remove,X-MyHeader)})
			</example>
			<example title="Just remove them ignoring any count">
			exten => 1,1,Set(=${PJSIP_HEADER(remove,X-MyHeader)})
			exten => 1,1,Set(PJSIP_HEADER(remove,X-MyHeader)=)
			</example>

			<note><para>If you call PJSIP_HEADER in a normal dialplan context you'll be
			operating on the <emphasis>caller's (incoming)</emphasis> channel which
			may not be what you want. To operate on the <emphasis>callee's (outgoing)</emphasis>
			channel call PJSIP_HEADER in a pre-dial handler. </para></note>
			<example title="Set headers on callee channel">
			[handler]
			exten => addheader,1,Set(PJSIP_HEADER(add,X-MyHeader)=myvalue)
			exten => addheader,2,Set(PJSIP_HEADER(add,X-MyHeader2)=myvalue2)

			[somecontext]
			exten => 1,1,Dial(PJSIP/${EXTEN},,b(handler^addheader^1))
			</example>
		</description>
	</function>
	<function name="PJSIP_HEADERS" language="en_US">
		<since>
			<version>16.20.0</version>
			<version>18.6.0</version>
			<version>19.0.0</version>
		</since>
		<synopsis>
			Gets the list of SIP header names from an INVITE message.
		</synopsis>
		<syntax>
			<parameter name="prefix">
				<para>If specified, only the headers matching the given prefix are returned.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns a comma-separated list of header names (without values) from the
			INVITE message. Multiple headers with the same name are included in the list only once.
			</para>
			<para>For example, <literal>${PJSIP_HEADERS(Co)}</literal> might return
			<literal>Contact,Content-Length,Content-Type</literal>. As a practical example,
			you may use <literal>${PJSIP_HEADERS(X-)}</literal> to enumerate optional extended
			headers.</para>
		</description>
		<see-also>
			<ref type="function">PJSIP_HEADER</ref>
		</see-also>
	</function>
	<function name="PJSIP_RESPONSE_HEADER" language="en_US">
		<synopsis>
			Gets headers of 200 response from an outbound PJSIP channel.
		</synopsis>
		<syntax>
			<parameter name="action" required="true">
				<enumlist>
					<enum name="read">
						<para>Returns instance <replaceable>number</replaceable>
						of response header <replaceable>name</replaceable>.</para>
					</enum>
				</enumlist>
			</parameter>

			<parameter name="name" required="true">
				<para>The <replaceable>name</replaceable> of the response header.
				A <literal>*</literal> can be appended to the <replaceable>name</replaceable>
				to iterate over all response headers <emphasis>beginning with</emphasis>
				<replaceable>name</replaceable>.</para>
			</parameter>

			<parameter name="number" required="false">
				<para>If there's more than 1 header with the same name, this specifies which header
				to read.  If not specified, defaults to <literal>1</literal> meaning
				the first matching header.
				</para>
			</parameter>

		</syntax>
		<description>
			<para>PJSIP_RESPONSE_HEADER allows you to read specific SIP headers of 200 response
			from the outbound PJSIP channel.</para>
			<para>Examples:</para>
			<example title="Set 'somevar' to the value of the 'From' header">
				exten => 1,1,Set(somevar=${PJSIP_RESPONSE_HEADER(read,From)})
			</example>
			<example title="Set 'via2' to the value of the 2nd 'Via' header">
				exten => 1,1,Set(via2=${PJSIP_RESPONSE_HEADER(read,Via,2)})
			</example>
			<example title="Set 'xhdr' to the value of the 1sx X-header">
				exten => 1,1,Set(xhdr=${PJSIP_RESPONSE_HEADER(read,X-*,1)})
			</example>

			<note><para>If you call PJSIP_RESPONSE_HEADER in a normal dialplan context you'll be
			operating on the <emphasis>caller's (incoming)</emphasis> channel which
			may not be what you want. To operate on the <emphasis>callee's (outgoing)</emphasis>
			channel call PJSIP_RESPONSE_HEADER in a pre-connect handler.</para>
			</note>
			<example title="Usage on pre-connect handler">
				[handler]
				exten => readheader,1,NoOp(PJSIP_RESPONSE_HEADER(read,X-MyHeader))
				[somecontext]
				exten => 1,1,Dial(PJSIP/${EXTEN},,U(handler^readheader^1))
			</example>
		</description>
		<see-also>
			<ref type="function">PJSIP_RESPONSE_HEADERS</ref>
			<ref type="function">PJSIP_HEADER</ref>
		</see-also>
	</function>
	<function name="PJSIP_RESPONSE_HEADERS" language="en_US">
		<synopsis>
			Gets the list of SIP header names from the 200 response of INVITE message.
		</synopsis>
		<syntax>
			<parameter name="prefix">
				<para>If specified, only the headers matching the given prefix are returned.</para>
			</parameter>
		</syntax>
		<description>
			<para>Returns a comma-separated list of header names (without values) from the 200
			response of INVITE message. Multiple headers with the same name are included in the
			list only once.</para>
			<para>For example, <literal>${PJSIP_RESPONSE_HEADERS(Co)}</literal> might return
			<literal>Contact,Content-Length,Content-Type</literal>. As a practical example,
			you may use <literal>${PJSIP_RESPONSE_HEADERS(X-)}</literal> to enumerate optional
			extended headers.</para>
		</description>
		<see-also>
			<ref type="function">PJSIP_RESPONSE_HEADER</ref>
			<ref type="function">PJSIP_HEADERS</ref>
		</see-also>
	</function>
	<function name="PJSIP_HEADER_PARAM" language="en_US">
		<synopsis>
			Get or set header/URI parameters on a PJSIP channel.
		</synopsis>
		<syntax>
			<parameter name="header_name" required="true">
				<para>Header in which parameter should be read or set.</para>
				<para>Currently, the only supported header is <literal>From</literal>.</para>
			</parameter>
			<parameter name="parameter_type" required="true">
				<para>The type of parameter to get or set.</para>
				<para>Default is header parameter.</para>
				<enumlist>
					<enum name="header">
						<para>Header parameter.</para>
					</enum>
					<enum name="uri">
						<para>URI parameter.</para>
					</enum>
				</enumlist>
			</parameter>
			<parameter name="parameter_name" required="true">
				<para>Name of parameter.</para>
			</parameter>
		</syntax>
		<description>
			<para>PJSIP_HEADER_PARAM allows you to read or set parameters in a SIP header on a
			PJSIP channel.</para>
			<para>Both URI parameters and header parameters can be read and set using
			this function. URI parameters appear in the URI (inside the &lt;&gt; in the header)
			while header parameters appear afterwards.</para>
			<note><para>If you call PJSIP_HEADER_PARAM in a normal dialplan context you'll be
			operating on the <emphasis>caller's (incoming)</emphasis> channel which
			may not be what you want. To operate on the <emphasis>callee's (outgoing)</emphasis>
			channel call PJSIP_HEADER_PARAM in a pre-dial handler. </para></note>
			<example title="Set URI parameter in From header on outbound channel">
			[handler]
			exten => addheader,1,Set(PJSIP_HEADER_PARAM(From,uri,isup-oli)=27)
			same => n,Return()
			[somecontext]
			exten => 1,1,Dial(PJSIP/${EXTEN},,b(handler^addheader^1))
			</example>
			<example title="Read URI parameter in From header on inbound channel">
			same => n,Set(value=${PJSIP_HEADER_PARAM(From,uri,isup-oli)})
			</example>
		</description>
	</function>
 ***/

/*! \brief Linked list for accumulating headers */
struct hdr_list_entry {
	pjsip_hdr *hdr;
	AST_LIST_ENTRY(hdr_list_entry) nextptr;
};
AST_LIST_HEAD_NOLOCK(hdr_list, hdr_list_entry);

/*! \brief Datastore for saving headers */
static const struct ast_datastore_info header_datastore = {
	.type = "header_datastore",
};
/*! \brief Datastore for saving response headers */
static const struct ast_datastore_info response_header_datastore = {
	.type = "response_header_datastore",
};

/*! \brief Data structure used for ast_sip_push_task_wait_serializer  */
struct header_data {
	struct ast_sip_channel_pvt *channel;
	char *header_name;
	const char *header_value;
	char *buf;
	int header_number;
	size_t len;
	const struct ast_datastore_info *header_datastore;
};

/*!
 * \internal
 * \brief Insert the header pointers into the linked list.
 *
 * For each header in the message, allocate a list entry,
 * clone the header, then insert the entry.
 */
static int insert_headers(pj_pool_t * pool, struct hdr_list *list, pjsip_msg * msg)
{
	pjsip_hdr *hdr = msg->hdr.next;
	struct hdr_list_entry *le;

	while (hdr && hdr != &msg->hdr) {
		le = pj_pool_zalloc(pool, sizeof(struct hdr_list_entry));
		le->hdr = pjsip_hdr_clone(pool, hdr);
		AST_LIST_INSERT_TAIL(list, le, nextptr);
		hdr = hdr->next;
	}

	return 0;
}

/*!
 * \internal
 * \brief Session supplement callback on an incoming INVITE request
 *
 * Retrieve the header_datastore from the session or create one if it doesn't exist.
 * Create and initialize the list if needed.
 * Insert the headers.
 */
static int incoming_request(struct ast_sip_session *session, pjsip_rx_data * rdata)
{
	pj_pool_t *pool = session->inv_session->dlg->pool;
	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(session, header_datastore.type), ao2_cleanup);

	if (!datastore) {
		if (!(datastore =
			  ast_sip_session_alloc_datastore(&header_datastore, header_datastore.type))
			||
			!(datastore->data = pj_pool_alloc(pool, sizeof(struct hdr_list))) ||
			ast_sip_session_add_datastore(session, datastore)) {
			ast_log(AST_LOG_ERROR, "Unable to create datastore for header functions.\n");
			return 0;
		}
		AST_LIST_HEAD_INIT_NOLOCK((struct hdr_list *) datastore->data);
	}
	insert_headers(pool, (struct hdr_list *) datastore->data, rdata->msg_info.msg);

	return 0;
}

/*!
 * \internal
 * \brief Session supplement callback on an incoming INVITE response
 *
 * Retrieve the response_header_datastore from the session or create one if it doesn't exist.
 * Create and initialize the list if needed.
 * Insert the headers.
 */
static void incoming_response(struct ast_sip_session *session, pjsip_rx_data * rdata)
{
	pj_pool_t *pool = session->inv_session->dlg->pool;
	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(session, response_header_datastore.type), ao2_cleanup);
	pjsip_status_line status = rdata->msg_info.msg->line.status;

	/* Skip responses different of 200 OK, when 2xx is received. */
	if (session->inv_session->state != PJSIP_INV_STATE_CONNECTING || status.code!=200) {
		return;
	}

	if (!datastore) {
		if (!(datastore =
			  ast_sip_session_alloc_datastore(&response_header_datastore, response_header_datastore.type))
			||
			!(datastore->data = pj_pool_alloc(pool, sizeof(struct hdr_list))) ||
			ast_sip_session_add_datastore(session, datastore)) {
			ast_log(AST_LOG_ERROR, "Unable to create datastore for header functions.\n");
			return;
		}
		AST_LIST_HEAD_INIT_NOLOCK((struct hdr_list *) datastore->data);
	}
	insert_headers(pool, (struct hdr_list *) datastore->data, rdata->msg_info.msg);

	return;
}

/*!
 * \internal
 * \brief Search list for nth occurrence of specific header.
 */
static pjsip_hdr *find_header(struct hdr_list *list, const char *header_name,
							  int header_number)
{
	struct hdr_list_entry *le;
	pjsip_hdr *hdr = NULL;
	int i = 1;

	if (!list || ast_strlen_zero(header_name) || header_number < 1) {
		return NULL;
	}

	AST_LIST_TRAVERSE(list, le, nextptr) {
		if (pj_stricmp2(&le->hdr->name, header_name) == 0 && i++ == header_number) {
			hdr = le->hdr;
			break;
		}
	}

	return hdr;
}

/*!
 * \internal
 * \brief Implements PJSIP_HEADERS/PJSIP_RESPONSE_HEADERS by searching for the requested header prefix.
 *
 * Retrieve the header_datastore.
 * Search for the all matching headers.
 * Validate the pjsip_hdr found.
 * Parse pjsip_hdr into a name and copy to the buffer.
 * Return the value.
 */
static int read_headers(void *obj)
{
	struct header_data *data = obj;
	size_t len = !ast_strlen_zero(data->header_name) ? strlen(data->header_name) : 0;
	pjsip_hdr *hdr = NULL;
	char *pj_hdr_string;
	int pj_hdr_string_len;
	char *p;
	char *pos;
	size_t plen, wlen = 0;
	struct hdr_list_entry *le;
	struct hdr_list *list;

	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(data->channel->session, data->header_datastore->type),
			 ao2_cleanup);

	if (!datastore || !datastore->data) {
		ast_debug(1, "There was no datastore from which to read headers.\n");
		return -1;
	}

	list = datastore->data;
	pj_hdr_string = ast_alloca(data->len);
	AST_LIST_TRAVERSE(list, le, nextptr) {
		if (!len || pj_strnicmp2(&le->hdr->name, data->header_name, len) == 0) {
			/* Found matched header, append to buf */
			hdr = le->hdr;

			pj_hdr_string_len = pjsip_hdr_print_on(hdr, pj_hdr_string, data->len - 1);
			if (pj_hdr_string_len == -1) {
				ast_log(AST_LOG_ERROR,
					"Not enough buffer space in pjsip_hdr_print_on\n");
				return -1;
			}
			pj_hdr_string[pj_hdr_string_len] = '\0';
			p = strchr(pj_hdr_string, ':');
			if (!p) {
				ast_log(AST_LOG_WARNING,
					"A malformed header was returned from pjsip_hdr_print_on\n");
				continue;
			}

			pj_hdr_string[p - pj_hdr_string] = '\0';
			p = ast_strip(pj_hdr_string);
			plen = strlen(p);
			if (wlen + plen + 1 > data->len) {
				ast_log(AST_LOG_ERROR,
						"Buffer isn't big enough to hold header value.  %zu > %zu\n", plen + 1,
						data->len);
				return -1;
			}
			pos = strstr(data->buf, p);
			if (pos && pos[1] == ',') {
				if (pos == data->buf) {
					continue;
				} else if (pos[-1] == ',') {
					continue;
				}
			}
			ast_copy_string(data->buf + wlen, p, data->len - wlen);
			wlen += plen;
			ast_copy_string(data->buf + wlen, ",", data->len - wlen);
			wlen++;
		}
	}

	if (wlen == 0) {
		if (!len) {
			/* No headers at all on this channel */
			return 0;
		} else {
			ast_debug(1, "There was no header beginning with %s.\n", data->header_name);
			return -1;
		}
	} else {
		data->buf[wlen-1] = '\0';
	}
	return 0;
}

/*!
 * \internal
 * \brief Implements PJSIP_HEADER/PJSIP_RESPONSE_HEADER 'read' by searching the for the requested header.
 *
 * Retrieve the header_datastore.
 * Search for the nth matching header.
 * Validate the pjsip_hdr found.
 * Parse pjsip_hdr into a name and value.
 * Return the value.
 */
static int read_header(void *obj)
{
	struct header_data *data = obj;
	size_t len = strlen(data->header_name);
	pjsip_hdr *hdr = NULL;
	char *pj_hdr_string;
	int pj_hdr_string_len;
	char *p;
	size_t plen;
	struct hdr_list_entry *le;
	struct hdr_list *list;
	int i = 1;
	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(data->channel->session, data->header_datastore->type),
			 ao2_cleanup);

	if (!datastore || !datastore->data) {
		ast_debug(1, "There was no datastore from which to read headers.\n");
		return -1;
	}

	list = datastore->data;
	AST_LIST_TRAVERSE(list, le, nextptr) {
		if (data->header_name[len - 1] == '*') {
			if (pj_strnicmp2(&le->hdr->name, data->header_name, len - 1) == 0 && i++ == data->header_number) {
				hdr = le->hdr;
				break;
			}
		} else {
			if (pj_stricmp2(&le->hdr->name, data->header_name) == 0 && i++ == data->header_number) {
				hdr = le->hdr;
				break;
			}
		}
	}

	if (!hdr) {
		ast_debug(1, "There was no header named %s.\n", data->header_name);
		return -1;
	}

	pj_hdr_string = ast_alloca(data->len);
	pj_hdr_string_len = pjsip_hdr_print_on(hdr, pj_hdr_string, data->len - 1);
	if (pj_hdr_string_len == -1) {
		ast_log(AST_LOG_ERROR,
			"Not enough buffer space in pjsip_hdr_print_on\n");
		return -1;
	}

	pj_hdr_string[pj_hdr_string_len] = '\0';

	p = strchr(pj_hdr_string, ':');
	if (!p) {
		ast_log(AST_LOG_ERROR,
				"A malformed header was returned from pjsip_hdr_print_on.\n");
		return -1;
	}

	++p;
	p = ast_strip(p);
	plen = strlen(p);
	if (plen + 1 > data->len) {
		ast_log(AST_LOG_ERROR,
				"Buffer isn't big enough to hold header value.  %zu > %zu\n", plen + 1,
				data->len);
		return -1;
	}

	ast_copy_string(data->buf, p, data->len);

	return 0;
}

/*!
 * \internal
 * \brief Implements PJSIP_HEADER 'add' by inserting the specified header into the list.
 *
 * Retrieve the header_datastore from the session or create one if it doesn't exist.
 * Create and initialize the list if needed.
 * Create the pj_strs for name and value.
 * Create pjsip_msg and hdr_list_entry.
 * Add the entry to the list.
 */
static int add_header(void *obj)
{
	struct header_data *data = obj;
	struct ast_sip_session *session = data->channel->session;
	pj_pool_t *pool = session->inv_session->dlg->pool;
	pj_str_t pj_header_name;
	pj_str_t pj_header_value;
	struct hdr_list_entry *le;
	struct hdr_list *list;

	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(session, data->header_datastore->type), ao2_cleanup);

	if (!datastore) {
		if (!(datastore = ast_sip_session_alloc_datastore(data->header_datastore,
														data->header_datastore->type))
			|| !(datastore->data = pj_pool_alloc(pool, sizeof(struct hdr_list)))
			|| ast_sip_session_add_datastore(session, datastore)) {
			ast_log(AST_LOG_ERROR, "Unable to create datastore for header functions.\n");
			return -1;
		}
		AST_LIST_HEAD_INIT_NOLOCK((struct hdr_list *) datastore->data);
	}

	ast_debug(1, "Adding header %s with value %s\n", data->header_name,
			  data->header_value);

	pj_cstr(&pj_header_name, data->header_name);
	pj_cstr(&pj_header_value, data->header_value);
	le = pj_pool_zalloc(pool, sizeof(struct hdr_list_entry));
	le->hdr = (pjsip_hdr *) pjsip_generic_string_hdr_create(pool, &pj_header_name,
															&pj_header_value);
	list = datastore->data;

	AST_LIST_INSERT_TAIL(list, le, nextptr);

	return 0;
}

/*!
 * \internal
 * \brief Implements PJSIP_HEADER 'update' by finding the specified header and updating it.
 *
 * Retrieve the header_datastore from the session or create one if it doesn't exist.
 * Create and initialize the list if needed.
 * Create the pj_strs for name and value.
 * Create pjsip_msg and hdr_list_entry.
 * Add the entry to the list.
 */
static int update_header(void *obj)
{
	struct header_data *data = obj;
	pjsip_hdr *hdr = NULL;
	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(data->channel->session, data->header_datastore->type),
			 ao2_cleanup);

	if (!datastore || !datastore->data) {
		ast_log(AST_LOG_ERROR, "No headers had been previously added to this session.\n");
		return -1;
	}

	hdr = find_header((struct hdr_list *) datastore->data, data->header_name,
					  data->header_number);

	if (!hdr) {
		ast_log(AST_LOG_ERROR, "There was no header named %s.\n", data->header_name);
		return -1;
	}

	pj_strcpy2(&((pjsip_generic_string_hdr *) hdr)->hvalue, data->header_value);

	return 0;
}

/*!
 * \internal
 * \brief Implements PJSIP_HEADER 'remove' by finding the specified header and removing it.
 *
 * Retrieve the header_datastore from the session.  Fail if it doesn't exist.
 * If the header_name is exactly '*', the entire list is simply destroyed.
 * Otherwise search the list for the matching header name which may be a partial name.
 */
static int remove_header(void *obj)
{
	struct header_data *data = obj;
	size_t len = strlen(data->header_name);
	struct hdr_list *list;
	struct hdr_list_entry *le;
	int removed_count = 0;
	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(data->channel->session, data->header_datastore->type),
			 ao2_cleanup);

	if (!datastore || !datastore->data) {
		ast_log(AST_LOG_ERROR, "No headers had been previously added to this session.\n");
		return -1;
	}

	list = datastore->data;
	AST_LIST_TRAVERSE_SAFE_BEGIN(list, le, nextptr) {
		if (data->header_name[len - 1] == '*') {
			if (pj_strnicmp2(&le->hdr->name, data->header_name, len - 1) == 0) {
				AST_LIST_REMOVE_CURRENT(nextptr);
				removed_count++;
			}
		} else {
			if (pj_stricmp2(&le->hdr->name, data->header_name) == 0) {
				AST_LIST_REMOVE_CURRENT(nextptr);
				removed_count++;
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (data->buf && data->len) {
		snprintf(data->buf, data->len, "%d", removed_count);
	}

	return 0;
}

/*!
 * \brief Read list of unique SIP headers
 */
static int func_read_headers(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel = chan ? ast_channel_tech_pvt(chan) : NULL;
	struct header_data header_data;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(header_pattern);
	);
	AST_STANDARD_APP_ARGS(args, data);

	if (!chan || strncmp(ast_channel_name(chan), "PJSIP/", 6)) {
		ast_log(LOG_ERROR, "This function requires a PJSIP channel.\n");
		return -1;
	}

	header_data.channel = channel;
	header_data.header_name = args.header_pattern;
	header_data.header_value = NULL;
	header_data.buf = buf;
	header_data.len = len;
	header_data.header_datastore = &header_datastore;

	return ast_sip_push_task_wait_serializer(channel->session->serializer, read_headers, &header_data);

}

/*!
 * \brief Read list of unique SIP response headers
 */
static int func_response_read_headers(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel = chan ? ast_channel_tech_pvt(chan) : NULL;
	struct header_data header_data;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(header_pattern);
	);
	AST_STANDARD_APP_ARGS(args, data);

	if (!chan || strncmp(ast_channel_name(chan), "PJSIP/", 6)) {
		ast_log(LOG_ERROR, "This function requires a PJSIP channel.\n");
		return -1;
	}

	if (ast_strlen_zero(args.header_pattern)) {
		ast_log(AST_LOG_ERROR, "This function requires a pattern.\n");
		return -1;
	}

	header_data.channel = channel;
	header_data.header_name = args.header_pattern;
	header_data.header_value = NULL;
	header_data.buf = buf;
	header_data.len = len;
	header_data.header_datastore = &response_header_datastore;

	return ast_sip_push_task_wait_serializer(channel->session->serializer, read_headers, &header_data);

}

/*!
 * \brief Implements PJSIP_HEADER function 'read' callback.
 *
 * Valid actions are 'read' and 'remove'.
 */
static int func_read_header(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel = chan ? ast_channel_tech_pvt(chan) : NULL;
	struct header_data header_data;
	int number;
	AST_DECLARE_APP_ARGS(args,
						 AST_APP_ARG(action);
						 AST_APP_ARG(header_name); AST_APP_ARG(header_number););
	AST_STANDARD_APP_ARGS(args, data);

	if (!channel || strncmp(ast_channel_name(chan), "PJSIP/", 6)) {
		ast_log(LOG_ERROR, "This function requires a PJSIP channel.\n");
		return -1;
	}

	if (ast_strlen_zero(args.action)) {
		ast_log(AST_LOG_ERROR, "This function requires an action.\n");
		return -1;
	}
	if (ast_strlen_zero(args.header_name)) {
		ast_log(AST_LOG_ERROR, "This function requires a header name.\n");
		return -1;
	}
	if (!args.header_number) {
		number = 1;
	} else {
		sscanf(args.header_number, "%30d", &number);
		if (number < 1) {
			number = 1;
		}
	}

	header_data.channel = channel;
	header_data.header_name = args.header_name;
	header_data.header_number = number;
	header_data.header_value = NULL;
	header_data.buf = buf;
	header_data.len = len;
	header_data.header_datastore = &header_datastore;

	if (!strcasecmp(args.action, "read")) {
		return ast_sip_push_task_wait_serializer(channel->session->serializer, read_header, &header_data);
	} else if (!strcasecmp(args.action, "remove")) {
		return ast_sip_push_task_wait_serializer(channel->session->serializer,
			remove_header, &header_data);
	} else {
		ast_log(AST_LOG_ERROR,
				"Unknown action '%s' is not valid, must be 'read' or 'remove'.\n",
				args.action);
		return -1;
	}
}

/*!
 * \brief Implements PJSIP_RESPONSE_HEADER function 'read' callback.
 *
 * Valid actions are 'read'
 */
static int func_response_read_header(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel = chan ? ast_channel_tech_pvt(chan) : NULL;
	struct header_data header_data;
	int number;
	AST_DECLARE_APP_ARGS(args,
						 AST_APP_ARG(action);
						 AST_APP_ARG(header_name); AST_APP_ARG(header_number););
	AST_STANDARD_APP_ARGS(args, data);

	if (!channel || strncmp(ast_channel_name(chan), "PJSIP/", 6)) {
		ast_log(LOG_ERROR, "This function requires a PJSIP channel.\n");
		return -1;
	}

	if (ast_strlen_zero(args.action)) {
		ast_log(AST_LOG_ERROR, "This function requires an action.\n");
		return -1;
	}
	if (ast_strlen_zero(args.header_name)) {
		ast_log(AST_LOG_ERROR, "This function requires a header name.\n");
		return -1;
	}
	if (!args.header_number) {
		number = 1;
	} else {
		sscanf(args.header_number, "%30d", &number);
		if (number < 1) {
			number = 1;
		}
	}

	header_data.channel = channel;
	header_data.header_name = args.header_name;
	header_data.header_number = number;
	header_data.header_value = NULL;
	header_data.buf = buf;
	header_data.len = len;
	header_data.header_datastore = &response_header_datastore;

	if (!strcasecmp(args.action, "read")) {
		return ast_sip_push_task_wait_serializer(channel->session->serializer, read_header, &header_data);
	} else {
		ast_log(AST_LOG_ERROR,
				"Unknown action '%s' is not valid, must be 'read'.\n",
				args.action);
		return -1;
	}
}

/*!
 * \brief Implements PJSIP_HEADER function 'write' callback.
 *
 * Valid actions are 'add', 'update' and 'remove'.
 */
static int func_write_header(struct ast_channel *chan, const char *cmd, char *data,
							 const char *value)
{
	struct ast_sip_channel_pvt *channel = chan ? ast_channel_tech_pvt(chan) : NULL;
	struct header_data header_data;
	int header_number;
	AST_DECLARE_APP_ARGS(args,
						 AST_APP_ARG(action);
						 AST_APP_ARG(header_name); AST_APP_ARG(header_number););
	AST_STANDARD_APP_ARGS(args, data);

	if (!channel || strncmp(ast_channel_name(chan), "PJSIP/", 6)) {
		ast_log(LOG_ERROR, "This function requires a PJSIP channel.\n");
		return -1;
	}

	if (ast_strlen_zero(args.action)) {
		ast_log(AST_LOG_ERROR, "This function requires an action.\n");
		return -1;
	}
	if (ast_strlen_zero(args.header_name)) {
		ast_log(AST_LOG_ERROR, "This function requires a header name.\n");
		return -1;
	}
	if (!args.header_number) {
		header_number = 1;
	} else {
		sscanf(args.header_number, "%30d", &header_number);
		if (header_number < 1) {
			header_number = 1;
		}
	}

	header_data.channel = channel;
	header_data.header_name = args.header_name;
	header_data.header_number = header_number;
	header_data.header_value = value;
	header_data.buf = NULL;
	header_data.len = 0;
	header_data.header_datastore = &header_datastore;

	if (!strcasecmp(args.action, "add")) {
		return ast_sip_push_task_wait_serializer(channel->session->serializer,
			add_header, &header_data);
	} else if (!strcasecmp(args.action, "update")) {
		return ast_sip_push_task_wait_serializer(channel->session->serializer,
			update_header, &header_data);
	} else if (!strcasecmp(args.action, "remove")) {
		return ast_sip_push_task_wait_serializer(channel->session->serializer,
			remove_header, &header_data);
	} else {
		ast_log(AST_LOG_ERROR,
				"Unknown action '%s' is not valid, must be 'add', 'update', or 'remove'.\n",
				args.action);
		return -1;
	}
}

static struct ast_custom_function pjsip_header_function = {
	.name = "PJSIP_HEADER",
	.read = func_read_header,
	.write = func_write_header,
};

static struct ast_custom_function pjsip_headers_function = {
	.name = "PJSIP_HEADERS",
	.read = func_read_headers
};

static struct ast_custom_function pjsip_response_header_function = {
	.name = "PJSIP_RESPONSE_HEADER",
	.read = func_response_read_header
};

static struct ast_custom_function pjsip_response_headers_function = {
	.name = "PJSIP_RESPONSE_HEADERS",
	.read = func_response_read_headers
};

/*!
 * \internal
 * \brief Session supplement callback for outgoing INVITE requests
 *
 * Retrieve the header_datastore from the session.
 * Add each header in the list to the outgoing message.
 *
 * These pjsip_hdr structures will have been created by add_header.
 * Because outgoing_request may be called more than once with the same header
 * list (as in the case of an authentication exchange), each pjsip_hdr structure
 * MUST be newly cloned for each outgoing message.
 */
static void outgoing_request(struct ast_sip_session *session, pjsip_tx_data * tdata)
{
	struct hdr_list *list;
	struct hdr_list_entry *le;
	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(session, header_datastore.type), ao2_cleanup);

	if (!datastore || !datastore->data ||
		(session->inv_session->state >= PJSIP_INV_STATE_CONFIRMED)) {
		return;
	}

	list = datastore->data;
	AST_LIST_TRAVERSE(list, le, nextptr) {
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) pjsip_hdr_clone(tdata->pool, le->hdr));
	}
	ast_sip_session_remove_datastore(session, datastore->uid);
}

static struct ast_sip_session_supplement header_funcs_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL - 1000,
	.incoming_request = incoming_request,
	.outgoing_request = outgoing_request,
	.incoming_response = incoming_response,
};

enum param_type {
	PARAMETER_HEADER,
	PARAMETER_URI,
};

struct param_data {
	struct ast_sip_channel_pvt *channel;
	char *header_name;
	char *param_name;
	const char *param_value; /* Only used for write */
	enum param_type paramtype;
	/* For read function only */
	char *buf;
	size_t len;
};

static int read_param(void *obj)
{
	struct param_data *data = obj;
	struct ast_sip_session *session = data->channel->session;
	pj_str_t param_name;

	pjsip_fromto_hdr *dlg_info;
	pjsip_name_addr *dlg_info_name_addr;
	pjsip_sip_uri *dlg_info_uri;
	pjsip_param *param;
	size_t param_len;

	dlg_info = session->inv_session->dlg->remote.info; /* Remote dialog for incoming */
	dlg_info_name_addr = (pjsip_name_addr *) dlg_info->uri;
	dlg_info_uri = pjsip_uri_get_uri(dlg_info_name_addr);

	pj_cstr(&param_name, data->param_name);

	if (data->paramtype == PARAMETER_URI) { /* URI parameter */
		param = pjsip_param_find(&dlg_info_uri->other_param, &param_name);
	} else { /* Header parameter */
		param = pjsip_param_find(&dlg_info->other_param, &param_name);
	}

	if (!param) {
		ast_debug(1, "No %s parameter found named %s\n",
			data->paramtype == PARAMETER_URI ? "URI" : "header", data->param_name);
		return -1;
	}

	param_len = pj_strlen(&param->value);
	if (param_len >= data->len) {
		ast_log(LOG_ERROR, "Buffer is too small for parameter value (%zu > %zu)\n", param_len, data->len);
		return -1;
	}

	ast_debug(2, "Successfully read %s parameter %s (length %zu)\n",
		data->paramtype == PARAMETER_URI ? "URI" : "header", data->param_name, param_len);
	ast_copy_string(data->buf, pj_strbuf(&param->value), data->len);
	data->buf[pj_strlen(&param->value)] = '\0';

	return 0;
}

/*!
 * \internal
 * \brief Implements PJSIP_HEADER_PARAM 'add' by adding the specified parameter.
 * \note Unlike add_header, we can't add parameters in the outgoing_request callback: that's too late.
 *       That's why we do it here and not in a callback.
 */
static int add_param(void *obj)
{
	struct param_data *data = obj;
	struct ast_sip_session *session = data->channel->session;
	pj_pool_t *pool = session->inv_session->dlg->pool;

	pjsip_fromto_hdr *dlg_info;
	pjsip_name_addr *dlg_info_name_addr;
	pjsip_sip_uri *dlg_info_uri;

	dlg_info = session->inv_session->dlg->local.info; /* Local for outgoing */
	dlg_info_name_addr = (pjsip_name_addr *) dlg_info->uri;
	dlg_info_uri = pjsip_uri_get_uri(dlg_info_name_addr);
	if (!PJSIP_URI_SCHEME_IS_SIP(dlg_info_uri) && !PJSIP_URI_SCHEME_IS_SIPS(dlg_info_uri)) {
		ast_log(LOG_WARNING, "Non SIP/SIPS URI\n");
		return -1;
	}

	ast_debug(1, "Adding custom %s param %s = %s\n",
		data->paramtype == PARAMETER_URI ? "URI" : "header", data->param_name, data->param_value);

	/* This works the same as doing this in set_from_header in res_pjsip_session.c
	 * The way that this maps to pjproject is a little confusing.
	 * Say we have <sip:foo@bar.com;p1=abc;p2=def?h1=qrs&h2=tuv>;o1=foo;o2=bar
	 * p1 and p2 are URI parameters.
	 * (h1 and h2 are URI headers)
	 * o1 and o2 are header parameters (and don't have anything to do with the URI)
	 * In pjproject, other_param is used for adding all custom parameters.
	 * We use the URI for URI stuff, including URI parameters, and the header directly for header parameters.
	 */

#define param_add(pool, list, pname, pvalue) { \
	pjsip_param *param; \
	param = PJ_POOL_ALLOC_T(pool, pjsip_param); \
	pj_strdup2(pool, &param->name, pname); \
	pj_strdup2(pool, &param->value, pvalue); \
	pj_list_insert_before(list, param); \
}

	if (data->paramtype == PARAMETER_URI) { /* URI parameter */
		param_add(pool, &dlg_info_uri->other_param, data->param_name, S_OR(data->param_value, ""));
	} else { /* Header parameter */
		param_add(pool, &dlg_info->other_param, data->param_name, S_OR(data->param_value, ""));
	}

	return 0;
}

static int func_read_param(struct ast_channel *chan, const char *function, char *data, char *buf, size_t len)
{
	struct ast_sip_channel_pvt *channel = chan ? ast_channel_tech_pvt(chan) : NULL;
	struct param_data param_data;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(header_name);
		AST_APP_ARG(param_type);
		AST_APP_ARG(param_name);
	);

	AST_STANDARD_APP_ARGS(args, data);

	param_data.channel = channel;

	if (!channel || strncmp(ast_channel_name(chan), "PJSIP/", 6)) {
		ast_log(LOG_ERROR, "This function requires a PJSIP channel.\n");
		return -1;
	}
	if (ast_strlen_zero(args.param_type)) {
		ast_log(AST_LOG_ERROR, "This function requires a parameter type.\n");
		return -1;
	}
	if (ast_strlen_zero(args.param_name)) {
		ast_log(AST_LOG_ERROR, "This function requires a parameter name.\n");
		return -1;
	}

	/* Currently, only From is supported, but this could be extended in the future. */
	if (ast_strlen_zero(args.header_name) || strcasecmp(args.header_name, "From")) {
		ast_log(LOG_WARNING, "Only the From header is currently supported\n");
		return -1;
	}

	param_data.param_name = args.param_name;
	if (!strcasecmp(args.param_type, "header")) {
		param_data.paramtype = PARAMETER_HEADER;
	} else if (!strcasecmp(args.param_type, "uri")) {
		param_data.paramtype = PARAMETER_URI;
	} else {
		ast_log(LOG_WARNING, "Parameter type '%s' is invalid: must be 'header' or 'uri'\n", args.param_type);
		return -1;
	}

	param_data.buf = buf;
	param_data.len = len;

	return ast_sip_push_task_wait_serializer(channel->session->serializer, read_param, &param_data);
}

static int func_write_param(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_sip_channel_pvt *channel = chan ? ast_channel_tech_pvt(chan) : NULL;
	struct param_data param_data;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(header_name);
		AST_APP_ARG(param_type);
		AST_APP_ARG(param_name);
	);

	AST_STANDARD_APP_ARGS(args, data);

	param_data.channel = channel;

	if (!channel || strncmp(ast_channel_name(chan), "PJSIP/", 6)) {
		ast_log(LOG_ERROR, "This function requires a PJSIP channel.\n");
		return -1;
	}
	if (ast_strlen_zero(args.param_type)) {
		ast_log(AST_LOG_ERROR, "This function requires a parameter type.\n");
		return -1;
	}
	if (ast_strlen_zero(args.param_name)) {
		ast_log(AST_LOG_ERROR, "This function requires a parameter name.\n");
		return -1;
	}

	/* Currently, only From is supported, but this could be extended in the future. */
	if (ast_strlen_zero(args.header_name) || strcasecmp(args.header_name, "From")) {
		ast_log(LOG_WARNING, "Only the From header is currently supported\n");
		return -1;
	}

	param_data.param_name = args.param_name;
	if (!strcasecmp(args.param_type, "header")) {
		param_data.paramtype = PARAMETER_HEADER;
	} else if (!strcasecmp(args.param_type, "uri")) {
		param_data.paramtype = PARAMETER_URI;
	} else {
		ast_log(LOG_WARNING, "Parameter type '%s' is invalid: must be 'header' or 'uri'\n", args.param_type);
		return -1;
	}
	param_data.param_value = value;

	return ast_sip_push_task_wait_serializer(channel->session->serializer, add_param, &param_data);
}

static struct ast_custom_function pjsip_header_param_function = {
	.name = "PJSIP_HEADER_PARAM",
	.read = func_read_param,
	.write = func_write_param,
};

static int load_module(void)
{
	ast_sip_session_register_supplement(&header_funcs_supplement);
	ast_custom_function_register(&pjsip_header_function);
	ast_custom_function_register(&pjsip_headers_function);
	ast_custom_function_register(&pjsip_response_header_function);
	ast_custom_function_register(&pjsip_response_headers_function);
	ast_custom_function_register(&pjsip_header_param_function);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_custom_function_unregister(&pjsip_header_function);
	ast_custom_function_unregister(&pjsip_headers_function);
	ast_custom_function_unregister(&pjsip_response_header_function);
	ast_custom_function_unregister(&pjsip_response_headers_function);
	ast_custom_function_unregister(&pjsip_header_param_function);
	ast_sip_session_unregister_supplement(&header_funcs_supplement);
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "PJSIP Header Functions",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_APP_DEPEND,
	.requires = "res_pjsip,res_pjsip_session",
);
