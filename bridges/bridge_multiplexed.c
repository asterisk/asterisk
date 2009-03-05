/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2008, Digium, Inc.
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
 * \brief Two channel bridging module which groups bridges into batches of threads
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \ingroup bridges
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_technology.h"
#include "asterisk/frame.h"
#include "asterisk/astobj2.h"

/*! \brief Number of buckets our multiplexed thread container can have */
#define MULTIPLEXED_BUCKETS 53

/*! \brief Number of channels we handle in a single thread */
#define MULTIPLEXED_MAX_CHANNELS 8

/*! \brief Structure which represents a single thread handling multiple 2 channel bridges */
struct multiplexed_thread {
	/*! Thread itself */
	pthread_t thread;
	/*! Pipe used to wake up the multiplexed thread */
	int pipe[2];
	/*! Channels in this thread */
	struct ast_channel *chans[MULTIPLEXED_MAX_CHANNELS];
	/*! Number of channels in this thread */
	unsigned int count;
	/*! Bit used to indicate that the thread is waiting on channels */
	unsigned int waiting:1;
	/*! Number of channels actually being serviced by this thread */
	unsigned int service_count;
};

/*! \brief Container of all operating multiplexed threads */
static struct ao2_container *multiplexed_threads;

/*! \brief Callback function for finding a free multiplexed thread */
static int find_multiplexed_thread(void *obj, void *arg, int flags)
{
	struct multiplexed_thread *multiplexed_thread = obj;
	return (multiplexed_thread->count <= (MULTIPLEXED_MAX_CHANNELS - 2)) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Destroy callback for a multiplexed thread structure */
static void destroy_multiplexed_thread(void *obj)
{
	struct multiplexed_thread *multiplexed_thread = obj;

	if (multiplexed_thread->pipe[0] > -1) {
		close(multiplexed_thread->pipe[0]);
	}
	if (multiplexed_thread->pipe[1] > -1) {
		close(multiplexed_thread->pipe[1]);
	}

	return;
}

/*! \brief Create function which finds/reserves/references a multiplexed thread structure */
static int multiplexed_bridge_create(struct ast_bridge *bridge)
{
	struct multiplexed_thread *multiplexed_thread;

	ao2_lock(multiplexed_threads);

	/* Try to find an existing thread to handle our additional channels */
	if (!(multiplexed_thread = ao2_callback(multiplexed_threads, 0, find_multiplexed_thread, NULL))) {
		int flags;

		/* If we failed we will have to create a new one from scratch */
		if (!(multiplexed_thread = ao2_alloc(sizeof(*multiplexed_thread), destroy_multiplexed_thread))) {
			ast_debug(1, "Failed to find or create a new multiplexed thread for bridge '%p'\n", bridge);
			ao2_unlock(multiplexed_threads);
			return -1;
		}

		multiplexed_thread->pipe[0] = multiplexed_thread->pipe[1] = -1;
		/* Setup a pipe so we can poke the thread itself when needed */
		if (pipe(multiplexed_thread->pipe)) {
			ast_debug(1, "Failed to create a pipe for poking a multiplexed thread for bridge '%p'\n", bridge);
			ao2_ref(multiplexed_thread, -1);
			ao2_unlock(multiplexed_threads);
			return -1;
		}

		/* Setup each pipe for non-blocking operation */
		flags = fcntl(multiplexed_thread->pipe[0], F_GETFL);
		if (fcntl(multiplexed_thread->pipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
			ast_log(LOG_WARNING, "Failed to setup first nudge pipe for non-blocking operation on %p (%d: %s)\n", bridge, errno, strerror(errno));
			ao2_ref(multiplexed_thread, -1);
			ao2_unlock(multiplexed_threads);
			return -1;
		}
		flags = fcntl(multiplexed_thread->pipe[1], F_GETFL);
		if (fcntl(multiplexed_thread->pipe[1], F_SETFL, flags | O_NONBLOCK) < 0) {
			ast_log(LOG_WARNING, "Failed to setup second nudge pipe for non-blocking operation on %p (%d: %s)\n", bridge, errno, strerror(errno));
			ao2_ref(multiplexed_thread, -1);
			ao2_unlock(multiplexed_threads);
			return -1;
		}

		/* Set up default parameters */
		multiplexed_thread->thread = AST_PTHREADT_NULL;

		/* Finally link us into the container so others may find us */
		ao2_link(multiplexed_threads, multiplexed_thread);
		ast_debug(1, "Created multiplexed thread '%p' for bridge '%p'\n", multiplexed_thread, bridge);
	} else {
		ast_debug(1, "Found multiplexed thread '%p' for bridge '%p'\n", multiplexed_thread, bridge);
	}

	/* Bump the count of the thread structure up by two since the channels for this bridge will be joining shortly */
	multiplexed_thread->count += 2;

	ao2_unlock(multiplexed_threads);

	bridge->bridge_pvt = multiplexed_thread;

	return 0;
}

/*! \brief Internal function which nudges the thread */
static void multiplexed_nudge(struct multiplexed_thread *multiplexed_thread)
{
	int nudge = 0;

	if (multiplexed_thread->thread == AST_PTHREADT_NULL) {
		return;
	}

	if (write(multiplexed_thread->pipe[1], &nudge, sizeof(nudge)) != sizeof(nudge)) {
		ast_log(LOG_ERROR, "We couldn't poke multiplexed thread '%p'... something is VERY wrong\n", multiplexed_thread);
	}

	while (multiplexed_thread->waiting) {
		sched_yield();
	}

	return;
}

/*! \brief Destroy function which unreserves/unreferences/removes a multiplexed thread structure */
static int multiplexed_bridge_destroy(struct ast_bridge *bridge)
{
	struct multiplexed_thread *multiplexed_thread = bridge->bridge_pvt;

	ao2_lock(multiplexed_threads);

	multiplexed_thread->count -= 2;

	if (!multiplexed_thread->count) {
		ast_debug(1, "Unlinking multiplexed thread '%p' since nobody is using it anymore\n", multiplexed_thread);
		ao2_unlink(multiplexed_threads, multiplexed_thread);
	}

	multiplexed_nudge(multiplexed_thread);

	ao2_unlock(multiplexed_threads);

	ao2_ref(multiplexed_thread, -1);

	return 0;
}

/*! \brief Thread function that executes for multiplexed threads */
static void *multiplexed_thread_function(void *data)
{
	struct multiplexed_thread *multiplexed_thread = data;
	int fds = multiplexed_thread->pipe[0];

	ao2_lock(multiplexed_thread);

	ast_debug(1, "Starting actual thread for multiplexed thread '%p'\n", multiplexed_thread);

	while (multiplexed_thread->thread != AST_PTHREADT_STOP) {
		struct ast_channel *winner = NULL, *first = multiplexed_thread->chans[0];
		int to = -1, outfd = -1;

		/* Move channels around so not just the first one gets priority */
		memmove(multiplexed_thread->chans, multiplexed_thread->chans + 1, sizeof(struct ast_channel *) * (multiplexed_thread->service_count - 1));
		multiplexed_thread->chans[multiplexed_thread->service_count - 1] = first;

		multiplexed_thread->waiting = 1;
		ao2_unlock(multiplexed_thread);
		winner = ast_waitfor_nandfds(multiplexed_thread->chans, multiplexed_thread->service_count, &fds, 1, NULL, &outfd, &to);
		multiplexed_thread->waiting = 0;
		ao2_lock(multiplexed_thread);

		if (outfd > -1) {
			int nudge;

			if (read(multiplexed_thread->pipe[0], &nudge, sizeof(nudge)) < 0) {
				if (errno != EINTR && errno != EAGAIN) {
					ast_log(LOG_WARNING, "read() failed for pipe on multiplexed thread '%p': %s\n", multiplexed_thread, strerror(errno));
				}
			}
		}
		if (winner && winner->bridge) {
			ast_bridge_handle_trip(winner->bridge, NULL, winner, -1);
		}
	}

	multiplexed_thread->thread = AST_PTHREADT_NULL;

	ast_debug(1, "Stopping actual thread for multiplexed thread '%p'\n", multiplexed_thread);

	ao2_unlock(multiplexed_thread);
	ao2_ref(multiplexed_thread, -1);

	return NULL;
}

/*! \brief Helper function which adds or removes a channel and nudges the thread */
static void multiplexed_add_or_remove(struct multiplexed_thread *multiplexed_thread, struct ast_channel *chan, int add)
{
	int i, removed = 0;
	pthread_t thread = AST_PTHREADT_NULL;

	ao2_lock(multiplexed_thread);

	multiplexed_nudge(multiplexed_thread);

	for (i = 0; i < MULTIPLEXED_MAX_CHANNELS; i++) {
		if (multiplexed_thread->chans[i] == chan) {
			if (!add) {
				multiplexed_thread->chans[i] = NULL;
				multiplexed_thread->service_count--;
				removed = 1;
			}
			break;
		} else if (!multiplexed_thread->chans[i] && add) {
			multiplexed_thread->chans[i] = chan;
			multiplexed_thread->service_count++;
			break;
		}
	}

	if (multiplexed_thread->service_count && multiplexed_thread->thread == AST_PTHREADT_NULL) {
		ao2_ref(multiplexed_thread, +1);
		if (ast_pthread_create(&multiplexed_thread->thread, NULL, multiplexed_thread_function, multiplexed_thread)) {
			ao2_ref(multiplexed_thread, -1);
			ast_debug(1, "Failed to create an actual thread for multiplexed thread '%p', trying next time\n", multiplexed_thread);
		}
	} else if (!multiplexed_thread->service_count && multiplexed_thread->thread != AST_PTHREADT_NULL) {
		thread = multiplexed_thread->thread;
		multiplexed_thread->thread = AST_PTHREADT_STOP;
	} else if (!add && removed) {
		memmove(multiplexed_thread->chans + i, multiplexed_thread->chans + i + 1, sizeof(struct ast_channel *) * (MULTIPLEXED_MAX_CHANNELS - (i + 1)));
	}

	ao2_unlock(multiplexed_thread);

	if (thread != AST_PTHREADT_NULL) {
		pthread_join(thread, NULL);
	}

	return;
}

/*! \brief Join function which actually adds the channel into the array to be monitored */
static int multiplexed_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_channel *c0 = AST_LIST_FIRST(&bridge->channels)->chan, *c1 = AST_LIST_LAST(&bridge->channels)->chan;
	struct multiplexed_thread *multiplexed_thread = bridge->bridge_pvt;

	ast_debug(1, "Adding channel '%s' to multiplexed thread '%p' for monitoring\n", bridge_channel->chan->name, multiplexed_thread);

	multiplexed_add_or_remove(multiplexed_thread, bridge_channel->chan, 1);

	/* If the second channel has not yet joined do not make things compatible */
	if (c0 == c1) {
		return 0;
	}

	if (((c0->writeformat == c1->readformat) && (c0->readformat == c1->writeformat) && (c0->nativeformats == c1->nativeformats))) {
		return 0;
	}

	return ast_channel_make_compatible(c0, c1);
}

/*! \brief Leave function which actually removes the channel from the array */
static int multiplexed_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct multiplexed_thread *multiplexed_thread = bridge->bridge_pvt;

	ast_debug(1, "Removing channel '%s' from multiplexed thread '%p'\n", bridge_channel->chan->name, multiplexed_thread);

	multiplexed_add_or_remove(multiplexed_thread, bridge_channel->chan, 0);

	return 0;
}

/*! \brief Suspend function which means control of the channel is going elsewhere */
static void multiplexed_bridge_suspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct multiplexed_thread *multiplexed_thread = bridge->bridge_pvt;

	ast_debug(1, "Suspending channel '%s' from multiplexed thread '%p'\n", bridge_channel->chan->name, multiplexed_thread);

	multiplexed_add_or_remove(multiplexed_thread, bridge_channel->chan, 0);

	return;
}

/*! \brief Unsuspend function which means control of the channel is coming back to us */
static void multiplexed_bridge_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct multiplexed_thread *multiplexed_thread = bridge->bridge_pvt;

	ast_debug(1, "Unsuspending channel '%s' from multiplexed thread '%p'\n", bridge_channel->chan->name, multiplexed_thread);

	multiplexed_add_or_remove(multiplexed_thread, bridge_channel->chan, 1);

	return;
}

/*! \brief Write function for writing frames into the bridge */
static enum ast_bridge_write_result multiplexed_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_channel *other;

	if (AST_LIST_FIRST(&bridge->channels) == AST_LIST_LAST(&bridge->channels)) {
		return AST_BRIDGE_WRITE_FAILED;
	}

	if (!(other = (AST_LIST_FIRST(&bridge->channels) == bridge_channel ? AST_LIST_LAST(&bridge->channels) : AST_LIST_FIRST(&bridge->channels)))) {
		return AST_BRIDGE_WRITE_FAILED;
	}

	if (other->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		ast_write(other->chan, frame);
	}

	return AST_BRIDGE_WRITE_SUCCESS;
}

static struct ast_bridge_technology multiplexed_bridge = {
	.name = "multiplexed_bridge",
	.capabilities = AST_BRIDGE_CAPABILITY_1TO1MIX,
	.preference = AST_BRIDGE_PREFERENCE_HIGH,
	.formats = AST_FORMAT_AUDIO_MASK | AST_FORMAT_VIDEO_MASK | AST_FORMAT_TEXT_MASK,
	.create = multiplexed_bridge_create,
	.destroy = multiplexed_bridge_destroy,
	.join = multiplexed_bridge_join,
	.leave = multiplexed_bridge_leave,
	.suspend = multiplexed_bridge_suspend,
	.unsuspend = multiplexed_bridge_unsuspend,
	.write = multiplexed_bridge_write,
};

static int unload_module(void)
{
	int res = ast_bridge_technology_unregister(&multiplexed_bridge);

	ao2_ref(multiplexed_threads, -1);

	return res;
}

static int load_module(void)
{
	if (!(multiplexed_threads = ao2_container_alloc(MULTIPLEXED_BUCKETS, NULL, NULL))) {
		return AST_MODULE_LOAD_DECLINE;
	}

	return ast_bridge_technology_register(&multiplexed_bridge);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Multiplexed two channel bridging module");
