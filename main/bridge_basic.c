/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013 Digium, Inc.
 *
 * Richard Mudgett <rmudgett@digium.com>
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

/*!
 * \file
 * \brief Basic bridge class.  It is a subclass of struct ast_bridge.
 *
 * \author Richard Mudgett <rmudgett@digium.com>
 *
 * See Also:
 * \arg \ref AstCREDITS
 */


#include "asterisk.h"

ASTERISK_REGISTER_FILE()

#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/linkedlists.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_internal.h"
#include "asterisk/bridge_basic.h"
#include "asterisk/bridge_after.h"
#include "asterisk/astobj2.h"
#include "asterisk/features_config.h"
#include "asterisk/pbx.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/dial.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/features.h"
#include "asterisk/format_cache.h"
#include "asterisk/test.h"

#define NORMAL_FLAGS	(AST_BRIDGE_FLAG_DISSOLVE_HANGUP | AST_BRIDGE_FLAG_DISSOLVE_EMPTY \
			| AST_BRIDGE_FLAG_SMART)

#define TRANSFER_FLAGS AST_BRIDGE_FLAG_SMART

struct attended_transfer_properties;

enum bridge_basic_personality_type {
	/*! Index for "normal" basic bridge personality */
	BRIDGE_BASIC_PERSONALITY_NORMAL,
	/*! Index for attended transfer basic bridge personality */
	BRIDGE_BASIC_PERSONALITY_ATXFER,
	/*! Indicates end of enum. Must always remain the last element */
	BRIDGE_BASIC_PERSONALITY_END,
};

/*!
 * \brief Change basic bridge personality
 *
 * Changing personalities allows for the bridge to remain in use but have
 * properties such as its v_table and its flags change.
 *
 * \param bridge The bridge
 * \param type The personality to change the bridge to
 * \user_data Private data to attach to the personality.
 */
static void bridge_basic_change_personality(struct ast_bridge *bridge,
		enum bridge_basic_personality_type type, void *user_data);

/* ------------------------------------------------------------------- */

static const struct ast_datastore_info dtmf_features_info = {
	.type = "bridge-dtmf-features",
	.destroy = ast_free_ptr,
};

/*!
 * \internal
 * \since 12.0.0
 * \brief read a feature code character and set it on for the give feature_flags struct
 *
 * \param feature_flags flags being modifed
 * \param feature feature code provided - should be an uppercase letter
 *
 * \retval 0 if the feature was set successfully
 * \retval -1 failure because the requested feature code isn't handled by this function
 */
static int set_feature_flag_from_char(struct ast_flags *feature_flags, char feature)
{
	switch (feature) {
	case 'T':
		ast_set_flag(feature_flags, AST_FEATURE_REDIRECT);
		return 0;
	case 'K':
		ast_set_flag(feature_flags, AST_FEATURE_PARKCALL);
		return 0;
	case 'H':
		ast_set_flag(feature_flags, AST_FEATURE_DISCONNECT);
		return 0;
	case 'W':
		ast_set_flag(feature_flags, AST_FEATURE_AUTOMON);
		return 0;
	case 'X':
		ast_set_flag(feature_flags, AST_FEATURE_AUTOMIXMON);
		return 0;
	default:
		return -1;
	}
}

/*!
 * \internal
 * \since 12.0.0
 * \brief Write a features string to a string buffer based on the feature flags provided
 *
 * \param feature_flags pointer to the feature flags to write from.
 * \param buffer pointer to a string buffer to write the features
 * \param buffer_size size of the buffer provided (should be able to fit all feature codes)
 *
 * \retval 0 on successful write
 * \retval -1 failure due to running out of buffer space
 */
static int dtmf_features_flags_to_string(struct ast_flags *feature_flags, char *buffer, size_t buffer_size)
{
	size_t buffer_expended = 0;
	unsigned int cur_feature;
	static const struct {
		char letter;
		unsigned int flag;
	} associations[] = {
		{ 'T', AST_FEATURE_REDIRECT },
		{ 'K', AST_FEATURE_PARKCALL },
		{ 'H', AST_FEATURE_DISCONNECT },
		{ 'W', AST_FEATURE_AUTOMON },
		{ 'X', AST_FEATURE_AUTOMIXMON },
	};

	for (cur_feature = 0; cur_feature < ARRAY_LEN(associations); cur_feature++) {
		if (ast_test_flag(feature_flags, associations[cur_feature].flag)) {
			if (buffer_expended == buffer_size - 1) {
				buffer[buffer_expended] = '\0';
				return -1;
			}
			buffer[buffer_expended++] = associations[cur_feature].letter;
		}
	}

	buffer[buffer_expended] = '\0';
	return 0;
}

static int build_dtmf_features(struct ast_flags *flags, const char *features)
{
	const char *feature;

	char missing_features[strlen(features) + 1];
	size_t number_of_missing_features = 0;

	for (feature = features; *feature; feature++) {
		if (!isupper(*feature)) {
			ast_log(LOG_ERROR, "Features string '%s' rejected because it contains non-uppercase feature.\n", features);
			return -1;
		}

		if (set_feature_flag_from_char(flags, *feature)) {
			missing_features[number_of_missing_features++] = *feature;
		}
	}

	missing_features[number_of_missing_features] = '\0';

	if (number_of_missing_features) {
		ast_log(LOG_WARNING, "Features '%s' from features string '%s' can not be applied.\n", missing_features, features);
	}

	return 0;
}

int ast_bridge_features_ds_set_string(struct ast_channel *chan, const char *features)
{
	struct ast_flags flags = {0};

	if (build_dtmf_features(&flags, features)) {
		return -1;
	}

	ast_channel_lock(chan);
	if (ast_bridge_features_ds_set(chan, &flags)) {
		ast_channel_unlock(chan);
		ast_log(LOG_ERROR, "Failed to apply features datastore for '%s' to channel '%s'\n", features, ast_channel_name(chan));
		return -1;
	}
	ast_channel_unlock(chan);

	return 0;
}

int ast_bridge_features_ds_get_string(struct ast_channel *chan, char *buffer, size_t buf_size)
{
	struct ast_flags *channel_flags;
	struct ast_flags held_copy;

	ast_channel_lock(chan);
	if (!(channel_flags = ast_bridge_features_ds_get(chan))) {
		ast_channel_unlock(chan);
		return -1;
	}
	held_copy = *channel_flags;
	ast_channel_unlock(chan);

	return dtmf_features_flags_to_string(&held_copy, buffer, buf_size);
}

static int bridge_features_ds_set_full(struct ast_channel *chan, struct ast_flags *flags, int replace)
{
	struct ast_datastore *datastore;
	struct ast_flags *ds_flags;

	datastore = ast_channel_datastore_find(chan, &dtmf_features_info, NULL);
	if (datastore) {
		ds_flags = datastore->data;
		if (replace) {
			*ds_flags = *flags;
		} else {
			flags->flags = flags->flags | ds_flags->flags;
			*ds_flags = *flags;
		}
		return 0;
	}

	datastore = ast_datastore_alloc(&dtmf_features_info, NULL);
	if (!datastore) {
		return -1;
	}

	ds_flags = ast_malloc(sizeof(*ds_flags));
	if (!ds_flags) {
		ast_datastore_free(datastore);
		return -1;
	}

	*ds_flags = *flags;
	datastore->data = ds_flags;
	ast_channel_datastore_add(chan, datastore);
	return 0;
}

int ast_bridge_features_ds_set(struct ast_channel *chan, struct ast_flags *flags)
{
	return bridge_features_ds_set_full(chan, flags, 1);
}

int ast_bridge_features_ds_append(struct ast_channel *chan, struct ast_flags *flags)
{
	return bridge_features_ds_set_full(chan, flags, 0);
}

struct ast_flags *ast_bridge_features_ds_get(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = ast_channel_datastore_find(chan, &dtmf_features_info, NULL);
	if (!datastore) {
		return NULL;
	}
	return datastore->data;
}

/*!
 * \internal
 * \brief Determine if we should dissolve the bridge from a hangup.
 * \since 12.0.0
 *
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
static int basic_hangup_hook(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	int bridge_count = 0;
	struct ast_bridge_channel *iter;

	ast_bridge_channel_lock_bridge(bridge_channel);
	AST_LIST_TRAVERSE(&bridge_channel->bridge->channels, iter, entry) {
		if (iter != bridge_channel && iter->state == BRIDGE_CHANNEL_STATE_WAIT) {
			++bridge_count;
		}
	}
	if (2 <= bridge_count) {
		/* Just allow this channel to leave the multi-party bridge. */
		ast_bridge_channel_leave_bridge(bridge_channel,
			BRIDGE_CHANNEL_STATE_END_NO_DISSOLVE, 0);
	}
	ast_bridge_unlock(bridge_channel->bridge);
	return 0;
}

/*!
 * \brief Details for specific basic bridge personalities
 */
struct personality_details {
	/*! The v_table to use for this personality */
	struct ast_bridge_methods *v_table;
	/*! Flags to set on this type of bridge */
	unsigned int bridge_flags;
	/*! User data for this personality. If used, must be an ao2 object */
	void *pvt;
	/*! Callback to be called when changing to the personality */
	void (*on_personality_change)(struct ast_bridge *bridge);
};

/*!
 * \brief structure that organizes different personalities for basic bridges.
 */
struct bridge_basic_personality {
	/*! The current bridge personality in use */
	enum bridge_basic_personality_type current;
	/*! Array of details for the types of bridge personalities supported */
	struct personality_details details[BRIDGE_BASIC_PERSONALITY_END];
};

/*
 * \internal
 * \brief Get the extension for a given builtin feature.
 *
 * \param chan Get the feature extension for this channel.
 * \param feature_name features.conf name of feature.
 * \param buf Where to put the extension.
 * \param len Length of the given extension buffer.
 *
 * \retval 0 success
 * \retval non-zero failiure
 */
static int builtin_feature_get_exten(struct ast_channel *chan, const char *feature_name, char *buf, size_t len)
{
	SCOPED_CHANNELLOCK(lock, chan);

	return ast_get_builtin_feature(chan, feature_name, buf, len);
}

/*!
 * \internal
 * \brief Helper to add a builtin DTMF feature hook to the features struct.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param chan Get features from this channel.
 * \param flags Feature flags on the channel.
 * \param feature_flag Feature flag to test.
 * \param feature_name features.conf name of feature.
 * \param feature_bridge Bridge feature enum to get hook callback.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int builtin_features_helper(struct ast_bridge_features *features, struct ast_channel *chan,
	struct ast_flags *flags, unsigned int feature_flag, const char *feature_name, enum ast_bridge_builtin_feature feature_bridge)
{
	char dtmf[AST_FEATURE_MAX_LEN];
	int res;

	res = 0;
	if (ast_test_flag(flags, feature_flag)
		&& !builtin_feature_get_exten(chan, feature_name, dtmf, sizeof(dtmf))
		&& !ast_strlen_zero(dtmf)) {
		res = ast_bridge_features_enable(features, feature_bridge, dtmf, NULL, NULL,
			AST_BRIDGE_HOOK_REMOVE_ON_PULL | AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE);
		if (res) {
			ast_log(LOG_ERROR, "Channel %s: Requested DTMF feature %s not available.\n",
				ast_channel_name(chan), feature_name);
		}
	}

	return res;
}

/*!
 * \internal
 * \brief Setup bridge builtin features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param chan Get features from this channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_bridge_features_builtin(struct ast_bridge_features *features, struct ast_channel *chan)
{
	struct ast_flags *flags;
	int res;

	ast_channel_lock(chan);
	flags = ast_bridge_features_ds_get(chan);
	ast_channel_unlock(chan);
	if (!flags) {
		return 0;
	}

	res = 0;
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_REDIRECT, "blindxfer", AST_BRIDGE_BUILTIN_BLINDTRANSFER);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_REDIRECT, "atxfer", AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_DISCONNECT, "disconnect", AST_BRIDGE_BUILTIN_HANGUP);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_PARKCALL, "parkcall", AST_BRIDGE_BUILTIN_PARKCALL);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_AUTOMON, "automon", AST_BRIDGE_BUILTIN_AUTOMON);
	res |= builtin_features_helper(features, chan, flags, AST_FEATURE_AUTOMIXMON, "automixmon", AST_BRIDGE_BUILTIN_AUTOMIXMON);

	return res ? -1 : 0;
}

struct dynamic_dtmf_hook_run {
	/*! Offset into app_name[] where the channel name that activated the hook starts. */
	int activated_offset;
	/*! Offset into app_name[] where the dynamic feature name starts. */
	int feature_offset;
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

static void dynamic_dtmf_hook_callback(struct ast_bridge_channel *bridge_channel,
	const void *payload, size_t payload_size)
{
	struct ast_channel *chan = bridge_channel->chan;
	const struct dynamic_dtmf_hook_run *run_data = payload;

	pbx_builtin_setvar_helper(chan, "DYNAMIC_FEATURENAME",
		&run_data->app_name[run_data->feature_offset]);
	pbx_builtin_setvar_helper(chan, "DYNAMIC_WHO_ACTIVATED",
		&run_data->app_name[run_data->activated_offset]);

	ast_bridge_channel_run_app(bridge_channel, run_data->app_name,
		run_data->app_args_offset ? &run_data->app_name[run_data->app_args_offset] : NULL,
		run_data->moh_offset ? &run_data->app_name[run_data->moh_offset] : NULL);
}

struct dynamic_dtmf_hook_data {
	/*! Which side of bridge to run app (AST_FEATURE_FLAG_ONSELF/AST_FEATURE_FLAG_ONPEER) */
	unsigned int flags;
	/*! Offset into app_name[] where the dynamic feature name starts. */
	int feature_offset;
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

/*!
 * \internal
 * \brief Activated dynamic DTMF feature hook.
 * \since 12.0.0
 *
 * \param bridge_channel Channel executing the feature
 * \param hook_pvt Private data passed in when the hook was created
 *
 * \retval 0 Keep the callback hook.
 * \retval -1 Remove the callback hook.
 */
static int dynamic_dtmf_hook_trip(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct dynamic_dtmf_hook_data *pvt = hook_pvt;
	struct dynamic_dtmf_hook_run *run_data;
	const char *activated_name;
	size_t len_name;
	size_t len_args;
	size_t len_moh;
	size_t len_feature;
	size_t len_activated;
	size_t len_data;

	/* Determine lengths of things. */
	len_name = strlen(pvt->app_name) + 1;
	len_args = pvt->app_args_offset ? strlen(&pvt->app_name[pvt->app_args_offset]) + 1 : 0;
	len_moh = pvt->moh_offset ? strlen(&pvt->app_name[pvt->moh_offset]) + 1 : 0;
	len_feature = strlen(&pvt->app_name[pvt->feature_offset]) + 1;
	ast_channel_lock(bridge_channel->chan);
	activated_name = ast_strdupa(ast_channel_name(bridge_channel->chan));
	ast_channel_unlock(bridge_channel->chan);
	len_activated = strlen(activated_name) + 1;
	len_data = sizeof(*run_data) + len_name + len_args + len_moh + len_feature + len_activated;

	/* Fill in dynamic feature run hook data. */
	run_data = ast_alloca(len_data);
	run_data->app_args_offset = len_args ? len_name : 0;
	run_data->moh_offset = len_moh ? len_name + len_args : 0;
	run_data->feature_offset = len_name + len_args + len_moh;
	run_data->activated_offset = len_name + len_args + len_moh + len_feature;
	strcpy(run_data->app_name, pvt->app_name);/* Safe */
	if (len_args) {
		strcpy(&run_data->app_name[run_data->app_args_offset],
			&pvt->app_name[pvt->app_args_offset]);/* Safe */
	}
	if (len_moh) {
		strcpy(&run_data->app_name[run_data->moh_offset],
			&pvt->app_name[pvt->moh_offset]);/* Safe */
	}
	strcpy(&run_data->app_name[run_data->feature_offset],
		&pvt->app_name[pvt->feature_offset]);/* Safe */
	strcpy(&run_data->app_name[run_data->activated_offset], activated_name);/* Safe */

	if (ast_test_flag(pvt, AST_FEATURE_FLAG_ONPEER)) {
		ast_bridge_channel_write_callback(bridge_channel,
			AST_BRIDGE_CHANNEL_CB_OPTION_MEDIA,
			dynamic_dtmf_hook_callback, run_data, len_data);
	} else {
		dynamic_dtmf_hook_callback(bridge_channel, run_data, len_data);
	}
	return 0;
}

/*!
 * \internal
 * \brief Add a dynamic DTMF feature hook to the bridge features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param flags Which side of bridge to run app (AST_FEATURE_FLAG_ONSELF/AST_FEATURE_FLAG_ONPEER).
 * \param dtmf DTMF trigger sequence.
 * \param feature_name Name of the dynamic feature.
 * \param app_name Dialplan application name to run.
 * \param app_args Dialplan application arguments. (Empty or NULL if no arguments)
 * \param moh_class MOH class to play to peer. (Empty or NULL if no MOH played)
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int dynamic_dtmf_hook_add(struct ast_bridge_features *features, unsigned int flags, const char *dtmf, const char *feature_name, const char *app_name, const char *app_args, const char *moh_class)
{
	struct dynamic_dtmf_hook_data *hook_data;
	size_t len_name = strlen(app_name) + 1;
	size_t len_args = ast_strlen_zero(app_args) ? 0 : strlen(app_args) + 1;
	size_t len_moh = ast_strlen_zero(moh_class) ? 0 : strlen(moh_class) + 1;
	size_t len_feature = strlen(feature_name) + 1;
	size_t len_data = sizeof(*hook_data) + len_name + len_args + len_moh + len_feature;
	int res;

	/* Fill in application run hook data. */
	hook_data = ast_malloc(len_data);
	if (!hook_data) {
		return -1;
	}
	hook_data->flags = flags;
	hook_data->app_args_offset = len_args ? len_name : 0;
	hook_data->moh_offset = len_moh ? len_name + len_args : 0;
	hook_data->feature_offset = len_name + len_args + len_moh;
	strcpy(hook_data->app_name, app_name);/* Safe */
	if (len_args) {
		strcpy(&hook_data->app_name[hook_data->app_args_offset], app_args);/* Safe */
	}
	if (len_moh) {
		strcpy(&hook_data->app_name[hook_data->moh_offset], moh_class);/* Safe */
	}
	strcpy(&hook_data->app_name[hook_data->feature_offset], feature_name);/* Safe */

	res = ast_bridge_dtmf_hook(features, dtmf, dynamic_dtmf_hook_trip, hook_data,
		ast_free_ptr,
		AST_BRIDGE_HOOK_REMOVE_ON_PULL | AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE);
	if (res) {
		ast_free(hook_data);
	}
	return res;
}

static int setup_dynamic_feature(void *obj, void *arg, void *data, int flags)
{
	struct ast_applicationmap_item *item = obj;
	struct ast_bridge_features *features = arg;
	int *res = data;

	*res |= dynamic_dtmf_hook_add(features,
		item->activate_on_self ? AST_FEATURE_FLAG_ONSELF : AST_FEATURE_FLAG_ONPEER,
		item->dtmf, item->name, item->app, item->app_data, item->moh_class);

	return 0;
}

/*!
 * \internal
 * \brief Setup bridge dynamic features.
 * \since 12.0.0
 *
 * \param features Bridge features to setup.
 * \param chan Get features from this channel.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int setup_bridge_features_dynamic(struct ast_bridge_features *features, struct ast_channel *chan)
{
	RAII_VAR(struct ao2_container *, applicationmap, NULL, ao2_cleanup);
	int res = 0;

	ast_channel_lock(chan);
	applicationmap = ast_get_chan_applicationmap(chan);
	ast_channel_unlock(chan);
	if (!applicationmap) {
		return 0;
	}

	ao2_callback_data(applicationmap, 0, setup_dynamic_feature, features, &res);

	return res;
}

/*!
 * \internal
 * \brief Setup DTMF feature hooks using the channel features datastore property.
 * \since 12.0.0
 *
 * \param bridge_channel What to setup DTMF features on.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int bridge_basic_setup_features(struct ast_bridge_channel *bridge_channel)
{
	int res = 0;

	res |= setup_bridge_features_builtin(bridge_channel->features, bridge_channel->chan);
	res |= setup_bridge_features_dynamic(bridge_channel->features, bridge_channel->chan);

	return res;
}

static int add_normal_hooks(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	return ast_bridge_hangup_hook(bridge_channel->features, basic_hangup_hook,
			NULL, NULL, AST_BRIDGE_HOOK_REMOVE_ON_PULL)
		|| bridge_basic_setup_features(bridge_channel);
}

/*!
 * \internal
 * \brief ast_bridge basic push method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \note On entry, self is already locked.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int bridge_personality_normal_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	if (add_normal_hooks(self, bridge_channel)) {
		return -1;
	}

	return 0;
}

static int bridge_basic_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	struct bridge_basic_personality *personality = self->personality;

	ast_assert(personality != NULL);

	if (personality->details[personality->current].v_table->push
		&& personality->details[personality->current].v_table->push(self, bridge_channel, swap)) {
		return -1;
	}

	ast_bridge_channel_update_linkedids(bridge_channel, swap);
	ast_bridge_channel_update_accountcodes(bridge_channel, swap);

	return ast_bridge_base_v_table.push(self, bridge_channel, swap);
}

static void bridge_basic_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	struct bridge_basic_personality *personality = self->personality;

	ast_assert(personality != NULL);

	if (personality->details[personality->current].v_table->pull) {
		personality->details[personality->current].v_table->pull(self, bridge_channel);
	}

	ast_bridge_channel_update_accountcodes(NULL, bridge_channel);

	ast_bridge_base_v_table.pull(self, bridge_channel);
}

static void bridge_basic_destroy(struct ast_bridge *self)
{
	struct bridge_basic_personality *personality = self->personality;

	ao2_cleanup(personality);

	ast_bridge_base_v_table.destroy(self);
}

/*!
 * \brief Remove appropriate hooks when basic bridge personality changes
 *
 * Hooks that have the AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE flag
 * set will be removed from all bridge channels in the bridge.
 *
 * \param bridge Basic bridge undergoing personality change
 */
static void remove_hooks_on_personality_change(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *iter;

	AST_LIST_TRAVERSE(&bridge->channels, iter, entry) {
		SCOPED_LOCK(lock, iter, ast_bridge_channel_lock, ast_bridge_channel_unlock);
		ast_bridge_features_remove(iter->features, AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE);
	}
}

/*!
 * \brief Attended transfer superstates.
 *
 * An attended transfer's progress is facilitated by a state machine.
 * The individual states of the state machine fall into the realm of
 * one of two superstates.
 */
enum attended_transfer_superstate {
	/*!
	 * \brief Transfer superstate
	 *
	 * The attended transfer state machine begins in this superstate. The
	 * goal of this state is for a transferer channel to facilitate a
	 * transfer from a transferee to a transfer target.
	 *
	 * There are two bridges used in this superstate. The transferee bridge is
	 * the bridge that the transferer and transferee channels originally
	 * communicate in, and the target bridge is the bridge where the transfer
	 * target is being dialed.
	 *
	 * The transferer channel is capable of moving between the bridges using
	 * the DTMF swap sequence.
	 */
	SUPERSTATE_TRANSFER,
	/*!
	 * \brief Recall superstate
	 *
	 * The attended transfer state machine moves to this superstate if
	 * atxferdropcall is set to "no" and the transferer channel hangs up
	 * during a transfer. The goal in this superstate is to call back either
	 * the transfer target or transferer and rebridge with the transferee
	 * channel(s).
	 *
	 * In this superstate, there is only a single bridge used, the original
	 * transferee bridge. Rather than distinguishing between a transferer
	 * and transfer target, all outbound calls are toward a "recall_target"
	 * channel.
	 */
	SUPERSTATE_RECALL,
};

/*!
 * The states in the attended transfer state machine.
 */
enum attended_transfer_state {
	/*!
	 * \brief Calling Target state
	 *
	 * This state describes the initial state of a transfer. The transferer
	 * waits in the transfer target's bridge for the transfer target to answer.
	 *
	 * Superstate: Transfer
	 *
	 * Preconditions:
	 * 1) Transfer target is RINGING
	 * 2) Transferer is in transferee bridge
	 * 3) Transferee is on hold
	 *
	 * Transitions to TRANSFER_CALLING_TARGET:
	 * 1) This is the initial state for an attended transfer.
	 * 2) TRANSFER_HESITANT: Transferer presses DTMF swap sequence
	 *
	 * State operation:
	 * The transferer is moved from the transferee bridge into the transfer
	 * target bridge.
	 *
	 * Transitions from TRANSFER_CALLING_TARGET:
	 * 1) TRANSFER_FAIL: Transferee hangs up.
	 * 2) TRANSFER_BLOND: Transferer hangs up or presses DTMF swap sequence
	 * and configured atxferdropcall setting is yes.
	 * 3) TRANSFER_BLOND_NONFINAL: Transferer hangs up or presses DTMF swap
	 * sequence and configured atxferdroppcall setting is no.
	 * 4) TRANSFER_CONSULTING: Transfer target answers the call.
	 * 5) TRANSFER_REBRIDGE: Transfer target hangs up, call to transfer target
	 * times out, or transferer presses DTMF abort sequence.
	 * 6) TRANSFER_THREEWAY: Transferer presses DTMF threeway sequence.
	 * 7) TRANSFER_HESITANT: Transferer presses DTMF swap sequence.
	 */
	TRANSFER_CALLING_TARGET,
	/*!
	 * \brief Hesitant state
	 *
	 * This state only arises if when waiting for the transfer target to
	 * answer, the transferer presses the DTMF swap sequence. This will
	 * cause the transferer to be rebridged with the transferee temporarily.
	 *
	 * Superstate: Transfer
	 *
	 * Preconditions:
	 * 1) Transfer target is in ringing state
	 * 2) Transferer is in transfer target bridge
	 * 3) Transferee is on hold
	 *
	 * Transitions to TRANSFER_HESITANT:
	 * 1) TRANSFER_CALLING_TARGET: Transferer presses DTMF swap sequence.
	 *
	 * State operation:
	 * The transferer is moved from the transfer target bridge into the
	 * transferee bridge, and the transferee is taken off hold.
	 *
	 * Transitions from TRANSFER_HESITANT:
	 * 1) TRANSFER_FAIL: Transferee hangs up
	 * 2) TRANSFER_BLOND: Transferer hangs up or presses DTMF swap sequence
	 * and configured atxferdropcall setting is yes.
	 * 3) TRANSFER_BLOND_NONFINAL: Transferer hangs up or presses DTMF swap
	 * sequence and configured atxferdroppcall setting is no.
	 * 4) TRANSFER_DOUBLECHECKING: Transfer target answers the call
	 * 5) TRANSFER_RESUME: Transfer target hangs up, call to transfer target
	 * times out, or transferer presses DTMF abort sequence.
	 * 6) TRANSFER_THREEWAY: Transferer presses DTMF threeway sequence.
	 * 7) TRANSFER_CALLING_TARGET: Transferer presses DTMF swap sequence.
	 */
	TRANSFER_HESITANT,
	/*!
	 * \brief Rebridge state
	 *
	 * This is a terminal state that indicates that the transferer needs
	 * to move back to the transferee's bridge. This is a failed attended
	 * transfer result.
	 *
	 * Superstate: Transfer
	 *
	 * Preconditions:
	 * 1) Transferer is in transfer target bridge
	 * 2) Transferee is on hold
	 *
	 * Transitions to TRANSFER_REBRIDGE:
	 * 1) TRANSFER_CALLING_TARGET: Transfer target hangs up, call to transfer target
	 * times out, or transferer presses DTMF abort sequence.
	 * 2) TRANSFER_STATE_CONSULTING: Transfer target hangs up, or transferer presses
	 * DTMF abort sequence.
	 *
	 * State operation:
	 * The transferer channel is moved from the transfer target bridge to the
	 * transferee bridge. The transferee is taken off hold. A stasis transfer
	 * message is published indicating a failed attended transfer.
	 *
	 * Transitions from TRANSFER_REBRIDGE:
	 * None
	 */
	TRANSFER_REBRIDGE,
	/*!
	 * \brief Resume state
	 *
	 * This is a terminal state that indicates that the party bridged with the
	 * transferee is the final party to be bridged with that transferee. This state
	 * may come about due to a successful recall or due to a failed transfer.
	 *
	 * Superstate: Transfer or Recall
	 *
	 * Preconditions:
	 * In Transfer Superstate:
	 * 1) Transferer is in transferee bridge
	 * 2) Transferee is not on hold
	 * In Recall Superstate:
	 * 1) The recall target is in the transferee bridge
	 * 2) Transferee is not on hold
	 *
	 * Transitions to TRANSFER_RESUME:
	 * TRANSFER_HESITANT: Transfer target hangs up, call to transfer target times out,
	 * or transferer presses DTMF abort sequence.
	 * TRANSFER_DOUBLECHECKING: Transfer target hangs up or transferer presses DTMF
	 * abort sequence.
	 * TRANSFER_BLOND_NONFINAL: Recall target answers
	 * TRANSFER_RECALLING: Recall target answers
	 * TRANSFER_RETRANSFER: Recall target answers
	 *
	 * State operations:
	 * None
	 *
	 * Transitions from TRANSFER_RESUME:
	 * None
	 */
	TRANSFER_RESUME,
	/*!
	 * \brief Threeway state
	 *
	 * This state results when the transferer wishes to have all parties involved
	 * in a transfer to be in the same bridge together.
	 *
	 * Superstate: Transfer
	 *
	 * Preconditions:
	 * 1) Transfer target state is either RINGING or UP
	 * 2) Transferer is in either bridge
	 * 3) Transferee is not on hold
	 *
	 * Transitions to TRANSFER_THREEWAY:
	 * 1) TRANSFER_CALLING_TARGET: Transferer presses DTMF threeway sequence.
	 * 2) TRANSFER_HESITANT: Transferer presses DTMF threeway sequence.
	 * 3) TRANSFER_CONSULTING: Transferer presses DTMF threeway sequence.
	 * 4) TRANSFER_DOUBLECHECKING: Transferer presses DTMF threeway sequence.
	 *
	 * State operation:
	 * The transfer target bridge is merged into the transferee bridge.
	 *
	 * Transitions from TRANSFER_THREEWAY:
	 * None.
	 */
	TRANSFER_THREEWAY,
	/*!
	 * \brief Consulting state
	 *
	 * This state describes the case where the transferer and transfer target
	 * are able to converse in the transfer target's bridge prior to completing
	 * the transfer.
	 *
	 * Superstate: Transfer
	 *
	 * Preconditions:
	 * 1) Transfer target is UP
	 * 2) Transferer is in target bridge
	 * 3) Transferee is on hold
	 *
	 * Transitions to TRANSFER_CONSULTING:
	 * 1) TRANSFER_CALLING_TARGET: Transfer target answers.
	 * 2) TRANSFER_DOUBLECHECKING: Transferer presses DTMF swap sequence.
	 *
	 * State operations:
	 * None.
	 *
	 * Transitions from TRANSFER_CONSULTING:
	 * TRANSFER_COMPLETE: Transferer hangs up or transferer presses DTMF complete sequence.
	 * TRANSFER_REBRIDGE: Transfer target hangs up or transferer presses DTMF abort sequence.
	 * TRANSFER_THREEWAY: Transferer presses DTMF threeway sequence.
	 * TRANSFER_DOUBLECHECKING: Transferer presses DTMF swap sequence.
	 */
	TRANSFER_CONSULTING,
	/*!
	 * \brief Double-checking state
	 *
	 * This state describes the case where the transferer and transferee are
	 * able to converse in the transferee's bridge prior to completing the transfer. The
	 * difference between this and TRANSFER_HESITANT is that the transfer target is
	 * UP in this case.
	 *
	 * Superstate: Transfer
	 *
	 * Preconditions:
	 * 1) Transfer target is UP and on hold
	 * 2) Transferer is in transferee bridge
	 * 3) Transferee is off hold
	 *
	 * Transitions to TRANSFER_DOUBLECHECKING:
	 * 1) TRANSFER_HESITANT: Transfer target answers.
	 * 2) TRANSFER_CONSULTING: Transferer presses DTMF swap sequence.
	 *
	 * State operations:
	 * None.
	 *
	 * Transitions from TRANSFER_DOUBLECHECKING:
	 * 1) TRANSFER_FAIL: Transferee hangs up.
	 * 2) TRANSFER_COMPLETE: Transferer hangs up or presses DTMF complete sequence.
	 * 3) TRANSFER_RESUME: Transfer target hangs up or transferer presses DTMF abort sequence.
	 * 4) TRANSFER_THREEWAY: Transferer presses DTMF threeway sequence.
	 * 5) TRANSFER_CONSULTING: Transferer presses the DTMF swap sequence.
	 */
	TRANSFER_DOUBLECHECKING,
	/*!
	 * \brief Complete state
	 *
	 * This is a terminal state where a transferer has successfully completed an attended
	 * transfer. This state's goal is to get the transfer target and transferee into
	 * the same bridge and the transferer off the call.
	 *
	 * Superstate: Transfer
	 *
	 * Preconditions:
	 * 1) Transfer target is UP and off hold.
	 * 2) Transferer is in either bridge.
	 * 3) Transferee is off hold.
	 *
	 * Transitions to TRANSFER_COMPLETE:
	 * 1) TRANSFER_CONSULTING: transferer hangs up or presses the DTMF complete sequence.
	 * 2) TRANSFER_DOUBLECHECKING: transferer hangs up or presses the DTMF complete sequence.
	 *
	 * State operation:
	 * The transfer target bridge is merged into the transferee bridge. The transferer
	 * channel is kicked out of the bridges as part of the merge.
	 *
	 * State operations:
	 * 1) Merge the transfer target bridge into the transferee bridge,
	 * excluding the transferer channel from the merge.
	 * 2) Publish a stasis transfer message.
	 *
	 * Exit operations:
	 * This is a terminal state, so there are no exit operations.
	 */
	TRANSFER_COMPLETE,
	/*!
	 * \brief Blond state
	 *
	 * This is a terminal state where a transferer has completed an attended transfer prior
	 * to the transfer target answering. This state is only entered if atxferdropcall
	 * is set to 'yes'. This is considered to be a successful attended transfer.
	 *
	 * Superstate: Transfer
	 *
	 * Preconditions:
	 * 1) Transfer target is RINGING.
	 * 2) Transferer is in either bridge.
	 * 3) Transferee is off hold.
	 *
	 * Transitions to TRANSFER_BLOND:
	 * 1) TRANSFER_CALLING_TARGET: Transferer hangs up or presses the DTMF complete sequence.
	 *    atxferdropcall is set to 'yes'.
	 * 2) TRANSFER_HESITANT: Transferer hangs up or presses the DTMF complete sequence.
	 *    atxferdropcall is set to 'yes'.
	 *
	 * State operations:
	 * The transfer target bridge is merged into the transferee bridge. The transferer
	 * channel is kicked out of the bridges as part of the merge. A stasis transfer
	 * publication is sent indicating a successful transfer.
	 *
	 * Transitions from TRANSFER_BLOND:
	 * None
	 */
	TRANSFER_BLOND,
	/*!
	 * \brief Blond non-final state
	 *
	 * This state is very similar to the TRANSFER_BLOND state, except that
	 * this state is entered when atxferdropcall is set to 'no'. This is the
	 * initial state of the Recall superstate, so state operations mainly involve
	 * moving to the Recall superstate. This means that the transfer target, that
	 * is currently ringing is now known as the recall target.
	 *
	 * Superstate: Recall
	 *
	 * Preconditions:
	 * 1) Recall target is RINGING.
	 * 2) Transferee is off hold.
	 *
	 * Transitions to TRANSFER_BLOND_NONFINAL:
	 * 1) TRANSFER_CALLING_TARGET: Transferer hangs up or presses the DTMF complete sequence.
	 *    atxferdropcall is set to 'no'.
	 * 2) TRANSFER_HESITANT: Transferer hangs up or presses the DTMF complete sequence.
	 *    atxferdropcall is set to 'no'.
	 *
	 * State operation:
	 * The superstate of the attended transfer is changed from Transfer to Recall.
	 * The transfer target bridge is merged into the transferee bridge. The transferer
	 * channel is kicked out of the bridges as part of the merge.
	 *
	 * Transitions from TRANSFER_BLOND_NONFINAL:
	 * 1) TRANSFER_FAIL: Transferee hangs up
	 * 2) TRANSFER_RESUME: Recall target answers
	 * 3) TRANSFER_RECALLING: Recall target hangs up or time expires.
	 */
	TRANSFER_BLOND_NONFINAL,
	/*!
	 * \brief Recalling state
	 *
	 * This state is entered if the recall target from the TRANSFER_BLOND_NONFINAL
	 * or TRANSFER_RETRANSFER states hangs up or does not answer. The goal of this
	 * state is to call back the original transferer in an attempt to recover the
	 * original call.
	 *
	 * Superstate: Recall
	 *
	 * Preconditions:
	 * 1) Recall target is down.
	 * 2) Transferee is off hold.
	 *
	 * Transitions to TRANSFER_RECALLING:
	 * 1) TRANSFER_BLOND_NONFINAL: Recall target hangs up or time expires.
	 * 2) TRANSFER_RETRANSFER: Recall target hangs up or time expires.
	 *    atxferloopdelay is non-zero.
	 * 3) TRANSFER_WAIT_TO_RECALL: Time expires.
	 *
	 * State operation:
	 * The original transferer becomes the recall target and is called using the Dialing API.
	 * Ringing is indicated to the transferee.
	 *
	 * Transitions from TRANSFER_RECALLING:
	 * 1) TRANSFER_FAIL:
	 *    a) Transferee hangs up.
	 *    b) Recall target hangs up or time expires, and number of recall attempts exceeds atxfercallbackretries
	 * 2) TRANSFER_WAIT_TO_RETRANSFER: Recall target hangs up or time expires.
	 *    atxferloopdelay is non-zero.
	 * 3) TRANSFER_RETRANSFER: Recall target hangs up or time expires.
	 *    atxferloopdelay is zero.
	 * 4) TRANSFER_RESUME: Recall target answers.
	 */
	TRANSFER_RECALLING,
	/*!
	 * \brief Wait to Retransfer state
	 *
	 * This state is used simply to give a bit of breathing room between attempting
	 * to call back the original transferer and attempting to call back the original
	 * transfer target. The transferee hears music on hold during this state as an
	 * auditory clue that no one is currently being dialed.
	 *
	 * Superstate: Recall
	 *
	 * Preconditions:
	 * 1) Recall target is down.
	 * 2) Transferee is off hold.
	 *
	 * Transitions to TRANSFER_WAIT_TO_RETRANSFER:
	 * 1) TRANSFER_RECALLING: Recall target hangs up or time expires.
	 *    atxferloopdelay is non-zero.
	 *
	 * State operation:
	 * The transferee is placed on hold.
	 *
	 * Transitions from TRANSFER_WAIT_TO_RETRANSFER:
	 * 1) TRANSFER_FAIL: Transferee hangs up.
	 * 2) TRANSFER_RETRANSFER: Time expires.
	 */
	TRANSFER_WAIT_TO_RETRANSFER,
	/*!
	 * \brief Retransfer state
	 *
	 * This state is used in order to attempt to call back the original
	 * transfer target channel from the transfer. The transferee hears
	 * ringing during this state as an auditory cue that a party is being
	 * dialed.
	 *
	 * Superstate: Recall
	 *
	 * Preconditions:
	 * 1) Recall target is down.
	 * 2) Transferee is off hold.
	 *
	 * Transitions to TRANSFER_RETRANSFER:
	 * 1) TRANSFER_RECALLING: Recall target hangs up or time expires.
	 *    atxferloopdelay is zero.
	 * 2) TRANSFER_WAIT_TO_RETRANSFER: Time expires.
	 *
	 * State operation:
	 * The original transfer target is requested and is set as the recall target.
	 * The recall target is called and placed into the transferee bridge.
	 *
	 * Transitions from TRANSFER_RETRANSFER:
	 * 1) TRANSFER_FAIL: Transferee hangs up.
	 * 2) TRANSFER_WAIT_TO_RECALL: Recall target hangs up or time expires.
	 *    atxferloopdelay is non-zero.
	 * 3) TRANSFER_RECALLING: Recall target hangs up or time expires.
	 *    atxferloopdelay is zero.
	 */
	TRANSFER_RETRANSFER,
	/*!
	 * \brief Wait to recall state
	 *
	 * This state is used simply to give a bit of breathing room between attempting
	 * to call back the original transfer target and attempting to call back the
	 * original transferer. The transferee hears music on hold during this state as an
	 * auditory clue that no one is currently being dialed.
	 *
	 * Superstate: Recall
	 *
	 * Preconditions:
	 * 1) Recall target is down.
	 * 2) Transferee is off hold.
	 *
	 * Transitions to TRANSFER_WAIT_TO_RECALL:
	 * 1) TRANSFER_RETRANSFER: Recall target hangs up or time expires.
	 *    atxferloopdelay is non-zero.
	 *
	 * State operation:
	 * Transferee is placed on hold.
	 *
	 * Transitions from TRANSFER_WAIT_TO_RECALL:
	 * 1) TRANSFER_FAIL: Transferee hangs up
	 * 2) TRANSFER_RECALLING: Time expires
	 */
	TRANSFER_WAIT_TO_RECALL,
	/*!
	 * \brief Fail state
	 *
	 * This state indicates that something occurred during the transfer that
	 * makes a graceful completion impossible. The most common stimulus for this
	 * state is when the transferee hangs up.
	 *
	 * Superstate: Transfer and Recall
	 *
	 * Preconditions:
	 * None
	 *
	 * Transitions to TRANSFER_FAIL:
	 * 1) TRANSFER_CALLING_TARGET: Transferee hangs up.
	 * 2) TRANSFER_HESITANT: Transferee hangs up.
	 * 3) TRANSFER_DOUBLECHECKING: Transferee hangs up.
	 * 4) TRANSFER_BLOND_NONFINAL: Transferee hangs up.
	 * 5) TRANSFER_RECALLING:
	 *    a) Transferee hangs up.
	 *    b) Recall target hangs up or time expires, and number of
	 *       recall attempts exceeds atxfercallbackretries.
	 * 6) TRANSFER_WAIT_TO_RETRANSFER: Transferee hangs up.
	 * 7) TRANSFER_RETRANSFER: Transferee hangs up.
	 * 8) TRANSFER_WAIT_TO_RECALL: Transferee hangs up.
	 *
	 * State operation:
	 * A transfer stasis publication is made indicating a failed transfer.
	 * The transferee bridge is destroyed.
	 *
	 * Transitions from TRANSFER_FAIL:
	 * None.
	 */
	TRANSFER_FAIL,
};

/*!
 * \brief Stimuli that can cause transfer state changes
 */
enum attended_transfer_stimulus {
	/*! No stimulus. This literally can never happen. */
	STIMULUS_NONE,
	/*! All of the transferee channels have been hung up. */
	STIMULUS_TRANSFEREE_HANGUP,
	/*! The transferer has hung up. */
	STIMULUS_TRANSFERER_HANGUP,
	/*! The transfer target channel has hung up. */
	STIMULUS_TRANSFER_TARGET_HANGUP,
	/*! The transfer target channel has answered. */
	STIMULUS_TRANSFER_TARGET_ANSWER,
	/*! The recall target channel has hung up. */
	STIMULUS_RECALL_TARGET_HANGUP,
	/*! The recall target channel has answered. */
	STIMULUS_RECALL_TARGET_ANSWER,
	/*! The current state's timer has expired. */
	STIMULUS_TIMEOUT,
	/*! The transferer pressed the abort DTMF sequence. */
	STIMULUS_DTMF_ATXFER_ABORT,
	/*! The transferer pressed the complete DTMF sequence. */
	STIMULUS_DTMF_ATXFER_COMPLETE,
	/*! The transferer pressed the three-way DTMF sequence. */
	STIMULUS_DTMF_ATXFER_THREEWAY,
	/*! The transferer pressed the swap DTMF sequence. */
	STIMULUS_DTMF_ATXFER_SWAP,
};

/*!
 * \brief String representations of the various stimuli
 *
 * Used for debugging purposes
 */
const char *stimulus_strs[] = {
	[STIMULUS_NONE] = "None",
	[STIMULUS_TRANSFEREE_HANGUP] = "Transferee Hangup",
	[STIMULUS_TRANSFERER_HANGUP] = "Transferer Hangup",
	[STIMULUS_TRANSFER_TARGET_HANGUP] = "Transfer Target Hangup",
	[STIMULUS_TRANSFER_TARGET_ANSWER] = "Transfer Target Answer",
	[STIMULUS_RECALL_TARGET_HANGUP] = "Recall Target Hangup",
	[STIMULUS_RECALL_TARGET_ANSWER] = "Recall Target Answer",
	[STIMULUS_TIMEOUT] = "Timeout",
	[STIMULUS_DTMF_ATXFER_ABORT] = "DTMF Abort",
	[STIMULUS_DTMF_ATXFER_COMPLETE] = "DTMF Complete",
	[STIMULUS_DTMF_ATXFER_THREEWAY] = "DTMF Threeway",
	[STIMULUS_DTMF_ATXFER_SWAP] = "DTMF Swap",
};

struct stimulus_list {
	enum attended_transfer_stimulus stimulus;
	AST_LIST_ENTRY(stimulus_list) next;
};

/*!
 * \brief Collection of data related to an attended transfer attempt
 */
struct attended_transfer_properties {
	AST_DECLARE_STRING_FIELDS (
		/*! Extension of transfer target */
		AST_STRING_FIELD(exten);
		/*! Context of transfer target */
		AST_STRING_FIELD(context);
		/*! Sound to play on failure */
		AST_STRING_FIELD(failsound);
		/*! Sound to play when transfer completes */
		AST_STRING_FIELD(xfersound);
		/*! The channel technology of the transferer channel */
		AST_STRING_FIELD(transferer_type);
		/*! The transferer channel address */
		AST_STRING_FIELD(transferer_addr);
	);
	/*! Condition used to synchronize when stimuli are reported to the monitor thread */
	ast_cond_t cond;
	/*! The bridge where the transferee resides. This bridge is also the bridge that
	 * survives a successful attended transfer.
	 */
	struct ast_bridge *transferee_bridge;
	/*! The bridge used to place an outbound call to the transfer target. This
	 * bridge is merged with the transferee_bridge on a successful transfer.
	 */
	struct ast_bridge *target_bridge;
	/*! The party that performs the attended transfer. */
	struct ast_channel *transferer;
	/*! The local channel dialed to reach the transfer target. */
	struct ast_channel *transfer_target;
	/*! The party that is currently being recalled. Depending on
	 * the current state, this may be either the party that originally
	 * was the transferer or the original transfer target.  This is
	 * set with reference when entering the BLOND_NONFINAL, RECALLING,
	 * and RETRANSFER states, and the reference released on state exit
	 * if continuing with recall or retransfer to avoid leak.
	 */
	struct ast_channel *recall_target;
	/*! The absolute starting time for running timers */
	struct timeval start;
	AST_LIST_HEAD_NOLOCK(,stimulus_list) stimulus_queue;
	/*! The current state of the attended transfer */
	enum attended_transfer_state state;
	/*! The current superstate of the attended transfer */
	enum attended_transfer_superstate superstate;
	/*! Configured atxferdropcall from features.conf */
	int atxferdropcall;
	/*! Configured atxfercallbackretries from features.conf */
	int atxfercallbackretries;
	/*! Configured atxferloopdelay from features.conf */
	int atxferloopdelay;
	/*! Configured atxfernoanswertimeout from features.conf */
	int atxfernoanswertimeout;
	/*! Count of the number of times that recalls have been attempted */
	int retry_attempts;
	/*! Framehook ID for outbounc call to transfer target or recall target */
	int target_framehook_id;
	/*! Dial structure used when recalling transferer channel */
	struct ast_dial *dial;
	/*! The bridging features the transferer has available */
	struct ast_flags transferer_features;
	/*! Saved transferer connected line data for recalling the transferer. */
	struct ast_party_connected_line original_transferer_colp;
};

static void attended_transfer_properties_destructor(void *obj)
{
	struct attended_transfer_properties *props = obj;

	ast_debug(1, "Destroy attended transfer properties %p\n", props);

	ao2_cleanup(props->target_bridge);
	ao2_cleanup(props->transferee_bridge);
	/* Use ast_channel_cleanup() instead of ast_channel_unref() for channels since they may be NULL */
	ast_channel_cleanup(props->transferer);
	ast_channel_cleanup(props->transfer_target);
	ast_channel_cleanup(props->recall_target);
	ast_party_connected_line_free(&props->original_transferer_colp);
	ast_string_field_free_memory(props);
	ast_cond_destroy(&props->cond);
}

/*!
 * \internal
 * \brief Determine the transfer context to use.
 * \since 12.0.0
 *
 * \param transferer Channel initiating the transfer.
 * \param context User supplied context if available.  May be NULL.
 *
 * \return The context to use for the transfer.
 */
static const char *get_transfer_context(struct ast_channel *transferer, const char *context)
{
	if (!ast_strlen_zero(context)) {
		return context;
	}
	context = pbx_builtin_getvar_helper(transferer, "TRANSFER_CONTEXT");
	if (!ast_strlen_zero(context)) {
		return context;
	}
	context = ast_channel_macrocontext(transferer);
	if (!ast_strlen_zero(context)) {
		return context;
	}
	context = ast_channel_context(transferer);
	if (!ast_strlen_zero(context)) {
		return context;
	}
	return "default";
}

/*!
 * \brief Allocate and initialize attended transfer properties
 *
 * \param transferer The channel performing the attended transfer
 * \param context Suggestion for what context the transfer target extension can be found in
 *
 * \retval NULL Failure to allocate or initialize
 * \retval non-NULL Newly allocated properties
 */
static struct attended_transfer_properties *attended_transfer_properties_alloc(
	struct ast_channel *transferer, const char *context)
{
	struct attended_transfer_properties *props;
	char *tech;
	char *addr;
	char *serial;
	RAII_VAR(struct ast_features_xfer_config *, xfer_cfg, NULL, ao2_cleanup);
	struct ast_flags *transferer_features;

	props = ao2_alloc(sizeof(*props), attended_transfer_properties_destructor);
	if (!props || ast_string_field_init(props, 64)) {
		return NULL;
	}

	ast_cond_init(&props->cond, NULL);

	props->target_framehook_id = -1;
	props->transferer = ast_channel_ref(transferer);

	ast_channel_lock(props->transferer);
	xfer_cfg = ast_get_chan_features_xfer_config(props->transferer);
	if (!xfer_cfg) {
		ast_log(LOG_ERROR, "Unable to get transfer configuration from channel %s\n", ast_channel_name(props->transferer));
		ast_channel_unlock(props->transferer);
		ao2_ref(props, -1);
		return NULL;
	}
	transferer_features = ast_bridge_features_ds_get(props->transferer);
	if (transferer_features) {
		props->transferer_features = *transferer_features;
	}
	props->atxferdropcall = xfer_cfg->atxferdropcall;
	props->atxfercallbackretries = xfer_cfg->atxfercallbackretries;
	props->atxfernoanswertimeout = xfer_cfg->atxfernoanswertimeout;
	props->atxferloopdelay = xfer_cfg->atxferloopdelay;
	ast_string_field_set(props, context, get_transfer_context(transferer, context));
	ast_string_field_set(props, failsound, xfer_cfg->xferfailsound);
	ast_string_field_set(props, xfersound, xfer_cfg->xfersound);

	/*
	 * Save the transferee's party information for any recall calls.
	 * This is the only piece of information needed that gets overwritten
	 * on the transferer channel by the inital call to the transfer target.
	 */
	ast_party_connected_line_copy(&props->original_transferer_colp,
		ast_channel_connected(props->transferer));

	tech = ast_strdupa(ast_channel_name(props->transferer));
	addr = strchr(tech, '/');
	if (!addr) {
		ast_log(LOG_ERROR, "Transferer channel name does not follow typical channel naming format (tech/address)\n");
		ast_channel_unlock(props->transferer);
		ao2_ref(props, -1);
		return NULL;
	}
	*addr++ = '\0';
	serial = strrchr(addr, '-');
	if (serial) {
		*serial = '\0';
	}
	ast_string_field_set(props, transferer_type, tech);
	ast_string_field_set(props, transferer_addr, addr);

	ast_channel_unlock(props->transferer);

	ast_debug(1, "Allocated attended transfer properties %p for transfer from %s\n",
			props, ast_channel_name(props->transferer));
	return props;
}

/*!
 * \brief Free backlog of stimuli in the queue
 */
static void clear_stimulus_queue(struct attended_transfer_properties *props)
{
	struct stimulus_list *list;
	SCOPED_AO2LOCK(lock, props);

	while ((list = AST_LIST_REMOVE_HEAD(&props->stimulus_queue, next))) {
		ast_free(list);
	}
}

/*!
 * \brief Initiate shutdown of attended transfer properties
 *
 * Calling this indicates that the attended transfer properties are no longer needed
 * because the transfer operation has concluded.
 */
static void attended_transfer_properties_shutdown(struct attended_transfer_properties *props)
{
	ast_debug(1, "Shutting down attended transfer %p\n", props);

	if (props->transferee_bridge) {
		bridge_basic_change_personality(props->transferee_bridge,
				BRIDGE_BASIC_PERSONALITY_NORMAL, NULL);
		ast_bridge_merge_inhibit(props->transferee_bridge, -1);
	}

	if (props->target_bridge) {
		ast_bridge_destroy(props->target_bridge, 0);
		props->target_bridge = NULL;
	}

	if (props->transferer) {
		ast_channel_remove_bridge_role(props->transferer, AST_TRANSFERER_ROLE_NAME);
	}

	clear_stimulus_queue(props);

	ao2_cleanup(props);
}

static void stimulate_attended_transfer(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	struct stimulus_list *list;

	list = ast_calloc(1, sizeof(*list));
	if (!list) {
		ast_log(LOG_ERROR, "Unable to push event to attended transfer queue. Expect transfer to fail\n");
		return;
	}

	list->stimulus = stimulus;
	ao2_lock(props);
	AST_LIST_INSERT_TAIL(&props->stimulus_queue, list, next);
	ast_cond_signal(&props->cond);
	ao2_unlock(props);
}

/*!
 * \brief Get a desired transfer party for a bridge the transferer is not in.
 *
 * \param bridge The bridge to get the party from. May be NULL.
 * \param[out] party The lone channel in the bridge. Will be set NULL if bridge is NULL or multiple parties are present.
 */
static void get_transfer_party_non_transferer_bridge(struct ast_bridge *bridge,
		struct ast_channel **party)
{
	if (bridge && bridge->num_channels == 1) {
		*party = ast_channel_ref(AST_LIST_FIRST(&bridge->channels)->chan);
	} else {
		*party = NULL;
	}
}

/*!
 * \brief Get the transferee and transfer target when the transferer is in a bridge with
 * one of the desired parties.
 *
 * \param transferer_bridge The bridge the transferer is in
 * \param other_bridge The bridge the transferer is not in. May be NULL.
 * \param transferer The transferer party
 * \param[out] transferer_peer The party that is in the bridge with the transferer
 * \param[out] other_party The party that is in the other_bridge
 */
static void get_transfer_parties_transferer_bridge(struct ast_bridge *transferer_bridge,
		struct ast_bridge *other_bridge, struct ast_channel *transferer,
		struct ast_channel **transferer_peer, struct ast_channel **other_party)
{
	*transferer_peer = ast_bridge_peer(transferer_bridge, transferer);
	get_transfer_party_non_transferer_bridge(other_bridge, other_party);
}

/*!
 * \brief determine transferee and transfer target for an attended transfer
 *
 * In builtin attended transfers, there is a single transferer channel that jumps between
 * the two bridges involved. At the time the attended transfer occurs, the transferer could
 * be in either bridge, so determining the parties is a bit more complex than normal.
 *
 * The method used here is to determine which of the two bridges the transferer is in, and
 * grabbing the peer from that bridge. The other bridge, if it only has a single channel in it,
 * has the other desired channel.
 *
 * \param transferer The channel performing the transfer
 * \param transferee_bridge The bridge that the transferee is in
 * \param target_bridge The bridge that the transfer target is in
 * \param[out] transferee The transferee channel
 * \param[out] transfer_target The transfer target channel
 */
static void get_transfer_parties(struct ast_channel *transferer, struct ast_bridge *transferee_bridge,
		struct ast_bridge *target_bridge, struct ast_channel **transferee,
		struct ast_channel **transfer_target)
{
	struct ast_bridge *transferer_bridge;

	ast_channel_lock(transferer);
	transferer_bridge = ast_channel_get_bridge(transferer);
	ast_channel_unlock(transferer);

	if (transferer_bridge == transferee_bridge) {
		get_transfer_parties_transferer_bridge(transferee_bridge, target_bridge,
				transferer, transferee, transfer_target);
	} else if (transferer_bridge == target_bridge) {
		get_transfer_parties_transferer_bridge(target_bridge, transferee_bridge,
				transferer, transfer_target, transferee);
	} else {
		get_transfer_party_non_transferer_bridge(transferee_bridge, transferee);
		get_transfer_party_non_transferer_bridge(target_bridge, transfer_target);
	}

	ao2_cleanup(transferer_bridge);
}

/*!
 * \brief Send a stasis publication for a successful attended transfer
 */
static void publish_transfer_success(struct attended_transfer_properties *props,
		struct ast_channel *transferee_channel, struct ast_channel *target_channel)
{
	struct ast_attended_transfer_message *transfer_msg;

	transfer_msg = ast_attended_transfer_message_create(0, props->transferer,
			props->transferee_bridge, props->transferer, props->target_bridge,
			transferee_channel, target_channel);

	if (!transfer_msg) {
		ast_log(LOG_ERROR, "Unable to publish successful attended transfer from %s\n",
				ast_channel_name(props->transferer));
		return;
	}

	ast_attended_transfer_message_add_merge(transfer_msg, props->transferee_bridge);
	ast_bridge_publish_attended_transfer(transfer_msg);
	ao2_cleanup(transfer_msg);
}

/*!
 * \brief Send a stasis publication for an attended transfer that ends in a threeway call
 */
static void publish_transfer_threeway(struct attended_transfer_properties *props,
		struct ast_channel *transferee_channel, struct ast_channel *target_channel)
{
	struct ast_attended_transfer_message *transfer_msg;

	transfer_msg = ast_attended_transfer_message_create(0, props->transferer,
			props->transferee_bridge, props->transferer, props->target_bridge,
			transferee_channel, target_channel);

	if (!transfer_msg) {
		ast_log(LOG_ERROR, "Unable to publish successful three-way transfer from %s\n",
				ast_channel_name(props->transferer));
		return;
	}

	ast_attended_transfer_message_add_threeway(transfer_msg, props->transferer,
			props->transferee_bridge);
	ast_bridge_publish_attended_transfer(transfer_msg);
	ao2_cleanup(transfer_msg);
}

/*!
 * \brief Send a stasis publication for a failed attended transfer
 */
static void publish_transfer_fail(struct attended_transfer_properties *props)
{
	struct ast_attended_transfer_message *transfer_msg;

	transfer_msg = ast_attended_transfer_message_create(0, props->transferer,
			props->transferee_bridge, props->transferer, props->target_bridge,
			NULL, NULL);

	if (!transfer_msg) {
		ast_log(LOG_ERROR, "Unable to publish failed transfer from %s\n",
				ast_channel_name(props->transferer));
		return;
	}

	transfer_msg->result = AST_BRIDGE_TRANSFER_FAIL;
	ast_bridge_publish_attended_transfer(transfer_msg);
	ao2_cleanup(transfer_msg);
}

/*!
 * \brief Helper method to play a sound on a channel in a bridge
 *
 * \param chan The channel to play the sound to
 * \param sound The sound to play
 */
static void play_sound(struct ast_channel *chan, const char *sound)
{
	RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);

	ast_channel_lock(chan);
	bridge_channel = ast_channel_get_bridge_channel(chan);
	ast_channel_unlock(chan);

	if (!bridge_channel) {
		return;
	}

	ast_bridge_channel_queue_playfile(bridge_channel, NULL, sound, NULL);
}

/*!
 * \brief Helper method to place a channel in a bridge on hold
 */
static void hold(struct ast_channel *chan)
{
	RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);

	if (chan) {
		ast_channel_lock(chan);
		bridge_channel = ast_channel_get_bridge_channel(chan);
		ast_channel_unlock(chan);

		ast_assert(bridge_channel != NULL);

		ast_bridge_channel_write_hold(bridge_channel, NULL);
	}
}

/*!
 * \brief Helper method to take a channel in a bridge off hold
 */
static void unhold(struct ast_channel *chan)
{
	RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);

	ast_channel_lock(chan);
	bridge_channel = ast_channel_get_bridge_channel(chan);
	ast_channel_unlock(chan);

	ast_assert(bridge_channel != NULL);

	ast_bridge_channel_write_unhold(bridge_channel);
}

/*!
 * \brief Helper method to send a ringing indication to a channel in a bridge
 */
static void ringing(struct ast_channel *chan)
{
	RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);

	ast_channel_lock(chan);
	bridge_channel = ast_channel_get_bridge_channel(chan);
	ast_channel_unlock(chan);

	ast_assert(bridge_channel != NULL);

	ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_RINGING, NULL, 0);
}

/*!
 * \brief Helper method to send a ringing indication to all channels in a bridge
 */
static void bridge_ringing(struct ast_bridge *bridge)
{
	struct ast_frame ringing = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_RINGING,
	};

	ast_bridge_queue_everyone_else(bridge, NULL, &ringing);
}

/*!
 * \brief Helper method to send a hold frame to all channels in a bridge
 */
static void bridge_hold(struct ast_bridge *bridge)
{
	struct ast_frame hold = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_HOLD,
	};

	ast_bridge_queue_everyone_else(bridge, NULL, &hold);
}

/*!
 * \brief Helper method to send an unhold frame to all channels in a bridge
 */
static void bridge_unhold(struct ast_bridge *bridge)
{
	struct ast_frame unhold = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = AST_CONTROL_UNHOLD,
	};

	ast_bridge_queue_everyone_else(bridge, NULL, &unhold);
}

/*!
 * \brief Wrapper for \ref bridge_do_move
 */
static int bridge_move(struct ast_bridge *dest, struct ast_bridge *src, struct ast_channel *channel, struct ast_channel *swap)
{
	int res;
	RAII_VAR(struct ast_bridge_channel *, bridge_channel, NULL, ao2_cleanup);

	ast_bridge_lock_both(src, dest);

	ast_channel_lock(channel);
	bridge_channel = ast_channel_get_bridge_channel(channel);
	ast_channel_unlock(channel);

	ast_assert(bridge_channel != NULL);

	ao2_lock(bridge_channel);
	bridge_channel->swap = swap;
	ao2_unlock(bridge_channel);

	res = bridge_do_move(dest, bridge_channel, 1, 0);

	ast_bridge_unlock(dest);
	ast_bridge_unlock(src);

	return res;
}

/*!
 * \brief Wrapper for \ref bridge_do_merge
 */
static void bridge_merge(struct ast_bridge *dest, struct ast_bridge *src, struct ast_channel **kick_channels, unsigned int num_channels)
{
	struct ast_bridge_channel **kick_bridge_channels = num_channels ?
		ast_alloca(num_channels * sizeof(*kick_bridge_channels)) : NULL;
	int i;
	int num_bridge_channels = 0;

	ast_bridge_lock_both(dest, src);

	for (i = 0; i < num_channels; ++i) {
		struct ast_bridge_channel *kick_bridge_channel;

		kick_bridge_channel = bridge_find_channel(src, kick_channels[i]);
		if (!kick_bridge_channel) {
			kick_bridge_channel = bridge_find_channel(dest, kick_channels[i]);
		}

		/* It's possible (and fine) for the bridge channel to be NULL at this point if the
		 * channel has hung up already. If that happens, we can just remove it from the list
		 * of bridge channels to kick from the bridge
		 */
		if (!kick_bridge_channel) {
			continue;
		}

		kick_bridge_channels[num_bridge_channels++] = kick_bridge_channel;
	}

	bridge_do_merge(dest, src, kick_bridge_channels, num_bridge_channels, 0);
	ast_bridge_unlock(dest);
	ast_bridge_unlock(src);
}

/*!
 * \brief Flags that indicate properties of attended transfer states
 */
enum attended_transfer_state_flags {
	/*! This state requires that the timer be reset when entering the state */
	TRANSFER_STATE_FLAG_TIMER_RESET = (1 << 0),
	/*! This state's timer uses atxferloopdelay */
	TRANSFER_STATE_FLAG_TIMER_LOOP_DELAY = (1 << 1),
	/*! This state's timer uses atxfernoanswertimeout */
	TRANSFER_STATE_FLAG_ATXFER_NO_ANSWER = (1 << 2),
	/*! This state has a time limit associated with it */
	TRANSFER_STATE_FLAG_TIMED = (TRANSFER_STATE_FLAG_TIMER_RESET |
			TRANSFER_STATE_FLAG_TIMER_LOOP_DELAY | TRANSFER_STATE_FLAG_ATXFER_NO_ANSWER),
	/*! This state does not transition to any other states */
	TRANSFER_STATE_FLAG_TERMINAL = (1 << 3),
};

static int calling_target_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state calling_target_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int hesitant_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state hesitant_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int rebridge_enter(struct attended_transfer_properties *props);

static int resume_enter(struct attended_transfer_properties *props);

static int threeway_enter(struct attended_transfer_properties *props);

static int consulting_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state consulting_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int double_checking_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state double_checking_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int complete_enter(struct attended_transfer_properties *props);

static int blond_enter(struct attended_transfer_properties *props);

static int blond_nonfinal_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state blond_nonfinal_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int recalling_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state recalling_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int wait_to_retransfer_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state wait_to_retransfer_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int retransfer_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state retransfer_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int wait_to_recall_enter(struct attended_transfer_properties *props);
static enum attended_transfer_state wait_to_recall_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus);

static int fail_enter(struct attended_transfer_properties *props);

/*!
 * \brief Properties of an attended transfer state
 */
struct attended_transfer_state_properties {
	/*! The name of the state. Used for debugging */
	const char *state_name;
	/*! Function used to enter a state */
	int (*enter)(struct attended_transfer_properties *props);
	/*!
	 * Function used to exit a state
	 * This is used both to determine what the next state
	 * to transition to will be and to perform any cleanup
	 * necessary before exiting the current state.
	 */
	enum attended_transfer_state (*exit)(struct attended_transfer_properties *props,
			enum attended_transfer_stimulus stimulus);
	/*! Flags associated with this state */
	enum attended_transfer_state_flags flags;
};

static const struct attended_transfer_state_properties state_properties[] = {
	[TRANSFER_CALLING_TARGET] = {
		.state_name = "Calling Target",
		.enter = calling_target_enter,
		.exit = calling_target_exit,
		.flags = TRANSFER_STATE_FLAG_ATXFER_NO_ANSWER | TRANSFER_STATE_FLAG_TIMER_RESET,
	},
	[TRANSFER_HESITANT] = {
		.state_name = "Hesitant",
		.enter = hesitant_enter,
		.exit = hesitant_exit,
		.flags = TRANSFER_STATE_FLAG_ATXFER_NO_ANSWER,
	},
	[TRANSFER_REBRIDGE] = {
		.state_name = "Rebridge",
		.enter = rebridge_enter,
		.flags = TRANSFER_STATE_FLAG_TERMINAL,
	},
	[TRANSFER_RESUME] = {
		.state_name = "Resume",
		.enter = resume_enter,
		.flags = TRANSFER_STATE_FLAG_TERMINAL,
	},
	[TRANSFER_THREEWAY] = {
		.state_name = "Threeway",
		.enter = threeway_enter,
		.flags = TRANSFER_STATE_FLAG_TERMINAL,
	},
	[TRANSFER_CONSULTING] = {
		.state_name = "Consulting",
		.enter = consulting_enter,
		.exit = consulting_exit,
	},
	[TRANSFER_DOUBLECHECKING] = {
		.state_name = "Double Checking",
		.enter = double_checking_enter,
		.exit = double_checking_exit,
	},
	[TRANSFER_COMPLETE] = {
		.state_name = "Complete",
		.enter = complete_enter,
		.flags = TRANSFER_STATE_FLAG_TERMINAL,
	},
	[TRANSFER_BLOND] = {
		.state_name = "Blond",
		.enter = blond_enter,
		.flags = TRANSFER_STATE_FLAG_TERMINAL,
	},
	[TRANSFER_BLOND_NONFINAL] = {
		.state_name = "Blond Non-Final",
		.enter = blond_nonfinal_enter,
		.exit = blond_nonfinal_exit,
		.flags = TRANSFER_STATE_FLAG_ATXFER_NO_ANSWER,
	},
	[TRANSFER_RECALLING] = {
		.state_name = "Recalling",
		.enter = recalling_enter,
		.exit = recalling_exit,
		.flags = TRANSFER_STATE_FLAG_ATXFER_NO_ANSWER | TRANSFER_STATE_FLAG_TIMER_RESET,
	},
	[TRANSFER_WAIT_TO_RETRANSFER] = {
		.state_name = "Wait to Retransfer",
		.enter = wait_to_retransfer_enter,
		.exit = wait_to_retransfer_exit,
		.flags = TRANSFER_STATE_FLAG_TIMER_RESET | TRANSFER_STATE_FLAG_TIMER_LOOP_DELAY,
	},
	[TRANSFER_RETRANSFER] = {
		.state_name = "Retransfer",
		.enter = retransfer_enter,
		.exit = retransfer_exit,
		.flags = TRANSFER_STATE_FLAG_ATXFER_NO_ANSWER | TRANSFER_STATE_FLAG_TIMER_RESET,
	},
	[TRANSFER_WAIT_TO_RECALL] = {
		.state_name = "Wait to Recall",
		.enter = wait_to_recall_enter,
		.exit = wait_to_recall_exit,
		.flags = TRANSFER_STATE_FLAG_TIMER_RESET | TRANSFER_STATE_FLAG_TIMER_LOOP_DELAY,
	},
	[TRANSFER_FAIL] = {
		.state_name = "Fail",
		.enter = fail_enter,
		.flags = TRANSFER_STATE_FLAG_TERMINAL,
	},
};

static int calling_target_enter(struct attended_transfer_properties *props)
{
	return bridge_move(props->target_bridge, props->transferee_bridge, props->transferer, NULL);
}

static enum attended_transfer_state calling_target_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		play_sound(props->transferer, props->failsound);
		publish_transfer_fail(props);
		return TRANSFER_FAIL;
	case STIMULUS_DTMF_ATXFER_COMPLETE:
	case STIMULUS_TRANSFERER_HANGUP:
		bridge_unhold(props->transferee_bridge);
		return props->atxferdropcall ? TRANSFER_BLOND : TRANSFER_BLOND_NONFINAL;
	case STIMULUS_TRANSFER_TARGET_ANSWER:
		return TRANSFER_CONSULTING;
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_TIMEOUT:
	case STIMULUS_DTMF_ATXFER_ABORT:
		play_sound(props->transferer, props->failsound);
		return TRANSFER_REBRIDGE;
	case STIMULUS_DTMF_ATXFER_THREEWAY:
		bridge_unhold(props->transferee_bridge);
		return TRANSFER_THREEWAY;
	case STIMULUS_DTMF_ATXFER_SWAP:
		return TRANSFER_HESITANT;
	case STIMULUS_NONE:
	case STIMULUS_RECALL_TARGET_ANSWER:
	case STIMULUS_RECALL_TARGET_HANGUP:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

static int hesitant_enter(struct attended_transfer_properties *props)
{
	if (bridge_move(props->transferee_bridge, props->target_bridge, props->transferer, NULL)) {
		return -1;
	}

	unhold(props->transferer);
	return 0;
}

static enum attended_transfer_state hesitant_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		play_sound(props->transferer, props->failsound);
		publish_transfer_fail(props);
		return TRANSFER_FAIL;
	case STIMULUS_DTMF_ATXFER_COMPLETE:
	case STIMULUS_TRANSFERER_HANGUP:
		return props->atxferdropcall ? TRANSFER_BLOND : TRANSFER_BLOND_NONFINAL;
	case STIMULUS_TRANSFER_TARGET_ANSWER:
		return TRANSFER_DOUBLECHECKING;
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_TIMEOUT:
	case STIMULUS_DTMF_ATXFER_ABORT:
		play_sound(props->transferer, props->failsound);
		return TRANSFER_RESUME;
	case STIMULUS_DTMF_ATXFER_THREEWAY:
		return TRANSFER_THREEWAY;
	case STIMULUS_DTMF_ATXFER_SWAP:
		hold(props->transferer);
		return TRANSFER_CALLING_TARGET;
	case STIMULUS_NONE:
	case STIMULUS_RECALL_TARGET_HANGUP:
	case STIMULUS_RECALL_TARGET_ANSWER:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

static int rebridge_enter(struct attended_transfer_properties *props)
{
	if (bridge_move(props->transferee_bridge, props->target_bridge,
			props->transferer, NULL)) {
		return -1;
	}

	unhold(props->transferer);
	return 0;
}

static int resume_enter(struct attended_transfer_properties *props)
{
	return 0;
}

static int threeway_enter(struct attended_transfer_properties *props)
{
	struct ast_channel *transferee_channel;
	struct ast_channel *target_channel;

	get_transfer_parties(props->transferer, props->transferee_bridge, props->target_bridge,
			&transferee_channel, &target_channel);
	bridge_merge(props->transferee_bridge, props->target_bridge, NULL, 0);
	play_sound(props->transfer_target, props->xfersound);
	play_sound(props->transferer, props->xfersound);
	publish_transfer_threeway(props, transferee_channel, target_channel);

	ast_channel_cleanup(transferee_channel);
	ast_channel_cleanup(target_channel);
	return 0;
}

static int consulting_enter(struct attended_transfer_properties *props)
{
	return 0;
}

static enum attended_transfer_state consulting_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		/* This is a one-of-a-kind event. The transferer and transfer target are talking in
		 * one bridge, and the transferee has hung up in a separate bridge. In this case, we
		 * will change the personality of the transfer target bridge back to normal, and play
		 * a sound to the transferer to indicate the transferee is gone.
		 */
		bridge_basic_change_personality(props->target_bridge, BRIDGE_BASIC_PERSONALITY_NORMAL, NULL);
		play_sound(props->transferer, props->failsound);
		ast_bridge_merge_inhibit(props->target_bridge, -1);
		/* These next two lines are here to ensure that our reference to the target bridge
		 * is cleaned up properly and that the target bridge is not destroyed when the
		 * monitor thread exits
		 */
		ao2_ref(props->target_bridge, -1);
		props->target_bridge = NULL;
		return TRANSFER_FAIL;
	case STIMULUS_TRANSFERER_HANGUP:
	case STIMULUS_DTMF_ATXFER_COMPLETE:
		/* We know the transferer is in the target_bridge, so take the other bridge off hold */
		bridge_unhold(props->transferee_bridge);
		return TRANSFER_COMPLETE;
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_DTMF_ATXFER_ABORT:
		play_sound(props->transferer, props->failsound);
		return TRANSFER_REBRIDGE;
	case STIMULUS_DTMF_ATXFER_THREEWAY:
		bridge_unhold(props->transferee_bridge);
		return TRANSFER_THREEWAY;
	case STIMULUS_DTMF_ATXFER_SWAP:
		hold(props->transferer);
		bridge_move(props->transferee_bridge, props->target_bridge, props->transferer, NULL);
		unhold(props->transferer);
		return TRANSFER_DOUBLECHECKING;
	case STIMULUS_NONE:
	case STIMULUS_TIMEOUT:
	case STIMULUS_TRANSFER_TARGET_ANSWER:
	case STIMULUS_RECALL_TARGET_HANGUP:
	case STIMULUS_RECALL_TARGET_ANSWER:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

static int double_checking_enter(struct attended_transfer_properties *props)
{
	return 0;
}

static enum attended_transfer_state double_checking_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		play_sound(props->transferer, props->failsound);
		publish_transfer_fail(props);
		return TRANSFER_FAIL;
	case STIMULUS_TRANSFERER_HANGUP:
	case STIMULUS_DTMF_ATXFER_COMPLETE:
		/* We know the transferer is in the transferee, so take the other bridge off hold */
		bridge_unhold(props->target_bridge);
		return TRANSFER_COMPLETE;
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_DTMF_ATXFER_ABORT:
		play_sound(props->transferer, props->failsound);
		return TRANSFER_RESUME;
	case STIMULUS_DTMF_ATXFER_THREEWAY:
		bridge_unhold(props->target_bridge);
		return TRANSFER_THREEWAY;
	case STIMULUS_DTMF_ATXFER_SWAP:
		hold(props->transferer);
		bridge_move(props->target_bridge, props->transferee_bridge, props->transferer, NULL);
		unhold(props->transferer);
		return TRANSFER_CONSULTING;
	case STIMULUS_NONE:
	case STIMULUS_TIMEOUT:
	case STIMULUS_TRANSFER_TARGET_ANSWER:
	case STIMULUS_RECALL_TARGET_HANGUP:
	case STIMULUS_RECALL_TARGET_ANSWER:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

static int complete_enter(struct attended_transfer_properties *props)
{
	struct ast_channel *transferee_channel;
	struct ast_channel *target_channel;

	get_transfer_parties(props->transferer, props->transferee_bridge, props->target_bridge,
			&transferee_channel, &target_channel);
	bridge_merge(props->transferee_bridge, props->target_bridge, &props->transferer, 1);
	play_sound(props->transfer_target, props->xfersound);
	publish_transfer_success(props, transferee_channel, target_channel);

	ast_channel_cleanup(transferee_channel);
	ast_channel_cleanup(target_channel);
	return 0;
}

static int blond_enter(struct attended_transfer_properties *props)
{
	struct ast_channel *transferee_channel;
	struct ast_channel *target_channel;

	get_transfer_parties(props->transferer, props->transferee_bridge, props->target_bridge,
			&transferee_channel, &target_channel);
	bridge_merge(props->transferee_bridge, props->target_bridge, &props->transferer, 1);
	ringing(props->transfer_target);
	publish_transfer_success(props, transferee_channel, target_channel);

	ast_channel_cleanup(transferee_channel);
	ast_channel_cleanup(target_channel);
	return 0;
}

static int blond_nonfinal_enter(struct attended_transfer_properties *props)
{
	int res;
	props->superstate = SUPERSTATE_RECALL;
	/* move the transfer target to the recall target along with its reference */
	props->recall_target = ast_channel_ref(props->transfer_target);
	res = blond_enter(props);
	props->transfer_target = ast_channel_unref(props->transfer_target);
	return res;
}

static enum attended_transfer_state blond_nonfinal_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		return TRANSFER_FAIL;
	case STIMULUS_RECALL_TARGET_ANSWER:
		return TRANSFER_RESUME;
	case STIMULUS_TIMEOUT:
		ast_softhangup(props->recall_target, AST_SOFTHANGUP_EXPLICIT);
	case STIMULUS_RECALL_TARGET_HANGUP:
		props->recall_target = ast_channel_unref(props->recall_target);
		return TRANSFER_RECALLING;
	case STIMULUS_NONE:
	case STIMULUS_DTMF_ATXFER_ABORT:
	case STIMULUS_DTMF_ATXFER_COMPLETE:
	case STIMULUS_DTMF_ATXFER_THREEWAY:
	case STIMULUS_DTMF_ATXFER_SWAP:
	case STIMULUS_TRANSFERER_HANGUP:
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_TRANSFER_TARGET_ANSWER:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

/*!
 * \brief Dial callback when attempting to recall the original transferer channel
 *
 * This is how we can monitor if the recall target has answered or has hung up.
 * If one of the two is detected, then an appropriate stimulus is sent to the
 * attended transfer monitor thread.
 */
static void recall_callback(struct ast_dial *dial)
{
	struct attended_transfer_properties *props = ast_dial_get_user_data(dial);

	switch (ast_dial_state(dial)) {
	default:
	case AST_DIAL_RESULT_INVALID:
	case AST_DIAL_RESULT_FAILED:
	case AST_DIAL_RESULT_TIMEOUT:
	case AST_DIAL_RESULT_HANGUP:
	case AST_DIAL_RESULT_UNANSWERED:
		/* Failure cases */
		stimulate_attended_transfer(props, STIMULUS_RECALL_TARGET_HANGUP);
		break;
	case AST_DIAL_RESULT_RINGING:
	case AST_DIAL_RESULT_PROGRESS:
	case AST_DIAL_RESULT_PROCEEDING:
	case AST_DIAL_RESULT_TRYING:
		/* Don't care about these cases */
		break;
	case AST_DIAL_RESULT_ANSWERED:
		/* We struck gold! */
		props->recall_target = ast_dial_answered_steal(dial);
		stimulate_attended_transfer(props, STIMULUS_RECALL_TARGET_ANSWER);
		break;
	}
}

/*!
 * \internal
 * \brief Setup common things to transferrer and transfer_target recall channels.
 *
 * \param recall Channel for recalling a party.
 * \param transferer Channel supplying recall information.
 *
 * \details
 * Setup callid, variables, datastores, accountcode, and peeraccount.
 *
 * \pre Both channels are locked on entry.
 *
 * \pre COLP and CLID on the recall channel are setup by the caller but not
 * explicitly published yet.
 *
 * \return Nothing
 */
static void common_recall_channel_setup(struct ast_channel *recall, struct ast_channel *transferer)
{
	ast_callid callid;

	callid = ast_read_threadstorage_callid();
	if (callid) {
		ast_channel_callid_set(recall, callid);
	}

	ast_channel_inherit_variables(transferer, recall);
	ast_channel_datastore_inherit(transferer, recall);

	/*
	 * Stage a snapshot to ensure that a snapshot is always done
	 * on the recall channel so earler COLP and CLID setup will
	 * get published.
	 */
	ast_channel_stage_snapshot(recall);
	ast_channel_req_accountcodes(recall, transferer, AST_CHANNEL_REQUESTOR_REPLACEMENT);
	ast_channel_stage_snapshot_done(recall);
}

static int recalling_enter(struct attended_transfer_properties *props)
{
	RAII_VAR(struct ast_format_cap *, cap, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	struct ast_channel *recall;

	if (!cap) {
		return -1;
	}

	ast_format_cap_append(cap, ast_format_slin, 0);

	/* When we dial the transfer target, since we are communicating
	 * with a local channel, we can place the local channel in a bridge
	 * and then call out to it. When recalling the transferer, though, we
	 * have to use the dialing API because the channel is not local.
	 */
	props->dial = ast_dial_create();
	if (!props->dial) {
		return -1;
	}

	if (ast_dial_append(props->dial, props->transferer_type, props->transferer_addr, NULL)) {
		return -1;
	}

	if (ast_dial_prerun(props->dial, NULL, cap)) {
		return -1;
	}

	/*
	 * Setup callid, variables, datastores, accountcode, peeraccount,
	 * COLP, and CLID on the recalled transferrer.
	 */
	recall = ast_dial_get_channel(props->dial, 0);
	if (!recall) {
		return -1;
	}
	ast_channel_lock_both(recall, props->transferer);

	ast_party_caller_copy(ast_channel_caller(recall),
		ast_channel_caller(props->transferer));
	ast_party_connected_line_copy(ast_channel_connected(recall),
		&props->original_transferer_colp);

	common_recall_channel_setup(recall, props->transferer);
	ast_channel_unlock(recall);
	ast_channel_unlock(props->transferer);

	ast_dial_set_state_callback(props->dial, recall_callback);

	ao2_ref(props, +1);
	ast_dial_set_user_data(props->dial, props);

	if (ast_dial_run(props->dial, NULL, 1) == AST_DIAL_RESULT_FAILED) {
		ao2_ref(props, -1);
		return -1;
	}

	bridge_ringing(props->transferee_bridge);
	return 0;
}

static enum attended_transfer_state recalling_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	/* No matter what the outcome was, we need to kill off the dial */
	ast_dial_join(props->dial);
	ast_dial_destroy(props->dial);
	props->dial = NULL;
	/* This reference is the one we incremented for the dial state callback (recall_callback) to use */
	ao2_ref(props, -1);

	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		return TRANSFER_FAIL;
	case STIMULUS_TIMEOUT:
	case STIMULUS_RECALL_TARGET_HANGUP:
		++props->retry_attempts;
		if (props->retry_attempts >= props->atxfercallbackretries) {
			return TRANSFER_FAIL;
		}
		if (props->atxferloopdelay) {
			return TRANSFER_WAIT_TO_RETRANSFER;
		}
		return TRANSFER_RETRANSFER;
	case STIMULUS_RECALL_TARGET_ANSWER:
		/* Setting this datastore up will allow the transferer to have all of his
		 * call features set up automatically when the bridge changes back to a
		 * normal personality
		 */
		ast_bridge_features_ds_set(props->recall_target, &props->transferer_features);
		ast_channel_ref(props->recall_target);
		if (ast_bridge_impart(props->transferee_bridge, props->recall_target, NULL, NULL,
			AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
			ast_hangup(props->recall_target);
			ast_channel_unref(props->recall_target);
			return TRANSFER_FAIL;
		}
		return TRANSFER_RESUME;
	case STIMULUS_NONE:
	case STIMULUS_DTMF_ATXFER_ABORT:
	case STIMULUS_DTMF_ATXFER_COMPLETE:
	case STIMULUS_DTMF_ATXFER_THREEWAY:
	case STIMULUS_DTMF_ATXFER_SWAP:
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_TRANSFER_TARGET_ANSWER:
	case STIMULUS_TRANSFERER_HANGUP:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

static int wait_to_retransfer_enter(struct attended_transfer_properties *props)
{
	bridge_hold(props->transferee_bridge);
	return 0;
}

static enum attended_transfer_state wait_to_retransfer_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	bridge_unhold(props->transferee_bridge);
	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		return TRANSFER_FAIL;
	case STIMULUS_TIMEOUT:
		return TRANSFER_RETRANSFER;
	case STIMULUS_NONE:
	case STIMULUS_DTMF_ATXFER_ABORT:
	case STIMULUS_DTMF_ATXFER_COMPLETE:
	case STIMULUS_DTMF_ATXFER_THREEWAY:
	case STIMULUS_DTMF_ATXFER_SWAP:
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_TRANSFER_TARGET_ANSWER:
	case STIMULUS_TRANSFERER_HANGUP:
	case STIMULUS_RECALL_TARGET_HANGUP:
	case STIMULUS_RECALL_TARGET_ANSWER:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

static int attach_framehook(struct attended_transfer_properties *props, struct ast_channel *channel);

static int retransfer_enter(struct attended_transfer_properties *props)
{
	RAII_VAR(struct ast_format_cap *, cap, ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT), ao2_cleanup);
	char destination[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2];
	int cause;

	if (!cap) {
		return -1;
	}

	snprintf(destination, sizeof(destination), "%s@%s", props->exten, props->context);

	ast_format_cap_append(cap, ast_format_slin, 0);

	/* Get a channel that is the destination we wish to call */
	props->recall_target = ast_request("Local", cap, NULL, NULL, destination, &cause);
	if (!props->recall_target) {
		ast_log(LOG_ERROR, "Unable to request outbound channel for recall target\n");
		return -1;
	}

	if (attach_framehook(props, props->recall_target)) {
		ast_log(LOG_ERROR, "Unable to attach framehook to recall target\n");
		ast_hangup(props->recall_target);
		props->recall_target = NULL;
		return -1;
	}

	/*
	 * Setup callid, variables, datastores, accountcode, peeraccount,
	 * and COLP on the recalled transfer target.
	 */
	ast_channel_lock_both(props->recall_target, props->transferer);

	ast_party_connected_line_copy(ast_channel_connected(props->recall_target),
		&props->original_transferer_colp);
	ast_party_id_reset(&ast_channel_connected(props->recall_target)->priv);

	common_recall_channel_setup(props->recall_target, props->recall_target);
	ast_channel_unlock(props->recall_target);
	ast_channel_unlock(props->transferer);

	if (ast_call(props->recall_target, destination, 0)) {
		ast_log(LOG_ERROR, "Unable to place outbound call to recall target\n");
		ast_hangup(props->recall_target);
		props->recall_target = NULL;
		return -1;
	}

	ast_channel_ref(props->recall_target);
	if (ast_bridge_impart(props->transferee_bridge, props->recall_target, NULL, NULL,
		AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
		ast_log(LOG_ERROR, "Unable to place recall target into bridge\n");
		ast_hangup(props->recall_target);
		ast_channel_unref(props->recall_target);
		return -1;
	}

	return 0;
}

static enum attended_transfer_state retransfer_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		return TRANSFER_FAIL;
	case STIMULUS_TIMEOUT:
		ast_softhangup(props->recall_target, AST_SOFTHANGUP_EXPLICIT);
	case STIMULUS_RECALL_TARGET_HANGUP:
		props->recall_target = ast_channel_unref(props->recall_target);
		if (props->atxferloopdelay) {
			return TRANSFER_WAIT_TO_RECALL;
		}
		return TRANSFER_RECALLING;
	case STIMULUS_RECALL_TARGET_ANSWER:
		return TRANSFER_RESUME;
	case STIMULUS_NONE:
	case STIMULUS_DTMF_ATXFER_ABORT:
	case STIMULUS_DTMF_ATXFER_COMPLETE:
	case STIMULUS_DTMF_ATXFER_THREEWAY:
	case STIMULUS_DTMF_ATXFER_SWAP:
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_TRANSFER_TARGET_ANSWER:
	case STIMULUS_TRANSFERER_HANGUP:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

static int wait_to_recall_enter(struct attended_transfer_properties *props)
{
	bridge_hold(props->transferee_bridge);
	return 0;
}

static enum attended_transfer_state wait_to_recall_exit(struct attended_transfer_properties *props,
		enum attended_transfer_stimulus stimulus)
{
	bridge_unhold(props->transferee_bridge);
	switch (stimulus) {
	case STIMULUS_TRANSFEREE_HANGUP:
		return TRANSFER_FAIL;
	case STIMULUS_TIMEOUT:
		return TRANSFER_RECALLING;
	case STIMULUS_NONE:
	case STIMULUS_DTMF_ATXFER_ABORT:
	case STIMULUS_DTMF_ATXFER_COMPLETE:
	case STIMULUS_DTMF_ATXFER_THREEWAY:
	case STIMULUS_DTMF_ATXFER_SWAP:
	case STIMULUS_TRANSFER_TARGET_HANGUP:
	case STIMULUS_TRANSFER_TARGET_ANSWER:
	case STIMULUS_TRANSFERER_HANGUP:
	case STIMULUS_RECALL_TARGET_HANGUP:
	case STIMULUS_RECALL_TARGET_ANSWER:
	default:
		ast_log(LOG_WARNING, "Unexpected stimulus '%s' received in attended transfer state '%s'\n",
				stimulus_strs[stimulus], state_properties[props->state].state_name);
		return props->state;
	}
}

static int fail_enter(struct attended_transfer_properties *props)
{
	if (props->transferee_bridge) {
		ast_bridge_destroy(props->transferee_bridge, 0);
		props->transferee_bridge = NULL;
	}
	return 0;
}

/*!
 * \brief DTMF hook when transferer presses abort sequence.
 *
 * Sends a stimulus to the attended transfer monitor thread that the abort sequence has been pressed
 */
static int atxfer_abort(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct attended_transfer_properties *props = hook_pvt;

	ast_debug(1, "Transferer on attended transfer %p pressed abort sequence\n", props);
	stimulate_attended_transfer(props, STIMULUS_DTMF_ATXFER_ABORT);
	return 0;
}

/*!
 * \brief DTMF hook when transferer presses complete sequence.
 *
 * Sends a stimulus to the attended transfer monitor thread that the complete sequence has been pressed
 */
static int atxfer_complete(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct attended_transfer_properties *props = hook_pvt;

	ast_debug(1, "Transferer on attended transfer %p pressed complete sequence\n", props);
	stimulate_attended_transfer(props, STIMULUS_DTMF_ATXFER_COMPLETE);
	return 0;
}

/*!
 * \brief DTMF hook when transferer presses threeway sequence.
 *
 * Sends a stimulus to the attended transfer monitor thread that the threeway sequence has been pressed
 */
static int atxfer_threeway(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct attended_transfer_properties *props = hook_pvt;

	ast_debug(1, "Transferer on attended transfer %p pressed threeway sequence\n", props);
	stimulate_attended_transfer(props, STIMULUS_DTMF_ATXFER_THREEWAY);
	return 0;
}

/*!
 * \brief DTMF hook when transferer presses swap sequence.
 *
 * Sends a stimulus to the attended transfer monitor thread that the swap sequence has been pressed
 */
static int atxfer_swap(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct attended_transfer_properties *props = hook_pvt;

	ast_debug(1, "Transferer on attended transfer %p pressed swap sequence\n", props);
	stimulate_attended_transfer(props, STIMULUS_DTMF_ATXFER_SWAP);
	return 0;
}

/*!
 * \brief Hangup hook for transferer channel.
 *
 * Sends a stimulus to the attended transfer monitor thread that the transferer has hung up.
 */
static int atxfer_transferer_hangup(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct attended_transfer_properties *props = hook_pvt;

	ast_debug(1, "Transferer on attended transfer %p hung up\n", props);
	stimulate_attended_transfer(props, STIMULUS_TRANSFERER_HANGUP);
	return 0;
}

/*!
 * \brief Frame hook for transfer target channel
 *
 * This is used to determine if the transfer target or recall target has answered
 * the outgoing call.
 *
 * When an answer is detected, a stimulus is sent to the attended transfer monitor
 * thread to indicate that the transfer target or recall target has answered.
 *
 * \param chan The channel the framehook is attached to.
 * \param frame The frame being read or written.
 * \param event What is being done with the frame.
 * \param data The attended transfer properties.
 */
static struct ast_frame *transfer_target_framehook_cb(struct ast_channel *chan,
		struct ast_frame *frame, enum ast_framehook_event event, void *data)
{
	struct attended_transfer_properties *props = data;

	if (event == AST_FRAMEHOOK_EVENT_READ &&
			frame && frame->frametype == AST_FRAME_CONTROL &&
			frame->subclass.integer == AST_CONTROL_ANSWER) {

		ast_debug(1, "Detected an answer for recall attempt on attended transfer %p\n", props);
		if (props->superstate == SUPERSTATE_TRANSFER) {
			stimulate_attended_transfer(props, STIMULUS_TRANSFER_TARGET_ANSWER);
		} else {
			stimulate_attended_transfer(props, STIMULUS_RECALL_TARGET_ANSWER);
		}
		ast_framehook_detach(chan, props->target_framehook_id);
		props->target_framehook_id = -1;
	}

	return frame;
}

/*! \brief Callback function which informs upstream if we are consuming a frame of a specific type */
static int transfer_target_framehook_consume(void *data, enum ast_frame_type type)
{
	return (type == AST_FRAME_CONTROL ? 1 : 0);
}

static void transfer_target_framehook_destroy_cb(void *data)
{
	struct attended_transfer_properties *props = data;
	ao2_cleanup(props);
}

static int bridge_personality_atxfer_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	const char *abort_dtmf;
	const char *complete_dtmf;
	const char *threeway_dtmf;
	const char *swap_dtmf;
	struct bridge_basic_personality *personality = self->personality;

	if (!ast_channel_has_role(bridge_channel->chan, AST_TRANSFERER_ROLE_NAME)) {
		return 0;
	}

	abort_dtmf = ast_channel_get_role_option(bridge_channel->chan, AST_TRANSFERER_ROLE_NAME, "abort");
	complete_dtmf = ast_channel_get_role_option(bridge_channel->chan, AST_TRANSFERER_ROLE_NAME, "complete");
	threeway_dtmf = ast_channel_get_role_option(bridge_channel->chan, AST_TRANSFERER_ROLE_NAME, "threeway");
	swap_dtmf = ast_channel_get_role_option(bridge_channel->chan, AST_TRANSFERER_ROLE_NAME, "swap");

	if (!ast_strlen_zero(abort_dtmf) && ast_bridge_dtmf_hook(bridge_channel->features,
			abort_dtmf, atxfer_abort, personality->details[personality->current].pvt, NULL,
			AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE | AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
		return -1;
	}
	if (!ast_strlen_zero(complete_dtmf) && ast_bridge_dtmf_hook(bridge_channel->features,
			complete_dtmf, atxfer_complete, personality->details[personality->current].pvt, NULL,
			AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE | AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
		return -1;
	}
	if (!ast_strlen_zero(threeway_dtmf) && ast_bridge_dtmf_hook(bridge_channel->features,
			threeway_dtmf, atxfer_threeway, personality->details[personality->current].pvt, NULL,
			AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE | AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
		return -1;
	}
	if (!ast_strlen_zero(swap_dtmf) && ast_bridge_dtmf_hook(bridge_channel->features,
			swap_dtmf, atxfer_swap, personality->details[personality->current].pvt, NULL,
			AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE | AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
		return -1;
	}
	if (ast_bridge_hangup_hook(bridge_channel->features, atxfer_transferer_hangup,
			personality->details[personality->current].pvt, NULL,
			AST_BRIDGE_HOOK_REMOVE_ON_PERSONALITY_CHANGE | AST_BRIDGE_HOOK_REMOVE_ON_PULL)) {
		return -1;
	}

	return 0;
}

static void transfer_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct attended_transfer_properties *props)
{
	if (self->num_channels > 1 || bridge_channel->state == BRIDGE_CHANNEL_STATE_WAIT) {
		return;
	}

	if (self->num_channels == 1) {
		RAII_VAR(struct ast_bridge_channel *, transferer_bridge_channel, NULL, ao2_cleanup);

		ast_channel_lock(props->transferer);
		transferer_bridge_channel = ast_channel_get_bridge_channel(props->transferer);
		ast_channel_unlock(props->transferer);

		if (!transferer_bridge_channel) {
			return;
		}

		if (AST_LIST_FIRST(&self->channels) != transferer_bridge_channel) {
			return;
		}
	}

	/* Reaching this point means that either
	 * 1) The bridge has no channels in it
	 * 2) The bridge has one channel, and it's the transferer
	 * In either case, it indicates that the non-transferer parties
	 * are no longer in the bridge.
	 */
	if (self == props->transferee_bridge) {
		stimulate_attended_transfer(props, STIMULUS_TRANSFEREE_HANGUP);
	} else {
		stimulate_attended_transfer(props, STIMULUS_TRANSFER_TARGET_HANGUP);
	}
}

static void recall_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct attended_transfer_properties *props)
{
	if (self == props->target_bridge) {
		/* Once we're in the recall superstate, we no longer care about this bridge */
		return;
	}

	if (bridge_channel->chan == props->recall_target) {
		stimulate_attended_transfer(props, STIMULUS_RECALL_TARGET_HANGUP);
		return;
	}

	if (self->num_channels == 0) {
		/* Empty bridge means all transferees are gone for sure */
		stimulate_attended_transfer(props, STIMULUS_TRANSFEREE_HANGUP);
		return;
	}

	if (self->num_channels == 1) {
		RAII_VAR(struct ast_bridge_channel *, target_bridge_channel, NULL, ao2_cleanup);
		if (!props->recall_target) {
			/* No recall target means that the pull happened on a transferee. If there's still
			 * a channel left in the bridge, we don't need to send a stimulus
			 */
			return;
		}

		ast_channel_lock(props->recall_target);
		target_bridge_channel = ast_channel_get_bridge_channel(props->recall_target);
		ast_channel_unlock(props->recall_target);

		if (!target_bridge_channel) {
			return;
		}

		if (AST_LIST_FIRST(&self->channels) == target_bridge_channel) {
			stimulate_attended_transfer(props, STIMULUS_TRANSFEREE_HANGUP);
		}
	}
}

static void bridge_personality_atxfer_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	struct bridge_basic_personality *personality = self->personality;
	struct attended_transfer_properties *props = personality->details[personality->current].pvt;

	switch (props->superstate) {
	case SUPERSTATE_TRANSFER:
		transfer_pull(self, bridge_channel, props);
		break;
	case SUPERSTATE_RECALL:
		recall_pull(self, bridge_channel, props);
		break;
	}
}

static enum attended_transfer_stimulus wait_for_stimulus(struct attended_transfer_properties *props)
{
	RAII_VAR(struct stimulus_list *, list, NULL, ast_free_ptr);
	SCOPED_MUTEX(lock, ao2_object_get_lockaddr(props));

	while (!(list = AST_LIST_REMOVE_HEAD(&props->stimulus_queue, next))) {
		if (!(state_properties[props->state].flags & TRANSFER_STATE_FLAG_TIMED)) {
			ast_cond_wait(&props->cond, lock);
		} else {
			struct timeval relative_timeout = { 0, };
			struct timeval absolute_timeout;
			struct timespec timeout_arg;

			if (state_properties[props->state].flags & TRANSFER_STATE_FLAG_TIMER_RESET) {
				props->start = ast_tvnow();
			}

			if (state_properties[props->state].flags & TRANSFER_STATE_FLAG_TIMER_LOOP_DELAY) {
				relative_timeout.tv_sec = props->atxferloopdelay;
			} else {
				/* Implied TRANSFER_STATE_FLAG_TIMER_ATXFER_NO_ANSWER */
				relative_timeout.tv_sec = props->atxfernoanswertimeout;
			}

			absolute_timeout = ast_tvadd(props->start, relative_timeout);
			timeout_arg.tv_sec = absolute_timeout.tv_sec;
			timeout_arg.tv_nsec = absolute_timeout.tv_usec * 1000;

			if (ast_cond_timedwait(&props->cond, lock, &timeout_arg) == ETIMEDOUT) {
				return STIMULUS_TIMEOUT;
			}
		}
	}
	return list->stimulus;
}

/*!
 * \brief The main loop for the attended transfer monitor thread.
 *
 * This loop runs continuously until the attended transfer reaches
 * a terminal state. Stimuli for changes in the attended transfer
 * state are handled in this thread so that all factors in an
 * attended transfer can be handled in an orderly fashion.
 *
 * \param data The attended transfer properties
 */
static void *attended_transfer_monitor_thread(void *data)
{
	struct attended_transfer_properties *props = data;
	ast_callid callid;

	/*
	 * Set thread callid to the transferer's callid because we
	 * are doing all this on that channel's behalf.
	 */
	ast_channel_lock(props->transferer);
	callid = ast_channel_callid(props->transferer);
	ast_channel_unlock(props->transferer);
	if (callid) {
		ast_callid_threadassoc_add(callid);
	}

	for (;;) {
		enum attended_transfer_stimulus stimulus;

		ast_debug(1, "About to enter state %s for attended transfer %p\n", state_properties[props->state].state_name, props);

		if (state_properties[props->state].enter &&
				state_properties[props->state].enter(props)) {
			ast_log(LOG_ERROR, "State %s enter function returned an error for attended transfer %p\n",
					state_properties[props->state].state_name, props);
			break;
		}

		if (state_properties[props->state].flags & TRANSFER_STATE_FLAG_TERMINAL) {
			ast_debug(1, "State %s is a terminal state. Ending attended transfer %p\n",
					state_properties[props->state].state_name, props);
			break;
		}

		stimulus = wait_for_stimulus(props);

		ast_debug(1, "Received stimulus %s on attended transfer %p\n", stimulus_strs[stimulus], props);

		ast_assert(state_properties[props->state].exit != NULL);

		props->state = state_properties[props->state].exit(props, stimulus);

		ast_debug(1, "Told to enter state %s exit on attended transfer %p\n", state_properties[props->state].state_name, props);
	}

	attended_transfer_properties_shutdown(props);

	if (callid) {
		ast_callid_threadassoc_remove();
	}

	return NULL;
}

static int attach_framehook(struct attended_transfer_properties *props, struct ast_channel *channel)
{
	struct ast_framehook_interface target_interface = {
		.version = AST_FRAMEHOOK_INTERFACE_VERSION,
		.event_cb = transfer_target_framehook_cb,
		.destroy_cb = transfer_target_framehook_destroy_cb,
		.consume_cb = transfer_target_framehook_consume,
		.disable_inheritance = 1,
	};

	ao2_ref(props, +1);
	target_interface.data = props;

	props->target_framehook_id = ast_framehook_attach(channel, &target_interface);
	if (props->target_framehook_id == -1) {
		ao2_ref(props, -1);
		return -1;
	}
	return 0;
}

static int add_transferer_role(struct ast_channel *chan, struct ast_bridge_features_attended_transfer *attended_transfer)
{
	const char *atxfer_abort;
	const char *atxfer_threeway;
	const char *atxfer_complete;
	const char *atxfer_swap;
	RAII_VAR(struct ast_features_xfer_config *, xfer_cfg, NULL, ao2_cleanup);
	SCOPED_CHANNELLOCK(lock, chan);

	xfer_cfg = ast_get_chan_features_xfer_config(chan);
	if (!xfer_cfg) {
		return -1;
	}
	if (attended_transfer) {
		atxfer_abort = ast_strdupa(S_OR(attended_transfer->abort, xfer_cfg->atxferabort));
		atxfer_threeway = ast_strdupa(S_OR(attended_transfer->threeway, xfer_cfg->atxferthreeway));
		atxfer_complete = ast_strdupa(S_OR(attended_transfer->complete, xfer_cfg->atxfercomplete));
		atxfer_swap = ast_strdupa(S_OR(attended_transfer->swap, xfer_cfg->atxferswap));
	} else {
		atxfer_abort = ast_strdupa(xfer_cfg->atxferabort);
		atxfer_threeway = ast_strdupa(xfer_cfg->atxferthreeway);
		atxfer_complete = ast_strdupa(xfer_cfg->atxfercomplete);
		atxfer_swap = ast_strdupa(xfer_cfg->atxferswap);
	}

	return ast_channel_add_bridge_role(chan, AST_TRANSFERER_ROLE_NAME) ||
		ast_channel_set_bridge_role_option(chan, AST_TRANSFERER_ROLE_NAME, "abort", atxfer_abort) ||
		ast_channel_set_bridge_role_option(chan, AST_TRANSFERER_ROLE_NAME, "complete", atxfer_complete) ||
		ast_channel_set_bridge_role_option(chan, AST_TRANSFERER_ROLE_NAME, "threeway", atxfer_threeway) ||
		ast_channel_set_bridge_role_option(chan, AST_TRANSFERER_ROLE_NAME, "swap", atxfer_swap);
}

/*!
 * \brief Helper function that presents dialtone and grabs extension
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int grab_transfer(struct ast_channel *chan, char *exten, size_t exten_len, const char *context)
{
	int res;
	int digit_timeout;
	int attempts = 0;
	int max_attempts;
	RAII_VAR(struct ast_features_xfer_config *, xfer_cfg, NULL, ao2_cleanup);
	char *retry_sound;
	char *invalid_sound;

	ast_channel_lock(chan);
	xfer_cfg = ast_get_chan_features_xfer_config(chan);
	if (!xfer_cfg) {
		ast_log(LOG_ERROR, "Unable to get transfer configuration\n");
		ast_channel_unlock(chan);
		return -1;
	}
	digit_timeout = xfer_cfg->transferdigittimeout * 1000;
	max_attempts = xfer_cfg->transferdialattempts;
	retry_sound = ast_strdupa(xfer_cfg->transferretrysound);
	invalid_sound = ast_strdupa(xfer_cfg->transferinvalidsound);
	ast_channel_unlock(chan);

	/* Play the simple "transfer" prompt out and wait */
	res = ast_stream_and_wait(chan, "pbx-transfer", AST_DIGIT_ANY);
	ast_stopstream(chan);
	if (res < 0) {
		/* Hangup or error */
		return -1;
	}
	if (res) {
		/* Store the DTMF digit that interrupted playback of the file. */
		exten[0] = res;
	}

	/* Drop to dialtone so they can enter the extension they want to transfer to */
	do {
		++attempts;

		ast_test_suite_event_notify("TRANSFER_BEGIN_DIAL",
				"Channel: %s\r\n"
				"Attempt: %d",
				ast_channel_name(chan), attempts);
		res = ast_app_dtget(chan, context, exten, exten_len, exten_len - 1, digit_timeout);
		ast_test_suite_event_notify("TRANSFER_DIALLED",
				"Channel: %s\r\n"
				"Attempt: %d\r\n"
				"Dialled: %s\r\n"
				"Result: %s",
				ast_channel_name(chan), attempts, exten, res > 0 ? "Success" : "Failure");
		if (res < 0) {
			/* Hangup or error */
			res = -1;
		} else if (!res) {
			/* 0 for invalid extension dialed. */
			if (ast_strlen_zero(exten)) {
				ast_debug(1, "%s dialed no digits.\n", ast_channel_name(chan));
			} else {
				ast_debug(1, "%s dialed '%s@%s' does not exist.\n",
					ast_channel_name(chan), exten, context);
			}
			if (attempts < max_attempts) {
				ast_stream_and_wait(chan, retry_sound, AST_DIGIT_NONE);
			} else {
				ast_stream_and_wait(chan, invalid_sound, AST_DIGIT_NONE);
			}
			memset(exten, 0, exten_len);
			res = 1;
		} else {
			/* Dialed extension is valid. */
			res = 0;
		}
	} while (res > 0 && attempts < max_attempts);

	ast_test_suite_event_notify("TRANSFER_DIAL_FINAL",
			"Channel: %s\r\n"
			"Result: %s",
			ast_channel_name(chan), res == 0 ? "Success" : "Failure");

	return res ? -1 : 0;
}

static void copy_caller_data(struct ast_channel *dest, struct ast_channel *caller)
{
	ast_channel_lock_both(caller, dest);
	ast_connected_line_copy_from_caller(ast_channel_connected(dest), ast_channel_caller(caller));
	ast_channel_inherit_variables(caller, dest);
	ast_channel_datastore_inherit(caller, dest);
	ast_channel_unlock(dest);
	ast_channel_unlock(caller);
}

/*! \brief Helper function that creates an outgoing channel and returns it immediately */
static struct ast_channel *dial_transfer(struct ast_channel *caller, const char *destination)
{
	struct ast_channel *chan;
	int cause;

	/* Now we request a local channel to prepare to call the destination */
	chan = ast_request("Local", ast_channel_nativeformats(caller), NULL, caller, destination,
		&cause);
	if (!chan) {
		return NULL;
	}

	ast_channel_lock_both(chan, caller);

	ast_channel_req_accountcodes(chan, caller, AST_CHANNEL_REQUESTOR_BRIDGE_PEER);

	/* Who is transferring the call. */
	pbx_builtin_setvar_helper(chan, "TRANSFERERNAME", ast_channel_name(caller));

	ast_bridge_set_transfer_variables(chan, ast_channel_name(caller), 1);

	ast_channel_unlock(chan);
	ast_channel_unlock(caller);

	/* Before we actually dial out let's inherit appropriate information. */
	copy_caller_data(chan, caller);

	return chan;
}

/*!
 * \brief Internal built in feature for attended transfers
 *
 * This hook will set up a thread for monitoring the progress of
 * an attended transfer. For more information about attended transfer
 * progress, see documentation on the transfer state machine.
 *
 * \param bridge_channel The channel that pressed the attended transfer DTMF sequence
 * \param hook_pvt Structure with further information about the attended transfer
 */
static int feature_attended_transfer(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	struct ast_bridge_features_attended_transfer *attended_transfer = hook_pvt;
	struct attended_transfer_properties *props;
	struct ast_bridge *bridge;
	char destination[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 1];
	char exten[AST_MAX_EXTENSION] = "";
	pthread_t thread;

	/* Inhibit the bridge before we do anything else. */
	bridge = ast_bridge_channel_merge_inhibit(bridge_channel, +1);

	if (strcmp(bridge->v_table->name, "basic")) {
		ast_log(LOG_ERROR, "Attended transfer attempted on unsupported bridge type '%s'.\n",
			bridge->v_table->name);
		ast_bridge_merge_inhibit(bridge, -1);
		ao2_ref(bridge, -1);
		return 0;
	}

	/* Was the bridge inhibited before we inhibited it? */
	if (1 < bridge->inhibit_merge) {
		/*
		 * The peer likely initiated attended transfer at the same time
		 * and we lost the race.
		 */
		ast_verb(3, "Channel %s: Bridge '%s' does not permit merging at this time.\n",
			ast_channel_name(bridge_channel->chan), bridge->uniqueid);
		ast_bridge_merge_inhibit(bridge, -1);
		ao2_ref(bridge, -1);
		return 0;
	}

	props = attended_transfer_properties_alloc(bridge_channel->chan,
		attended_transfer ? attended_transfer->context : NULL);
	if (!props) {
		ast_log(LOG_ERROR, "Unable to allocate control structure for performing attended transfer.\n");
		ast_bridge_merge_inhibit(bridge, -1);
		ao2_ref(bridge, -1);
		return 0;
	}

	props->transferee_bridge = bridge;

	if (add_transferer_role(props->transferer, attended_transfer)) {
		ast_log(LOG_ERROR, "Unable to set transferrer bridge role.\n");
		attended_transfer_properties_shutdown(props);
		return 0;
	}

	ast_bridge_channel_write_hold(bridge_channel, NULL);

	/* Grab the extension to transfer to */
	if (grab_transfer(bridge_channel->chan, exten, sizeof(exten), props->context)) {
		ast_log(LOG_WARNING, "Unable to acquire target extension for attended transfer.\n");
		ast_bridge_channel_write_unhold(bridge_channel);
		attended_transfer_properties_shutdown(props);
		return 0;
	}

	ast_string_field_set(props, exten, exten);

	/* Fill the variable with the extension and context we want to call */
	snprintf(destination, sizeof(destination), "%s@%s", props->exten, props->context);

	ast_debug(1, "Attended transfer to '%s'\n", destination);

	/* Get a channel that is the destination we wish to call */
	props->transfer_target = dial_transfer(bridge_channel->chan, destination);
	if (!props->transfer_target) {
		ast_log(LOG_ERROR, "Unable to request outbound channel for attended transfer target.\n");
		ast_stream_and_wait(props->transferer, props->failsound, AST_DIGIT_NONE);
		ast_bridge_channel_write_unhold(bridge_channel);
		attended_transfer_properties_shutdown(props);
		return 0;
	}


	/* Create a bridge to use to talk to the person we are calling */
	props->target_bridge = ast_bridge_basic_new();
	if (!props->target_bridge) {
		ast_log(LOG_ERROR, "Unable to create bridge for attended transfer target.\n");
		ast_stream_and_wait(props->transferer, props->failsound, AST_DIGIT_NONE);
		ast_bridge_channel_write_unhold(bridge_channel);
		ast_hangup(props->transfer_target);
		props->transfer_target = NULL;
		attended_transfer_properties_shutdown(props);
		return 0;
	}
	ast_bridge_merge_inhibit(props->target_bridge, +1);

	if (attach_framehook(props, props->transfer_target)) {
		ast_log(LOG_ERROR, "Unable to attach framehook to transfer target.\n");
		ast_stream_and_wait(props->transferer, props->failsound, AST_DIGIT_NONE);
		ast_bridge_channel_write_unhold(bridge_channel);
		ast_hangup(props->transfer_target);
		props->transfer_target = NULL;
		attended_transfer_properties_shutdown(props);
		return 0;
	}

	bridge_basic_change_personality(props->target_bridge,
			BRIDGE_BASIC_PERSONALITY_ATXFER, props);
	bridge_basic_change_personality(bridge,
			BRIDGE_BASIC_PERSONALITY_ATXFER, props);

	if (ast_call(props->transfer_target, destination, 0)) {
		ast_log(LOG_ERROR, "Unable to place outbound call to transfer target.\n");
		ast_stream_and_wait(bridge_channel->chan, props->failsound, AST_DIGIT_NONE);
		ast_bridge_channel_write_unhold(bridge_channel);
		ast_hangup(props->transfer_target);
		props->transfer_target = NULL;
		attended_transfer_properties_shutdown(props);
		return 0;
	}

	/* We increase the refcount of the transfer target because ast_bridge_impart() will
	 * steal the reference we already have. We need to keep a reference, so the only
	 * choice is to give it a bump
	 */
	ast_channel_ref(props->transfer_target);
	if (ast_bridge_impart(props->target_bridge, props->transfer_target, NULL, NULL,
		AST_BRIDGE_IMPART_CHAN_INDEPENDENT)) {
		ast_log(LOG_ERROR, "Unable to place transfer target into bridge.\n");
		ast_stream_and_wait(bridge_channel->chan, props->failsound, AST_DIGIT_NONE);
		ast_bridge_channel_write_unhold(bridge_channel);
		ast_hangup(props->transfer_target);
		props->transfer_target = NULL;
		attended_transfer_properties_shutdown(props);
		return 0;
	}

	if (ast_pthread_create_detached(&thread, NULL, attended_transfer_monitor_thread, props)) {
		ast_log(LOG_ERROR, "Unable to create monitoring thread for attended transfer.\n");
		ast_stream_and_wait(bridge_channel->chan, props->failsound, AST_DIGIT_NONE);
		ast_bridge_channel_write_unhold(bridge_channel);
		attended_transfer_properties_shutdown(props);
		return 0;
	}

	/* Once the monitoring thread has been created, it is responsible for destroying all
	 * of the necessary components.
	 */
	return 0;
}

static void blind_transfer_cb(struct ast_channel *new_channel, struct transfer_channel_data *user_data_wrapper,
		enum ast_transfer_type transfer_type)
{
	struct ast_channel *transferer_channel = user_data_wrapper->data;

	if (transfer_type == AST_BRIDGE_TRANSFER_MULTI_PARTY) {
		copy_caller_data(new_channel, transferer_channel);
	}
}

/*! \brief Internal built in feature for blind transfers */
static int feature_blind_transfer(struct ast_bridge_channel *bridge_channel, void *hook_pvt)
{
	char exten[AST_MAX_EXTENSION] = "";
	struct ast_bridge_features_blind_transfer *blind_transfer = hook_pvt;
	const char *context;
	char *goto_on_blindxfr;

	ast_bridge_channel_write_hold(bridge_channel, NULL);

	ast_channel_lock(bridge_channel->chan);
	context = ast_strdupa(get_transfer_context(bridge_channel->chan,
		blind_transfer ? blind_transfer->context : NULL));
	goto_on_blindxfr = ast_strdupa(S_OR(pbx_builtin_getvar_helper(bridge_channel->chan,
		"GOTO_ON_BLINDXFR"), ""));
	ast_channel_unlock(bridge_channel->chan);

	/* Grab the extension to transfer to */
	if (grab_transfer(bridge_channel->chan, exten, sizeof(exten), context)) {
		ast_bridge_channel_write_unhold(bridge_channel);
		return 0;
	}

	if (!ast_strlen_zero(goto_on_blindxfr)) {
		ast_debug(1, "After transfer, transferer %s goes to %s\n",
				ast_channel_name(bridge_channel->chan), goto_on_blindxfr);
		ast_bridge_set_after_go_on(bridge_channel->chan, NULL, NULL, 0, goto_on_blindxfr);
	}

	if (ast_bridge_transfer_blind(0, bridge_channel->chan, exten, context, blind_transfer_cb,
			bridge_channel->chan) != AST_BRIDGE_TRANSFER_SUCCESS &&
			!ast_strlen_zero(goto_on_blindxfr)) {
		ast_bridge_discard_after_goto(bridge_channel->chan);
	}

	return 0;
}

struct ast_bridge_methods ast_bridge_basic_v_table;
struct ast_bridge_methods personality_normal_v_table;
struct ast_bridge_methods personality_atxfer_v_table;

static void bridge_basic_change_personality(struct ast_bridge *bridge,
		enum bridge_basic_personality_type type, void *user_data)
{
	struct bridge_basic_personality *personality = bridge->personality;
	SCOPED_LOCK(lock, bridge, ast_bridge_lock, ast_bridge_unlock);

	remove_hooks_on_personality_change(bridge);

	ao2_cleanup(personality->details[personality->current].pvt);
	personality->details[personality->current].pvt = NULL;
	ast_clear_flag(&bridge->feature_flags, AST_FLAGS_ALL);

	personality->current = type;
	if (user_data) {
		ao2_ref(user_data, +1);
	}
	personality->details[personality->current].pvt = user_data;
	ast_set_flag(&bridge->feature_flags, personality->details[personality->current].bridge_flags);
	if (personality->details[personality->current].on_personality_change) {
		personality->details[personality->current].on_personality_change(bridge);
	}
}

static void personality_destructor(void *obj)
{
	struct bridge_basic_personality *personality = obj;
	int i;

	for (i = 0; i < BRIDGE_BASIC_PERSONALITY_END; ++i) {
		ao2_cleanup(personality->details[i].pvt);
	}
}

static void on_personality_change_normal(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *iter;

	AST_LIST_TRAVERSE(&bridge->channels, iter, entry) {
		if (add_normal_hooks(bridge, iter)) {
			ast_log(LOG_WARNING, "Unable to set up bridge hooks for channel %s. Features may not work properly\n",
					ast_channel_name(iter->chan));
		}
	}
}

static void init_details(struct personality_details *details,
		enum bridge_basic_personality_type type)
{
	switch (type) {
	case BRIDGE_BASIC_PERSONALITY_NORMAL:
		details->v_table = &personality_normal_v_table;
		details->bridge_flags = NORMAL_FLAGS;
		details->on_personality_change = on_personality_change_normal;
		break;
	case BRIDGE_BASIC_PERSONALITY_ATXFER:
		details->v_table = &personality_atxfer_v_table;
		details->bridge_flags = TRANSFER_FLAGS;
		break;
	default:
		ast_log(LOG_WARNING, "Asked to initialize unexpected basic bridge personality type.\n");
		break;
	}
}

static struct ast_bridge *bridge_basic_personality_alloc(struct ast_bridge *bridge)
{
	struct bridge_basic_personality *personality;
	int i;

	if (!bridge) {
		return NULL;
	}

	personality = ao2_alloc(sizeof(*personality), personality_destructor);
	if (!personality) {
		ao2_ref(bridge, -1);
		return NULL;
	}
	for (i = 0; i < BRIDGE_BASIC_PERSONALITY_END; ++i) {
		init_details(&personality->details[i], i);
	}
	personality->current = BRIDGE_BASIC_PERSONALITY_NORMAL;
	bridge->personality = personality;

	return bridge;
}

struct ast_bridge *ast_bridge_basic_new(void)
{
	struct ast_bridge *bridge;

	bridge = bridge_alloc(sizeof(struct ast_bridge), &ast_bridge_basic_v_table);
	bridge = bridge_base_init(bridge,
		AST_BRIDGE_CAPABILITY_NATIVE | AST_BRIDGE_CAPABILITY_1TO1MIX
			| AST_BRIDGE_CAPABILITY_MULTIMIX, NORMAL_FLAGS, NULL, NULL, NULL);
	bridge = bridge_basic_personality_alloc(bridge);
	bridge = bridge_register(bridge);
	return bridge;
}

void ast_bridge_basic_set_flags(struct ast_bridge *bridge, unsigned int flags)
{
	SCOPED_LOCK(lock, bridge, ast_bridge_lock, ast_bridge_unlock);
	struct bridge_basic_personality *personality = bridge->personality;

	personality->details[personality->current].bridge_flags |= flags;
	ast_set_flag(&bridge->feature_flags, flags);
}

void ast_bridging_init_basic(void)
{
	/* Setup bridge basic subclass v_table. */
	ast_bridge_basic_v_table = ast_bridge_base_v_table;
	ast_bridge_basic_v_table.name = "basic";
	ast_bridge_basic_v_table.push = bridge_basic_push;
	ast_bridge_basic_v_table.pull = bridge_basic_pull;
	ast_bridge_basic_v_table.destroy = bridge_basic_destroy;

	/*
	 * Personality vtables don't have the same rules as
	 * normal bridge vtables.  These vtable functions are
	 * used as alterations to the ast_bridge_basic_v_table
	 * method functionality and are checked for NULL before
	 * calling.
	 */
	personality_normal_v_table.name = "normal";
	personality_normal_v_table.push = bridge_personality_normal_push;

	personality_atxfer_v_table.name = "attended transfer";
	personality_atxfer_v_table.push = bridge_personality_atxfer_push;
	personality_atxfer_v_table.pull = bridge_personality_atxfer_pull;

	ast_bridge_features_register(AST_BRIDGE_BUILTIN_ATTENDEDTRANSFER, feature_attended_transfer, NULL);
	ast_bridge_features_register(AST_BRIDGE_BUILTIN_BLINDTRANSFER, feature_blind_transfer, NULL);
}

