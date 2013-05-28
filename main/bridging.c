/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007 - 2009, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
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
 * \brief Channel Bridging API
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <signal.h>

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_basic.h"
#include "asterisk/bridging_technology.h"
#include "asterisk/stasis_bridging.h"
#include "asterisk/app.h"
#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"
#include "asterisk/test.h"
#include "asterisk/_private.h"

#include "asterisk/heap.h"
#include "asterisk/say.h"
#include "asterisk/timing.h"
#include "asterisk/stringfields.h"
#include "asterisk/musiconhold.h"
#include "asterisk/features.h"
#include "asterisk/cli.h"
#include "asterisk/parking.h"
#include "asterisk/core_local.h"

/*! All bridges container. */
static struct ao2_container *bridges;

static AST_RWLIST_HEAD_STATIC(bridge_technologies, ast_bridge_technology);

/* Initial starting point for the bridge array of channels */
#define BRIDGE_ARRAY_START 128

/* Grow rate of bridge array of channels */
#define BRIDGE_ARRAY_GROW 32

static void cleanup_video_mode(struct ast_bridge *bridge);
static int bridge_make_compatible(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel);
static void bridge_features_remove_on_pull(struct ast_bridge_features *features);

/*! Default DTMF keys for built in features */
static char builtin_features_dtmf[AST_BRIDGE_BUILTIN_END][MAXIMUM_DTMF_FEATURE_STRING];

/*! Function handlers for the built in features */
static void *builtin_features_handlers[AST_BRIDGE_BUILTIN_END];

/*! Function handlers for built in interval features */
static ast_bridge_builtin_set_limits_fn builtin_interval_handlers[AST_BRIDGE_BUILTIN_INTERVAL_END];

/*! Bridge manager service request */
struct bridge_manager_request {
	/*! List of bridge service requests. */
	AST_LIST_ENTRY(bridge_manager_request) node;
	/*! Refed bridge requesting service. */
	struct ast_bridge *bridge;
};

struct bridge_manager_controller {
	/*! Condition, used to wake up the bridge manager thread. */
	ast_cond_t cond;
	/*! Queue of bridge service requests. */
	AST_LIST_HEAD_NOLOCK(, bridge_manager_request) service_requests;
	/*! Manager thread */
	pthread_t thread;
	/*! TRUE if the manager needs to stop. */
	unsigned int stop:1;
};

/*! Bridge manager controller. */
static struct bridge_manager_controller *bridge_manager;

/*!
 * \internal
 * \brief Request service for a bridge from the bridge manager.
 * \since 12.0.0
 *
 * \param bridge Requesting service.
 *
 * \return Nothing
 */
static void bridge_manager_service_req(struct ast_bridge *bridge)
{
	struct bridge_manager_request *request;

	ao2_lock(bridge_manager);
	if (bridge_manager->stop) {
		ao2_unlock(bridge_manager);
		return;
	}

	/* Create the service request. */
	request = ast_calloc(1, sizeof(*request));
	if (!request) {
		/* Well. This isn't good. */
		ao2_unlock(bridge_manager);
		return;
	}
	ao2_ref(bridge, +1);
	request->bridge = bridge;

	/* Put request into the queue and wake the bridge manager. */
	AST_LIST_INSERT_TAIL(&bridge_manager->service_requests, request, node);
	ast_cond_signal(&bridge_manager->cond);
	ao2_unlock(bridge_manager);
}

int __ast_bridge_technology_register(struct ast_bridge_technology *technology, struct ast_module *module)
{
	struct ast_bridge_technology *current;

	/* Perform a sanity check to make sure the bridge technology conforms to our needed requirements */
	if (ast_strlen_zero(technology->name)
		|| !technology->capabilities
		|| !technology->write) {
		ast_log(LOG_WARNING, "Bridge technology %s failed registration sanity check.\n",
			technology->name);
		return -1;
	}

	AST_RWLIST_WRLOCK(&bridge_technologies);

	/* Look for duplicate bridge technology already using this name, or already registered */
	AST_RWLIST_TRAVERSE(&bridge_technologies, current, entry) {
		if ((!strcasecmp(current->name, technology->name)) || (current == technology)) {
			ast_log(LOG_WARNING, "A bridge technology of %s already claims to exist in our world.\n",
				technology->name);
			AST_RWLIST_UNLOCK(&bridge_technologies);
			return -1;
		}
	}

	/* Copy module pointer so reference counting can keep the module from unloading */
	technology->mod = module;

	/* Insert our new bridge technology into the list and print out a pretty message */
	AST_RWLIST_INSERT_TAIL(&bridge_technologies, technology, entry);

	AST_RWLIST_UNLOCK(&bridge_technologies);

	ast_verb(2, "Registered bridge technology %s\n", technology->name);

	return 0;
}

int ast_bridge_technology_unregister(struct ast_bridge_technology *technology)
{
	struct ast_bridge_technology *current;

	AST_RWLIST_WRLOCK(&bridge_technologies);

	/* Ensure the bridge technology is registered before removing it */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&bridge_technologies, current, entry) {
		if (current == technology) {
			AST_RWLIST_REMOVE_CURRENT(entry);
			ast_verb(2, "Unregistered bridge technology %s\n", technology->name);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	AST_RWLIST_UNLOCK(&bridge_technologies);

	return current ? 0 : -1;
}

void ast_bridge_channel_lock_bridge(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge;

	for (;;) {
		/* Safely get the bridge pointer */
		ast_bridge_channel_lock(bridge_channel);
		bridge = bridge_channel->bridge;
		ao2_ref(bridge, +1);
		ast_bridge_channel_unlock(bridge_channel);

		/* Lock the bridge and see if it is still the bridge we need to lock. */
		ast_bridge_lock(bridge);
		if (bridge == bridge_channel->bridge) {
			ao2_ref(bridge, -1);
			return;
		}
		ast_bridge_unlock(bridge);
		ao2_ref(bridge, -1);
	}
}

static void bridge_channel_poke(struct ast_bridge_channel *bridge_channel)
{
	if (!pthread_equal(pthread_self(), bridge_channel->thread)) {
		while (bridge_channel->waiting) {
			pthread_kill(bridge_channel->thread, SIGURG);
			sched_yield();
		}
	}
}

void ast_bridge_change_state_nolock(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state)
{
/* BUGBUG need cause code for the bridge_channel leaving the bridge. */
	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
		return;
	}

	ast_debug(1, "Setting %p(%s) state from:%d to:%d\n",
		bridge_channel, ast_channel_name(bridge_channel->chan), bridge_channel->state,
		new_state);

	/* Change the state on the bridge channel */
	bridge_channel->state = new_state;

	bridge_channel_poke(bridge_channel);
}

void ast_bridge_change_state(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state)
{
	ast_bridge_channel_lock(bridge_channel);
	ast_bridge_change_state_nolock(bridge_channel, new_state);
	ast_bridge_channel_unlock(bridge_channel);
}

/*!
 * \internal
 * \brief Put an action onto the specified bridge. Don't dup the action frame.
 * \since 12.0.0
 *
 * \param bridge What to queue the action on.
 * \param action What to do.
 *
 * \return Nothing
 */
static void bridge_queue_action_nodup(struct ast_bridge *bridge, struct ast_frame *action)
{
	ast_debug(1, "Bridge %s: queueing action type:%d sub:%d\n",
		bridge->uniqueid, action->frametype, action->subclass.integer);

	ast_bridge_lock(bridge);
	AST_LIST_INSERT_TAIL(&bridge->action_queue, action, frame_list);
	ast_bridge_unlock(bridge);
	bridge_manager_service_req(bridge);
}

int ast_bridge_queue_action(struct ast_bridge *bridge, struct ast_frame *action)
{
	struct ast_frame *dup;

	dup = ast_frdup(action);
	if (!dup) {
		return -1;
	}
	bridge_queue_action_nodup(bridge, dup);
	return 0;
}

int ast_bridge_channel_queue_frame(struct ast_bridge_channel *bridge_channel, struct ast_frame *fr)
{
	struct ast_frame *dup;
	char nudge = 0;

	if (bridge_channel->suspended
		/* Also defer DTMF frames. */
		&& fr->frametype != AST_FRAME_DTMF_BEGIN
		&& fr->frametype != AST_FRAME_DTMF_END
		&& !ast_is_deferrable_frame(fr)) {
		/* Drop non-deferable frames when suspended. */
		return 0;
	}

	dup = ast_frdup(fr);
	if (!dup) {
		return -1;
	}

	ast_bridge_channel_lock(bridge_channel);
	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
		/* Drop frames on channels leaving the bridge. */
		ast_bridge_channel_unlock(bridge_channel);
		ast_frfree(dup);
		return 0;
	}

	AST_LIST_INSERT_TAIL(&bridge_channel->wr_queue, dup, frame_list);
	if (write(bridge_channel->alert_pipe[1], &nudge, sizeof(nudge)) != sizeof(nudge)) {
		ast_log(LOG_ERROR, "We couldn't write alert pipe for %p(%s)... something is VERY wrong\n",
			bridge_channel, ast_channel_name(bridge_channel->chan));
	}
	ast_bridge_channel_unlock(bridge_channel);
	return 0;
}

void ast_bridge_channel_queue_action_data(struct ast_bridge_channel *bridge_channel, enum ast_bridge_action_type action, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = action,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	ast_bridge_channel_queue_frame(bridge_channel, &frame);
}

void ast_bridge_channel_queue_control_data(struct ast_bridge_channel *bridge_channel, enum ast_control_frame_type control, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = control,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	ast_bridge_channel_queue_frame(bridge_channel, &frame);
}

void ast_bridge_channel_restore_formats(struct ast_bridge_channel *bridge_channel)
{
	/* Restore original formats of the channel as they came in */
	if (ast_format_cmp(ast_channel_readformat(bridge_channel->chan), &bridge_channel->read_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Bridge is returning %p(%s) to read format %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan),
			ast_getformatname(&bridge_channel->read_format));
		if (ast_set_read_format(bridge_channel->chan, &bridge_channel->read_format)) {
			ast_debug(1, "Bridge failed to return %p(%s) to read format %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan),
				ast_getformatname(&bridge_channel->read_format));
		}
	}
	if (ast_format_cmp(ast_channel_writeformat(bridge_channel->chan), &bridge_channel->write_format) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Bridge is returning %p(%s) to write format %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan),
			ast_getformatname(&bridge_channel->write_format));
		if (ast_set_write_format(bridge_channel->chan, &bridge_channel->write_format)) {
			ast_debug(1, "Bridge failed to return %p(%s) to write format %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan),
				ast_getformatname(&bridge_channel->write_format));
		}
	}
}

/*!
 * \internal
 * \brief Helper function to find a bridge channel given a channel.
 *
 * \param bridge What to search
 * \param chan What to search for.
 *
 * \note On entry, bridge is already locked.
 *
 * \retval bridge_channel if channel is in the bridge.
 * \retval NULL if not in bridge.
 */
static struct ast_bridge_channel *find_bridge_channel(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (bridge_channel->chan == chan) {
			break;
		}
	}

	return bridge_channel;
}

/*!
 * \internal
 * \brief Dissolve the bridge.
 * \since 12.0.0
 *
 * \param bridge Bridge to eject all channels
 *
 * \details
 * Force out all channels that are not already going out of the
 * bridge.  Any new channels joining will leave immediately.
 *
 * \note On entry, bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_dissolve(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel;
	struct ast_frame action = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = AST_BRIDGE_ACTION_DEFERRED_DISSOLVING,
	};

	if (bridge->dissolved) {
		return;
	}
	bridge->dissolved = 1;

	ast_debug(1, "Bridge %s: dissolving bridge\n", bridge->uniqueid);

/* BUGBUG need a cause code on the bridge for the later ejected channels. */
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
	}

	/* Must defer dissolving bridge because it is already locked. */
	ast_bridge_queue_action(bridge, &action);
}

/*!
 * \internal
 * \brief Check if a bridge should dissolve and do it.
 * \since 12.0.0
 *
 * \param bridge_channel Channel causing the check.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_dissolve_check(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;

	if (bridge->dissolved) {
		return;
	}

	if (!bridge->num_channels
		&& ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_DISSOLVE_EMPTY)) {
		/* Last channel leaving the bridge turns off the lights. */
		bridge_dissolve(bridge);
		return;
	}

	switch (bridge_channel->state) {
	case AST_BRIDGE_CHANNEL_STATE_END:
		/* Do we need to dissolve the bridge because this channel hung up? */
		if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_DISSOLVE_HANGUP)
			|| (bridge_channel->features->usable
				&& ast_test_flag(&bridge_channel->features->feature_flags,
					AST_BRIDGE_CHANNEL_FLAG_DISSOLVE_HANGUP))) {
			bridge_dissolve(bridge);
			return;
		}
		break;
	default:
		break;
	}
/* BUGBUG need to implement AST_BRIDGE_CHANNEL_FLAG_LONELY support here */
}

/*!
 * \internal
 * \brief Pull the bridge channel out of its current bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to pull.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_channel_pull(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;

	if (!bridge_channel->in_bridge) {
		return;
	}
	bridge_channel->in_bridge = 0;

	ast_debug(1, "Bridge %s: pulling %p(%s)\n",
		bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));

/* BUGBUG This is where incoming HOLD/UNHOLD memory should write UNHOLD into bridge. (if not local optimizing) */
/* BUGBUG This is where incoming DTMF begin/end memory should write DTMF end into bridge. (if not local optimizing) */
	if (!bridge_channel->just_joined) {
		/* Tell the bridge technology we are leaving so they tear us down */
		ast_debug(1, "Bridge %s: %p(%s) is leaving %s technology\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			bridge->technology->name);
		if (bridge->technology->leave) {
			bridge->technology->leave(bridge, bridge_channel);
		}
	}

	/* Remove channel from the bridge */
	if (!bridge_channel->suspended) {
		--bridge->num_active;
	}
	--bridge->num_channels;
	AST_LIST_REMOVE(&bridge->channels, bridge_channel, entry);
	bridge->v_table->pull(bridge, bridge_channel);

	ast_bridge_channel_clear_roles(bridge_channel);

	bridge_dissolve_check(bridge_channel);

	bridge->reconfigured = 1;
	ast_bridge_publish_leave(bridge, bridge_channel->chan);
}

/*!
 * \internal
 * \brief Push the bridge channel into its specified bridge.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to push.
 *
 * \note On entry, bridge_channel->bridge is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.  The channel did not get pushed.
 */
static int bridge_channel_push(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;
	struct ast_bridge_channel *swap;

	ast_assert(!bridge_channel->in_bridge);

	swap = find_bridge_channel(bridge, bridge_channel->swap);
	bridge_channel->swap = NULL;

	if (swap) {
		ast_debug(1, "Bridge %s: pushing %p(%s) by swapping with %p(%s)\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			swap, ast_channel_name(swap->chan));
	} else {
		ast_debug(1, "Bridge %s: pushing %p(%s)\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));
	}

	/* Add channel to the bridge */
	if (bridge->dissolved
		|| bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT
		|| (swap && swap->state != AST_BRIDGE_CHANNEL_STATE_WAIT)
		|| bridge->v_table->push(bridge, bridge_channel, swap)
		|| ast_bridge_channel_establish_roles(bridge_channel)) {
		ast_debug(1, "Bridge %s: pushing %p(%s) into bridge failed\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan));
		return -1;
	}
	bridge_channel->in_bridge = 1;
	bridge_channel->just_joined = 1;
	AST_LIST_INSERT_TAIL(&bridge->channels, bridge_channel, entry);
	++bridge->num_channels;
	if (!bridge_channel->suspended) {
		++bridge->num_active;
	}
	if (swap) {
		ast_bridge_change_state(swap, AST_BRIDGE_CHANNEL_STATE_HANGUP);
		bridge_channel_pull(swap);
	}

	bridge->reconfigured = 1;
	ast_bridge_publish_enter(bridge, bridge_channel->chan);
	return 0;
}

/*! \brief Internal function to handle DTMF from a channel */
static struct ast_frame *bridge_handle_dtmf(struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	char dtmf[2];

/* BUGBUG the feature hook matching needs to be done here.  Any matching feature hook needs to be queued onto the bridge_channel.  Also the feature hook digit timeout needs to be handled. */
/* BUGBUG the AMI atxfer action just sends DTMF end events to initiate DTMF atxfer and dial the extension.  Another reason the DTMF hook matching needs rework. */
	/* See if this DTMF matches the beginnings of any feature hooks, if so we switch to the feature state to either execute the feature or collect more DTMF */
	dtmf[0] = frame->subclass.integer;
	dtmf[1] = '\0';
	hook = ao2_find(features->dtmf_hooks, dtmf, OBJ_PARTIAL_KEY);
	if (hook) {
		struct ast_frame action = {
			.frametype = AST_FRAME_BRIDGE_ACTION,
			.subclass.integer = AST_BRIDGE_ACTION_FEATURE,
		};

		ast_frfree(frame);
		frame = NULL;
		ast_bridge_channel_queue_frame(bridge_channel, &action);
		ao2_ref(hook, -1);
	}

	return frame;
}

/*!
 * \internal
 * \brief Handle bridge hangup event.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is hanging up.
 *
 * \return Nothing
 */
static void bridge_handle_hangup(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run any hangup hooks. */
	iter = ao2_iterator_init(features->hangup_hooks, 0);
	for (; (hook = ao2_iterator_next(&iter)); ao2_ref(hook, -1)) {
		int failed;

		failed = hook->callback(bridge_channel->bridge, bridge_channel, hook->hook_pvt);
		if (failed) {
			ast_debug(1, "Hangup hook %p is being removed from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_unlink(features->hangup_hooks, hook);
		}
	}
	ao2_iterator_destroy(&iter);

	/* Default hangup action. */
	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_END);
}

static int bridge_channel_interval_ready(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	int ready;

	ast_heap_wrlock(features->interval_hooks);
	hook = ast_heap_peek(features->interval_hooks, 1);
	ready = hook && ast_tvdiff_ms(hook->parms.timer.trip_time, ast_tvnow()) <= 0;
	ast_heap_unlock(features->interval_hooks);

	return ready;
}

void ast_bridge_notify_talking(struct ast_bridge_channel *bridge_channel, int started_talking)
{
	struct ast_frame action = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = started_talking
			? AST_BRIDGE_ACTION_TALKING_START : AST_BRIDGE_ACTION_TALKING_STOP,
	};

	ast_bridge_channel_queue_frame(bridge_channel, &action);
}

static void bridge_channel_write_frame(struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	ast_bridge_channel_lock_bridge(bridge_channel);
/*
 * BUGBUG need to implement a deferred write queue for when there is no peer channel in the bridge (yet or it was kicked).
 *
 * The tech decides if a frame needs to be pushed back for deferral.
 * simple_bridge/native_bridge are likely the only techs that will do this.
 */
	bridge_channel->bridge->technology->write(bridge_channel->bridge, bridge_channel, frame);
	ast_bridge_unlock(bridge_channel->bridge);
}

void ast_bridge_channel_write_action_data(struct ast_bridge_channel *bridge_channel, enum ast_bridge_action_type action, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_BRIDGE_ACTION,
		.subclass.integer = action,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	bridge_channel_write_frame(bridge_channel, &frame);
}

void ast_bridge_channel_write_control_data(struct ast_bridge_channel *bridge_channel, enum ast_control_frame_type control, const void *data, size_t datalen)
{
	struct ast_frame frame = {
		.frametype = AST_FRAME_CONTROL,
		.subclass.integer = control,
		.datalen = datalen,
		.data.ptr = (void *) data,
	};

	bridge_channel_write_frame(bridge_channel, &frame);
}

static int run_app_helper(struct ast_channel *chan, const char *app_name, const char *app_args)
{
	int res = 0;

	if (!strcasecmp("Gosub", app_name)) {
		ast_app_exec_sub(NULL, chan, app_args, 0);
	} else if (!strcasecmp("Macro", app_name)) {
		ast_app_exec_macro(NULL, chan, app_args);
	} else {
		struct ast_app *app;

		app = pbx_findapp(app_name);
		if (!app) {
			ast_log(LOG_WARNING, "Could not find application (%s)\n", app_name);
		} else {
			res = pbx_exec(chan, app, app_args);
		}
	}
	return res;
}

void ast_bridge_channel_run_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	if (moh_class) {
		if (ast_strlen_zero(moh_class)) {
			ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_HOLD,
				NULL, 0);
		} else {
			ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_HOLD,
				moh_class, strlen(moh_class) + 1);
		}
	}
	if (run_app_helper(bridge_channel->chan, app_name, S_OR(app_args, ""))) {
		/* Break the bridge if the app returns non-zero. */
		bridge_handle_hangup(bridge_channel);
	}
	if (moh_class) {
		ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_UNHOLD,
			NULL, 0);
	}
}

struct bridge_run_app {
	/*! Offset into app_name[] where the MOH class name starts.  (zero if no MOH) */
	int moh_offset;
	/*! Offset into app_name[] where the application argument string starts. (zero if no arguments) */
	int app_args_offset;
	/*! Application name to run. */
	char app_name[0];
};

/*!
 * \internal
 * \brief Handle the run application bridge action.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to run the application on.
 * \param data Action frame data to run the application.
 *
 * \return Nothing
 */
static void bridge_channel_run_app(struct ast_bridge_channel *bridge_channel, struct bridge_run_app *data)
{
	ast_bridge_channel_run_app(bridge_channel, data->app_name,
		data->app_args_offset ? &data->app_name[data->app_args_offset] : NULL,
		data->moh_offset ? &data->app_name[data->moh_offset] : NULL);
}

static void payload_helper_app(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	struct bridge_run_app *app_data;
	size_t len_name = strlen(app_name) + 1;
	size_t len_args = ast_strlen_zero(app_args) ? 0 : strlen(app_args) + 1;
	size_t len_moh = !moh_class ? 0 : strlen(moh_class) + 1;
	size_t len_data = sizeof(*app_data) + len_name + len_args + len_moh;

	/* Fill in application run frame data. */
	app_data = alloca(len_data);
	app_data->app_args_offset = len_args ? len_name : 0;
	app_data->moh_offset = len_moh ? len_name + len_args : 0;
	strcpy(app_data->app_name, app_name);/* Safe */
	if (len_args) {
		strcpy(&app_data->app_name[app_data->app_args_offset], app_args);/* Safe */
	}
	if (moh_class) {
		strcpy(&app_data->app_name[app_data->moh_offset], moh_class);/* Safe */
	}

	post_it(bridge_channel, AST_BRIDGE_ACTION_RUN_APP, app_data, len_data);
}

void ast_bridge_channel_write_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	payload_helper_app(ast_bridge_channel_write_action_data,
		bridge_channel, app_name, app_args, moh_class);
}

void ast_bridge_channel_queue_app(struct ast_bridge_channel *bridge_channel, const char *app_name, const char *app_args, const char *moh_class)
{
	payload_helper_app(ast_bridge_channel_queue_action_data,
		bridge_channel, app_name, app_args, moh_class);
}

void ast_bridge_channel_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	if (moh_class) {
		if (ast_strlen_zero(moh_class)) {
			ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_HOLD,
				NULL, 0);
		} else {
			ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_HOLD,
				moh_class, strlen(moh_class) + 1);
		}
	}
	if (custom_play) {
		custom_play(bridge_channel, playfile);
	} else {
		ast_stream_and_wait(bridge_channel->chan, playfile, AST_DIGIT_NONE);
	}
	if (moh_class) {
		ast_bridge_channel_write_control_data(bridge_channel, AST_CONTROL_UNHOLD,
			NULL, 0);
	}

	/*
	 * It may be necessary to resume music on hold after we finish
	 * playing the announcment.
	 *
	 * XXX We have no idea what MOH class was in use before playing
	 * the file.
	 */
	if (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_MOH)) {
		ast_moh_start(bridge_channel->chan, NULL, NULL);
	}
}

struct bridge_playfile {
	/*! Call this function to play the playfile. (NULL if normal sound file to play) */
	ast_bridge_custom_play_fn custom_play;
	/*! Offset into playfile[] where the MOH class name starts.  (zero if no MOH)*/
	int moh_offset;
	/*! Filename to play. */
	char playfile[0];
};

/*!
 * \internal
 * \brief Handle the playfile bridge action.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel to play a file on.
 * \param payload Action frame payload to play a file.
 *
 * \return Nothing
 */
static void bridge_channel_playfile(struct ast_bridge_channel *bridge_channel, struct bridge_playfile *payload)
{
	ast_bridge_channel_playfile(bridge_channel, payload->custom_play, payload->playfile,
		payload->moh_offset ? &payload->playfile[payload->moh_offset] : NULL);
}

static void payload_helper_playfile(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	struct bridge_playfile *payload;
	size_t len_name = strlen(playfile) + 1;
	size_t len_moh = !moh_class ? 0 : strlen(moh_class) + 1;
	size_t len_payload = sizeof(*payload) + len_name + len_moh;

	/* Fill in play file frame data. */
	payload = alloca(len_payload);
	payload->custom_play = custom_play;
	payload->moh_offset = len_moh ? len_name : 0;
	strcpy(payload->playfile, playfile);/* Safe */
	if (moh_class) {
		strcpy(&payload->playfile[payload->moh_offset], moh_class);/* Safe */
	}

	post_it(bridge_channel, AST_BRIDGE_ACTION_PLAY_FILE, payload, len_payload);
}

void ast_bridge_channel_write_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	payload_helper_playfile(ast_bridge_channel_write_action_data,
		bridge_channel, custom_play, playfile, moh_class);
}

void ast_bridge_channel_queue_playfile(struct ast_bridge_channel *bridge_channel, ast_bridge_custom_play_fn custom_play, const char *playfile, const char *moh_class)
{
	payload_helper_playfile(ast_bridge_channel_queue_action_data,
		bridge_channel, custom_play, playfile, moh_class);
}

struct bridge_park {
	int parker_uuid_offset;
	int app_data_offset;
	/* buffer used for holding those strings */
	char parkee_uuid[0];
};

static void bridge_channel_park(struct ast_bridge_channel *bridge_channel, struct bridge_park *payload)
{
	ast_bridge_channel_park(bridge_channel, payload->parkee_uuid,
		&payload->parkee_uuid[payload->parker_uuid_offset],
		payload->app_data_offset ? &payload->parkee_uuid[payload->app_data_offset] : NULL);
}

static void payload_helper_park(ast_bridge_channel_post_action_data post_it,
	struct ast_bridge_channel *bridge_channel,
	const char *parkee_uuid,
	const char *parker_uuid,
	const char *app_data)
{
	struct bridge_park *payload;
	size_t len_parkee_uuid = strlen(parkee_uuid) + 1;
	size_t len_parker_uuid = strlen(parker_uuid) + 1;
	size_t len_app_data = !app_data ? 0 : strlen(app_data) + 1;
	size_t len_payload = sizeof(*payload) + len_parker_uuid + len_parkee_uuid + len_app_data;

	payload = alloca(len_payload);
	payload->app_data_offset = len_app_data ? len_parkee_uuid + len_parker_uuid : 0;
	payload->parker_uuid_offset = len_parkee_uuid;
	strcpy(payload->parkee_uuid, parkee_uuid);
	strcpy(&payload->parkee_uuid[payload->parker_uuid_offset], parker_uuid);
	if (app_data) {
		strcpy(&payload->parkee_uuid[payload->app_data_offset], app_data);
	}

	post_it(bridge_channel, AST_BRIDGE_ACTION_PARK, payload, len_payload);
}

void ast_bridge_channel_write_park(struct ast_bridge_channel *bridge_channel, const char *parkee_uuid, const char *parker_uuid, const char *app_data)
{
	payload_helper_park(ast_bridge_channel_write_action_data,
		bridge_channel, parkee_uuid, parker_uuid, app_data);
}

/*!
 * \internal
 * \brief Feed notification that a frame is waiting on a channel into the bridging core
 *
 * \param bridge_channel Bridge channel the notification was received on
 */
static void bridge_handle_trip(struct ast_bridge_channel *bridge_channel)
{
	struct ast_frame *frame;

	if (bridge_channel->features->mute) {
		frame = ast_read_noaudio(bridge_channel->chan);
	} else {
		frame = ast_read(bridge_channel->chan);
	}

	if (!frame) {
		bridge_handle_hangup(bridge_channel);
		return;
	}
	switch (frame->frametype) {
	case AST_FRAME_NULL:
		/* Just discard it. */
		ast_frfree(frame);
		return;
	case AST_FRAME_CONTROL:
		switch (frame->subclass.integer) {
		case AST_CONTROL_HANGUP:
			bridge_handle_hangup(bridge_channel);
			ast_frfree(frame);
			return;
/* BUGBUG This is where incoming HOLD/UNHOLD memory should register.  Write UNHOLD into bridge when this channel is pulled. */
		default:
			break;
		}
		break;
	case AST_FRAME_DTMF_BEGIN:
		frame = bridge_handle_dtmf(bridge_channel, frame);
		if (!frame) {
			return;
		}
		/* Fall through */
	case AST_FRAME_DTMF_END:
		if (!bridge_channel->features->dtmf_passthrough) {
			ast_frfree(frame);
			return;
		}
/* BUGBUG This is where incoming DTMF begin/end memory should register.  Write DTMF end into bridge when this channel is pulled. */
		break;
	default:
		break;
	}

/* BUGBUG bridge join or impart needs to do CONNECTED_LINE updates if the channels are being swapped and it is a 1-1 bridge. */

	/* Simply write the frame out to the bridge technology. */
/* BUGBUG The tech is where AST_CONTROL_ANSWER hook should go. (early bridge) */
/* BUGBUG The tech is where incoming BUSY/CONGESTION hangup should happen? (early bridge) */
	bridge_channel_write_frame(bridge_channel, frame);
	ast_frfree(frame);
}

/*!
 * \internal
 * \brief Complete joining new channels to the bridge.
 * \since 12.0.0
 *
 * \param bridge Check for new channels on this bridge.
 *
 * \note On entry, bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_complete_join(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel;

	if (bridge->dissolved) {
		/*
		 * No sense in completing the join on channels for a dissolved
		 * bridge.  They are just going to be removed soon anyway.
		 * However, we do have reason to abort here because the bridge
		 * technology may not be able to handle the number of channels
		 * still in the bridge.
		 */
		return;
	}

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (!bridge_channel->just_joined) {
			continue;
		}

		/* Make the channel compatible with the bridge */
		bridge_make_compatible(bridge, bridge_channel);

		/* Tell the bridge technology we are joining so they set us up */
		ast_debug(1, "Bridge %s: %p(%s) is joining %s technology\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			bridge->technology->name);
		if (bridge->technology->join
			&& bridge->technology->join(bridge, bridge_channel)) {
			ast_debug(1, "Bridge %s: %p(%s) failed to join %s technology\n",
				bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
				bridge->technology->name);
		}

		bridge_channel->just_joined = 0;
	}
}

/*! \brief Helper function used to find the "best" bridge technology given specified capabilities */
static struct ast_bridge_technology *find_best_technology(uint32_t capabilities, struct ast_bridge *bridge)
{
	struct ast_bridge_technology *current;
	struct ast_bridge_technology *best = NULL;

	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, current, entry) {
		if (current->suspended) {
			ast_debug(1, "Bridge technology %s is suspended. Skipping.\n",
				current->name);
			continue;
		}
		if (!(current->capabilities & capabilities)) {
			ast_debug(1, "Bridge technology %s does not have any capabilities we want.\n",
				current->name);
			continue;
		}
		if (best && current->preference <= best->preference) {
			ast_debug(1, "Bridge technology %s has less preference than %s (%d <= %d). Skipping.\n",
				current->name, best->name, current->preference, best->preference);
			continue;
		}
		if (current->compatible && !current->compatible(bridge)) {
			ast_debug(1, "Bridge technology %s is not compatible with properties of existing bridge.\n",
				current->name);
			continue;
		}
		best = current;
	}

	if (best) {
		/* Increment it's module reference count if present so it does not get unloaded while in use */
		ast_module_ref(best->mod);
		ast_debug(1, "Chose bridge technology %s\n", best->name);
	}

	AST_RWLIST_UNLOCK(&bridge_technologies);

	return best;
}

struct tech_deferred_destroy {
	struct ast_bridge_technology *tech;
	void *tech_pvt;
};

/*!
 * \internal
 * \brief Deferred destruction of bridge tech private structure.
 * \since 12.0.0
 *
 * \param bridge What to execute the action on.
 * \param action Deferred bridge tech destruction.
 *
 * \note On entry, bridge must not be locked.
 *
 * \return Nothing
 */
static void bridge_tech_deferred_destroy(struct ast_bridge *bridge, struct ast_frame *action)
{
	struct tech_deferred_destroy *deferred = action->data.ptr;
	struct ast_bridge dummy_bridge = {
		.technology = deferred->tech,
		.tech_pvt = deferred->tech_pvt,
		};

	ast_copy_string(dummy_bridge.uniqueid, bridge->uniqueid, sizeof(dummy_bridge.uniqueid));
	ast_debug(1, "Bridge %s: calling %s technology destructor (deferred, dummy)\n",
		dummy_bridge.uniqueid, dummy_bridge.technology->name);
	dummy_bridge.technology->destroy(&dummy_bridge);
	ast_module_unref(dummy_bridge.technology->mod);
}

/*!
 * \internal
 * \brief Handle bridge action frame.
 * \since 12.0.0
 *
 * \param bridge What to execute the action on.
 * \param action What to do.
 *
 * \note On entry, bridge is already locked.
 * \note Can be called by the bridge destructor.
 *
 * \return Nothing
 */
static void bridge_action_bridge(struct ast_bridge *bridge, struct ast_frame *action)
{
#if 0	/* In case we need to know when the destructor is calling us. */
	int in_destructor = !ao2_ref(bridge, 0);
#endif

	switch (action->subclass.integer) {
	case AST_BRIDGE_ACTION_DEFERRED_TECH_DESTROY:
		ast_bridge_unlock(bridge);
		bridge_tech_deferred_destroy(bridge, action);
		ast_bridge_lock(bridge);
		break;
	case AST_BRIDGE_ACTION_DEFERRED_DISSOLVING:
		ast_bridge_unlock(bridge);
		bridge->v_table->dissolving(bridge);
		ast_bridge_lock(bridge);
		break;
	default:
		/* Unexpected deferred action type.  Should never happen. */
		ast_assert(0);
		break;
	}
}

/*!
 * \internal
 * \brief Do any pending bridge actions.
 * \since 12.0.0
 *
 * \param bridge What to do actions on.
 *
 * \note On entry, bridge is already locked.
 * \note Can be called by the bridge destructor.
 *
 * \return Nothing
 */
static void bridge_handle_actions(struct ast_bridge *bridge)
{
	struct ast_frame *action;

	while ((action = AST_LIST_REMOVE_HEAD(&bridge->action_queue, frame_list))) {
		switch (action->frametype) {
		case AST_FRAME_BRIDGE_ACTION:
			bridge_action_bridge(bridge, action);
			break;
		default:
			/* Unexpected deferred frame type.  Should never happen. */
			ast_assert(0);
			break;
		}
		ast_frfree(action);
	}
}

static void destroy_bridge(void *obj)
{
	struct ast_bridge *bridge = obj;
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);

	ast_debug(1, "Bridge %s: actually destroying %s bridge, nobody wants it anymore\n",
		bridge->uniqueid, bridge->v_table->name);

	msg = stasis_cache_clear_create(ast_bridge_snapshot_type(), bridge->uniqueid);
	if (msg) {
		stasis_publish(ast_bridge_topic(bridge), msg);
	}

	/* Do any pending actions in the context of destruction. */
	ast_bridge_lock(bridge);
	bridge_handle_actions(bridge);
	ast_bridge_unlock(bridge);

	/* There should not be any channels left in the bridge. */
	ast_assert(AST_LIST_EMPTY(&bridge->channels));

	ast_debug(1, "Bridge %s: calling %s bridge destructor\n",
		bridge->uniqueid, bridge->v_table->name);
	bridge->v_table->destroy(bridge);

	/* Pass off the bridge to the technology to destroy if needed */
	if (bridge->technology) {
		ast_debug(1, "Bridge %s: calling %s technology destructor\n",
			bridge->uniqueid, bridge->technology->name);
		if (bridge->technology->destroy) {
			bridge->technology->destroy(bridge);
		}
		ast_module_unref(bridge->technology->mod);
		bridge->technology = NULL;
	}

	if (bridge->callid) {
		bridge->callid = ast_callid_unref(bridge->callid);
	}

	cleanup_video_mode(bridge);
}

struct ast_bridge *ast_bridge_register(struct ast_bridge *bridge)
{
	if (bridge) {
		ast_bridge_publish_state(bridge);
		if (!ao2_link(bridges, bridge)) {
			ast_bridge_destroy(bridge);
			bridge = NULL;
		}
	}
	return bridge;
}

struct ast_bridge *ast_bridge_alloc(size_t size, const struct ast_bridge_methods *v_table)
{
	struct ast_bridge *bridge;

	/* Check v_table that all methods are present. */
	if (!v_table
		|| !v_table->name
		|| !v_table->destroy
		|| !v_table->dissolving
		|| !v_table->push
		|| !v_table->pull
		|| !v_table->notify_masquerade
		|| !v_table->get_merge_priority) {
		ast_log(LOG_ERROR, "Virtual method table for bridge class %s not complete.\n",
			v_table && v_table->name ? v_table->name : "<unknown>");
		ast_assert(0);
		return NULL;
	}

	bridge = ao2_alloc(size, destroy_bridge);
	if (bridge) {
		bridge->v_table = v_table;
	}
	return bridge;
}

struct ast_bridge *ast_bridge_base_init(struct ast_bridge *self, uint32_t capabilities, unsigned int flags)
{
	if (!self) {
		return NULL;
	}

	ast_uuid_generate_str(self->uniqueid, sizeof(self->uniqueid));
	ast_set_flag(&self->feature_flags, flags);
	self->allowed_capabilities = capabilities;

	/* Use our helper function to find the "best" bridge technology. */
	self->technology = find_best_technology(capabilities, self);
	if (!self->technology) {
		ast_debug(1, "Bridge %s: Could not create.  No technology available to support it.\n",
			self->uniqueid);
		ao2_ref(self, -1);
		return NULL;
	}

	/* Pass off the bridge to the technology to manipulate if needed */
	ast_debug(1, "Bridge %s: calling %s technology constructor\n",
		self->uniqueid, self->technology->name);
	if (self->technology->create && self->technology->create(self)) {
		ast_debug(1, "Bridge %s: failed to setup %s technology\n",
			self->uniqueid, self->technology->name);
		ao2_ref(self, -1);
		return NULL;
	}

	if (!ast_bridge_topic(self)) {
		ao2_ref(self, -1);
		return NULL;
	}

	return self;
}

/*!
 * \internal
 * \brief ast_bridge base class destructor.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 *
 * \note Stub because of nothing to do.
 *
 * \return Nothing
 */
static void bridge_base_destroy(struct ast_bridge *self)
{
}

/*!
 * \internal
 * \brief The bridge is being dissolved.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 *
 * \return Nothing
 */
static void bridge_base_dissolving(struct ast_bridge *self)
{
	ao2_unlink(bridges, self);
}

/*!
 * \internal
 * \brief ast_bridge base push method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to push.
 * \param swap Bridge channel to swap places with if not NULL.
 *
 * \note On entry, self is already locked.
 * \note Stub because of nothing to do.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int bridge_base_push(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel, struct ast_bridge_channel *swap)
{
	return 0;
}

/*!
 * \internal
 * \brief ast_bridge base pull method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel to pull.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
static void bridge_base_pull(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	bridge_features_remove_on_pull(bridge_channel->features);
}

/*!
 * \internal
 * \brief ast_bridge base notify_masquerade method.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 * \param bridge_channel Bridge channel that was masqueraded.
 *
 * \note On entry, self is already locked.
 *
 * \return Nothing
 */
static void bridge_base_notify_masquerade(struct ast_bridge *self, struct ast_bridge_channel *bridge_channel)
{
	self->reconfigured = 1;
}

/*!
 * \internal
 * \brief Get the merge priority of this bridge.
 * \since 12.0.0
 *
 * \param self Bridge to operate upon.
 *
 * \note On entry, self is already locked.
 *
 * \return Merge priority
 */
static int bridge_base_get_merge_priority(struct ast_bridge *self)
{
	return 0;
}

struct ast_bridge_methods ast_bridge_base_v_table = {
	.name = "base",
	.destroy = bridge_base_destroy,
	.dissolving = bridge_base_dissolving,
	.push = bridge_base_push,
	.pull = bridge_base_pull,
	.notify_masquerade = bridge_base_notify_masquerade,
	.get_merge_priority = bridge_base_get_merge_priority,
};

struct ast_bridge *ast_bridge_base_new(uint32_t capabilities, unsigned int flags)
{
	void *bridge;

	bridge = ast_bridge_alloc(sizeof(struct ast_bridge), &ast_bridge_base_v_table);
	bridge = ast_bridge_base_init(bridge, capabilities, flags);
	bridge = ast_bridge_register(bridge);
	return bridge;
}

int ast_bridge_destroy(struct ast_bridge *bridge)
{
	ast_debug(1, "Bridge %s: telling all channels to leave the party\n", bridge->uniqueid);
	ast_bridge_lock(bridge);
	bridge_dissolve(bridge);
	ast_bridge_unlock(bridge);

	ao2_ref(bridge, -1);

	return 0;
}

static int bridge_make_compatible(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_format read_format;
	struct ast_format write_format;
	struct ast_format best_format;
	char codec_buf[512];

	ast_format_copy(&read_format, ast_channel_readformat(bridge_channel->chan));
	ast_format_copy(&write_format, ast_channel_writeformat(bridge_channel->chan));

	/* Are the formats currently in use something this bridge can handle? */
	if (!ast_format_cap_iscompatible(bridge->technology->format_capabilities, ast_channel_readformat(bridge_channel->chan))) {
		ast_best_codec(bridge->technology->format_capabilities, &best_format);

		/* Read format is a no go... */
		ast_debug(1, "Bridge technology %s wants to read any of formats %s but channel has %s\n",
			bridge->technology->name,
			ast_getformatname_multiple(codec_buf, sizeof(codec_buf), bridge->technology->format_capabilities),
			ast_getformatname(&read_format));

		/* Switch read format to the best one chosen */
		if (ast_set_read_format(bridge_channel->chan, &best_format)) {
			ast_log(LOG_WARNING, "Failed to set channel %s to read format %s\n",
				ast_channel_name(bridge_channel->chan), ast_getformatname(&best_format));
			return -1;
		}
		ast_debug(1, "Bridge %s put channel %s into read format %s\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan),
			ast_getformatname(&best_format));
	} else {
		ast_debug(1, "Bridge %s is happy that channel %s already has read format %s\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan),
			ast_getformatname(&read_format));
	}

	if (!ast_format_cap_iscompatible(bridge->technology->format_capabilities, &write_format)) {
		ast_best_codec(bridge->technology->format_capabilities, &best_format);

		/* Write format is a no go... */
		ast_debug(1, "Bridge technology %s wants to write any of formats %s but channel has %s\n",
			bridge->technology->name,
			ast_getformatname_multiple(codec_buf, sizeof(codec_buf), bridge->technology->format_capabilities),
			ast_getformatname(&write_format));

		/* Switch write format to the best one chosen */
		if (ast_set_write_format(bridge_channel->chan, &best_format)) {
			ast_log(LOG_WARNING, "Failed to set channel %s to write format %s\n",
				ast_channel_name(bridge_channel->chan), ast_getformatname(&best_format));
			return -1;
		}
		ast_debug(1, "Bridge %s put channel %s into write format %s\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan),
			ast_getformatname(&best_format));
	} else {
		ast_debug(1, "Bridge %s is happy that channel %s already has write format %s\n",
			bridge->uniqueid, ast_channel_name(bridge_channel->chan),
			ast_getformatname(&write_format));
	}

	return 0;
}

/*!
 * \internal
 * \brief Perform the smart bridge operation.
 * \since 12.0.0
 *
 * \param bridge Work on this bridge.
 *
 * \details
 * Basically see if a new bridge technology should be used instead
 * of the current one.
 *
 * \note On entry, bridge is already locked.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int smart_bridge_operation(struct ast_bridge *bridge)
{
	uint32_t new_capabilities;
	struct ast_bridge_technology *new_technology;
	struct ast_bridge_technology *old_technology = bridge->technology;
	struct ast_bridge_channel *bridge_channel;
	struct ast_frame *deferred_action;
	struct ast_bridge dummy_bridge = {
		.technology = bridge->technology,
		.tech_pvt = bridge->tech_pvt,
	};

	if (bridge->dissolved) {
		ast_debug(1, "Bridge %s is dissolved, not performing smart bridge operation.\n",
			bridge->uniqueid);
		return 0;
	}

	/* Determine new bridge technology capabilities needed. */
	if (2 < bridge->num_channels) {
		new_capabilities = AST_BRIDGE_CAPABILITY_MULTIMIX;
		new_capabilities &= bridge->allowed_capabilities;
	} else {
		new_capabilities = AST_BRIDGE_CAPABILITY_NATIVE | AST_BRIDGE_CAPABILITY_1TO1MIX;
		new_capabilities &= bridge->allowed_capabilities;
		if (!new_capabilities
			&& (bridge->allowed_capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX)) {
			/* Allow switching between different multimix bridge technologies. */
			new_capabilities = AST_BRIDGE_CAPABILITY_MULTIMIX;
		}
	}

	/* Find a bridge technology to satisfy the new capabilities. */
	new_technology = find_best_technology(new_capabilities, bridge);
	if (!new_technology) {
		int is_compatible = 0;

		if (old_technology->compatible) {
			is_compatible = old_technology->compatible(bridge);
		} else if (old_technology->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX) {
			is_compatible = 1;
		} else if (bridge->num_channels <= 2
			&& (old_technology->capabilities & AST_BRIDGE_CAPABILITY_1TO1MIX)) {
			is_compatible = 1;
		}

		if (is_compatible) {
			ast_debug(1, "Bridge %s could not get a new technology, staying with old technology.\n",
				bridge->uniqueid);
			return 0;
		}
		ast_log(LOG_WARNING, "Bridge %s has no technology available to support it.\n",
			bridge->uniqueid);
		return -1;
	}
	if (new_technology == old_technology) {
		ast_debug(1, "Bridge %s is already using the new technology.\n",
			bridge->uniqueid);
		ast_module_unref(old_technology->mod);
		return 0;
	}

	ast_copy_string(dummy_bridge.uniqueid, bridge->uniqueid, sizeof(dummy_bridge.uniqueid));

	if (old_technology->destroy) {
		struct tech_deferred_destroy deferred_tech_destroy = {
			.tech = dummy_bridge.technology,
			.tech_pvt = dummy_bridge.tech_pvt,
		};
		struct ast_frame action = {
			.frametype = AST_FRAME_BRIDGE_ACTION,
			.subclass.integer = AST_BRIDGE_ACTION_DEFERRED_TECH_DESTROY,
			.data.ptr = &deferred_tech_destroy,
			.datalen = sizeof(deferred_tech_destroy),
		};

		/*
		 * We need to defer the bridge technology destroy callback
		 * because we have the bridge locked.
		 */
		deferred_action = ast_frdup(&action);
		if (!deferred_action) {
			ast_module_unref(new_technology->mod);
			return -1;
		}
	} else {
		deferred_action = NULL;
	}

	/*
	 * We are now committed to changing the bridge technology.  We
	 * must not release the bridge lock until we have installed the
	 * new bridge technology.
	 */
	ast_debug(1, "Bridge %s: switching %s technology to %s\n",
		bridge->uniqueid, old_technology->name, new_technology->name);

	/*
	 * Since we are soon going to pass this bridge to a new
	 * technology we need to NULL out the tech_pvt pointer but
	 * don't worry as it still exists in dummy_bridge, ditto for the
	 * old technology.
	 */
	bridge->tech_pvt = NULL;
	bridge->technology = new_technology;

	/* Setup the new bridge technology. */
	ast_debug(1, "Bridge %s: calling %s technology constructor\n",
		bridge->uniqueid, new_technology->name);
	if (new_technology->create && new_technology->create(bridge)) {
		ast_log(LOG_WARNING, "Bridge %s: failed to setup bridge technology %s\n",
			bridge->uniqueid, new_technology->name);
		bridge->tech_pvt = dummy_bridge.tech_pvt;
		bridge->technology = dummy_bridge.technology;
		ast_module_unref(new_technology->mod);
		return -1;
	}

	/* Move existing channels over to the new technology. */
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (bridge_channel->just_joined) {
			/*
			 * This channel has not completed joining the bridge so it is
			 * not in the old bridge technology.
			 */
			continue;
		}

		/* First we part them from the old technology */
		ast_debug(1, "Bridge %s: %p(%s) is leaving %s technology (dummy)\n",
			dummy_bridge.uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			old_technology->name);
		if (old_technology->leave) {
			old_technology->leave(&dummy_bridge, bridge_channel);
		}

		/* Second we make them compatible again with the bridge */
		bridge_make_compatible(bridge, bridge_channel);

		/* Third we join them to the new technology */
		ast_debug(1, "Bridge %s: %p(%s) is joining %s technology\n",
			bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
			new_technology->name);
		if (new_technology->join && new_technology->join(bridge, bridge_channel)) {
			ast_debug(1, "Bridge %s: %p(%s) failed to join %s technology\n",
				bridge->uniqueid, bridge_channel, ast_channel_name(bridge_channel->chan),
				new_technology->name);
		}
	}

	/*
	 * Now that all the channels have been moved over we need to get
	 * rid of all the information the old technology may have left
	 * around.
	 */
	if (old_technology->destroy) {
		ast_debug(1, "Bridge %s: deferring %s technology destructor\n",
			bridge->uniqueid, old_technology->name);
		bridge_queue_action_nodup(bridge, deferred_action);
	} else {
		ast_debug(1, "Bridge %s: calling %s technology destructor\n",
			bridge->uniqueid, old_technology->name);
		ast_module_unref(old_technology->mod);
	}

	return 0;
}

/*!
 * \internal
 * \brief Notify the bridge that it has been reconfigured.
 * \since 12.0.0
 *
 * \param bridge Reconfigured bridge.
 *
 * \details
 * After a series of bridge_channel_push and
 * bridge_channel_pull calls, you need to call this function
 * to cause the bridge to complete restruturing for the change
 * in the channel makeup of the bridge.
 *
 * \note On entry, the bridge is already locked.
 *
 * \return Nothing
 */
static void bridge_reconfigured(struct ast_bridge *bridge)
{
	if (!bridge->reconfigured) {
		return;
	}
	bridge->reconfigured = 0;
	if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_SMART)
		&& smart_bridge_operation(bridge)) {
		/* Smart bridge failed. */
		bridge_dissolve(bridge);
		return;
	}
	bridge_complete_join(bridge);
}

/*!
 * \internal
 * \brief Suspend a channel from a bridge.
 *
 * \param bridge_channel Channel to suspend.
 *
 * \note This function assumes bridge_channel->bridge is locked.
 *
 * \return Nothing
 */
static void bridge_channel_suspend_nolock(struct ast_bridge_channel *bridge_channel)
{
	bridge_channel->suspended = 1;
	if (bridge_channel->in_bridge) {
		--bridge_channel->bridge->num_active;
	}

	/* Get technology bridge threads off of the channel. */
	if (bridge_channel->bridge->technology->suspend) {
		bridge_channel->bridge->technology->suspend(bridge_channel->bridge, bridge_channel);
	}
}

/*!
 * \internal
 * \brief Suspend a channel from a bridge.
 *
 * \param bridge_channel Channel to suspend.
 *
 * \return Nothing
 */
static void bridge_channel_suspend(struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge_channel_suspend_nolock(bridge_channel);
	ast_bridge_unlock(bridge_channel->bridge);
}

/*!
 * \internal
 * \brief Unsuspend a channel from a bridge.
 *
 * \param bridge_channel Channel to unsuspend.
 *
 * \note This function assumes bridge_channel->bridge is locked.
 *
 * \return Nothing
 */
static void bridge_channel_unsuspend_nolock(struct ast_bridge_channel *bridge_channel)
{
	bridge_channel->suspended = 0;
	if (bridge_channel->in_bridge) {
		++bridge_channel->bridge->num_active;
	}

	/* Wake technology bridge threads to take care of channel again. */
	if (bridge_channel->bridge->technology->unsuspend) {
		bridge_channel->bridge->technology->unsuspend(bridge_channel->bridge, bridge_channel);
	}

	/* Wake suspended channel. */
	ast_bridge_channel_lock(bridge_channel);
	ast_cond_signal(&bridge_channel->cond);
	ast_bridge_channel_unlock(bridge_channel);
}

/*!
 * \internal
 * \brief Unsuspend a channel from a bridge.
 *
 * \param bridge_channel Channel to unsuspend.
 *
 * \return Nothing
 */
static void bridge_channel_unsuspend(struct ast_bridge_channel *bridge_channel)
{
	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge_channel_unsuspend_nolock(bridge_channel);
	ast_bridge_unlock(bridge_channel->bridge);
}

/*! \brief Internal function that activates interval hooks on a bridge channel */
static void bridge_channel_interval(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_hook *hook;
	struct timeval start;

	ast_heap_wrlock(bridge_channel->features->interval_hooks);
	start = ast_tvnow();
	while ((hook = ast_heap_peek(bridge_channel->features->interval_hooks, 1))) {
		int interval;
		unsigned int execution_time;

		if (ast_tvdiff_ms(hook->parms.timer.trip_time, start) > 0) {
			ast_debug(1, "Hook %p on %p(%s) wants to happen in the future, stopping our traversal\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}
		ao2_ref(hook, +1);
		ast_heap_unlock(bridge_channel->features->interval_hooks);

		ast_debug(1, "Executing hook %p on %p(%s)\n",
			hook, bridge_channel, ast_channel_name(bridge_channel->chan));
		interval = hook->callback(bridge_channel->bridge, bridge_channel, hook->hook_pvt);

		ast_heap_wrlock(bridge_channel->features->interval_hooks);
		if (ast_heap_peek(bridge_channel->features->interval_hooks,
			hook->parms.timer.heap_index) != hook
			|| !ast_heap_remove(bridge_channel->features->interval_hooks, hook)) {
			/* Interval hook is already removed from the bridge_channel. */
			ao2_ref(hook, -1);
			continue;
		}
		ao2_ref(hook, -1);

		if (interval < 0) {
			ast_debug(1, "Removed interval hook %p from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_ref(hook, -1);
			continue;
		}
		if (interval) {
			/* Set new interval for the hook. */
			hook->parms.timer.interval = interval;
		}

		ast_debug(1, "Updating interval hook %p with interval %u on %p(%s)\n",
			hook, hook->parms.timer.interval, bridge_channel,
			ast_channel_name(bridge_channel->chan));

		/* resetting start */
		start = ast_tvnow();

		/*
		 * Resetup the interval hook for the next interval.  We may need
		 * to skip over any missed intervals because the hook was
		 * delayed or took too long.
		 */
		execution_time = ast_tvdiff_ms(start, hook->parms.timer.trip_time);
		while (hook->parms.timer.interval < execution_time) {
			execution_time -= hook->parms.timer.interval;
		}
		hook->parms.timer.trip_time = ast_tvadd(start, ast_samp2tv(hook->parms.timer.interval - execution_time, 1000));
		hook->parms.timer.seqno = ast_atomic_fetchadd_int((int *) &bridge_channel->features->interval_sequence, +1);

		if (ast_heap_push(bridge_channel->features->interval_hooks, hook)) {
			/* Could not push the hook back onto the heap. */
			ao2_ref(hook, -1);
		}
	}
	ast_heap_unlock(bridge_channel->features->interval_hooks);
}

static void bridge_channel_write_dtmf_stream(struct ast_bridge_channel *bridge_channel, const char *dtmf)
{
	ast_bridge_channel_write_action_data(bridge_channel,
		AST_BRIDGE_ACTION_DTMF_STREAM, dtmf, strlen(dtmf) + 1);
}

/*!
 * \brief Internal function that executes a feature on a bridge channel
 * \note Neither the bridge nor the bridge_channel locks should be held when entering
 * this function.
 */
static void bridge_channel_feature(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook = NULL;
	char dtmf[MAXIMUM_DTMF_FEATURE_STRING] = "";
	size_t dtmf_len = 0;

	/* The channel is now under our control and we don't really want any begin frames to do our DTMF matching so disable 'em at the core level */
	ast_set_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_END_DTMF_ONLY);

	/* Wait for DTMF on the channel and put it into a buffer. If the buffer matches any feature hook execute the hook. */
	do {
		int res;

		/* If the above timed out simply exit */
		res = ast_waitfordigit(bridge_channel->chan, 3000);
		if (!res) {
			ast_debug(1, "DTMF feature string collection on %p(%s) timed out\n",
				bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}
		if (res < 0) {
			ast_debug(1, "DTMF feature string collection failed on %p(%s) for some reason\n",
				bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}

/* BUGBUG need to record the duration of DTMF digits so when the string is played back, they are reproduced. */
		/* Add the above DTMF into the DTMF string so we can do our matching */
		dtmf[dtmf_len++] = res;
		ast_debug(1, "DTMF feature string on %p(%s) is now '%s'\n",
			bridge_channel, ast_channel_name(bridge_channel->chan), dtmf);

		/* See if a DTMF feature hook matches or can match */
		hook = ao2_find(features->dtmf_hooks, dtmf, OBJ_PARTIAL_KEY);
		if (!hook) {
			ast_debug(1, "No DTMF feature hooks on %p(%s) match '%s'\n",
				bridge_channel, ast_channel_name(bridge_channel->chan), dtmf);
			break;
		}
		if (strlen(hook->parms.dtmf.code) == dtmf_len) {
			ast_debug(1, "DTMF feature hook %p matched DTMF string '%s' on %p(%s)\n",
				hook, dtmf, bridge_channel, ast_channel_name(bridge_channel->chan));
			break;
		}
		ao2_ref(hook, -1);
		hook = NULL;

		/* Stop if we have reached the maximum length of a DTMF feature string. */
	} while (dtmf_len < ARRAY_LEN(dtmf) - 1);

	/* Since we are done bringing DTMF in return to using both begin and end frames */
	ast_clear_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_END_DTMF_ONLY);

	/* If a hook was actually matched execute it on this channel, otherwise stream up the DTMF to the other channels */
	if (hook) {
		int failed;

		failed = hook->callback(bridge_channel->bridge, bridge_channel, hook->hook_pvt);
		if (failed) {
			ast_debug(1, "DTMF hook %p is being removed from %p(%s)\n",
				hook, bridge_channel, ast_channel_name(bridge_channel->chan));
			ao2_unlink(features->dtmf_hooks, hook);
		}
		ao2_ref(hook, -1);

		/*
		 * If we are handing the channel off to an external hook for
		 * ownership, we are not guaranteed what kind of state it will
		 * come back in.  If the channel hungup, we need to detect that
		 * here if the hook did not already change the state.
		 */
		if (bridge_channel->chan && ast_check_hangup_locked(bridge_channel->chan)) {
			bridge_handle_hangup(bridge_channel);
		}
	} else if (features->dtmf_passthrough) {
		bridge_channel_write_dtmf_stream(bridge_channel, dtmf);
	}
}

static void bridge_channel_talking(struct ast_bridge_channel *bridge_channel, int talking)
{
	struct ast_bridge_features *features = bridge_channel->features;

	if (features->talker_cb) {
		features->talker_cb(bridge_channel, features->talker_pvt_data, talking);
	}
}

/*! \brief Internal function that plays back DTMF on a bridge channel */
static void bridge_channel_dtmf_stream(struct ast_bridge_channel *bridge_channel, const char *dtmf)
{
	ast_debug(1, "Playing DTMF stream '%s' out to %p(%s)\n",
		dtmf, bridge_channel, ast_channel_name(bridge_channel->chan));
	ast_dtmf_stream(bridge_channel->chan, NULL, dtmf, 0, 0);
}

struct blind_transfer_data {
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_CONTEXT];
};

static void bridge_channel_blind_transfer(struct ast_bridge_channel *bridge_channel,
		struct blind_transfer_data *blind_data)
{
	ast_async_goto(bridge_channel->chan, blind_data->context, blind_data->exten, 1);
	bridge_handle_hangup(bridge_channel);
}

static void after_bridge_move_channel(struct ast_channel *chan_bridged, void *data)
{
	RAII_VAR(struct ast_channel *, chan_target, data, ao2_cleanup);
	ast_channel_move(chan_target, chan_bridged);
}

static void after_bridge_move_channel_fail(enum ast_after_bridge_cb_reason reason, void *data)
{
	RAII_VAR(struct ast_channel *, chan_target, data, ao2_cleanup);

	ast_log(LOG_WARNING, "Unable to complete transfer: %s\n",
			ast_after_bridge_cb_reason_string(reason));
}

static void bridge_channel_attended_transfer(struct ast_bridge_channel *bridge_channel,
		const char *target_chan_name)
{
	RAII_VAR(struct ast_channel *, chan_target, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan_bridged, NULL, ao2_cleanup);

	chan_target = ast_channel_get_by_name(target_chan_name);
	if (!chan_target) {
		/* Dang, it disappeared somehow */
		return;
	}

	{
		SCOPED_CHANNELLOCK(lock, bridge_channel);
		chan_bridged = bridge_channel->chan;
		if (!chan_bridged) {
			return;
		}
		ao2_ref(chan_bridged, +1);
	}

	if (ast_after_bridge_callback_set(chan_bridged, after_bridge_move_channel,
			after_bridge_move_channel_fail, ast_channel_ref(chan_target))) {
		return;
	}
	bridge_handle_hangup(bridge_channel);
}

/*!
 * \internal
 * \brief Handle bridge channel bridge action frame.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to execute the action on.
 * \param action What to do.
 *
 * \return Nothing
 */
static void bridge_channel_handle_action(struct ast_bridge_channel *bridge_channel, struct ast_frame *action)
{
	switch (action->subclass.integer) {
	case AST_BRIDGE_ACTION_INTERVAL:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_interval(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case AST_BRIDGE_ACTION_FEATURE:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_feature(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case AST_BRIDGE_ACTION_DTMF_STREAM:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_dtmf_stream(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case AST_BRIDGE_ACTION_TALKING_START:
	case AST_BRIDGE_ACTION_TALKING_STOP:
		bridge_channel_talking(bridge_channel,
			action->subclass.integer == AST_BRIDGE_ACTION_TALKING_START);
		break;
	case AST_BRIDGE_ACTION_PLAY_FILE:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_playfile(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case AST_BRIDGE_ACTION_PARK:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_park(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case AST_BRIDGE_ACTION_RUN_APP:
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_run_app(bridge_channel, action->data.ptr);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
		break;
	case AST_BRIDGE_ACTION_BLIND_TRANSFER:
		bridge_channel_blind_transfer(bridge_channel, action->data.ptr);
		break;
	case AST_BRIDGE_ACTION_ATTENDED_TRANSFER:
		bridge_channel_attended_transfer(bridge_channel, action->data.ptr);
		break;
	default:
		break;
	}
}

/*!
 * \internal
 * \brief Handle bridge channel control frame action.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to execute the control frame action on.
 * \param fr Control frame to handle.
 *
 * \return Nothing
 */
static void bridge_channel_handle_control(struct ast_bridge_channel *bridge_channel, struct ast_frame *fr)
{
	struct ast_channel *chan;
	struct ast_option_header *aoh;
	int is_caller;
	int intercept_failed;

	chan = bridge_channel->chan;
	switch (fr->subclass.integer) {
	case AST_CONTROL_REDIRECTING:
		is_caller = !ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING);
		bridge_channel_suspend(bridge_channel);
		intercept_failed = ast_channel_redirecting_sub(NULL, chan, fr, 1)
			&& ast_channel_redirecting_macro(NULL, chan, fr, is_caller, 1);
		bridge_channel_unsuspend(bridge_channel);
		if (intercept_failed) {
			ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		}
		break;
	case AST_CONTROL_CONNECTED_LINE:
		is_caller = !ast_test_flag(ast_channel_flags(chan), AST_FLAG_OUTGOING);
		bridge_channel_suspend(bridge_channel);
		intercept_failed = ast_channel_connected_line_sub(NULL, chan, fr, 1)
			&& ast_channel_connected_line_macro(NULL, chan, fr, is_caller, 1);
		bridge_channel_unsuspend(bridge_channel);
		if (intercept_failed) {
			ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		}
		break;
	case AST_CONTROL_HOLD:
	case AST_CONTROL_UNHOLD:
/*
 * BUGBUG bridge_channels should remember sending/receiving an outstanding HOLD to/from the bridge
 *
 * When the sending channel is pulled from the bridge it needs to write into the bridge an UNHOLD before being pulled.
 * When the receiving channel is pulled from the bridge it needs to generate its own UNHOLD.
 * Something similar needs to be done for DTMF begin/end.
 */
	case AST_CONTROL_VIDUPDATE:
	case AST_CONTROL_SRCUPDATE:
	case AST_CONTROL_SRCCHANGE:
	case AST_CONTROL_T38_PARAMETERS:
/* BUGBUG may have to do something with a jitter buffer for these. */
		ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		break;
	case AST_CONTROL_OPTION:
		/*
		 * Forward option Requests, but only ones we know are safe These
		 * are ONLY sent by chan_iax2 and I'm not convinced that they
		 * are useful. I haven't deleted them entirely because I just am
		 * not sure of the ramifications of removing them.
		 */
		aoh = fr->data.ptr;
		if (aoh && aoh->flag == AST_OPTION_FLAG_REQUEST) {
			switch (ntohs(aoh->option)) {
			case AST_OPTION_TONE_VERIFY:
			case AST_OPTION_TDD:
			case AST_OPTION_RELAXDTMF:
			case AST_OPTION_AUDIO_MODE:
			case AST_OPTION_DIGIT_DETECT:
			case AST_OPTION_FAX_DETECT:
				ast_channel_setoption(chan, ntohs(aoh->option), aoh->data,
					fr->datalen - sizeof(*aoh), 0);
				break;
			default:
				break;
			}
		}
		break;
	case AST_CONTROL_ANSWER:
		if (ast_channel_state(chan) != AST_STATE_UP) {
			ast_answer(chan);
		} else {
			ast_indicate(chan, -1);
		}
		break;
	default:
		ast_indicate_data(chan, fr->subclass.integer, fr->data.ptr, fr->datalen);
		break;
	}
}

/*!
 * \internal
 * \brief Handle bridge channel write frame to channel.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to write outgoing frame.
 *
 * \return Nothing
 */
static void bridge_channel_handle_write(struct ast_bridge_channel *bridge_channel)
{
	struct ast_frame *fr;
	char nudge;

	ast_bridge_channel_lock(bridge_channel);
	if (read(bridge_channel->alert_pipe[0], &nudge, sizeof(nudge)) < 0) {
		if (errno != EINTR && errno != EAGAIN) {
			ast_log(LOG_WARNING, "read() failed for alert pipe on %p(%s): %s\n",
				bridge_channel, ast_channel_name(bridge_channel->chan), strerror(errno));
		}
	}
	fr = AST_LIST_REMOVE_HEAD(&bridge_channel->wr_queue, frame_list);
	ast_bridge_channel_unlock(bridge_channel);
	if (!fr) {
		return;
	}
	switch (fr->frametype) {
	case AST_FRAME_BRIDGE_ACTION:
		bridge_channel_handle_action(bridge_channel, fr);
		break;
	case AST_FRAME_CONTROL:
		bridge_channel_handle_control(bridge_channel, fr);
		break;
	case AST_FRAME_NULL:
		break;
	default:
		/* Write the frame to the channel. */
		bridge_channel->activity = AST_BRIDGE_CHANNEL_THREAD_SIMPLE;
		ast_write(bridge_channel->chan, fr);
		break;
	}
	ast_frfree(fr);
}

/*!
 * \internal
 * \brief Handle bridge channel interval expiration.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to check interval on.
 *
 * \return Nothing
 */
static void bridge_channel_handle_interval(struct ast_bridge_channel *bridge_channel)
{
	struct ast_timer *interval_timer;

	interval_timer = bridge_channel->features->interval_timer;
	if (interval_timer) {
		if (ast_wait_for_input(ast_timer_fd(interval_timer), 0) == 1) {
			ast_timer_ack(interval_timer, 1);
			if (bridge_channel_interval_ready(bridge_channel)) {
/* BUGBUG since this is now only run by the channel thread, there is no need to queue the action once this intervals become a first class wait item in bridge_channel_wait(). */
				struct ast_frame interval_action = {
					.frametype = AST_FRAME_BRIDGE_ACTION,
					.subclass.integer = AST_BRIDGE_ACTION_INTERVAL,
				};

				ast_bridge_channel_queue_frame(bridge_channel, &interval_action);
			}
		}
	}
}

/*!
 * \internal
 * \brief Wait for something to happen on the bridge channel and handle it.
 * \since 12.0.0
 *
 * \param bridge_channel Channel to wait.
 *
 * \note Each channel does writing/reading in their own thread.
 *
 * \return Nothing
 */
static void bridge_channel_wait(struct ast_bridge_channel *bridge_channel)
{
	int ms = -1;
	int outfd;
	struct ast_channel *chan;

	/* Wait for data to either come from the channel or us to be signaled */
	ast_bridge_channel_lock(bridge_channel);
	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
	} else if (bridge_channel->suspended) {
/* BUGBUG the external party use of suspended will go away as will these references because this is the bridge channel thread */
		ast_debug(1, "Bridge %s: %p(%s) is going into a signal wait\n",
			bridge_channel->bridge->uniqueid, bridge_channel,
			ast_channel_name(bridge_channel->chan));
		ast_cond_wait(&bridge_channel->cond, ao2_object_get_lockaddr(bridge_channel));
	} else {
		ast_debug(10, "Bridge %s: %p(%s) is going into a waitfor\n",
			bridge_channel->bridge->uniqueid, bridge_channel,
			ast_channel_name(bridge_channel->chan));
		bridge_channel->waiting = 1;
		ast_bridge_channel_unlock(bridge_channel);
		outfd = -1;
/* BUGBUG need to make the next expiring active interval setup ms timeout rather than holding up the chan reads. */
		chan = ast_waitfor_nandfds(&bridge_channel->chan, 1,
			&bridge_channel->alert_pipe[0], 1, NULL, &outfd, &ms);
		bridge_channel->waiting = 0;
		if (ast_channel_softhangup_internal_flag(bridge_channel->chan) & AST_SOFTHANGUP_UNBRIDGE) {
			ast_channel_clear_softhangup(bridge_channel->chan, AST_SOFTHANGUP_UNBRIDGE);
			ast_bridge_channel_lock_bridge(bridge_channel);
			bridge_channel->bridge->reconfigured = 1;
			bridge_reconfigured(bridge_channel->bridge);
			ast_bridge_unlock(bridge_channel->bridge);
		}
		ast_bridge_channel_lock(bridge_channel);
		bridge_channel->activity = AST_BRIDGE_CHANNEL_THREAD_FRAME;
		ast_bridge_channel_unlock(bridge_channel);
		if (!bridge_channel->suspended
			&& bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
			if (chan) {
				bridge_channel_handle_interval(bridge_channel);
				bridge_handle_trip(bridge_channel);
			} else if (-1 < outfd) {
				bridge_channel_handle_write(bridge_channel);
			}
		}
		bridge_channel->activity = AST_BRIDGE_CHANNEL_THREAD_IDLE;
		return;
	}
	ast_bridge_channel_unlock(bridge_channel);
}

/*!
 * \internal
 * \brief Handle bridge channel join event.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is joining.
 *
 * \return Nothing
 */
static void bridge_channel_handle_join(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run any join hooks. */
	iter = ao2_iterator_init(features->join_hooks, AO2_ITERATOR_UNLINK);
	hook = ao2_iterator_next(&iter);
	if (hook) {
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		do {
			hook->callback(bridge_channel->bridge, bridge_channel, hook->hook_pvt);
			ao2_ref(hook, -1);
		} while ((hook = ao2_iterator_next(&iter)));
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
	}
	ao2_iterator_destroy(&iter);
}

/*!
 * \internal
 * \brief Handle bridge channel leave event.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel is leaving.
 *
 * \return Nothing
 */
static void bridge_channel_handle_leave(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = bridge_channel->features;
	struct ast_bridge_hook *hook;
	struct ao2_iterator iter;

	/* Run any leave hooks. */
	iter = ao2_iterator_init(features->leave_hooks, AO2_ITERATOR_UNLINK);
	hook = ao2_iterator_next(&iter);
	if (hook) {
		bridge_channel_suspend(bridge_channel);
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		do {
			hook->callback(bridge_channel->bridge, bridge_channel, hook->hook_pvt);
			ao2_ref(hook, -1);
		} while ((hook = ao2_iterator_next(&iter)));
		ast_indicate(bridge_channel->chan, AST_CONTROL_SRCUPDATE);
		bridge_channel_unsuspend(bridge_channel);
	}
	ao2_iterator_destroy(&iter);
}

/*! \brief Join a channel to a bridge and handle anything the bridge may want us to do */
static void bridge_channel_join(struct ast_bridge_channel *bridge_channel)
{
	ast_format_copy(&bridge_channel->read_format, ast_channel_readformat(bridge_channel->chan));
	ast_format_copy(&bridge_channel->write_format, ast_channel_writeformat(bridge_channel->chan));

	ast_debug(1, "Bridge %s: %p(%s) is joining\n",
		bridge_channel->bridge->uniqueid,
		bridge_channel, ast_channel_name(bridge_channel->chan));

	/*
	 * Get "in the bridge" before pushing the channel for any
	 * masquerades on the channel to happen before bridging.
	 */
	ast_channel_lock(bridge_channel->chan);
	ast_channel_internal_bridge_set(bridge_channel->chan, bridge_channel->bridge);
	ast_channel_unlock(bridge_channel->chan);

	/* Add the jitterbuffer if the channel requires it */
	ast_jb_enable_for_channel(bridge_channel->chan);

	/*
	 * Directly locking the bridge is safe here because nobody else
	 * knows about this bridge_channel yet.
	 */
	ast_bridge_lock(bridge_channel->bridge);

	if (!bridge_channel->bridge->callid) {
		bridge_channel->bridge->callid = ast_read_threadstorage_callid();
	}

	if (bridge_channel_push(bridge_channel)) {
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
	}
	bridge_reconfigured(bridge_channel->bridge);

	if (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		/*
		 * Indicate a source change since this channel is entering the
		 * bridge system only if the bridge technology is not MULTIMIX
		 * capable.  The MULTIMIX technology has already done it.
		 */
		if (!(bridge_channel->bridge->technology->capabilities
			& AST_BRIDGE_CAPABILITY_MULTIMIX)) {
			ast_indicate(bridge_channel->chan, AST_CONTROL_SRCCHANGE);
		}

		ast_bridge_unlock(bridge_channel->bridge);
		bridge_channel_handle_join(bridge_channel);
		while (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
			/* Wait for something to do. */
			bridge_channel_wait(bridge_channel);
		}
		bridge_channel_handle_leave(bridge_channel);
		ast_bridge_channel_lock_bridge(bridge_channel);
	}

	bridge_channel_pull(bridge_channel);
	bridge_reconfigured(bridge_channel->bridge);

	ast_bridge_unlock(bridge_channel->bridge);

	/* Indicate a source change since this channel is leaving the bridge system. */
	ast_indicate(bridge_channel->chan, AST_CONTROL_SRCCHANGE);

/* BUGBUG Revisit in regards to moving channels between bridges and local channel optimization. */
/* BUGBUG This is where outgoing HOLD/UNHOLD memory should write UNHOLD to channel. */
	/* Complete any partial DTMF digit before exiting the bridge. */
	if (ast_channel_sending_dtmf_digit(bridge_channel->chan)) {
		ast_bridge_end_dtmf(bridge_channel->chan,
			ast_channel_sending_dtmf_digit(bridge_channel->chan),
			ast_channel_sending_dtmf_tv(bridge_channel->chan), "bridge end");
	}

	/*
	 * Wait for any dual redirect to complete.
	 *
	 * Must be done while "still in the bridge" for ast_async_goto()
	 * to work right.
	 */
	while (ast_test_flag(ast_channel_flags(bridge_channel->chan), AST_FLAG_BRIDGE_DUAL_REDIRECT_WAIT)) {
		sched_yield();
	}
	ast_channel_lock(bridge_channel->chan);
	ast_channel_internal_bridge_set(bridge_channel->chan, NULL);
	ast_channel_unlock(bridge_channel->chan);

	ast_bridge_channel_restore_formats(bridge_channel);
}

/*!
 * \internal
 * \brief Close a pipe.
 * \since 12.0.0
 *
 * \param my_pipe What to close.
 *
 * \return Nothing
 */
static void pipe_close(int *my_pipe)
{
	if (my_pipe[0] > -1) {
		close(my_pipe[0]);
		my_pipe[0] = -1;
	}
	if (my_pipe[1] > -1) {
		close(my_pipe[1]);
		my_pipe[1] = -1;
	}
}

/*!
 * \internal
 * \brief Initialize a pipe as non-blocking.
 * \since 12.0.0
 *
 * \param my_pipe What to initialize.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int pipe_init_nonblock(int *my_pipe)
{
	int flags;

	my_pipe[0] = -1;
	my_pipe[1] = -1;
	if (pipe(my_pipe)) {
		ast_log(LOG_WARNING, "Can't create pipe! Try increasing max file descriptors with ulimit -n\n");
		return -1;
	}
	flags = fcntl(my_pipe[0], F_GETFL);
	if (fcntl(my_pipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
		ast_log(LOG_WARNING, "Unable to set read pipe nonblocking! (%d: %s)\n",
			errno, strerror(errno));
		return -1;
	}
	flags = fcntl(my_pipe[1], F_GETFL);
	if (fcntl(my_pipe[1], F_SETFL, flags | O_NONBLOCK) < 0) {
		ast_log(LOG_WARNING, "Unable to set write pipe nonblocking! (%d: %s)\n",
			errno, strerror(errno));
		return -1;
	}
	return 0;
}

/* Destroy elements of the bridge channel structure and the bridge channel structure itself */
static void bridge_channel_destroy(void *obj)
{
	struct ast_bridge_channel *bridge_channel = obj;
	struct ast_frame *fr;

	if (bridge_channel->callid) {
		bridge_channel->callid = ast_callid_unref(bridge_channel->callid);
	}

	if (bridge_channel->bridge) {
		ao2_ref(bridge_channel->bridge, -1);
		bridge_channel->bridge = NULL;
	}

	/* Flush any unhandled wr_queue frames. */
	while ((fr = AST_LIST_REMOVE_HEAD(&bridge_channel->wr_queue, frame_list))) {
		ast_frfree(fr);
	}
	pipe_close(bridge_channel->alert_pipe);

	ast_cond_destroy(&bridge_channel->cond);
}

static struct ast_bridge_channel *bridge_channel_alloc(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel;

	bridge_channel = ao2_alloc(sizeof(struct ast_bridge_channel), bridge_channel_destroy);
	if (!bridge_channel) {
		return NULL;
	}
	ast_cond_init(&bridge_channel->cond, NULL);
	if (pipe_init_nonblock(bridge_channel->alert_pipe)) {
		ao2_ref(bridge_channel, -1);
		return NULL;
	}
	if (bridge) {
		bridge_channel->bridge = bridge;
		ao2_ref(bridge_channel->bridge, +1);
	}

	return bridge_channel;
}

struct after_bridge_cb_ds {
	/*! Desired callback function. */
	ast_after_bridge_cb callback;
	/*! After bridge callback will not be called and destroy any resources data may contain. */
	ast_after_bridge_cb_failed failed;
	/*! Extra data to pass to the callback. */
	void *data;
};

/*!
 * \internal
 * \brief Destroy the after bridge callback datastore.
 * \since 12.0.0
 *
 * \param data After bridge callback data to destroy.
 *
 * \return Nothing
 */
static void after_bridge_cb_destroy(void *data)
{
	struct after_bridge_cb_ds *after_bridge = data;

	if (after_bridge->failed) {
		after_bridge->failed(AST_AFTER_BRIDGE_CB_REASON_DESTROY, after_bridge->data);
		after_bridge->failed = NULL;
	}
}

/*!
 * \internal
 * \brief Fixup the after bridge callback datastore.
 * \since 12.0.0
 *
 * \param data After bridge callback data to fixup.
 * \param old_chan The datastore is moving from this channel.
 * \param new_chan The datastore is moving to this channel.
 *
 * \return Nothing
 */
static void after_bridge_cb_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	/* There can be only one.  Discard any already on the new channel. */
	ast_after_bridge_callback_discard(new_chan, AST_AFTER_BRIDGE_CB_REASON_MASQUERADE);
}

static const struct ast_datastore_info after_bridge_cb_info = {
	.type = "after-bridge-cb",
	.destroy = after_bridge_cb_destroy,
	.chan_fixup = after_bridge_cb_fixup,
};

/*!
 * \internal
 * \brief Remove channel after the bridge callback and return it.
 * \since 12.0.0
 *
 * \param chan Channel to remove after bridge callback.
 *
 * \retval datastore on success.
 * \retval NULL on error or not found.
 */
static struct ast_datastore *after_bridge_cb_remove(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &after_bridge_cb_info, NULL);
	if (datastore && ast_channel_datastore_remove(chan, datastore)) {
		datastore = NULL;
	}
	ast_channel_unlock(chan);

	return datastore;
}

void ast_after_bridge_callback_discard(struct ast_channel *chan, enum ast_after_bridge_cb_reason reason)
{
	struct ast_datastore *datastore;

	datastore = after_bridge_cb_remove(chan);
	if (datastore) {
		struct after_bridge_cb_ds *after_bridge = datastore->data;

		if (after_bridge && after_bridge->failed) {
			after_bridge->failed(reason, after_bridge->data);
			after_bridge->failed = NULL;
		}
		ast_datastore_free(datastore);
	}
}

/*!
 * \internal
 * \brief Run any after bridge callback if possible.
 * \since 12.0.0
 *
 * \param chan Channel to run after bridge callback.
 *
 * \return Nothing
 */
static void after_bridge_callback_run(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct after_bridge_cb_ds *after_bridge;

	if (ast_check_hangup(chan)) {
		return;
	}

	/* Get after bridge goto datastore. */
	datastore = after_bridge_cb_remove(chan);
	if (!datastore) {
		return;
	}

	after_bridge = datastore->data;
	if (after_bridge) {
		after_bridge->failed = NULL;
		after_bridge->callback(chan, after_bridge->data);
	}

	/* Discard after bridge callback datastore. */
	ast_datastore_free(datastore);
}

int ast_after_bridge_callback_set(struct ast_channel *chan, ast_after_bridge_cb callback, ast_after_bridge_cb_failed failed, void *data)
{
	struct ast_datastore *datastore;
	struct after_bridge_cb_ds *after_bridge;

	/* Sanity checks. */
	ast_assert(chan != NULL);
	if (!chan || !callback) {
		return -1;
	}

	/* Create a new datastore. */
	datastore = ast_datastore_alloc(&after_bridge_cb_info, NULL);
	if (!datastore) {
		return -1;
	}
	after_bridge = ast_calloc(1, sizeof(*after_bridge));
	if (!after_bridge) {
		ast_datastore_free(datastore);
		return -1;
	}

	/* Initialize it. */
	after_bridge->callback = callback;
	after_bridge->failed = failed;
	after_bridge->data = data;
	datastore->data = after_bridge;

	/* Put it on the channel replacing any existing one. */
	ast_channel_lock(chan);
	ast_after_bridge_callback_discard(chan, AST_AFTER_BRIDGE_CB_REASON_REPLACED);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	return 0;
}

const char *reason_strings[] = {
	[AST_AFTER_BRIDGE_CB_REASON_DESTROY] = "Bridge Destroyed",
	[AST_AFTER_BRIDGE_CB_REASON_REPLACED] = "Channel replaced",
	[AST_AFTER_BRIDGE_CB_REASON_MASQUERADE] = "Channel masqueraded",
	[AST_AFTER_BRIDGE_CB_REASON_DEPART] = "Channel departed",
	[AST_AFTER_BRIDGE_CB_REASON_REMOVED] = "Channel removed",
};

const char *ast_after_bridge_cb_reason_string(enum ast_after_bridge_cb_reason reason)
{
	if (reason < AST_AFTER_BRIDGE_CB_REASON_DESTROY || reason > AST_AFTER_BRIDGE_CB_REASON_REMOVED) {
		return "Unknown";
	}

	return reason_strings[reason];
}

struct after_bridge_goto_ds {
	/*! Goto string that can be parsed by ast_parseable_goto(). */
	const char *parseable_goto;
	/*! Specific goto context or default context for parseable_goto. */
	const char *context;
	/*! Specific goto exten or default exten for parseable_goto. */
	const char *exten;
	/*! Specific goto priority or default priority for parseable_goto. */
	int priority;
	/*! TRUE if the peer should run the h exten. */
	unsigned int run_h_exten:1;
	/*! Specific goto location */
	unsigned int specific:1;
};

/*!
 * \internal
 * \brief Destroy the after bridge goto datastore.
 * \since 12.0.0
 *
 * \param data After bridge goto data to destroy.
 *
 * \return Nothing
 */
static void after_bridge_goto_destroy(void *data)
{
	struct after_bridge_goto_ds *after_bridge = data;

	ast_free((char *) after_bridge->parseable_goto);
	ast_free((char *) after_bridge->context);
	ast_free((char *) after_bridge->exten);
}

/*!
 * \internal
 * \brief Fixup the after bridge goto datastore.
 * \since 12.0.0
 *
 * \param data After bridge goto data to fixup.
 * \param old_chan The datastore is moving from this channel.
 * \param new_chan The datastore is moving to this channel.
 *
 * \return Nothing
 */
static void after_bridge_goto_fixup(void *data, struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	/* There can be only one.  Discard any already on the new channel. */
	ast_after_bridge_goto_discard(new_chan);
}

static const struct ast_datastore_info after_bridge_goto_info = {
	.type = "after-bridge-goto",
	.destroy = after_bridge_goto_destroy,
	.chan_fixup = after_bridge_goto_fixup,
};

/*!
 * \internal
 * \brief Remove channel goto location after the bridge and return it.
 * \since 12.0.0
 *
 * \param chan Channel to remove after bridge goto location.
 *
 * \retval datastore on success.
 * \retval NULL on error or not found.
 */
static struct ast_datastore *after_bridge_goto_remove(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &after_bridge_goto_info, NULL);
	if (datastore && ast_channel_datastore_remove(chan, datastore)) {
		datastore = NULL;
	}
	ast_channel_unlock(chan);

	return datastore;
}

void ast_after_bridge_goto_discard(struct ast_channel *chan)
{
	struct ast_datastore *datastore;

	datastore = after_bridge_goto_remove(chan);
	if (datastore) {
		ast_datastore_free(datastore);
	}
}

int ast_after_bridge_goto_setup(struct ast_channel *chan)
{
	struct ast_datastore *datastore;
	struct after_bridge_goto_ds *after_bridge;
	int goto_failed = -1;

	/* Determine if we are going to setup a dialplan location and where. */
	if (ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO) {
		/* An async goto has already setup a location. */
		ast_channel_clear_softhangup(chan, AST_SOFTHANGUP_ASYNCGOTO);
		if (!ast_check_hangup(chan)) {
			goto_failed = 0;
		}
		return goto_failed;
	}

	/* Get after bridge goto datastore. */
	datastore = after_bridge_goto_remove(chan);
	if (!datastore) {
		return goto_failed;
	}

	after_bridge = datastore->data;
	if (after_bridge->run_h_exten) {
		if (ast_exists_extension(chan, after_bridge->context, "h", 1,
			S_COR(ast_channel_caller(chan)->id.number.valid,
				ast_channel_caller(chan)->id.number.str, NULL))) {
			ast_debug(1, "Running after bridge goto h exten %s,h,1\n",
				ast_channel_context(chan));
			ast_pbx_h_exten_run(chan, after_bridge->context);
		}
	} else if (!ast_check_hangup(chan)) {
		if (after_bridge->specific) {
			goto_failed = ast_explicit_goto(chan, after_bridge->context,
				after_bridge->exten, after_bridge->priority);
		} else if (!ast_strlen_zero(after_bridge->parseable_goto)) {
			char *context;
			char *exten;
			int priority;

			/* Option F(x) for Bridge(), Dial(), and Queue() */

			/* Save current dialplan location in case of failure. */
			context = ast_strdupa(ast_channel_context(chan));
			exten = ast_strdupa(ast_channel_exten(chan));
			priority = ast_channel_priority(chan);

			/* Set current dialplan position to default dialplan position */
			ast_explicit_goto(chan, after_bridge->context, after_bridge->exten,
				after_bridge->priority);

			/* Then perform the goto */
			goto_failed = ast_parseable_goto(chan, after_bridge->parseable_goto);
			if (goto_failed) {
				/* Restore original dialplan location. */
				ast_channel_context_set(chan, context);
				ast_channel_exten_set(chan, exten);
				ast_channel_priority_set(chan, priority);
			}
		} else {
			/* Option F() for Bridge(), Dial(), and Queue() */
			goto_failed = ast_goto_if_exists(chan, after_bridge->context,
				after_bridge->exten, after_bridge->priority + 1);
		}
		if (!goto_failed) {
			ast_debug(1, "Setup after bridge goto location to %s,%s,%d.\n",
				ast_channel_context(chan),
				ast_channel_exten(chan),
				ast_channel_priority(chan));
		}
	}

	/* Discard after bridge goto datastore. */
	ast_datastore_free(datastore);

	return goto_failed;
}

void ast_after_bridge_goto_run(struct ast_channel *chan)
{
	int goto_failed;

	goto_failed = ast_after_bridge_goto_setup(chan);
	if (goto_failed || ast_pbx_run(chan)) {
		ast_hangup(chan);
	}
}

/*!
 * \internal
 * \brief Set after bridge goto location of channel.
 * \since 12.0.0
 *
 * \param chan Channel to setup after bridge goto location.
 * \param run_h_exten TRUE if the h exten should be run.
 * \param specific TRUE if the context/exten/priority is exactly specified.
 * \param context Context to goto after bridge.
 * \param exten Exten to goto after bridge. (Could be NULL if run_h_exten)
 * \param priority Priority to goto after bridge.
 * \param parseable_goto User specified goto string. (Could be NULL)
 *
 * \details Add a channel datastore to setup the goto location
 * when the channel leaves the bridge and run a PBX from there.
 *
 * If run_h_exten then execute the h exten found in the given context.
 * Else if specific then goto the given context/exten/priority.
 * Else if parseable_goto then use the given context/exten/priority
 *   as the relative position for the parseable_goto.
 * Else goto the given context/exten/priority+1.
 *
 * \return Nothing
 */
static void __after_bridge_set_goto(struct ast_channel *chan, int run_h_exten, int specific, const char *context, const char *exten, int priority, const char *parseable_goto)
{
	struct ast_datastore *datastore;
	struct after_bridge_goto_ds *after_bridge;

	/* Sanity checks. */
	ast_assert(chan != NULL);
	if (!chan) {
		return;
	}
	if (run_h_exten) {
		ast_assert(run_h_exten && context);
		if (!context) {
			return;
		}
	} else {
		ast_assert(context && exten && 0 < priority);
		if (!context || !exten || priority < 1) {
			return;
		}
	}

	/* Create a new datastore. */
	datastore = ast_datastore_alloc(&after_bridge_goto_info, NULL);
	if (!datastore) {
		return;
	}
	after_bridge = ast_calloc(1, sizeof(*after_bridge));
	if (!after_bridge) {
		ast_datastore_free(datastore);
		return;
	}

	/* Initialize it. */
	after_bridge->parseable_goto = ast_strdup(parseable_goto);
	after_bridge->context = ast_strdup(context);
	after_bridge->exten = ast_strdup(exten);
	after_bridge->priority = priority;
	after_bridge->run_h_exten = run_h_exten ? 1 : 0;
	after_bridge->specific = specific ? 1 : 0;
	datastore->data = after_bridge;
	if ((parseable_goto && !after_bridge->parseable_goto)
		|| (context && !after_bridge->context)
		|| (exten && !after_bridge->exten)) {
		ast_datastore_free(datastore);
		return;
	}

	/* Put it on the channel replacing any existing one. */
	ast_channel_lock(chan);
	ast_after_bridge_goto_discard(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);
}

void ast_after_bridge_set_goto(struct ast_channel *chan, const char *context, const char *exten, int priority)
{
	__after_bridge_set_goto(chan, 0, 1, context, exten, priority, NULL);
}

void ast_after_bridge_set_h(struct ast_channel *chan, const char *context)
{
	__after_bridge_set_goto(chan, 1, 0, context, NULL, 1, NULL);
}

void ast_after_bridge_set_go_on(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *parseable_goto)
{
	char *p_goto;

	if (!ast_strlen_zero(parseable_goto)) {
		p_goto = ast_strdupa(parseable_goto);
		ast_replace_subargument_delimiter(p_goto);
	} else {
		p_goto = NULL;
	}
	__after_bridge_set_goto(chan, 0, 0, context, exten, priority, p_goto);
}

void ast_bridge_notify_masquerade(struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
	struct ast_bridge *bridge;

	/* Safely get the bridge_channel pointer for the chan. */
	ast_channel_lock(chan);
	bridge_channel = ast_channel_get_bridge_channel(chan);
	ast_channel_unlock(chan);
	if (!bridge_channel) {
		/* Not in a bridge */
		return;
	}

	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge = bridge_channel->bridge;
	if (bridge_channel == find_bridge_channel(bridge, chan)) {
/* BUGBUG this needs more work.  The channels need to be made compatible again if the formats change. The bridge_channel thread needs to monitor for this case. */
		/* The channel we want to notify is still in a bridge. */
		bridge->v_table->notify_masquerade(bridge, bridge_channel);
		bridge_reconfigured(bridge);
	}
	ast_bridge_unlock(bridge);
	ao2_ref(bridge_channel, -1);
}

/*
 * BUGBUG make ast_bridge_join() require features to be allocated just like ast_bridge_impart() and not expect the struct back.
 *
 * This change is really going to break ConfBridge.  All other
 * users are easily changed.  However, it is needed so the
 * bridging code can manipulate features on all channels
 * consistently no matter how they joined.
 *
 * Need to update the features parameter doxygen when this
 * change is made to be like ast_bridge_impart().
 */
enum ast_bridge_channel_state ast_bridge_join(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_channel *swap,
	struct ast_bridge_features *features,
	struct ast_bridge_tech_optimizations *tech_args,
	int pass_reference)
{
	struct ast_bridge_channel *bridge_channel;
	enum ast_bridge_channel_state state;

	bridge_channel = bridge_channel_alloc(bridge);
	if (pass_reference) {
		ao2_ref(bridge, -1);
	}
	if (!bridge_channel) {
		state = AST_BRIDGE_CHANNEL_STATE_HANGUP;
		goto join_exit;
	}
/* BUGBUG features cannot be NULL when passed in. When it is changed to allocated we can do like ast_bridge_impart() and allocate one. */
	ast_assert(features != NULL);
	if (!features) {
		ao2_ref(bridge_channel, -1);
		state = AST_BRIDGE_CHANNEL_STATE_HANGUP;
		goto join_exit;
	}
	if (tech_args) {
		bridge_channel->tech_args = *tech_args;
	}

	/* Initialize various other elements of the bridge channel structure that we can't do above */
	ast_channel_lock(chan);
	ast_channel_internal_bridge_channel_set(chan, bridge_channel);
	ast_channel_unlock(chan);
	bridge_channel->thread = pthread_self();
	bridge_channel->chan = chan;
	bridge_channel->swap = swap;
	bridge_channel->features = features;

	bridge_channel_join(bridge_channel);
	state = bridge_channel->state;

	/* Cleanup all the data in the bridge channel after it leaves the bridge. */
	ast_channel_lock(chan);
	ast_channel_internal_bridge_channel_set(chan, NULL);
	ast_channel_unlock(chan);
	bridge_channel->chan = NULL;
	bridge_channel->swap = NULL;
	bridge_channel->features = NULL;

	ao2_ref(bridge_channel, -1);

join_exit:;
/* BUGBUG this is going to cause problems for DTMF atxfer attended bridge between B & C.  Maybe an ast_bridge_join_internal() that does not do the after bridge goto for this case. */
	after_bridge_callback_run(chan);
	if (!(ast_channel_softhangup_internal_flag(chan) & AST_SOFTHANGUP_ASYNCGOTO)
		&& !ast_after_bridge_goto_setup(chan)) {
		/* Claim the after bridge goto is an async goto destination. */
		ast_channel_lock(chan);
		ast_softhangup_nolock(chan, AST_SOFTHANGUP_ASYNCGOTO);
		ast_channel_unlock(chan);
	}
	return state;
}

/*! \brief Thread responsible for imparted bridged channels to be departed */
static void *bridge_channel_depart_thread(void *data)
{
	struct ast_bridge_channel *bridge_channel = data;

	if (bridge_channel->callid) {
		ast_callid_threadassoc_add(bridge_channel->callid);
	}

	bridge_channel_join(bridge_channel);

	/* cleanup */
	bridge_channel->swap = NULL;
	ast_bridge_features_destroy(bridge_channel->features);
	bridge_channel->features = NULL;

	ast_after_bridge_callback_discard(bridge_channel->chan, AST_AFTER_BRIDGE_CB_REASON_DEPART);
	ast_after_bridge_goto_discard(bridge_channel->chan);

	return NULL;
}

/*! \brief Thread responsible for independent imparted bridged channels */
static void *bridge_channel_ind_thread(void *data)
{
	struct ast_bridge_channel *bridge_channel = data;
	struct ast_channel *chan;

	if (bridge_channel->callid) {
		ast_callid_threadassoc_add(bridge_channel->callid);
	}

	bridge_channel_join(bridge_channel);
	chan = bridge_channel->chan;

	/* cleanup */
	ast_channel_lock(chan);
	ast_channel_internal_bridge_channel_set(chan, NULL);
	ast_channel_unlock(chan);
	bridge_channel->chan = NULL;
	bridge_channel->swap = NULL;
	ast_bridge_features_destroy(bridge_channel->features);
	bridge_channel->features = NULL;

	ao2_ref(bridge_channel, -1);

	after_bridge_callback_run(chan);
	ast_after_bridge_goto_run(chan);
	return NULL;
}

int ast_bridge_impart(struct ast_bridge *bridge, struct ast_channel *chan, struct ast_channel *swap, struct ast_bridge_features *features, int independent)
{
	int res;
	struct ast_bridge_channel *bridge_channel;

	/* Supply an empty features structure if the caller did not. */
	if (!features) {
		features = ast_bridge_features_new();
		if (!features) {
			return -1;
		}
	}

	/* Try to allocate a structure for the bridge channel */
	bridge_channel = bridge_channel_alloc(bridge);
	if (!bridge_channel) {
		ast_bridge_features_destroy(features);
		return -1;
	}

	/* Setup various parameters */
	ast_channel_lock(chan);
	ast_channel_internal_bridge_channel_set(chan, bridge_channel);
	ast_channel_unlock(chan);
	bridge_channel->chan = chan;
	bridge_channel->swap = swap;
	bridge_channel->features = features;
	bridge_channel->depart_wait = independent ? 0 : 1;
	bridge_channel->callid = ast_read_threadstorage_callid();

	/* Actually create the thread that will handle the channel */
	if (independent) {
		/* Independently imparted channels cannot have a PBX. */
		ast_assert(!ast_channel_pbx(chan));

		res = ast_pthread_create_detached(&bridge_channel->thread, NULL,
			bridge_channel_ind_thread, bridge_channel);
	} else {
		/* Imparted channels to be departed should not have a PBX either. */
		ast_assert(!ast_channel_pbx(chan));

		res = ast_pthread_create(&bridge_channel->thread, NULL,
			bridge_channel_depart_thread, bridge_channel);
	}

	if (res) {
		/* cleanup */
		ast_channel_lock(chan);
		ast_channel_internal_bridge_channel_set(chan, NULL);
		ast_channel_unlock(chan);
		bridge_channel->chan = NULL;
		bridge_channel->swap = NULL;
		ast_bridge_features_destroy(bridge_channel->features);
		bridge_channel->features = NULL;

		ao2_ref(bridge_channel, -1);
		return -1;
	}

	return 0;
}

int ast_bridge_depart(struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
	int departable;

	ast_channel_lock(chan);
	bridge_channel = ast_channel_internal_bridge_channel(chan);
	departable = bridge_channel && bridge_channel->depart_wait;
	ast_channel_unlock(chan);
	if (!departable) {
		ast_log(LOG_ERROR, "Channel %s cannot be departed.\n",
			ast_channel_name(chan));
		/*
		 * Should never happen.  It likely means that
		 * ast_bridge_depart() is called by two threads for the same
		 * channel, the channel was never imparted to be departed, or it
		 * has already been departed.
		 */
		ast_assert(0);
		return -1;
	}

	/*
	 * We are claiming the reference held by the depart bridge
	 * channel thread.
	 */

	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);

	/* Wait for the depart thread to die */
	ast_debug(1, "Waiting for %p(%s) bridge thread to die.\n",
		bridge_channel, ast_channel_name(bridge_channel->chan));
	pthread_join(bridge_channel->thread, NULL);

	ast_channel_lock(chan);
	ast_channel_internal_bridge_channel_set(chan, NULL);
	ast_channel_unlock(chan);

	/* We can get rid of the bridge_channel after the depart thread has died. */
	ao2_ref(bridge_channel, -1);
	return 0;
}

int ast_bridge_remove(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;

	ast_bridge_lock(bridge);

	/* Try to find the channel that we want to remove */
	if (!(bridge_channel = find_bridge_channel(bridge, chan))) {
		ast_bridge_unlock(bridge);
		return -1;
	}

	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);

	ast_bridge_unlock(bridge);

	return 0;
}

/*!
 * \internal
 * \brief Point the bridge_channel to a new bridge.
 * \since 12.0.0
 *
 * \param bridge_channel What is to point to a new bridge.
 * \param new_bridge Where the bridge channel should point.
 *
 * \return Nothing
 */
static void bridge_channel_change_bridge(struct ast_bridge_channel *bridge_channel, struct ast_bridge *new_bridge)
{
	struct ast_bridge *old_bridge;

	ao2_ref(new_bridge, +1);
	ast_bridge_channel_lock(bridge_channel);
	ast_channel_lock(bridge_channel->chan);
	old_bridge = bridge_channel->bridge;
	bridge_channel->bridge = new_bridge;
	ast_channel_internal_bridge_set(bridge_channel->chan, new_bridge);
	ast_channel_unlock(bridge_channel->chan);
	ast_bridge_channel_unlock(bridge_channel);
	ao2_ref(old_bridge, -1);
}

/*!
 * \internal
 * \brief Do the merge of two bridges.
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of merge.
 * \param src_bridge Source bridge of merge.
 * \param kick_me Array of channels to kick from the bridges.
 * \param num_kick Number of channels in the kick_me array.
 *
 * \return Nothing
 *
 * \note The two bridges are assumed already locked.
 *
 * This moves the channels in src_bridge into the bridge pointed
 * to by dst_bridge.
 */
static void bridge_merge_do(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, struct ast_bridge_channel **kick_me, unsigned int num_kick)
{
	struct ast_bridge_channel *bridge_channel;
	unsigned int idx;

	ast_debug(1, "Merging bridge %s into bridge %s\n",
		src_bridge->uniqueid, dst_bridge->uniqueid);

	ast_bridge_publish_merge(dst_bridge, src_bridge);

	/*
	 * Move channels from src_bridge over to dst_bridge.
	 *
	 * We must use AST_LIST_TRAVERSE_SAFE_BEGIN() because
	 * bridge_channel_pull() alters the list we are traversing.
	 */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&src_bridge->channels, bridge_channel, entry) {
		if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
			/*
			 * The channel is already leaving let it leave normally because
			 * pulling it may delete hooks that should run for this channel.
			 */
			continue;
		}
		if (ast_test_flag(&bridge_channel->features->feature_flags,
			AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE)) {
			continue;
		}

		if (kick_me) {
			for (idx = 0; idx < num_kick; ++idx) {
				if (bridge_channel == kick_me[idx]) {
					ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
					break;
				}
			}
		}
		bridge_channel_pull(bridge_channel);
		if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
			/*
			 * The channel died as a result of being pulled or it was
			 * kicked.  Leave it pointing to the original bridge.
			 */
			continue;
		}

		/* Point to new bridge.*/
		bridge_channel_change_bridge(bridge_channel, dst_bridge);

		if (bridge_channel_push(bridge_channel)) {
			ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;

	if (kick_me) {
		/*
		 * Now we can kick any channels in the dst_bridge without
		 * potentially dissolving the bridge.
		 */
		for (idx = 0; idx < num_kick; ++idx) {
			bridge_channel = kick_me[idx];
			ast_bridge_channel_lock(bridge_channel);
			if (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
				ast_bridge_change_state_nolock(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
				bridge_channel_pull(bridge_channel);
			}
			ast_bridge_channel_unlock(bridge_channel);
		}
	}

	bridge_reconfigured(dst_bridge);
	bridge_reconfigured(src_bridge);

	ast_debug(1, "Merged bridge %s into bridge %s\n",
		src_bridge->uniqueid, dst_bridge->uniqueid);
}

struct merge_direction {
	/*! Destination merge bridge. */
	struct ast_bridge *dest;
	/*! Source merge bridge. */
	struct ast_bridge *src;
};

/*!
 * \internal
 * \brief Determine which bridge should merge into the other.
 * \since 12.0.0
 *
 * \param bridge1 A bridge for merging
 * \param bridge2 A bridge for merging
 *
 * \note The two bridges are assumed already locked.
 *
 * \return Which bridge merges into which or NULL bridges if cannot merge.
 */
static struct merge_direction bridge_merge_determine_direction(struct ast_bridge *bridge1, struct ast_bridge *bridge2)
{
	struct merge_direction merge = { NULL, NULL };
	int bridge1_priority;
	int bridge2_priority;

	if (!ast_test_flag(&bridge1->feature_flags,
			AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)
		&& !ast_test_flag(&bridge2->feature_flags,
			AST_BRIDGE_FLAG_MERGE_INHIBIT_TO | AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)) {
		/*
		 * Can merge either way.  Merge to the higher priority merge
		 * bridge.  Otherwise merge to the larger bridge.
		 */
		bridge1_priority = bridge1->v_table->get_merge_priority(bridge1);
		bridge2_priority = bridge2->v_table->get_merge_priority(bridge2);
		if (bridge2_priority < bridge1_priority) {
			merge.dest = bridge1;
			merge.src = bridge2;
		} else if (bridge1_priority < bridge2_priority) {
			merge.dest = bridge2;
			merge.src = bridge1;
		} else {
			/* Merge to the larger bridge. */
			if (bridge2->num_channels <= bridge1->num_channels) {
				merge.dest = bridge1;
				merge.src = bridge2;
			} else {
				merge.dest = bridge2;
				merge.src = bridge1;
			}
		}
	} else if (!ast_test_flag(&bridge1->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_TO)
		&& !ast_test_flag(&bridge2->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)) {
		/* Can merge only one way. */
		merge.dest = bridge1;
		merge.src = bridge2;
	} else if (!ast_test_flag(&bridge2->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_TO)
		&& !ast_test_flag(&bridge1->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)) {
		/* Can merge only one way. */
		merge.dest = bridge2;
		merge.src = bridge1;
	}

	return merge;
}

/*!
 * \internal
 * \brief Merge two bridges together
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of merge.
 * \param src_bridge Source bridge of merge.
 * \param merge_best_direction TRUE if don't care about which bridge merges into the other.
 * \param kick_me Array of channels to kick from the bridges.
 * \param num_kick Number of channels in the kick_me array.
 *
 * \note The dst_bridge and src_bridge are assumed already locked.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int bridge_merge_locked(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, int merge_best_direction, struct ast_channel **kick_me, unsigned int num_kick)
{
	struct merge_direction merge;
	struct ast_bridge_channel **kick_them = NULL;

	/* Sanity check. */
	ast_assert(dst_bridge && src_bridge && dst_bridge != src_bridge && (!num_kick || kick_me));

	if (dst_bridge->dissolved || src_bridge->dissolved) {
		ast_debug(1, "Can't merge bridges %s and %s, at least one bridge is dissolved.\n",
			src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (ast_test_flag(&dst_bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)
		|| ast_test_flag(&src_bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)) {
		ast_debug(1, "Can't merge bridges %s and %s, masquerade only.\n",
			src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (dst_bridge->inhibit_merge || src_bridge->inhibit_merge) {
		ast_debug(1, "Can't merge bridges %s and %s, merging temporarily inhibited.\n",
			src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}

	if (merge_best_direction) {
		merge = bridge_merge_determine_direction(dst_bridge, src_bridge);
	} else {
		merge.dest = dst_bridge;
		merge.src = src_bridge;
	}

	if (!merge.dest
		|| ast_test_flag(&merge.dest->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_TO)
		|| ast_test_flag(&merge.src->feature_flags, AST_BRIDGE_FLAG_MERGE_INHIBIT_FROM)) {
		ast_debug(1, "Can't merge bridges %s and %s, merging inhibited.\n",
			src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (merge.src->num_channels < 2) {
		/*
		 * For a two party bridge, a channel may be temporarily removed
		 * from the source bridge or the initial bridge members have not
		 * joined yet.
		 */
		ast_debug(1, "Can't merge bridge %s into bridge %s, not enough channels in source bridge.\n",
			merge.src->uniqueid, merge.dest->uniqueid);
		return -1;
	}
	if (2 + num_kick < merge.dest->num_channels + merge.src->num_channels
		&& !(merge.dest->technology->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX)
		&& (!ast_test_flag(&merge.dest->feature_flags, AST_BRIDGE_FLAG_SMART)
			|| !(merge.dest->allowed_capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX))) {
		ast_debug(1, "Can't merge bridge %s into bridge %s, multimix is needed and it cannot be acquired.\n",
			merge.src->uniqueid, merge.dest->uniqueid);
		return -1;
	}

	if (num_kick) {
		unsigned int num_to_kick = 0;
		unsigned int idx;

		kick_them = ast_alloca(num_kick * sizeof(*kick_them));
		for (idx = 0; idx < num_kick; ++idx) {
			kick_them[num_to_kick] = find_bridge_channel(merge.src, kick_me[idx]);
			if (!kick_them[num_to_kick]) {
				kick_them[num_to_kick] = find_bridge_channel(merge.dest, kick_me[idx]);
			}
			if (kick_them[num_to_kick]) {
				++num_to_kick;
			}
		}

		if (num_to_kick != num_kick) {
			ast_debug(1, "Can't merge bridge %s into bridge %s, at least one kicked channel is not in either bridge.\n",
				merge.src->uniqueid, merge.dest->uniqueid);
			return -1;
		}
	}

	bridge_merge_do(merge.dest, merge.src, kick_them, num_kick);
	return 0;
}

int ast_bridge_merge(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, int merge_best_direction, struct ast_channel **kick_me, unsigned int num_kick)
{
	int res;

	/* Sanity check. */
	ast_assert(dst_bridge && src_bridge);

	ast_bridge_lock_both(dst_bridge, src_bridge);
	res = bridge_merge_locked(dst_bridge, src_bridge, merge_best_direction, kick_me, num_kick);
	ast_bridge_unlock(src_bridge);
	ast_bridge_unlock(dst_bridge);
	return res;
}

/*!
 * \internal
 * \brief Move a bridge channel from one bridge to another.
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of bridge channel move.
 * \param bridge_channel Channel moving from one bridge to another.
 * \param attempt_recovery TRUE if failure attempts to push channel back into original bridge.
 *
 * \note The dst_bridge and bridge_channel->bridge are assumed already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int bridge_move_do(struct ast_bridge *dst_bridge, struct ast_bridge_channel *bridge_channel, int attempt_recovery)
{
	struct ast_bridge *orig_bridge;
	int was_in_bridge;
	int res = 0;

/* BUGBUG need bridge move stasis event and a success/fail event. */
	if (bridge_channel->swap) {
		ast_debug(1, "Moving %p(%s) into bridge %s swapping with %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan), dst_bridge->uniqueid,
			ast_channel_name(bridge_channel->swap));
	} else {
		ast_debug(1, "Moving %p(%s) into bridge %s\n",
			bridge_channel, ast_channel_name(bridge_channel->chan), dst_bridge->uniqueid);
	}

	orig_bridge = bridge_channel->bridge;
	was_in_bridge = bridge_channel->in_bridge;

	bridge_channel_pull(bridge_channel);
	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
		/*
		 * The channel died as a result of being pulled.  Leave it
		 * pointing to the original bridge.
		 */
		bridge_reconfigured(orig_bridge);
		return -1;
	}

	/* Point to new bridge.*/
	ao2_ref(orig_bridge, +1);/* Keep a ref in case the push fails. */
	bridge_channel_change_bridge(bridge_channel, dst_bridge);

	if (bridge_channel_push(bridge_channel)) {
		/* Try to put the channel back into the original bridge. */
		if (attempt_recovery && was_in_bridge) {
			/* Point back to original bridge. */
			bridge_channel_change_bridge(bridge_channel, orig_bridge);

			if (bridge_channel_push(bridge_channel)) {
				ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
			}
		} else {
			ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
		}
		res = -1;
	}

	bridge_reconfigured(dst_bridge);
	bridge_reconfigured(orig_bridge);
	ao2_ref(orig_bridge, -1);
	return res;
}

/*!
 * \internal
 * \brief Move a channel from one bridge to another.
 * \since 12.0.0
 *
 * \param dst_bridge Destination bridge of bridge channel move.
 * \param src_bridge Source bridge of bridge channel move.
 * \param chan Channel to move.
 * \param swap Channel to replace in dst_bridge.
 * \param attempt_recovery TRUE if failure attempts to push channel back into original bridge.
 *
 * \note The dst_bridge and src_bridge are assumed already locked.
 *
 * \retval 0 on success.
 * \retval -1 on failure.
 */
static int bridge_move_locked(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, struct ast_channel *chan, struct ast_channel *swap, int attempt_recovery)
{
	struct ast_bridge_channel *bridge_channel;

	if (dst_bridge->dissolved || src_bridge->dissolved) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, at least one bridge is dissolved.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (ast_test_flag(&dst_bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)
		|| ast_test_flag(&src_bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, masquerade only.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (dst_bridge->inhibit_merge || src_bridge->inhibit_merge) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, temporarily inhibited.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}

	bridge_channel = find_bridge_channel(src_bridge, chan);
	if (!bridge_channel) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, channel not in bridge.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, channel leaving bridge.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}
	if (ast_test_flag(&bridge_channel->features->feature_flags,
		AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE)) {
		ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, channel immovable.\n",
			ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid);
		return -1;
	}

	if (swap) {
		struct ast_bridge_channel *bridge_channel_swap;

		bridge_channel_swap = find_bridge_channel(dst_bridge, swap);
		if (!bridge_channel_swap) {
			ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, swap channel %s not in bridge.\n",
				ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid,
				ast_channel_name(swap));
			return -1;
		}
		if (bridge_channel_swap->state != AST_BRIDGE_CHANNEL_STATE_WAIT) {
			ast_debug(1, "Can't move channel %s from bridge %s into bridge %s, swap channel %s leaving bridge.\n",
				ast_channel_name(chan), src_bridge->uniqueid, dst_bridge->uniqueid,
				ast_channel_name(swap));
			return -1;
		}
	}

	bridge_channel->swap = swap;
	return bridge_move_do(dst_bridge, bridge_channel, attempt_recovery);
}

int ast_bridge_move(struct ast_bridge *dst_bridge, struct ast_bridge *src_bridge, struct ast_channel *chan, struct ast_channel *swap, int attempt_recovery)
{
	int res;

	ast_bridge_lock_both(dst_bridge, src_bridge);
	res = bridge_move_locked(dst_bridge, src_bridge, chan, swap, attempt_recovery);
	ast_bridge_unlock(src_bridge);
	ast_bridge_unlock(dst_bridge);
	return res;
}

struct ast_bridge_channel *ast_bridge_channel_peer(struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge *bridge = bridge_channel->bridge;
	struct ast_bridge_channel *other = NULL;

	if (bridge_channel->in_bridge && bridge->num_channels == 2) {
		AST_LIST_TRAVERSE(&bridge->channels, other, entry) {
			if (other != bridge_channel) {
				break;
			}
		}
	}

	return other;
}

static int bridge_allows_optimization(struct ast_bridge *bridge)
{
	return !(bridge->inhibit_merge
		|| bridge->dissolved
		|| ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY));
}

static int bridge_channel_allows_optimization(struct ast_bridge_channel *bridge_channel)
{
	return bridge_channel->in_bridge
		&& AST_LIST_EMPTY(&bridge_channel->wr_queue);
}

/*!
 * \internal
 * \brief Lock the unreal channel stack for chan and prequalify it.
 * \since 12.0.0
 *
 * \param chan Unreal channel writing a frame into the channel driver.
 *
 * \note It is assumed that chan is already locked.
 *
 * \retval bridge on success with bridge and bridge_channel locked.
 * \retval NULL if cannot do optimization now.
 */
static struct ast_bridge *optimize_lock_chan_stack(struct ast_channel *chan)
{
	struct ast_bridge *bridge;
	struct ast_bridge_channel *bridge_channel;

	if (!AST_LIST_EMPTY(ast_channel_readq(chan))) {
		return NULL;
	}
	bridge_channel = ast_channel_internal_bridge_channel(chan);
	if (!bridge_channel || ast_bridge_channel_trylock(bridge_channel)) {
		return NULL;
	}
	bridge = bridge_channel->bridge;
	if (bridge_channel->activity != AST_BRIDGE_CHANNEL_THREAD_SIMPLE
		|| bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT
		|| ast_bridge_trylock(bridge)) {
		ast_bridge_channel_unlock(bridge_channel);
		return NULL;
	}
	if (!bridge_channel_allows_optimization(bridge_channel) ||
			!bridge_allows_optimization(bridge)) {
		ast_bridge_unlock(bridge);
		ast_bridge_channel_unlock(bridge_channel);
		return NULL;
	}
	return bridge;
}

/*!
 * \internal
 * \brief Lock the unreal channel stack for peer and prequalify it.
 * \since 12.0.0
 *
 * \param peer Other unreal channel in the pair.
 *
 * \retval bridge on success with bridge, bridge_channel, and peer locked.
 * \retval NULL if cannot do optimization now.
 */
static struct ast_bridge *optimize_lock_peer_stack(struct ast_channel *peer)
{
	struct ast_bridge *bridge;
	struct ast_bridge_channel *bridge_channel;

	if (ast_channel_trylock(peer)) {
		return NULL;
	}
	if (!AST_LIST_EMPTY(ast_channel_readq(peer))) {
		ast_channel_unlock(peer);
		return NULL;
	}
	bridge_channel = ast_channel_internal_bridge_channel(peer);
	if (!bridge_channel || ast_bridge_channel_trylock(bridge_channel)) {
		ast_channel_unlock(peer);
		return NULL;
	}
	bridge = bridge_channel->bridge;
	if (bridge_channel->activity != AST_BRIDGE_CHANNEL_THREAD_IDLE
		|| bridge_channel->state != AST_BRIDGE_CHANNEL_STATE_WAIT
		|| ast_bridge_trylock(bridge)) {
		ast_bridge_channel_unlock(bridge_channel);
		ast_channel_unlock(peer);
		return NULL;
	}
	if (!bridge_allows_optimization(bridge) ||
			!bridge_channel_allows_optimization(bridge_channel)) {
		ast_bridge_unlock(bridge);
		ast_bridge_channel_unlock(bridge_channel);
		ast_channel_unlock(peer);
		return NULL;
	}
	return bridge;
}

/*!
 * \internal
 * \brief Indicates allowability of a swap optimization
 */
enum bridge_allow_swap {
	/*! Bridges cannot allow for a swap optimization to occur */
	SWAP_PROHIBITED,
	/*! Bridge swap optimization can occur into the chan_bridge */
	SWAP_TO_CHAN_BRIDGE,
	/*! Bridge swap optimization can occur into the peer_bridge */
	SWAP_TO_PEER_BRIDGE,
};

/*!
 * \internal
 * \brief Determine if two bridges allow for swap optimization to occur
 *
 * \param chan_bridge First bridge being tested
 * \param peer_bridge Second bridge being tested
 * \return Allowability of swap optimization
 */
static enum bridge_allow_swap bridges_allow_swap_optimization(struct ast_bridge *chan_bridge,
		struct ast_bridge *peer_bridge)
{
	int chan_priority;
	int peer_priority;

	if (!ast_test_flag(&chan_bridge->feature_flags,
			AST_BRIDGE_FLAG_SWAP_INHIBIT_TO | AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM |
			AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY)
		&& !ast_test_flag(&peer_bridge->feature_flags,
			AST_BRIDGE_FLAG_SWAP_INHIBIT_TO | AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM |
			AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY)) {
		/*
		 * Can swap either way.  Swap to the higher priority merge
		 * bridge.
		 */
		chan_priority = chan_bridge->v_table->get_merge_priority(chan_bridge);
		peer_priority = peer_bridge->v_table->get_merge_priority(peer_bridge);
		if (chan_bridge->num_channels == 2
			&& chan_priority <= peer_priority) {
			return SWAP_TO_PEER_BRIDGE;
		} else if (peer_bridge->num_channels == 2
			&& peer_priority <= chan_priority) {
			return SWAP_TO_CHAN_BRIDGE;
		}
	} else if (chan_bridge->num_channels == 2
		&& !ast_test_flag(&chan_bridge->feature_flags, AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY)
		&& !ast_test_flag(&peer_bridge->feature_flags, AST_BRIDGE_FLAG_SWAP_INHIBIT_TO)) {
		/* Can swap optimize only one way. */
		return SWAP_TO_PEER_BRIDGE;
	} else if (peer_bridge->num_channels == 2
		&& !ast_test_flag(&peer_bridge->feature_flags, AST_BRIDGE_FLAG_SWAP_INHIBIT_FROM | AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY)
		&& !ast_test_flag(&chan_bridge->feature_flags, AST_BRIDGE_FLAG_SWAP_INHIBIT_TO)) {
		/* Can swap optimize only one way. */
		return SWAP_TO_CHAN_BRIDGE;
	}

	return SWAP_PROHIBITED;
}

/*!
 * \internal
 * \brief Check and attempt to swap optimize out the unreal channels.
 * \since 12.0.0
 *
 * \param chan_bridge
 * \param chan_bridge_channel
 * \param peer_bridge
 * \param peer_bridge_channel
 *
 * \retval 1 if unreal channels failed to optimize out.
 * \retval 0 if unreal channels were not optimized out.
 * \retval -1 if unreal channels were optimized out.
 */
static int check_swap_optimize_out(struct ast_bridge *chan_bridge,
	struct ast_bridge_channel *chan_bridge_channel, struct ast_bridge *peer_bridge,
	struct ast_bridge_channel *peer_bridge_channel)
{
	struct ast_bridge *dst_bridge;
	struct ast_bridge_channel *dst_bridge_channel;
	struct ast_bridge_channel *src_bridge_channel;
	struct ast_bridge_channel *other;
	int res = 1;

	switch (bridges_allow_swap_optimization(chan_bridge, peer_bridge)) {
	case SWAP_TO_CHAN_BRIDGE:
		dst_bridge = chan_bridge;
		dst_bridge_channel = chan_bridge_channel;
		src_bridge_channel = peer_bridge_channel;
		break;
	case SWAP_TO_PEER_BRIDGE:
		dst_bridge = peer_bridge;
		dst_bridge_channel = peer_bridge_channel;
		src_bridge_channel = chan_bridge_channel;
		break;
	case SWAP_PROHIBITED:
	default:
		return 0;
	}

	other = ast_bridge_channel_peer(src_bridge_channel);
	if (other && other->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		ast_debug(1, "Move-swap optimizing %s <-- %s.\n",
			ast_channel_name(dst_bridge_channel->chan),
			ast_channel_name(other->chan));

		other->swap = dst_bridge_channel->chan;
		if (!bridge_move_do(dst_bridge, other, 1)) {
			ast_bridge_change_state(src_bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);
			res = -1;
		}
	}
	return res;
}

/*!
 * \internal
 * \brief Indicates allowability of a merge optimization
 */
enum bridge_allow_merge {
	/*! Bridge properties prohibit merge optimization */
	MERGE_PROHIBITED,
	/*! Merge optimization cannot occur because the source bridge has too few channels */
	MERGE_NOT_ENOUGH_CHANNELS,
	/*! Merge optimization cannot occur because multimix capability could not be requested */
	MERGE_NO_MULTIMIX,
	/*! Merge optimization allowed between bridges */
	MERGE_ALLOWED,
};

/*!
 * \internal
 * \brief Determines allowability of a merge optimization
 *
 * \note The merge output parameter is undefined if MERGE_PROHIBITED is returned. For success
 * and other failure returns, a merge direction was determined, and the parameter is safe to
 * access.
 *
 * \param chan_bridge First bridge being tested
 * \param peer_bridge Second bridge being tested
 * \param num_kick_channels The number of channels to remove from the bridges during merging
 * \param[out] merge Indicates the recommended direction for the bridge merge
 */
static enum bridge_allow_merge bridges_allow_merge_optimization(struct ast_bridge *chan_bridge,
		struct ast_bridge *peer_bridge, int num_kick_channels, struct merge_direction *merge)
{
	*merge = bridge_merge_determine_direction(chan_bridge, peer_bridge);
	if (!merge->dest) {
		return MERGE_PROHIBITED;
	}
	if (merge->src->num_channels < 2) {
		return MERGE_NOT_ENOUGH_CHANNELS;
	} else if ((2 + num_kick_channels) < merge->dest->num_channels + merge->src->num_channels
		&& !(merge->dest->technology->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX)
		&& (!ast_test_flag(&merge->dest->feature_flags, AST_BRIDGE_FLAG_SMART)
			|| !(merge->dest->allowed_capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX))) {
		return MERGE_NO_MULTIMIX;
	}

	return MERGE_ALLOWED;
}

/*!
 * \internal
 * \brief Check and attempt to merge optimize out the unreal channels.
 * \since 12.0.0
 *
 * \param chan_bridge
 * \param chan_bridge_channel
 * \param peer_bridge
 * \param peer_bridge_channel
 *
 * \retval 0 if unreal channels were not optimized out.
 * \retval -1 if unreal channels were optimized out.
 */
static int check_merge_optimize_out(struct ast_bridge *chan_bridge,
	struct ast_bridge_channel *chan_bridge_channel, struct ast_bridge *peer_bridge,
	struct ast_bridge_channel *peer_bridge_channel)
{
	struct merge_direction merge;
	struct ast_bridge_channel *kick_me[] = {
		chan_bridge_channel,
		peer_bridge_channel,
	};

	switch (bridges_allow_merge_optimization(chan_bridge, peer_bridge, ARRAY_LEN(kick_me), &merge)) {
	case MERGE_ALLOWED:
		break;
	case MERGE_PROHIBITED:
		return 0;
	case MERGE_NOT_ENOUGH_CHANNELS:
		ast_debug(4, "Can't optimize %s -- %s out, not enough channels in bridge %s.\n",
			ast_channel_name(chan_bridge_channel->chan),
			ast_channel_name(peer_bridge_channel->chan),
			merge.src->uniqueid);
		return 0;
	case MERGE_NO_MULTIMIX:
		ast_debug(4, "Can't optimize %s -- %s out, multimix is needed and it cannot be acquired.\n",
			ast_channel_name(chan_bridge_channel->chan),
			ast_channel_name(peer_bridge_channel->chan));
		return 0;
	}

	ast_debug(1, "Merge optimizing %s -- %s out.\n",
		ast_channel_name(chan_bridge_channel->chan),
		ast_channel_name(peer_bridge_channel->chan));

	bridge_merge_do(merge.dest, merge.src, kick_me, ARRAY_LEN(kick_me));

	return -1;
}

int ast_bridge_unreal_optimized_out(struct ast_channel *chan, struct ast_channel *peer)
{
	struct ast_bridge *chan_bridge;
	struct ast_bridge *peer_bridge;
	struct ast_bridge_channel *chan_bridge_channel;
	struct ast_bridge_channel *peer_bridge_channel;
	int res = 0;

	chan_bridge = optimize_lock_chan_stack(chan);
	if (!chan_bridge) {
		return res;
	}
	chan_bridge_channel = ast_channel_internal_bridge_channel(chan);

	peer_bridge = optimize_lock_peer_stack(peer);
	if (peer_bridge) {
		peer_bridge_channel = ast_channel_internal_bridge_channel(peer);

		res = check_swap_optimize_out(chan_bridge, chan_bridge_channel,
			peer_bridge, peer_bridge_channel);
		if (!res) {
			res = check_merge_optimize_out(chan_bridge, chan_bridge_channel,
				peer_bridge, peer_bridge_channel);
		} else if (0 < res) {
			res = 0;
		}

		/* Release peer locks. */
		ast_bridge_unlock(peer_bridge);
		ast_bridge_channel_unlock(peer_bridge_channel);
		ast_channel_unlock(peer);
	}

	/* Release chan locks. */
	ast_bridge_unlock(chan_bridge);
	ast_bridge_channel_unlock(chan_bridge_channel);

	return res;
}

enum ast_bridge_optimization ast_bridges_allow_optimization(struct ast_bridge *chan_bridge,
		struct ast_bridge *peer_bridge)
{
	struct merge_direction merge;

	if (!bridge_allows_optimization(chan_bridge) || !bridge_allows_optimization(peer_bridge)) {
		return AST_BRIDGE_OPTIMIZE_PROHIBITED;
	}

	switch (bridges_allow_swap_optimization(chan_bridge, peer_bridge)) {
	case SWAP_TO_CHAN_BRIDGE:
		return AST_BRIDGE_OPTIMIZE_SWAP_TO_CHAN_BRIDGE;
	case SWAP_TO_PEER_BRIDGE:
		return AST_BRIDGE_OPTIMIZE_SWAP_TO_PEER_BRIDGE;
	case SWAP_PROHIBITED:
	default:
		break;
	}

	/* Two channels will be kicked from the bridges, the unreal;1 and unreal;2 channels */
	if (bridges_allow_merge_optimization(chan_bridge, peer_bridge, 2, &merge) != MERGE_ALLOWED) {
		return AST_BRIDGE_OPTIMIZE_PROHIBITED;
	}

	if (merge.dest == chan_bridge) {
		return AST_BRIDGE_OPTIMIZE_MERGE_TO_CHAN_BRIDGE;
	} else {
		return AST_BRIDGE_OPTIMIZE_MERGE_TO_PEER_BRIDGE;
	}
}

/*!
 * \internal
 * \brief Adjust the bridge merge inhibit request count.
 * \since 12.0.0
 *
 * \param bridge What to operate on.
 * \param request Inhibit request increment.
 *     (Positive to add requests.  Negative to remove requests.)
 *
 * \note This function assumes bridge is locked.
 *
 * \return Nothing
 */
static void bridge_merge_inhibit_nolock(struct ast_bridge *bridge, int request)
{
	int new_request;

	new_request = bridge->inhibit_merge + request;
	ast_assert(0 <= new_request);
	bridge->inhibit_merge = new_request;
}

void ast_bridge_merge_inhibit(struct ast_bridge *bridge, int request)
{
	ast_bridge_lock(bridge);
	bridge_merge_inhibit_nolock(bridge, request);
	ast_bridge_unlock(bridge);
}

struct ast_bridge *ast_bridge_channel_merge_inhibit(struct ast_bridge_channel *bridge_channel, int request)
{
	struct ast_bridge *bridge;

	ast_bridge_channel_lock_bridge(bridge_channel);
	bridge = bridge_channel->bridge;
	ao2_ref(bridge, +1);
	bridge_merge_inhibit_nolock(bridge, request);
	ast_bridge_unlock(bridge);
	return bridge;
}

int ast_bridge_suspend(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
/* BUGBUG the case of a disolved bridge while channel is suspended is not handled. */
/* BUGBUG suspend/unsuspend needs to be rethought. The caller must block until it has successfully suspended the channel for temporary control. */
/* BUGBUG external suspend/unsuspend needs to be eliminated. The channel may be playing a file at the time and stealing it then is not good. */

	ast_bridge_lock(bridge);

	if (!(bridge_channel = find_bridge_channel(bridge, chan))) {
		ast_bridge_unlock(bridge);
		return -1;
	}

	bridge_channel_suspend_nolock(bridge_channel);

	ast_bridge_unlock(bridge);

	return 0;
}

int ast_bridge_unsuspend(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;
/* BUGBUG the case of a disolved bridge while channel is suspended is not handled. */

	ast_bridge_lock(bridge);

	if (!(bridge_channel = find_bridge_channel(bridge, chan))) {
		ast_bridge_unlock(bridge);
		return -1;
	}

	bridge_channel_unsuspend_nolock(bridge_channel);

	ast_bridge_unlock(bridge);

	return 0;
}

void ast_bridge_technology_suspend(struct ast_bridge_technology *technology)
{
	technology->suspended = 1;
}

void ast_bridge_technology_unsuspend(struct ast_bridge_technology *technology)
{
/* BUGBUG unsuspending a bridge technology probably needs to prod all existing bridges to see if they should start using it. */
	technology->suspended = 0;
}

int ast_bridge_features_register(enum ast_bridge_builtin_feature feature, ast_bridge_hook_callback callback, const char *dtmf)
{
	if (ARRAY_LEN(builtin_features_handlers) <= feature
		|| builtin_features_handlers[feature]) {
		return -1;
	}

	if (!ast_strlen_zero(dtmf)) {
		ast_copy_string(builtin_features_dtmf[feature], dtmf, sizeof(builtin_features_dtmf[feature]));
	}

	builtin_features_handlers[feature] = callback;

	return 0;
}

int ast_bridge_features_unregister(enum ast_bridge_builtin_feature feature)
{
	if (ARRAY_LEN(builtin_features_handlers) <= feature
		|| !builtin_features_handlers[feature]) {
		return -1;
	}

	builtin_features_handlers[feature] = NULL;

	return 0;
}

int ast_bridge_interval_register(enum ast_bridge_builtin_interval interval, ast_bridge_builtin_set_limits_fn callback)
{
	if (ARRAY_LEN(builtin_interval_handlers) <= interval
		|| builtin_interval_handlers[interval]) {
		return -1;
	}

	builtin_interval_handlers[interval] = callback;

	return 0;
}

int ast_bridge_interval_unregister(enum ast_bridge_builtin_interval interval)
{
	if (ARRAY_LEN(builtin_interval_handlers) <= interval
		|| !builtin_interval_handlers[interval]) {
		return -1;
	}

	builtin_interval_handlers[interval] = NULL;

	return 0;

}

/*!
 * \internal
 * \brief Bridge hook destructor.
 * \since 12.0.0
 *
 * \param vhook Object to destroy.
 *
 * \return Nothing
 */
static void bridge_hook_destroy(void *vhook)
{
	struct ast_bridge_hook *hook = vhook;

	if (hook->destructor) {
		hook->destructor(hook->hook_pvt);
	}
}

/*!
 * \internal
 * \brief Allocate and setup a generic bridge hook.
 * \since 12.0.0
 *
 * \param size How big an object to allocate.
 * \param callback Function to execute upon activation
 * \param hook_pvt Unique data
 * \param destructor Optional destructor callback for hook_pvt data
 * \param remove_on_pull TRUE if remove the hook when the channel is pulled from the bridge.
 *
 * \retval hook on success.
 * \retval NULL on error.
 */
static struct ast_bridge_hook *bridge_hook_generic(size_t size,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull)
{
	struct ast_bridge_hook *hook;

	/* Allocate new hook and setup it's basic variables */
	hook = ao2_alloc_options(size, bridge_hook_destroy, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (hook) {
		hook->callback = callback;
		hook->destructor = destructor;
		hook->hook_pvt = hook_pvt;
		hook->remove_on_pull = remove_on_pull;
	}

	return hook;
}

int ast_bridge_dtmf_hook(struct ast_bridge_features *features,
	const char *dtmf,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull)
{
	struct ast_bridge_hook *hook;
	int res;

	/* Allocate new hook and setup it's various variables */
	hook = bridge_hook_generic(sizeof(*hook), callback, hook_pvt, destructor,
		remove_on_pull);
	if (!hook) {
		return -1;
	}
	ast_copy_string(hook->parms.dtmf.code, dtmf, sizeof(hook->parms.dtmf.code));

	/* Once done we put it in the container. */
	res = ao2_link(features->dtmf_hooks, hook) ? 0 : -1;
	ao2_ref(hook, -1);

	return res;
}

int ast_bridge_hangup_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull)
{
	struct ast_bridge_hook *hook;
	int res;

	/* Allocate new hook and setup it's various variables */
	hook = bridge_hook_generic(sizeof(*hook), callback, hook_pvt, destructor,
		remove_on_pull);
	if (!hook) {
		return -1;
	}

	/* Once done we put it in the container. */
	res = ao2_link(features->hangup_hooks, hook) ? 0 : -1;
	ao2_ref(hook, -1);

	return res;
}

int ast_bridge_join_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull)
{
	struct ast_bridge_hook *hook;
	int res;

	/* Allocate new hook and setup it's various variables */
	hook = bridge_hook_generic(sizeof(*hook), callback, hook_pvt, destructor,
		remove_on_pull);
	if (!hook) {
		return -1;
	}

	/* Once done we put it in the container. */
	res = ao2_link(features->join_hooks, hook) ? 0 : -1;
	ao2_ref(hook, -1);

	return res;
}

int ast_bridge_leave_hook(struct ast_bridge_features *features,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull)
{
	struct ast_bridge_hook *hook;
	int res;

	/* Allocate new hook and setup it's various variables */
	hook = bridge_hook_generic(sizeof(*hook), callback, hook_pvt, destructor,
		remove_on_pull);
	if (!hook) {
		return -1;
	}

	/* Once done we put it in the container. */
	res = ao2_link(features->leave_hooks, hook) ? 0 : -1;
	ao2_ref(hook, -1);

	return res;
}

void ast_bridge_features_set_talk_detector(struct ast_bridge_features *features,
	ast_bridge_talking_indicate_callback talker_cb,
	ast_bridge_talking_indicate_destructor talker_destructor,
	void *pvt_data)
{
	features->talker_cb = talker_cb;
	features->talker_destructor_cb = talker_destructor;
	features->talker_pvt_data = pvt_data;
}

int ast_bridge_interval_hook(struct ast_bridge_features *features,
	unsigned int interval,
	ast_bridge_hook_callback callback,
	void *hook_pvt,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull)
{
	struct ast_bridge_hook *hook;
	int res;

	if (!features ||!interval || !callback) {
		return -1;
	}

	if (!features->interval_timer) {
		if (!(features->interval_timer = ast_timer_open())) {
			ast_log(LOG_ERROR, "Failed to open a timer when adding a timed bridging feature.\n");
			return -1;
		}
		ast_timer_set_rate(features->interval_timer, BRIDGE_FEATURES_INTERVAL_RATE);
	}

	/* Allocate new hook and setup it's various variables */
	hook = bridge_hook_generic(sizeof(*hook), callback, hook_pvt, destructor,
		remove_on_pull);
	if (!hook) {
		return -1;
	}
	hook->parms.timer.interval = interval;
	hook->parms.timer.trip_time = ast_tvadd(ast_tvnow(), ast_samp2tv(hook->parms.timer.interval, 1000));
	hook->parms.timer.seqno = ast_atomic_fetchadd_int((int *) &features->interval_sequence, +1);

	ast_debug(1, "Putting interval hook %p with interval %u in the heap on features %p\n",
		hook, hook->parms.timer.interval, features);
	ast_heap_wrlock(features->interval_hooks);
	res = ast_heap_push(features->interval_hooks, hook);
	if (res) {
		/* Could not push the hook onto the heap. */
		ao2_ref(hook, -1);
	}
	ast_heap_unlock(features->interval_hooks);

	return res ? -1 : 0;
}

int ast_bridge_features_enable(struct ast_bridge_features *features,
	enum ast_bridge_builtin_feature feature,
	const char *dtmf,
	void *config,
	ast_bridge_hook_pvt_destructor destructor,
	int remove_on_pull)
{
	if (ARRAY_LEN(builtin_features_handlers) <= feature
		|| !builtin_features_handlers[feature]) {
		return -1;
	}

	/* If no alternate DTMF stream was provided use the default one */
	if (ast_strlen_zero(dtmf)) {
		dtmf = builtin_features_dtmf[feature];
		/* If no DTMF is still available (ie: it has been disabled) then error out now */
		if (ast_strlen_zero(dtmf)) {
			ast_debug(1, "Failed to enable built in feature %d on %p, no DTMF string is available for it.\n",
				feature, features);
			return -1;
		}
	}

	/*
	 * The rest is basically pretty easy.  We create another hook
	 * using the built in feature's DTMF callback.  Easy as pie.
	 */
	return ast_bridge_dtmf_hook(features, dtmf, builtin_features_handlers[feature],
		config, destructor, remove_on_pull);
}

int ast_bridge_features_limits_construct(struct ast_bridge_features_limits *limits)
{
	memset(limits, 0, sizeof(*limits));

	if (ast_string_field_init(limits, 256)) {
		ast_free(limits);
		return -1;
	}

	return 0;
}

void ast_bridge_features_limits_destroy(struct ast_bridge_features_limits *limits)
{
	ast_string_field_free_memory(limits);
}

int ast_bridge_features_set_limits(struct ast_bridge_features *features, struct ast_bridge_features_limits *limits, int remove_on_pull)
{
	if (builtin_interval_handlers[AST_BRIDGE_BUILTIN_INTERVAL_LIMITS]) {
		ast_bridge_builtin_set_limits_fn bridge_features_set_limits_callback;

		bridge_features_set_limits_callback = builtin_interval_handlers[AST_BRIDGE_BUILTIN_INTERVAL_LIMITS];
		return bridge_features_set_limits_callback(features, limits, remove_on_pull);
	}

	ast_log(LOG_ERROR, "Attempted to set limits without an AST_BRIDGE_BUILTIN_INTERVAL_LIMITS callback registered.\n");
	return -1;
}

void ast_bridge_features_set_flag(struct ast_bridge_features *features, unsigned int flag)
{
	ast_set_flag(&features->feature_flags, flag);
	features->usable = 1;
}

/*!
 * \internal
 * \brief ao2 object match remove_on_pull hooks.
 * \since 12.0.0
 *
 * \param obj Feature hook object.
 * \param arg Not used
 * \param flags Not used
 *
 * \retval CMP_MATCH if hook has remove_on_pull set.
 * \retval 0 if not match.
 */
static int hook_remove_on_pull_match(void *obj, void *arg, int flags)
{
	struct ast_bridge_hook *hook = obj;

	if (hook->remove_on_pull) {
		return CMP_MATCH;
	} else {
		return 0;
	}
}

/*!
 * \internal
 * \brief Remove all remove_on_pull hooks in the container.
 * \since 12.0.0
 *
 * \param hooks Hooks container to work on.
 *
 * \return Nothing
 */
static void hooks_remove_on_pull_container(struct ao2_container *hooks)
{
	ao2_callback(hooks, OBJ_UNLINK | OBJ_NODATA | OBJ_MULTIPLE,
		hook_remove_on_pull_match, NULL);
}

/*!
 * \internal
 * \brief Remove all remove_on_pull hooks in the heap.
 * \since 12.0.0
 *
 * \param hooks Hooks heap to work on.
 *
 * \return Nothing
 */
static void hooks_remove_on_pull_heap(struct ast_heap *hooks)
{
	struct ast_bridge_hook *hook;
	int changed;

	ast_heap_wrlock(hooks);
	do {
		int idx;

		changed = 0;
		for (idx = ast_heap_size(hooks); idx; --idx) {
			hook = ast_heap_peek(hooks, idx);
			if (hook->remove_on_pull) {
				ast_heap_remove(hooks, hook);
				ao2_ref(hook, -1);
				changed = 1;
			}
		}
	} while (changed);
	ast_heap_unlock(hooks);
}

/*!
 * \internal
 * \brief Remove marked bridge channel feature hooks.
 * \since 12.0.0
 *
 * \param features Bridge featues structure
 *
 * \return Nothing
 */
static void bridge_features_remove_on_pull(struct ast_bridge_features *features)
{
	hooks_remove_on_pull_container(features->dtmf_hooks);
	hooks_remove_on_pull_container(features->hangup_hooks);
	hooks_remove_on_pull_container(features->join_hooks);
	hooks_remove_on_pull_container(features->leave_hooks);
	hooks_remove_on_pull_heap(features->interval_hooks);
}

static int interval_hook_time_cmp(void *a, void *b)
{
	struct ast_bridge_hook *hook_a = a;
	struct ast_bridge_hook *hook_b = b;
	int cmp;

	cmp = ast_tvcmp(hook_b->parms.timer.trip_time, hook_a->parms.timer.trip_time);
	if (cmp) {
		return cmp;
	}

	cmp = hook_b->parms.timer.seqno - hook_a->parms.timer.seqno;
	return cmp;
}

/*!
 * \internal
 * \brief DTMF hook container sort comparison function.
 * \since 12.0.0
 *
 * \param obj_left pointer to the (user-defined part) of an object.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_POINTER - if set, 'obj_right', is an object.
 *   OBJ_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 *
 * \retval <0 if obj_left < obj_right
 * \retval =0 if obj_left == obj_right
 * \retval >0 if obj_left > obj_right
 */
static int bridge_dtmf_hook_sort(const void *obj_left, const void *obj_right, int flags)
{
	const struct ast_bridge_hook *hook_left = obj_left;
	const struct ast_bridge_hook *hook_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_key = hook_right->parms.dtmf.code;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcasecmp(hook_left->parms.dtmf.code, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncasecmp(hook_left->parms.dtmf.code, right_key, strlen(right_key));
		break;
	}
	return cmp;
}

/* BUGBUG make ast_bridge_features_init() static when make ast_bridge_join() requires features to be allocated. */
int ast_bridge_features_init(struct ast_bridge_features *features)
{
	/* Zero out the structure */
	memset(features, 0, sizeof(*features));

	/* Initialize the DTMF hooks container */
	features->dtmf_hooks = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, bridge_dtmf_hook_sort, NULL);
	if (!features->dtmf_hooks) {
		return -1;
	}

	/* Initialize the hangup hooks container */
	features->hangup_hooks = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL,
		NULL);
	if (!features->hangup_hooks) {
		return -1;
	}

	/* Initialize the join hooks container */
	features->join_hooks = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL,
		NULL);
	if (!features->join_hooks) {
		return -1;
	}

	/* Initialize the leave hooks container */
	features->leave_hooks = ao2_container_alloc_list(AO2_ALLOC_OPT_LOCK_MUTEX, 0, NULL,
		NULL);
	if (!features->leave_hooks) {
		return -1;
	}

	/* Initialize the interval hooks heap */
	features->interval_hooks = ast_heap_create(8, interval_hook_time_cmp,
		offsetof(struct ast_bridge_hook, parms.timer.heap_index));
	if (!features->interval_hooks) {
		return -1;
	}

	return 0;
}

/* BUGBUG make ast_bridge_features_cleanup() static when make ast_bridge_join() requires features to be allocated. */
void ast_bridge_features_cleanup(struct ast_bridge_features *features)
{
	struct ast_bridge_hook *hook;

	/* Destroy the interval hooks heap. */
	if (features->interval_hooks) {
		while ((hook = ast_heap_pop(features->interval_hooks))) {
			ao2_ref(hook, -1);
		}
		features->interval_hooks = ast_heap_destroy(features->interval_hooks);
	}

	if (features->interval_timer) {
		ast_timer_close(features->interval_timer);
		features->interval_timer = NULL;
	}

	/* If the features contains a limits pvt, destroy that as well. */
	if (features->limits) {
		ast_bridge_features_limits_destroy(features->limits);
		ast_free(features->limits);
		features->limits = NULL;
	}

	if (features->talker_destructor_cb && features->talker_pvt_data) {
		features->talker_destructor_cb(features->talker_pvt_data);
		features->talker_pvt_data = NULL;
	}

	/* Destroy the leave hooks container. */
	ao2_cleanup(features->leave_hooks);
	features->leave_hooks = NULL;

	/* Destroy the join hooks container. */
	ao2_cleanup(features->join_hooks);
	features->join_hooks = NULL;

	/* Destroy the hangup hooks container. */
	ao2_cleanup(features->hangup_hooks);
	features->hangup_hooks = NULL;

	/* Destroy the DTMF hooks container. */
	ao2_cleanup(features->dtmf_hooks);
	features->dtmf_hooks = NULL;
}

void ast_bridge_features_destroy(struct ast_bridge_features *features)
{
	if (!features) {
		return;
	}
	ast_bridge_features_cleanup(features);
	ast_free(features);
}

struct ast_bridge_features *ast_bridge_features_new(void)
{
	struct ast_bridge_features *features;

	features = ast_malloc(sizeof(*features));
	if (features) {
		if (ast_bridge_features_init(features)) {
			ast_bridge_features_destroy(features);
			features = NULL;
		}
	}

	return features;
}

void ast_bridge_set_mixing_interval(struct ast_bridge *bridge, unsigned int mixing_interval)
{
	ast_bridge_lock(bridge);
	bridge->internal_mixing_interval = mixing_interval;
	ast_bridge_unlock(bridge);
}

void ast_bridge_set_internal_sample_rate(struct ast_bridge *bridge, unsigned int sample_rate)
{
	ast_bridge_lock(bridge);
	bridge->internal_sample_rate = sample_rate;
	ast_bridge_unlock(bridge);
}

static void cleanup_video_mode(struct ast_bridge *bridge)
{
	switch (bridge->video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		if (bridge->video_mode.mode_data.single_src_data.chan_vsrc) {
			ast_channel_unref(bridge->video_mode.mode_data.single_src_data.chan_vsrc);
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		if (bridge->video_mode.mode_data.talker_src_data.chan_vsrc) {
			ast_channel_unref(bridge->video_mode.mode_data.talker_src_data.chan_vsrc);
		}
		if (bridge->video_mode.mode_data.talker_src_data.chan_old_vsrc) {
			ast_channel_unref(bridge->video_mode.mode_data.talker_src_data.chan_old_vsrc);
		}
	}
	memset(&bridge->video_mode, 0, sizeof(bridge->video_mode));
}

void ast_bridge_set_single_src_video_mode(struct ast_bridge *bridge, struct ast_channel *video_src_chan)
{
	ast_bridge_lock(bridge);
	cleanup_video_mode(bridge);
	bridge->video_mode.mode = AST_BRIDGE_VIDEO_MODE_SINGLE_SRC;
	bridge->video_mode.mode_data.single_src_data.chan_vsrc = ast_channel_ref(video_src_chan);
	ast_test_suite_event_notify("BRIDGE_VIDEO_MODE", "Message: video mode set to single source\r\nVideo Mode: %d\r\nVideo Channel: %s", bridge->video_mode.mode, ast_channel_name(video_src_chan));
	ast_indicate(video_src_chan, AST_CONTROL_VIDUPDATE);
	ast_bridge_unlock(bridge);
}

void ast_bridge_set_talker_src_video_mode(struct ast_bridge *bridge)
{
	ast_bridge_lock(bridge);
	cleanup_video_mode(bridge);
	bridge->video_mode.mode = AST_BRIDGE_VIDEO_MODE_TALKER_SRC;
	ast_test_suite_event_notify("BRIDGE_VIDEO_MODE", "Message: video mode set to talker source\r\nVideo Mode: %d", bridge->video_mode.mode);
	ast_bridge_unlock(bridge);
}

void ast_bridge_update_talker_src_video_mode(struct ast_bridge *bridge, struct ast_channel *chan, int talker_energy, int is_keyframe)
{
	struct ast_bridge_video_talker_src_data *data;
	/* If the channel doesn't support video, we don't care about it */
	if (!ast_format_cap_has_type(ast_channel_nativeformats(chan), AST_FORMAT_TYPE_VIDEO)) {
		return;
	}

	ast_bridge_lock(bridge);
	data = &bridge->video_mode.mode_data.talker_src_data;

	if (data->chan_vsrc == chan) {
		data->average_talking_energy = talker_energy;
	} else if ((data->average_talking_energy < talker_energy) && is_keyframe) {
		if (data->chan_old_vsrc) {
			ast_channel_unref(data->chan_old_vsrc);
		}
		if (data->chan_vsrc) {
			data->chan_old_vsrc = data->chan_vsrc;
			ast_indicate(data->chan_old_vsrc, AST_CONTROL_VIDUPDATE);
		}
		data->chan_vsrc = ast_channel_ref(chan);
		data->average_talking_energy = talker_energy;
		ast_test_suite_event_notify("BRIDGE_VIDEO_SRC", "Message: video source updated\r\nVideo Channel: %s", ast_channel_name(data->chan_vsrc));
		ast_indicate(data->chan_vsrc, AST_CONTROL_VIDUPDATE);
	} else if ((data->average_talking_energy < talker_energy) && !is_keyframe) {
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	} else if (!data->chan_vsrc && is_keyframe) {
		data->chan_vsrc = ast_channel_ref(chan);
		data->average_talking_energy = talker_energy;
		ast_test_suite_event_notify("BRIDGE_VIDEO_SRC", "Message: video source updated\r\nVideo Channel: %s", ast_channel_name(data->chan_vsrc));
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	} else if (!data->chan_old_vsrc && is_keyframe) {
		data->chan_old_vsrc = ast_channel_ref(chan);
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	}
	ast_bridge_unlock(bridge);
}

int ast_bridge_number_video_src(struct ast_bridge *bridge)
{
	int res = 0;

	ast_bridge_lock(bridge);
	switch (bridge->video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		if (bridge->video_mode.mode_data.single_src_data.chan_vsrc) {
			res = 1;
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		if (bridge->video_mode.mode_data.talker_src_data.chan_vsrc) {
			res++;
		}
		if (bridge->video_mode.mode_data.talker_src_data.chan_old_vsrc) {
			res++;
		}
	}
	ast_bridge_unlock(bridge);
	return res;
}

int ast_bridge_is_video_src(struct ast_bridge *bridge, struct ast_channel *chan)
{
	int res = 0;

	ast_bridge_lock(bridge);
	switch (bridge->video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		if (bridge->video_mode.mode_data.single_src_data.chan_vsrc == chan) {
			res = 1;
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		if (bridge->video_mode.mode_data.talker_src_data.chan_vsrc == chan) {
			res = 1;
		} else if (bridge->video_mode.mode_data.talker_src_data.chan_old_vsrc == chan) {
			res = 2;
		}

	}
	ast_bridge_unlock(bridge);
	return res;
}

void ast_bridge_remove_video_src(struct ast_bridge *bridge, struct ast_channel *chan)
{
	ast_bridge_lock(bridge);
	switch (bridge->video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		if (bridge->video_mode.mode_data.single_src_data.chan_vsrc == chan) {
			if (bridge->video_mode.mode_data.single_src_data.chan_vsrc) {
				ast_channel_unref(bridge->video_mode.mode_data.single_src_data.chan_vsrc);
			}
			bridge->video_mode.mode_data.single_src_data.chan_vsrc = NULL;
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		if (bridge->video_mode.mode_data.talker_src_data.chan_vsrc == chan) {
			if (bridge->video_mode.mode_data.talker_src_data.chan_vsrc) {
				ast_channel_unref(bridge->video_mode.mode_data.talker_src_data.chan_vsrc);
			}
			bridge->video_mode.mode_data.talker_src_data.chan_vsrc = NULL;
			bridge->video_mode.mode_data.talker_src_data.average_talking_energy = 0;
		}
		if (bridge->video_mode.mode_data.talker_src_data.chan_old_vsrc == chan) {
			if (bridge->video_mode.mode_data.talker_src_data.chan_old_vsrc) {
				ast_channel_unref(bridge->video_mode.mode_data.talker_src_data.chan_old_vsrc);
			}
			bridge->video_mode.mode_data.talker_src_data.chan_old_vsrc = NULL;
		}
	}
	ast_bridge_unlock(bridge);
}

static int channel_hash(const void *obj, int flags)
{
	const struct ast_channel *chan = obj;
	const char *name = obj;
	int hash;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		name = ast_channel_name(chan);
		/* Fall through */
	case OBJ_KEY:
		hash = ast_str_hash(name);
		break;
	case OBJ_PARTIAL_KEY:
		/* Should never happen in hash callback. */
		ast_assert(0);
		hash = 0;
		break;
	}
	return hash;
}

static int channel_cmp(void *obj, void *arg, int flags)
{
	const struct ast_channel *left = obj;
	const struct ast_channel *right = arg;
	const char *right_name = arg;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_name = ast_channel_name(right);
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(ast_channel_name(left), right_name);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncmp(ast_channel_name(left), right_name, strlen(right_name));
		break;
	}
	return cmp ? 0 : CMP_MATCH;
}

struct ao2_container *ast_bridge_peers_nolock(struct ast_bridge *bridge)
{
	struct ao2_container *channels;
	struct ast_bridge_channel *iter;

	channels = ao2_container_alloc_options(AO2_ALLOC_OPT_LOCK_NOLOCK,
		13, channel_hash, channel_cmp);
	if (!channels) {
		return NULL;
	}

	AST_LIST_TRAVERSE(&bridge->channels, iter, entry) {
		ao2_link(channels, iter->chan);
	}

	return channels;
}

struct ao2_container *ast_bridge_peers(struct ast_bridge *bridge)
{
	struct ao2_container *channels;

	ast_bridge_lock(bridge);
	channels = ast_bridge_peers_nolock(bridge);
	ast_bridge_unlock(bridge);

	return channels;
}

struct ast_channel *ast_bridge_peer_nolock(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_channel *peer = NULL;
	struct ast_bridge_channel *iter;

	/* Asking for the peer channel only makes sense on a two-party bridge. */
	if (bridge->num_channels == 2
		&& bridge->technology->capabilities
			& (AST_BRIDGE_CAPABILITY_NATIVE | AST_BRIDGE_CAPABILITY_1TO1MIX)) {
		int in_bridge = 0;

		AST_LIST_TRAVERSE(&bridge->channels, iter, entry) {
			if (iter->chan != chan) {
				peer = iter->chan;
			} else {
				in_bridge = 1;
			}
		}
		if (in_bridge && peer) {
			ast_channel_ref(peer);
		} else {
			peer = NULL;
		}
	}

	return peer;
}

struct ast_channel *ast_bridge_peer(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_channel *peer;

	ast_bridge_lock(bridge);
	peer = ast_bridge_peer_nolock(bridge, chan);
	ast_bridge_unlock(bridge);

	return peer;
}

/*!
 * \internal
 * \brief Transfer an entire bridge to a specific destination.
 *
 * This creates a local channel to dial out and swaps the called local channel
 * with the transferer channel. By doing so, all participants in the bridge are
 * connected to the specified destination.
 *
 * While this means of transferring would work for both two-party and multi-party
 * bridges, this method is only used for multi-party bridges since this method would
 * be less efficient for two-party bridges.
 *
 * \param transferer The channel performing a transfer
 * \param bridge The bridge where the transfer is being performed
 * \param exten The destination extension for the blind transfer
 * \param context The destination context for the blind transfer
 * \param hook Framehook to attach to local channel
 * \return The success or failure of the operation
 */
static enum ast_transfer_result blind_transfer_bridge(struct ast_channel *transferer,
		struct ast_bridge *bridge, const char *exten, const char *context,
		transfer_channel_cb new_channel_cb, void *user_data)
{
	struct ast_channel *local;
	char chan_name[AST_MAX_EXTENSION + AST_MAX_CONTEXT + 2];
	int cause;

	snprintf(chan_name, sizeof(chan_name), "%s@%s", exten, context);
	local = ast_request("Local", ast_channel_nativeformats(transferer), transferer,
			chan_name, &cause);
	if (!local) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (new_channel_cb) {
		new_channel_cb(local, user_data, AST_BRIDGE_TRANSFER_MULTI_PARTY);
	}

	if (ast_call(local, chan_name, 0)) {
		ast_hangup(local);
		return AST_BRIDGE_TRANSFER_FAIL;
	}
	if (ast_bridge_impart(bridge, local, transferer, NULL, 1)) {
		ast_hangup(local);
		return AST_BRIDGE_TRANSFER_FAIL;
	}
	return AST_BRIDGE_TRANSFER_SUCCESS;
}

/*!
 * \brief Perform an attended transfer of a bridge
 *
 * This performs an attended transfer of an entire bridge to a target.
 * The target varies, depending on what bridges exist during the transfer
 * attempt.
 *
 * If two bridges exist, then a local channel is created to link the two
 * bridges together. Local channel optimization may result in the bridges
 * merging.
 *
 * If only one bridge exists, then a local channel is created with one end
 * placed into the existing bridge and the other end masquerading into
 * the unbridged channel.
 *
 * \param chan1 Transferer channel. Guaranteed to be bridged.
 * \param chan2 Other transferer channel. May or may not be bridged.
 * \param bridge1 Bridge that chan1 is in. Guaranteed to be non-NULL.
 * \param bridge2 Bridge that chan2 is in. If NULL, then chan2 is not bridged.
 * \retval AST_BRIDGE_TRANSFER_FAIL Internal error occurred
 * \retval AST_BRIDGE_TRANSFER_SUCCESS Succesfully transferred the bridge
 */
static enum ast_transfer_result attended_transfer_bridge(struct ast_channel *chan1,
		struct ast_channel *chan2, struct ast_bridge *bridge1, struct ast_bridge *bridge2)
{
	static const char *dest = "_attended@transfer/m";
	struct ast_channel *local_chan;
	int cause;
	int res;

	local_chan = ast_request("Local", ast_channel_nativeformats(chan1), chan1,
			dest, &cause);

	if (!local_chan) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (bridge2) {
		res = ast_local_setup_bridge(local_chan, bridge2, chan2, NULL);
	} else {
		res = ast_local_setup_masquerade(local_chan, chan2);
	}

	if (res) {
		ast_hangup(local_chan);
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (ast_call(local_chan, dest, 0)) {
		ast_hangup(local_chan);
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (ast_bridge_impart(bridge1, local_chan, chan1, NULL, 1)) {
		ast_hangup(local_chan);
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	return AST_BRIDGE_TRANSFER_SUCCESS;
}

/*!
 * \internal
 * \brief Get the transferee channel
 *
 * This is only applicable to cases where a transfer is occurring on a
 * two-party bridge. The channels container passed in is expected to only
 * contain two channels, the transferer and the transferee. The transferer
 * channel is passed in as a parameter to ensure we don't return it as
 * the transferee channel.
 *
 * \param channels A two-channel container containing the transferer and transferee
 * \param transferer The party that is transfering the call
 * \return The party that is being transferred
 */
static struct ast_channel *get_transferee(struct ao2_container *channels, struct ast_channel *transferer)
{
	struct ao2_iterator channel_iter;
	struct ast_channel *transferee;

	for (channel_iter = ao2_iterator_init(channels, 0);
			(transferee = ao2_iterator_next(&channel_iter));
			ao2_cleanup(transferee)) {
		if (transferee != transferer) {
			break;
		}
	}

	ao2_iterator_destroy(&channel_iter);
	return transferee;
}

/*!
 * \internal
 * \brief Queue a blind transfer action on a transferee bridge channel
 *
 * This is only relevant for when a blind transfer is performed on a two-party
 * bridge. The transferee's bridge channel will have a blind transfer bridge
 * action queued onto it, resulting in the party being redirected to a new
 * destination
 *
 * \param transferee The channel to have the action queued on
 * \param exten The destination extension for the transferee
 * \param context The destination context for the transferee
 * \param hook Frame hook to attach to transferee
 * \retval 0 Successfully queued the action
 * \retval non-zero Failed to queue the action
 */
static int bridge_channel_queue_blind_transfer(struct ast_channel *transferee,
		const char *exten, const char *context,
		transfer_channel_cb new_channel_cb, void *user_data)
{
	RAII_VAR(struct ast_bridge_channel *, transferee_bridge_channel, NULL, ao2_cleanup);
	struct blind_transfer_data blind_data;

	ast_channel_lock(transferee);
	transferee_bridge_channel = ast_channel_get_bridge_channel(transferee);
	ast_channel_unlock(transferee);

	if (!transferee_bridge_channel) {
		return -1;
	}

	if (new_channel_cb) {
		new_channel_cb(transferee, user_data, AST_BRIDGE_TRANSFER_SINGLE_PARTY);
	}

	ast_copy_string(blind_data.exten, exten, sizeof(blind_data.exten));
	ast_copy_string(blind_data.context, context, sizeof(blind_data.context));

/* BUGBUG Why doesn't this function return success/failure? */
	ast_bridge_channel_queue_action_data(transferee_bridge_channel,
			AST_BRIDGE_ACTION_BLIND_TRANSFER, &blind_data, sizeof(blind_data));

	return 0;
}

static int bridge_channel_queue_attended_transfer(struct ast_channel *transferee,
		struct ast_channel *unbridged_chan)
{
	RAII_VAR(struct ast_bridge_channel *, transferee_bridge_channel, NULL, ao2_cleanup);
	char unbridged_chan_name[AST_CHANNEL_NAME];

	ast_channel_lock(transferee);
	transferee_bridge_channel = ast_channel_get_bridge_channel(transferee);
	ast_channel_unlock(transferee);

	if (!transferee_bridge_channel) {
		return -1;
	}

	ast_copy_string(unbridged_chan_name, ast_channel_name(unbridged_chan),
			sizeof(unbridged_chan_name));

	ast_bridge_channel_queue_action_data(transferee_bridge_channel,
			AST_BRIDGE_ACTION_ATTENDED_TRANSFER, unbridged_chan_name,
			sizeof(unbridged_chan_name));

	return 0;
}

enum try_parking_result {
	PARKING_SUCCESS,
	PARKING_FAILURE,
	PARKING_NOT_APPLICABLE,
};

static enum try_parking_result try_parking(struct ast_bridge *bridge, struct ast_channel *transferer,
		const char *exten, const char *context)
{
	/* BUGBUG The following is all commented out because the functionality is not
	 * present yet. The functions referenced here are available at team/jrose/bridge_projects.
	 * Once the code there has been merged into team/group/bridge_construction,
	 * this can be uncommented and tested
	 */

#if 0
	RAII_VAR(struct ast_bridge_channel *, transferer_bridge_channel, NULL, ao2_cleanup);
	struct ast_exten *parking_exten;

	ast_channel_lock(transferer);
	transfer_bridge_channel = ast_channel_get_bridge_channel(transferer);
	ast_channel_unlock(transferer);

	if (!transfer_bridge_channel) {
		return PARKING_FAILURE;
	}

	parking_exten = ast_get_parking_exten(exten, NULL, context);
	if (parking_exten) {
		return ast_park_blind_xfer(bridge, transferer, parking_exten) == 0 ?
			PARKING_SUCCESS : PARKING_FAILURE;
	}
#endif

	return PARKING_NOT_APPLICABLE;
}

/*!
 * \internal
 * \brief Set the BLINDTRANSFER variable as appropriate on channels involved in the transfer
 *
 * The transferer channel will have its BLINDTRANSFER variable set the same as its BRIDGEPEER
 * variable. This will account for all channels that it is bridged to. The other channels
 * involved in the transfer will have their BLINDTRANSFER variable set to the transferer
 * channel's name.
 *
 * \param transferer The channel performing the blind transfer
 * \param channels The channels belonging to the bridge
 */
static void set_blind_transfer_variables(struct ast_channel *transferer, struct ao2_container *channels)
{
	struct ao2_iterator iter;
	struct ast_channel *chan;
	const char *transferer_name;
	const char *transferer_bridgepeer;

	ast_channel_lock(transferer);
	transferer_name = ast_strdupa(ast_channel_name(transferer));
	transferer_bridgepeer = ast_strdupa(S_OR(pbx_builtin_getvar_helper(transferer, "BRIDGEPEER"), ""));
	ast_channel_unlock(transferer);

	for (iter = ao2_iterator_init(channels, 0);
			(chan = ao2_iterator_next(&iter));
			ao2_cleanup(chan)) {
		if (chan == transferer) {
			pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", transferer_bridgepeer);
		} else {
			pbx_builtin_setvar_helper(chan, "BLINDTRANSFER", transferer_name);
		}
	}

	ao2_iterator_destroy(&iter);
}

enum ast_transfer_result ast_bridge_transfer_blind(struct ast_channel *transferer,
		const char *exten, const char *context,
		transfer_channel_cb new_channel_cb, void *user_data)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, channels, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, transferee, NULL, ao2_cleanup);
	int do_bridge_transfer;
	int transfer_prohibited;
	enum try_parking_result parking_result;

	ast_channel_lock(transferer);
	bridge = ast_channel_get_bridge(transferer);
	ast_channel_unlock(transferer);

	if (!bridge) {
		return AST_BRIDGE_TRANSFER_INVALID;
	}

	parking_result = try_parking(bridge, transferer, exten, context);
	switch (parking_result) {
	case PARKING_SUCCESS:
		return AST_BRIDGE_TRANSFER_SUCCESS;
	case PARKING_FAILURE:
		return AST_BRIDGE_TRANSFER_FAIL;
	case PARKING_NOT_APPLICABLE:
	default:
		break;
	}

	{
		SCOPED_LOCK(lock, bridge, ast_bridge_lock, ast_bridge_unlock);
		channels = ast_bridge_peers_nolock(bridge);
		if (!channels) {
			return AST_BRIDGE_TRANSFER_FAIL;
		}
		if (ao2_container_count(channels) <= 1) {
			return AST_BRIDGE_TRANSFER_INVALID;
		}
		transfer_prohibited = ast_test_flag(&bridge->feature_flags,
				AST_BRIDGE_FLAG_TRANSFER_PROHIBITED);
		do_bridge_transfer = ast_test_flag(&bridge->feature_flags,
				AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY) ||
				ao2_container_count(channels) > 2;
	}

	if (transfer_prohibited) {
		return AST_BRIDGE_TRANSFER_NOT_PERMITTED;
	}

	set_blind_transfer_variables(transferer, channels);

	if (do_bridge_transfer) {
		return blind_transfer_bridge(transferer, bridge, exten, context,
				new_channel_cb, user_data);
	}

	/* Reaching this portion means that we're dealing with a two-party bridge */

	transferee = get_transferee(channels, transferer);
	if (!transferee) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (bridge_channel_queue_blind_transfer(transferee, exten, context,
				new_channel_cb, user_data)) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	ast_bridge_remove(bridge, transferer);
	return AST_BRIDGE_TRANSFER_SUCCESS;
}

static struct ast_bridge *acquire_bridge(struct ast_channel *chan)
{
	struct ast_bridge *bridge;

	ast_channel_lock(chan);
	bridge = ast_channel_get_bridge(chan);
	ast_channel_unlock(chan);

	if (!bridge) {
		return NULL;
	}

	if (ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_MASQUERADE_ONLY)) {
		ao2_ref(bridge, -1);
		bridge = NULL;
	}

	return bridge;
}

/*!
 * \internal
 * \brief Performs an attended transfer by moving a channel from one bridge to another
 *
 * The channel that is bridged to the source_channel is moved into the dest_bridge from
 * the source_bridge_channel's bridge. The swap_channel is swapped out of the dest_bridge and placed in
 * the source_bridge_channel's bridge.
 *
 * \note dest_bridge and source_bridge_channel's bridge MUST be locked before calling this function.
 *
 * \param dest_bridge The final bridge for the attended transfer
 * \param source_channel Channel who is bridged to the channel that will move
 * \param swap_channel Channel to be swapped out of the dest_bridge
 * \return The success or failure of the swap attempt
 */
static enum ast_transfer_result bridge_swap_attended_transfer(struct ast_bridge *dest_bridge,
		struct ast_bridge_channel *source_bridge_channel, struct ast_channel *swap_channel)
{
	struct ast_bridge_channel *bridged_to_source;

	bridged_to_source = ast_bridge_channel_peer(source_bridge_channel);
	if (bridged_to_source && bridged_to_source->state == AST_BRIDGE_CHANNEL_STATE_WAIT
			&& !ast_test_flag(&bridged_to_source->features->feature_flags, AST_BRIDGE_CHANNEL_FLAG_IMMOVABLE)) {
		bridged_to_source->swap = swap_channel;
		return bridge_move_do(dest_bridge, bridged_to_source, 1) ?
			AST_BRIDGE_TRANSFER_FAIL : AST_BRIDGE_TRANSFER_SUCCESS;
	} else {
		return AST_BRIDGE_TRANSFER_INVALID;
	}
}

/*!
 * \internal
 * \brief Function that performs an attended transfer when both transferer channels are bridged
 *
 * The method by which the transfer is performed is dependent on whether the bridges allow for
 * optimization to occur between them. If no optimization is permitted, then an unreal channel
 * is placed as a link between the two bridges. If optimization is permitted, then that means
 * we are free to perform move or merge operations in order to perform the transfer.
 *
 * \note to_transferee_bridge and to_target_bridge MUST be locked before calling this function
 *
 * \param to_transferee The channel that is bridged to the transferee
 * \param to_transferee_bridge_channel to_transferee's bridge_channel
 * \param to_transfer_target The channel that is bridged to the transfer target
 * \param to_target_bridge_channel to_transfer_target's bridge_channel
 * \param to_transferee_bridge The bridge between to_transferee and the transferee
 * \param to_target_bridge The bridge between to_transfer_target and the transfer_target
 * \return The success or failure of the attended transfer
 */
static enum ast_transfer_result two_bridge_attended_transfer(struct ast_channel *to_transferee,
		struct ast_bridge_channel *to_transferee_bridge_channel,
		struct ast_channel *to_transfer_target,
		struct ast_bridge_channel *to_target_bridge_channel,
		struct ast_bridge *to_transferee_bridge, struct ast_bridge *to_target_bridge)
{
	struct ast_bridge_channel *kick_me[] = {
			to_transferee_bridge_channel,
			to_target_bridge_channel,
	};

	switch (ast_bridges_allow_optimization(to_transferee_bridge, to_target_bridge)) {
	case AST_BRIDGE_OPTIMIZE_SWAP_TO_CHAN_BRIDGE:
		return bridge_swap_attended_transfer(to_transferee_bridge, to_target_bridge_channel, to_transferee);
	case AST_BRIDGE_OPTIMIZE_SWAP_TO_PEER_BRIDGE:
		return bridge_swap_attended_transfer(to_target_bridge, to_transferee_bridge_channel, to_transfer_target);
	case AST_BRIDGE_OPTIMIZE_MERGE_TO_CHAN_BRIDGE:
		bridge_merge_do(to_transferee_bridge, to_target_bridge, kick_me, ARRAY_LEN(kick_me));
		return AST_BRIDGE_TRANSFER_SUCCESS;
	case AST_BRIDGE_OPTIMIZE_MERGE_TO_PEER_BRIDGE:
		bridge_merge_do(to_target_bridge, to_transferee_bridge, kick_me, ARRAY_LEN(kick_me));
		return AST_BRIDGE_TRANSFER_SUCCESS;
	case AST_BRIDGE_OPTIMIZE_PROHIBITED:
	default:
		/* Just because optimization wasn't doable doesn't necessarily mean
		 * that we can actually perform the transfer. Some reasons for non-optimization
		 * indicate bridge invalidity, so let's check those before proceeding.
		 */
		if (to_transferee_bridge->inhibit_merge || to_transferee_bridge->dissolved ||
				to_target_bridge->inhibit_merge || to_target_bridge->dissolved) {
			return AST_BRIDGE_TRANSFER_INVALID;
		}
		return attended_transfer_bridge(to_transferee, to_transfer_target,
			to_transferee_bridge, to_target_bridge);
	}
}

enum ast_transfer_result ast_bridge_transfer_attended(struct ast_channel *to_transferee,
		struct ast_channel *to_transfer_target)
{
	RAII_VAR(struct ast_bridge *, to_transferee_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_bridge *, to_target_bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ao2_container *, channels, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, transferee, NULL, ao2_cleanup);
	struct ast_bridge *the_bridge;
	struct ast_channel *chan_bridged;
	struct ast_channel *chan_unbridged;
	int transfer_prohibited;
	int do_bridge_transfer;

	to_transferee_bridge = acquire_bridge(to_transferee);
	to_target_bridge = acquire_bridge(to_transfer_target);

	/* They can't both be unbridged, you silly goose! */
	if (!to_transferee_bridge && !to_target_bridge) {
		return AST_BRIDGE_TRANSFER_INVALID;
	}

	/* Let's get the easy one out of the way first */
	if (to_transferee_bridge && to_target_bridge) {
		RAII_VAR(struct ast_bridge_channel *, to_transferee_bridge_channel, NULL, ao2_cleanup);
		RAII_VAR(struct ast_bridge_channel *, to_target_bridge_channel, NULL, ao2_cleanup);
		enum ast_transfer_result res;

		ast_channel_lock(to_transferee);
		to_transferee_bridge_channel = ast_channel_get_bridge_channel(to_transferee);
		ast_channel_unlock(to_transferee);

		ast_channel_lock(to_transfer_target);
		to_target_bridge_channel = ast_channel_get_bridge_channel(to_transfer_target);
		ast_channel_unlock(to_transfer_target);

		ast_bridge_lock_both(to_transferee_bridge, to_target_bridge);
		res = two_bridge_attended_transfer(to_transferee, to_transferee_bridge_channel,
				to_transfer_target, to_target_bridge_channel,
				to_transferee_bridge, to_target_bridge);
		ast_bridge_unlock(to_transferee_bridge);
		ast_bridge_unlock(to_target_bridge);

		return res;
	}

	the_bridge = to_transferee_bridge ?: to_target_bridge;
	chan_bridged = to_transferee_bridge ? to_transferee : to_transfer_target;
	chan_unbridged = to_transferee_bridge ? to_transfer_target : to_transferee;

	{
		int chan_count;
		SCOPED_LOCK(lock, the_bridge, ast_bridge_lock, ast_bridge_unlock);

		channels = ast_bridge_peers_nolock(the_bridge);
		if (!channels) {
			return AST_BRIDGE_TRANSFER_FAIL;
		}
		chan_count = ao2_container_count(channels);
		if (chan_count <= 1) {
			return AST_BRIDGE_TRANSFER_INVALID;
		}
		transfer_prohibited = ast_test_flag(&the_bridge->feature_flags,
				AST_BRIDGE_FLAG_TRANSFER_PROHIBITED);
		do_bridge_transfer = ast_test_flag(&the_bridge->feature_flags,
				AST_BRIDGE_FLAG_TRANSFER_BRIDGE_ONLY) ||
				chan_count > 2;
	}

	if (transfer_prohibited) {
		return AST_BRIDGE_TRANSFER_NOT_PERMITTED;
	}

	if (do_bridge_transfer) {
		return attended_transfer_bridge(chan_bridged, chan_unbridged, the_bridge, NULL);
	}

	transferee = get_transferee(channels, chan_bridged);
	if (!transferee) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	if (bridge_channel_queue_attended_transfer(transferee, chan_unbridged)) {
		return AST_BRIDGE_TRANSFER_FAIL;
	}

	ast_bridge_remove(the_bridge, chan_bridged);
	return AST_BRIDGE_TRANSFER_SUCCESS;
}

/*!
 * \internal
 * \brief Service the bridge manager request.
 * \since 12.0.0
 *
 * \param bridge requesting service.
 *
 * \return Nothing
 */
static void bridge_manager_service(struct ast_bridge *bridge)
{
	ast_bridge_lock(bridge);
	if (bridge->callid) {
		ast_callid_threadassoc_change(bridge->callid);
	}

	/* Do any pending bridge actions. */
	bridge_handle_actions(bridge);
	ast_bridge_unlock(bridge);
}

/*!
 * \internal
 * \brief Bridge manager service thread.
 * \since 12.0.0
 *
 * \return Nothing
 */
static void *bridge_manager_thread(void *data)
{
	struct bridge_manager_controller *manager = data;
	struct bridge_manager_request *request;

	ao2_lock(manager);
	while (!manager->stop) {
		request = AST_LIST_REMOVE_HEAD(&manager->service_requests, node);
		if (!request) {
			ast_cond_wait(&manager->cond, ao2_object_get_lockaddr(manager));
			continue;
		}
		ao2_unlock(manager);

		/* Service the bridge. */
		bridge_manager_service(request->bridge);
		ao2_ref(request->bridge, -1);
		ast_free(request);

		ao2_lock(manager);
	}
	ao2_unlock(manager);

	return NULL;
}

/*!
 * \internal
 * \brief Destroy the bridge manager controller.
 * \since 12.0.0
 *
 * \param obj Bridge manager to destroy.
 *
 * \return Nothing
 */
static void bridge_manager_destroy(void *obj)
{
	struct bridge_manager_controller *manager = obj;
	struct bridge_manager_request *request;

	if (manager->thread != AST_PTHREADT_NULL) {
		/* Stop the manager thread. */
		ao2_lock(manager);
		manager->stop = 1;
		ast_cond_signal(&manager->cond);
		ao2_unlock(manager);
		ast_debug(1, "Waiting for bridge manager thread to die.\n");
		pthread_join(manager->thread, NULL);
	}

	/* Destroy the service request queue. */
	while ((request = AST_LIST_REMOVE_HEAD(&manager->service_requests, node))) {
		ao2_ref(request->bridge, -1);
		ast_free(request);
	}

	ast_cond_destroy(&manager->cond);
}

/*!
 * \internal
 * \brief Create the bridge manager controller.
 * \since 12.0.0
 *
 * \retval manager on success.
 * \retval NULL on error.
 */
static struct bridge_manager_controller *bridge_manager_create(void)
{
	struct bridge_manager_controller *manager;

	manager = ao2_alloc(sizeof(*manager), bridge_manager_destroy);
	if (!manager) {
		/* Well. This isn't good. */
		return NULL;
	}
	ast_cond_init(&manager->cond, NULL);
	AST_LIST_HEAD_INIT_NOLOCK(&manager->service_requests);

	/* Create the bridge manager thread. */
	if (ast_pthread_create(&manager->thread, NULL, bridge_manager_thread, manager)) {
		/* Well. This isn't good either. */
		manager->thread = AST_PTHREADT_NULL;
		ao2_ref(manager, -1);
		manager = NULL;
	}

	return manager;
}

/*!
 * \internal
 * \brief Bridge ao2 container sort function.
 * \since 12.0.0
 *
 * \param obj_left pointer to the (user-defined part) of an object.
 * \param obj_right pointer to the (user-defined part) of an object.
 * \param flags flags from ao2_callback()
 *   OBJ_POINTER - if set, 'obj_right', is an object.
 *   OBJ_KEY - if set, 'obj_right', is a search key item that is not an object.
 *   OBJ_PARTIAL_KEY - if set, 'obj_right', is a partial search key item that is not an object.
 *
 * \retval <0 if obj_left < obj_right
 * \retval =0 if obj_left == obj_right
 * \retval >0 if obj_left > obj_right
 */
static int bridge_sort_cmp(const void *obj_left, const void *obj_right, int flags)
{
	const struct ast_bridge *bridge_left = obj_left;
	const struct ast_bridge *bridge_right = obj_right;
	const char *right_key = obj_right;
	int cmp;

	switch (flags & (OBJ_POINTER | OBJ_KEY | OBJ_PARTIAL_KEY)) {
	default:
	case OBJ_POINTER:
		right_key = bridge_right->uniqueid;
		/* Fall through */
	case OBJ_KEY:
		cmp = strcmp(bridge_left->uniqueid, right_key);
		break;
	case OBJ_PARTIAL_KEY:
		cmp = strncmp(bridge_left->uniqueid, right_key, strlen(right_key));
		break;
	}
	return cmp;
}

struct bridge_complete {
	/*! Nth match to return. */
	int state;
	/*! Which match currently on. */
	int which;
};

static int complete_bridge_search(void *obj, void *arg, void *data, int flags)
{
	struct bridge_complete *search = data;

	if (++search->which > search->state) {
		return CMP_MATCH;
	}
	return 0;
}

static char *complete_bridge(const char *word, int state)
{
	char *ret;
	struct ast_bridge *bridge;
	struct bridge_complete search = {
		.state = state,
		};

	bridge = ao2_callback_data(bridges, ast_strlen_zero(word) ? 0 : OBJ_PARTIAL_KEY,
		complete_bridge_search, (char *) word, &search);
	if (!bridge) {
		return NULL;
	}
	ret = ast_strdup(bridge->uniqueid);
	ao2_ref(bridge, -1);
	return ret;
}

static char *handle_bridge_show_all(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT_HDR "%-36s %5s %-15s %s\n"
#define FORMAT_ROW "%-36s %5u %-15s %s\n"

	struct ao2_iterator iter;
	struct ast_bridge *bridge;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge show all";
		e->usage =
			"Usage: bridge show all\n"
			"       List all bridges\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

/* BUGBUG this command may need to be changed to look at the stasis cache. */
	ast_cli(a->fd, FORMAT_HDR, "Bridge-ID", "Chans", "Type", "Technology");
	iter = ao2_iterator_init(bridges, 0);
	for (; (bridge = ao2_iterator_next(&iter)); ao2_ref(bridge, -1)) {
		ast_bridge_lock(bridge);
		ast_cli(a->fd, FORMAT_ROW,
			bridge->uniqueid,
			bridge->num_channels,
			bridge->v_table ? bridge->v_table->name : "<unknown>",
			bridge->technology ? bridge->technology->name : "<unknown>");
		ast_bridge_unlock(bridge);
	}
	ao2_iterator_destroy(&iter);
	return CLI_SUCCESS;

#undef FORMAT_HDR
#undef FORMAT_ROW
}

static char *handle_bridge_show_specific(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_bridge *bridge;
	struct ast_bridge_channel *bridge_channel;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge show";
		e->usage =
			"Usage: bridge show <bridge-id>\n"
			"       Show information about the <bridge-id> bridge\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_bridge(a->word, a->n);
		}
		return NULL;
	}

/* BUGBUG this command may need to be changed to look at the stasis cache. */
	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	bridge = ao2_find(bridges, a->argv[2], OBJ_KEY);
	if (!bridge) {
		ast_cli(a->fd, "Bridge '%s' not found\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	ast_bridge_lock(bridge);
	ast_cli(a->fd, "Id: %s\n", bridge->uniqueid);
	ast_cli(a->fd, "Type: %s\n", bridge->v_table ? bridge->v_table->name : "<unknown>");
	ast_cli(a->fd, "Technology: %s\n",
		bridge->technology ? bridge->technology->name : "<unknown>");
	ast_cli(a->fd, "Num-Channels: %u\n", bridge->num_channels);
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		ast_cli(a->fd, "Channel: %s\n", ast_channel_name(bridge_channel->chan));
	}
	ast_bridge_unlock(bridge);
	ao2_ref(bridge, -1);

	return CLI_SUCCESS;
}

static char *handle_bridge_destroy_specific(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_bridge *bridge;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge destroy";
		e->usage =
			"Usage: bridge destroy <bridge-id>\n"
			"       Destroy the <bridge-id> bridge\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_bridge(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc != 3) {
		return CLI_SHOWUSAGE;
	}

	bridge = ao2_find(bridges, a->argv[2], OBJ_KEY);
	if (!bridge) {
		ast_cli(a->fd, "Bridge '%s' not found\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	ast_cli(a->fd, "Destroying bridge '%s'\n", a->argv[2]);
	ast_bridge_destroy(bridge);

	return CLI_SUCCESS;
}

static char *complete_bridge_participant(const char *bridge_name, const char *line, const char *word, int pos, int state)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	struct ast_bridge_channel *bridge_channel;
	int which;
	int wordlen;

	bridge = ao2_find(bridges, bridge_name, OBJ_KEY);
	if (!bridge) {
		return NULL;
	}

	{
		SCOPED_LOCK(bridge_lock, bridge, ast_bridge_lock, ast_bridge_unlock);

		which = 0;
		wordlen = strlen(word);
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			if (!strncasecmp(ast_channel_name(bridge_channel->chan), word, wordlen)
				&& ++which > state) {
				return ast_strdup(ast_channel_name(bridge_channel->chan));
			}
		}
	}

	return NULL;
}

static char *handle_bridge_kick_channel(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	RAII_VAR(struct ast_bridge *, bridge, NULL, ao2_cleanup);
	RAII_VAR(struct ast_channel *, chan, NULL, ao2_cleanup);

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge kick";
		e->usage =
			"Usage: bridge kick <bridge-id> <channel-name>\n"
			"       Kick the <channel-name> channel out of the <bridge-id> bridge\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 2) {
			return complete_bridge(a->word, a->n);
		}
		if (a->pos == 3) {
			return complete_bridge_participant(a->argv[2], a->line, a->word, a->pos, a->n);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	bridge = ao2_find(bridges, a->argv[2], OBJ_KEY);
	if (!bridge) {
		ast_cli(a->fd, "Bridge '%s' not found\n", a->argv[2]);
		return CLI_SUCCESS;
	}

	chan = ast_channel_get_by_name_prefix(a->argv[3], strlen(a->argv[3]));
	if (!chan) {
		ast_cli(a->fd, "Channel '%s' not found\n", a->argv[3]);
		return CLI_SUCCESS;
	}

/*
 * BUGBUG the CLI kick needs to get the bridge to decide if it should dissolve.
 *
 * Likely the best way to do this is to add a kick method.  The
 * basic bridge class can then decide to dissolve the bridge if
 * one of two channels is kicked.
 *
 * SIP/foo -- Local;1==Local;2 -- .... -- Local;1==Local;2 -- SIP/bar
 * Kick a ;1 channel and the chain toward SIP/foo goes away.
 * Kick a ;2 channel and the chain toward SIP/bar goes away.
 *
 * This can leave a local channel chain between the kicked ;1
 * and ;2 channels that are orphaned until you manually request
 * one of those channels to hangup or request the bridge to
 * dissolve.
 */
	ast_cli(a->fd, "Kicking channel '%s' from bridge '%s'\n",
		ast_channel_name(chan), a->argv[2]);
	ast_bridge_remove(bridge, chan);

	return CLI_SUCCESS;
}

/*! Bridge technology capabilities to string. */
static const char *tech_capability2str(uint32_t capabilities)
{
	const char *type;

	if (capabilities & AST_BRIDGE_CAPABILITY_HOLDING) {
		type = "Holding";
	} else if (capabilities & AST_BRIDGE_CAPABILITY_EARLY) {
		type = "Early";
	} else if (capabilities & AST_BRIDGE_CAPABILITY_NATIVE) {
		type = "Native";
	} else if (capabilities & AST_BRIDGE_CAPABILITY_1TO1MIX) {
		type = "1to1Mix";
	} else if (capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX) {
		type = "MultiMix";
	} else {
		type = "<Unknown>";
	}
	return type;
}

static char *handle_bridge_technology_show(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define FORMAT_HDR "%-20s %-20s %8s %s\n"
#define FORMAT_ROW "%-20s %-20s %8d %s\n"

	struct ast_bridge_technology *cur;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge technology show";
		e->usage =
			"Usage: bridge technology show\n"
			"       List registered bridge technologies\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	ast_cli(a->fd, FORMAT_HDR, "Name", "Type", "Priority", "Suspended");
	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, cur, entry) {
		const char *type;

		/* Decode type for display */
		type = tech_capability2str(cur->capabilities);

		ast_cli(a->fd, FORMAT_ROW, cur->name, type, cur->preference,
			AST_CLI_YESNO(cur->suspended));
	}
	AST_RWLIST_UNLOCK(&bridge_technologies);
	return CLI_SUCCESS;

#undef FORMAT
}

static char *complete_bridge_technology(const char *word, int state)
{
	struct ast_bridge_technology *cur;
	char *res;
	int which;
	int wordlen;

	which = 0;
	wordlen = strlen(word);
	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, cur, entry) {
		if (!strncasecmp(cur->name, word, wordlen) && ++which > state) {
			res = ast_strdup(cur->name);
			AST_RWLIST_UNLOCK(&bridge_technologies);
			return res;
		}
	}
	AST_RWLIST_UNLOCK(&bridge_technologies);
	return NULL;
}

static char *handle_bridge_technology_suspend(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_bridge_technology *cur;
	int suspend;
	int successful;

	switch (cmd) {
	case CLI_INIT:
		e->command = "bridge technology {suspend|unsuspend}";
		e->usage =
			"Usage: bridge technology {suspend|unsuspend} <technology-name>\n"
			"       Suspend or unsuspend a bridge technology.\n";
		return NULL;
	case CLI_GENERATE:
		if (a->pos == 3) {
			return complete_bridge_technology(a->word, a->n);
		}
		return NULL;
	}

	if (a->argc != 4) {
		return CLI_SHOWUSAGE;
	}

	suspend = !strcasecmp(a->argv[2], "suspend");
	successful = 0;
	AST_RWLIST_WRLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, cur, entry) {
		if (!strcasecmp(cur->name, a->argv[3])) {
			successful = 1;
			if (suspend) {
				ast_bridge_technology_suspend(cur);
			} else {
				ast_bridge_technology_unsuspend(cur);
			}
			break;
		}
	}
	AST_RWLIST_UNLOCK(&bridge_technologies);

	if (successful) {
		if (suspend) {
			ast_cli(a->fd, "Suspended bridge technology '%s'\n", a->argv[3]);
		} else {
			ast_cli(a->fd, "Unsuspended bridge technology '%s'\n", a->argv[3]);
		}
	} else {
		ast_cli(a->fd, "Bridge technology '%s' not found\n", a->argv[3]);
	}

	return CLI_SUCCESS;
}

static struct ast_cli_entry bridge_cli[] = {
	AST_CLI_DEFINE(handle_bridge_show_all, "List all bridges"),
	AST_CLI_DEFINE(handle_bridge_show_specific, "Show information about a bridge"),
	AST_CLI_DEFINE(handle_bridge_destroy_specific, "Destroy a bridge"),
	AST_CLI_DEFINE(handle_bridge_kick_channel, "Kick a channel from a bridge"),
	AST_CLI_DEFINE(handle_bridge_technology_show, "List registered bridge technologies"),
	AST_CLI_DEFINE(handle_bridge_technology_suspend, "Suspend/unsuspend a bridge technology"),
};

/*!
 * \internal
 * \brief Shutdown the bridging system.
 * \since 12.0.0
 *
 * \return Nothing
 */
static void bridge_shutdown(void)
{
	ast_cli_unregister_multiple(bridge_cli, ARRAY_LEN(bridge_cli));
	ao2_cleanup(bridges);
	bridges = NULL;
	ao2_cleanup(bridge_manager);
	bridge_manager = NULL;
	ast_stasis_bridging_shutdown();
}

int ast_bridging_init(void)
{
	if (ast_stasis_bridging_init()) {
		bridge_shutdown();
		return -1;
	}

	bridge_manager = bridge_manager_create();
	if (!bridge_manager) {
		bridge_shutdown();
		return -1;
	}

	bridges = ao2_container_alloc_rbtree(AO2_ALLOC_OPT_LOCK_MUTEX,
		AO2_CONTAINER_ALLOC_OPT_DUPS_REPLACE, bridge_sort_cmp, NULL);
	if (!bridges) {
		bridge_shutdown();
		return -1;
	}

	ast_bridging_init_basic();

/* BUGBUG need AMI action equivalents to the CLI commands. */
	ast_cli_register_multiple(bridge_cli, ARRAY_LEN(bridge_cli));

	ast_register_atexit(bridge_shutdown);
	return 0;
}
