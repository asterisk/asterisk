/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2007, Digium, Inc.
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
 * \brief Multi-party software based channel mixing
 *
 * \author Joshua Colp <jcolp@digium.com>
 *
 * \ingroup bridges
 *
 * \todo This bridge operates in 8 kHz mode unless a define is uncommented.
 * This needs to be improved so the bridge moves between the dominant codec as needed depending
 * on channels present in the bridge and transcoding capabilities.
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridging.h"
#include "asterisk/bridging_technology.h"
#include "asterisk/frame.h"
#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/slinfactory.h"
#include "asterisk/astobj2.h"
#include "asterisk/timing.h"

/*! \brief Interval at which mixing will take place. Valid options are 10, 20, and 40. */
#define SOFTMIX_INTERVAL 20

/*! \brief Size of the buffer used for sample manipulation */
#define SOFTMIX_DATALEN (160 * (SOFTMIX_INTERVAL / 10))

/*! \brief Number of samples we are dealing with */
#define SOFTMIX_SAMPLES (SOFTMIX_DATALEN / 2)

/*! \brief Define used to turn on 16 kHz audio support */
/* #define SOFTMIX_16_SUPPORT */

/*! \brief Structure which contains per-channel mixing information */
struct softmix_channel {
	/*! Lock to protect this structure */
	ast_mutex_t lock;
	/*! Factory which contains audio read in from the channel */
	struct ast_slinfactory factory;
	/*! Frame that contains mixed audio to be written out to the channel */
	struct ast_frame frame;
	/*! Bit used to indicate that the channel provided audio for this mixing interval */
	int have_audio:1;
	/*! Bit used to indicate that a frame is available to be written out to the channel */
	int have_frame:1;
	/*! Buffer containing final mixed audio from all sources */
	short final_buf[SOFTMIX_DATALEN];
	/*! Buffer containing only the audio from the channel */
	short our_buf[SOFTMIX_DATALEN];
};

/*! \brief Function called when a bridge is created */
static int softmix_bridge_create(struct ast_bridge *bridge)
{
	struct ast_timer *timer;

	if (!(timer = ast_timer_open())) {
		return -1;
	}

	bridge->bridge_pvt = timer;

	return 0;
}

/*! \brief Function called when a bridge is destroyed */
static int softmix_bridge_destroy(struct ast_bridge *bridge)
{
	if (!bridge->bridge_pvt) {
		return -1;
	}
	ast_timer_close((struct ast_timer *) bridge->bridge_pvt);

	return 0;
}

/*! \brief Function called when a channel is joined into the bridge */
static int softmix_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct softmix_channel *sc = NULL;

	/* Create a new softmix_channel structure and allocate various things on it */
	if (!(sc = ast_calloc(1, sizeof(*sc)))) {
		return -1;
	}

	/* Can't forget the lock */
	ast_mutex_init(&sc->lock);

	/* Setup smoother */
	ast_slinfactory_init(&sc->factory);

	/* Setup frame parameters */
	sc->frame.frametype = AST_FRAME_VOICE;
#ifdef SOFTMIX_16_SUPPORT
	sc->frame.subclass.codec = AST_FORMAT_SLINEAR16;
#else
	sc->frame.subclass.codec = AST_FORMAT_SLINEAR;
#endif
	sc->frame.data.ptr = sc->final_buf;
	sc->frame.datalen = SOFTMIX_DATALEN;
	sc->frame.samples = SOFTMIX_SAMPLES;

	/* Can't forget to record our pvt structure within the bridged channel structure */
	bridge_channel->bridge_pvt = sc;

	return 0;
}

/*! \brief Function called when a channel leaves the bridge */
static int softmix_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct softmix_channel *sc = bridge_channel->bridge_pvt;

	/* Drop mutex lock */
	ast_mutex_destroy(&sc->lock);

	/* Drop the factory */
	ast_slinfactory_destroy(&sc->factory);

	/* Eep! drop ourselves */
	ast_free(sc);

	return 0;
}

/*! \brief Function called when a channel writes a frame into the bridge */
static enum ast_bridge_write_result softmix_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct softmix_channel *sc = bridge_channel->bridge_pvt;

	/* Only accept audio frames, all others are unsupported */
	if (frame->frametype != AST_FRAME_VOICE) {
		return AST_BRIDGE_WRITE_UNSUPPORTED;
	}

	ast_mutex_lock(&sc->lock);

	/* If a frame was provided add it to the smoother */
#ifdef SOFTMIX_16_SUPPORT
	if (frame->frametype == AST_FRAME_VOICE && frame->subclass.codec == AST_FORMAT_SLINEAR16) {
#else
	if (frame->frametype == AST_FRAME_VOICE && frame->subclass.codec == AST_FORMAT_SLINEAR) {
#endif
		ast_slinfactory_feed(&sc->factory, frame);
	}

	/* If a frame is ready to be written out, do so */
	if (sc->have_frame) {
		ast_write(bridge_channel->chan, &sc->frame);
		sc->have_frame = 0;
	}

	/* Alllll done */
	ast_mutex_unlock(&sc->lock);

	return AST_BRIDGE_WRITE_SUCCESS;
}

/*! \brief Function called when the channel's thread is poked */
static int softmix_bridge_poke(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct softmix_channel *sc = bridge_channel->bridge_pvt;

	ast_mutex_lock(&sc->lock);

	if (sc->have_frame) {
		ast_write(bridge_channel->chan, &sc->frame);
		sc->have_frame = 0;
	}

	ast_mutex_unlock(&sc->lock);

	return 0;
}

/*! \brief Function which acts as the mixing thread */
static int softmix_bridge_thread(struct ast_bridge *bridge)
{
	struct ast_timer *timer = (struct ast_timer *) bridge->bridge_pvt;
	int timingfd = ast_timer_fd(timer);

	ast_timer_set_rate(timer, (1000 / SOFTMIX_INTERVAL));

	while (!bridge->stop && !bridge->refresh && bridge->array_num) {
		struct ast_bridge_channel *bridge_channel = NULL;
		short buf[SOFTMIX_DATALEN] = {0, };
		int timeout = -1;

		/* Go through pulling audio from each factory that has it available */
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			struct softmix_channel *sc = bridge_channel->bridge_pvt;

			ast_mutex_lock(&sc->lock);

			/* Try to get audio from the factory if available */
			if (ast_slinfactory_available(&sc->factory) >= SOFTMIX_SAMPLES && ast_slinfactory_read(&sc->factory, sc->our_buf, SOFTMIX_SAMPLES)) {
				short *data1, *data2;
				int i;

				/* Put into the local final buffer */
				for (i = 0, data1 = buf, data2 = sc->our_buf; i < SOFTMIX_DATALEN; i++, data1++, data2++)
					ast_slinear_saturated_add(data1, data2);
				/* Yay we have our own audio */
				sc->have_audio = 1;
			} else {
				/* Awww we don't have audio ;( */
				sc->have_audio = 0;
			}
			ast_mutex_unlock(&sc->lock);
		}

		/* Next step go through removing the channel's own audio and creating a good frame... */
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			struct softmix_channel *sc = bridge_channel->bridge_pvt;
			int i = 0;

			/* Copy from local final buffer to our final buffer */
			memcpy(sc->final_buf, buf, sizeof(sc->final_buf));

			/* If we provided audio then take it out */
			if (sc->have_audio) {
				for (i = 0; i < SOFTMIX_DATALEN; i++) {
					ast_slinear_saturated_subtract(&sc->final_buf[i], &sc->our_buf[i]);
				}
			}

			/* The frame is now ready for use... */
			sc->have_frame = 1;

			/* Poke bridged channel thread just in case */
			pthread_kill(bridge_channel->thread, SIGURG);
		}

		ao2_unlock(bridge);

		/* Wait for the timing source to tell us to wake up and get things done */
		ast_waitfor_n_fd(&timingfd, 1, &timeout, NULL);

		ast_timer_ack(timer, 1);

		ao2_lock(bridge);
	}

	return 0;
}

static struct ast_bridge_technology softmix_bridge = {
	.name = "softmix",
	.capabilities = AST_BRIDGE_CAPABILITY_MULTIMIX | AST_BRIDGE_CAPABILITY_THREAD | AST_BRIDGE_CAPABILITY_MULTITHREADED,
	.preference = AST_BRIDGE_PREFERENCE_LOW,
#ifdef SOFTMIX_16_SUPPORT
	.formats = AST_FORMAT_SLINEAR16,
#else
	.formats = AST_FORMAT_SLINEAR,
#endif
	.create = softmix_bridge_create,
	.destroy = softmix_bridge_destroy,
	.join = softmix_bridge_join,
	.leave = softmix_bridge_leave,
	.write = softmix_bridge_write,
	.thread = softmix_bridge_thread,
	.poke = softmix_bridge_poke,
};

static int unload_module(void)
{
	return ast_bridge_technology_unregister(&softmix_bridge);
}

static int load_module(void)
{
	return ast_bridge_technology_register(&softmix_bridge);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Multi-party software based channel mixing");
