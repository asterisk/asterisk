/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Jonathan Rose <jrose@digium.com>
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
 * \brief Confbridge manager events for stasis messages
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/channel.h"
#include "asterisk/bridging.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_bridging.h"
#include "asterisk/manager.h"
#include "asterisk/stasis_message_router.h"
#include "include/confbridge.h"

/*** DOCUMENTATION
	<managerEvent language="en_US" name="ConfbridgeStart">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a conference starts.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeEnd</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeEnd">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a conference ends.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeStart</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeJoin">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel joins a Confbridge conference.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeLeave</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeLeave">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a Confbridge conference.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeJoin</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeRecord">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a conference starts recording.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeStopRecord</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeStopRecord">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a conference that was recording stops recording.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeRecord</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeMute">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a Confbridge participant mutes.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeUnmute</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ConfbridgeUnmute">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a confbridge participant unmutes.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<see-also>
				<ref type="managerEvent">ConfbridgeMute</ref>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>

	<managerEvent language="en_US" name="ConfbridgeTalking">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a confbridge participant unmutes.</synopsis>
			<syntax>
				<parameter name="Conference">
					<para>The name of the Confbridge conference.</para>
				</parameter>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='BridgeCreate']/managerEventInstance/syntax/parameter)" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='Newchannel']/managerEventInstance/syntax/parameter)" />
				<parameter name="TalkingStatus">
					<enumlist>
						<enum name="on"/>
						<enum name="off"/>
					</enumlist>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
***/

static struct stasis_message_router *bridge_state_router;
static struct stasis_message_router *channel_state_router;

static void append_event_header(struct ast_str **fields_string,
					const char *header, const char *value)
{
	struct ast_str *working_str = *fields_string;

	if (!working_str) {
		working_str = ast_str_create(128);
		if (!working_str) {
			return;
		}
		*fields_string = working_str;
	}

	ast_str_append(&working_str, 0,
		"%s: %s\r\n",
		header, value);
}

static void stasis_confbridge_cb(void *data, struct stasis_subscription *sub,
					struct stasis_topic *topic,
					struct stasis_message *message)
{
	struct ast_bridge_blob *blob = stasis_message_data(message);
	const char *type = ast_bridge_blob_json_type(blob);
	const char *conference_name;
	RAII_VAR(struct ast_str *, bridge_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, extra_text, NULL, ast_free);
	char *event;

	if (!blob || !type) {
		ast_assert(0);
		return;
	}

	if (!strcmp("confbridge_start", type)) {
		event = "ConfbridgeStart";
	} else if (!strcmp("confbridge_end", type)) {
		event = "ConfbridgeEnd";
	} else if (!strcmp("confbridge_leave", type)) {
		event = "ConfbridgeLeave";
	} else if (!strcmp("confbridge_join", type)) {
		event = "ConfbridgeJoin";
	} else if (!strcmp("confbridge_record", type)) {
		event = "ConfbridgeRecord";
	} else if (!strcmp("confbridge_stop_record", type)) {
		event = "ConfbridgeStopRecord";
	} else if (!strcmp("confbridge_mute", type)) {
		event = "ConfbridgeMute";
	} else if (!strcmp("confbridge_unmute", type)) {
		event = "ConfbridgeUnmute";
	} else if (!strcmp("confbridge_talking", type)) {
		const char *talking_status = ast_json_string_get(ast_json_object_get(blob->blob, "talking_status"));
		event = "ConfbridgeTalking";

		if (!talking_status) {
			return;
		}

		append_event_header(&extra_text, "TalkingStatus", talking_status);

	} else {
		return;
	}

	conference_name = ast_json_string_get(ast_json_object_get(blob->blob, "conference"));

	if (!conference_name) {
		ast_assert(0);
		return;
	}

	bridge_text = ast_manager_build_bridge_state_string(blob->bridge, "");
	if (blob->channel) {
		channel_text = ast_manager_build_channel_state_string(blob->channel);
	}

	manager_event(EVENT_FLAG_CALL, event,
		"Conference: %s\r\n"
		"%s"
		"%s"
		"%s",
		conference_name,
		ast_str_buffer(bridge_text),
		channel_text ? ast_str_buffer(channel_text) : "",
		extra_text ? ast_str_buffer(extra_text) : "");
}

static struct stasis_message_type *confbridge_msg_type;

struct stasis_message_type *confbridge_message_type(void)
{
	return confbridge_msg_type;
}

void manager_confbridge_shutdown(void) {
	ao2_cleanup(confbridge_msg_type);
	confbridge_msg_type = NULL;

	if (bridge_state_router) {
		stasis_message_router_unsubscribe(bridge_state_router);
		bridge_state_router = NULL;
	}

	if (channel_state_router) {
		stasis_message_router_unsubscribe(channel_state_router);
		channel_state_router = NULL;
	}
}

int manager_confbridge_init(void)
{
	if (!(confbridge_msg_type = stasis_message_type_create("confbridge"))) {
		return -1;
	}

	bridge_state_router = stasis_message_router_create(
		stasis_caching_get_topic(ast_bridge_topic_all_cached()));

	if (!bridge_state_router) {
		return -1;
	}

	if (stasis_message_router_add(bridge_state_router,
					 confbridge_message_type(),
					 stasis_confbridge_cb,
					 NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}

	channel_state_router = stasis_message_router_create(
		stasis_caching_get_topic(ast_channel_topic_all_cached()));

	if (!channel_state_router) {
		manager_confbridge_shutdown();
		return -1;
	}

	if (stasis_message_router_add(channel_state_router,
					 confbridge_message_type(),
					 stasis_confbridge_cb,
					 NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}

	return 0;
}
