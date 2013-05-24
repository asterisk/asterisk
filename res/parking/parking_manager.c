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
#include "asterisk/event.h"
#include "asterisk/utils.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/astobj2.h"
#include "asterisk/features.h"
#include "asterisk/manager.h"

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
	<managerEvent language="en_US" name="ParkedCall">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel is parked.</synopsis>
			<syntax>
				<parameter name="ChannelParkee">
				</parameter>
				<parameter name="ChannelStateParkee">
					<para>A numeric code for the channel's current state, related to ChannelStateDesc</para>
				</parameter>
				<parameter name="ChannelStateDescParkee">
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
				<parameter name="CallerIDNumParkee">
				</parameter>
				<parameter name="CallerIDNameParkee">
				</parameter>
				<parameter name="ConnectedLineNumParkee">
				</parameter>
				<parameter name="ConnectedLineNameParkee">
				</parameter>
				<parameter name="AccountCodeParkee">
				</parameter>
				<parameter name="ContextParkee">
				</parameter>
				<parameter name="ExtenParkee">
				</parameter>
				<parameter name="PriorityParkee">
				</parameter>
				<parameter name="UniqueidParkee">
				</parameter>
				<parameter name="ChannelParker">
				</parameter>
				<parameter name="ChannelStateParker">
				<para>A numeric code for the channel's current state, related to ChannelStateDesc</para>
				</parameter>
				<parameter name="ChannelStateDescParker">
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
				<parameter name="CallerIDNumParker">
				</parameter>
				<parameter name="CallerIDNameParker">
				</parameter>
				<parameter name="ConnectedLineNumParker">
				</parameter>
				<parameter name="ConnectedLineNameParker">
				</parameter>
				<parameter name="AccountCodeParker">
				</parameter>
				<parameter name="ContextParker">
				</parameter>
				<parameter name="ExtenParker">
				</parameter>
				<parameter name="PriorityParker">
				</parameter>
				<parameter name="UniqueidParker">
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
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCall']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ParkedCallGiveUp">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a parking lot because it hung up without being answered.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCall']/managerEventInstance/syntax/parameter)" />
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="UnParkedCall">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a channel leaves a parking lot because it was retrieved from the parking lot and reconnected.</synopsis>
			<syntax>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ParkedCall']/managerEventInstance/syntax/parameter)" />
				<parameter name="ChannelRetriever">
				</parameter>
				<parameter name="ChannelStateRetriever">
					<para>A numeric code for the channel's current state, related to ChannelStateDesc</para>
				</parameter>
				<parameter name="ChannelStateDescRetriever">
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
				<parameter name="CallerIDNumRetriever">
				</parameter>
				<parameter name="CallerIDNameRetriever">
				</parameter>
				<parameter name="ConnectedLineNumRetriever">
				</parameter>
				<parameter name="ConnectedLineNameRetriever">
				</parameter>
				<parameter name="AccountCodeRetriever">
				</parameter>
				<parameter name="ContextRetriever">
				</parameter>
				<parameter name="ExtenRetriever">
				</parameter>
				<parameter name="PriorityRetriever">
				</parameter>
				<parameter name="UniqueidRetriever">
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
 ***/

/*! \brief subscription to the parking lot topic */
static struct stasis_subscription *parking_sub;

static struct ast_parked_call_payload *parked_call_payload_from_failure(struct ast_channel *chan)
{
	RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, parkee_snapshot, NULL, ao2_cleanup);

	parkee_snapshot = ast_channel_snapshot_create(chan);
	if (!parkee_snapshot) {
		return NULL;
	}

	return ast_parked_call_payload_create(PARKED_CALL_FAILED, parkee_snapshot, NULL, NULL, NULL, 0, 0, 0);
}

static struct ast_parked_call_payload *parked_call_payload_from_parked_user(struct parked_user *pu, enum ast_parked_call_event_type event_type)
{
	RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel_snapshot *, parkee_snapshot, NULL, ao2_cleanup);
	long int timeout;
	long int duration;
	struct timeval now = ast_tvnow();
	const char *lot_name = pu->lot->name;

	if (!pu->parker) {
		return NULL;
	}

	parkee_snapshot = ast_channel_snapshot_create(pu->chan);

	if (!parkee_snapshot) {
		return NULL;
	}

	timeout = pu->start.tv_sec + (long) pu->time_limit - now.tv_sec;
	duration = now.tv_sec - pu->start.tv_sec;

	return ast_parked_call_payload_create(event_type, parkee_snapshot, pu->parker, pu->retriever, lot_name, pu->parking_space, timeout, duration);

}

/*! \brief Builds a manager string based on the contents of a parked call payload */
static struct ast_str *manager_build_parked_call_string(const struct ast_parked_call_payload *payload)
{
	struct ast_str *out = ast_str_create(1024);
	RAII_VAR(struct ast_str *, parkee_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, parker_string, NULL, ast_free);
	RAII_VAR(struct ast_str *, retriever_string, NULL, ast_free);

	if (!out) {
		return NULL;
	}

	parkee_string = ast_manager_build_channel_state_string_prefix(payload->parkee, "Parkee");

	if (payload->parker) {
		parker_string = ast_manager_build_channel_state_string_prefix(payload->parker, "Parker");
	}

	if (payload->retriever) {
		retriever_string = ast_manager_build_channel_state_string_prefix(payload->retriever, "Retriever");
	}

	ast_str_set(&out, 0,
		"%s" /* parkee channel state */
		"%s" /* parker channel state */
		"%s" /* retriever channel state (when available) */
		"Parkinglot: %s\r\n"
		"ParkingSpace: %u\r\n"
		"ParkingTimeout: %lu\r\n"
		"ParkingDuration: %lu\r\n",

		ast_str_buffer(parkee_string),
		parker_string ? ast_str_buffer(parker_string) : "",
		retriever_string ? ast_str_buffer(retriever_string) : "",
		payload->parkinglot,
		payload->parkingspace,
		payload->timeout,
		payload->duration);

	return out;
}

static int manager_parking_status_single_lot(struct mansession *s, const struct message *m, const char *id_text, const char *lot_name)
{
	RAII_VAR(struct parking_lot *, curlot, NULL, ao2_cleanup);
	struct parked_user *curuser;
	struct ao2_iterator iter_users;
	int total = 0;

	curlot = parking_lot_find_by_name(lot_name);

	if (!curlot) {
		astman_send_error(s, m, "Requested parking lot could not be found.");
		return RESULT_SUCCESS;
	}

	astman_send_ack(s, m, "Parked calls will follow");

	iter_users = ao2_iterator_init(curlot->parked_users, 0);
	while ((curuser = ao2_iterator_next(&iter_users))) {
		RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
		RAII_VAR(struct ast_str *, parked_call_string, NULL, ast_free);

		payload = parked_call_payload_from_parked_user(curuser, PARKED_CALL);
		if (!payload) {
			astman_send_error(s, m, "Failed to retrieve parking data about a parked user.");
			return RESULT_FAILURE;
		}

		parked_call_string = manager_build_parked_call_string(payload);
		if (!parked_call_string) {
			astman_send_error(s, m, "Failed to retrieve parkingd ata about a parked user.");
			return RESULT_FAILURE;
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

	astman_append(s,
		"Event: ParkedCallsComplete\r\n"
		"Total: %d\r\n"
		"%s"
		"\r\n",
		total, id_text);

	return RESULT_SUCCESS;
}

static int manager_parking_status_all_lots(struct mansession *s, const struct message *m, const char *id_text)
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
		return RESULT_SUCCESS;
	}

	iter_lots = ao2_iterator_init(lot_container, 0);

	astman_send_ack(s, m, "Parked calls will follow");

	while ((curlot = ao2_iterator_next(&iter_lots))) {
		iter_users = ao2_iterator_init(curlot->parked_users, 0);
		while ((curuser = ao2_iterator_next(&iter_users))) {
			RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
			RAII_VAR(struct ast_str *, parked_call_string, NULL, ast_free);

			payload = parked_call_payload_from_parked_user(curuser, PARKED_CALL);
			if (!payload) {
				return RESULT_FAILURE;
			}

			parked_call_string = manager_build_parked_call_string(payload);
			if (!payload) {
				return RESULT_FAILURE;
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

	ao2_iterator_destroy(&iter_lots);

	astman_append(s,
		"Event: ParkedCallsComplete\r\n"
		"Total: %d\r\n"
		"%s"
		"\r\n",
		total, id_text);

	return RESULT_SUCCESS;
}

static int manager_parking_status(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	const char *lot_name = astman_get_header(m, "ParkingLot");
	char id_text[256] = "";

	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	}

	if (!ast_strlen_zero(lot_name)) {
		return manager_parking_status_single_lot(s, m, id_text, lot_name);
	}

	return manager_parking_status_all_lots(s, m, id_text);

}

static int manager_append_event_parking_lot_data_cb(void *obj, void *arg, void *data, int flags)
{
	struct parking_lot *curlot = obj;
	struct mansession *s = arg;
	char *id_text = data;

	astman_append(s, "Event: Parkinglot\r\n"
		"Name: %s\r\n"
		"StartSpace: %d\r\n"
		"StopSpace: %d\r\n"
		"Timeout: %d\r\n"
		"%s" /* The Action ID */
		"\r\n",
		curlot->name,
		curlot->cfg->parking_start,
		curlot->cfg->parking_stop,
		curlot->cfg->parkingtime,
		id_text);

	return 0;
}

static int manager_parking_lot_list(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m, "ActionID");
	char id_text[256] = "";
	struct ao2_container *lot_container;

	if (!ast_strlen_zero(id)) {
		snprintf(id_text, sizeof(id_text), "ActionID: %s\r\n", id);
	}

	lot_container = get_parking_lot_container();

	if (!lot_container) {
		ast_log(LOG_ERROR, "Failed to obtain parking lot list. Action canceled.\n");
		astman_send_error(s, m, "Could not create parking lot list");
		return -1;
	}

	astman_send_ack(s, m, "Parking lots will follow");

	ao2_callback_data(lot_container, OBJ_MULTIPLE | OBJ_NODATA, manager_append_event_parking_lot_data_cb, s, id_text);

	astman_append(s,
		"Event: ParkinglotsComplete\r\n"
		"%s"
		"\r\n",id_text);

	return RESULT_SUCCESS;
}

void publish_parked_call_failure(struct ast_channel *parkee)
{
	RAII_VAR(struct ast_parked_call_payload *, payload, NULL, ao2_cleanup);
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

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

static void parking_event_cb(void *data, struct stasis_subscription *sub, struct stasis_topic *topic, struct stasis_message *message)
{
	if (stasis_message_type(message) == ast_parked_call_type()) {
		struct ast_parked_call_payload *parked_call_message = stasis_message_data(message);
		parked_call_message_response(parked_call_message);
	}
}

static void parking_manager_enable_stasis(void)
{
	ast_parking_stasis_init();
	if (!parking_sub) {
		parking_sub = stasis_subscribe(ast_parking_topic(), parking_event_cb, NULL);
	}
}

int load_parking_manager(void)
{
	int res;

	res = ast_manager_register_xml_core("Parkinglots", 0, manager_parking_lot_list);
	res |= ast_manager_register_xml_core("ParkedCalls", 0, manager_parking_status);
	/* TODO Add a 'Park' manager action */
	parking_manager_enable_stasis();
	return res ? -1 : 0;
}

static void parking_manager_disable_stasis(void)
{
	parking_sub = stasis_unsubscribe(parking_sub);
	ast_parking_stasis_disable();
}

void unload_parking_manager(void)
{
	ast_manager_unregister("Parkinglots");
	ast_manager_unregister("ParkedCalls");
	parking_manager_disable_stasis();
}
