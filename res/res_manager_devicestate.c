/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2014, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<manager name="DeviceStateList" language="en_US">
		<since>
			<version>13.0.0</version>
		</since>
		<synopsis>
			List the current known device states.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>This will list out all known device states in a
			sequence of <replaceable>DeviceStateChange</replaceable> events.
			When finished, a <replaceable>DeviceStateListComplete</replaceable> event
			will be emitted.</para>
		</description>
		<see-also>
			<ref type="managerEvent">DeviceStateChange</ref>
			<ref type="function">DEVICE_STATE</ref>
		</see-also>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='DeviceStateChange'])" />
			</list-elements>
			<managerEvent name="DeviceStateListComplete" language="en_US">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<since>
						<version>13.0.0</version>
					</since>
					<synopsis>
						Indicates the end of the list the current known extension states.
					</synopsis>
					<syntax>
						<parameter name="EventList">
							<para>Conveys the status of the event list.</para>
						</parameter>
						<parameter name="ListItems">
							<para>Conveys the number of statuses reported.</para>
						</parameter>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>
	<manager name="DeviceStateChange" language="en_US">
		<since>
			<version>24.1.0</version>
			<version>23.7.0</version>
			<version>22.13.0</version>
			<version>20.23.0</version>
		</since>
		<synopsis>
			Set a device state
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Device" required="true">
				<para>Device name. Does not need to be Custom; however, set device states for non-Custom devices with caution.</para>
			</parameter>
			<parameter name="State" required="true">
				<para>The new device state value.</para>
				<para>Should be one of the following:</para>
				<para>The possible values are:</para>
				<para>UNKNOWN | NOT_INUSE | INUSE | BUSY | INVALID | UNAVAILABLE | RINGING | RINGINUSE | ONHOLD</para>
			</parameter>
			<parameter name="Cachable" required="false">
				<para>Whether or not this device state is cachable. Default is true, which is needed to persist the update.</para>
			</parameter>
			<parameter name="EntityID" required="true">
				<para>The Entity ID of the remote Asterisk system that originated this device state update.</para>
				<para>If the Entity ID provided matches the local Asterisk system's Entity ID, the update will be rejected.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sets a device state value.</para>
			<para>This can be used to manually synchronize the device state of remote devices.
			It should NOT be used to set the device state of local devices.</para>
		</description>
		<see-also>
			<ref type="manager">MailboxStateChange</ref>
		</see-also>
	</manager>
	<manager name="MailboxStateChange" language="en_US">
		<since>
			<version>24.0.0</version>
		</since>
		<synopsis>
			Set mailbox state
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Mailbox" required="true">
				<para>The name of the mailbox (with optional context).</para>
			</parameter>
			<parameter name="NewMessages" required="true">
				<para>The number of new messages.</para>
			</parameter>
			<parameter name="OldMessages" required="true">
				<para>The number of old messages.</para>
			</parameter>
			<parameter name="EntityID" required="true">
				<para>The Entity ID of the remote Asterisk system that originated this mailbox state update.</para>
				<para>If the Entity ID provided matches the local Asterisk system's Entity ID, the update will be rejected.</para>
			</parameter>
		</syntax>
		<description>
			<para>Sets a mailbox's state.</para>
			<para>This can be used to manually synchronize the mailbox state of remote mailboxes.
			It should NOT be used to set the mailbox state of local mailboxes.</para>
			<para>Note this differs from the <literal>MWIUpdate</literal> action, which is more heavyweight
			and depends on <literal>res_mwi_external</literal>.</para>
		</description>
		<see-also>
			<ref type="manager">DeviceStateChange</ref>
			<ref type="manager">MWIUpdate</ref>
		</see-also>
	</manager>
 ***/


#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/manager.h"
#include "asterisk/stasis.h"
#include "asterisk/devicestate.h"
#include "asterisk/mwi.h"
#include "asterisk/conversions.h"

static struct stasis_forward *topic_forwarder;

static int action_devicestatelist(struct mansession *s, const struct message *m)
{
	RAII_VAR(struct ao2_container *, device_states, NULL, ao2_cleanup);
	const char *action_id = astman_get_header(m, "ActionID");
	struct stasis_message *msg;
	struct ao2_iterator it_states;
	int count = 0;

	device_states = stasis_cache_dump_by_eid(ast_device_state_cache(),
		ast_device_state_message_type(), NULL);
	if (!device_states) {
		astman_send_error(s, m, "Memory Allocation Failure");
		return 0;
	}

	astman_send_listack(s, m, "Device State Changes will follow", "start");

	it_states = ao2_iterator_init(device_states, 0);
	for (; (msg = ao2_iterator_next(&it_states)); ao2_ref(msg, -1)) {
		struct ast_manager_event_blob *blob = stasis_message_to_ami(msg);

		if (!blob) {
			continue;
		}

		count++;

		astman_append(s, "Event: %s\r\n", blob->manager_event);
		if (!ast_strlen_zero(action_id)) {
			astman_append(s, "ActionID: %s\r\n", action_id);
		}
		astman_append(s, "%s\r\n", blob->extra_fields);
		ao2_ref(blob, -1);
	}
	ao2_iterator_destroy(&it_states);

	astman_send_list_complete_start(s, m, "DeviceStateListComplete", count);
	astman_send_list_complete_end(s);

	return 0;
}

static int action_devicestatechange(struct mansession *s, const struct message *m)
{
	struct ast_eid eid;
	enum ast_device_state state_val;
	const char *device = astman_get_header(m, "Device");
	const char *state = astman_get_header(m, "State");
	const char *cachable = astman_get_header(m, "Cachable");
	const char *entity_id = astman_get_header(m, "EntityID");

	if (ast_strlen_zero(device) || ast_strlen_zero(state)) {
		astman_send_error(s, m, "Missing device or device state");
		return 0;
	}
	if (ast_strlen_zero(entity_id) || ast_str_to_eid(&eid, entity_id)) {
		astman_send_error(s, m, "Missing or invalid entity ID");
		return 0;
	}
	if (!strchr(device, '/') && !strchr(device, ':')) {
		astman_send_error(s, m, "Invalid device name");
		return 0;
	}

	state_val = ast_devstate_val(state);
	if (state_val == AST_DEVICE_UNKNOWN && strcasecmp(state, "UNKNOWN")) {
		astman_send_error(s, m, "Invalid device state value");
		return 0;
	}

	if (!ast_eid_cmp(&ast_eid_default, &eid)) {
		astman_send_error(s, m, "Entity ID is ourself (must be from a different Asterisk system)");
		return 0;
	}

	ast_publish_device_state_full(device, state_val, ast_false(cachable) ? AST_DEVSTATE_NOT_CACHABLE : AST_DEVSTATE_CACHABLE, &eid);

	astman_send_ack(s, m, "Updated or set device state");
	return 0;
}

static int action_mailboxstatechange(struct mansession *s, const struct message *m)
{
	struct ast_eid eid;
	int new_msgs, old_msgs;
	char *context, *mailbox;
	const char *mbox = astman_get_header(m, "Mailbox");
	const char *newmsgs = astman_get_header(m, "NewMessages");
	const char *oldmsgs = astman_get_header(m, "OldMessages");
	const char *entity_id = astman_get_header(m, "EntityID");

	if (ast_strlen_zero(mbox) || ast_strlen_zero(newmsgs) || ast_strlen_zero(oldmsgs)) {
		astman_send_error(s, m, "Missing required parameters");
		return 0;
	}
	if (ast_strlen_zero(entity_id) || ast_str_to_eid(&eid, entity_id)) {
		astman_send_error(s, m, "Missing or invalid entity ID");
		return 0;
	}

	if (ast_str_to_int(newmsgs, &new_msgs) || ast_str_to_int(oldmsgs, &old_msgs)) {
		astman_send_error(s, m, "Invalid mailbox counts");
		return 0;
	}

	if (!ast_eid_cmp(&ast_eid_default, &eid)) {
		astman_send_error(s, m, "Entity ID is ourself (must be from a different Asterisk system)");
		return 0;
	}

	context = ast_strdupa(mbox);
	mailbox = strsep(&context, "@");

	ast_publish_mwi_state_full(mailbox, context, new_msgs, old_msgs, NULL, &eid);
	astman_send_ack(s, m, "Updated mailbox state");
	return 0;
}

static int unload_module(void)
{
	topic_forwarder = stasis_forward_cancel(topic_forwarder);
	ast_manager_unregister("DeviceStateList");
	ast_manager_unregister("DeviceStateChange");
	ast_manager_unregister("MailboxStateChange");

	return 0;
}

static int load_module(void)
{
	struct stasis_topic *manager_topic;

	if (ast_eid_is_empty(&ast_eid_default)) {
		ast_log(LOG_ERROR, "Entity ID is not set.\n");
		return AST_MODULE_LOAD_DECLINE;
	}

	manager_topic = ast_manager_get_topic();
	if (!manager_topic) {
		return AST_MODULE_LOAD_DECLINE;
	}
	topic_forwarder = stasis_forward_all(ast_device_state_topic_all(), manager_topic);
	if (!topic_forwarder) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_manager_register_xml("DeviceStateList", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING,
		                         action_devicestatelist)) {
		topic_forwarder = stasis_forward_cancel(topic_forwarder);
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_manager_register_xml("DeviceStateChange", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_devicestatechange)) {
		topic_forwarder = stasis_forward_cancel(topic_forwarder);
		ast_manager_unregister("DeviceStateList");
		return AST_MODULE_LOAD_DECLINE;
	}
	if (ast_manager_register_xml("MailboxStateChange", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING, action_mailboxstatechange)) {
		topic_forwarder = stasis_forward_cancel(topic_forwarder);
		ast_manager_unregister("DeviceStateList");
		ast_manager_unregister("DeviceStateChange");
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Manager Device State Topic Forwarder",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEVSTATE_CONSUMER,
);
