/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * David M. Lee, II <dlee@digium.com>
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
 * \brief The Asterisk Management Interface - AMI (channel event handling)
 *
 * \author David M. Lee, II <dlee@digium.com>
 *
 * AMI generated many per-channel and global-channel events by converting Stasis
 * messages to AMI events. It makes sense to simply put them into a single file.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/callerid.h"
#include "asterisk/channel.h"
#include "asterisk/manager.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis_channels.h"

static struct stasis_message_router *channel_state_router;

/*** DOCUMENTATION
	<managerEvent language="en_US" name="Newchannel">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a new channel is created.</synopsis>
			<syntax>
				<parameter name="Channel">
				</parameter>
				<parameter name="ChannelState">
					<para>A numeric code for the channel's current state, related to ChannelStateDesc</para>
				</parameter>
				<parameter name="ChannelStateDesc">
					<enumlist>
						<enum name="Down"/>
						<enum name="Rsrvd"/>
						<enum name="OffHook"/>
						<enum name="Dialing"/>
						<enum name="Ring"/>
						<enum name="Ringing"/>
						<enum name="Up"/>
						<enum name="Busy"/>
						<enum name="Dialing Offhook"/>
						<enum name="Pre-ring"/>
						<enum name="Unknown"/>
					</enumlist>
				</parameter>
				<parameter name="CallerIDNum">
				</parameter>
				<parameter name="CallerIDName">
				</parameter>
				<parameter name="ConnectedLineNum">
				</parameter>
				<parameter name="ConnectedLineName">
				</parameter>
				<parameter name="AccountCode">
				</parameter>
				<parameter name="Context">
				</parameter>
				<parameter name="Exten">
				</parameter>
				<parameter name="Priority">
				</parameter>
				<parameter name="Uniqueid">
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="Newstate">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel's state changes.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="Hangup">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel is hung up.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<parameter name="Cause">
					<para>A numeric cause code for why the channel was hung up.</para>
				</parameter>
				<parameter name="Cause-txt">
					<para>A description of why the channel was hung up.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="HangupRequest">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a hangup is requested.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Hangup']/managerEventInstance/syntax/parameter[@name='Cause'])" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="SoftHangupRequest">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a soft hangup is requested with a specific cause code.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Hangup']/managerEventInstance/syntax/parameter[@name='Cause'])" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="NewExten">
		<managerEventInstance class="EVENT_FLAG_DIALPLAN">
			<synopsis>Raised when a channel enters a new context, extension, priority.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<parameter name="Extension">
					<para>Deprecated in 12, but kept for
					backward compatability. Please use
					'Exten' instead.</para>
				</parameter>
				<parameter name="Application">
					<para>The application about to be executed.</para>
				</parameter>
				<parameter name="AppData">
					<para>The data to be passed to the application.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="NewCallerid">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel receives new Caller ID information.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<parameter name="CID-CallingPres">
					<para>A description of the Caller ID presentation.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="DialBegin">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a dial action has started.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<parameter name="ChannelDest">
				</parameter>
				<parameter name="ChannelStateDest">
					<para>A numeric code for the channel's current state, related to ChannelStateDescDest</para>
				</parameter>
				<parameter name="ChannelStateDescDest">
					<enumlist>
						<enum name="Down"/>
						<enum name="Rsrvd"/>
						<enum name="OffHook"/>
						<enum name="Dialing"/>
						<enum name="Ring"/>
						<enum name="Ringing"/>
						<enum name="Up"/>
						<enum name="Busy"/>
						<enum name="Dialing Offhook"/>
						<enum name="Pre-ring"/>
						<enum name="Unknown"/>
					</enumlist>
				</parameter>
				<parameter name="CallerIDNumDest">
				</parameter>
				<parameter name="CallerIDNameDest">
				</parameter>
				<parameter name="ConnectedLineNumDest">
				</parameter>
				<parameter name="ConnectedLineNameDest">
				</parameter>
				<parameter name="AccountCodeDest">
				</parameter>
				<parameter name="ContextDest">
				</parameter>
				<parameter name="ExtenDest">
				</parameter>
				<parameter name="PriorityDest">
				</parameter>
				<parameter name="UniqueidDest">
				</parameter>
				<parameter name="DialString">
					<para>The non-technology specific device being dialed.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">Dial</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="DialEnd">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a dial action has completed.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='DialBegin']/managerEventInstance/syntax/parameter[contains(@name, 'Dest')])" />
				<parameter name="DialStatus">
					<para>The result of the dial operation.</para>
					<enumlist>
						<enum name="ANSWER" />
						<enum name="BUSY" />
						<enum name="CANCEL" />
						<enum name="CHANUNAVAIL" />
						<enum name="CONGESTION" />
						<enum name="NOANSWER" />
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">Dial</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
 ***/

struct ast_str *ast_manager_build_channel_state_string_suffix(
		const struct ast_channel_snapshot *snapshot,
		const char *suffix)
{
	struct ast_str *out = ast_str_create(1024);
	int res = 0;
	if (!out) {
		return NULL;
	}
	res = ast_str_set(&out, 0,
		"Channel%s: %s\r\n"
		"ChannelState%s: %d\r\n"
		"ChannelStateDesc%s: %s\r\n"
		"CallerIDNum%s: %s\r\n"
		"CallerIDName%s: %s\r\n"
		"ConnectedLineNum%s: %s\r\n"
		"ConnectedLineName%s: %s\r\n"
		"AccountCode%s: %s\r\n"
		"Context%s: %s\r\n"
		"Exten%s: %s\r\n"
		"Priority%s: %d\r\n"
		"Uniqueid%s: %s\r\n",
		suffix, snapshot->name,
		suffix, snapshot->state,
		suffix, ast_state2str(snapshot->state),
		suffix, S_OR(snapshot->caller_number, "<unknown>"),
		suffix, S_OR(snapshot->caller_name, "<unknown>"),
		suffix, S_OR(snapshot->connected_number, "<unknown>"),
		suffix, S_OR(snapshot->connected_name, "<unknown>"),
		suffix, snapshot->accountcode,
		suffix, snapshot->context,
		suffix, snapshot->exten,
		suffix, snapshot->priority,
		suffix, snapshot->uniqueid);

	if (!res) {
		return NULL;
	}

	if (snapshot->manager_vars) {
		struct ast_var_t *var;
		AST_LIST_TRAVERSE(snapshot->manager_vars, var, entries) {
			ast_str_append(&out, 0, "ChanVariable%s: %s=%s\r\n",
				       suffix,
				       var->name, var->value);
		}
	}

	return out;
}

struct ast_str *ast_manager_build_channel_state_string(
	const struct ast_channel_snapshot *snapshot)
{
	return ast_manager_build_channel_state_string_suffix(snapshot, "");
}

/*! \brief Struct containing info for an AMI channel event to send out. */
struct snapshot_manager_event {
	/*! event_flags manager_event() flags parameter. */
	int event_flags;
	/*!  manager_event manager_event() category. */
	const char *manager_event;
	AST_DECLARE_STRING_FIELDS(
		/* extra fields to include in the event. */
		AST_STRING_FIELD(extra_fields);
		);
};

static void snapshot_manager_event_dtor(void *obj)
{
	struct snapshot_manager_event *ev = obj;
	ast_string_field_free_memory(ev);
}

/*!
 * \brief Construct a \ref snapshot_manager_event.
 * \param event_flags manager_event() flags parameter.
 * \param manager_event manager_event() category.
 * \param extra_fields_fmt Format string for extra fields to include.
 *                         Or NO_EXTRA_FIELDS for no extra fields.
 * \return New \ref snapshot_manager_event object.
 * \return \c NULL on error.
 */
static struct snapshot_manager_event *
__attribute__((format(printf, 3, 4)))
snapshot_manager_event_create(
	int event_flags,
	const char *manager_event,
	const char *extra_fields_fmt,
	...)
{
	RAII_VAR(struct snapshot_manager_event *, ev, NULL, ao2_cleanup);
	va_list argp;

	ast_assert(extra_fields_fmt != NULL);
	ast_assert(manager_event != NULL);

	ev = ao2_alloc(sizeof(*ev), snapshot_manager_event_dtor);
	if (!ev) {
		return NULL;
	}

	if (ast_string_field_init(ev, 20)) {
		return NULL;
	}

	ev->manager_event = manager_event;
	ev->event_flags = event_flags;

	va_start(argp, extra_fields_fmt);
	ast_string_field_ptr_build_va(ev, &ev->extra_fields, extra_fields_fmt,
				      argp);
	va_end(argp);

	ao2_ref(ev, +1);
	return ev;
}

/*! GCC warns about blank or NULL format strings. So, shenanigans! */
#define NO_EXTRA_FIELDS "%s", ""

/*! \brief Typedef for callbacks that get called on channel snapshot updates */
typedef struct snapshot_manager_event *(*snapshot_monitor)(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot);

/*! \brief Handle channel state changes */
static struct snapshot_manager_event *channel_state_change(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	int is_hungup, was_hungup;

	if (!new_snapshot) {
		/* Ignore cache clearing events; we'll see the hangup first */
		return NULL;
	}

	/* The Newchannel, Newstate and Hangup events are closely related, in
	 * in that they are mutually exclusive, basically different flavors
	 * of a new channel state event.
	 */

	if (!old_snapshot) {
		return snapshot_manager_event_create(
			EVENT_FLAG_CALL, "Newchannel", NO_EXTRA_FIELDS);
	}

	was_hungup = ast_test_flag(&old_snapshot->flags, AST_FLAG_ZOMBIE) ? 1 : 0;
	is_hungup = ast_test_flag(&new_snapshot->flags, AST_FLAG_ZOMBIE) ? 1 : 0;

	if (!was_hungup && is_hungup) {
		return snapshot_manager_event_create(
			EVENT_FLAG_CALL, "Hangup",
			"Cause: %d\r\n"
			"Cause-txt: %s\r\n",
			new_snapshot->hangupcause,
			ast_cause2str(new_snapshot->hangupcause));
	}

	if (old_snapshot->state != new_snapshot->state) {
		return snapshot_manager_event_create(
			EVENT_FLAG_CALL, "Newstate", NO_EXTRA_FIELDS);
	}

	/* No event */
	return NULL;
}

static struct snapshot_manager_event *channel_newexten(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	/* No Newexten event on cache clear */
	if (!new_snapshot) {
		return NULL;
	}

	/* Empty application is not valid for a Newexten event */
	if (ast_strlen_zero(new_snapshot->appl)) {
		return NULL;
	}

	if (old_snapshot && ast_channel_snapshot_cep_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	/* DEPRECATED: Extension field deprecated in 12; remove in 14 */
	return snapshot_manager_event_create(
		EVENT_FLAG_CALL, "Newexten",
		"Extension: %s\r\n"
		"Application: %s\r\n"
		"AppData: %s\r\n",
		new_snapshot->exten,
		new_snapshot->appl,
		new_snapshot->data);
}

static struct snapshot_manager_event *channel_new_callerid(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	/* No NewCallerid event on cache clear or first event */
	if (!old_snapshot || !new_snapshot) {
		return NULL;
	}

	if (ast_channel_snapshot_caller_id_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	return snapshot_manager_event_create(
		EVENT_FLAG_CALL, "NewCallerid",
		"CID-CallingPres: %d (%s)\r\n",
		new_snapshot->caller_pres,
		ast_describe_caller_presentation(new_snapshot->caller_pres));
}

snapshot_monitor monitors[] = {
	channel_state_change,
	channel_newexten,
	channel_new_callerid
};

static void channel_snapshot_update(void *data, struct stasis_subscription *sub,
				    struct stasis_topic *topic,
				    struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	struct stasis_cache_update *update;
	struct ast_channel_snapshot *old_snapshot;
	struct ast_channel_snapshot *new_snapshot;
	size_t i;

	update = stasis_message_data(message);

	if (ast_channel_snapshot_type() != update->type) {
		return;
	}

	old_snapshot = stasis_message_data(update->old_snapshot);
	new_snapshot = stasis_message_data(update->new_snapshot);

	for (i = 0; i < ARRAY_LEN(monitors); ++i) {
		RAII_VAR(struct snapshot_manager_event *, ev, NULL, ao2_cleanup);
		ev = monitors[i](old_snapshot, new_snapshot);

		if (!ev) {
			continue;
		}

		/* If we haven't already, build the channel event string */
		if (!channel_event_string) {
			channel_event_string =
				ast_manager_build_channel_state_string(new_snapshot);
			if (!channel_event_string) {
				return;
			}
		}

		manager_event(ev->event_flags, ev->manager_event, "%s%s",
			ast_str_buffer(channel_event_string),
			ev->extra_fields);
	}
}

static void channel_varset_cb(void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic, struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	const char *variable = ast_json_string_get(ast_json_object_get(obj->blob, "variable"));
	const char *value = ast_json_string_get(ast_json_object_get(obj->blob, "value"));

	if (obj->snapshot) {
		channel_event_string = ast_manager_build_channel_state_string(obj->snapshot);
	} else {
		channel_event_string = ast_str_create(35);
		ast_str_set(&channel_event_string, 0,
			    "Channel: none\r\n"
			    "Uniqueid: none\r\n");
	}

	if (!channel_event_string) {
		return;
	}

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a variable is set to a particular value.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<parameter name="Variable">
					<para>The variable being set.</para>
				</parameter>
				<parameter name="Value">
					<para>The new value of the variable.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_DIALPLAN, "VarSet",
		      "%s"
		      "Variable: %s\r\n"
		      "Value: %s\r\n",
		      ast_str_buffer(channel_event_string),
		      variable, value);
}

/*!
 * \brief Callback used to determine whether a key should be skipped when converting a JSON object to a manager blob
 * \param key Key from JSON blob to be evaluated
 * \retval non-zero if the key should be excluded
 * \retval zero if the key should not be excluded
 */
typedef int (*key_exclusion_cb)(const char *key);

static struct ast_str *manager_str_from_json_object(struct ast_json *blob, key_exclusion_cb exclusion_cb)
{
	struct ast_str *output_str = ast_str_create(32);
	struct ast_json_iter *blob_iter = ast_json_object_iter(blob);
	if (!output_str || !blob_iter) {
		return NULL;
	}

	do {
		const char *key = ast_json_object_iter_key(blob_iter);
		const char *value = ast_json_string_get(ast_json_object_iter_value(blob_iter));
		if (exclusion_cb && exclusion_cb(key)) {
			continue;
		}

		ast_str_append(&output_str, 0, "%s: %s\r\n", key, value);
		if (!output_str) {
			return NULL;
		}
	} while ((blob_iter = ast_json_object_iter_next(blob, blob_iter)));

	return output_str;
}

static int userevent_exclusion_cb(const char *key)
{
	if (!strcmp("type", key)) {
		return 1;
	}
	if (!strcmp("eventname", key)) {
		return 1;
	}
	return 0;
}

static void channel_user_event_cb(void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic, struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, body, NULL, ast_free);
	const char *eventname;

	eventname = ast_json_string_get(ast_json_object_get(obj->blob, "eventname"));
	body = manager_str_from_json_object(obj->blob, userevent_exclusion_cb);
	channel_event_string = ast_manager_build_channel_state_string(obj->snapshot);

	if (!channel_event_string || !body) {
		return;
	}

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>A user defined event raised from the dialplan.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<parameter name="UserEvent">
					<para>The event name, as specified in the dialplan.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">UserEvent</ref>
			</see-also>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_USER, "UserEvent",
		      "%s"
		      "UserEvent: %s\r\n"
		      "%s",
		      ast_str_buffer(channel_event_string), eventname, ast_str_buffer(body));
}

static void channel_hangup_request_cb(void *data,
	struct stasis_subscription *sub, struct stasis_topic *topic,
	struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	RAII_VAR(struct ast_str *, extra, NULL, ast_free);
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	struct ast_json *cause;
	int is_soft;
	char *manager_event = "HangupRequest";

	extra = ast_str_create(20);
	if (!extra) {
		return;
	}

	channel_event_string = ast_manager_build_channel_state_string(obj->snapshot);

	if (!channel_event_string) {
		return;
	}

	cause = ast_json_object_get(obj->blob, "cause");
	if (cause) {
		ast_str_append(&extra, 0,
			       "Cause: %jd\r\n",
			       ast_json_integer_get(cause));
	}

	is_soft = ast_json_is_true(ast_json_object_get(obj->blob, "soft"));
	if (is_soft) {
		manager_event = "SoftHangupRequest";
	}

	manager_event(EVENT_FLAG_CALL, manager_event,
		      "%s%s",
		      ast_str_buffer(channel_event_string),
		      ast_str_buffer(extra));
}

static void channel_dtmf_begin_cb(void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic, struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	const char *digit =
		ast_json_string_get(ast_json_object_get(obj->blob, "digit"));
	const char *direction =
		ast_json_string_get(ast_json_object_get(obj->blob, "direction"));

	channel_event_string = ast_manager_build_channel_state_string(obj->snapshot);

	if (!channel_event_string) {
		return;
	}

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a DTMF digit has started on a channel.</synopsis>
				<syntax>
					<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
					<parameter name="Digit">
						<para>DTMF digit received or transmitted (0-9, A-E, # or *</para>
					</parameter>
					<parameter name="Direction">
						<enumlist>
							<enum name="Received"/>
							<enum name="Sent"/>
						</enumlist>
					</parameter>
				</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_DTMF, "DTMFBegin",
		"%s"
		"Digit: %s\r\n"
		"Direction: %s\r\n",
		ast_str_buffer(channel_event_string),
		digit, direction);
}

static void channel_dtmf_end_cb(void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic, struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	const char *digit =
		ast_json_string_get(ast_json_object_get(obj->blob, "digit"));
	const char *direction =
		ast_json_string_get(ast_json_object_get(obj->blob, "direction"));
	long duration_ms =
		ast_json_integer_get(ast_json_object_get(obj->blob, "duration_ms"));

	channel_event_string = ast_manager_build_channel_state_string(obj->snapshot);

	if (!channel_event_string) {
		return;
	}

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when a DTMF digit has ended on a channel.</synopsis>
				<syntax>
					<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
					<parameter name="Digit">
						<para>DTMF digit received or transmitted (0-9, A-E, # or *</para>
					</parameter>
					<parameter name="DurationMs">
						<para>Duration (in milliseconds) DTMF was sent/received</para>
					</parameter>
					<parameter name="Direction">
						<enumlist>
							<enum name="Received"/>
							<enum name="Sent"/>
						</enumlist>
					</parameter>
				</syntax>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_DTMF, "DTMFEnd",
		"%s"
		"Digit: %s\r\n"
		"DurationMs: %ld\r\n"
		"Direction: %s\r\n",
		ast_str_buffer(channel_event_string),
		digit, duration_ms, direction);
}

/*!
 * \brief Callback processing messages for channel dialing
 */
static void channel_dial_cb(void *data, struct stasis_subscription *sub,
	struct stasis_topic *topic, struct stasis_message *message)
{
	struct ast_multi_channel_blob *obj = stasis_message_data(message);
	const char *dialstatus;
	const char *dialstring;
	struct ast_channel_snapshot *caller;
	struct ast_channel_snapshot *peer;
	RAII_VAR(struct ast_str *, caller_event_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, peer_event_string, NULL, ast_free);

	caller = ast_multi_channel_blob_get_channel(obj, "caller");
	peer = ast_multi_channel_blob_get_channel(obj, "peer");

	/* Peer is required - otherwise, who are we dialing? */
	ast_assert(peer != NULL);
	peer_event_string = ast_manager_build_channel_state_string_suffix(peer, "Dest");
	if (!peer_event_string) {
		return;
	}

	if (caller) {
		caller_event_string = ast_manager_build_channel_state_string(caller);
		if (!caller_event_string) {
			return;
		}
		dialstatus = ast_json_string_get(ast_json_object_get(ast_multi_channel_blob_get_json(obj), "dialstatus"));
		dialstring = ast_json_string_get(ast_json_object_get(ast_multi_channel_blob_get_json(obj), "dialstring"));
		if (ast_strlen_zero(dialstatus)) {
			manager_event(EVENT_FLAG_CALL, "DialBegin",
					"%s"
					"%s"
					"DialString: %s\r\n",
					ast_str_buffer(caller_event_string),
					ast_str_buffer(peer_event_string),
					S_OR(dialstring, "unknown"));
		} else {
			manager_event(EVENT_FLAG_CALL, "DialEnd",
					"%s"
					"%s"
					"DialStatus: %s\r\n",
					ast_str_buffer(caller_event_string),
					ast_str_buffer(peer_event_string),
					S_OR(dialstatus, "unknown"));
		}
	} else {
		/* TODO: If we don't have a caller, this should be treated as an Originate */
	}
}

static void manager_channels_shutdown(void)
{
	stasis_message_router_unsubscribe_and_join(channel_state_router);
	channel_state_router = NULL;
}

int manager_channels_init(void)
{
	int ret = 0;

	if (channel_state_router) {
		/* Already initialized */
		return 0;
	}

	ast_register_atexit(manager_channels_shutdown);

	channel_state_router = stasis_message_router_create(
		stasis_caching_get_topic(ast_channel_topic_all_cached()));

	if (!channel_state_router) {
		return -1;
	}

	ret |= stasis_message_router_add(channel_state_router,
					 stasis_cache_update_type(),
					 channel_snapshot_update,
					 NULL);

	ret |= stasis_message_router_add(channel_state_router,
					 ast_channel_varset_type(),
					 channel_varset_cb,
					 NULL);

	ret |= stasis_message_router_add(channel_state_router,
					 ast_channel_user_event_type(),
					 channel_user_event_cb,
					 NULL);

	ret |= stasis_message_router_add(channel_state_router,
					 ast_channel_dtmf_begin_type(),
					 channel_dtmf_begin_cb,
					 NULL);

	ret |= stasis_message_router_add(channel_state_router,
					 ast_channel_dtmf_end_type(),
					 channel_dtmf_end_cb,
					 NULL);

	ret |= stasis_message_router_add(channel_state_router,
					 ast_channel_hangup_request_type(),
					 channel_hangup_request_cb,
					 NULL);

	ret |= stasis_message_router_add(channel_state_router,
					 ast_channel_dial_type(),
					 channel_dial_cb,
					 NULL);

	/* If somehow we failed to add any routes, just shut down the whole
	 * thing and fail it.
	 */
	if (ret) {
		manager_channels_shutdown();
		return -1;
	}

	return 0;
}

