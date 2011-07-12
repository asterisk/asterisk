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
#include "asterisk/bridging_technology.h"
#include "asterisk/app.h"
#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/astobj2.h"

static AST_RWLIST_HEAD_STATIC(bridge_technologies, ast_bridge_technology);

/* Initial starting point for the bridge array of channels */
#define BRIDGE_ARRAY_START 128

/* Grow rate of bridge array of channels */
#define BRIDGE_ARRAY_GROW 32

static void cleanup_video_mode(struct ast_bridge *bridge);

/*! Default DTMF keys for built in features */
static char builtin_features_dtmf[AST_BRIDGE_BUILTIN_END][MAXIMUM_DTMF_FEATURE_STRING];

/*! Function handlers for the built in features */
static void *builtin_features_handlers[AST_BRIDGE_BUILTIN_END];

int __ast_bridge_technology_register(struct ast_bridge_technology *technology, struct ast_module *module)
{
	struct ast_bridge_technology *current = NULL;

	/* Perform a sanity check to make sure the bridge technology conforms to our needed requirements */
	if (ast_strlen_zero(technology->name) || !technology->capabilities || !technology->write) {
		ast_log(LOG_WARNING, "Bridge technology %s failed registration sanity check.\n", technology->name);
		return -1;
	}

	AST_RWLIST_WRLOCK(&bridge_technologies);

	/* Look for duplicate bridge technology already using this name, or already registered */
	AST_RWLIST_TRAVERSE(&bridge_technologies, current, entry) {
		if ((!strcasecmp(current->name, technology->name)) || (current == technology)) {
			ast_log(LOG_WARNING, "A bridge technology of %s already claims to exist in our world.\n", technology->name);
			AST_RWLIST_UNLOCK(&bridge_technologies);
			return -1;
		}
	}

	/* Copy module pointer so reference counting can keep the module from unloading */
	technology->mod = module;

	/* Insert our new bridge technology into the list and print out a pretty message */
	AST_RWLIST_INSERT_TAIL(&bridge_technologies, technology, entry);

	AST_RWLIST_UNLOCK(&bridge_technologies);

	if (option_verbose > 1) {
		ast_verbose(VERBOSE_PREFIX_2 "Registered bridge technology %s\n", technology->name);
	}

	return 0;
}

int ast_bridge_technology_unregister(struct ast_bridge_technology *technology)
{
	struct ast_bridge_technology *current = NULL;

	AST_RWLIST_WRLOCK(&bridge_technologies);

	/* Ensure the bridge technology is registered before removing it */
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&bridge_technologies, current, entry) {
		if (current == technology) {
			AST_RWLIST_REMOVE_CURRENT(entry);
			if (option_verbose > 1) {
				ast_verbose(VERBOSE_PREFIX_2 "Unregistered bridge technology %s\n", technology->name);
			}
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	AST_RWLIST_UNLOCK(&bridge_technologies);

	return current ? 0 : -1;
}

void ast_bridge_change_state(struct ast_bridge_channel *bridge_channel, enum ast_bridge_channel_state new_state)
{
	/* Change the state on the bridge channel */
	bridge_channel->state = new_state;

	/* Only poke the channel's thread if it is not us */
	if (!pthread_equal(pthread_self(), bridge_channel->thread)) {
		pthread_kill(bridge_channel->thread, SIGURG);
		ao2_lock(bridge_channel);
		ast_cond_signal(&bridge_channel->cond);
		ao2_unlock(bridge_channel);
	}

	return;
}

/*! \brief Helper function to poke the bridge thread */
static void bridge_poke(struct ast_bridge *bridge)
{
	/* Poke the thread just in case */
	if (bridge->thread != AST_PTHREADT_NULL && bridge->thread != AST_PTHREADT_STOP) {
		pthread_kill(bridge->thread, SIGURG);
	}

	return;
}

/*! \brief Helper function to add a channel to the bridge array
 *
 * \note This function assumes the bridge is locked.
 */
static void bridge_array_add(struct ast_bridge *bridge, struct ast_channel *chan)
{
	/* We have to make sure the bridge thread is not using the bridge array before messing with it */
	while (bridge->waiting) {
		bridge_poke(bridge);
		sched_yield();
	}

	bridge->array[bridge->array_num++] = chan;

	ast_debug(1, "Added channel %s(%p) to bridge array on %p, new count is %d\n", chan->name, chan, bridge, (int)bridge->array_num);

	/* If the next addition of a channel will exceed our array size grow it out */
	if (bridge->array_num == bridge->array_size) {
		struct ast_channel **tmp;
		ast_debug(1, "Growing bridge array on %p from %d to %d\n", bridge, (int)bridge->array_size, (int)bridge->array_size + BRIDGE_ARRAY_GROW);
		if (!(tmp = ast_realloc(bridge->array, (bridge->array_size + BRIDGE_ARRAY_GROW) * sizeof(struct ast_channel *)))) {
			ast_log(LOG_ERROR, "Failed to allocate more space for another channel on bridge '%p', this is not going to end well\n", bridge);
			return;
		}
		bridge->array = tmp;
		bridge->array_size += BRIDGE_ARRAY_GROW;
	}

	return;
}

/*! \brief Helper function to remove a channel from the bridge array
 *
 * \note This function assumes the bridge is locked.
 */
static void bridge_array_remove(struct ast_bridge *bridge, struct ast_channel *chan)
{
	int i;

	/* We have to make sure the bridge thread is not using the bridge array before messing with it */
	while (bridge->waiting) {
		bridge_poke(bridge);
		sched_yield();
	}

	for (i = 0; i < bridge->array_num; i++) {
		if (bridge->array[i] == chan) {
			bridge->array[i] = (bridge->array[(bridge->array_num - 1)] != chan ? bridge->array[(bridge->array_num - 1)] : NULL);
			bridge->array[(bridge->array_num - 1)] = NULL;
			bridge->array_num--;
			ast_debug(1, "Removed channel %p from bridge array on %p, new count is %d\n", chan, bridge, (int)bridge->array_num);
			break;
		}
	}

	return;
}

/*! \brief Helper function to find a bridge channel given a channel */
static struct ast_bridge_channel *find_bridge_channel(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel = NULL;

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (bridge_channel->chan == chan) {
			break;
		}
	}

	return bridge_channel;
}

/*! \brief Internal function to see whether a bridge should dissolve, and if so do it */
static void bridge_check_dissolve(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_channel *bridge_channel2 = NULL;

	if (!ast_test_flag(&bridge->feature_flags, AST_BRIDGE_FLAG_DISSOLVE) && (!bridge_channel->features || !bridge_channel->features->usable || !ast_test_flag(&bridge_channel->features->feature_flags, AST_BRIDGE_FLAG_DISSOLVE))) {
		return;
	}

	ast_debug(1, "Dissolving bridge %p\n", bridge);

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel2, entry) {
		if (bridge_channel2->state != AST_BRIDGE_CHANNEL_STATE_END && bridge_channel2->state != AST_BRIDGE_CHANNEL_STATE_DEPART) {
			ast_bridge_change_state(bridge_channel2, AST_BRIDGE_CHANNEL_STATE_HANGUP);
		}
	}

	/* Since all the channels are going away let's go ahead and stop our on thread */
	bridge->stop = 1;

	return;
}

/*! \brief Internal function to handle DTMF from a channel */
static struct ast_frame *bridge_handle_dtmf(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_features *features = (bridge_channel->features ? bridge_channel->features : &bridge->features);
	struct ast_bridge_features_hook *hook = NULL;

	/* If the features structure we grabbed is not usable immediately return the frame */
	if (!features->usable) {
		return frame;
	}

	/* See if this DTMF matches the beginnings of any feature hooks, if so we switch to the feature state to either execute the feature or collect more DTMF */
	AST_LIST_TRAVERSE(&features->hooks, hook, entry) {
		if (hook->dtmf[0] == frame->subclass.integer) {
			ast_frfree(frame);
			frame = NULL;
			ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_FEATURE);
			break;
		}
	}

	return frame;
}

/*! \brief Internal function used to determine whether a control frame should be dropped or not */
static int bridge_drop_control_frame(int subclass)
{
	switch (subclass) {
	case AST_CONTROL_ANSWER:
	case -1:
		return 1;
	default:
		return 0;
	}
}

void ast_bridge_notify_talking(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, int started_talking)
{
	if (started_talking) {
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_START_TALKING);
	} else {
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_STOP_TALKING);
	}
}

void ast_bridge_handle_trip(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_channel *chan, int outfd)
{
	/* If no bridge channel has been provided and the actual channel has been provided find it */
	if (chan && !bridge_channel) {
		bridge_channel = find_bridge_channel(bridge, chan);
	}

	/* If a bridge channel with actual channel is present read a frame and handle it */
	if (chan && bridge_channel) {
		struct ast_frame *frame = (((bridge->features.mute) || (bridge_channel->features && bridge_channel->features->mute)) ? ast_read_noaudio(chan) : ast_read(chan));

		/* This is pretty simple... see if they hung up */
		if (!frame || (frame->frametype == AST_FRAME_CONTROL && frame->subclass.integer == AST_CONTROL_HANGUP)) {
			/* Signal the thread that is handling the bridged channel that it should be ended */
			ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_END);
		} else if (frame->frametype == AST_FRAME_CONTROL && bridge_drop_control_frame(frame->subclass.integer)) {
			ast_debug(1, "Dropping control frame from bridge channel %p\n", bridge_channel);
		} else if (frame->frametype == AST_FRAME_DTMF_BEGIN || frame->frametype == AST_FRAME_DTMF_END) {
			int dtmf_passthrough = bridge_channel->features ?
				bridge_channel->features->dtmf_passthrough :
				bridge->features.dtmf_passthrough;

			if (frame->frametype == AST_FRAME_DTMF_BEGIN) {
				frame = bridge_handle_dtmf(bridge, bridge_channel, frame);
			}

			if (frame && dtmf_passthrough) {
				bridge->technology->write(bridge, bridge_channel, frame);
			}
		} else {
			/* Simply write the frame out to the bridge technology if it still exists */
			bridge->technology->write(bridge, bridge_channel, frame);
		}

		if (frame) {
			ast_frfree(frame);
		}
		return;
	}

	/* If a file descriptor actually tripped pass it off to the bridge technology */
	if (outfd > -1 && bridge->technology->fd) {
		bridge->technology->fd(bridge, bridge_channel, outfd);
		return;
	}

	/* If all else fails just poke the bridge */
	if (bridge->technology->poke && bridge_channel) {
		bridge->technology->poke(bridge, bridge_channel);
		return;
	}

	return;
}

/*! \brief Generic thread loop, TODO: Rethink this/improve it */
static int generic_thread_loop(struct ast_bridge *bridge)
{
	while (!bridge->stop && !bridge->refresh && bridge->array_num) {
		struct ast_channel *winner = NULL;
		int to = -1;

		/* Move channels around for priority reasons if we have more than one channel in our array */
		if (bridge->array_num > 1) {
			struct ast_channel *first = bridge->array[0];
			memmove(bridge->array, bridge->array + 1, sizeof(struct ast_channel *) * (bridge->array_num - 1));
			bridge->array[(bridge->array_num - 1)] = first;
		}

		/* Wait on the channels */
		bridge->waiting = 1;
		ao2_unlock(bridge);
		winner = ast_waitfor_n(bridge->array, (int)bridge->array_num, &to);
		bridge->waiting = 0;
		ao2_lock(bridge);

		/* Process whatever they did */
		ast_bridge_handle_trip(bridge, NULL, winner, -1);
	}

	return 0;
}

/*! \brief Bridge thread function */
static void *bridge_thread(void *data)
{
	struct ast_bridge *bridge = data;
	int res = 0;

	ao2_lock(bridge);

	ast_debug(1, "Started bridge thread for %p\n", bridge);

	/* Loop around until we are told to stop */
	while (!bridge->stop && bridge->array_num && !res) {
		/* In case the refresh bit was set simply set it back to off */
		bridge->refresh = 0;

		ast_debug(1, "Launching bridge thread function %p for bridge %p\n", (bridge->technology->thread ? bridge->technology->thread : &generic_thread_loop), bridge);

		/* Execute the appropriate thread function. If the technology does not provide one we use the generic one */
		res = (bridge->technology->thread ? bridge->technology->thread(bridge) : generic_thread_loop(bridge));
	}

	ast_debug(1, "Ending bridge thread for %p\n", bridge);

	/* Indicate the bridge thread is no longer active */
	bridge->thread = AST_PTHREADT_NULL;
	ao2_unlock(bridge);

	ao2_ref(bridge, -1);

	return NULL;
}

/*! \brief Helper function used to find the "best" bridge technology given a specified capabilities */
static struct ast_bridge_technology *find_best_technology(uint32_t capabilities)
{
	struct ast_bridge_technology *current = NULL, *best = NULL;

	AST_RWLIST_RDLOCK(&bridge_technologies);
	AST_RWLIST_TRAVERSE(&bridge_technologies, current, entry) {
		if (current->suspended) {
			ast_debug(1, "Bridge technology %s is suspended. Skipping.\n", current->name);
			continue;
		}
		if (!(current->capabilities & capabilities)) {
			ast_debug(1, "Bridge technology %s does not have the capabilities we need.\n", current->name);
			continue;
		}
		if (best && best->preference < current->preference) {
			ast_debug(1, "Bridge technology %s has preference %d while %s has preference %d. Skipping.\n", current->name, current->preference, best->name, best->preference);
			continue;
		}
		best = current;
	}

	if (best) {
		/* Increment it's module reference count if present so it does not get unloaded while in use */
		if (best->mod) {
			ast_module_ref(best->mod);
		}
		ast_debug(1, "Chose bridge technology %s\n", best->name);
	}

	AST_RWLIST_UNLOCK(&bridge_technologies);

	return best;
}

static void destroy_bridge(void *obj)
{
	struct ast_bridge *bridge = obj;

	ast_debug(1, "Actually destroying bridge %p, nobody wants it anymore\n", bridge);

	/* Pass off the bridge to the technology to destroy if needed */
	if (bridge->technology->destroy) {
		ast_debug(1, "Giving bridge technology %s the bridge structure %p to destroy\n", bridge->technology->name, bridge);
		if (bridge->technology->destroy(bridge)) {
			ast_debug(1, "Bridge technology %s failed to destroy bridge structure %p... trying our best\n", bridge->technology->name, bridge);
		}
	}

	/* We are no longer using the bridge technology so decrement the module reference count on it */
	if (bridge->technology->mod) {
		ast_module_unref(bridge->technology->mod);
	}

	/* Last but not least clean up the features configuration */
	ast_bridge_features_cleanup(&bridge->features);

	/* Drop the array of channels */
	ast_free(bridge->array);

	cleanup_video_mode(bridge);

	return;
}

struct ast_bridge *ast_bridge_new(uint32_t capabilities, int flags)
{
	struct ast_bridge *bridge = NULL;
	struct ast_bridge_technology *bridge_technology = NULL;

	/* If we need to be a smart bridge see if we can move between 1to1 and multimix bridges */
	if (flags & AST_BRIDGE_FLAG_SMART) {
		struct ast_bridge *other_bridge;

		if (!(other_bridge = ast_bridge_new((capabilities & AST_BRIDGE_CAPABILITY_1TO1MIX) ? AST_BRIDGE_CAPABILITY_MULTIMIX : AST_BRIDGE_CAPABILITY_1TO1MIX, 0))) {
			return NULL;
		}

		ast_bridge_destroy(other_bridge);
	}

	/* If capabilities were provided use our helper function to find the "best" bridge technology, otherwise we can
	 * just look for the most basic capability needed, single 1to1 mixing. */
	bridge_technology = (capabilities ? find_best_technology(capabilities) : find_best_technology(AST_BRIDGE_CAPABILITY_1TO1MIX));

	/* If no bridge technology was found we can't possibly do bridging so fail creation of the bridge */
	if (!bridge_technology) {
		return NULL;
	}

	/* We have everything we need to create this bridge... so allocate the memory, link things together, and fire her up! */
	if (!(bridge = ao2_alloc(sizeof(*bridge), destroy_bridge))) {
		return NULL;
	}

	bridge->technology = bridge_technology;
	bridge->thread = AST_PTHREADT_NULL;

	/* Create an array of pointers for the channels that will be joining us */
	bridge->array = ast_calloc(BRIDGE_ARRAY_START, sizeof(struct ast_channel*));
	bridge->array_size = BRIDGE_ARRAY_START;

	ast_set_flag(&bridge->feature_flags, flags);

	/* Pass off the bridge to the technology to manipulate if needed */
	if (bridge->technology->create) {
		ast_debug(1, "Giving bridge technology %s the bridge structure %p to setup\n", bridge->technology->name, bridge);
		if (bridge->technology->create(bridge)) {
			ast_debug(1, "Bridge technology %s failed to setup bridge structure %p\n", bridge->technology->name, bridge);
			ao2_ref(bridge, -1);
			bridge = NULL;
		}
	}

	return bridge;
}

int ast_bridge_check(uint32_t capabilities)
{
	struct ast_bridge_technology *bridge_technology = NULL;

	if (!(bridge_technology = find_best_technology(capabilities))) {
		return 0;
	}

	ast_module_unref(bridge_technology->mod);

	return 1;
}

int ast_bridge_destroy(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel = NULL;

	ao2_lock(bridge);

	bridge->stop = 1;

	bridge_poke(bridge);

	ast_debug(1, "Telling all channels in bridge %p to end and leave the party\n", bridge);

	/* Drop every bridged channel, the last one will cause the bridge thread (if it exists) to exit */
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_END);
	}

	ao2_unlock(bridge);

	ao2_ref(bridge, -1);

	return 0;
}

static int bridge_make_compatible(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_format formats[2];
	ast_format_copy(&formats[0], &bridge_channel->chan->readformat);
	ast_format_copy(&formats[1], &bridge_channel->chan->writeformat);

	/* Are the formats currently in use something ths bridge can handle? */
	if (!ast_format_cap_iscompatible(bridge->technology->format_capabilities, &bridge_channel->chan->readformat)) {
		struct ast_format best_format;
		ast_best_codec(bridge->technology->format_capabilities, &best_format);

		/* Read format is a no go... */
		if (option_debug) {
			char codec_buf[512];
			ast_debug(1, "Bridge technology %s wants to read any of formats %s but channel has %s\n", bridge->technology->name,
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf), bridge->technology->format_capabilities),
				ast_getformatname(&formats[0]));
		}
		/* Switch read format to the best one chosen */
		if (ast_set_read_format(bridge_channel->chan, &best_format)) {
			ast_log(LOG_WARNING, "Failed to set channel %s to read format %s\n", bridge_channel->chan->name, ast_getformatname(&best_format));
			return -1;
		}
		ast_debug(1, "Bridge %p put channel %s into read format %s\n", bridge, bridge_channel->chan->name, ast_getformatname(&best_format));
	} else {
		ast_debug(1, "Bridge %p is happy that channel %s already has read format %s\n", bridge, bridge_channel->chan->name, ast_getformatname(&formats[0]));
	}

	if (!ast_format_cap_iscompatible(bridge->technology->format_capabilities, &formats[1])) {
		struct ast_format best_format;
		ast_best_codec(bridge->technology->format_capabilities, &best_format);

		/* Write format is a no go... */
		if (option_debug) {
			char codec_buf[512];
			ast_debug(1, "Bridge technology %s wants to write any of formats %s but channel has %s\n", bridge->technology->name,
				ast_getformatname_multiple(codec_buf, sizeof(codec_buf), bridge->technology->format_capabilities),
				ast_getformatname(&formats[1]));
		}
		/* Switch write format to the best one chosen */
		if (ast_set_write_format(bridge_channel->chan, &best_format)) {
			ast_log(LOG_WARNING, "Failed to set channel %s to write format %s\n", bridge_channel->chan->name, ast_getformatname(&best_format));
			return -1;
		}
		ast_debug(1, "Bridge %p put channel %s into write format %s\n", bridge, bridge_channel->chan->name, ast_getformatname(&best_format));
	} else {
		ast_debug(1, "Bridge %p is happy that channel %s already has write format %s\n", bridge, bridge_channel->chan->name, ast_getformatname(&formats[1]));
	}

	return 0;
}

/*! \brief Perform the smart bridge operation. Basically sees if a new bridge technology should be used instead of the current one. */
static int smart_bridge_operation(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, int count)
{
	uint32_t new_capabilities = 0;
	struct ast_bridge_technology *new_technology = NULL, *old_technology = bridge->technology;
	struct ast_bridge temp_bridge = {
		.technology = bridge->technology,
		.bridge_pvt = bridge->bridge_pvt,
	};
	struct ast_bridge_channel *bridge_channel2 = NULL;

	/* Based on current feature determine whether we want to change bridge technologies or not */
	if (bridge->technology->capabilities & AST_BRIDGE_CAPABILITY_1TO1MIX) {
		if (count <= 2) {
			ast_debug(1, "Bridge %p channel count (%d) is within limits for bridge technology %s, not performing smart bridge operation.\n", bridge, count, bridge->technology->name);
			return 0;
		}
		new_capabilities = AST_BRIDGE_CAPABILITY_MULTIMIX;
	} else if (bridge->technology->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX) {
		if (count > 2) {
			ast_debug(1, "Bridge %p channel count (%d) is within limits for bridge technology %s, not performing smart bridge operation.\n", bridge, count, bridge->technology->name);
			return 0;
		}
		new_capabilities = AST_BRIDGE_CAPABILITY_1TO1MIX;
	}

	if (!new_capabilities) {
		ast_debug(1, "Bridge '%p' has no new capabilities, not performing smart bridge operation.\n", bridge);
		return 0;
	}

	/* Attempt to find a new bridge technology to satisfy the capabilities */
	if (!(new_technology = find_best_technology(new_capabilities))) {
		return -1;
	}

	ast_debug(1, "Performing smart bridge operation on bridge %p, moving from bridge technology %s to %s\n", bridge, old_technology->name, new_technology->name);

	/* If a thread is currently executing for the current technology tell it to stop */
	if (bridge->thread != AST_PTHREADT_NULL) {
		/* If the new bridge technology also needs a thread simply tell the bridge thread to refresh itself. This has the benefit of not incurring the cost/time of tearing down and bringing up a new thread. */
		if (new_technology->capabilities & AST_BRIDGE_CAPABILITY_THREAD) {
			ast_debug(1, "Telling current bridge thread for bridge %p to refresh\n", bridge);
			bridge->refresh = 1;
			bridge_poke(bridge);
		} else {
			pthread_t bridge_thread = bridge->thread;
			ast_debug(1, "Telling current bridge thread for bridge %p to stop\n", bridge);
			bridge->stop = 1;
			bridge_poke(bridge);
			ao2_unlock(bridge);
			pthread_join(bridge_thread, NULL);
			ao2_lock(bridge);
		}
	}

	/* Since we are soon going to pass this bridge to a new technology we need to NULL out the bridge_pvt pointer but don't worry as it still exists in temp_bridge, ditto for the old technology */
	bridge->bridge_pvt = NULL;
	bridge->technology = new_technology;

	/* Pass the bridge to the new bridge technology so it can set it up */
	if (new_technology->create) {
		ast_debug(1, "Giving bridge technology %s the bridge structure %p to setup\n", new_technology->name, bridge);
		if (new_technology->create(bridge)) {
			ast_debug(1, "Bridge technology %s failed to setup bridge structure %p\n", new_technology->name, bridge);
		}
	}

	/* Move existing channels over to the new technology, while taking them away from the old one */
	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel2, entry) {
		/* Skip over channel that initiated the smart bridge operation */
		if (bridge_channel == bridge_channel2) {
			continue;
		}

		/* First we part them from the old technology */
		if (old_technology->leave) {
			ast_debug(1, "Giving bridge technology %s notification that %p is leaving bridge %p (really %p)\n", old_technology->name, bridge_channel2, &temp_bridge, bridge);
			if (old_technology->leave(&temp_bridge, bridge_channel2)) {
				ast_debug(1, "Bridge technology %s failed to allow %p (really %p) to leave bridge %p\n", old_technology->name, bridge_channel2, &temp_bridge, bridge);
			}
		}

		/* Second we make them compatible again with the bridge */
		bridge_make_compatible(bridge, bridge_channel2);

		/* Third we join them to the new technology */
		if (new_technology->join) {
			ast_debug(1, "Giving bridge technology %s notification that %p is joining bridge %p\n", new_technology->name, bridge_channel2, bridge);
			if (new_technology->join(bridge, bridge_channel2)) {
				ast_debug(1, "Bridge technology %s failed to join %p to bridge %p\n", new_technology->name, bridge_channel2, bridge);
			}
		}

		/* Fourth we tell them to wake up so they become aware that they above has happened */
		pthread_kill(bridge_channel2->thread, SIGURG);
		ao2_lock(bridge_channel2);
		ast_cond_signal(&bridge_channel2->cond);
		ao2_unlock(bridge_channel2);
	}

	/* Now that all the channels have been moved over we need to get rid of all the information the old technology may have left around */
	if (old_technology->destroy) {
		ast_debug(1, "Giving bridge technology %s the bridge structure %p (really %p) to destroy\n", old_technology->name, &temp_bridge, bridge);
		if (old_technology->destroy(&temp_bridge)) {
			ast_debug(1, "Bridge technology %s failed to destroy bridge structure %p (really %p)... some memory may have leaked\n", old_technology->name, &temp_bridge, bridge);
		}
	}

	/* Finally if the old technology has module referencing remove our reference, we are no longer going to use it */
	if (old_technology->mod) {
		ast_module_unref(old_technology->mod);
	}

	return 0;
}

/*! \brief Run in a multithreaded model. Each joined channel does writing/reading in their own thread. TODO: Improve */
static enum ast_bridge_channel_state bridge_channel_join_multithreaded(struct ast_bridge_channel *bridge_channel)
{
	int fds[4] = { -1, }, nfds = 0, i = 0, outfd = -1, ms = -1;
	struct ast_channel *chan = NULL;

	/* Add any file descriptors we may want to monitor */
	if (bridge_channel->bridge->technology->fd) {
		for (i = 0; i < 4; i ++) {
			if (bridge_channel->fds[i] >= 0) {
				fds[nfds++] = bridge_channel->fds[i];
			}
		}
	}

	ao2_unlock(bridge_channel->bridge);

	/* Wait for data to either come from the channel or us to be signalled */
	if (!bridge_channel->suspended) {
		ast_debug(10, "Going into a multithreaded waitfor for bridge channel %p of bridge %p\n", bridge_channel, bridge_channel->bridge);
		chan = ast_waitfor_nandfds(&bridge_channel->chan, 1, fds, nfds, NULL, &outfd, &ms);
	} else {
		ao2_lock(bridge_channel);
		ast_debug(10, "Going into a multithreaded signal wait for bridge channel %p of bridge %p\n", bridge_channel, bridge_channel->bridge);
		ast_cond_wait(&bridge_channel->cond, ao2_object_get_lockaddr(bridge_channel));
		ao2_unlock(bridge_channel);
	}

	ao2_lock(bridge_channel->bridge);

	if (!bridge_channel->suspended) {
		ast_bridge_handle_trip(bridge_channel->bridge, bridge_channel, chan, outfd);
	}

	return bridge_channel->state;
}

/*! \brief Run in a singlethreaded model. Each joined channel yields itself to the main bridge thread. TODO: Improve */
static enum ast_bridge_channel_state bridge_channel_join_singlethreaded(struct ast_bridge_channel *bridge_channel)
{
	ao2_unlock(bridge_channel->bridge);
	ao2_lock(bridge_channel);
	if (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		ast_debug(1, "Going into a single threaded signal wait for bridge channel %p of bridge %p\n", bridge_channel, bridge_channel->bridge);
		ast_cond_wait(&bridge_channel->cond, ao2_object_get_lockaddr(bridge_channel));
	}
	ao2_unlock(bridge_channel);
	ao2_lock(bridge_channel->bridge);

	return bridge_channel->state;
}

/*! \brief Internal function that suspends a channel from a bridge */
static void bridge_channel_suspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	bridge_channel->suspended = 1;

	bridge_array_remove(bridge, bridge_channel->chan);

	if (bridge->technology->suspend) {
		bridge->technology->suspend(bridge, bridge_channel);
	}

	return;
}

/*! \brief Internal function that unsuspends a channel from a bridge */
static void bridge_channel_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	bridge_channel->suspended =0;

	bridge_array_add(bridge, bridge_channel->chan);

	if (bridge->technology->unsuspend) {
		bridge->technology->unsuspend(bridge, bridge_channel);
	}

	return;
}

/*!
 * \brief Internal function that executes a feature on a bridge channel
 * \note Neither the bridge nor the bridge_channel locks should be held when entering
 * this function.
 */
static void bridge_channel_feature(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = (bridge_channel->features ? bridge_channel->features : &bridge->features);
	struct ast_bridge_features_hook *hook = NULL;
	char dtmf[MAXIMUM_DTMF_FEATURE_STRING] = "";
	int look_for_dtmf = 1, dtmf_len = 0;

	/* The channel is now under our control and we don't really want any begin frames to do our DTMF matching so disable 'em at the core level */
	ast_set_flag(bridge_channel->chan, AST_FLAG_END_DTMF_ONLY);

	/* Wait for DTMF on the channel and put it into a buffer. If the buffer matches any feature hook execute the hook. */
	while (look_for_dtmf) {
		int res = ast_waitfordigit(bridge_channel->chan, 3000);

		/* If the above timed out simply exit */
		if (!res) {
			ast_debug(1, "DTMF feature string collection on bridge channel %p timed out\n", bridge_channel);
			break;
		} else if (res < 0) {
			ast_debug(1, "DTMF feature string collection failed on bridge channel %p for some reason\n", bridge_channel);
			break;
		}

		/* Add the above DTMF into the DTMF string so we can do our matching */
		dtmf[dtmf_len++] = res;

		ast_debug(1, "DTMF feature string on bridge channel %p is now '%s'\n", bridge_channel, dtmf);

		/* Assume that we do not want to look for DTMF any longer */
		look_for_dtmf = 0;

		/* See if a DTMF feature hook matches or can match */
		AST_LIST_TRAVERSE(&features->hooks, hook, entry) {
			/* If this hook matches just break out now */
			if (!strcmp(hook->dtmf, dtmf)) {
				ast_debug(1, "DTMF feature hook %p matched DTMF string '%s' on bridge channel %p\n", hook, dtmf, bridge_channel);
				look_for_dtmf = 0;
				break;
			} else if (!strncmp(hook->dtmf, dtmf, dtmf_len)) {
				ast_debug(1, "DTMF feature hook %p can match DTMF string '%s', it wants '%s', on bridge channel %p\n", hook, dtmf, hook->dtmf, bridge_channel);
				look_for_dtmf = 1;
			} else {
				ast_debug(1, "DTMF feature hook %p does not match DTMF string '%s', it wants '%s', on bridge channel %p\n", hook, dtmf, hook->dtmf, bridge_channel);
			}
		}

		/* If we have reached the maximum length of a DTMF feature string bail out */
		if (dtmf_len == MAXIMUM_DTMF_FEATURE_STRING) {
			break;
		}
	}

	/* Since we are done bringing DTMF in return to using both begin and end frames */
	ast_clear_flag(bridge_channel->chan, AST_FLAG_END_DTMF_ONLY);

	/* If a hook was actually matched execute it on this channel, otherwise stream up the DTMF to the other channels */
	if (hook) {
		hook->callback(bridge, bridge_channel, hook->hook_pvt);
	} else {
		ast_bridge_dtmf_stream(bridge, dtmf, bridge_channel->chan);
	}

	/* if the channel is still in feature state, revert it back to wait state */
	if (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_FEATURE) {
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_WAIT);
	}

	return;
}

static void bridge_channel_talking(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_bridge_features *features = (bridge_channel->features ? bridge_channel->features : &bridge->features);

	if (features && features->talker_cb) {
		features->talker_cb(bridge, bridge_channel, features->talker_pvt_data);
	}
	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_WAIT);
}

/*! \brief Internal function that plays back DTMF on a bridge channel */
static void bridge_channel_dtmf_stream(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	char dtmf_q[8] = "";

	ast_copy_string(dtmf_q, bridge_channel->dtmf_stream_q, sizeof(dtmf_q));
	bridge_channel->dtmf_stream_q[0] = '\0';

	ast_debug(1, "Playing DTMF stream '%s' out to bridge channel %p\n", dtmf_q, bridge_channel);
	ast_dtmf_stream(bridge_channel->chan, NULL, dtmf_q, 250, 0);

	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_WAIT);

	return;
}

/*! \brief Join a channel to a bridge and handle anything the bridge may want us to do */
static enum ast_bridge_channel_state bridge_channel_join(struct ast_bridge_channel *bridge_channel)
{
	struct ast_format formats[2];
	enum ast_bridge_channel_state state;
	ast_format_copy(&formats[0], &bridge_channel->chan->readformat);
	ast_format_copy(&formats[1], &bridge_channel->chan->writeformat);

	/* Record the thread that will be the owner of us */
	bridge_channel->thread = pthread_self();

	ast_debug(1, "Joining bridge channel %p to bridge %p\n", bridge_channel, bridge_channel->bridge);

	ao2_lock(bridge_channel->bridge);

	state = bridge_channel->state;

	/* Add channel into the bridge */
	AST_LIST_INSERT_TAIL(&bridge_channel->bridge->channels, bridge_channel, entry);
	bridge_channel->bridge->num++;

	bridge_array_add(bridge_channel->bridge, bridge_channel->chan);

	if (bridge_channel->swap) {
		struct ast_bridge_channel *bridge_channel2 = NULL;

		/* If we are performing a swap operation we do not need
		 * to execute the smart bridge operation as the actual number
		 * of channels involved will not have changed, we just need to
		 * tell the other channel to leave */
		if ((bridge_channel2 = find_bridge_channel(bridge_channel->bridge, bridge_channel->swap))) {
			ast_debug(1, "Swapping bridge channel %p out from bridge %p so bridge channel %p can slip in\n", bridge_channel2, bridge_channel->bridge, bridge_channel);
			ast_bridge_change_state(bridge_channel2, AST_BRIDGE_CHANNEL_STATE_HANGUP);
		}

		bridge_channel->swap = NULL;
	} else if (ast_test_flag(&bridge_channel->bridge->feature_flags, AST_BRIDGE_FLAG_SMART)) {
		/* Perform the smart bridge operation, basically see if we need to move around between technologies */
		smart_bridge_operation(bridge_channel->bridge, bridge_channel, bridge_channel->bridge->num);
	}

	/* Make the channel compatible with the bridge */
	bridge_make_compatible(bridge_channel->bridge, bridge_channel);

	/* Tell the bridge technology we are joining so they set us up */
	if (bridge_channel->bridge->technology->join) {
		ast_debug(1, "Giving bridge technology %s notification that %p is joining bridge %p\n", bridge_channel->bridge->technology->name, bridge_channel, bridge_channel->bridge);
		if (bridge_channel->bridge->technology->join(bridge_channel->bridge, bridge_channel)) {
			ast_debug(1, "Bridge technology %s failed to join %p to bridge %p\n", bridge_channel->bridge->technology->name, bridge_channel, bridge_channel->bridge);
		}
	}

	/* Actually execute the respective threading model, and keep our bridge thread alive */
	while (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		/* Update bridge pointer on channel */
		bridge_channel->chan->bridge = bridge_channel->bridge;
		/* If the technology requires a thread and one is not running, start it up */
		if (bridge_channel->bridge->thread == AST_PTHREADT_NULL && (bridge_channel->bridge->technology->capabilities & AST_BRIDGE_CAPABILITY_THREAD)) {
			bridge_channel->bridge->stop = 0;
			ast_debug(1, "Starting a bridge thread for bridge %p\n", bridge_channel->bridge);
			ao2_ref(bridge_channel->bridge, +1);
			if (ast_pthread_create(&bridge_channel->bridge->thread, NULL, bridge_thread, bridge_channel->bridge)) {
				ast_debug(1, "Failed to create a bridge thread for bridge %p, giving it another go.\n", bridge_channel->bridge);
				ao2_ref(bridge_channel->bridge, -1);
				continue;
			}
		}
		/* Execute the threading model */
		state = (bridge_channel->bridge->technology->capabilities & AST_BRIDGE_CAPABILITY_MULTITHREADED ? bridge_channel_join_multithreaded(bridge_channel) : bridge_channel_join_singlethreaded(bridge_channel));
		/* Depending on the above state see what we need to do */
		switch (state) {
		case AST_BRIDGE_CHANNEL_STATE_FEATURE:
			bridge_channel_suspend(bridge_channel->bridge, bridge_channel);
			ao2_unlock(bridge_channel->bridge);
			bridge_channel_feature(bridge_channel->bridge, bridge_channel);
			ao2_lock(bridge_channel->bridge);
			bridge_channel_unsuspend(bridge_channel->bridge, bridge_channel);
			break;
		case AST_BRIDGE_CHANNEL_STATE_DTMF:
			bridge_channel_suspend(bridge_channel->bridge, bridge_channel);
			bridge_channel_dtmf_stream(bridge_channel->bridge, bridge_channel);
			bridge_channel_unsuspend(bridge_channel->bridge, bridge_channel);
			break;
		case AST_BRIDGE_CHANNEL_STATE_START_TALKING:
		case AST_BRIDGE_CHANNEL_STATE_STOP_TALKING:
			ao2_unlock(bridge_channel->bridge);
			bridge_channel_talking(bridge_channel->bridge, bridge_channel);
			ao2_lock(bridge_channel->bridge);
			break;
		default:
			break;
		}
	}

	bridge_channel->chan->bridge = NULL;

	/* See if we need to dissolve the bridge itself if they hung up */
	if (bridge_channel->state == AST_BRIDGE_CHANNEL_STATE_END) {
		bridge_check_dissolve(bridge_channel->bridge, bridge_channel);
	}

	/* Tell the bridge technology we are leaving so they tear us down */
	if (bridge_channel->bridge->technology->leave) {
		ast_debug(1, "Giving bridge technology %s notification that %p is leaving bridge %p\n", bridge_channel->bridge->technology->name, bridge_channel, bridge_channel->bridge);
		if (bridge_channel->bridge->technology->leave(bridge_channel->bridge, bridge_channel)) {
			ast_debug(1, "Bridge technology %s failed to leave %p from bridge %p\n", bridge_channel->bridge->technology->name, bridge_channel, bridge_channel->bridge);
		}
	}

	/* Remove channel from the bridge */
	bridge_channel->bridge->num--;
	AST_LIST_REMOVE(&bridge_channel->bridge->channels, bridge_channel, entry);

	bridge_array_remove(bridge_channel->bridge, bridge_channel->chan);

	/* Perform the smart bridge operation if needed since a channel has left */
	if (ast_test_flag(&bridge_channel->bridge->feature_flags, AST_BRIDGE_FLAG_SMART)) {
		smart_bridge_operation(bridge_channel->bridge, NULL, bridge_channel->bridge->num);
	}

	ao2_unlock(bridge_channel->bridge);

	/* Restore original formats of the channel as they came in */
	if (ast_format_cmp(&bridge_channel->chan->readformat, &formats[0]) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Bridge is returning %p to read format %s(%d)\n", bridge_channel, ast_getformatname(&formats[0]), formats[0].id);
		if (ast_set_read_format(bridge_channel->chan, &formats[0])) {
			ast_debug(1, "Bridge failed to return channel %p to read format %s(%d)\n", bridge_channel, ast_getformatname(&formats[0]), formats[0].id);
		}
	}
	if (ast_format_cmp(&bridge_channel->chan->writeformat, &formats[1]) == AST_FORMAT_CMP_NOT_EQUAL) {
		ast_debug(1, "Bridge is returning %p to write format %s(%d)\n", bridge_channel, ast_getformatname(&formats[1]), formats[1].id);
		if (ast_set_write_format(bridge_channel->chan, &formats[1])) {
			ast_debug(1, "Bridge failed to return channel %p to write format %s(%d)\n", bridge_channel, ast_getformatname(&formats[1]), formats[1].id);
		}
	}

	return bridge_channel->state;
}

static void bridge_channel_destroy(void *obj)
{
	struct ast_bridge_channel *bridge_channel = obj;

	if (bridge_channel->bridge) {
		ao2_ref(bridge_channel->bridge, -1);
		bridge_channel->bridge = NULL;
	}
	/* Destroy elements of the bridge channel structure and the bridge channel structure itself */
	ast_cond_destroy(&bridge_channel->cond);
}

static struct ast_bridge_channel *bridge_channel_alloc(struct ast_bridge *bridge)
{
	struct ast_bridge_channel *bridge_channel = ao2_alloc(sizeof(struct ast_bridge_channel), bridge_channel_destroy);
	if (!(bridge_channel)) {
		return NULL;
	}
	ast_cond_init(&bridge_channel->cond, NULL);
	if (bridge) {
		bridge_channel->bridge = bridge;
		ao2_ref(bridge_channel->bridge, +1);
	}
	return bridge_channel;
}

enum ast_bridge_channel_state ast_bridge_join(struct ast_bridge *bridge,
	struct ast_channel *chan,
	struct ast_channel *swap,
	struct ast_bridge_features *features,
	struct ast_bridge_tech_optimizations *tech_args)
{
	struct ast_bridge_channel *bridge_channel = bridge_channel_alloc(bridge);
	enum ast_bridge_channel_state state = AST_BRIDGE_CHANNEL_STATE_HANGUP;

	if (!bridge_channel) {
		return state;
	}
	if (tech_args) {
		memcpy(&bridge_channel->tech_args, tech_args, sizeof(bridge_channel->tech_args));
	}

	/* Initialize various other elements of the bridge channel structure that we can't do above */
	bridge_channel->chan = chan;
	bridge_channel->swap = swap;
	bridge_channel->features = features;

	state = bridge_channel_join(bridge_channel);

	/* Cleanup all the data in the bridge channel after it leaves the bridge. */
	ao2_lock(bridge_channel);
	bridge_channel->chan = NULL;
	bridge_channel->swap = NULL;
	bridge_channel->features = NULL;
	ao2_unlock(bridge_channel);

	ao2_ref(bridge_channel, -1);

	return state;
}

/*! \brief Thread responsible for imparted bridged channels */
static void *bridge_channel_thread(void *data)
{
	struct ast_bridge_channel *bridge_channel = data;
	enum ast_bridge_channel_state state;

	state = bridge_channel_join(bridge_channel);

	/* If no other thread is going to take the channel then hang it up, or else we would have to service it until something else came along */
	if (state == AST_BRIDGE_CHANNEL_STATE_END || state == AST_BRIDGE_CHANNEL_STATE_HANGUP) {
		ast_hangup(bridge_channel->chan);
	}

	/* cleanup */
	ao2_lock(bridge_channel);
	bridge_channel->chan = NULL;
	bridge_channel->swap = NULL;
	bridge_channel->features = NULL;
	ao2_unlock(bridge_channel);

	ao2_ref(bridge_channel, -1);

	return NULL;
}

int ast_bridge_impart(struct ast_bridge *bridge, struct ast_channel *chan, struct ast_channel *swap, struct ast_bridge_features *features)
{
	struct ast_bridge_channel *bridge_channel = bridge_channel_alloc(bridge);
	/* Try to allocate a structure for the bridge channel */
	if (!(bridge_channel)) {
		return -1;
	}

	/* Setup various parameters */
	bridge_channel->chan = chan;
	bridge_channel->swap = swap;
	bridge_channel->features = features;

	/* Actually create the thread that will handle the channel */
	if (ast_pthread_create(&bridge_channel->thread, NULL, bridge_channel_thread, bridge_channel)) {
		ao2_ref(bridge_channel, -1);
		return -1;
	}

	return 0;
}

int ast_bridge_depart(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel = NULL;
	pthread_t thread;

	ao2_lock(bridge);

	/* Try to find the channel that we want to depart */
	if (!(bridge_channel = find_bridge_channel(bridge, chan))) {
		ao2_unlock(bridge);
		return -1;
	}

	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_DEPART);
	thread = bridge_channel->thread;

	ao2_unlock(bridge);

	pthread_join(thread, NULL);

	return 0;
}

int ast_bridge_remove(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel = NULL;

	ao2_lock(bridge);

	/* Try to find the channel that we want to remove */
	if (!(bridge_channel = find_bridge_channel(bridge, chan))) {
		ao2_unlock(bridge);
		return -1;
	}

	ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_HANGUP);

	ao2_unlock(bridge);

	return 0;
}

int ast_bridge_merge(struct ast_bridge *bridge0, struct ast_bridge *bridge1)
{
	struct ast_bridge_channel *bridge_channel = NULL;

	ao2_lock(bridge0);
	ao2_lock(bridge1);

	/* If the first bridge currently has 2 channels and is not capable of becoming a multimixing bridge we can not merge */
	if ((bridge0->num + bridge1->num) > 2 && (!(bridge0->technology->capabilities & AST_BRIDGE_CAPABILITY_MULTIMIX) && !ast_test_flag(&bridge0->feature_flags, AST_BRIDGE_FLAG_SMART))) {
		ao2_unlock(bridge1);
		ao2_unlock(bridge0);
		ast_debug(1, "Can't merge bridge %p into bridge %p, multimix is needed and it could not be acquired.\n", bridge1, bridge0);
		return -1;
	}

	ast_debug(1, "Merging channels from bridge %p into bridge %p\n", bridge1, bridge0);

	/* Perform smart bridge operation on bridge we are merging into so it can change bridge technology if needed */
	if (smart_bridge_operation(bridge0, NULL, bridge0->num + bridge1->num)) {
		ao2_unlock(bridge1);
		ao2_unlock(bridge0);
		ast_debug(1, "Can't merge bridge %p into bridge %p, tried to perform smart bridge operation and failed.\n", bridge1, bridge0);
		return -1;
	}

	/* If a thread is currently executing on bridge1 tell it to stop */
	if (bridge1->thread) {
		ast_debug(1, "Telling bridge thread on bridge %p to stop as it is being merged into %p\n", bridge1, bridge0);
		bridge1->thread = AST_PTHREADT_STOP;
	}

	/* Move channels from bridge1 over to bridge0 */
	while ((bridge_channel = AST_LIST_REMOVE_HEAD(&bridge1->channels, entry))) {
		/* Tell the technology handling bridge1 that the bridge channel is leaving */
		if (bridge1->technology->leave) {
			ast_debug(1, "Giving bridge technology %s notification that %p is leaving bridge %p\n", bridge1->technology->name, bridge_channel, bridge1);
			if (bridge1->technology->leave(bridge1, bridge_channel)) {
				ast_debug(1, "Bridge technology %s failed to allow %p to leave bridge %p\n", bridge1->technology->name, bridge_channel, bridge1);
			}
		}

		/* Drop channel count and reference count on the bridge they are leaving */
		bridge1->num--;
		ao2_ref(bridge1, -1);

		bridge_array_remove(bridge1, bridge_channel->chan);

		/* Now add them into the bridge they are joining, increase channel count, and bump up reference count */
		bridge_channel->bridge = bridge0;
		AST_LIST_INSERT_TAIL(&bridge0->channels, bridge_channel, entry);
		bridge0->num++;
		ao2_ref(bridge0, +1);

		bridge_array_add(bridge0, bridge_channel->chan);

		/* Make the channel compatible with the new bridge it is joining or else formats would go amuck */
		bridge_make_compatible(bridge0, bridge_channel);

		/* Tell the technology handling bridge0 that the bridge channel is joining */
		if (bridge0->technology->join) {
			ast_debug(1, "Giving bridge technology %s notification that %p is joining bridge %p\n", bridge0->technology->name, bridge_channel, bridge0);
			if (bridge0->technology->join(bridge0, bridge_channel)) {
				ast_debug(1, "Bridge technology %s failed to join %p to bridge %p\n", bridge0->technology->name, bridge_channel, bridge0);
			}
		}

		/* Poke the bridge channel, this will cause it to wake up and execute the proper threading model for the new bridge it is in */
		pthread_kill(bridge_channel->thread, SIGURG);
		ao2_lock(bridge_channel);
		ast_cond_signal(&bridge_channel->cond);
		ao2_unlock(bridge_channel);
	}

	ast_debug(1, "Merged channels from bridge %p into bridge %p\n", bridge1, bridge0);

	ao2_unlock(bridge1);
	ao2_unlock(bridge0);

	return 0;
}

int ast_bridge_suspend(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;

	ao2_lock(bridge);

	if (!(bridge_channel = find_bridge_channel(bridge, chan))) {
		ao2_unlock(bridge);
		return -1;
	}

	bridge_channel_suspend(bridge, bridge_channel);

	ao2_unlock(bridge);

	return 0;
}

int ast_bridge_unsuspend(struct ast_bridge *bridge, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel;

	ao2_lock(bridge);

	if (!(bridge_channel = find_bridge_channel(bridge, chan))) {
		ao2_unlock(bridge);
		return -1;
	}

	bridge_channel_unsuspend(bridge, bridge_channel);

	ao2_unlock(bridge);

	return 0;
}

void ast_bridge_technology_suspend(struct ast_bridge_technology *technology)
{
	technology->suspended = 1;
	return;
}

void ast_bridge_technology_unsuspend(struct ast_bridge_technology *technology)
{
	technology->suspended = 0;
	return;
}

int ast_bridge_features_register(enum ast_bridge_builtin_feature feature, ast_bridge_features_hook_callback callback, const char *dtmf)
{
	if (builtin_features_handlers[feature]) {
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
	if (!builtin_features_handlers[feature]) {
		return -1;
	}

	builtin_features_handlers[feature] = NULL;

	return 0;
}

int ast_bridge_features_hook(struct ast_bridge_features *features,
	const char *dtmf,
	ast_bridge_features_hook_callback callback,
	void *hook_pvt,
	ast_bridge_features_hook_pvt_destructor destructor)
{
	struct ast_bridge_features_hook *hook = NULL;

	/* Allocate new memory and setup it's various variables */
	if (!(hook = ast_calloc(1, sizeof(*hook)))) {
		return -1;
	}

	ast_copy_string(hook->dtmf, dtmf, sizeof(hook->dtmf));
	hook->callback = callback;
	hook->destructor = destructor;
	hook->hook_pvt = hook_pvt;

	/* Once done we add it onto the list. Now it will be picked up when DTMF is used */
	AST_LIST_INSERT_TAIL(&features->hooks, hook, entry);

	features->usable = 1;

	return 0;
}

int ast_bridge_features_set_talk_detector(struct ast_bridge_features *features,
	ast_bridge_talking_indicate_callback talker_cb,
	ast_bridge_talking_indicate_destructor talker_destructor,
	void *pvt_data)
{
	features->talker_cb = talker_cb;
	features->talker_destructor_cb = talker_destructor;
	features->talker_pvt_data = pvt_data;
	return 0;
}

int ast_bridge_features_enable(struct ast_bridge_features *features, enum ast_bridge_builtin_feature feature, const char *dtmf, void *config)
{
	/* If no alternate DTMF stream was provided use the default one */
	if (ast_strlen_zero(dtmf)) {
		dtmf = builtin_features_dtmf[feature];
		/* If no DTMF is still available (ie: it has been disabled) then error out now */
		if (ast_strlen_zero(dtmf)) {
			ast_debug(1, "Failed to enable built in feature %d on %p, no DTMF string is available for it.\n", feature, features);
			return -1;
		}
	}

	if (!builtin_features_handlers[feature]) {
		return -1;
	}

	/* The rest is basically pretty easy. We create another hook using the built in feature's callback and DTMF, easy as pie. */
	return ast_bridge_features_hook(features, dtmf, builtin_features_handlers[feature], config, NULL);
}

int ast_bridge_features_set_flag(struct ast_bridge_features *features, enum ast_bridge_feature_flags flag)
{
	ast_set_flag(&features->feature_flags, flag);
	features->usable = 1;
	return 0;
}

int ast_bridge_features_init(struct ast_bridge_features *features)
{
	/* Zero out the structure */
	memset(features, 0, sizeof(*features));

	/* Initialize the hooks list, just in case */
	AST_LIST_HEAD_INIT_NOLOCK(&features->hooks);

	return 0;
}

int ast_bridge_features_cleanup(struct ast_bridge_features *features)
{
	struct ast_bridge_features_hook *hook = NULL;

	/* This is relatively simple, hooks are kept as a list on the features structure so we just pop them off and free them */
	while ((hook = AST_LIST_REMOVE_HEAD(&features->hooks, entry))) {
		if (hook->destructor) {
			hook->destructor(hook->hook_pvt);
		}
		ast_free(hook);
	}
	if (features->talker_destructor_cb && features->talker_pvt_data) {
		features->talker_destructor_cb(features->talker_pvt_data);
		features->talker_pvt_data = NULL;
	}

	return 0;
}

int ast_bridge_dtmf_stream(struct ast_bridge *bridge, const char *dtmf, struct ast_channel *chan)
{
	struct ast_bridge_channel *bridge_channel = NULL;

	ao2_lock(bridge);

	AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
		if (bridge_channel->chan == chan) {
			continue;
		}
		ast_copy_string(bridge_channel->dtmf_stream_q, dtmf, sizeof(bridge_channel->dtmf_stream_q));
		ast_bridge_change_state(bridge_channel, AST_BRIDGE_CHANNEL_STATE_DTMF);
	}

	ao2_unlock(bridge);

	return 0;
}

void ast_bridge_set_mixing_interval(struct ast_bridge *bridge, unsigned int mixing_interval)
{
	ao2_lock(bridge);
	bridge->internal_mixing_interval = mixing_interval;
	ao2_unlock(bridge);
}

void ast_bridge_set_internal_sample_rate(struct ast_bridge *bridge, unsigned int sample_rate)
{

	ao2_lock(bridge);
	bridge->internal_sample_rate = sample_rate;
	ao2_unlock(bridge);
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
	ao2_lock(bridge);
	cleanup_video_mode(bridge);
	bridge->video_mode.mode = AST_BRIDGE_VIDEO_MODE_SINGLE_SRC;
	bridge->video_mode.mode_data.single_src_data.chan_vsrc = ast_channel_ref(video_src_chan);
	ast_indicate(video_src_chan, AST_CONTROL_VIDUPDATE);
	ao2_unlock(bridge);
}

void ast_bridge_set_talker_src_video_mode(struct ast_bridge *bridge)
{
	ao2_lock(bridge);
	cleanup_video_mode(bridge);
	bridge->video_mode.mode = AST_BRIDGE_VIDEO_MODE_TALKER_SRC;
	ao2_unlock(bridge);
}

void ast_bridge_update_talker_src_video_mode(struct ast_bridge *bridge, struct ast_channel *chan, int talker_energy, int is_keyframe)
{
	struct ast_bridge_video_talker_src_data *data;
	/* If the channel doesn't support video, we don't care about it */
	if (!ast_format_cap_has_type(chan->nativeformats, AST_FORMAT_TYPE_VIDEO)) {
		return;
	}

	ao2_lock(bridge);
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
		ast_indicate(data->chan_vsrc, AST_CONTROL_VIDUPDATE);
	} else if ((data->average_talking_energy < talker_energy) && !is_keyframe) {
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	} else if (!data->chan_vsrc && is_keyframe) {
		data->chan_vsrc = ast_channel_ref(chan);
		data->average_talking_energy = talker_energy;
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	} else if (!data->chan_old_vsrc && is_keyframe) {
		data->chan_old_vsrc = ast_channel_ref(chan);
		ast_indicate(chan, AST_CONTROL_VIDUPDATE);
	}
	ao2_unlock(bridge);
}

int ast_bridge_number_video_src(struct ast_bridge *bridge)
{
	int res = 0;

	ao2_lock(bridge);
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
	ao2_unlock(bridge);
	return res;
}

int ast_bridge_is_video_src(struct ast_bridge *bridge, struct ast_channel *chan)
{
	int res = 0;

	ao2_lock(bridge);
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
	ao2_unlock(bridge);
	return res;
}

void ast_bridge_remove_video_src(struct ast_bridge *bridge, struct ast_channel *chan)
{
	ao2_lock(bridge);
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
	ao2_unlock(bridge);
}
