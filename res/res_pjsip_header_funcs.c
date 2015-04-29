/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Fairview 5 Engineering, LLC
 *
 * George Joseph <george.joseph@fairview5.com>
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
	<load_priority>app_depend</load_priority>
	<depend>pjproject</depend>
	<use type="module">res_pjsip</use>
	<use type="module">res_pjsip_session</use>
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
			Gets, adds, updates or removes the specified SIP header from a PJSIP session.
		</synopsis>
		<syntax>
			<parameter name="action" required="true">
				<enumlist>
					<enum name="read"><para>Returns instance <replaceable>number</replaceable>
					of header <replaceable>name</replaceable>.</para></enum>

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
			<para>Examples:</para>
			<para>;</para>
			<para>; Set 'somevar' to the value of the 'From' header.</para>
			<para>exten => 1,1,Set(somevar=${PJSIP_HEADER(read,From)})</para>
			<para>;</para>
			<para>; Set 'via2' to the value of the 2nd 'Via' header.</para>
			<para>exten => 1,1,Set(via2=${PJSIP_HEADER(read,Via,2)})</para>
			<para>;</para>
			<para>; Add an 'X-Myheader' header with the value of 'myvalue'.</para>
			<para>exten => 1,1,Set(PJSIP_HEADER(add,X-MyHeader)=myvalue)</para>
			<para>;</para>
			<para>; Add an 'X-Myheader' header with an empty value.</para>
			<para>exten => 1,1,Set(PJSIP_HEADER(add,X-MyHeader)=)</para>
			<para>;</para>
			<para>; Update the value of the header named 'X-Myheader' to 'newvalue'.</para>
			<para>; 'X-Myheader' must already exist or the call will fail.</para>
			<para>exten => 1,1,Set(PJSIP_HEADER(update,X-MyHeader)=newvalue)</para>
			<para>;</para>
			<para>; Remove all headers whose names exactly match 'X-MyHeader'.</para>
			<para>exten => 1,1,Set(PJSIP_HEADER(remove,X-MyHeader)=)</para>
			<para>;</para>
			<para>; Remove all headers that begin with 'X-My'.</para>
			<para>exten => 1,1,Set(PJSIP_HEADER(remove,X-My*)=)</para>
			<para>;</para>
			<para>; Remove all previously added headers.</para>
			<para>exten => 1,1,Set(PJSIP_HEADER(remove,*)=)</para>
			<para>;</para>

			<note><para>The <literal>remove</literal> action can be called by reading
			<emphasis>or</emphasis> writing PJSIP_HEADER.</para>
			<para>;</para>
			<para>; Display the number of headers removed</para>
			<para>exten => 1,1,Verbose( Removed ${PJSIP_HEADER(remove,X-MyHeader)} headers)</para>
			<para>;</para>
			<para>; Set a variable to the number of headers removed</para>
			<para>exten => 1,1,Set(count=${PJSIP_HEADER(remove,X-MyHeader)})</para>
			<para>;</para>
			<para>; Just remove them ignoring any count</para>
			<para>exten => 1,1,Set(=${PJSIP_HEADER(remove,X-MyHeader)})</para>
			<para>exten => 1,1,Set(PJSIP_HEADER(remove,X-MyHeader)=)</para>
			<para>;</para>
			</note>

			<note><para>If you call PJSIP_HEADER in a normal dialplan context you'll be
			operating on the <emphasis>caller's (incoming)</emphasis> channel which
			may not be what you want.  To operate on the <emphasis>callee's (outgoing)</emphasis>
			channel call PJSIP_HEADER in a pre-dial handler. </para>
			<para>Example:</para>
			<para>;</para>
			<para>[handler]</para>
			<para>exten => addheader,1,Set(PJSIP_HEADER(add,X-MyHeader)=myvalue)</para>
			<para>exten => addheader,2,Set(PJSIP_HEADER(add,X-MyHeader2)=myvalue2)</para>
			<para>;</para>
			<para>[somecontext]</para>
			<para>exten => 1,1,Dial(PJSIP/${EXTEN},,b(handler^addheader^1))</para>
			<para>;</para>
			</note>
		</description>
	</function>
 ***/

/*! \brief Linked list for accumulating headers */
struct hdr_list_entry {
	pjsip_hdr *hdr;
	AST_LIST_ENTRY(hdr_list_entry) nextptr;
};
AST_LIST_HEAD(hdr_list, hdr_list_entry);

/*! \brief Destructor for hdr_list */
static void hdr_list_destroy(void *obj)
{
	AST_LIST_HEAD_DESTROY((struct hdr_list *) obj);
}

/*! \brief Datastore for saving headers */
static const struct ast_datastore_info header_datastore = {
	.type = "header_datastore",
	.destroy = hdr_list_destroy,
};

/*! \brief Data structure used for ast_sip_push_task_synchronous  */
struct header_data {
	struct ast_sip_channel_pvt *channel;
	char *header_name;
	const char *header_value;
	char *buf;
	int header_number;
	size_t len;
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
		AST_LIST_HEAD_INIT((struct hdr_list *) datastore->data);
	}
	insert_headers(pool, (struct hdr_list *) datastore->data, rdata->msg_info.msg);

	return 0;
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
 * \brief Implements PJSIP_HEADER 'read' by searching the for the requested header.
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
	pjsip_hdr *hdr = NULL;
	char *pj_hdr_string;
	size_t pj_hdr_string_len;
	char *p;
	size_t plen;
	RAII_VAR(struct ast_datastore *, datastore,
			 ast_sip_session_get_datastore(data->channel->session, header_datastore.type),
			 ao2_cleanup);

	if (!datastore || !datastore->data) {
		ast_debug(1, "There was no datastore from which to read headers.\n");
		return -1;
	}

	hdr = find_header((struct hdr_list *) datastore->data, data->header_name,
					  data->header_number);

	if (!hdr) {
		ast_debug(1, "There was no header named %s.\n", data->header_name);
		return -1;
	}

	pj_hdr_string = ast_alloca(data->len);
	pj_hdr_string_len = pjsip_hdr_print_on(hdr, pj_hdr_string, data->len);
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
 * \brief Implements PJSIP_HEADER 'add' by inserting the specified header into thge list.
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
			 ast_sip_session_get_datastore(session, header_datastore.type), ao2_cleanup);

	if (!datastore) {
		if (!(datastore = ast_sip_session_alloc_datastore(&header_datastore,
														  header_datastore.type))
			|| !(datastore->data = pj_pool_alloc(pool, sizeof(struct hdr_list)))
			|| ast_sip_session_add_datastore(session, datastore)) {
			ast_log(AST_LOG_ERROR, "Unable to create datastore for header functions.\n");
			return -1;
		}
		AST_LIST_HEAD_INIT((struct hdr_list *) datastore->data);
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
			 ast_sip_session_get_datastore(data->channel->session, header_datastore.type),
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
			 ast_sip_session_get_datastore(data->channel->session, header_datastore.type),
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
 * \brief Implements function 'read' callback.
 *
 * Valid actions are 'read' and 'remove'.
 */
static int func_read_header(struct ast_channel *chan, const char *function, char *data,
							char *buf, size_t len)
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

	if (stricmp(args.action, "read") == 0) {
		return ast_sip_push_task_synchronous(channel->session->serializer, read_header,
											 &header_data);
	} else if (stricmp(args.action, "remove") == 0) {
		return ast_sip_push_task_synchronous(channel->session->serializer, remove_header,
											 &header_data);
	} else {
		ast_log(AST_LOG_ERROR,
				"Unknown action \'%s\' is not valid,  Must be \'read\' or \'remove\'.\n",
				args.action);
		return -1;
	}
}

/*!
 * \brief Implements function 'write' callback.
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

	if (stricmp(args.action, "add") == 0) {
		return ast_sip_push_task_synchronous(channel->session->serializer, add_header,
											 &header_data);
	} else if (stricmp(args.action, "update") == 0) {
		return ast_sip_push_task_synchronous(channel->session->serializer, update_header,
											 &header_data);
	} else if (stricmp(args.action, "remove") == 0) {
		return ast_sip_push_task_synchronous(channel->session->serializer, remove_header,
											 &header_data);
	} else {
		ast_log(AST_LOG_ERROR,
				"Unknown action \'%s\' is not valid,  Must be \'add\', \'update\', or \'remove\'.\n",
				args.action);
		return -1;
	}
}

static struct ast_custom_function pjsip_header_function = {
	.name = "PJSIP_HEADER",
	.read = func_read_header,
	.write = func_write_header,
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
	pj_pool_t *pool = session->inv_session->dlg->pool;
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
		pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) pjsip_hdr_clone(pool, le->hdr));
	}
	ast_sip_session_remove_datastore(session, datastore->uid);
}

static struct ast_sip_session_supplement header_funcs_supplement = {
	.method = "INVITE",
	.priority = AST_SIP_SUPPLEMENT_PRIORITY_CHANNEL - 1000,
	.incoming_request = incoming_request,
	.outgoing_request = outgoing_request,
};

static int load_module(void)
{
	ast_sip_session_register_supplement(&header_funcs_supplement);
	ast_custom_function_register(&pjsip_header_function);

	return AST_MODULE_LOAD_SUCCESS;
}

static void unload_module(void)
{
	ast_sip_session_unregister_supplement(&header_funcs_supplement);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "PJSIP Header Functions");
