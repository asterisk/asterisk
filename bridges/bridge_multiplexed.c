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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

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

/*! \brief Number of bridges we handle in a single thread */
#define MULTIPLEXED_MAX_BRIDGES		4

/*! \brief Structure which represents a single thread handling multiple 2 channel bridges */
struct multiplexed_thread {
	/*! Thread itself */
	pthread_t thread;
	/*! Channels serviced by this thread */
	struct ast_channel *chans[2 * MULTIPLEXED_MAX_BRIDGES];
	/*! Pipe used to wake up the multiplexed thread */
	int pipe[2];
	/*! Number of channels actually being serviced by this thread */
	unsigned int service_count;
	/*! Number of bridges in this thread */
	unsigned int bridges;
	/*! TRUE if the thread is waiting on channels */
	unsigned int waiting:1;
};

/*! \brief Container of all operating multiplexed threads */
static struct ao2_container *muxed_threads;

/*! \brief Callback function for finding a free multiplexed thread */
static int find_multiplexed_thread(void *obj, void *arg, int flags)
{
	struct multiplexed_thread *muxed_thread = obj;

	return (muxed_thread->bridges < MULTIPLEXED_MAX_BRIDGES) ? CMP_MATCH | CMP_STOP : 0;
}

/*! \brief Destroy callback for a multiplexed thread structure */
static void destroy_multiplexed_thread(void *obj)
{
	struct multiplexed_thread *muxed_thread = obj;

	if (muxed_thread->pipe[0] > -1) {
		close(muxed_thread->pipe[0]);
	}
	if (muxed_thread->pipe[1] > -1) {
		close(muxed_thread->pipe[1]);
	}
}

/*! \brief Create function which finds/reserves/references a multiplexed thread structure */
static int multiplexed_bridge_create(struct ast_bridge *bridge)
{
	struct multiplexed_thread *muxed_thread;

	ao2_lock(muxed_threads);

	/* Try to find an existing thread to handle our additional channels */
	muxed_thread = ao2_callback(muxed_threads, 0, find_multiplexed_thread, NULL);
	if (!muxed_thread) {
		int flags;

		/* If we failed we will have to create a new one from scratch */
		muxed_thread = ao2_alloc(sizeof(*muxed_thread), destroy_multiplexed_thread);
		if (!muxed_thread) {
			ast_debug(1, "Failed to find or create a new multiplexed thread for bridge '%p'\n", bridge);
			ao2_unlock(muxed_threads);
			return -1;
		}

		muxed_thread->pipe[0] = muxed_thread->pipe[1] = -1;
		/* Setup a pipe so we can poke the thread itself when needed */
		if (pipe(muxed_thread->pipe)) {
			ast_debug(1, "Failed to create a pipe for poking a multiplexed thread for bridge '%p'\n", bridge);
			ao2_ref(muxed_thread, -1);
			ao2_unlock(muxed_threads);
			return -1;
		}

		/* Setup each pipe for non-blocking operation */
		flags = fcntl(muxed_thread->pipe[0], F_GETFL);
		if (fcntl(muxed_thread->pipe[0], F_SETFL, flags | O_NONBLOCK) < 0) {
			ast_log(LOG_WARNING, "Failed to setup first nudge pipe for non-blocking operation on %p (%d: %s)\n", bridge, errno, strerror(errno));
			ao2_ref(muxed_thread, -1);
			ao2_unlock(muxed_threads);
			return -1;
		}
		flags = fcntl(muxed_thread->pipe[1], F_GETFL);
		if (fcntl(muxed_thread->pipe[1], F_SETFL, flags | O_NONBLOCK) < 0) {
			ast_log(LOG_WARNING, "Failed to setup second nudge pipe for non-blocking operation on %p (%d: %s)\n", bridge, errno, strerror(errno));
			ao2_ref(muxed_thread, -1);
			ao2_unlock(muxed_threads);
			return -1;
		}

		/* Set up default parameters */
		muxed_thread->thread = AST_PTHREADT_NULL;

		/* Finally link us into the container so others may find us */
		ao2_link(muxed_threads, muxed_thread);
		ast_debug(1, "Created multiplexed thread '%p' for bridge '%p'\n", muxed_thread, bridge);
	} else {
		ast_debug(1, "Found multiplexed thread '%p' for bridge '%p'\n", muxed_thread, bridge);
	}

	/* Increase the number of bridges using this multiplexed bridge */
	++muxed_thread->bridges;

	ao2_unlock(muxed_threads);

	bridge->bridge_pvt = muxed_thread;

	return 0;
}

/*!
 * \internal
 * \brief Nudges the multiplex thread.
 * \since 12.0.0
 *
 * \param muxed_thread Controller to poke the thread.
 *
 * \note This function assumes the muxed_thread is locked.
 *
 * \return Nothing
 */
static void multiplexed_nudge(struct multiplexed_thread *muxed_thread)
{
	int nudge = 0;

	if (muxed_thread->thread == AST_PTHREADT_NULL) {
		return;
	}

	if (write(muxed_thread->pipe[1], &nudge, sizeof(nudge)) != sizeof(nudge)) {
		ast_log(LOG_ERROR, "We couldn't poke multiplexed thread '%p'... something is VERY wrong\n", muxed_thread);
	}

	while (muxed_thread->waiting) {
		sched_yield();
	}
}

/*! \brief Destroy function which unreserves/unreferences/removes a multiplexed thread structure */
static int multiplexed_bridge_destroy(struct ast_bridge *bridge)
{
	struct multiplexed_thread *muxed_thread;
	pthread_t thread;

	muxed_thread = bridge->bridge_pvt;
	if (!muxed_thread) {
		return -1;
	}
	bridge->bridge_pvt = NULL;

	ao2_lock(muxed_threads);

	if (--muxed_thread->bridges) {
		/* Other bridges are still using the multiplexed thread. */
		ao2_unlock(muxed_threads);
	} else {
		ast_debug(1, "Unlinking multiplexed thread '%p' since nobody is using it anymore\n",
			muxed_thread);
		ao2_unlink(muxed_threads, muxed_thread);
		ao2_unlock(muxed_threads);

		/* Stop the multiplexed bridge thread. */
		ao2_lock(muxed_thread);
		multiplexed_nudge(muxed_thread);
		thread = muxed_thread->thread;
		muxed_thread->thread = AST_PTHREADT_STOP;
		ao2_unlock(muxed_thread);

		if (thread != AST_PTHREADT_NULL) {
			/* Wait for multiplexed bridge thread to die. */
			pthread_join(thread, NULL);
		}
	}

	ao2_ref(muxed_thread, -1);
	return 0;
}

/*! \brief Thread function that executes for multiplexed threads */
static void *multiplexed_thread_function(void *data)
{
	struct multiplexed_thread *muxed_thread = data;
	int fds = muxed_thread->pipe[0];

	ast_debug(1, "Starting actual thread for multiplexed thread '%p'\n", muxed_thread);

	ao2_lock(muxed_thread);

	while (muxed_thread->thread != AST_PTHREADT_STOP) {
		struct ast_channel *winner;
		int to = -1;
		int outfd = -1;

		if (1 < muxed_thread->service_count) {
			struct ast_channel *first;

			/* Move channels around so not just the first one gets priority */
			first = muxed_thread->chans[0];
			memmove(muxed_thread->chans, muxed_thread->chans + 1,
				sizeof(struct ast_channel *) * (muxed_thread->service_count - 1));
			muxed_thread->chans[muxed_thread->service_count - 1] = first;
		}

		muxed_thread->waiting = 1;
		ao2_unlock(muxed_thread);
		winner = ast_waitfor_nandfds(muxed_thread->chans, muxed_thread->service_count, &fds, 1, NULL, &outfd, &to);
		muxed_thread->waiting = 0;
		ao2_lock(muxed_thread);
		if (muxed_thread->thread == AST_PTHREADT_STOP) {
			break;
		}

		if (outfd > -1) {
			int nudge;

			if (read(muxed_thread->pipe[0], &nudge, sizeof(nudge)) < 0) {
				if (errno != EINTR && errno != EAGAIN) {
					ast_log(LOG_WARNING, "read() failed for pipe on multiplexed thread '%p': %s\n", muxed_thread, strerror(errno));
				}
			}
		}
		if (winner && ast_channel_internal_bridge(winner)) {
			struct ast_bridge *bridge;
			int stop = 0;

			ao2_unlock(muxed_thread);
			while ((bridge = ast_channel_internal_bridge(winner)) && ao2_trylock(bridge)) {
				sched_yield();
				if (muxed_thread->thread == AST_PTHREADT_STOP) {
					stop = 1;
					break;
				}
			}
			if (!stop && bridge) {
				ast_bridge_handle_trip(bridge, NULL, winner, -1);
				ao2_unlock(bridge);
			}
			ao2_lock(muxed_thread);
		}
	}

	ao2_unlock(muxed_thread);

	ast_debug(1, "Stopping actual thread for multiplexed thread '%p'\n", muxed_thread);
	ao2_ref(muxed_thread, -1);

	return NULL;
}

/*!
 * \internal
 * \brief Check to see if the multiplexed bridge thread needs to be started.
 * \since 12.0.0
 *
 * \param muxed_thread Controller to check if need to start thread.
 *
 * \note This function assumes the muxed_thread is locked.
 *
 * \return Nothing
 */
static void multiplexed_thread_start(struct multiplexed_thread *muxed_thread)
{
	if (muxed_thread->service_count && muxed_thread->thread == AST_PTHREADT_NULL) {
		ao2_ref(muxed_thread, +1);
		if (ast_pthread_create(&muxed_thread->thread, NULL, multiplexed_thread_function, muxed_thread)) {
			muxed_thread->thread = AST_PTHREADT_NULL;/* For paranoia's sake. */
			ao2_ref(muxed_thread, -1);
			ast_log(LOG_WARNING, "Failed to create the common thread for multiplexed thread '%p', trying next time\n",
				muxed_thread);
		}
	}
}

/*!
 * \internal
 * \brief Add a channel to the multiplexed bridge.
 * \since 12.0.0
 *
 * \param muxed_thread Controller to add a channel.
 * \param chan Channel to add to the channel service array.
 *
 * \return Nothing
 */
static void multiplexed_chan_add(struct multiplexed_thread *muxed_thread, struct ast_channel *chan)
{
	int idx;

	ao2_lock(muxed_thread);

	multiplexed_nudge(muxed_thread);

	/* Check if already in the channel service array for safety. */
	for (idx = 0; idx < muxed_thread->service_count; ++idx) {
		if (muxed_thread->chans[idx] == chan) {
			break;
		}
	}
	if (idx == muxed_thread->service_count) {
		/* Channel to add was not already in the array. */
		if (muxed_thread->service_count < ARRAY_LEN(muxed_thread->chans)) {
			muxed_thread->chans[muxed_thread->service_count++] = chan;
		} else {
			ast_log(LOG_ERROR, "Could not add channel %s to multiplexed thread %p.  Array not large enough.\n",
				ast_channel_name(chan), muxed_thread);
			ast_assert(0);
		}
	}

	multiplexed_thread_start(muxed_thread);

	ao2_unlock(muxed_thread);
}

/*!
 * \internal
 * \brief Remove a channel from the multiplexed bridge.
 * \since 12.0.0
 *
 * \param muxed_thread Controller to remove a channel.
 * \param chan Channel to remove from the channel service array.
 *
 * \return Nothing
 */
static void multiplexed_chan_remove(struct multiplexed_thread *muxed_thread, struct ast_channel *chan)
{
	int idx;

	ao2_lock(muxed_thread);

	multiplexed_nudge(muxed_thread);

	/* Remove channel from service array. */
	for (idx = 0; idx < muxed_thread->service_count; ++idx) {
		if (muxed_thread->chans[idx] != chan) {
			continue;
		}
		muxed_thread->chans[idx] = muxed_thread->chans[--muxed_thread->service_count];
		break;
	}

	multiplexed_thread_start(muxed_thread);

	ao2_unlock(muxed_thread);
}

/*! \brief Join function which actually adds the channel into the array to be monitored */
static int multiplexed_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct ast_channel *c0 = AST_LIST_FIRST(&bridge->channels)->chan;
	struct ast_channel *c1 = AST_LIST_LAST(&bridge->channels)->chan;
	struct multiplexed_thread *muxed_thread = bridge->bridge_pvt;

	ast_debug(1, "Adding channel '%s' to multiplexed thread '%p' for monitoring\n", ast_channel_name(bridge_channel->chan), muxed_thread);

	multiplexed_chan_add(muxed_thread, bridge_channel->chan);

	/* If the second channel has not yet joined do not make things compatible */
	if (c0 == c1) {
		return 0;
	}

	if ((ast_format_cmp(ast_channel_writeformat(c0), ast_channel_readformat(c1)) == AST_FORMAT_CMP_EQUAL) &&
		(ast_format_cmp(ast_channel_readformat(c0), ast_channel_writeformat(c1)) == AST_FORMAT_CMP_EQUAL) &&
		(ast_format_cap_identical(ast_channel_nativeformats(c0), ast_channel_nativeformats(c1)))) {
		return 0;
	}

	return ast_channel_make_compatible(c0, c1);
}

/*! \brief Leave function which actually removes the channel from the array */
static int multiplexed_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct multiplexed_thread *muxed_thread = bridge->bridge_pvt;

	ast_debug(1, "Removing channel '%s' from multiplexed thread '%p'\n", ast_channel_name(bridge_channel->chan), muxed_thread);

	multiplexed_chan_remove(muxed_thread, bridge_channel->chan);

	return 0;
}

/*! \brief Suspend function which means control of the channel is going elsewhere */
static void multiplexed_bridge_suspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct multiplexed_thread *muxed_thread = bridge->bridge_pvt;

	ast_debug(1, "Suspending channel '%s' from multiplexed thread '%p'\n", ast_channel_name(bridge_channel->chan), muxed_thread);

	multiplexed_chan_remove(muxed_thread, bridge_channel->chan);
}

/*! \brief Unsuspend function which means control of the channel is coming back to us */
static void multiplexed_bridge_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct multiplexed_thread *muxed_thread = bridge->bridge_pvt;

	ast_debug(1, "Unsuspending channel '%s' from multiplexed thread '%p'\n", ast_channel_name(bridge_channel->chan), muxed_thread);

	multiplexed_chan_add(muxed_thread, bridge_channel->chan);
}

/*! \brief Write function for writing frames into the bridge */
static enum ast_bridge_write_result multiplexed_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_bridge_channel *other;

	/* If this is the only channel in this bridge then immediately exit */
	if (AST_LIST_FIRST(&bridge->channels) == AST_LIST_LAST(&bridge->channels)) {
		return AST_BRIDGE_WRITE_FAILED;
	}

	/* Find the channel we actually want to write to */
	if (!(other = (AST_LIST_FIRST(&bridge->channels) == bridge_channel ? AST_LIST_LAST(&bridge->channels) : AST_LIST_FIRST(&bridge->channels)))) {
		return AST_BRIDGE_WRITE_FAILED;
	}

	/* Write the frame out if they are in the waiting state... don't worry about freeing it, the bridging core will take care of it */
	if (other->state == AST_BRIDGE_CHANNEL_STATE_WAIT) {
		ast_write(other->chan, frame);
	}

	return AST_BRIDGE_WRITE_SUCCESS;
}

static struct ast_bridge_technology multiplexed_bridge = {
	.name = "multiplexed_bridge",
	.capabilities = AST_BRIDGE_CAPABILITY_1TO1MIX,
	.preference = AST_BRIDGE_PREFERENCE_HIGH,
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

	ao2_ref(muxed_threads, -1);
	multiplexed_bridge.format_capabilities = ast_format_cap_destroy(multiplexed_bridge.format_capabilities);

	return res;
}

static int load_module(void)
{
	if (!(muxed_threads = ao2_container_alloc(MULTIPLEXED_BUCKETS, NULL, NULL))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	if (!(multiplexed_bridge.format_capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_add_all_by_type(multiplexed_bridge.format_capabilities, AST_FORMAT_TYPE_AUDIO);
	ast_format_cap_add_all_by_type(multiplexed_bridge.format_capabilities, AST_FORMAT_TYPE_VIDEO);
	ast_format_cap_add_all_by_type(multiplexed_bridge.format_capabilities, AST_FORMAT_TYPE_TEXT);
	return ast_bridge_technology_register(&multiplexed_bridge);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Multiplexed two channel bridging module");
