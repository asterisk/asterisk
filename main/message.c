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

#include "asterisk/_private.h"

#include "asterisk/module.h"
#include "asterisk/datastore.h"
#include "asterisk/pbx.h"
#include "asterisk/manager.h"
#include "asterisk/strings.h"
#include "asterisk/astobj2.h"
#include "asterisk/vector.h"
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
					<para>When processing an
					incoming message, this will be set to the destination listed as
					the recipient of the message that was received by Asterisk.</para>
					<para>
					</para>
					<para>For an outgoing message, this will set the To header in the
					outgoing SIP message.  This may be overridden by the "to" parameter
					of MessageSend.
					</para>
				</enum>
				<enum name="from">
					<para>When processing an
					incoming message, this will be set to the source of the message.</para>
					<para>
					</para>
					<para>For an outgoing message, this will set the From header in the
					outgoing SIP message. This may be overridden by the "from" parameter
					of MessageSend.
					</para>
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
			<parameter name="destination" required="true">
				<para>A To URI for the message.</para>
				<xi:include xpointer="xpointer(/docs/info[@name='MessageDestinationInfo'])" />
			</parameter>
			<parameter name="from" required="false">
				<para>A From URI for the message if needed for the
				message technology being used to send this message. This can be a
				SIP(S) URI, such as <literal>Alice &lt;sip:alice@atlanta.com&gt;</literal>,
				or a string in the format <literal>alice@atlanta.com</literal>.
				This will override a <literal>from</literal>
				specified using the MESSAGE dialplan function or the <literal>from</literal>
				that may have been on an incoming message.
				</para>
				<xi:include xpointer="xpointer(/docs/info[@name='MessageFromInfo'])" />
			</parameter>
			<parameter name="to" required="false">
				<para>A To URI for the message if needed for the
				message technology being used to send this message. This can be a
				SIP(S) URI, such as <literal>Alice &lt;sip:alice@atlanta.com&gt;</literal>,
				or a string in the format <literal>alice@atlanta.com</literal>.
				This will override a <literal>to</literal>
				specified using the MESSAGE dialplan function or the <literal>to</literal>
				that may have been on an incoming message.
				</para>
				<xi:include xpointer="xpointer(/docs/info[@name='MessageToInfo'])" />
			</parameter>
		</syntax>
		<description>
			<para>Send a text message.  The body of the message that will be
			sent is what is currently set to <literal>MESSAGE(body)</literal>.
			This may he come from an incoming message.
			The technology chosen for sending the message is determined
			based on a prefix to the <literal>destination</literal> parameter.</para>
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
			<parameter name="Destination" required="false">
				<para>A To URI for the message. If Destination is provided, the To
				parameter can also be supplied and may alter the message based on
				the specified message technology.</para>
				<para>For backwards compatibility, if Destination is not provided,
				the To parameter must be provided and will be used as the message
				destination.</para>
				<xi:include xpointer="xpointer(/docs/info[@name='MessageDestinationInfo'])" />
			</parameter>
			<parameter name="To" required="false">
				<para>A To URI for the message if needed for the
				message technology being used to send this message. This can be a
				SIP(S) URI, such as <literal>Alice &lt;sip:alice@atlanta.com&gt;</literal>,
				or a string in the format <literal>alice@atlanta.com</literal>.</para>
				<para>This parameter is required if the Destination parameter is not
				provided.</para>
				<xi:include xpointer="xpointer(/docs/info[@name='MessageToInfo'])" />
			</parameter>
			<parameter name="From">
				<para>A From URI for the message if needed for the
				message technology being used to send this message.</para>
				<xi:include xpointer="xpointer(/docs/info[@name='MessageFromInfo'])" />
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
	unsigned int send; /* Whether to send out on outbound messages */
};

AST_LIST_HEAD_NOLOCK(outhead, msg_data);

/*!
 * \brief A message.
 */
struct ast_msg {
	AST_DECLARE_STRING_FIELDS(
		/*! Where the message is going */
		AST_STRING_FIELD(to);
		/*! Where we "say" the message came from */
		AST_STRING_FIELD(from);
		/*! The text to send */
		AST_STRING_FIELD(body);
		/*! The dialplan context for the message */
		AST_STRING_FIELD(context);
		/*! The dialplan extension for the message */
		AST_STRING_FIELD(exten);
		/*! An endpoint associated with this message */
		AST_STRING_FIELD(endpoint);
		/*! The technology of the endpoint associated with this message */
		AST_STRING_FIELD(tech);
	);
	/*! Technology/dialplan specific variables associated with the message */
	struct ao2_container *vars;
};

/*! \brief Lock for \c msg_techs vector */
static ast_rwlock_t msg_techs_lock;

/*! \brief Vector of message technologies */
AST_VECTOR(, const struct ast_msg_tech *) msg_techs;

/*! \brief Lock for \c msg_handlers vector */
static ast_rwlock_t msg_handlers_lock;

/*! \brief Vector of received message handlers */
AST_VECTOR(, const struct ast_msg_handler *) msg_handlers;

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
static struct ast_channel_tech msg_chan_tech_hack = {
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

	ast_string_field_free_memory(msg);
	ao2_cleanup(msg->vars);
}

struct ast_msg *ast_msg_alloc(void)
{
	struct ast_msg *msg;

	if (!(msg = ao2_alloc(sizeof(*msg), msg_destructor))) {
		return NULL;
	}

	if (ast_string_field_init(msg, 128)) {
		ao2_ref(msg, -1);
		return NULL;
	}

	msg->vars = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0,
		NULL, msg_data_cmp_fn);
	if (!msg->vars) {
		ao2_ref(msg, -1);
		return NULL;
	}
	ast_string_field_set(msg, context, "default");

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

	va_start(ap, fmt);
	ast_string_field_build_va(msg, to, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_msg_set_from(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(msg, from, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_msg_set_body(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(msg, body, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_msg_set_context(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(msg, context, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_msg_set_exten(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(msg, exten, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_msg_set_tech(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(msg, tech, fmt, ap);
	va_end(ap);

	return 0;
}

int ast_msg_set_endpoint(struct ast_msg *msg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	ast_string_field_build_va(msg, endpoint, fmt, ap);
	va_end(ap);

	return 0;
}

const char *ast_msg_get_body(const struct ast_msg *msg)
{
	return msg->body;
}

const char *ast_msg_get_from(const struct ast_msg *msg)
{
	return msg->from;
}

const char *ast_msg_get_to(const struct ast_msg *msg)
{
	return msg->to;
}

const char *ast_msg_get_tech(const struct ast_msg *msg)
{
	return msg->tech;
}

const char *ast_msg_get_endpoint(const struct ast_msg *msg)
{
	return msg->endpoint;
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

static int ast_msg_var_iterator_get_next(const struct ast_msg *msg,
	struct ast_msg_var_iterator *iter, const char **name, const char **value,
	unsigned int send)
{
	struct msg_data *data;

	if (!iter) {
		return 0;
	}

	/* Skip any that we're told to */
	while ((data = ao2_iterator_next(&iter->iter)) && (data->send != send)) {
		ao2_ref(data, -1);
	}

	if (!data) {
		return 0;
	}

	if (data->send == send) {
		*name = data->name;
		*value = data->value;
	}

	/* Leave the refcount to be cleaned up by the caller with
	 * ast_msg_var_unref_current after they finish with the pointers to the data */
	iter->current_used = data;

	return 1;
}

int ast_msg_var_iterator_next(const struct ast_msg *msg, struct ast_msg_var_iterator *iter, const char **name, const char **value)
{
	return ast_msg_var_iterator_get_next(msg, iter, name, value, 1);
}

int ast_msg_var_iterator_next_received(const struct ast_msg *msg,
	  struct ast_msg_var_iterator *iter, const char **name, const char **value)
{
	return ast_msg_var_iterator_get_next(msg, iter, name, value, 0);
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
			NULL, NULL, NULL, NULL, 0,
			"%s", "Message/ast_msg_queue");

	if (!chan) {
		return NULL;
	}

	if (ast_opt_hide_messaging_ami_events) {
		msg_chan_tech_hack.properties |= AST_CHAN_TP_INTERNAL;
	}

	ast_channel_tech_set(chan, &msg_chan_tech_hack);
	ast_channel_unlock(chan);
	ast_channel_unlink(chan);

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

	ast_explicit_goto(chan, msg->context, S_OR(msg->exten, "s"), 1);

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

	/*
	 * Flush the alert pipe in case we miscounted somewhere when
	 * messing with frames on the read queue, we had to flush the
	 * read queue above, or we had an "Exceptionally long queue
	 * length" event.
	 */
	ast_channel_internal_alert_flush(chan);

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

/*! \internal \brief Handle a message bound for the dialplan */
static int dialplan_handle_msg_cb(struct ast_msg *msg)
{
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

	return 0;
}

/*! \internal \brief Determine if a message has a destination in the dialplan */
static int dialplan_has_destination_cb(const struct ast_msg *msg)
{
	if (ast_strlen_zero(msg->context)) {
		return 0;
	}

	return ast_exists_extension(NULL, msg->context, S_OR(msg->exten, "s"), 1, NULL);
}

static struct ast_msg_handler dialplan_msg_handler = {
	.name = "dialplan",
	.handle_msg = dialplan_handle_msg_cb,
	.has_destination = dialplan_has_destination_cb,
};

/*!
 * \internal
 * \brief Message queue task processor callback
 *
 * \retval 0 success
 * \retval non-zero failure
 *
 * \note Even though this returns a value, the taskprocessor code ignores the value.
 */
static int msg_q_cb(void *data)
{
	struct ast_msg *msg = data;
	int res = 1;
	int i;

	ast_rwlock_rdlock(&msg_handlers_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&msg_handlers); i++) {
		const struct ast_msg_handler *handler = AST_VECTOR_GET(&msg_handlers, i);

		if (!handler->has_destination(msg)) {
			ast_debug(5, "Handler %s doesn't want message, moving on\n", handler->name);
			continue;
		}

		ast_debug(5, "Dispatching message to %s handler\n", handler->name);
		res &= handler->handle_msg(msg);
	}
	ast_rwlock_unlock(&msg_handlers_lock);

	if (res != 0) {
		ast_log(LOG_WARNING, "No handler processed message from %s to %s\n",
			S_OR(msg->from, "<unknown>"), S_OR(msg->to, "<unknown>"));
	}

	ao2_ref(msg, -1);

	return res;
}

int ast_msg_has_destination(const struct ast_msg *msg)
{
	int i;
	int result = 0;

	ast_rwlock_rdlock(&msg_handlers_lock);
	for (i = 0; i < AST_VECTOR_SIZE(&msg_handlers); i++) {
		const struct ast_msg_handler *handler = AST_VECTOR_GET(&msg_handlers, i);

		ast_debug(5, "Seeing if %s can handle message\n", handler->name);
		if (handler->has_destination(msg)) {
			ast_debug(5, "%s can handle message\n", handler->name);
			result = 1;
			break;
		}
	}
	ast_rwlock_unlock(&msg_handlers_lock);

	return result;
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
 * \return the channel's message datastore
 * \retval NULL on error
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
		ast_copy_string(buf, msg->to, len);
	} else if (!strcasecmp(data, "from")) {
		ast_copy_string(buf, msg->from, len);
	} else if (!strcasecmp(data, "body")) {
		ast_copy_string(buf, msg->body, len);
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

/*!
 * \internal \brief Find a \c ast_msg_tech by its technology name
 *
 * \param tech_name The name of the message technology
 *
 * \note \c msg_techs should be locked via \c msg_techs_lock prior to
 *       calling this function
 *
 * \retval NULL if no \ref ast_msg_tech has been registered
 * \return \ref ast_msg_tech if registered
 */
static const struct ast_msg_tech *msg_find_by_tech_name(const char *tech_name)
{
	const struct ast_msg_tech *current;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&msg_techs); i++) {
		current = AST_VECTOR_GET(&msg_techs, i);
		if (!strcmp(current->name, tech_name)) {
			return current;
		}
	}

	return NULL;
}

/*!
 * \internal \brief Find a \c ast_msg_handler by its technology name
 *
 * \param tech_name The name of the message technology
 *
 * \note \c msg_handlers should be locked via \c msg_handlers_lock
 *       prior to calling this function
 *
 * \retval NULL if no \ref ast_msg_handler has been registered
 * \return \ref ast_msg_handler if registered
 */
static const struct ast_msg_handler *msg_handler_find_by_tech_name(const char *tech_name)
{
	const struct ast_msg_handler *current;
	int i;

	for (i = 0; i < AST_VECTOR_SIZE(&msg_handlers); i++) {
		current = AST_VECTOR_GET(&msg_handlers, i);
		if (!strcmp(current->name, tech_name)) {
			return current;
		}
	}

	return NULL;
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
	const struct ast_msg_tech *msg_tech;
	char *parse;
	int res = -1;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(destination);
		AST_APP_ARG(from);
		AST_APP_ARG(to);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "An argument is required to MessageSend()\n");
		pbx_builtin_setvar_helper(chan, "MESSAGE_SEND_STATUS", "INVALID_URI");
		return 0;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.destination)) {
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

	tech_name = ast_strdupa(args.destination);
	tech_name = strsep(&tech_name, ":");

	ast_rwlock_rdlock(&msg_techs_lock);
	msg_tech = msg_find_by_tech_name(tech_name);

	if (!msg_tech) {
		ast_log(LOG_WARNING, "No message technology '%s' found.\n", tech_name);
		pbx_builtin_setvar_helper(chan, "MESSAGE_SEND_STATUS", "INVALID_PROTOCOL");
		goto exit_cleanup;
	}

	/*
	 * If there was a "to" in the call to MessageSend,
	 * replace the to already in the channel datastore.
	 */
	if (!ast_strlen_zero(args.to)) {
		ast_string_field_set(msg, to, args.to);
	}

	/*
	 * The message lock is held here to safely allow the technology
	 * implementation to access the message fields without worrying
	 * that they could change.
	 */
	ao2_lock(msg);
	res = msg_tech->msg_send(msg, S_OR(args.destination, ""), S_OR(args.from, ""));
	ao2_unlock(msg);

	pbx_builtin_setvar_helper(chan, "MESSAGE_SEND_STATUS", res ? "FAILURE" : "SUCCESS");

exit_cleanup:
	ast_rwlock_unlock(&msg_techs_lock);
	ao2_ref(msg, -1);

	return 0;
}

static int action_messagesend(struct mansession *s, const struct message *m)
{
	const char *destination = astman_get_header(m, "Destination");
	const char *to = astman_get_header(m, "To");
	const char *from = astman_get_header(m, "From");
	const char *body = astman_get_header(m, "Body");
	const char *base64body = astman_get_header(m, "Base64Body");
	const char *to_override = NULL;
	char base64decoded[1301] = { 0, };
	char *tech_name = NULL;
	struct ast_variable *vars = NULL;
	struct ast_variable *data = NULL;
	const struct ast_msg_tech *msg_tech;
	struct ast_msg *msg;
	int res = -1;

	if (!ast_strlen_zero(destination)) {
		if (!ast_strlen_zero(to)) {
			to_override = to;
		}
		to = destination;
	} else {
		if (ast_strlen_zero(to)) {
			astman_send_error(s, m, "No 'To' address specified.");
			return 0;
		}
	}

	if (!ast_strlen_zero(base64body)) {
		ast_base64decode((unsigned char *) base64decoded, base64body, sizeof(base64decoded) - 1);
		body = base64decoded;
	}

	tech_name = ast_strdupa(to);
	tech_name = strsep(&tech_name, ":");

	ast_rwlock_rdlock(&msg_techs_lock);
	msg_tech = msg_find_by_tech_name(tech_name);
	if (!msg_tech) {
		ast_rwlock_unlock(&msg_techs_lock);
		astman_send_error(s, m, "Message technology not found.");
		return 0;
	}

	if (!(msg = ast_msg_alloc())) {
		ast_rwlock_unlock(&msg_techs_lock);
		astman_send_error(s, m, "Internal failure\n");
		return 0;
	}

	data = astman_get_variables_order(m, ORDER_NATURAL);
	for (vars = data; vars; vars = vars->next) {
		ast_msg_set_var_outbound(msg, vars->name, vars->value);
	}

	ast_msg_set_body(msg, "%s", body);

	if (to_override) {
		ast_string_field_set(msg, to, to_override);
	}

	res = msg_tech->msg_send(msg, S_OR(to, ""), S_OR(from, ""));

	ast_rwlock_unlock(&msg_techs_lock);

	ast_variables_destroy(vars);
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
	const struct ast_msg_tech *msg_tech;
	int res = -1;

	if (ast_strlen_zero(to)) {
		ao2_ref(msg, -1);
		return -1;
	}

	tech_name = ast_strdupa(to);
	tech_name = strsep(&tech_name, ":");

	ast_rwlock_rdlock(&msg_techs_lock);
	msg_tech = msg_find_by_tech_name(tech_name);

	if (!msg_tech) {
		ast_log(LOG_ERROR, "Unknown message tech: %s\n", tech_name);
		ast_rwlock_unlock(&msg_techs_lock);
		return -1;
	}

	res = msg_tech->msg_send(msg, S_OR(to, ""), S_OR(from, ""));

	ast_rwlock_unlock(&msg_techs_lock);

	ao2_ref(msg, -1);

	return res;
}

/*!
 * \brief Structure used to transport a message through the frame core
 * \since 13.22.0
 * \since 15.5.0
 */
struct ast_msg_data {
	/*! The length of this structure plus the actual length of the allocated buffer */
	size_t length;
	enum ast_msg_data_source_type source;
	/*! These are indices into the buffer where teh attribute starts */
	int attribute_value_offsets[__AST_MSG_DATA_ATTR_LAST];
	/*! The buffer containing the NULL separated attributes */
	char buf[0];
};

#define ATTRIBUTE_UNSET -1

struct ast_msg_data *ast_msg_data_alloc(enum ast_msg_data_source_type source,
	struct ast_msg_data_attribute attributes[], size_t count)
{
	struct ast_msg_data *msg;
	size_t len = sizeof(*msg);
	size_t i;
	size_t current_offset = 0;
	enum ast_msg_data_attribute_type attr_type;

	if (!attributes) {
		ast_assert(attributes != NULL);
		return NULL;
	}

	if (!count) {
		ast_assert(count > 0);
		return NULL;
	}

	/* Calculate the length required for the buffer */
	for (i=0; i < count; i++) {
		if (!attributes[i].value) {
			ast_assert(attributes[i].value != NULL);
			return NULL;
		}
		len += (strlen(attributes[i].value) + 1);
	}

	msg = ast_calloc(1, len);
	if (!msg) {
		return NULL;
	}
	msg->source = source;
	msg->length = len;

	/* Mark all of the attributes as unset */
	for (attr_type = 0; attr_type < __AST_MSG_DATA_ATTR_LAST; attr_type++) {
		msg->attribute_value_offsets[attr_type] = ATTRIBUTE_UNSET;
	}

	/* Set the ones we have and increment the offset */
	for (i=0; i < count; i++) {
		len = (strlen(attributes[i].value) + 1);
		ast_copy_string(msg->buf + current_offset, attributes[i].value, len); /* Safe */
		msg->attribute_value_offsets[attributes[i].type] = current_offset;
		current_offset += len;
	}

	return msg;
}

struct ast_msg_data *ast_msg_data_alloc2(enum ast_msg_data_source_type source_type,
	const char *to, const char *from, const char *content_type, const char *body)
{
	struct ast_msg_data_attribute attrs[] =
	{
		{
			.type = AST_MSG_DATA_ATTR_TO,
			.value = (char *)S_OR(to, ""),
		},
		{
			.type = AST_MSG_DATA_ATTR_FROM,
			.value = (char *)S_OR(from, ""),
		},
		{
			.type = AST_MSG_DATA_ATTR_CONTENT_TYPE,
			.value = (char *)S_OR(content_type, ""),
		},
		{
			.type = AST_MSG_DATA_ATTR_BODY,
			.value = (char *)S_OR(body, ""),
		},
	};

	return ast_msg_data_alloc(source_type, attrs, ARRAY_LEN(attrs));
}

struct ast_msg_data *ast_msg_data_dup(struct ast_msg_data *msg)
{
	struct ast_msg_data *dest;

	if (!msg) {
		ast_assert(msg != NULL);
		return NULL;
	}

	dest = ast_malloc(msg->length);
	if (!dest) {
		return NULL;
	}
	memcpy(dest, msg, msg->length);

	return dest;
}

size_t ast_msg_data_get_length(struct ast_msg_data *msg)
{
	if (!msg) {
		ast_assert(msg != NULL);
		return 0;
	}

	return msg->length;
}

enum ast_msg_data_source_type ast_msg_data_get_source_type(struct ast_msg_data *msg)
{
	if (!msg) {
		ast_assert(msg != NULL);
		return AST_MSG_DATA_SOURCE_TYPE_UNKNOWN;
	}

	return msg->source;
}

const char *ast_msg_data_get_attribute(struct ast_msg_data *msg,
	enum ast_msg_data_attribute_type attribute_type)
{
	if (!msg) {
		ast_assert(msg != NULL);
		return "";
	}

	if (msg->attribute_value_offsets[attribute_type] > ATTRIBUTE_UNSET) {
		return msg->buf + msg->attribute_value_offsets[attribute_type];
	}

	return "";
}

int ast_msg_data_queue_frame(struct ast_channel *channel, struct ast_msg_data *msg)
{
	struct ast_frame f;

	if (!channel) {
		ast_assert(channel != NULL);
		return -1;
	}

	if (!msg) {
		ast_assert(msg != NULL);
		return -1;
	}

	memset(&f, 0, sizeof(f));
	f.frametype = AST_FRAME_TEXT_DATA;
	f.data.ptr = msg;
	f.datalen = msg->length;
	return ast_queue_frame(channel, &f);
}

int ast_msg_tech_register(const struct ast_msg_tech *tech)
{
	const struct ast_msg_tech *match;

	ast_rwlock_wrlock(&msg_techs_lock);

	match = msg_find_by_tech_name(tech->name);
	if (match) {
		ast_log(LOG_ERROR, "Message technology already registered for '%s'\n",
		        tech->name);
		ast_rwlock_unlock(&msg_techs_lock);
		return -1;
	}

	if (AST_VECTOR_APPEND(&msg_techs, tech)) {
		ast_log(LOG_ERROR, "Failed to register message technology for '%s'\n",
		        tech->name);
		ast_rwlock_unlock(&msg_techs_lock);
		return -1;
	}
	ast_verb(3, "Message technology '%s' registered.\n", tech->name);

	ast_rwlock_unlock(&msg_techs_lock);

	return 0;
}

/*!
 * \brief Comparison callback for \c ast_msg_tech vector removal
 *
 * \param vec_elem The element in the vector being compared
 * \param srch The element being looked up
 *
 * \retval non-zero The items are equal
 * \retval 0 The items are not equal
 */
static int msg_tech_cmp(const struct ast_msg_tech *vec_elem, const struct ast_msg_tech *srch)
{
	return !strcmp(vec_elem->name, srch->name);
}

int ast_msg_tech_unregister(const struct ast_msg_tech *tech)
{
	int match;

	ast_rwlock_wrlock(&msg_techs_lock);
	match = AST_VECTOR_REMOVE_CMP_UNORDERED(&msg_techs, tech, msg_tech_cmp,
	                                        AST_VECTOR_ELEM_CLEANUP_NOOP);
	ast_rwlock_unlock(&msg_techs_lock);

	if (match) {
		ast_log(LOG_ERROR, "No '%s' message technology found.\n", tech->name);
		return -1;
	}

	ast_verb(2, "Message technology '%s' unregistered.\n", tech->name);

	return 0;
}

int ast_msg_handler_register(const struct ast_msg_handler *handler)
{
	const struct ast_msg_handler *match;

	ast_rwlock_wrlock(&msg_handlers_lock);

	match = msg_handler_find_by_tech_name(handler->name);
	if (match) {
		ast_log(LOG_ERROR, "Message handler already registered for '%s'\n",
		        handler->name);
		ast_rwlock_unlock(&msg_handlers_lock);
		return -1;
	}

	if (AST_VECTOR_APPEND(&msg_handlers, handler)) {
		ast_log(LOG_ERROR, "Failed to register message handler for '%s'\n",
		        handler->name);
		ast_rwlock_unlock(&msg_handlers_lock);
		return -1;
	}
	ast_verb(2, "Message handler '%s' registered.\n", handler->name);

	ast_rwlock_unlock(&msg_handlers_lock);

	return 0;

}

/*!
 * \brief Comparison callback for \c ast_msg_handler vector removal
 *
 * \param vec_elem The element in the vector being compared
 * \param srch The element being looked up
 *
 * \retval non-zero The items are equal
 * \retval 0 The items are not equal
 */
static int msg_handler_cmp(const struct ast_msg_handler *vec_elem, const struct ast_msg_handler *srch)
{
	return !strcmp(vec_elem->name, srch->name);
}

int ast_msg_handler_unregister(const struct ast_msg_handler *handler)
{
	int match;

	ast_rwlock_wrlock(&msg_handlers_lock);
	match = AST_VECTOR_REMOVE_CMP_UNORDERED(&msg_handlers, handler, msg_handler_cmp,
	                                        AST_VECTOR_ELEM_CLEANUP_NOOP);
	ast_rwlock_unlock(&msg_handlers_lock);

	if (match) {
		ast_log(LOG_ERROR, "No '%s' message handler found.\n", handler->name);
		return -1;
	}

	ast_verb(3, "Message handler '%s' unregistered.\n", handler->name);
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
	ast_msg_handler_unregister(&dialplan_msg_handler);

	ast_custom_function_unregister(&msg_function);
	ast_custom_function_unregister(&msg_data_function);
	ast_unregister_application(app_msg_send);
	ast_manager_unregister("MessageSend");

	AST_VECTOR_FREE(&msg_techs);
	ast_rwlock_destroy(&msg_techs_lock);

	AST_VECTOR_FREE(&msg_handlers);
	ast_rwlock_destroy(&msg_handlers_lock);
}

/*!
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

	ast_rwlock_init(&msg_techs_lock);
	if (AST_VECTOR_INIT(&msg_techs, 8)) {
		return -1;
	}

	ast_rwlock_init(&msg_handlers_lock);
	if (AST_VECTOR_INIT(&msg_handlers, 4)) {
		return -1;
	}

	res = ast_msg_handler_register(&dialplan_msg_handler);

	res |= __ast_custom_function_register(&msg_function, NULL);
	res |= __ast_custom_function_register(&msg_data_function, NULL);
	res |= ast_register_application2(app_msg_send, msg_send_exec, NULL, NULL, NULL);
	res |= ast_manager_register_xml_core("MessageSend", EVENT_FLAG_MESSAGE, action_messagesend);

	ast_register_cleanup(message_shutdown);

	return res;
}
