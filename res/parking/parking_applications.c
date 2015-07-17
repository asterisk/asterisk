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
 * \brief Call Parking Applications
 *
 * \author Jonathan Rose <jrose@digium.com>
 */

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "res_parking.h"
#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/features.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/say.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/format_cache.h"

/*** DOCUMENTATION
	<application name="Park" language="en_US">
		<synopsis>
			Park yourself.
		</synopsis>
		<syntax>
			<parameter name="parking_lot_name">
				<para>Specify in which parking lot to park a call.</para>
				<para>The parking lot used is selected in the following order:</para>
				<para>1) parking_lot_name option to this application</para>
				<para>2) <variable>PARKINGLOT</variable> variable</para>
				<para>3) <literal>CHANNEL(parkinglot)</literal> function
				(Possibly preset by the channel driver.)</para>
				<para>4) Default parking lot.</para>
			</parameter>
			<parameter name="options">
				<para>A list of options for this parked call.</para>
				<optionlist>
					<option name="r">
						<para>Send ringing instead of MOH to the parked call.</para>
					</option>
					<option name="R">
						<para>Randomize the selection of a parking space.</para>
					</option>
					<option name="s">
						<para>Silence announcement of the parking space number.</para>
					</option>
					<option name="c" argsep=",">
						<argument name="context" required="false" />
						<argument name="extension" required="false" />
						<argument name="priority" required="true" />
						<para>If the parking times out, go to this place in the dialplan
							instead of where the parking lot defines the call should go.
						</para>
					</option>
					<option name="t">
						<argument name="duration" required="true" />
						<para>Use a timeout of <literal>duration</literal> seconds instead
							of the timeout specified by the parking lot.</para>
					</option>
				</optionlist>
			</parameter>
		</syntax>
		<description>
			<para>Used to park yourself (typically in combination with an attended
			transfer to know the parking space).</para>
			<para>If you set the <variable>PARKINGEXTEN</variable> variable to a
				parking space extension in the parking lot, Park() will attempt to park the
				call on that extension. If the extension is already in use then execution
				will continue at the next priority.
			</para>
		</description>
		<see-also>
			<ref type="application">ParkedCall</ref>
		</see-also>
	</application>

	<application name="ParkedCall" language="en_US">
		<synopsis>
			Retrieve a parked call.
		</synopsis>
		<syntax>
			<parameter name="parking_lot_name">
				<para>Specify from which parking lot to retrieve a parked call.</para>
				<para>The parking lot used is selected in the following order:</para>
				<para>1) parking_lot_name option</para>
				<para>2) <variable>PARKINGLOT</variable> variable</para>
				<para>3) <literal>CHANNEL(parkinglot)</literal> function
				(Possibly preset by the channel driver.)</para>
				<para>4) Default parking lot.</para>
			</parameter>
			<parameter name="parking_space">
				<para>Parking space to retrieve a parked call from.
				If not provided then the first available parked call in the
				parking lot will be retrieved.</para>
			</parameter>
		</syntax>
		<description>
			<para>Used to retrieve a parked call from a parking lot.</para>
			<note>
				<para>If a parking lot's parkext option is set, then Parking lots
				will automatically create and manage dialplan extensions in
				the parking lot context. If that is the case then you will not
				need to manage parking extensions yourself, just include the
				parking context of the parking lot.</para>
			</note>
		</description>
		<see-also>
			<ref type="application">Park</ref>
		</see-also>
	</application>

	<application name="ParkAndAnnounce" language="en_US">
		<synopsis>
			Park and Announce.
		</synopsis>
		<syntax>
			<parameter name="parking_lot_name">
				<para>Specify in which parking lot to park a call.</para>
				<para>The parking lot used is selected in the following order:</para>
				<para>1) parking_lot_name option to this application</para>
				<para>2) <variable>PARKINGLOT</variable> variable</para>
				<para>3) <literal>CHANNEL(parkinglot)</literal> function
				(Possibly preset by the channel driver.)</para>
				<para>4) Default parking lot.</para>
			</parameter>
			<parameter name="options">
				<para>A list of options for this parked call.</para>
				<optionlist>
					<option name="r">
						<para>Send ringing instead of MOH to the parked call.</para>
					</option>
					<option name="R">
						<para>Randomize the selection of a parking space.</para>
					</option>
					<option name="c" argsep=",">
						<argument name="context" required="false" />
						<argument name="extension" required="false" />
						<argument name="priority" required="true" />
						<para>If the parking times out, go to this place in the dialplan
							instead of where the parking lot defines the call should go.
						</para>
					</option>
					<option name="t">
						<argument name="duration" required="true" />
						<para>Use a timeout of <literal>duration</literal> seconds instead
							of the timeout specified by the parking lot.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="announce_template" required="true" argsep=":">
				<argument name="announce" required="true">
					<para>Colon-separated list of files to announce. The word
					<literal>PARKED</literal> will be replaced by a say_digits of the extension in which
					the call is parked.</para>
				</argument>
				<argument name="announce1" multiple="true" />
			</parameter>
			<parameter name="dial" required="true">
				<para>The app_dial style resource to call to make the
				announcement. Console/dsp calls the console.</para>
			</parameter>
		</syntax>
		<description>
			<para>Park a call into the parkinglot and announce the call to another channel.</para>
			<para>The variable <variable>PARKEDAT</variable> will contain the parking extension
			into which the call was placed.  Use with the Local channel to allow the dialplan to make
			use of this information.</para>
		</description>
		<see-also>
			<ref type="application">Park</ref>
			<ref type="application">ParkedCall</ref>
		</see-also>
	</application>
 ***/

#define PARK_AND_ANNOUNCE_APPLICATION "ParkAndAnnounce"

/* Park a call */

enum park_args {
	OPT_ARG_COMEBACK,
	OPT_ARG_TIMEOUT,
	OPT_ARG_ARRAY_SIZE /* Always the last element of the enum */
};

enum park_flags {
	MUXFLAG_RINGING = (1 << 0),
	MUXFLAG_RANDOMIZE = (1 << 1),
	MUXFLAG_NOANNOUNCE = (1 << 2),
	MUXFLAG_COMEBACK_OVERRIDE = (1 << 3),
	MUXFLAG_TIMEOUT_OVERRIDE = (1 << 4),
};

AST_APP_OPTIONS(park_opts, {
	AST_APP_OPTION('r', MUXFLAG_RINGING),
	AST_APP_OPTION('R', MUXFLAG_RANDOMIZE),
	AST_APP_OPTION('s', MUXFLAG_NOANNOUNCE),
	AST_APP_OPTION_ARG('c', MUXFLAG_COMEBACK_OVERRIDE, OPT_ARG_COMEBACK),
	AST_APP_OPTION_ARG('t', MUXFLAG_TIMEOUT_OVERRIDE, OPT_ARG_TIMEOUT),
});

static int apply_option_timeout (int *var, char *timeout_arg)
{
	if (ast_strlen_zero(timeout_arg)) {
		ast_log(LOG_ERROR, "No duration value provided for the timeout ('t') option.\n");
		return -1;
	}

	if (sscanf(timeout_arg, "%d", var) != 1 || *var < 0) {
		ast_log(LOG_ERROR, "Duration value provided for timeout ('t') option must be 0 or greater.\n");
		return -1;
	}

	return 0;
}

static int park_app_parse_data(const char *data, int *disable_announce, int *use_ringing, int *randomize, int *time_limit, char **comeback_override, char **lot_name)
{
	char *parse;
	struct ast_flags flags = { 0 };

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(lot_name);
		AST_APP_ARG(options);
		AST_APP_ARG(other);	/* Any remaining unused arguments */
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (args.options) {
		char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };
		ast_app_parse_options(park_opts, &flags, opts, args.options);
		if (ast_test_flag(&flags, MUXFLAG_TIMEOUT_OVERRIDE)) {
			if (apply_option_timeout(time_limit, opts[OPT_ARG_TIMEOUT])) {
				return -1;
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_COMEBACK_OVERRIDE)) {
			*comeback_override = ast_strdup(opts[OPT_ARG_COMEBACK]);
		}

		if (ast_test_flag(&flags, MUXFLAG_NOANNOUNCE)) {
			if (disable_announce) {
				*disable_announce = 1;
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_RINGING)) {
			*use_ringing = 1;
		}

		if (ast_test_flag(&flags, MUXFLAG_RANDOMIZE)) {
			*randomize = 1;
		}
	}

	if (!ast_strlen_zero(args.lot_name)) {
		*lot_name = ast_strdup(args.lot_name);
	}

	return 0;
}

void park_common_datastore_free(struct park_common_datastore *datastore)
{
	if (!datastore) {
		return;
	}

	ast_free(datastore->parker_uuid);
	ast_free(datastore->parker_dial_string);
	ast_free(datastore->comeback_override);
	ast_free(datastore);
}

static void park_common_datastore_destroy(void *data)
{
	struct park_common_datastore *datastore = data;
	park_common_datastore_free(datastore);
}

static const struct ast_datastore_info park_common_info = {
	.type = "park entry data",
	.destroy = park_common_datastore_destroy,
};

static void wipe_park_common_datastore(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &park_common_info, NULL);
	if (datastore) {
		ast_channel_datastore_remove(chan, datastore);
		ast_datastore_free(datastore);
	}
	ast_channel_unlock(chan);
}

static int setup_park_common_datastore(struct ast_channel *parkee, const char *parker_uuid, const char *comeback_override, int randomize, int time_limit, int silence_announce)
{
	struct ast_datastore *datastore = NULL;
	struct park_common_datastore *park_datastore;
	const char *attended_transfer;
	const char *blind_transfer;
	char *parker_dial_string = NULL;

	wipe_park_common_datastore(parkee);

	if (!(datastore = ast_datastore_alloc(&park_common_info, NULL))) {
		return -1;
	}

	if (!(park_datastore = ast_calloc(1, sizeof(*park_datastore)))) {
		ast_datastore_free(datastore);
		return -1;
	}

	if (parker_uuid) {
		park_datastore->parker_uuid = ast_strdup(parker_uuid);
	}

	ast_channel_lock(parkee);

	attended_transfer = pbx_builtin_getvar_helper(parkee, "ATTENDEDTRANSFER");
	blind_transfer = pbx_builtin_getvar_helper(parkee, "BLINDTRANSFER");

	if (!ast_strlen_zero(attended_transfer)) {
		parker_dial_string = ast_strdupa(attended_transfer);
	} else if (!ast_strlen_zero(blind_transfer)) {
		parker_dial_string = ast_strdupa(blind_transfer);
		/* Ensure that attended_transfer is NULL and not an empty string. */
		attended_transfer = NULL;
	}

	ast_channel_unlock(parkee);

	if (!ast_strlen_zero(parker_dial_string)) {
		ast_channel_name_to_dial_string(parker_dial_string);
		ast_verb(4, "Setting Parker dial string to %s from %s value\n",
			parker_dial_string,
			attended_transfer ? "ATTENDEDTRANSFER" : "BLINDTRANSFER");
		park_datastore->parker_dial_string = ast_strdup(parker_dial_string);
	}

	park_datastore->randomize = randomize;
	park_datastore->time_limit = time_limit;
	park_datastore->silence_announce = silence_announce;

	if (comeback_override) {
		park_datastore->comeback_override = ast_strdup(comeback_override);
	}


	datastore->data = park_datastore;
	ast_channel_lock(parkee);
	ast_channel_datastore_add(parkee, datastore);
	ast_channel_unlock(parkee);

	return 0;
}

struct park_common_datastore *get_park_common_datastore_copy(struct ast_channel *parkee)
{
	struct ast_datastore *datastore;
	struct park_common_datastore *data;
	struct park_common_datastore *data_copy;

	SCOPED_CHANNELLOCK(lock, parkee);
	if (!(datastore = ast_channel_datastore_find(parkee, &park_common_info, NULL))) {
		return NULL;
	}

	data = datastore->data;

	if (!data) {
		/* This data should always be populated if this datastore was appended to the channel */
		ast_assert(0);
	}

	data_copy = ast_calloc(1, sizeof(*data_copy));
	if (!data_copy) {
		return NULL;
	}

	if (!(data_copy->parker_uuid = ast_strdup(data->parker_uuid))) {
		park_common_datastore_free(data_copy);
		return NULL;
	}

	data_copy->randomize = data->randomize;
	data_copy->time_limit = data->time_limit;
	data_copy->silence_announce = data->silence_announce;

	if (data->comeback_override) {
		data_copy->comeback_override = ast_strdup(data->comeback_override);
		if (!data_copy->comeback_override) {
			park_common_datastore_free(data_copy);
			return NULL;
		}
	}

	if (data->parker_dial_string) {
		data_copy->parker_dial_string = ast_strdup(data->parker_dial_string);
		if (!data_copy->parker_dial_string) {
			park_common_datastore_free(data_copy);
			return NULL;
		}
	}

	return data_copy;
}

struct ast_bridge *park_common_setup(struct ast_channel *parkee, struct ast_channel *parker,
		const char *lot_name, const char *comeback_override,
		int use_ringing, int randomize, int time_limit, int silence_announcements)
{
	struct ast_bridge *parking_bridge;
	RAII_VAR(struct parking_lot *, lot, NULL, ao2_cleanup);

	if (!parker) {
		parker = parkee;
	}

	/* If the name of the parking lot isn't specified in the arguments, find it based on the channel. */
	if (ast_strlen_zero(lot_name)) {
		ast_channel_lock(parker);
		lot_name = ast_strdupa(find_channel_parking_lot_name(parker));
		ast_channel_unlock(parker);
	}

	lot = parking_lot_find_by_name(lot_name);
	if (!lot) {
		lot = parking_create_dynamic_lot(lot_name, parkee);
	}

	if (!lot) {
		ast_log(LOG_ERROR, "Could not find parking lot: '%s'\n", lot_name);
		return NULL;
	}

	ao2_lock(lot);
	parking_bridge = parking_lot_get_bridge(lot);
	ao2_unlock(lot);

	if (!parking_bridge) {
		return NULL;
	}

	/* Apply relevant bridge roles and such to the parking channel */
	parking_channel_set_roles(parkee, lot, use_ringing);
	setup_park_common_datastore(parkee, ast_channel_uniqueid(parker), comeback_override, randomize, time_limit,
		silence_announcements);
	return parking_bridge;
}

struct ast_bridge *park_application_setup(struct ast_channel *parkee, struct ast_channel *parker, const char *app_data,
		int *silence_announcements)
{
	int use_ringing = 0;
	int randomize = 0;
	int time_limit = -1;

	RAII_VAR(char *, comeback_override, NULL, ast_free);
	RAII_VAR(char *, lot_name_app_arg, NULL, ast_free);

	if (app_data) {
		park_app_parse_data(app_data, silence_announcements, &use_ringing, &randomize, &time_limit, &comeback_override, &lot_name_app_arg);
	}

	return park_common_setup(parkee, parker, lot_name_app_arg, comeback_override, use_ringing,
		randomize, time_limit, silence_announcements ? *silence_announcements : 0);

}

static int park_app_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct ast_bridge *, parking_bridge, NULL, ao2_cleanup);

	struct ast_bridge_features chan_features;
	int res;
	int silence_announcements = 0;
	const char *transferer;

	/* Answer the channel if needed */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}

	ast_channel_lock(chan);
	if (!(transferer = pbx_builtin_getvar_helper(chan, "ATTENDEDTRANSFER"))) {
		transferer = pbx_builtin_getvar_helper(chan, "BLINDTRANSFER");
	}
	transferer = ast_strdupa(S_OR(transferer, ""));
	ast_channel_unlock(chan);

	/* Handle the common parking setup stuff */
	if (!(parking_bridge = park_application_setup(chan, NULL, data, &silence_announcements))) {
		if (!silence_announcements && !transferer) {
			ast_stream_and_wait(chan, "pbx-parkingfailed", "");
		}
		publish_parked_call_failure(chan);
		return 0;
	}

	/* Initialize bridge features for the channel. */
	res = ast_bridge_features_init(&chan_features);
	if (res) {
		ast_bridge_features_cleanup(&chan_features);
		publish_parked_call_failure(chan);
		return -1;
	}

	/* Now for the fun part... park it! */
	ast_bridge_join(parking_bridge, chan, NULL, &chan_features, NULL, 0);

	/*
	 * If the bridge was broken for a hangup that isn't real, then
	 * don't run the h extension, because the channel isn't really
	 * hung up.  This should only happen with AST_SOFTHANGUP_ASYNCGOTO.
	 */
	res = -1;

	ast_channel_lock(chan);
	if (ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO) {
		res = 0;
	}
	ast_channel_unlock(chan);

	ast_bridge_features_cleanup(&chan_features);

	return res;
}

/* Retrieve a parked call */

static int parked_call_app_exec(struct ast_channel *chan, const char *data)
{
	RAII_VAR(struct parking_lot *, lot, NULL, ao2_cleanup);
	RAII_VAR(struct parked_user *, pu, NULL, ao2_cleanup); /* Parked user being retrieved */
	struct ast_bridge *retrieval_bridge;
	int res;
	int target_space = -1;
	struct ast_bridge_features chan_features;
	char *parse;
	char *lot_name;

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(lot_name);
		AST_APP_ARG(parking_space);
		AST_APP_ARG(other);	/* Any remaining unused arguments */
	);

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	/* Answer the channel if needed */
	if (ast_channel_state(chan) != AST_STATE_UP) {
		ast_answer(chan);
	}

	lot_name = args.lot_name;

	/* If the name of the parking lot isn't in the arguments, find it based on the channel. */
	if (ast_strlen_zero(lot_name)) {
		ast_channel_lock(chan);
		lot_name = ast_strdupa(find_channel_parking_lot_name(chan));
		ast_channel_unlock(chan);
	}

	lot = parking_lot_find_by_name(lot_name);

	if (!lot) {
		ast_log(LOG_ERROR, "Could not find the requested parking lot\n");
		ast_stream_and_wait(chan, "pbx-invalidpark", "");
		return -1;
	}

	if (!ast_strlen_zero(args.parking_space)) {
		if (sscanf(args.parking_space, "%d", &target_space) != 1 || target_space < 0) {
			ast_stream_and_wait(chan, "pbx-invalidpark", "");
			ast_log(LOG_ERROR, "value '%s' for parking_space argument is invalid. Must be an integer greater than 0.\n", args.parking_space);
			return -1;
		}
	}

	/* Attempt to get the parked user from the parking lot */
	pu = parking_lot_retrieve_parked_user(lot, target_space);
	if (!pu) {
		ast_stream_and_wait(chan, "pbx-invalidpark", "");
		return -1;
	}

	/* The parked call needs to know who is retrieving it before we move it out of the parking bridge */
	ast_assert(pu->retriever == NULL);
	pu->retriever = ast_channel_snapshot_create(chan);

	/* Create bridge */
	retrieval_bridge = ast_bridge_basic_new();
	if (!retrieval_bridge) {
		return -1;
	}

	/* Move the parkee into the new bridge */
	if (ast_bridge_move(retrieval_bridge, lot->parking_bridge, pu->chan, NULL, 0)) {
		ast_bridge_destroy(retrieval_bridge, 0);
		return -1;
	}

	/* Initialize our bridge features */
	res = ast_bridge_features_init(&chan_features);
	if (res) {
		ast_bridge_destroy(retrieval_bridge, 0);
		ast_bridge_features_cleanup(&chan_features);
		return -1;
	}

	/* Set the features */
	parked_call_retrieve_enable_features(chan, lot, AST_FEATURE_FLAG_BYCALLER);

	/* If the parkedplay option is set for the caller to hear, play that tone now. */
	if (lot->cfg->parkedplay & AST_FEATURE_FLAG_BYCALLER) {
		ast_stream_and_wait(chan, lot->cfg->courtesytone, NULL);
	}

	/* Now we should try to join the new bridge ourselves... */
	ast_bridge_join(retrieval_bridge, chan, NULL, &chan_features, NULL,
		AST_BRIDGE_JOIN_PASS_REFERENCE);

	ast_bridge_features_cleanup(&chan_features);

	/* Return -1 so that call does not continue in the dialplan. This is to make
	 * behavior consistent with Asterisk versions prior to 12.
	 */
	return -1;
}

struct park_announce_subscription_data {
	char *parkee_uuid;
	char *dial_string;
	char *announce_string;
};

static void park_announce_subscription_data_destroy(void *data)
{
	struct park_announce_subscription_data *pa_data = data;
	ast_free(pa_data->parkee_uuid);
	ast_free(pa_data->dial_string);
	ast_free(pa_data->announce_string);
	ast_free(pa_data);
}

static struct park_announce_subscription_data *park_announce_subscription_data_create(const char *parkee_uuid,
		const char *dial_string,
		const char *announce_string)
{
	struct park_announce_subscription_data *pa_data;

	if (!(pa_data = ast_calloc(1, sizeof(*pa_data)))) {
		return NULL;
	}

	if (!(pa_data->parkee_uuid = ast_strdup(parkee_uuid))
		|| !(pa_data->dial_string = ast_strdup(dial_string))
		|| !(pa_data->announce_string = ast_strdup(announce_string))) {
		park_announce_subscription_data_destroy(pa_data);
		return NULL;
	}

	return pa_data;
}

static void announce_to_dial(char *dial_string, char *announce_string, int parkingspace, struct ast_channel_snapshot *parkee_snapshot)
{
	struct ast_channel *dchan;
	struct outgoing_helper oh = { 0, };
	int outstate;
	struct ast_format_cap *cap_slin = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	char buf[13];
	char *dial_tech;
	char *cur_announce;

	dial_tech = strsep(&dial_string, "/");
	ast_verb(3, "Dial Tech,String: (%s,%s)\n", dial_tech, dial_string);

	if (!cap_slin) {
		ast_log(LOG_WARNING, "PARK: Failed to announce park.\n");
		goto announce_cleanup;
	}
	ast_format_cap_append(cap_slin, ast_format_slin, 0);

	snprintf(buf, sizeof(buf), "%d", parkingspace);
	oh.vars = ast_variable_new("_PARKEDAT", buf, "");
	dchan = __ast_request_and_dial(dial_tech, cap_slin, NULL, NULL, dial_string, 30000,
		&outstate,
		parkee_snapshot->caller_number,
		parkee_snapshot->caller_name,
		&oh);

	ast_variables_destroy(oh.vars);
	if (!dchan) {
		ast_log(LOG_WARNING, "PARK: Unable to allocate announce channel.\n");
		goto announce_cleanup;
	}

	ast_verb(4, "Announce Template: %s\n", announce_string);

	for (cur_announce = strsep(&announce_string, ":"); cur_announce; cur_announce = strsep(&announce_string, ":")) {
		ast_verb(4, "Announce:%s\n", cur_announce);
		if (!strcmp(cur_announce, "PARKED")) {
			ast_say_digits(dchan, parkingspace, "", ast_channel_language(dchan));
		} else {
			int dres = ast_streamfile(dchan, cur_announce, ast_channel_language(dchan));
			if (!dres) {
				dres = ast_waitstream(dchan, "");
			} else {
				ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", cur_announce, ast_channel_name(dchan));
			}
		}
	}

	ast_stopstream(dchan);
	ast_hangup(dchan);

announce_cleanup:
	ao2_cleanup(cap_slin);
}

static void park_announce_update_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message)
{
	struct park_announce_subscription_data *pa_data = data;
	char *dial_string = pa_data->dial_string;

	struct ast_parked_call_payload *payload = stasis_message_data(message);

	if (stasis_subscription_final_message(sub, message)) {
		park_announce_subscription_data_destroy(data);
		return;
	}

	if (payload->event_type != PARKED_CALL) {
		/* We are only concerned with calls parked */
		return;
	}

	if (strcmp(payload->parkee->uniqueid, pa_data->parkee_uuid)) {
		/* We are only concerned with the parkee we are subscribed for. */
		return;
	}

	if (!ast_strlen_zero(dial_string)) {
		announce_to_dial(dial_string, pa_data->announce_string, payload->parkingspace, payload->parkee);
	}

	*dial_string = '\0'; /* If we observe this dial string on a second pass, we don't want to do anything with it. */
}

static int park_and_announce_app_exec(struct ast_channel *chan, const char *data)
{
	struct ast_bridge_features chan_features;
	char *parse;
	int res;
	int silence_announcements = 1;

	struct stasis_subscription *parking_subscription;
	struct park_announce_subscription_data *pa_data;

	RAII_VAR(struct ast_bridge *, parking_bridge, NULL, ao2_cleanup);

	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(lot_name);
		AST_APP_ARG(options);
		AST_APP_ARG(announce_template);
		AST_APP_ARG(dial);
		AST_APP_ARG(others);/* Any remaining unused arguments */
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "ParkAndAnnounce has required arguments. No arguments were provided.\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.announce_template)) {
		/* improperly configured arguments for the application */
		ast_log(LOG_ERROR, "ParkAndAnnounce requires the announce_template argument.\n");
		return -1;
	}

	if (ast_strlen_zero(args.dial)) {
		/* improperly configured arguments */
		ast_log(LOG_ERROR, "ParkAndAnnounce requires the dial argument.\n");
		return -1;
	}

	if (!strchr(args.dial, '/')) {
		ast_log(LOG_ERROR, "ParkAndAnnounce dial string '%s' is improperly formed.\n", args.dial);
		return -1;
	}

	/* Handle the common parking setup stuff */
	if (!(parking_bridge = park_application_setup(chan, NULL, data, &silence_announcements))) {
		return 0;
	}

	/* Initialize bridge features for the channel. */
	res = ast_bridge_features_init(&chan_features);
	if (res) {
		ast_bridge_features_cleanup(&chan_features);
		return -1;
	}

	/* subscribe to the parking message so that we can announce once it is parked */
	pa_data = park_announce_subscription_data_create(ast_channel_uniqueid(chan), args.dial, args.announce_template);
	if (!pa_data) {
		return -1;
	}

	if (!(parking_subscription = stasis_subscribe_pool(ast_parking_topic(), park_announce_update_cb, pa_data))) {
		/* Failed to create subscription */
		park_announce_subscription_data_destroy(pa_data);
		return -1;
	}

	/* Now for the fun part... park it! */
	ast_bridge_join(parking_bridge, chan, NULL, &chan_features, NULL, 0);

	/* Toss the subscription since we aren't bridged at this point. */
	stasis_unsubscribe(parking_subscription);

	/*
	 * If the bridge was broken for a hangup that isn't real, then
	 * don't run the h extension, because the channel isn't really
	 * hung up.  This should only happen with AST_SOFTHANGUP_ASYNCGOTO.
	 */
	res = -1;

	ast_channel_lock(chan);
	if (ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO) {
		res = 0;
	}
	ast_channel_unlock(chan);

	ast_bridge_features_cleanup(&chan_features);

	return res;
}

int load_parking_applications(void)
{
	if (ast_register_application_xml(PARK_APPLICATION, park_app_exec)) {
		return -1;
	}

	if (ast_register_application_xml(PARKED_CALL_APPLICATION, parked_call_app_exec)) {
		return -1;
	}

	if (ast_register_application_xml(PARK_AND_ANNOUNCE_APPLICATION, park_and_announce_app_exec)) {
		return -1;
	}

	return 0;
}

void unload_parking_applications(void)
{
	ast_unregister_application(PARK_APPLICATION);
	ast_unregister_application(PARKED_CALL_APPLICATION);
	ast_unregister_application(PARK_AND_ANNOUNCE_APPLICATION);
}
