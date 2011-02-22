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

#define MAX_DATALEN 3840

/*! \brief Interval at which mixing will take place. Valid options are 10, 20, and 40. */
#define SOFTMIX_INTERVAL 20

/*! \brief Size of the buffer used for sample manipulation */
#define SOFTMIX_DATALEN(rate) ((rate/50) * (SOFTMIX_INTERVAL / 10))

/*! \brief Number of samples we are dealing with */
#define SOFTMIX_SAMPLES(rate) (SOFTMIX_DATALEN(rate) / 2)

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
	short final_buf[MAX_DATALEN];
	/*! Buffer containing only the audio from the channel */
	short our_buf[MAX_DATALEN];
};

struct softmix_bridge_data {
	struct ast_timer *timer;
	unsigned int internal_rate;
};

/*! \brief Function called when a bridge is created */
static int softmix_bridge_create(struct ast_bridge *bridge)
{
	struct softmix_bridge_data *bridge_data;

	if (!(bridge_data = ast_calloc(1, sizeof(*bridge_data)))) {
		return -1;
	}
	if (!(bridge_data->timer = ast_timer_open())) {
		ast_free(bridge_data);
		return -1;
	}

	/* start at 8khz, let it grow from there */
	bridge_data->internal_rate = 8000;

	bridge->bridge_pvt = bridge_data;
	return 0;
}

/*! \brief Function called when a bridge is destroyed */
static int softmix_bridge_destroy(struct ast_bridge *bridge)
{
	struct softmix_bridge_data *bridge_data = bridge->bridge_pvt;
	if (!bridge->bridge_pvt) {
		return -1;
	}
	ast_timer_close(bridge_data->timer);
	ast_free(bridge_data);
	return 0;
}

static void set_softmix_bridge_data(int rate, struct ast_bridge_channel *bridge_channel, int reset)
{
	struct softmix_channel *sc = bridge_channel->bridge_pvt;
	if (reset) {
		ast_slinfactory_destroy(&sc->factory);
	}
	/* Setup frame parameters */
	sc->frame.frametype = AST_FRAME_VOICE;

	ast_format_set(&sc->frame.subclass.format, ast_format_slin_by_rate(rate), 0);
	sc->frame.data.ptr = sc->final_buf;
	sc->frame.datalen = SOFTMIX_DATALEN(rate);
	sc->frame.samples = SOFTMIX_SAMPLES(rate);

	/* Setup smoother */
	ast_slinfactory_init_with_format(&sc->factory, &sc->frame.subclass.format);

	ast_set_read_format(bridge_channel->chan, &sc->frame.subclass.format);
	ast_set_write_format(bridge_channel->chan, &sc->frame.subclass.format);
}

/*! \brief Function called when a channel is joined into the bridge */
static int softmix_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct softmix_channel *sc = NULL;
	struct softmix_bridge_data *bridge_data = bridge->bridge_pvt;

	/* Create a new softmix_channel structure and allocate various things on it */
	if (!(sc = ast_calloc(1, sizeof(*sc)))) {
		return -1;
	}

	/* Can't forget the lock */
	ast_mutex_init(&sc->lock);

	/* Can't forget to record our pvt structure within the bridged channel structure */
	bridge_channel->bridge_pvt = sc;

	set_softmix_bridge_data(bridge_data->internal_rate, bridge_channel, 0);

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
	if (frame->frametype == AST_FRAME_VOICE && ast_format_is_slinear(&frame->subclass.format)) {
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
	struct {
		/*! Each index represents a sample rate used above the internal rate. */
		unsigned int sample_rates[8];
		/*! Each index represents the number of channels using the same index in the sample_rates array.  */
		unsigned int num_channels[8];
		/*! the number of channels above the internal sample rate */
		unsigned int num_above_internal_rate;
		/*! the number of channels at the internal sample rate */
		unsigned int num_at_internal_rate;
		/*! the absolute highest sample rate supported by any channel in the bridge */
		unsigned int highest_supported_rate;
	} stats;
	struct softmix_bridge_data *bridge_data = bridge->bridge_pvt;
	struct ast_timer *timer = bridge_data->timer;
	int timingfd = ast_timer_fd(timer);
	int update_all_rates = 0; /* set this when the internal sample rate has changed */
	int i;

	ast_timer_set_rate(timer, (1000 / SOFTMIX_INTERVAL));

	while (!bridge->stop && !bridge->refresh && bridge->array_num) {
		struct ast_bridge_channel *bridge_channel = NULL;
		short buf[MAX_DATALEN] = {0, };
		int timeout = -1;

		/* these variables help determine if a rate change is required */
		memset(&stats, 0, sizeof(stats));
		stats.highest_supported_rate = 8000;

		/* Go through pulling audio from each factory that has it available */
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			struct softmix_channel *sc = bridge_channel->bridge_pvt;
			int channel_native_rate;

			ast_mutex_lock(&sc->lock);

			if (update_all_rates) {
				set_softmix_bridge_data(bridge_data->internal_rate, bridge_channel, 1);
			}

			/* Try to get audio from the factory if available */
			if (ast_slinfactory_available(&sc->factory) >= SOFTMIX_SAMPLES(bridge_data->internal_rate) &&
				ast_slinfactory_read(&sc->factory, sc->our_buf, SOFTMIX_SAMPLES(bridge_data->internal_rate))) {
				short *data1, *data2;
				int i;

				/* Put into the local final buffer */
				for (i = 0, data1 = buf, data2 = sc->our_buf; i < SOFTMIX_DATALEN(bridge_data->internal_rate); i++, data1++, data2++)
					ast_slinear_saturated_add(data1, data2);
				/* Yay we have our own audio */
				sc->have_audio = 1;
			} else {
				/* Awww we don't have audio ;( */
				sc->have_audio = 0;
			}

			/* Gather stats about channel sample rates.  */
			channel_native_rate = MAX(ast_format_rate(&bridge_channel->chan->rawwriteformat),
				ast_format_rate(&bridge_channel->chan->rawreadformat));

			if (channel_native_rate > stats.highest_supported_rate) {
				stats.highest_supported_rate = channel_native_rate;
			}
			if (channel_native_rate > bridge_data->internal_rate) {
				for (i = 0; i < ARRAY_LEN(stats.sample_rates); i++) {
					if (stats.sample_rates[i] == channel_native_rate) {
						stats.num_channels[i]++;
						break;
					} else if (!stats.sample_rates[i]) {
						stats.sample_rates[i] = channel_native_rate;
						stats.num_channels[i]++;
						break;
					}
				}
				stats.num_above_internal_rate++;
			} else if (channel_native_rate == bridge_data->internal_rate) {
				stats.num_at_internal_rate++;
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
				for (i = 0; i < SOFTMIX_DATALEN(bridge_data->internal_rate); i++) {
					ast_slinear_saturated_subtract(&sc->final_buf[i], &sc->our_buf[i]);
				}
			}

			/* The frame is now ready for use... */
			sc->have_frame = 1;

			/* Poke bridged channel thread just in case */
			pthread_kill(bridge_channel->thread, SIGURG);
		}

		/* Re-adjust the internal bridge sample rate if
		 * 1. two or more channels support a higher sample rate
		 * 2. no channels support the current sample rate or a higher rate
		 */
		if (stats.num_above_internal_rate >= 2) {
			/* the highest rate is just used as a starting point */
			unsigned int best_rate = stats.highest_supported_rate;
			int best_index = -1;

			/* 1. pick the best sample rate two or more channels support
			 * 2. if two or more channels do not support the same rate, pick the
			 * lowest sample rate that is still above the internal rate. */
			for (i = 0; ((i < ARRAY_LEN(stats.num_channels)) && stats.num_channels[i]); i++) {
				if ((stats.num_channels[i] >= 2 && (best_index == -1)) ||
					((best_index != -1) &&
					(stats.num_channels[i] >= 2) &&
					(stats.sample_rates[best_index] < stats.sample_rates[i]))) {

					best_rate = stats.sample_rates[i];
					best_index = i;
				} else if (best_index == -1) {
					best_rate = MIN(best_rate, stats.sample_rates[i]);
				}
			}

			ast_debug(1, " Bridge changed from %d To %d\n", bridge_data->internal_rate, best_rate);
			bridge_data->internal_rate = best_rate;
			update_all_rates = 1;
		} else if (!stats.num_at_internal_rate && !stats.num_above_internal_rate) {
			update_all_rates = 1;
			/* in this case, the highest supported rate is actually lower than the internal rate */
			bridge_data->internal_rate = stats.highest_supported_rate;
			ast_debug(1, " Bridge changed from %d to %d\n", bridge_data->internal_rate, stats.highest_supported_rate);
			update_all_rates = 1;
		} else {
			update_all_rates = 0;
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
	ast_format_cap_destroy(softmix_bridge.format_capabilities);
	return ast_bridge_technology_unregister(&softmix_bridge);
}

static int load_module(void)
{
	struct ast_format tmp;
	if (!(softmix_bridge.format_capabilities = ast_format_cap_alloc())) {
		return AST_MODULE_LOAD_DECLINE;
	}
#ifdef SOFTMIX_16_SUPPORT
	ast_format_cap_add(softmix_bridge.format_capabilities, ast_format_set(&tmp, AST_FORMAT_SLINEAR16, 0));
#else
	ast_format_cap_add(softmix_bridge.format_capabilities, ast_format_set(&tmp, AST_FORMAT_SLINEAR, 0));
#endif
	return ast_bridge_technology_register(&softmix_bridge);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Multi-party software based channel mixing");
