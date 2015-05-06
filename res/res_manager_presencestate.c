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
	<manager name="PresenceStateList" language="en_US">
		<synopsis>
			List the current known presence states.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>This will list out all known presence states in a
			sequence of <replaceable>PresenceStateChange</replaceable> events.
			When finished, a <replaceable>PresenceStateListComplete</replaceable> event
			will be emitted.</para>
		</description>
		<see-also>
			<ref type="manager">PresenceState</ref>
			<ref type="managerEvent">PresenceStatus</ref>
			<ref type="function">PRESENCE_STATE</ref>
		</see-also>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='PresenceStateChange'])" />
			</list-elements>
			<managerEvent name="PresenceStateListComplete" language="en_US">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
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
 ***/

#include "asterisk.h"
#include "asterisk/module.h"
#include "asterisk/manager.h"
#include "asterisk/stasis.h"
#include "asterisk/presencestate.h"

static struct stasis_forward *topic_forwarder;

static int action_presencestatelist(struct mansession *s, const struct message *m)
{
	RAII_VAR(struct ao2_container *, presence_states, NULL, ao2_cleanup);
	const char *action_id = astman_get_header(m, "ActionID");
	struct stasis_message *msg;
	struct ao2_iterator it_states;
	int count = 0;

	presence_states = stasis_cache_dump(ast_presence_state_cache(),
		ast_presence_state_message_type());
	if (!presence_states) {
		astman_send_error(s, m, "Memory Allocation Failure");
		return 0;
	}

	astman_send_listack(s, m, "Presence State Changes will follow", "start");

	it_states = ao2_iterator_init(presence_states, 0);
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

	astman_send_list_complete_start(s, m, "PresenceStateListComplete", count);
	astman_send_list_complete_end(s);

	return 0;
}

static int unload_module(void)
{
	ast_manager_unregister("PresenceStateList");
	topic_forwarder = stasis_forward_cancel(topic_forwarder);
	return 0;
}

static int load_module(void)
{
	struct stasis_topic *manager_topic;

	manager_topic = ast_manager_get_topic();
	if (!manager_topic) {
		return AST_MODULE_LOAD_DECLINE;
	}
	topic_forwarder = stasis_forward_all(ast_presence_state_topic_all(), manager_topic);
	if (!topic_forwarder) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (ast_manager_register_xml("PresenceStateList", EVENT_FLAG_CALL | EVENT_FLAG_REPORTING,
		                         action_presencestatelist)) {
		topic_forwarder = stasis_forward_cancel(topic_forwarder);
		return AST_MODULE_LOAD_DECLINE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Manager Presence State Topic Forwarder",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_DEVSTATE_CONSUMER,
);
