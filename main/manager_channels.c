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

#include "asterisk/channel.h"
#include "asterisk/manager.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/pbx.h"

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
				<parameter name="Cause">
					<para>A numeric cause code for why the channel was hung up.</para>
				</parameter>
				<parameter name="Cause-txt">
					<para>A description of why the channel was hung up.</para>
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
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

/*!
 * \brief Generate the AMI message body from a channel snapshot
 * \internal
 *
 * \param snapshot the channel snapshot for which to generate an AMI message
 *                 body
 *
 * \retval NULL on error
 * \retval ast_str* on success (must be ast_freed by caller)
 */
static struct ast_str *manager_build_channel_state_string(
	const struct ast_channel_snapshot *snapshot)
{
	struct ast_str *out = ast_str_create(1024);
	int res = 0;
	if (!out) {
		return NULL;
	}
	res = ast_str_set(&out, 0,
		"Channel: %s\r\n"
		"ChannelState: %d\r\n"
		"ChannelStateDesc: %s\r\n"
		"CallerIDNum: %s\r\n"
		"CallerIDName: %s\r\n"
		"ConnectedLineNum: %s\r\n"
		"ConnectedLineName: %s\r\n"
		"AccountCode: %s\r\n"
		"Context: %s\r\n"
		"Exten: %s\r\n"
		"Priority: %d\r\n"
		"Uniqueid: %s\r\n"
		"Cause: %d\r\n"
		"Cause-txt: %s\r\n",
		snapshot->name,
		snapshot->state,
		ast_state2str(snapshot->state),
		snapshot->caller_number,
		snapshot->caller_name,
		snapshot->connected_number,
		snapshot->connected_name,
		snapshot->accountcode,
		snapshot->context,
		snapshot->exten,
		snapshot->priority,
		snapshot->uniqueid,
		snapshot->hangupcause,
		ast_cause2str(snapshot->hangupcause));

	if (!res) {
		return NULL;
	}

	return out;
}

static inline int cep_has_changed(
	const struct ast_channel_snapshot *old_snapshot,
	const struct ast_channel_snapshot *new_snapshot)
{
	ast_assert(old_snapshot != NULL);
	ast_assert(new_snapshot != NULL);
	return old_snapshot->priority != new_snapshot->priority ||
		strcmp(old_snapshot->context, new_snapshot->context) != 0 ||
		strcmp(old_snapshot->exten, new_snapshot->exten) != 0;
}

static void channel_snapshot_update(void *data, struct stasis_subscription *sub,
				    struct stasis_topic *topic,
				    struct stasis_message *message)
{
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	struct stasis_cache_update *update = stasis_message_data(message);
	struct ast_channel_snapshot *old_snapshot;
	struct ast_channel_snapshot *new_snapshot;
	int is_hungup, was_hungup;
	char *manager_event = NULL;
	int new_exten;

	if (ast_channel_snapshot() != update->type) {
		return;
	}

	old_snapshot = stasis_message_data(update->old_snapshot);
	new_snapshot = stasis_message_data(update->new_snapshot);

	if (!new_snapshot) {
		/* Ignore cache clearing events; we'll see the hangup first */
		return;
	}

	was_hungup = (old_snapshot && ast_test_flag(&old_snapshot->flags, AST_FLAG_ZOMBIE)) ? 1 : 0;
	is_hungup = ast_test_flag(&new_snapshot->flags, AST_FLAG_ZOMBIE) ? 1 : 0;

	if (!old_snapshot) {
		manager_event = "Newchannel";
	}

	if (old_snapshot && old_snapshot->state != new_snapshot->state) {
		manager_event = "Newstate";
	}

	if (!was_hungup && is_hungup) {
		manager_event = "Hangup";
	}

	/* Detect Newexten transitions
	 *  - if new snapshot has an application set AND
	 *    - first snapshot OR
	 *    - if the old snapshot has no application (first Newexten) OR
	 *    - if the context/priority/exten changes
	 */
	new_exten = !ast_strlen_zero(new_snapshot->appl) && (
		!old_snapshot ||
		ast_strlen_zero(old_snapshot->appl) ||
		cep_has_changed(old_snapshot, new_snapshot));

	if (manager_event || new_exten) {
		channel_event_string =
			manager_build_channel_state_string(new_snapshot);
	}

	if (!channel_event_string) {
		return;
	}

	/* Channel state change events */
	if (manager_event) {
		manager_event(EVENT_FLAG_CALL, manager_event, "%s",
			      ast_str_buffer(channel_event_string));
	}

	if (new_exten) {
		/* DEPRECATED: Extension field deprecated in 12; remove in 14 */
		/*** DOCUMENTATION
			<managerEventInstance>
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
		***/
		manager_event(EVENT_FLAG_DIALPLAN, "Newexten",
			      "%s"
			      "Extension: %s\r\n"
			      "Application: %s\r\n"
			      "AppData: %s\r\n",
			      ast_str_buffer(channel_event_string),
			      new_snapshot->exten,
			      new_snapshot->appl,
			      new_snapshot->data);
	}
}

static void channel_varset(struct ast_channel_blob *obj)
{
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	const char *variable = ast_json_string_get(ast_json_object_get(obj->blob, "variable"));
	const char *value = ast_json_string_get(ast_json_object_get(obj->blob, "value"));

	if (obj->snapshot) {
		channel_event_string = manager_build_channel_state_string(obj->snapshot);
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

static void channel_userevent(struct ast_channel_blob *obj)
{
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	const char *eventname;
	const char *body;

	eventname = ast_json_string_get(ast_json_object_get(obj->blob, "eventname"));
	body = ast_json_string_get(ast_json_object_get(obj->blob, "body"));
	channel_event_string = manager_build_channel_state_string(obj->snapshot);

	if (!channel_event_string) {
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
		      ast_str_buffer(channel_event_string), eventname, body);
}

/*!
 * \brief Callback processing messages on the channel topic.
 */
static void channel_blob_cb(void *data, struct stasis_subscription *sub,
			    struct stasis_topic *topic,
			    struct stasis_message *message)
{
	struct ast_channel_blob *obj = stasis_message_data(message);

	if (strcmp("varset", ast_channel_blob_type(obj)) == 0) {
		channel_varset(obj);
	} else if (strcmp("userevent", ast_channel_blob_type(obj)) == 0) {
		channel_userevent(obj);
	}
}

static void manager_channels_shutdown(void)
{
	stasis_message_router_unsubscribe(channel_state_router);
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
					 stasis_cache_update(),
					 channel_snapshot_update,
					 NULL);

	ret |= stasis_message_router_add(channel_state_router,
					 ast_channel_blob(),
					 channel_blob_cb,
					 NULL);

	/* If somehow we failed to add any routes, just shut down the whole
	 * things and fail it.
	 */
	if (ret) {
		manager_channels_shutdown();
		return -1;
	}

	return 0;
}
