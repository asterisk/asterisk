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
 * \brief Call Parking Manager Actions and Events
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "res_parking.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"
#include "asterisk/features.h"
#include "asterisk/manager.h"
#include "asterisk/bridge.h"
#include "asterisk/module.h"

/*** DOCUMENTATION
	<manager name="Parkinglots" language="en_US">
		<synopsis>
			Get a list of parking lots
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
		</syntax>
		<description>
			<para>List all parking lots as a series of AMI events</para>
		</description>
	</manager>
	<manager name="ParkedCalls" language="en_US">
		<synopsis>
			List parked calls.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="ParkingLot">
				<para>If specified, only show parked calls from the parking lot with this name.</para>
			</parameter>
		</syntax>
		<description>
			<para>List parked calls.</para>
		</description>
	</manager>
	<manager name="Park" language="en_US">
		<synopsis>
			Park a channel.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Channel name to park.</para>
			</parameter>
			<parameter name="TimeoutChannel" required="false">
				<para>Channel name to use when constructing the dial string that will be dialed if the parked channel
				times out. If <literal>TimeoutChannel</literal> is in a two party bridge with
				<literal>Channel</literal>, then <literal>TimeoutChannel</literal> will receive an announcement and be
				treated as having parked <literal>Channel</literal> in the same manner as the Park Call DTMF feature.
				</para>
			</parameter>
			<parameter name="AnnounceChannel" required="false">
				<para>If specified, then this channel will receive an announcement when <literal>Channel</literal>
				is parked if <literal>AnnounceChannel</literal> is in a state where it can receive announcements
				(AnnounceChannel must be bridged). <literal>AnnounceChannel</literal> has no bearing on the actual
				state of the parked call.</para>
			</parameter>
			<parameter name="Timeout" required="false">
				<para>Overrides the timeout of the parking lot for this park action. Specified in milliseconds, but will be converted to
					seconds. Use a value of 0 to disable the timeout.
				</para>
			</parameter>
			<parameter name="Parkinglot" required="false">
				<para>The parking lot to use when parking the channel</para>
			</parameter>
		</syntax>
		<description>
			<para>Park an arbitrary channel with optional arguments for specifying the parking lot used, how long
				the channel should remain parked, and what dial string to use as the parker if the call times out.
			</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="ParkedCall">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel is parked.</synopsis>
			<syntax>
				<channel_snapshot prefix="Parkee"/>
				<parameter name="ParkerDialString">
					<para>Dial String that can be used to call back the parker on ParkingTimeout.</para>
				</parameter>
				<parameter name="Parkinglot">
					<para>Name of the parking lot that the parkee is parked in</para>
				</parameter>
				<parameter name="ParkingSpace">
					<para>Parking Space that the parkee is parked in</para>
				</parameter>
				<parameter name="ParkingTimeout">
				<para>Time remaining until the parkee is forcefully removed from parking in seconds</para>
				</parameter>
				<parameter name="ParkingDuration">
					<para>Time the parkee has been in the parking bridge (in seconds)</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ParkedCallTimeOut">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a parking lot due to reaching the time limit of being parked.</synopsis>
			<syntax>
				<channel_snapshot prefix="Parkee"/>
				<channel_snapshot prefix="Parker"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCall']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ParkedCallGiveUp">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a parking lot because it hung up without being answered.</synopsis>
			<syntax>
				<channel_snapshot prefix="Parkee"/>
				<channel_snapshot prefix="Parker"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCall']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="UnParkedCall">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a parking lot because it was retrieved from the parking lot and reconnected.</synopsis>
			<syntax>
				<channel_snapshot prefix="Parkee"/>
				<channel_snapshot prefix="Parker"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCall']/managerEventInstance/syntax/parameter)" />
				<channel_snapshot prefix="Retriever"/>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ParkedCallSwap">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel takes the place of a previously parked channel</synopsis>
			<syntax>
				<channel_snapshot prefix="Parkee"/>
				<channel_snapshot prefix="Parker"/>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCall']/managerEventInstance/syntax/parameter)" />
			</syntax>
			<description>
				<para>This event is raised when a channel initially parked in the parking lot
				is swapped out with a different channel. The most common case for this is when
				an attended transfer to a parking lot occurs. The Parkee information in the event
				will indicate the party that was swapped into the parking lot.</para>
			</description>
		</managerEventInstance>
	</managerEvent>
 ***/

/*! \brief subscription to the parking lot topic */
static struct stasis_subscription *parking_sub;

static struct ast_parked_call_payload *parked_call_payload_from_failure(struct ast_channel *chan)
{
	RAII_VAR(struct ast_channel_snapshot *, parkee_snapshot, NULL, ao2_cleanup);

	ast_channel_lock(chan);
	parkee_snapshot = ast_channel_snapshot_create(chan);
	ast_channel_unlock(chan);
	if (!parkee_snapshot) {
		return NULL;
	}

	return ast_parked_call_payload_create(PARKED_CALL_FAILED, parkee_snapshot, NULL, NULL, NULL, 0, 0, 0);
}

static struct ast_parked_call_payload *parked_call_payload_from_parked_user(struct parked_user *pu, enum ast_parked_call_event_type event_type)
{
	RAII_VAR(struct ast_channel_snapshot *, parkee_snapshot, NULL, ao2_cleanup);
	long int timeout;
	long int duration;
	struct timeval now = ast_tvnow();
	const char *lot_name = pu->lot->name;

	ast_channel_lock(pu->chan);
	parkee_snapshot = ast_channel_snapshot_create(pu->chan);
	ast_channel_unlock(pu->chan);
	if (!parkee_snapshot) {
		return NULL;
	}

	timeout = pu->start.tv_sec + (long) pu->time_limit - now.tv_sec;
	duration = now.tv_sec - pu->start.tv_sec;

	return ast_parked_call_payload_create(event_type, parkee_snapshot, pu->parker_dial_string, pu->retriever, lot_name, pu->parking_space, timeout, duration);

}

/*! \brief Builds a manager string based on the contents of a parked call payload */
static struct ast_str *manager_build_parked_call_string(const struct ast_parked_call_payload *payload)
{
	struct ast_str *out = ast_str_create(1024);
	RAII_VAR(struct ast_str *, parkee_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, retriever_string, NULL, ast_free);

	if (!out) {
		return NULL;
	}

	parkee_string = ast_manager_build_channel_state_string_prefix(payload->parkee, "Parkee");
	if (!parkee_string) {
		ast_free(out);
		return NULL;
	}

	if (payload->retriever) {
		retriever_string = ast_manager_build_channel_state_string_prefix(payload->retriever, "Retriever");
		if (!retriever_string) {
			ast_free(out);
			return NULL;
		}
	}

	ast_str_set(&out, 0,
		"%s" /* parkee channel state */
		"%s" /* retriever channel state (when available) */
		"ParkerDialString: %s\r\n"
		"Parkinglot: %s\r\n"
		"ParkingSpace: %u\r\n"
		"ParkingTimeout: %lu\r\n"
		"ParkingDuration: %lu\r\n",

		ast_str_buffer(parkee_string),
		retriever_string ? ast_str_buffer(retriever_string) : "",
		payload->parker_dial_string,
		payload->parkinglot,
		payload->parkingspace,
		payload->timeout,
		payload->duration);

	return out;
}

static void manager_parking_status_single_lot(struct mansession *s, const struct message *m, const char *id_text, const char *lot_name)
{
	RAII_VAR(struct parking_lot *, curlot, NULL, ao2_cleanup);
	struct parked_user *curuser;
	struct ao2_iterator iter_users;
	int total = 0;

	curlot = parking_lot_find_by_name(lot_name);
	if (!curlot) {
		astman_send_error(s, m, "Requested parking lot could not be found.");
		return;
	}

	astman_send_listack(s, m, "Parked calls will follow", "start");

	iter_users = ao2_iterator_init(curlot->parked_users, 0);
	while ((curuser = ao2_iterator_next(&iter_users))) {
		RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
		RAII_VAR(struct ast_str *, parked_call_string, NULL, ast_free);

		payload = parked_call_payload_from_parked_user(curuser, PARKED_CALL);
		if (!payload) {
			ao2_ref(curuser, -1);
			break;
		}

		parked_call_string = manager_build_parked_call_string(payload);
		if (!parked_call_string) {
			ao2_ref(curuser, -1);
			break;
		}

		total++;

		astman_append(s, "Event: ParkedCall\r\n"
			"%s" /* The parked call string */
			"%s" /* The action ID */
			"\r\n",
			ast_str_buffer(parked_call_string),
			id_text);

		ao2_ref(curuser, -1);
	}
	ao2_iterator_destroy(&iter_users);

	astman_send_list_complete_start(s, m, "ParkedCallsComplete", total);
	astman_append(s, "Total: %d\r\n", total);
	astman_send_list_complete_end(s);
}

static void manager_parking_status_all_lots(struct mansession *s, const struct message *m, const char *id_text)
{
	struct parked_user *curuser;
	struct ao2_container *lot_container;
	struct ao2_iterator iter_lots;
	struct ao2_iterator iter_users;
	struct parking_lot *curlot;
	int total = 0;

	lot_container = get_parking_lot_container();
	if (!lot_container) {
		ast_log(LOG_ERROR, "Failed to obtain parking lot list. Action canceled.\n");
		astman_send_error(s, m, "Could not create parking lot list");
		return;
	}

	astman_send_listack(s, m, "Parked calls will follow", "start");

	iter_lots = ao2_iterator_init(lot_container, 0);
	while ((curlot = ao2_iterator_next(&iter_lots))) {
		iter_users = ao2_iterator_init(curlot->parked_users, 0);
		while ((curuser = ao2_iterator_next(&iter_users))) {
			RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
			RAII_VAR(struct ast_str *, parked_call_string, NULL, ast_free);

			payload = parked_call_payload_from_parked_user(curuser, PARKED_CALL);
			if (!payload) {
				ao2_ref(curuser, -1);
				ao2_iterator_destroy(&iter_users);
				ao2_ref(curlot, -1);
				goto abort_list;
			}

			parked_call_string = manager_build_parked_call_string(payload);
			if (!parked_call_string) {
				ao2_ref(curuser, -1);
				ao2_iterator_destroy(&iter_users);
				ao2_ref(curlot, -1);
				goto abort_list;
			}

			total++;

			astman_append(s, "Event: ParkedCall\r\n"
				"%s" /* The parked call string */
				"%s" /* The action ID */
				"\r\n",
				ast_str_buffer(parked_call_string),
				id_text);

			ao2_ref(curuser, -1);
		}
		ao2_iterator_destroy(&iter_users);
		ao2_ref(curlot, -1);
	}
abort_list:
	ao2_iterator_destroy(&iter_lots);

	astman_send_list_complete_start(s, m, "ParkedCallsComplete", total);
	astman_append(s, "Total: %d\r\n", total);
	astman_send_list_complete_end(s);
}

static int manager_parking_status(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *lot_name = astman_get_header(m, "ParkingLot");
	char id_text[256];

	id_text[0] = '\0';
	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	}

	if (!ast_strlen_zero(lot_name)) {
		manager_parking_status_single_lot(s, m, id_text, lot_name);
	} else {
		manager_parking_status_all_lots(s, m, id_text);
	}

	return 0;
}

struct park_list_data {
	const char *id_text;
	int count;
};

static int manager_append_event_parking_lot_data_cb(void *obj, void *arg, void *data, int flags)
{
	struct parking_lot *curlot = obj;
	struct mansession *s = arg;
	struct park_list_data *list_data = data;

	astman_append(s, "Event: Parkinglot\r\n"
		"%s" /* The Action ID */
		"Name: %s\r\n"
		"StartSpace: %d\r\n"
		"StopSpace: %d\r\n"
		"Timeout: %u\r\n"
		"\r\n",
		list_data->id_text,
		curlot->name,
		curlot->cfg->parking_start,
		curlot->cfg->parking_stop,
		curlot->cfg->parkingtime);
	++list_data->count;

	return 0;
}

static int manager_parking_lot_list(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	struct ao2_container *lot_container;
	char id_text[256];
	struct park_list_data list_data;

	id_text[0] = '\0';
	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	}

	lot_container = get_parking_lot_container();
	if (!lot_container) {
		ast_log(LOG_ERROR, "Failed to obtain parking lot list. Action canceled.\n");
		astman_send_error(s, m, "Could not create parking lot list");
		return 0;
	}

	astman_send_listack(s, m, "Parking lots will follow", "start");

	list_data.id_text = id_text;
	list_data.count = 0;
	ao2_callback_data(lot_container, OBJ_MULTIPLE | OBJ_NODATA,
		manager_append_event_parking_lot_data_cb, s, &list_data);

	astman_send_list_complete_start(s, m, "ParkinglotsComplete", list_data.count);
	astman_send_list_complete_end(s);

	return 0;
}

static void manager_park_unbridged(struct mansession *s, const struct message *m,
		struct ast_channel *chan, const char *parkinglot, int timeout_override)
{
	struct ast_bridge *parking_bridge = park_common_setup(chan,
		chan, parkinglot, NULL, 0, 0, timeout_override, 1);

	if (!parking_bridge) {
		astman_send_error(s, m, "Park action failed\n");
		return;
	}

	if (ast_bridge_add_channel(parking_bridge, chan, NULL, 0, NULL)) {
		astman_send_error(s, m, "Park action failed\n");
		ao2_cleanup(parking_bridge);
		return;
	}

	astman_send_ack(s, m, "Park successful\n");
	ao2_cleanup(parking_bridge);
}

static void manager_park_bridged(struct mansession *s, const struct message *m,
		struct ast_channel *chan, struct ast_channel *parker_chan,
		const char *parkinglot, int timeout_override)
{
	struct ast_bridge_channel *bridge_channel;
	char *app_data;

	if (timeout_override != -1) {
		if (ast_asprintf(&app_data, "%s,t(%d)", parkinglot, timeout_override) == -1) {
			astman_send_error(s, m, "Park action failed\n");
			return;
		}
	} else {
		if (ast_asprintf(&app_data, "%s", parkinglot) == -1) {
			astman_send_error(s, m, "Park action failed\n");
			return;
		}
	}

	ast_channel_lock(parker_chan);
	bridge_channel = ast_channel_get_bridge_channel(parker_chan);
	ast_channel_unlock(parker_chan);

	if (!bridge_channel) {
		ast_free(app_data);
		astman_send_error(s, m, "Park action failed\n");
		return;
	}

	/* Subscribe to park messages for the channel being parked */
	if (create_parked_subscription(parker_chan, ast_channel_uniqueid(chan), 1)) {
		ast_free(app_data);
		astman_send_error(s, m, "Park action failed\n");
		ao2_cleanup(bridge_channel);
		return;
	}

	ast_bridge_channel_write_park(bridge_channel, ast_channel_uniqueid(chan),
			ast_channel_uniqueid(parker_chan), app_data);

	ast_free(app_data);

	astman_send_ack(s, m, "Park successful\n");
	ao2_cleanup(bridge_channel);
}

static int manager_park(struct mansession *s, const struct message *m)
{
	const char *channel = astman_get_header(m, "Channel");
	const char *timeout_channel = S_OR(astman_get_header(m, "TimeoutChannel"), astman_get_header(m, "Channel2"));
	const char *announce_channel = astman_get_header(m, "AnnounceChannel");
	const char *timeout = astman_get_header(m, "Timeout");
	const char *parkinglot = astman_get_header(m, "Parkinglot");
	char buf[BUFSIZ];
	int timeout_override = -1;

	RAII_VAR(struct ast_channel *, parker_chan, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan, NULL, ao2_cleanup);

	if (ast_strlen_zero(channel)) {
		astman_send_error(s, m, "Channel not specified");
		return 0;
	}

	if (!ast_strlen_zero(timeout)) {
		if (sscanf(timeout, "%30d", &timeout_override) != 1 || timeout < 0) {
			astman_send_error(s, m, "Invalid Timeout value.");
			return 0;
		}

		if (timeout_override > 0) {
			/* If greater than zero, convert to seconds for internal use. Must be >= 1 second. */
			timeout_override = MAX(1, timeout_override / 1000);
		}
	}

	if (!(chan = ast_channel_get_by_name(channel))) {
		snprintf(buf, sizeof(buf), "Channel does not exist: %s", channel);
		astman_send_error(s, m, buf);
		return 0;
	}

	ast_channel_lock(chan);
	if (!ast_strlen_zero(timeout_channel)) {
		ast_bridge_set_transfer_variables(chan, timeout_channel, 0);
	}
	ast_channel_unlock(chan);

	parker_chan = ast_channel_bridge_peer(chan);
	if (!parker_chan || strcmp(ast_channel_name(parker_chan), timeout_channel)) {
		if (!ast_strlen_zero(announce_channel)) {
			struct ast_channel *announce_chan = ast_channel_get_by_name(announce_channel);
			if (!announce_channel) {
				astman_send_error(s, m, "AnnounceChannel does not exist");
				return 0;
			}

			create_parked_subscription(announce_chan, ast_channel_uniqueid(chan), 0);
			ast_channel_cleanup(announce_chan);
		}

		manager_park_unbridged(s, m, chan, parkinglot, timeout_override);
		return 0;
	}

	if (!ast_strlen_zero(announce_channel) && strcmp(announce_channel, timeout_channel)) {
		/* When using an announce_channel in bridge mode, only add the announce channel if it isn't
		 * the same as the timeout channel (which will play announcements anyway) */
		struct ast_channel *announce_chan = ast_channel_get_by_name(announce_channel);
		if (!announce_channel) {
			astman_send_error(s, m, "AnnounceChannel does not exist");
			return 0;
		}

		create_parked_subscription(announce_chan, ast_channel_uniqueid(chan), 0);
		ast_channel_cleanup(announce_chan);
	}

	manager_park_bridged(s, m, chan, parker_chan, parkinglot, timeout_override);
	return 0;
}

void publish_parked_call_failure(struct ast_channel *parkee)
{
	RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	if (!ast_parked_call_type()) {
		return;
	}

	payload = parked_call_payload_from_failure(parkee);
	if (!payload) {
		return;
	}

	msg = stasis_message_create(ast_parked_call_type(), payload);
	if (!msg) {
		return;
	}

	stasis_publish(ast_parking_topic(), msg);
}

void publish_parked_call(struct parked_user *pu, enum ast_parked_call_event_type event_type)
{
	RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	if (!ast_parked_call_type()) {
		return;
	}

	payload = parked_call_payload_from_parked_user(pu, event_type);
	if (!payload) {
		return;
	}

	msg = stasis_message_create(ast_parked_call_type(), payload);
	if (!msg) {
		return;
	}

	stasis_publish(ast_parking_topic(), msg);
}

static void parked_call_message_response(struct ast_parked_call_payload *parked_call)
{
	char *event_type = "";
	RAII_VAR(struct ast_str *, parked_call_string, NULL, ast_free);

	switch (parked_call->event_type) {
	case PARKED_CALL:
		event_type = "ParkedCall";
		break;
	case PARKED_CALL_TIMEOUT:
		event_type = "ParkedCallTimeOut";
		break;
	case PARKED_CALL_GIVEUP:
		event_type = "ParkedCallGiveUp";
		break;
	case PARKED_CALL_UNPARKED:
		event_type = "UnParkedCall";
		break;
	case PARKED_CALL_SWAP:
		event_type = "ParkedCallSwap";
		break;
	case PARKED_CALL_FAILED:
		/* PARKED_CALL_FAILED doesn't currently get a message and is used exclusively for bridging */
		return;
	}

	parked_call_string = manager_build_parked_call_string(parked_call);
	if (!parked_call_string) {
		ast_log(LOG_ERROR, "Failed to issue an AMI event of '%s' in response to a stasis message.\n", event_type);
		return;
	}

	manager_event(EVENT_FLAG_CALL, event_type,
			"%s",
			ast_str_buffer(parked_call_string)
		);
}

static void parking_event_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	if (stasis_message_type(message) == ast_parked_call_type()) {
		struct ast_parked_call_payload *parked_call_message = stasis_message_data(message);
		parked_call_message_response(parked_call_message);
	}
}

static void parking_manager_enable_stasis(void)
{
	if (!parking_sub) {
		parking_sub = stasis_subscribe(ast_parking_topic(), parking_event_cb, NULL);
	}
}

int load_parking_manager(void)
{
	int res;
	const struct ast_module_info *module = parking_get_module_info();

	res = ast_manager_register2("Parkinglots", EVENT_FLAG_CALL, manager_parking_lot_list, module->self, NULL, NULL);
	res |= ast_manager_register2("ParkedCalls", EVENT_FLAG_CALL, manager_parking_status, module->self, NULL, NULL);
	res |= ast_manager_register2("Park", EVENT_FLAG_CALL, manager_park, module->self, NULL, NULL);
	parking_manager_enable_stasis();
	return res ? -1 : 0;
}

static void parking_manager_disable_stasis(void)
{
	parking_sub = stasis_unsubscribe_and_join(parking_sub);
}

void unload_parking_manager(void)
{
	ast_manager_unregister("Parkinglots");
	ast_manager_unregister("ParkedCalls");
	ast_manager_unregister("Park");
	parking_manager_disable_stasis();
}
