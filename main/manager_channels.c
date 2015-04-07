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

/*** DOCUMENTATION
	<managerEvent language="en_US" name="Newchannel">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a new channel is created.</synopsis>
			<syntax>
				<channel_snapshot/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="Newstate">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel's state changes.</synopsis>
			<syntax>
				<channel_snapshot/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="Hangup">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel is hung up.</synopsis>
			<syntax>
				<channel_snapshot/>
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
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Hangup']/managerEventInstance/syntax/parameter[@name='Cause'])" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="SoftHangupRequest">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a soft hangup is requested with a specific cause code.</synopsis>
			<syntax>
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Hangup']/managerEventInstance/syntax/parameter[@name='Cause'])" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="NewExten">
		<managerEventInstance class="EVENT_FLAG_DIALPLAN">
			<synopsis>Raised when a channel enters a new context, extension, priority.</synopsis>
			<syntax>
				<channel_snapshot/>
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
				<channel_snapshot/>
				<parameter name="CID-CallingPres">
					<para>A description of the Caller ID presentation.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="NewAccountCode">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a Channel's AccountCode is changed.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="OldAccountCode">
					<para>The channel's previous account code</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="DialBegin">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a dial action has started.</synopsis>
			<syntax>
				<channel_snapshot/>
				<channel_snapshot prefix="Dest"/>
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
				<channel_snapshot/>
				<channel_snapshot prefix="Dest"/>
				<parameter name="DialStatus">
					<para>The result of the dial operation.</para>
					<enumlist>
						<enum name="ABORT">
							<para>The call was aborted.</para>
						</enum>
						<enum name="ANSWER">
							<para>The caller answered.</para>
						</enum>
						<enum name="BUSY">
							<para>The caller was busy.</para>
						</enum>
						<enum name="CANCEL">
							<para>The caller cancelled the call.</para>
						</enum>
						<enum name="CHANUNAVAIL">
							<para>The requested channel is unavailable.</para>
						</enum>
						<enum name="CONGESTION">
							<para>The called party is congested.</para>
						</enum>
						<enum name="CONTINUE">
							<para>The dial completed, but the caller elected
							to continue in the dialplan.</para>
						</enum>
						<enum name="GOTO">
							<para>The dial completed, but the caller jumped to
							a dialplan location.</para>
							<para>If known, the location the caller is jumping
							to will be appended to the result following a
							":".</para>
						</enum>
						<enum name="NOANSWER">
							<para>The called party failed to answer.</para>
						</enum>
					</enumlist>
				</parameter>
				<parameter name="Forward" required="false">
					<para>If the call was forwarded, where the call was
					forwarded to.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">Dial</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="Hold">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel goes on hold.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="MusicClass">
					<para>The suggested MusicClass, if provided.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="Unhold">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel goes off hold.</synopsis>
			<syntax>
				<channel_snapshot/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ChanSpyStart">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when one channel begins spying on another channel.</synopsis>
			<syntax>
				<channel_snapshot prefix="Spyer"/>
				<channel_snapshot prefix="Spyee"/>
			</syntax>
			<see-also>
				<ref type="application">ChanSpyStop</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ChanSpyStop">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel has stopped spying.</synopsis>
			<syntax>
				<channel_snapshot prefix="Spyer"/>
				<channel_snapshot prefix="Spyee"/>
			</syntax>
			<see-also>
				<ref type="application">ChanSpyStart</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="HangupHandlerRun">
		<managerEventInstance class="EVENT_FLAG_DIALPLAN">
			<synopsis>Raised when a hangup handler is about to be called.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Handler">
					<para>Hangup handler parameter string passed to the Gosub application.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="HangupHandlerPop">
		<managerEventInstance class="EVENT_FLAG_DIALPLAN">
			<synopsis>
				Raised when a hangup handler is removed from the handler stack
				by the CHANNEL() function.
			</synopsis>
			<syntax>
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='HangupHandlerRun']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">HangupHandlerPush</ref>
				<ref type="function">CHANNEL</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="HangupHandlerPush">
		<managerEventInstance class="EVENT_FLAG_DIALPLAN">
			<synopsis>
				Raised when a hangup handler is added to the handler stack by
				the CHANNEL() function.
			</synopsis>
			<syntax>
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='HangupHandlerRun']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">HangupHandlerPop</ref>
				<ref type="function">CHANNEL</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="FAXStatus">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>
				Raised periodically during a fax transmission.
			</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Operation">
					<enumlist>
						<enum name="gateway"/>
						<enum name="receive"/>
						<enum name="send"/>
					</enumlist>
				</parameter>
				<parameter name="Status">
					<para>A text message describing the current status of the fax</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ReceiveFAX']/managerEventInstance/syntax/parameter[@name='LocalStationID'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ReceiveFAX']/managerEventInstance/syntax/parameter[@name='FileName'])" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ReceiveFAX">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>
				Raised when a receive fax operation has completed.
			</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="LocalStationID">
					<para>The value of the <variable>LOCALSTATIONID</variable> channel variable</para>
				</parameter>
				<parameter name="RemoteStationID">
					<para>The value of the <variable>REMOTESTATIONID</variable> channel variable</para>
				</parameter>
				<parameter name="PagesTransferred">
					<para>The number of pages that have been transferred</para>
				</parameter>
				<parameter name="Resolution">
					<para>The negotiated resolution</para>
				</parameter>
				<parameter name="TransferRate">
					<para>The negotiated transfer rate</para>
				</parameter>
				<parameter name="FileName" multiple="yes">
					<para>The files being affected by the fax operation</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="SendFAX">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>
				Raised when a send fax operation has completed.
			</synopsis>
			<syntax>
				<channel_snapshot/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ReceiveFAX']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MusicOnHoldStart">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when music on hold has started on a channel.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Class">
					<para>The class of music being played on the channel</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="managerEvent">MusicOnHoldStop</ref>
				<ref type="application">MusicOnHold</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MusicOnHoldStop">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when music on hold has stopped on a channel.</synopsis>
			<syntax>
				<channel_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">MusicOnHoldStart</ref>
				<ref type="application">StopMusicOnHold</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MonitorStart">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when monitoring has started on a channel.</synopsis>
			<syntax>
				<channel_snapshot/>
			</syntax>
			<see-also>
				<ref type="managerEvent">MonitorStop</ref>
				<ref type="application">Monitor</ref>
				<ref type="manager">Monitor</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="MonitorStop">
		<managerEventInstance class="EVENT_FLAG_CALL">
		<synopsis>Raised when monitoring has stopped on a channel.</synopsis>
		<syntax>
			<channel_snapshot/>
		</syntax>
		<see-also>
			<ref type="managerEvent">MonitorStart</ref>
			<ref type="application">StopMonitor</ref>
			<ref type="manager">StopMonitor</ref>
		</see-also>
		</managerEventInstance>
	</managerEvent>
***/

/*! \brief The \ref stasis subscription returned by the forwarding of the channel topic
 * to the manager topic
 */
static struct stasis_forward *topic_forwarder;

struct ast_str *ast_manager_build_channel_state_string_prefix(
		const struct ast_channel_snapshot *snapshot,
		const char *prefix)
{
	struct ast_str *out = ast_str_create(1024);
	int res = 0;

	if (!out) {
		return NULL;
	}

	if (snapshot->tech_properties & AST_CHAN_TP_INTERNAL) {
		ast_free(out);
		return NULL;
	}

	res = ast_str_set(&out, 0,
		"%sChannel: %s\r\n"
		"%sChannelState: %u\r\n"
		"%sChannelStateDesc: %s\r\n"
		"%sCallerIDNum: %s\r\n"
		"%sCallerIDName: %s\r\n"
		"%sConnectedLineNum: %s\r\n"
		"%sConnectedLineName: %s\r\n"
		"%sLanguage: %s\r\n"
		"%sAccountCode: %s\r\n"
		"%sContext: %s\r\n"
		"%sExten: %s\r\n"
		"%sPriority: %d\r\n"
		"%sUniqueid: %s\r\n",
		prefix, snapshot->name,
		prefix, snapshot->state,
		prefix, ast_state2str(snapshot->state),
		prefix, S_OR(snapshot->caller_number, "<unknown>"),
		prefix, S_OR(snapshot->caller_name, "<unknown>"),
		prefix, S_OR(snapshot->connected_number, "<unknown>"),
		prefix, S_OR(snapshot->connected_name, "<unknown>"),
		prefix, snapshot->language,
		prefix, snapshot->accountcode,
		prefix, snapshot->context,
		prefix, snapshot->exten,
		prefix, snapshot->priority,
		prefix, snapshot->uniqueid);

	if (!res) {
		ast_free(out);
		return NULL;
	}

	if (snapshot->manager_vars) {
		struct ast_var_t *var;
		AST_LIST_TRAVERSE(snapshot->manager_vars, var, entries) {
			ast_str_append(&out, 0, "%sChanVariable: %s=%s\r\n",
				       prefix,
				       var->name, var->value);
		}
	}

	return out;
}

struct ast_str *ast_manager_build_channel_state_string(
		const struct ast_channel_snapshot *snapshot)
{
	return ast_manager_build_channel_state_string_prefix(snapshot, "");
}

/*! \brief Typedef for callbacks that get called on channel snapshot updates */
typedef struct ast_manager_event_blob *(*channel_snapshot_monitor)(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot);

/*! \brief Handle channel state changes */
static struct ast_manager_event_blob *channel_state_change(
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
		return ast_manager_event_blob_create(
			EVENT_FLAG_CALL, "Newchannel", NO_EXTRA_FIELDS);
	}

	was_hungup = ast_test_flag(&old_snapshot->flags, AST_FLAG_DEAD) ? 1 : 0;
	is_hungup = ast_test_flag(&new_snapshot->flags, AST_FLAG_DEAD) ? 1 : 0;

	if (!was_hungup && is_hungup) {
		return ast_manager_event_blob_create(
			EVENT_FLAG_CALL, "Hangup",
			"Cause: %d\r\n"
			"Cause-txt: %s\r\n",
			new_snapshot->hangupcause,
			ast_cause2str(new_snapshot->hangupcause));
	}

	if (old_snapshot->state != new_snapshot->state) {
		return ast_manager_event_blob_create(
			EVENT_FLAG_CALL, "Newstate", NO_EXTRA_FIELDS);
	}

	/* No event */
	return NULL;
}

static struct ast_manager_event_blob *channel_newexten(
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

	/* Ignore any updates if we're hungup */
	if (ast_test_flag(&new_snapshot->flags, AST_FLAG_DEAD)) {
		return NULL;
	}

	/* Ignore updates if the CEP is unchanged */
	if (old_snapshot && ast_channel_snapshot_cep_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	/* DEPRECATED: Extension field deprecated in 12; remove in 14 */
	return ast_manager_event_blob_create(
		EVENT_FLAG_CALL, "Newexten",
		"Extension: %s\r\n"
		"Application: %s\r\n"
		"AppData: %s\r\n",
		new_snapshot->exten,
		new_snapshot->appl,
		new_snapshot->data);
}

static struct ast_manager_event_blob *channel_new_callerid(
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

	return ast_manager_event_blob_create(
		EVENT_FLAG_CALL, "NewCallerid",
		"CID-CallingPres: %d (%s)\r\n",
		new_snapshot->caller_pres,
		ast_describe_caller_presentation(new_snapshot->caller_pres));
}

static struct ast_manager_event_blob *channel_new_connected_line(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	/* No NewConnectedLine event on cache clear or first event */
	if (!old_snapshot || !new_snapshot) {
		return NULL;
	}

	if (ast_channel_snapshot_connected_line_equal(old_snapshot, new_snapshot)) {
		return NULL;
	}

	return ast_manager_event_blob_create(
		EVENT_FLAG_CALL, "NewConnectedLine", "%s", "");
}

static struct ast_manager_event_blob *channel_new_accountcode(
	struct ast_channel_snapshot *old_snapshot,
	struct ast_channel_snapshot *new_snapshot)
{
	if (!old_snapshot || !new_snapshot) {
		return NULL;
	}

	if (!strcmp(old_snapshot->accountcode, new_snapshot->accountcode)) {
		return NULL;
	}

	return ast_manager_event_blob_create(
		EVENT_FLAG_CALL, "NewAccountCode",
		"OldAccountCode: %s\r\n", old_snapshot->accountcode);
}

channel_snapshot_monitor channel_monitors[] = {
	channel_state_change,
	channel_newexten,
	channel_new_callerid,
	channel_new_accountcode,
	channel_new_connected_line,
};

static void channel_snapshot_update(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	struct stasis_cache_update *update;
	struct ast_channel_snapshot *old_snapshot;
	struct ast_channel_snapshot *new_snapshot;
	size_t i;

	update = stasis_message_data(message);

	ast_assert(ast_channel_snapshot_type() == update->type);

	old_snapshot = stasis_message_data(update->old_snapshot);
	new_snapshot = stasis_message_data(update->new_snapshot);

	for (i = 0; i < ARRAY_LEN(channel_monitors); ++i) {
		RAII_VAR(struct ast_manager_event_blob *, ev, NULL, ao2_cleanup);
		ev = channel_monitors[i](old_snapshot, new_snapshot);

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

static void publish_basic_channel_event(const char *event, int class, struct ast_channel_snapshot *snapshot)
{
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);

	channel_event_string = ast_manager_build_channel_state_string(snapshot);
	if (!channel_event_string) {
		return;
	}

	manager_event(class, event,
		"%s",
		ast_str_buffer(channel_event_string));
}

static void channel_hangup_request_cb(void *data,
	struct stasis_subscription *sub,
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

static void channel_chanspy_stop_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, spyer_channel_string, NULL, ast_free);
	struct ast_channel_snapshot *spyer;
	struct ast_multi_channel_blob *payload = stasis_message_data(message);

	spyer = ast_multi_channel_blob_get_channel(payload, "spyer_channel");
	if (!spyer) {
		ast_log(AST_LOG_WARNING, "Received ChanSpy Start event with no spyer channel!\n");
		return;
	}

	spyer_channel_string = ast_manager_build_channel_state_string_prefix(spyer, "Spyer");
	if (!spyer_channel_string) {
		return;
	}

	manager_event(EVENT_FLAG_CALL, "ChanSpyStop",
		      "%s",
		      ast_str_buffer(spyer_channel_string));
}

static void channel_chanspy_start_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, spyer_channel_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, spyee_channel_string, NULL, ast_free);
	struct ast_channel_snapshot *spyer;
	struct ast_channel_snapshot *spyee;
	struct ast_multi_channel_blob *payload = stasis_message_data(message);

	spyer = ast_multi_channel_blob_get_channel(payload, "spyer_channel");
	if (!spyer) {
		ast_log(AST_LOG_WARNING, "Received ChanSpy Start event with no spyer channel!\n");
		return;
	}
	spyee = ast_multi_channel_blob_get_channel(payload, "spyee_channel");
	if (!spyee) {
		ast_log(AST_LOG_WARNING, "Received ChanSpy Start event with no spyee channel!\n");
		return;
	}

	spyer_channel_string = ast_manager_build_channel_state_string_prefix(spyer, "Spyer");
	if (!spyer_channel_string) {
		return;
	}
	spyee_channel_string = ast_manager_build_channel_state_string_prefix(spyee, "Spyee");
	if (!spyee_channel_string) {
		return;
	}

	manager_event(EVENT_FLAG_CALL, "ChanSpyStart",
		      "%s%s",
		      ast_str_buffer(spyer_channel_string),
		      ast_str_buffer(spyee_channel_string));
}

static void channel_dtmf_begin_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
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
					<channel_snapshot/>
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
	struct stasis_message *message)
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
					<channel_snapshot/>
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

static void channel_hangup_handler_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	struct ast_channel_blob *payload = stasis_message_data(message);
	const char *action = ast_json_string_get(ast_json_object_get(payload->blob, "type"));
	const char *handler = ast_json_string_get(ast_json_object_get(payload->blob, "handler"));
	const char *event;

	channel_event_string = ast_manager_build_channel_state_string(payload->snapshot);

	if (!channel_event_string) {
		return;
	}

	if (!strcmp(action, "type")) {
		event = "HangupHandlerRun";
	} else if (!strcmp(action, "type")) {
		event = "HangupHandlerPop";
	} else if (!strcmp(action, "type")) {
		event = "HangupHandlerPush";
	} else {
		return;
	}
	manager_event(EVENT_FLAG_DIALPLAN, event,
		"%s"
		"Handler: %s\r\n",
		ast_str_buffer(channel_event_string),
		handler);
}

static void channel_fax_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, event_buffer, ast_str_create(256), ast_free);
	struct ast_channel_blob *payload = stasis_message_data(message);
	const char *type = ast_json_string_get(ast_json_object_get(payload->blob, "type"));
	struct ast_json *operation = ast_json_object_get(payload->blob, "operation");
	struct ast_json *status = ast_json_object_get(payload->blob, "status");
	struct ast_json *local_station_id = ast_json_object_get(payload->blob, "local_station_id");
	struct ast_json *remote_station_id = ast_json_object_get(payload->blob, "remote_station_id");
	struct ast_json *fax_pages = ast_json_object_get(payload->blob, "fax_pages");
	struct ast_json *fax_resolution = ast_json_object_get(payload->blob, "fax_resolution");
	struct ast_json *fax_bitrate = ast_json_object_get(payload->blob, "fax_bitrate");
	struct ast_json *filenames = ast_json_object_get(payload->blob, "filenames");
	const char *event;
	size_t array_len;
	size_t i;

	if (!event_buffer) {
		return;
	}

	channel_event_string = ast_manager_build_channel_state_string(payload->snapshot);
	if (!channel_event_string) {
		return;
	}

	if (!strcmp(type, "status")) {
		event = "FAXStatus";
	} else if (!strcmp(type, "receive")) {
		event = "ReceiveFAX";
	} else if (!strcmp(type, "send")) {
		event = "SendFAX";
	} else {
		return;
	}

	if (operation) {
		ast_str_append(&event_buffer, 0, "Operation: %s\r\n", ast_json_string_get(operation));
	}
	if (status) {
		ast_str_append(&event_buffer, 0, "Status: %s\r\n", ast_json_string_get(status));
	}
	if (local_station_id) {
		ast_str_append(&event_buffer, 0, "LocalStationID: %s\r\n", ast_json_string_get(local_station_id));
	}
	if (remote_station_id) {
		ast_str_append(&event_buffer, 0, "RemoteStationID: %s\r\n", ast_json_string_get(remote_station_id));
	}
	if (fax_pages) {
		ast_str_append(&event_buffer, 0, "PagesTransferred: %s\r\n", ast_json_string_get(fax_pages));
	}
	if (fax_resolution) {
		ast_str_append(&event_buffer, 0, "Resolution: %s\r\n", ast_json_string_get(fax_resolution));
	}
	if (fax_bitrate) {
		ast_str_append(&event_buffer, 0, "TransferRate: %s\r\n", ast_json_string_get(fax_bitrate));
	}
	if (filenames) {
		array_len = ast_json_array_size(filenames);
		for (i = 0; i < array_len; i++) {
			ast_str_append(&event_buffer, 0, "FileName: %s\r\n", ast_json_string_get(ast_json_array_get(filenames, i)));
		}
	}

	manager_event(EVENT_FLAG_CALL, event,
		"%s"
		"%s",
		ast_str_buffer(channel_event_string),
		ast_str_buffer(event_buffer));
}

static void channel_moh_start_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_channel_blob *payload = stasis_message_data(message);
	struct ast_json *blob = payload->blob;
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);

	channel_event_string = ast_manager_build_channel_state_string(payload->snapshot);
	if (!channel_event_string) {
		return;
	}

	manager_event(EVENT_FLAG_CALL, "MusicOnHoldStart",
		"%s"
		"Class: %s\r\n",
		ast_str_buffer(channel_event_string),
		ast_json_string_get(ast_json_object_get(blob, "class")));

}

static void channel_moh_stop_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_channel_blob *payload = stasis_message_data(message);

	publish_basic_channel_event("MusicOnHoldStop", EVENT_FLAG_CALL, payload->snapshot);
}

static void channel_monitor_start_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_channel_blob *payload = stasis_message_data(message);

	publish_basic_channel_event("MonitorStart", EVENT_FLAG_CALL, payload->snapshot);
}

static void channel_monitor_stop_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	struct ast_channel_blob *payload = stasis_message_data(message);

	publish_basic_channel_event("MonitorStop", EVENT_FLAG_CALL, payload->snapshot);
}

/*!
 * \brief Callback processing messages for channel dialing
 */
static void channel_dial_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_multi_channel_blob *obj = stasis_message_data(message);
	const char *dialstatus;
	const char *dialstring;
	const char *forward;
	struct ast_channel_snapshot *caller;
	struct ast_channel_snapshot *peer;
	RAII_VAR(struct ast_str *, caller_event_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, peer_event_string, NULL, ast_free);

	caller = ast_multi_channel_blob_get_channel(obj, "caller");
	peer = ast_multi_channel_blob_get_channel(obj, "peer");

	/* Peer is required - otherwise, who are we dialing? */
	ast_assert(peer != NULL);
	peer_event_string = ast_manager_build_channel_state_string_prefix(peer, "Dest");
	if (!peer_event_string) {
		return;
	}

	if (caller && !(caller_event_string = ast_manager_build_channel_state_string(caller))) {
		return;
	}

	dialstatus = ast_json_string_get(ast_json_object_get(ast_multi_channel_blob_get_json(obj), "dialstatus"));
	dialstring = ast_json_string_get(ast_json_object_get(ast_multi_channel_blob_get_json(obj), "dialstring"));
	forward = ast_json_string_get(ast_json_object_get(ast_multi_channel_blob_get_json(obj), "forward"));
	if (ast_strlen_zero(dialstatus)) {
		manager_event(EVENT_FLAG_CALL, "DialBegin",
				"%s"
				"%s"
				"DialString: %s\r\n",
				caller_event_string ? ast_str_buffer(caller_event_string) : "",
				ast_str_buffer(peer_event_string),
				S_OR(dialstring, "unknown"));
	} else {
		int forwarded = !ast_strlen_zero(forward);

		manager_event(EVENT_FLAG_CALL, "DialEnd",
				"%s"
				"%s"
				"%s%s%s"
				"DialStatus: %s\r\n",
				caller_event_string ? ast_str_buffer(caller_event_string) : "",
				ast_str_buffer(peer_event_string),
				forwarded ? "Forward: " : "", S_OR(forward, ""), forwarded ? "\r\n" : "",
				S_OR(dialstatus, "unknown"));
	}

}

static void channel_hold_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	struct ast_str *musicclass_string = ast_str_create(32);
	struct ast_str *channel_event_string;

	if (!musicclass_string) {
		return;
	}

	channel_event_string = ast_manager_build_channel_state_string(obj->snapshot);
	if (!channel_event_string) {
		ast_free(musicclass_string);
		return;
	}

	if (obj->blob) {
		const char *musicclass;

		musicclass = ast_json_string_get(ast_json_object_get(obj->blob, "musicclass"));

		if (!ast_strlen_zero(musicclass)) {
			ast_str_set(&musicclass_string, 0, "MusicClass: %s\r\n", musicclass);
		}
	}

	manager_event(EVENT_FLAG_CALL, "Hold",
		"%s"
		"%s",
		ast_str_buffer(channel_event_string),
		ast_str_buffer(musicclass_string));

	ast_free(musicclass_string);
	ast_free(channel_event_string);
}

static void channel_unhold_cb(void *data, struct stasis_subscription *sub,
	struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);
	struct ast_str *channel_event_string;

	channel_event_string = ast_manager_build_channel_state_string(obj->snapshot);
	if (!channel_event_string) {
		return;
	}

	manager_event(EVENT_FLAG_CALL, "Unhold",
		"%s",
		ast_str_buffer(channel_event_string));

	ast_free(channel_event_string);
}

static void manager_channels_shutdown(void)
{
	stasis_forward_cancel(topic_forwarder);
	topic_forwarder = NULL;
}

int manager_channels_init(void)
{
	int ret = 0;
	struct stasis_topic *manager_topic;
	struct stasis_topic *channel_topic;
	struct stasis_message_router *message_router;

	manager_topic = ast_manager_get_topic();
	if (!manager_topic) {
		return -1;
	}
	message_router = ast_manager_get_message_router();
	if (!message_router) {
		return -1;
	}
	channel_topic = ast_channel_topic_all_cached();
	if (!channel_topic) {
		return -1;
	}

	topic_forwarder = stasis_forward_all(channel_topic, manager_topic);
	if (!topic_forwarder) {
		return -1;
	}

	ast_register_cleanup(manager_channels_shutdown);

	ret |= stasis_message_router_add_cache_update(message_router,
		ast_channel_snapshot_type(), channel_snapshot_update, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_dtmf_begin_type(), channel_dtmf_begin_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_dtmf_end_type(), channel_dtmf_end_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_hangup_request_type(), channel_hangup_request_cb,
		NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_dial_type(), channel_dial_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_hold_type(), channel_hold_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_unhold_type(), channel_unhold_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_fax_type(), channel_fax_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_chanspy_start_type(), channel_chanspy_start_cb,
		NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_chanspy_stop_type(), channel_chanspy_stop_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_hangup_handler_type(), channel_hangup_handler_cb,
		NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_moh_start_type(), channel_moh_start_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_moh_stop_type(), channel_moh_stop_cb, NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_monitor_start_type(), channel_monitor_start_cb,
		NULL);

	ret |= stasis_message_router_add(message_router,
		ast_channel_monitor_stop_type(), channel_monitor_stop_cb, NULL);

	/* If somehow we failed to add any routes, just shut down the whole
	 * thing and fail it.
	 */
	if (ret) {
		manager_channels_shutdown();
		return -1;
	}

	return 0;
}

