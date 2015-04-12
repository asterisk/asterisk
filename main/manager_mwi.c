/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Matt Jordan <mjordan@digium.com>
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
 * \brief The Asterisk Management Interface - AMI (MWI event handling)
 *
 * \author Matt Jordan <mjordan@digium.com>
 */

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/manager.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/stasis.h"

struct stasis_message_router *mwi_state_router;

/*** DOCUMENTATION
 ***/

/*! \brief The \ref stasis subscription returned by the forwarding of the MWI topic
 * to the manager topic
 */
static struct stasis_forward *topic_forwarder;

/*! \brief Callback function used by \ref mwi_app_event_cb to weed out "Event" keys */
static int exclude_event_cb(const char *key)
{
	if (!strcmp(key, "Event")) {
		return -1;
	}
	return 0;
}

/*! \brief Generic MWI event callback used for one-off events from voicemail modules */
static void mwi_app_event_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	struct ast_mwi_blob *payload = stasis_message_data(message);
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, event_buffer, NULL, ast_free);
	struct ast_json *event_json = ast_json_object_get(payload->blob, "Event");

	if (!event_json) {
		return;
	}

	if (payload->mwi_state && payload->mwi_state->snapshot) {
		channel_event_string = ast_manager_build_channel_state_string(payload->mwi_state->snapshot);
	}

	event_buffer = ast_manager_str_from_json_object(payload->blob, exclude_event_cb);
	if (!event_buffer) {
		ast_log(AST_LOG_WARNING, "Failed to create payload for event %s\n", ast_json_string_get(event_json));
		return;
	}

	manager_event(EVENT_FLAG_CALL, ast_json_string_get(event_json),
			"Mailbox: %s\r\n"
			"%s"
			"%s",
			payload->mwi_state ? payload->mwi_state->uniqueid : "Unknown",
			ast_str_buffer(event_buffer),
			channel_event_string ? ast_str_buffer(channel_event_string) : "");
}

static void mwi_update_cb(void *data, struct stasis_subscription *sub,
				    struct stasis_message *message)
{
	struct ast_mwi_state *mwi_state;
	RAII_VAR(struct ast_str *, channel_event_string, NULL, ast_free);

	if (ast_mwi_state_type() != stasis_message_type(message)) {
		return;
	}

	mwi_state = stasis_message_data(message);
	if (!mwi_state) {
		return;
	}

	if (mwi_state->snapshot) {
		channel_event_string = ast_manager_build_channel_state_string(mwi_state->snapshot);
	}

	/*** DOCUMENTATION
		<managerEventInstance>
			<synopsis>Raised when the state of messages in a voicemail mailbox
			has changed or when a channel has finished interacting with a
			mailbox.</synopsis>
			<syntax>
				<channel_snapshot/>
				<parameter name="Mailbox">
					<para>The mailbox with the new message, specified as <literal>mailbox</literal>@<literal>context</literal></para>
				</parameter>
				<parameter name="Waiting">
					<para>Whether or not the mailbox has messages waiting for it.</para>
				</parameter>
				<parameter name="New">
					<para>The number of new messages.</para>
				</parameter>
				<parameter name="Old">
					<para>The number of old messages.</para>
				</parameter>
			</syntax>
			<description>
				<note><para>The Channel related parameters are only present if a
				channel was involved in the manipulation of a mailbox. If no
				channel is involved, the parameters are not included with the
				event.</para>
				</note>
			</description>
		</managerEventInstance>
	***/
	manager_event(EVENT_FLAG_CALL, "MessageWaiting",
			"%s"
			"Mailbox: %s\r\n"
			"Waiting: %d\r\n"
			"New: %d\r\n"
			"Old: %d\r\n",
			AS_OR(channel_event_string, ""),
			mwi_state->uniqueid,
			ast_app_has_voicemail(mwi_state->uniqueid, NULL),
			mwi_state->new_msgs,
			mwi_state->old_msgs);
}

static void manager_mwi_shutdown(void)
{
	stasis_forward_cancel(topic_forwarder);
	topic_forwarder = NULL;
}

int manager_mwi_init(void)
{
	int ret = 0;
	struct stasis_topic *manager_topic;
	struct stasis_topic *mwi_topic;
	struct stasis_message_router *message_router;

	manager_topic = ast_manager_get_topic();
	if (!manager_topic) {
		return -1;
	}
	message_router = ast_manager_get_message_router();
	if (!message_router) {
		return -1;
	}
	mwi_topic = ast_mwi_topic_all();
	if (!mwi_topic) {
		return -1;
	}

	topic_forwarder = stasis_forward_all(mwi_topic, manager_topic);
	if (!topic_forwarder) {
		return -1;
	}

	ast_register_cleanup(manager_mwi_shutdown);

	ret |= stasis_message_router_add(message_router,
					 ast_mwi_state_type(),
					 mwi_update_cb,
					 NULL);

	ret |= stasis_message_router_add(message_router,
					 ast_mwi_vm_app_type(),
					 mwi_app_event_cb,
					 NULL);

	/* If somehow we failed to add any routes, just shut down the whole
	 * thing and fail it.
	 */
	if (ret) {
		manager_mwi_shutdown();
		return -1;
	}

	return 0;
}
