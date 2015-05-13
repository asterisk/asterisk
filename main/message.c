/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2010, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*! \file
 *
 * \brief Out-of-call text message support
 *
 * \author Russell Bryant <russell@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/_private.h"

#include "asterisk/module.h"
#include "asterisk/datastore.h"
#include "asterisk/pbx.h"
#include "asterisk/manager.h"
#include "asterisk/strings.h"
#include "asterisk/astobj2.h"
#include "asterisk/app.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/message.h"

/*** DOCUMENTATION
	<function name="MESSAGE" language="en_US">
		<synopsis>
			Create a message or read fields from a message.
		</synopsis>
		<syntax argsep="/">
			<parameter name="argument" required="true">
			<para>Field of the message to get or set.</para>
			<enumlist>
				<enum name="to">
					<para>Read-only.  The destination of the message.  When processing an
					incoming message, this will be set to the destination listed as
					the recipient of the message that was received by Asterisk.</para>
				</enum>
				<enum name="from">
					<para>Read-only.  The source of the message.  When processing an
					incoming message, this will be set to the source of the message.</para>
				</enum>
				<enum name="custom_data">
					<para>Write-only.  Mark or unmark all message headers for an outgoing
					message.  The following values can be set:</para>
					<enumlist>
						<enum name="mark_all_outbound">
							<para>Mark all headers for an outgoing message.</para>
						</enum>
						<enum name="clear_all_outbound">
							<para>Unmark all headers for an outgoing message.</para>
						</enum>
					</enumlist>
				</enum>
				<enum name="body">
					<para>Read/Write.  The message body.  When processing an incoming
					message, this includes the body of the message that Asterisk
					received.  When MessageSend() is executed, the contents of this
					field are used as the body of the outgoing message.  The body
					will always be UTF-8.</para>
				</enum>
			</enumlist>
			</parameter>
		</syntax>
		<description>
			<para>This function will read from or write a value to a text message.
			It is used both to read the data out of an incoming message, as well as
			modify or create a message that will be sent outbound.</para>
		</description>
		<see-also>
			<ref type="application">MessageSend</ref>
		</see-also>
	</function>
	<function name="MESSAGE_DATA" language="en_US">
		<synopsis>
			Read or write custom data attached to a message.
		</synopsis>
		<syntax argsep="/">
			<parameter name="argument" required="true">
			<para>Field of the message to get or set.</para>
			</parameter>
		</syntax>
		<description>
			<para>This function will read from or write a value to a text message.
			It is used both to read the data out of an incoming message, as well as
			modify a message that will be sent outbound.</para>
			<note>
				<para>If you want to set an outbound message to carry data in the
				current message, do
				Set(MESSAGE_DATA(<replaceable>key</replaceable>)=${MESSAGE_DATA(<replaceable>key</replaceable>)}).</para>
			</note>
		</description>
		<see-also>
			<ref type="application">MessageSend</ref>
		</see-also>
	</function>
	<application name="MessageSend" language="en_US">
		<synopsis>
			Send a text message.
		</synopsis>
		<syntax>
			<parameter name="to" required="true">
				<para>A To URI for the message.</para>
				<xi:include xpointer="xpointer(/docs/info[@name='SIPMessageToInfo'])" />
				<xi:include xpointer="xpointer(/docs/info[@name='XMPPMessageToInfo'])" />
			</parameter>
			<parameter name="from" required="false">
				<para>A From URI for the message if needed for the
				message technology being used to send this message.</para>
				<xi:include xpointer="xpointer(/docs/info[@name='SIPMessageFromInfo'])" />
				<xi:include xpointer="xpointer(/docs/info[@name='XMPPMessageFromInfo'])" />
			</parameter>
		</syntax>
		<description>
			<para>Send a text message.  The body of the message that will be
			sent is what is currently set to <literal>MESSAGE(body)</literal>.
			  The technology chosen for sending the message is determined
			based on a prefix to the <literal>to</literal> parameter.</para>
			<para>This application sets the following channel variables:</para>
			<variablelist>
				<variable name="MESSAGE_SEND_STATUS">
					<para>This is the message delivery status returned by this application.</para>
					<value name="INVALID_PROTOCOL">
						No handler for the technology part of the URI was found.
					</value>
					<value name="INVALID_URI">
						The protocol handler reported that the URI was not valid.
					</value>
					<value name="SUCCESS">
						Successfully passed on to the protocol handler, but delivery has not necessarily been guaranteed.
					</value>
					<value name="FAILURE">
						The protocol handler reported that it was unabled to deliver the message for some reason.
					</value>
				</variable>
			</variablelist>
		</description>
	</application>
	<manager name="MessageSend" language="en_US">
		<synopsis>
			Send an out of call message to an endpoint.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="To" required="true">
				<para>The URI the message is to be sent to.</para>
				<xi:include xpointer="xpointer(/docs/info[@name='SIPMessageToInfo'])" />
				<xi:include xpointer="xpointer(/docs/info[@name='XMPPMessageToInfo'])" />
			</parameter>
			<parameter name="From">
				<para>A From URI for the message if needed for the
				message technology being used to send this message.</para>
				<xi:include xpointer="xpointer(/docs/info[@name='SIPMessageFromInfo'])" />
				<xi:include xpointer="xpointer(/docs/info[@name='XMPPMessageFromInfo'])" />
			</parameter>
			<parameter name="Body">
				<para>The message body text.  This must not contain any newlines as that
				conflicts with the AMI protocol.</para>
			</parameter>
			<parameter name="Base64Body">
				<para>Text bodies requiring the use of newlines have to be base64 encoded
				in this field.  Base64Body will be decoded before being sent out.
				Base64Body takes precedence over Body.</para>
			</parameter>
			<parameter name="Variable">
				<para>Message variable to set, multiple Variable: headers are
				allowed.  The header value is a comma separated list of
				name=value pairs.</para>
			</parameter>
		</syntax>
	</manager>
 ***/

struct msg_data {
	AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(name);
		AST_STRING_FIELD(value);
	);
	unsigned int send:1; /* Whether to send out on outbound messages */
};

AST_LIST_HEAD_NOLOCK(outhead, msg_data);

/*!
 * \brief A message.
 *
 * \todo Consider whether stringfields would be an appropriate optimization here.
 */
struct ast_msg {
	struct ast_str *to;
	struct ast_str *from;
	struct ast_str *body;
	struct ast_str *context;
	struct ast_str *exten;
	struct ao2_container *vars;
};

struct ast_msg_tech_holder {
	const struct ast_msg_tech *tech;
	/*!
	 * \brief A rwlock for this object
	 *
	 * a read/write lock must be used to protect the wrapper instead
	 * of the ao2 lock. A rdlock must be held to read tech_holder->tech.
	 */
	ast_rwlock_t tech_lock;
};

static struct ao2_container *msg_techs;

static struct ast_taskprocessor *msg_q_tp;

static const char app_msg_send[] = "MessageSend";

static void msg_ds_destroy(void *data);

static const struct ast_datastore_info msg_datastore = {
	.type = "message",
	.destroy = msg_ds_destroy,
};

static int msg_func_read(struct ast_channel *chan, const char *function,
		char *data, char *buf, size_t len);
static int msg_func_write(struct ast_channel *chan, const char *function,
		char *data, const char *value);

static struct ast_custom_function msg_function = {
	.name = "MESSAGE",
	.read = msg_func_read,
	.write = msg_func_write,
};

static int msg_data_func_read(struct ast_channel *chan, const char *function,
		char *data, char *buf, size_t len);
static int msg_data_func_write(struct ast_channel *chan, const char *function,
		char *data, const char *value);

static struct ast_custom_function msg_data_function = {
	.name = "MESSAGE_DATA",
	.read = msg_data_func_read,
	.write = msg_data_func_write,
};

static struct ast_frame *chan_msg_read(struct ast_channel *chan);
static int chan_msg_write(struct ast_channel *chan, struct ast_frame *fr);
static int chan_msg_indicate(struct ast_channel *chan, int condition,
		const void *data, size_t datalen);
static int chan_msg_send_digit_begin(struct ast_channel *chan, char digit);
static int chan_msg_send_digit_end(struct ast_channel *chan, char digit,
		unsigned int duration);

/*!
 * \internal
 * \brief A bare minimum channel technology
 *
 * This will not be registered as we never want anything to try
 * to create Message channels other than internally in this file.
 */
static const struct ast_channel_tech msg_chan_tech_hack = {
	.type             = "Message",
	.description      = "Internal Text Message Processing",
	.read             = chan_msg_read,
	.write            = chan_msg_write,
	.indicate         = chan_msg_indicate,
	.send_digit_begin = chan_msg_send_digit_begin,
	.send_digit_end   = chan_msg_send_digit_end,
};

/*!
 * \internal
 * \brief ast_channel_tech read callback
 *
 * This should never be called.  However, we say that about chan_iax2's
 * read callback, too, and it seems to randomly get called for some
 * reason.  If it does, a simple NULL frame will suffice.
 */
static struct ast_frame *chan_msg_read(struct ast_channel *chan)
{
	return &ast_null_frame;
}

/*!
 * \internal
 * \brief ast_channel_tech write callback
 *
 * Throw all frames away.  We don't care about any of them.
 */
static int chan_msg_write(struct ast_channel *chan, struct ast_frame *fr)
{
	return 0;
}

/*!
 * \internal
 * \brief ast_channel_tech indicate callback
 *
 * The indicate callback is here just so it can return success.
 * We don't want any callers of ast_indicate() to think something
 * has failed.  We also don't want ast_indicate() itself to try
 * to generate inband tones since we didn't tell it that we took
 * care of it ourselves.
 */
static int chan_msg_indicate(struct ast_channel *chan, int condition,
		const void *data, size_t datalen)
{
	return 0;
}

/*!
 * \internal
 * \brief ast_channel_tech send_digit_begin callback
 *
 * This is here so that just in case a digit comes at a message channel
 * that the Asterisk core doesn't waste any time trying to generate
 * inband DTMF in audio.  It's a waste of resources.
 */
static int chan_msg_send_digit_begin(struct ast_channel *chan, char digit)
{
	return 0;
}

/*!
 * \internal
 * \brief ast_channel_tech send_digit_end callback
 *
 * This is here so that just in case a digit comes at a message channel
 * that the Asterisk core doesn't waste any time trying to generate
 * inband DTMF in audio.  It's a waste of resources.
 */
static int chan_msg_send_digit_end(struct ast_channel *chan, char digit,
		unsigned int duration)
{
	return 0;
}

static void msg_ds_destroy(void *data)
{
	struct ast_msg *msg = data;

	ao2_ref(msg, -1);
}

static int msg_data_hash_fn(const void *obj, const int flags)
{
	const struct msg_data *data = obj;
	return ast_str_case_hash(data->name);
}

static int msg_data_cmp_fn(void *obj, void *arg, int flags)
{
	const struct msg_data *one = obj, *two = arg;
	return !strcasecmp(one->name, two->name) ? CMP_MATCH | CMP_STOP : 0;
}

static void msg_data_destructor(void *obj)
{
	struct msg_data *data = obj;
	ast_string_field_free_memory(data);
}

static void msg_destructor(void *obj)
{
	struct ast_msg *msg = obj;

	ast_free(msg->to);
	msg->to = NULL;

	ast_free(msg->from);
	msg->from = NULL;

	ast_free(msg->body);
	msg->body = NULL;

	ast_free(msg->context);
	msg->context = NULL;

	ast_free(msg->exten);
	msg->exten = NULL;

	ao2_ref(msg->vars, -1);
}

struct ast_msg *ast_msg_alloc(void)
{
	struct ast_msg *msg;

	if (!(msg = ao2_alloc(sizeof(*msg), msg_destructor))) {
		return NULL;
	}

	if (!(msg->to = ast_str_create(32))) {
		ao2_ref(msg, -1);
		return NULL;
	}

	if (!(msg->from = ast_str_create(32))) {
		ao2_ref(msg, -1);
		return NULL;
	}

	if (!(msg->body = ast_str_create(128))) {
		ao2_ref(msg, -1);
		return NULL;
	}

	if (!(msg->context = ast_str_create(16))) {
		ao2_ref(msg, -1);
		return NULL;
	}

	if (!(msg->exten = ast_str_create(16))) {
		ao2_ref(msg, -1);
		return NULL;
	}

	if (!(msg->vars = ao2_container_alloc(1, msg_data_hash_fn, msg_data_cmp_fn))) {
		ao2_ref(msg, -1);
		return NULL;
	}

	ast_str_set(&msg->context, 0, "default");

	return msg;
}

struct ast_msg *ast_msg_ref(struct ast_msg *msg)
{
	ao2_ref(msg, 1);
	return msg;
}

struct ast_msg *ast_msg_destroy(struct ast_msg *msg)
{
	ao2_ref(msg, -1);

	return NULL;
}

int ast_msg_set_to(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = ast_str_set_va(&msg->to, 0, fmt, ap);
	va_end(ap);

	return res < 0 ? -1 : 0;
}

int ast_msg_set_from(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = ast_str_set_va(&msg->from, 0, fmt, ap);
	va_end(ap);

	return res < 0 ? -1 : 0;
}

int ast_msg_set_body(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = ast_str_set_va(&msg->body, 0, fmt, ap);
	va_end(ap);

	return res < 0 ? -1 : 0;
}

int ast_msg_set_context(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = ast_str_set_va(&msg->context, 0, fmt, ap);
	va_end(ap);

	return res < 0 ? -1 : 0;
}

int ast_msg_set_exten(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = ast_str_set_va(&msg->exten, 0, fmt, ap);
	va_end(ap);

	return res < 0 ? -1 : 0;
}

const char *ast_msg_get_body(const struct ast_msg *msg)
{
	return ast_str_buffer(msg->body);
}

static struct msg_data *msg_data_alloc(void)
{
	struct msg_data *data;

	if (!(data = ao2_alloc(sizeof(*data), msg_data_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(data, 32)) {
		ao2_ref(data, -1);
		return NULL;
	}

	return data;
}

static struct msg_data *msg_data_find(struct ao2_container *vars, const char *name)
{
	struct msg_data tmp = {
		.name = name,
	};
	return ao2_find(vars, &tmp, OBJ_POINTER);
}

static int msg_set_var_full(struct ast_msg *msg, const char *name, const char *value, unsigned int outbound)
{
	struct msg_data *data;

	if (!(data = msg_data_find(msg->vars, name))) {
		if (ast_strlen_zero(value)) {
			return 0;
		}
		if (!(data = msg_data_alloc())) {
			return -1;
		};

		ast_string_field_set(data, name, name);
		ast_string_field_set(data, value, value);
		data->send = outbound;
		ao2_link(msg->vars, data);
	} else {
		if (ast_strlen_zero(value)) {
			ao2_unlink(msg->vars, data);
		} else {
			ast_string_field_set(data, value, value);
			data->send = outbound;
		}
	}

	ao2_ref(data, -1);

	return 0;
}

int ast_msg_set_var_outbound(struct ast_msg *msg, const char *name, const char *value)
{
	return msg_set_var_full(msg, name, value, 1);
}

int ast_msg_set_var(struct ast_msg *msg, const char *name, const char *value)
{
	return msg_set_var_full(msg, name, value, 0);
}

const char *ast_msg_get_var(struct ast_msg *msg, const char *name)
{
	struct msg_data *data;
	const char *val = NULL;

	if (!(data = msg_data_find(msg->vars, name))) {
		return NULL;
	}

	/* Yep, this definitely looks like val would be a dangling pointer
	 * after the ref count is decremented.  As long as the message structure
	 * is used in a thread safe manner, this will not be the case though.
	 * The ast_msg holds a reference to this object in the msg->vars container. */
	val = data->value;
	ao2_ref(data, -1);

	return val;
}

struct ast_msg_var_iterator {
	struct ao2_iterator iter;
	struct msg_data *current_used;
};

struct ast_msg_var_iterator *ast_msg_var_iterator_init(const struct ast_msg *msg)
{
	struct ast_msg_var_iterator *iter;

	iter = ast_calloc(1, sizeof(*iter));
	if (!iter) {
		return NULL;
	}

	iter->iter = ao2_iterator_init(msg->vars, 0);

	return iter;
}

int ast_msg_var_iterator_next(const struct ast_msg *msg, struct ast_msg_var_iterator *iter, const char **name, const char **value)
{
	struct msg_data *data;

	if (!iter) {
		return 0;
	}

	/* Skip any that aren't marked for sending out */
	while ((data = ao2_iterator_next(&iter->iter)) && !data->send) {
		ao2_ref(data, -1);
	}

	if (!data) {
		return 0;
	}

	if (data->send) {
		*name = data->name;
		*value = data->value;
	}

	/* Leave the refcount to be cleaned up by the caller with
	 * ast_msg_var_unref_current after they finish with the pointers to the data */
	iter->current_used = data;

	return 1;
}

void ast_msg_var_unref_current(struct ast_msg_var_iterator *iter)
{
	ao2_cleanup(iter->current_used);
	iter->current_used = NULL;
}

void ast_msg_var_iterator_destroy(struct ast_msg_var_iterator *iter)
{
	if (iter) {
		ao2_iterator_destroy(&iter->iter);
		ast_msg_var_unref_current(iter);
		ast_free(iter);
	}
}

static struct ast_channel *create_msg_q_chan(void)
{
	struct ast_channel *chan;
	struct ast_datastore *ds;

	chan = ast_channel_alloc(1, AST_STATE_UP,
			NULL, NULL, NULL,
			NULL, NULL, NULL, 0,
			"%s", "Message/ast_msg_queue");

	if (!chan) {
		return NULL;
	}

	ast_channel_unlink(chan);

	ast_channel_tech_set(chan, &msg_chan_tech_hack);

	if (!(ds = ast_datastore_alloc(&msg_datastore, NULL))) {
		ast_hangup(chan);
		return NULL;
	}

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, ds);
	ast_channel_unlock(chan);

	return chan;
}

/*!
 * \internal
 * \brief Run the dialplan for message processing
 *
 * \pre The message has already been set up on the msg datastore
 *      on this channel.
 */
static void msg_route(struct ast_channel *chan, struct ast_msg *msg)
{
	struct ast_pbx_args pbx_args;

	ast_explicit_goto(chan, ast_str_buffer(msg->context), AS_OR(msg->exten, "s"), 1);

	memset(&pbx_args, 0, sizeof(pbx_args));
	pbx_args.no_hangup_chan = 1,
	ast_pbx_run_args(chan, &pbx_args);
}

/*!
 * \internal
 * \brief Clean up ast_channel after each message
 *
 * Reset various bits of state after routing each message so the same ast_channel
 * can just be reused.
 */
static void chan_cleanup(struct ast_channel *chan)
{
	struct ast_datastore *msg_ds, *ds;
	struct varshead *headp;
	struct ast_var_t *vardata;
	struct ast_frame *cur;

	ast_channel_lock(chan);

	/*
	 * Remove the msg datastore.  Free its data but keep around the datastore
	 * object and just reuse it.
	 */
	if ((msg_ds = ast_channel_datastore_find(chan, &msg_datastore, NULL)) && msg_ds->data) {
		ast_channel_datastore_remove(chan, msg_ds);
		ao2_ref(msg_ds->data, -1);
		msg_ds->data = NULL;
	}

	/*
	 * Destroy all other datastores.
	 */
	while ((ds = AST_LIST_REMOVE_HEAD(ast_channel_datastores(chan), entry))) {
		ast_datastore_free(ds);
	}

	/*
	 * Destroy all channel variables.
	 */
	headp = ast_channel_varshead(chan);
	while ((vardata = AST_LIST_REMOVE_HEAD(headp, entries))) {
		ast_var_delete(vardata);
	}

	/*
	 * Remove frames from read queue
	 */
	while ((cur = AST_LIST_REMOVE_HEAD(ast_channel_readq(chan), frame_list))) {
		ast_frfree(cur);
	}

	/*
	 * Restore msg datastore.
	 */
	if (msg_ds) {
		ast_channel_datastore_add(chan, msg_ds);
	}
	/*
	 * Clear softhangup flags.
	 */
	ast_channel_clear_softhangup(chan, AST_SOFTHANGUP_ALL);

	ast_channel_unlock(chan);
}

static void destroy_msg_q_chan(void *data)
{
	struct ast_channel **chan = data;

	if (!*chan) {
		return;
	}

	ast_channel_release(*chan);
}

AST_THREADSTORAGE_CUSTOM(msg_q_chan, NULL, destroy_msg_q_chan);

/*!
 * \internal
 * \brief Message queue task processor callback
 *
 * \retval 0 success
 * \retval -1 failure
 *
 * \note Even though this returns a value, the taskprocessor code ignores the value.
 */
static int msg_q_cb(void *data)
{
	struct ast_msg *msg = data;
	struct ast_channel **chan_p, *chan;
	struct ast_datastore *ds;

	if (!(chan_p = ast_threadstorage_get(&msg_q_chan, sizeof(struct ast_channel *)))) {
		return -1;
	}
	if (!*chan_p) {
		if (!(*chan_p = create_msg_q_chan())) {
			return -1;
		}
	}
	chan = *chan_p;

	ast_channel_lock(chan);
	if (!(ds = ast_channel_datastore_find(chan, &msg_datastore, NULL))) {
		ast_channel_unlock(chan);
		return -1;
	}
	ao2_ref(msg, +1);
	ds->data = msg;
	ast_channel_unlock(chan);

	msg_route(chan, msg);
	chan_cleanup(chan);

	ao2_ref(msg, -1);

	return 0;
}

int ast_msg_queue(struct ast_msg *msg)
{
	int res;

	res = ast_taskprocessor_push(msg_q_tp, msg_q_cb, msg);
	if (res == -1) {
		ao2_ref(msg, -1);
	}

	return res;
}

/*!
 * \internal
 * \brief Find or create a message datastore on a channel
 *
 * \pre chan is locked
 *
 * \param chan the relevant channel
 *
 * \return the channel's message datastore, or NULL on error
 */
static struct ast_datastore *msg_datastore_find_or_create(struct ast_channel *chan)
{
	struct ast_datastore *ds;

	if ((ds = ast_channel_datastore_find(chan, &msg_datastore, NULL))) {
		return ds;
	}

	if (!(ds = ast_datastore_alloc(&msg_datastore, NULL))) {
		return NULL;
	}

	if (!(ds->data = ast_msg_alloc())) {
		ast_datastore_free(ds);
		return NULL;
	}

	ast_channel_datastore_add(chan, ds);

	return ds;
}

static int msg_func_read(struct ast_channel *chan, const char *function,
		char *data, char *buf, size_t len)
{
	struct ast_datastore *ds;
	struct ast_msg *msg;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", function);
		return -1;
	}

	ast_channel_lock(chan);

	if (!(ds = ast_channel_datastore_find(chan, &msg_datastore, NULL))) {
		ast_channel_unlock(chan);
		ast_log(LOG_ERROR, "No MESSAGE data found on the channel to read.\n");
		return -1;
	}

	msg = ds->data;
	ao2_ref(msg, +1);
	ast_channel_unlock(chan);

	ao2_lock(msg);

	if (!strcasecmp(data, "to")) {
		ast_copy_string(buf, ast_str_buffer(msg->to), len);
	} else if (!strcasecmp(data, "from")) {
		ast_copy_string(buf, ast_str_buffer(msg->from), len);
	} else if (!strcasecmp(data, "body")) {
		ast_copy_string(buf, ast_msg_get_body(msg), len);
	} else {
		ast_log(LOG_WARNING, "Invalid argument to MESSAGE(): '%s'\n", data);
	}

	ao2_unlock(msg);
	ao2_ref(msg, -1);

	return 0;
}

static int msg_func_write(struct ast_channel *chan, const char *function,
		char *data, const char *value)
{
	struct ast_datastore *ds;
	struct ast_msg *msg;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", function);
		return -1;
	}

	ast_channel_lock(chan);

	if (!(ds = msg_datastore_find_or_create(chan))) {
		ast_channel_unlock(chan);
		return -1;
	}

	msg = ds->data;
	ao2_ref(msg, +1);
	ast_channel_unlock(chan);

	ao2_lock(msg);

	if (!strcasecmp(data, "to")) {
		ast_msg_set_to(msg, "%s", value);
	} else if (!strcasecmp(data, "from")) {
		ast_msg_set_from(msg, "%s", value);
	} else if (!strcasecmp(data, "body")) {
		ast_msg_set_body(msg, "%s", value);
	} else if (!strcasecmp(data, "custom_data")) {
		int outbound = -1;
		if (!strcasecmp(value, "mark_all_outbound")) {
			outbound = 1;
		} else if (!strcasecmp(value, "clear_all_outbound")) {
			outbound = 0;
		} else {
			ast_log(LOG_WARNING, "'%s' is not a valid value for custom_data\n", value);
		}

		if (outbound != -1) {
			struct msg_data *hdr_data;
			struct ao2_iterator iter = ao2_iterator_init(msg->vars, 0);

			while ((hdr_data = ao2_iterator_next(&iter))) {
				hdr_data->send = outbound;
				ao2_ref(hdr_data, -1);
			}
			ao2_iterator_destroy(&iter);
		}
	} else {
		ast_log(LOG_WARNING, "'%s' is not a valid write argument.\n", data);
	}

	ao2_unlock(msg);
	ao2_ref(msg, -1);

	return 0;
}

static int msg_data_func_read(struct ast_channel *chan, const char *function,
		char *data, char *buf, size_t len)
{
	struct ast_datastore *ds;
	struct ast_msg *msg;
	const char *val;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", function);
		return -1;
	}

	ast_channel_lock(chan);

	if (!(ds = ast_channel_datastore_find(chan, &msg_datastore, NULL))) {
		ast_channel_unlock(chan);
		ast_log(LOG_ERROR, "No MESSAGE data found on the channel to read.\n");
		return -1;
	}

	msg = ds->data;
	ao2_ref(msg, +1);
	ast_channel_unlock(chan);

	ao2_lock(msg);

	if ((val = ast_msg_get_var(msg, data))) {
		ast_copy_string(buf, val, len);
	}

	ao2_unlock(msg);
	ao2_ref(msg, -1);

	return 0;
}

static int msg_data_func_write(struct ast_channel *chan, const char *function,
		char *data, const char *value)
{
	struct ast_datastore *ds;
	struct ast_msg *msg;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", function);
		return -1;
	}

	ast_channel_lock(chan);

	if (!(ds = msg_datastore_find_or_create(chan))) {
		ast_channel_unlock(chan);
		return -1;
	}

	msg = ds->data;
	ao2_ref(msg, +1);
	ast_channel_unlock(chan);

	ao2_lock(msg);

	ast_msg_set_var_outbound(msg, data, value);

	ao2_unlock(msg);
	ao2_ref(msg, -1);

	return 0;
}
static int msg_tech_hash(const void *obj, const int flags)
{
	struct ast_msg_tech_holder *tech_holder = (struct ast_msg_tech_holder *) obj;
	int res = 0;

	ast_rwlock_rdlock(&tech_holder->tech_lock);
	if (tech_holder->tech) {
		res = ast_str_case_hash(tech_holder->tech->name);
	}
	ast_rwlock_unlock(&tech_holder->tech_lock);

	return res;
}

static int msg_tech_cmp(void *obj, void *arg, int flags)
{
	struct ast_msg_tech_holder *tech_holder = obj;
	const struct ast_msg_tech_holder *tech_holder2 = arg;
	int res = 1;

	ast_rwlock_rdlock(&tech_holder->tech_lock);
	/*
	 * tech_holder2 is a temporary fake tech_holder.
	 */
	if (tech_holder->tech) {
		res = strcasecmp(tech_holder->tech->name, tech_holder2->tech->name) ? 0 : CMP_MATCH | CMP_STOP;
	}
	ast_rwlock_unlock(&tech_holder->tech_lock);

	return res;
}

static struct ast_msg_tech_holder *msg_find_by_tech(const struct ast_msg_tech *msg_tech, int ao2_flags)
{
	struct ast_msg_tech_holder *tech_holder;
	struct ast_msg_tech_holder tmp_tech_holder = {
		.tech = msg_tech,
	};

	ast_rwlock_init(&tmp_tech_holder.tech_lock);
	tech_holder = ao2_find(msg_techs, &tmp_tech_holder, ao2_flags);
	ast_rwlock_destroy(&tmp_tech_holder.tech_lock);
	return tech_holder;
}

static struct ast_msg_tech_holder *msg_find_by_tech_name(const char *tech_name, int ao2_flags)
{
	struct ast_msg_tech tmp_msg_tech = {
		.name = tech_name,
	};
	return msg_find_by_tech(&tmp_msg_tech, ao2_flags);
}

/*!
 * \internal
 * \brief MessageSend() application
 */
static int msg_send_exec(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *ds;
	struct ast_msg *msg;
	char *tech_name;
	struct ast_msg_tech_holder *tech_holder = NULL;
	char *parse;
	int res = -1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(to);
		AST_APP_ARG(from);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "An argument is required to MessageSend()\n");
		pbx_builtin_setvar_helper(chan, "MESSAGE_SEND_STATUS", "INVALID_URI");
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.to)) {
		ast_log(LOG_WARNING, "A 'to' URI is required for MessageSend()\n");
		pbx_builtin_setvar_helper(chan, "MESSAGE_SEND_STATUS", "INVALID_URI");
		return 0;
	}

	ast_channel_lock(chan);

	if (!(ds = ast_channel_datastore_find(chan, &msg_datastore, NULL))) {
		ast_channel_unlock(chan);
		ast_log(LOG_WARNING, "No message data found on channel to send.\n");
		pbx_builtin_setvar_helper(chan, "MESSAGE_SEND_STATUS", "FAILURE");
		return 0;
	}

	msg = ds->data;
	ao2_ref(msg, +1);
	ast_channel_unlock(chan);

	tech_name = ast_strdupa(args.to);
	tech_name = strsep(&tech_name, ":");

	tech_holder = msg_find_by_tech_name(tech_name, OBJ_POINTER);

	if (!tech_holder) {
		ast_log(LOG_WARNING, "No message technology '%s' found.\n", tech_name);
		pbx_builtin_setvar_helper(chan, "MESSAGE_SEND_STATUS", "INVALID_PROTOCOL");
		goto exit_cleanup;
	}

	/*
	 * The message lock is held here to safely allow the technology
	 * implementation to access the message fields without worrying
	 * that they could change.
	 */
	ao2_lock(msg);
	ast_rwlock_rdlock(&tech_holder->tech_lock);
	if (tech_holder->tech) {
		res = tech_holder->tech->msg_send(msg, S_OR(args.to, ""),
							S_OR(args.from, ""));
	}
	ast_rwlock_unlock(&tech_holder->tech_lock);
	ao2_unlock(msg);

	pbx_builtin_setvar_helper(chan, "MESSAGE_SEND_STATUS", res ? "FAILURE" : "SUCCESS");

exit_cleanup:
	if (tech_holder) {
		ao2_ref(tech_holder, -1);
		tech_holder = NULL;
	}

	ao2_ref(msg, -1);

	return 0;
}

static int action_messagesend(struct mansession *s, const struct message *m)
{
	const char *to = ast_strdupa(astman_get_header(m, "To"));
	const char *from = astman_get_header(m, "From");
	const char *body = astman_get_header(m, "Body");
	const char *base64body = astman_get_header(m, "Base64Body");
	char base64decoded[1301] = { 0, };
	char *tech_name = NULL;
	struct ast_variable *vars = NULL;
	struct ast_variable *data = NULL;
	struct ast_msg_tech_holder *tech_holder = NULL;
	struct ast_msg *msg;
	int res = -1;

	if (ast_strlen_zero(to)) {
		astman_send_error(s, m, "No 'To' address specified.");
		return 0;
	}

	if (!ast_strlen_zero(base64body)) {
		ast_base64decode((unsigned char *) base64decoded, base64body, sizeof(base64decoded) - 1);
		body = base64decoded;
	}

	tech_name = ast_strdupa(to);
	tech_name = strsep(&tech_name, ":");

	tech_holder = msg_find_by_tech_name(tech_name, OBJ_POINTER);

	if (!tech_holder) {
		astman_send_error(s, m, "Message technology not found.");
		return 0;
	}

	if (!(msg = ast_msg_alloc())) {
		ao2_ref(tech_holder, -1);
		astman_send_error(s, m, "Internal failure\n");
		return 0;
	}

	data = astman_get_variables(m);
	for (vars = data; vars; vars = vars->next) {
		ast_msg_set_var_outbound(msg, vars->name, vars->value);
	}

	ast_msg_set_body(msg, "%s", body);

	ast_rwlock_rdlock(&tech_holder->tech_lock);
	if (tech_holder->tech) {
		res = tech_holder->tech->msg_send(msg, S_OR(to, ""), S_OR(from, ""));
	}
	ast_rwlock_unlock(&tech_holder->tech_lock);

	ast_variables_destroy(vars);
	ao2_ref(tech_holder, -1);
	ao2_ref(msg, -1);

	if (res) {
		astman_send_error(s, m, "Message failed to send.");
	} else {
		astman_send_ack(s, m, "Message successfully sent");
	}
	return 0;
}

int ast_msg_send(struct ast_msg *msg, const char *to, const char *from)
{
	char *tech_name = NULL;
	struct ast_msg_tech_holder *tech_holder = NULL;
	int res = -1;

	if (ast_strlen_zero(to)) {
		ao2_ref(msg, -1);
		return -1;
	}

	tech_name = ast_strdupa(to);
	tech_name = strsep(&tech_name, ":");

	tech_holder = msg_find_by_tech_name(tech_name, OBJ_POINTER);

	if (!tech_holder) {
		ao2_ref(msg, -1);
		return -1;
	}

	ast_rwlock_rdlock(&tech_holder->tech_lock);
	if (tech_holder->tech) {
		res = tech_holder->tech->msg_send(msg, S_OR(to, ""), S_OR(from, ""));
	}
	ast_rwlock_unlock(&tech_holder->tech_lock);

	ao2_ref(tech_holder, -1);
	ao2_ref(msg, -1);

	return res;
}

int ast_msg_tech_register(const struct ast_msg_tech *tech)
{
	struct ast_msg_tech_holder *tech_holder;

	if ((tech_holder = msg_find_by_tech(tech, OBJ_POINTER))) {
		ao2_ref(tech_holder, -1);
		ast_log(LOG_ERROR, "Message technology already registered for '%s'\n",
				tech->name);
		return -1;
	}

	if (!(tech_holder = ao2_alloc(sizeof(*tech_holder), NULL))) {
		return -1;
	}

	ast_rwlock_init(&tech_holder->tech_lock);
	tech_holder->tech = tech;

	ao2_link(msg_techs, tech_holder);

	ao2_ref(tech_holder, -1);
	tech_holder = NULL;

	ast_verb(3, "Message technology handler '%s' registered.\n", tech->name);

	return 0;
}

int ast_msg_tech_unregister(const struct ast_msg_tech *tech)
{
	struct ast_msg_tech_holder *tech_holder;

	tech_holder = msg_find_by_tech(tech, OBJ_POINTER | OBJ_UNLINK);

	if (!tech_holder) {
		ast_log(LOG_ERROR, "No '%s' message technology found.\n", tech->name);
		return -1;
	}

	ast_rwlock_wrlock(&tech_holder->tech_lock);
	tech_holder->tech = NULL;
	ast_rwlock_unlock(&tech_holder->tech_lock);

	ao2_ref(tech_holder, -1);
	tech_holder = NULL;

	ast_verb(3, "Message technology handler '%s' unregistered.\n", tech->name);

	return 0;
}

void ast_msg_shutdown(void)
{
	if (msg_q_tp) {
		msg_q_tp = ast_taskprocessor_unreference(msg_q_tp);
	}
}

/*!
 * \internal
 * \brief Clean up other resources on Asterisk shutdown
 *
 * \note This does not include the msg_q_tp object, which must be disposed
 * of prior to Asterisk checking for channel destruction in its shutdown
 * sequence.  The atexit handlers are executed after this occurs.
 */
static void message_shutdown(void)
{
	ast_custom_function_unregister(&msg_function);
	ast_custom_function_unregister(&msg_data_function);
	ast_unregister_application(app_msg_send);
	ast_manager_unregister("MessageSend");

	if (msg_techs) {
		ao2_ref(msg_techs, -1);
		msg_techs = NULL;
	}
}

/*
 * \internal
 * \brief Initialize stuff during Asterisk startup.
 *
 * Cleanup isn't a big deal in this function.  If we return non-zero,
 * Asterisk is going to exit.
 *
 * \retval 0 success
 * \retval non-zero failure
 */
int ast_msg_init(void)
{
	int res;

	msg_q_tp = ast_taskprocessor_get("ast_msg_queue", TPS_REF_DEFAULT);
	if (!msg_q_tp) {
		return -1;
	}

	msg_techs = ao2_container_alloc(17, msg_tech_hash, msg_tech_cmp);
	if (!msg_techs) {
		return -1;
	}

	res = __ast_custom_function_register(&msg_function, NULL);
	res |= __ast_custom_function_register(&msg_data_function, NULL);
	res |= ast_register_application2(app_msg_send, msg_send_exec, NULL, NULL, NULL);
	res |= ast_manager_register_xml_core("MessageSend", EVENT_FLAG_MESSAGE, action_messagesend);

	ast_register_cleanup(message_shutdown);

	return res;
}
