/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
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
 * \brief Audiohooks Architecture
 *
 * \author Joshua Colp <jcolp@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/audiohook.h"
#include "asterisk/slinfactory.h"
#include "asterisk/frame.h"
#include "asterisk/translate.h"

struct ast_audiohook_translate {
	struct ast_trans_pvt *trans_pvt;
	int format;
};

struct ast_audiohook_list {
	struct ast_audiohook_translate in_translate[2];
	struct ast_audiohook_translate out_translate[2];
	AST_LIST_HEAD_NOLOCK(, ast_audiohook) spy_list;
	AST_LIST_HEAD_NOLOCK(, ast_audiohook) whisper_list;
	AST_LIST_HEAD_NOLOCK(, ast_audiohook) manipulate_list;
};

/*! \brief Initialize an audiohook structure
 * \param audiohook Audiohook structure
 * \param type
 * \param source
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_init(struct ast_audiohook *audiohook, enum ast_audiohook_type type, const char *source)
{
	/* Need to keep the type and source */
	audiohook->type = type;
	audiohook->source = source;

	/* Initialize lock that protects our audiohook */
	ast_mutex_init(&audiohook->lock);
	ast_cond_init(&audiohook->trigger, NULL);

	/* Setup the factories that are needed for this audiohook type */
	switch (type) {
	case AST_AUDIOHOOK_TYPE_SPY:
		ast_slinfactory_init(&audiohook->read_factory);
	case AST_AUDIOHOOK_TYPE_WHISPER:
		ast_slinfactory_init(&audiohook->write_factory);
		break;
	default:
		break;
	}

	/* Since we are just starting out... this audiohook is new */
	audiohook->status = AST_AUDIOHOOK_STATUS_NEW;

	return 0;
}

/*! \brief Destroys an audiohook structure
 * \param audiohook Audiohook structure
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_destroy(struct ast_audiohook *audiohook)
{
	/* Drop the factories used by this audiohook type */
	switch (audiohook->type) {
	case AST_AUDIOHOOK_TYPE_SPY:
		ast_slinfactory_destroy(&audiohook->read_factory);
	case AST_AUDIOHOOK_TYPE_WHISPER:
		ast_slinfactory_destroy(&audiohook->write_factory);
		break;
	default:
		break;
	}

	/* Destroy translation path if present */
	if (audiohook->trans_pvt)
		ast_translator_free_path(audiohook->trans_pvt);

	/* Lock and trigger be gone! */
	ast_cond_destroy(&audiohook->trigger);
	ast_mutex_destroy(&audiohook->lock);

	return 0;
}

/*! \brief Writes a frame into the audiohook structure
 * \param audiohook Audiohook structure
 * \param direction Direction the audio frame came from
 * \param frame Frame to write in
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_write_frame(struct ast_audiohook *audiohook, enum ast_audiohook_direction direction, struct ast_frame *frame)
{
	struct ast_slinfactory *factory = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook->read_factory : &audiohook->write_factory);
	struct ast_slinfactory *other_factory = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook->write_factory : &audiohook->read_factory);
	struct timeval *time = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook->read_time : &audiohook->write_time), previous_time = *time;
	int our_factory_samples;
	int our_factory_ms;
	int other_factory_samples;
	int other_factory_ms;

	/* Update last feeding time to be current */
	*time = ast_tvnow();

	our_factory_samples = ast_slinfactory_available(factory);
	our_factory_ms = ast_tvdiff_ms(*time, previous_time) + (our_factory_samples / 8);
	other_factory_samples = ast_slinfactory_available(other_factory);
	other_factory_ms = other_factory_samples / 8;

	/* If we are using a sync trigger and this factory suddenly got audio fed in after a lapse, then flush both factories to ensure they remain in sync */
	if (ast_test_flag(audiohook, AST_AUDIOHOOK_TRIGGER_SYNC) && other_factory_samples && (our_factory_ms - other_factory_ms > AST_AUDIOHOOK_SYNC_TOLERANCE)) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Flushing audiohook %p so it remains in sync\n", audiohook);
		ast_slinfactory_flush(factory);
		ast_slinfactory_flush(other_factory);
	}

	if (ast_test_flag(audiohook, AST_AUDIOHOOK_SMALL_QUEUE) && (our_factory_samples > 640 || other_factory_samples > 640)) {
		if (option_debug) {
			ast_log(LOG_DEBUG, "Audiohook %p has stale audio in its factories. Flushing them both\n", audiohook);
		}
		ast_slinfactory_flush(factory);
		ast_slinfactory_flush(other_factory);
	}

	/* Write frame out to respective factory */
	ast_slinfactory_feed(factory, frame);

	/* If we need to notify the respective handler of this audiohook, do so */
	if ((ast_test_flag(audiohook, AST_AUDIOHOOK_TRIGGER_MODE) == AST_AUDIOHOOK_TRIGGER_READ) && (direction == AST_AUDIOHOOK_DIRECTION_READ)) {
		ast_cond_signal(&audiohook->trigger);
	} else if ((ast_test_flag(audiohook, AST_AUDIOHOOK_TRIGGER_MODE) == AST_AUDIOHOOK_TRIGGER_WRITE) && (direction == AST_AUDIOHOOK_DIRECTION_WRITE)) {
		ast_cond_signal(&audiohook->trigger);
	} else if (ast_test_flag(audiohook, AST_AUDIOHOOK_TRIGGER_SYNC)) {
		ast_cond_signal(&audiohook->trigger);
	}

	return 0;
}

static struct ast_frame *audiohook_read_frame_single(struct ast_audiohook *audiohook, size_t samples, enum ast_audiohook_direction direction)
{
	struct ast_slinfactory *factory = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook->read_factory : &audiohook->write_factory);
	int vol = (direction == AST_AUDIOHOOK_DIRECTION_READ ? audiohook->options.read_volume : audiohook->options.write_volume);
	short buf[samples];
	struct ast_frame frame = {
		.frametype = AST_FRAME_VOICE,
		.subclass = AST_FORMAT_SLINEAR,
		.data = buf,
		.datalen = sizeof(buf),
		.samples = samples,
	};

	/* Ensure the factory is able to give us the samples we want */
	if (samples > ast_slinfactory_available(factory))
		return NULL;
	
	/* Read data in from factory */
	if (!ast_slinfactory_read(factory, buf, samples))
		return NULL;

	/* If a volume adjustment needs to be applied apply it */
	if (vol)
		ast_frame_adjust_volume(&frame, vol);

	return ast_frdup(&frame);
}

static struct ast_frame *audiohook_read_frame_both(struct ast_audiohook *audiohook, size_t samples)
{
	int i = 0, usable_read, usable_write;
	short buf1[samples], buf2[samples], *read_buf = NULL, *write_buf = NULL, *final_buf = NULL, *data1 = NULL, *data2 = NULL;
	struct ast_frame frame = {
		.frametype = AST_FRAME_VOICE,
		.subclass = AST_FORMAT_SLINEAR,
		.data = NULL,
		.datalen = sizeof(buf1),
		.samples = samples,
	};

	/* Make sure both factories have the required samples */
	usable_read = (ast_slinfactory_available(&audiohook->read_factory) >= samples ? 1 : 0);
	usable_write = (ast_slinfactory_available(&audiohook->write_factory) >= samples ? 1 : 0);

	if (!usable_read && !usable_write) {
		/* If both factories are unusable bail out */
		if (option_debug)
			ast_log(LOG_DEBUG, "Read factory %p and write factory %p both fail to provide %zd samples\n", &audiohook->read_factory, &audiohook->write_factory, samples);
		return NULL;
	}

	/* If we want to provide only a read factory make sure we aren't waiting for other audio */
	if (usable_read && !usable_write && (ast_tvdiff_ms(ast_tvnow(), audiohook->write_time) < (samples/8)*2)) {
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Write factory %p was pretty quick last time, waiting for them.\n", &audiohook->write_factory);
		return NULL;
	}

	/* If we want to provide only a write factory make sure we aren't waiting for other audio */
	if (usable_write && !usable_read && (ast_tvdiff_ms(ast_tvnow(), audiohook->read_time) < (samples/8)*2)) {
		if (option_debug > 2)
			ast_log(LOG_DEBUG, "Read factory %p was pretty quick last time, waiting for them.\n", &audiohook->read_factory);
		return NULL;
	}

	/* Start with the read factory... if there are enough samples, read them in */
	if (usable_read) {
		if (ast_slinfactory_read(&audiohook->read_factory, buf1, samples)) {
			read_buf = buf1;
			/* Adjust read volume if need be */
			if (audiohook->options.read_volume) {
				int count = 0;
				short adjust_value = abs(audiohook->options.read_volume);
				for (count = 0; count < samples; count++) {
					if (audiohook->options.read_volume > 0)
						ast_slinear_saturated_multiply(&buf1[count], &adjust_value);
					else if (audiohook->options.read_volume < 0)
						ast_slinear_saturated_divide(&buf1[count], &adjust_value);
				}
			}
		}
	} else if (option_debug)
		ast_log(LOG_DEBUG, "Failed to get %zd samples from read factory %p\n", samples, &audiohook->read_factory);

	/* Move on to the write factory... if there are enough samples, read them in */
	if (usable_write) {
		if (ast_slinfactory_read(&audiohook->write_factory, buf2, samples)) {
			write_buf = buf2;
			/* Adjust write volume if need be */
			if (audiohook->options.write_volume) {
				int count = 0;
				short adjust_value = abs(audiohook->options.write_volume);
				for (count = 0; count < samples; count++) {
					if (audiohook->options.write_volume > 0)
						ast_slinear_saturated_multiply(&buf2[count], &adjust_value);
					else if (audiohook->options.write_volume < 0)
						ast_slinear_saturated_divide(&buf2[count], &adjust_value);
				}
			}
		}
	} else if (option_debug)
		ast_log(LOG_DEBUG, "Failed to get %zd samples from write factory %p\n", samples, &audiohook->write_factory);

	/* Basically we figure out which buffer to use... and if mixing can be done here */
	if (!read_buf && !write_buf)
		return NULL;
	else if (read_buf && write_buf) {
		for (i = 0, data1 = read_buf, data2 = write_buf; i < samples; i++, data1++, data2++)
			ast_slinear_saturated_add(data1, data2);
		final_buf = buf1;
	} else if (read_buf)
		final_buf = buf1;
	else if (write_buf)
		final_buf = buf2;

	/* Make the final buffer part of the frame, so it gets duplicated fine */
	frame.data = final_buf;

	/* Yahoo, a combined copy of the audio! */
	return ast_frdup(&frame);
}

/*! \brief Reads a frame in from the audiohook structure
 * \param audiohook Audiohook structure
 * \param samples Number of samples wanted
 * \param direction Direction the audio frame came from
 * \param format Format of frame remote side wants back
 * \return Returns frame on success, NULL on failure
 */
struct ast_frame *ast_audiohook_read_frame(struct ast_audiohook *audiohook, size_t samples, enum ast_audiohook_direction direction, int format)
{
	struct ast_frame *read_frame = NULL, *final_frame = NULL;

	if (!(read_frame = (direction == AST_AUDIOHOOK_DIRECTION_BOTH ? audiohook_read_frame_both(audiohook, samples) : audiohook_read_frame_single(audiohook, samples, direction))))
		return NULL;

	/* If they don't want signed linear back out, we'll have to send it through the translation path */
	if (format != AST_FORMAT_SLINEAR) {
		/* Rebuild translation path if different format then previously */
		if (audiohook->format != format) {
			if (audiohook->trans_pvt) {
				ast_translator_free_path(audiohook->trans_pvt);
				audiohook->trans_pvt = NULL;
			}
			/* Setup new translation path for this format... if we fail we can't very well return signed linear so free the frame and return nothing */
			if (!(audiohook->trans_pvt = ast_translator_build_path(format, AST_FORMAT_SLINEAR))) {
				ast_frfree(read_frame);
				return NULL;
			}
		}
		/* Convert to requested format, and allow the read in frame to be freed */
		final_frame = ast_translate(audiohook->trans_pvt, read_frame, 1);
	} else {
		final_frame = read_frame;
	}

	return final_frame;
}

/*! \brief Attach audiohook to channel
 * \param chan Channel
 * \param audiohook Audiohook structure
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_attach(struct ast_channel *chan, struct ast_audiohook *audiohook)
{
	ast_channel_lock(chan);

	if (!chan->audiohooks) {
		/* Whoops... allocate a new structure */
		if (!(chan->audiohooks = ast_calloc(1, sizeof(*chan->audiohooks)))) {
			ast_channel_unlock(chan);
			return -1;
		}
		AST_LIST_HEAD_INIT_NOLOCK(&chan->audiohooks->spy_list);
		AST_LIST_HEAD_INIT_NOLOCK(&chan->audiohooks->whisper_list);
		AST_LIST_HEAD_INIT_NOLOCK(&chan->audiohooks->manipulate_list);
	}

	/* Drop into respective list */
	if (audiohook->type == AST_AUDIOHOOK_TYPE_SPY)
		AST_LIST_INSERT_TAIL(&chan->audiohooks->spy_list, audiohook, list);
	else if (audiohook->type == AST_AUDIOHOOK_TYPE_WHISPER)
		AST_LIST_INSERT_TAIL(&chan->audiohooks->whisper_list, audiohook, list);
	else if (audiohook->type == AST_AUDIOHOOK_TYPE_MANIPULATE)
		AST_LIST_INSERT_TAIL(&chan->audiohooks->manipulate_list, audiohook, list);

	/* Change status over to running since it is now attached */
	audiohook->status = AST_AUDIOHOOK_STATUS_RUNNING;

	ast_channel_unlock(chan);

	return 0;
}

/*! \brief Detach audiohook from channel
 * \param audiohook Audiohook structure
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_detach(struct ast_audiohook *audiohook)
{
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE)
		return 0;

	audiohook->status = AST_AUDIOHOOK_STATUS_SHUTDOWN;

	while (audiohook->status != AST_AUDIOHOOK_STATUS_DONE)
		ast_audiohook_trigger_wait(audiohook);

	return 0;
}

/*! \brief Detach audiohooks from list and destroy said list
 * \param audiohook_list List of audiohooks
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_detach_list(struct ast_audiohook_list *audiohook_list)
{
	int i = 0;
	struct ast_audiohook *audiohook = NULL;

	/* Drop any spies */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->spy_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		AST_LIST_REMOVE_CURRENT(&audiohook_list->spy_list, list);
		audiohook->status = AST_AUDIOHOOK_STATUS_DONE;
		ast_cond_signal(&audiohook->trigger);
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END

	/* Drop any whispering sources */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->whisper_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		AST_LIST_REMOVE_CURRENT(&audiohook_list->whisper_list, list);
		audiohook->status = AST_AUDIOHOOK_STATUS_DONE;
		ast_cond_signal(&audiohook->trigger);
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END

	/* Drop any manipulaters */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->manipulate_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		AST_LIST_REMOVE_CURRENT(&audiohook_list->manipulate_list, list);
		audiohook->status = AST_AUDIOHOOK_STATUS_DONE;
		ast_audiohook_unlock(audiohook);
		audiohook->manipulate_callback(audiohook, NULL, NULL, 0);
	}
	AST_LIST_TRAVERSE_SAFE_END

	/* Drop translation paths if present */
	for (i = 0; i < 2; i++) {
		if (audiohook_list->in_translate[i].trans_pvt)
			ast_translator_free_path(audiohook_list->in_translate[i].trans_pvt);
		if (audiohook_list->out_translate[i].trans_pvt)
			ast_translator_free_path(audiohook_list->out_translate[i].trans_pvt);
	}
	
	/* Free ourselves */
	ast_free(audiohook_list);

	return 0;
}

static struct ast_audiohook *find_audiohook_by_source(struct ast_audiohook_list *audiohook_list, const char *source)
{
	struct ast_audiohook *audiohook = NULL;

	AST_LIST_TRAVERSE(&audiohook_list->spy_list, audiohook, list) {
		if (!strcasecmp(audiohook->source, source))
			return audiohook;
	}

	AST_LIST_TRAVERSE(&audiohook_list->whisper_list, audiohook, list) {
		if (!strcasecmp(audiohook->source, source))
			return audiohook;
	}

	AST_LIST_TRAVERSE(&audiohook_list->manipulate_list, audiohook, list) {
		if (!strcasecmp(audiohook->source, source))
			return audiohook;
	}

	return NULL;
}

void ast_audiohook_move_by_source (struct ast_channel *old_chan, struct ast_channel *new_chan, const char *source)
{
	struct ast_audiohook *audiohook;

	if (!old_chan->audiohooks || !(audiohook = find_audiohook_by_source(old_chan->audiohooks, source))) {
		return;
	}

	/* By locking both channels and the audiohook, we can assure that
	 * another thread will not have a chance to read the audiohook's status
	 * as done, even though ast_audiohook_remove signals the trigger
	 * condition
	 */
	ast_audiohook_lock(audiohook);
	ast_audiohook_remove(old_chan, audiohook);
	ast_audiohook_attach(new_chan, audiohook);
	ast_audiohook_unlock(audiohook);
}

/*! \brief Detach specified source audiohook from channel
 * \param chan Channel to detach from
 * \param source Name of source to detach
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_detach_source(struct ast_channel *chan, const char *source)
{
	struct ast_audiohook *audiohook = NULL;

	ast_channel_lock(chan);

	/* Ensure the channel has audiohooks on it */
	if (!chan->audiohooks) {
		ast_channel_unlock(chan);
		return -1;
	}

	audiohook = find_audiohook_by_source(chan->audiohooks, source);

	ast_channel_unlock(chan);

	if (audiohook && audiohook->status != AST_AUDIOHOOK_STATUS_DONE)
		audiohook->status = AST_AUDIOHOOK_STATUS_SHUTDOWN;

	return (audiohook ? 0 : -1);
}

/*!
 * \brief Remove an audiohook from a specified channel
 *
 * \param chan Channel to remove from
 * \param audiohook Audiohook to remove
 *
 * \return Returns 0 on success, -1 on failure
 *
 * \note The channel does not need to be locked before calling this function
 */
int ast_audiohook_remove(struct ast_channel *chan, struct ast_audiohook *audiohook)
{
	ast_channel_lock(chan);

	if (!chan->audiohooks) {
		ast_channel_unlock(chan);
		return -1;
	}

	if (audiohook->type == AST_AUDIOHOOK_TYPE_SPY)
		AST_LIST_REMOVE(&chan->audiohooks->spy_list, audiohook, list);
	else if (audiohook->type == AST_AUDIOHOOK_TYPE_WHISPER)
		AST_LIST_REMOVE(&chan->audiohooks->whisper_list, audiohook, list);
	else if (audiohook->type == AST_AUDIOHOOK_TYPE_MANIPULATE)
		AST_LIST_REMOVE(&chan->audiohooks->manipulate_list, audiohook, list);

	ast_audiohook_lock(audiohook);
	audiohook->status = AST_AUDIOHOOK_STATUS_DONE;
	ast_cond_signal(&audiohook->trigger);
	ast_audiohook_unlock(audiohook);

	ast_channel_unlock(chan);

	return 0;
}

/*! \brief Pass a DTMF frame off to be handled by the audiohook core
 * \param chan Channel that the list is coming off of
 * \param audiohook_list List of audiohooks
 * \param direction Direction frame is coming in from
 * \param frame The frame itself
 * \return Return frame on success, NULL on failure
 */
static struct ast_frame *dtmf_audiohook_write_list(struct ast_channel *chan, struct ast_audiohook_list *audiohook_list, enum ast_audiohook_direction direction, struct ast_frame *frame)
{
	struct ast_audiohook *audiohook = NULL;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->manipulate_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
			AST_LIST_REMOVE_CURRENT(&audiohook_list->manipulate_list, list);
			audiohook->status = AST_AUDIOHOOK_STATUS_DONE;
			ast_audiohook_unlock(audiohook);
			audiohook->manipulate_callback(audiohook, NULL, NULL, 0);
			continue;
		}
		if (ast_test_flag(audiohook, AST_AUDIOHOOK_WANTS_DTMF))
			audiohook->manipulate_callback(audiohook, chan, frame, direction);
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END

	return frame;
}

/*! \brief Pass an AUDIO frame off to be handled by the audiohook core
 * \param chan Channel that the list is coming off of
 * \param audiohook_list List of audiohooks
 * \param direction Direction frame is coming in from
 * \param frame The frame itself
 * \return Return frame on success, NULL on failure
 */
static struct ast_frame *audio_audiohook_write_list(struct ast_channel *chan, struct ast_audiohook_list *audiohook_list, enum ast_audiohook_direction direction, struct ast_frame *frame)
{
	struct ast_audiohook_translate *in_translate = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook_list->in_translate[0] : &audiohook_list->in_translate[1]);
	struct ast_audiohook_translate *out_translate = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook_list->out_translate[0] : &audiohook_list->out_translate[1]);
	struct ast_frame *start_frame = frame, *middle_frame = frame, *end_frame = frame;
	struct ast_audiohook *audiohook = NULL;
	int samples = frame->samples;
	
	/* If the frame coming in is not signed linear we have to send it through the in_translate path */
	if (frame->subclass != AST_FORMAT_SLINEAR) {
		if (in_translate->format != frame->subclass) {
			if (in_translate->trans_pvt)
				ast_translator_free_path(in_translate->trans_pvt);
			if (!(in_translate->trans_pvt = ast_translator_build_path(AST_FORMAT_SLINEAR, frame->subclass)))
				return frame;
			in_translate->format = frame->subclass;
		}
		if (!(middle_frame = ast_translate(in_translate->trans_pvt, frame, 0)))
			return frame;
		samples = middle_frame->samples;
	}

	/* Queue up signed linear frame to each spy */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->spy_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
			AST_LIST_REMOVE_CURRENT(&audiohook_list->spy_list, list);
			audiohook->status = AST_AUDIOHOOK_STATUS_DONE;
			ast_cond_signal(&audiohook->trigger);
			ast_audiohook_unlock(audiohook);
			continue;
		}
		ast_audiohook_write_frame(audiohook, direction, middle_frame);
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END

	/* If this frame is being written out to the channel then we need to use whisper sources */
	if (direction == AST_AUDIOHOOK_DIRECTION_WRITE && !AST_LIST_EMPTY(&audiohook_list->whisper_list)) {
		int i = 0;
		short read_buf[samples], combine_buf[samples], *data1 = NULL, *data2 = NULL;
		memset(&combine_buf, 0, sizeof(combine_buf));
		AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->whisper_list, audiohook, list) {
			ast_audiohook_lock(audiohook);
			if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
				AST_LIST_REMOVE_CURRENT(&audiohook_list->whisper_list, list);
				audiohook->status = AST_AUDIOHOOK_STATUS_DONE;
				ast_cond_signal(&audiohook->trigger);
				ast_audiohook_unlock(audiohook);
				continue;
			}
			if (ast_slinfactory_available(&audiohook->write_factory) >= samples && ast_slinfactory_read(&audiohook->write_factory, read_buf, samples)) {
				/* Take audio from this whisper source and combine it into our main buffer */
				for (i = 0, data1 = combine_buf, data2 = read_buf; i < samples; i++, data1++, data2++)
					ast_slinear_saturated_add(data1, data2);
			}
			ast_audiohook_unlock(audiohook);
		}
		AST_LIST_TRAVERSE_SAFE_END
		/* We take all of the combined whisper sources and combine them into the audio being written out */
		for (i = 0, data1 = middle_frame->data, data2 = combine_buf; i < samples; i++, data1++, data2++)
			ast_slinear_saturated_add(data1, data2);
		end_frame = middle_frame;
	}

	/* Pass off frame to manipulate audiohooks */
	if (!AST_LIST_EMPTY(&audiohook_list->manipulate_list)) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->manipulate_list, audiohook, list) {
			ast_audiohook_lock(audiohook);
			if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
				AST_LIST_REMOVE_CURRENT(&audiohook_list->manipulate_list, list);
				audiohook->status = AST_AUDIOHOOK_STATUS_DONE;
				ast_audiohook_unlock(audiohook);
				/* We basically drop all of our links to the manipulate audiohook and prod it to do it's own destructive things */
				audiohook->manipulate_callback(audiohook, chan, NULL, direction);
				continue;
			}
			/* Feed in frame to manipulation */
			audiohook->manipulate_callback(audiohook, chan, middle_frame, direction);
			ast_audiohook_unlock(audiohook);
		}
		AST_LIST_TRAVERSE_SAFE_END
		end_frame = middle_frame;
	}

	/* Now we figure out what to do with our end frame (whether to transcode or not) */
	if (middle_frame == end_frame) {
		/* Middle frame was modified and became the end frame... let's see if we need to transcode */
		if (end_frame->subclass != start_frame->subclass) {
			if (out_translate->format != start_frame->subclass) {
				if (out_translate->trans_pvt)
					ast_translator_free_path(out_translate->trans_pvt);
				if (!(out_translate->trans_pvt = ast_translator_build_path(start_frame->subclass, AST_FORMAT_SLINEAR))) {
					/* We can't transcode this... drop our middle frame and return the original */
					ast_frfree(middle_frame);
					return start_frame;
				}
				out_translate->format = start_frame->subclass;
			}
			/* Transcode from our middle (signed linear) frame to new format of the frame that came in */
			if (!(end_frame = ast_translate(out_translate->trans_pvt, middle_frame, 0))) {
				/* Failed to transcode the frame... drop it and return the original */
				ast_frfree(middle_frame);
				return start_frame;
			}
			/* Here's the scoop... middle frame is no longer of use to us */
			ast_frfree(middle_frame);
		}
	} else {
		/* No frame was modified, we can just drop our middle frame and pass the frame we got in out */
		ast_frfree(middle_frame);
	}

	return end_frame;
}

/*! \brief Pass a frame off to be handled by the audiohook core
 * \param chan Channel that the list is coming off of
 * \param audiohook_list List of audiohooks
 * \param direction Direction frame is coming in from
 * \param frame The frame itself
 * \return Return frame on success, NULL on failure
 */
struct ast_frame *ast_audiohook_write_list(struct ast_channel *chan, struct ast_audiohook_list *audiohook_list, enum ast_audiohook_direction direction, struct ast_frame *frame)
{
	/* Pass off frame to it's respective list write function */
	if (frame->frametype == AST_FRAME_VOICE)
		return audio_audiohook_write_list(chan, audiohook_list, direction, frame);
	else if (frame->frametype == AST_FRAME_DTMF)
		return dtmf_audiohook_write_list(chan, audiohook_list, direction, frame);
	else
		return frame;
}
			

/*! \brief Wait for audiohook trigger to be triggered
 * \param audiohook Audiohook to wait on
 */
void ast_audiohook_trigger_wait(struct ast_audiohook *audiohook)
{
	struct timeval tv;
	struct timespec ts;

	tv = ast_tvadd(ast_tvnow(), ast_samp2tv(50000, 1000));
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;
	
	ast_cond_timedwait(&audiohook->trigger, &audiohook->lock, &ts);
	
	return;
}
