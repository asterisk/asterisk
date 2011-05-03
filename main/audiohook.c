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

#include <signal.h>

#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/linkedlists.h"
#include "asterisk/audiohook.h"
#include "asterisk/slinfactory.h"
#include "asterisk/frame.h"
#include "asterisk/translate.h"

struct ast_audiohook_translate {
	struct ast_trans_pvt *trans_pvt;
	format_t format;
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
	ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_NEW);

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
	struct timeval *rwtime = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook->read_time : &audiohook->write_time), previous_time = *rwtime;
	int our_factory_samples;
	int our_factory_ms;
	int other_factory_samples;
	int other_factory_ms;
	int muteme = 0;

	/* Update last feeding time to be current */
	*rwtime = ast_tvnow();

	our_factory_samples = ast_slinfactory_available(factory);
	our_factory_ms = ast_tvdiff_ms(*rwtime, previous_time) + (our_factory_samples / 8);
	other_factory_samples = ast_slinfactory_available(other_factory);
	other_factory_ms = other_factory_samples / 8;

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

	/* swap frame data for zeros if mute is required */
	if ((ast_test_flag(audiohook, AST_AUDIOHOOK_MUTE_READ) && (direction == AST_AUDIOHOOK_DIRECTION_READ)) ||
		(ast_test_flag(audiohook, AST_AUDIOHOOK_MUTE_WRITE) && (direction == AST_AUDIOHOOK_DIRECTION_WRITE)) ||
		(ast_test_flag(audiohook, AST_AUDIOHOOK_MUTE_READ | AST_AUDIOHOOK_MUTE_WRITE) == (AST_AUDIOHOOK_MUTE_READ | AST_AUDIOHOOK_MUTE_WRITE))) {
			muteme = 1;
	}

	if (muteme && frame->datalen > 0) {
		ast_frame_clear(frame);
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
		.subclass.codec = AST_FORMAT_SLINEAR,
		.data.ptr = buf,
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
		.subclass.codec = AST_FORMAT_SLINEAR,
		.data.ptr = NULL,
		.datalen = sizeof(buf1),
		.samples = samples,
	};

	/* Make sure both factories have the required samples */
	usable_read = (ast_slinfactory_available(&audiohook->read_factory) >= samples ? 1 : 0);
	usable_write = (ast_slinfactory_available(&audiohook->write_factory) >= samples ? 1 : 0);

	if (!usable_read && !usable_write) {
		/* If both factories are unusable bail out */
		ast_debug(1, "Read factory %p and write factory %p both fail to provide %zd samples\n", &audiohook->read_factory, &audiohook->write_factory, samples);
		return NULL;
	}

	/* If we want to provide only a read factory make sure we aren't waiting for other audio */
	if (usable_read && !usable_write && (ast_tvdiff_ms(ast_tvnow(), audiohook->write_time) < (samples/8)*2)) {
		ast_debug(3, "Write factory %p was pretty quick last time, waiting for them.\n", &audiohook->write_factory);
		return NULL;
	}

	/* If we want to provide only a write factory make sure we aren't waiting for other audio */
	if (usable_write && !usable_read && (ast_tvdiff_ms(ast_tvnow(), audiohook->read_time) < (samples/8)*2)) {
		ast_debug(3, "Read factory %p was pretty quick last time, waiting for them.\n", &audiohook->read_factory);
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
		ast_log(LOG_DEBUG, "Failed to get %d samples from read factory %p\n", (int)samples, &audiohook->read_factory);

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
		ast_log(LOG_DEBUG, "Failed to get %d samples from write factory %p\n", (int)samples, &audiohook->write_factory);

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
	frame.data.ptr = final_buf;

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
struct ast_frame *ast_audiohook_read_frame(struct ast_audiohook *audiohook, size_t samples, enum ast_audiohook_direction direction, format_t format)
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
	ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_RUNNING);

	ast_channel_unlock(chan);

	return 0;
}

/*! \brief Update audiohook's status
 * \param audiohook Audiohook structure
 * \param status Audiohook status enum
 *
 * \note once status is updated to DONE, this function can not be used to set the
 * status back to any other setting.  Setting DONE effectively locks the status as such.
 */

void ast_audiohook_update_status(struct ast_audiohook *audiohook, enum ast_audiohook_status status)
{
	ast_audiohook_lock(audiohook);
	if (audiohook->status != AST_AUDIOHOOK_STATUS_DONE) {
		audiohook->status = status;
		ast_cond_signal(&audiohook->trigger);
	}
	ast_audiohook_unlock(audiohook);
}

/*! \brief Detach audiohook from channel
 * \param audiohook Audiohook structure
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_detach(struct ast_audiohook *audiohook)
{
	if (audiohook->status == AST_AUDIOHOOK_STATUS_NEW || audiohook->status == AST_AUDIOHOOK_STATUS_DONE)
		return 0;

	ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_SHUTDOWN);

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
	while ((audiohook = AST_LIST_REMOVE_HEAD(&audiohook_list->spy_list, list))) {
		ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
	}

	/* Drop any whispering sources */
	while ((audiohook = AST_LIST_REMOVE_HEAD(&audiohook_list->whisper_list, list))) {
		ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
	}

	/* Drop any manipulaters */
	while ((audiohook = AST_LIST_REMOVE_HEAD(&audiohook_list->manipulate_list, list))) {
		ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
		audiohook->manipulate_callback(audiohook, NULL, NULL, 0);
	}

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

/*! \brief find an audiohook based on its source
 * \param audiohook_list The list of audiohooks to search in
 * \param source The source of the audiohook we wish to find
 * \return Return the corresponding audiohook or NULL if it cannot be found.
 */
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

void ast_audiohook_move_by_source(struct ast_channel *old_chan, struct ast_channel *new_chan, const char *source)
{
	struct ast_audiohook *audiohook;
	enum ast_audiohook_status oldstatus;

	if (!old_chan->audiohooks || !(audiohook = find_audiohook_by_source(old_chan->audiohooks, source))) {
		return;
	}

	/* By locking both channels and the audiohook, we can assure that
	 * another thread will not have a chance to read the audiohook's status
	 * as done, even though ast_audiohook_remove signals the trigger
	 * condition.
	 */
	ast_audiohook_lock(audiohook);
	oldstatus = audiohook->status;

	ast_audiohook_remove(old_chan, audiohook);
	ast_audiohook_attach(new_chan, audiohook);

	audiohook->status = oldstatus;
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
		ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_SHUTDOWN);

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

	ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);

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
			AST_LIST_REMOVE_CURRENT(list);
			ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
			ast_audiohook_unlock(audiohook);
			audiohook->manipulate_callback(audiohook, NULL, NULL, 0);
			continue;
		}
		if (ast_test_flag(audiohook, AST_AUDIOHOOK_WANTS_DTMF))
			audiohook->manipulate_callback(audiohook, chan, frame, direction);
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return frame;
}

/*!
 * \brief Pass an AUDIO frame off to be handled by the audiohook core
 *
 * \details
 * This function has 3 ast_frames and 3 parts to handle each.  At the beginning of this
 * function all 3 frames, start_frame, middle_frame, and end_frame point to the initial
 * input frame.
 *
 * Part_1: Translate the start_frame into SLINEAR audio if it is not already in that
 *         format.  The result of this part is middle_frame is guaranteed to be in
 *         SLINEAR format for Part_2.
 * Part_2: Send middle_frame off to spies and manipulators.  At this point middle_frame is
 *         either a new frame as result of the translation, or points directly to the start_frame
 *         because no translation to SLINEAR audio was required.  The result of this part
 *         is end_frame will be updated to point to middle_frame if any audiohook manipulation
 *         took place.
 * Part_3: Translate end_frame's audio back into the format of start frame if necessary.
 *         At this point if middle_frame != end_frame, we are guaranteed that no manipulation
 *         took place and middle_frame can be freed as it was translated... If middle_frame was
 *         not translated and still pointed to start_frame, it would be equal to end_frame as well
 *         regardless if manipulation took place which would not result in this free.  The result
 *         of this part is end_frame is guaranteed to be the format of start_frame for the return.
 *         
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

	/* ---Part_1. translate start_frame to SLINEAR if necessary. */
	/* If the frame coming in is not signed linear we have to send it through the in_translate path */
	if (frame->subclass.codec != AST_FORMAT_SLINEAR) {
		if (in_translate->format != frame->subclass.codec) {
			if (in_translate->trans_pvt)
				ast_translator_free_path(in_translate->trans_pvt);
			if (!(in_translate->trans_pvt = ast_translator_build_path(AST_FORMAT_SLINEAR, frame->subclass.codec)))
				return frame;
			in_translate->format = frame->subclass.codec;
		}
		if (!(middle_frame = ast_translate(in_translate->trans_pvt, frame, 0)))
			return frame;
		samples = middle_frame->samples;
	}

	/* ---Part_2: Send middle_frame to spy and manipulator lists.  middle_frame is guaranteed to be SLINEAR here.*/
	/* Queue up signed linear frame to each spy */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->spy_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
			AST_LIST_REMOVE_CURRENT(list);
			ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
			ast_audiohook_unlock(audiohook);
			continue;
		}
		ast_audiohook_write_frame(audiohook, direction, middle_frame);
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* If this frame is being written out to the channel then we need to use whisper sources */
	if (direction == AST_AUDIOHOOK_DIRECTION_WRITE && !AST_LIST_EMPTY(&audiohook_list->whisper_list)) {
		int i = 0;
		short read_buf[samples], combine_buf[samples], *data1 = NULL, *data2 = NULL;
		memset(&combine_buf, 0, sizeof(combine_buf));
		AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->whisper_list, audiohook, list) {
			ast_audiohook_lock(audiohook);
			if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
				AST_LIST_REMOVE_CURRENT(list);
				ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
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
		AST_LIST_TRAVERSE_SAFE_END;
		/* We take all of the combined whisper sources and combine them into the audio being written out */
		for (i = 0, data1 = middle_frame->data.ptr, data2 = combine_buf; i < samples; i++, data1++, data2++)
			ast_slinear_saturated_add(data1, data2);
		end_frame = middle_frame;
	}

	/* Pass off frame to manipulate audiohooks */
	if (!AST_LIST_EMPTY(&audiohook_list->manipulate_list)) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->manipulate_list, audiohook, list) {
			ast_audiohook_lock(audiohook);
			if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
				AST_LIST_REMOVE_CURRENT(list);
				ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
				ast_audiohook_unlock(audiohook);
				/* We basically drop all of our links to the manipulate audiohook and prod it to do it's own destructive things */
				audiohook->manipulate_callback(audiohook, chan, NULL, direction);
				continue;
			}
			/* Feed in frame to manipulation. */
			if (audiohook->manipulate_callback(audiohook, chan, middle_frame, direction)) {
				/* XXX IGNORE FAILURE */

				/* If the manipulation fails then the frame will be returned in its original state.
				 * Since there are potentially more manipulator callbacks in the list, no action should
				 * be taken here to exit early. */
			}
			ast_audiohook_unlock(audiohook);
		}
		AST_LIST_TRAVERSE_SAFE_END;
		end_frame = middle_frame;
	}

	/* ---Part_3: Decide what to do with the end_frame (whether to transcode or not) */
	if (middle_frame == end_frame) {
		/* Middle frame was modified and became the end frame... let's see if we need to transcode */
		if (end_frame->subclass.codec != start_frame->subclass.codec) {
			if (out_translate->format != start_frame->subclass.codec) {
				if (out_translate->trans_pvt)
					ast_translator_free_path(out_translate->trans_pvt);
				if (!(out_translate->trans_pvt = ast_translator_build_path(start_frame->subclass.codec, AST_FORMAT_SLINEAR))) {
					/* We can't transcode this... drop our middle frame and return the original */
					ast_frfree(middle_frame);
					return start_frame;
				}
				out_translate->format = start_frame->subclass.codec;
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

int ast_audiohook_write_list_empty(struct ast_audiohook_list *audiohook_list)
{
	if (AST_LIST_EMPTY(&audiohook_list->spy_list) &&
		AST_LIST_EMPTY(&audiohook_list->whisper_list) &&
		AST_LIST_EMPTY(&audiohook_list->manipulate_list)) {

		return 1;
	}
	return 0;
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
	struct timeval wait;
	struct timespec ts;

	wait = ast_tvadd(ast_tvnow(), ast_samp2tv(50000, 1000));
	ts.tv_sec = wait.tv_sec;
	ts.tv_nsec = wait.tv_usec * 1000;
	
	ast_cond_timedwait(&audiohook->trigger, &audiohook->lock, &ts);
	
	return;
}

/* Count number of channel audiohooks by type, regardless of type */
int ast_channel_audiohook_count_by_source(struct ast_channel *chan, const char *source, enum ast_audiohook_type type)
{
	int count = 0;
	struct ast_audiohook *ah = NULL;

	if (!chan->audiohooks)
		return -1;

	switch (type) {
		case AST_AUDIOHOOK_TYPE_SPY:
			AST_LIST_TRAVERSE(&chan->audiohooks->spy_list, ah, list) {
				if (!strcmp(ah->source, source)) {
					count++;
				}
			}
			break;
		case AST_AUDIOHOOK_TYPE_WHISPER:
			AST_LIST_TRAVERSE(&chan->audiohooks->whisper_list, ah, list) {
				if (!strcmp(ah->source, source)) {
					count++;
				}
			}
			break;
		case AST_AUDIOHOOK_TYPE_MANIPULATE:
			AST_LIST_TRAVERSE(&chan->audiohooks->manipulate_list, ah, list) {
				if (!strcmp(ah->source, source)) {
					count++;
				}
			}
			break;
		default:
			ast_log(LOG_DEBUG, "Invalid audiohook type supplied, (%d)\n", type);
			return -1;
	}

	return count;
}

/* Count number of channel audiohooks by type that are running */
int ast_channel_audiohook_count_by_source_running(struct ast_channel *chan, const char *source, enum ast_audiohook_type type)
{
	int count = 0;
	struct ast_audiohook *ah = NULL;
	if (!chan->audiohooks)
		return -1;

	switch (type) {
		case AST_AUDIOHOOK_TYPE_SPY:
			AST_LIST_TRAVERSE(&chan->audiohooks->spy_list, ah, list) {
				if ((!strcmp(ah->source, source)) && (ah->status == AST_AUDIOHOOK_STATUS_RUNNING))
					count++;
			}
			break;
		case AST_AUDIOHOOK_TYPE_WHISPER:
			AST_LIST_TRAVERSE(&chan->audiohooks->whisper_list, ah, list) {
				if ((!strcmp(ah->source, source)) && (ah->status == AST_AUDIOHOOK_STATUS_RUNNING))
					count++;
			}
			break;
		case AST_AUDIOHOOK_TYPE_MANIPULATE:
			AST_LIST_TRAVERSE(&chan->audiohooks->manipulate_list, ah, list) {
				if ((!strcmp(ah->source, source)) && (ah->status == AST_AUDIOHOOK_STATUS_RUNNING))
					count++;
			}
			break;
		default:
			ast_log(LOG_DEBUG, "Invalid audiohook type supplied, (%d)\n", type);
			return -1;
	}
	return count;
}

/*! \brief Audiohook volume adjustment structure */
struct audiohook_volume {
	struct ast_audiohook audiohook; /*!< Audiohook attached to the channel */
	int read_adjustment;            /*!< Value to adjust frames read from the channel by */
	int write_adjustment;           /*!< Value to adjust frames written to the channel by */
};

/*! \brief Callback used to destroy the audiohook volume datastore
 * \param data Volume information structure
 * \return Returns nothing
 */
static void audiohook_volume_destroy(void *data)
{
	struct audiohook_volume *audiohook_volume = data;

	/* Destroy the audiohook as it is no longer in use */
	ast_audiohook_destroy(&audiohook_volume->audiohook);

	/* Finally free ourselves, we are of no more use */
	ast_free(audiohook_volume);

	return;
}

/*! \brief Datastore used to store audiohook volume information */
static const struct ast_datastore_info audiohook_volume_datastore = {
	.type = "Volume",
	.destroy = audiohook_volume_destroy,
};

/*! \brief Helper function which actually gets called by audiohooks to perform the adjustment
 * \param audiohook Audiohook attached to the channel
 * \param chan Channel we are attached to
 * \param frame Frame of audio we want to manipulate
 * \param direction Direction the audio came in from
 * \return Returns 0 on success, -1 on failure
 */
static int audiohook_volume_callback(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *frame, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore = NULL;
	struct audiohook_volume *audiohook_volume = NULL;
	int *gain = NULL;

	/* If the audiohook is shutting down don't even bother */
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE) {
		return 0;
	}

	/* Try to find the datastore containg adjustment information, if we can't just bail out */
	if (!(datastore = ast_channel_datastore_find(chan, &audiohook_volume_datastore, NULL))) {
		return 0;
	}

	audiohook_volume = datastore->data;

	/* Based on direction grab the appropriate adjustment value */
	if (direction == AST_AUDIOHOOK_DIRECTION_READ) {
		gain = &audiohook_volume->read_adjustment;
	} else if (direction == AST_AUDIOHOOK_DIRECTION_WRITE) {
		gain = &audiohook_volume->write_adjustment;
	}

	/* If an adjustment value is present modify the frame */
	if (gain && *gain) {
		ast_frame_adjust_volume(frame, *gain);
	}

	return 0;
}

/*! \brief Helper function which finds and optionally creates an audiohook_volume_datastore datastore on a channel
 * \param chan Channel to look on
 * \param create Whether to create the datastore if not found
 * \return Returns audiohook_volume structure on success, NULL on failure
 */
static struct audiohook_volume *audiohook_volume_get(struct ast_channel *chan, int create)
{
	struct ast_datastore *datastore = NULL;
	struct audiohook_volume *audiohook_volume = NULL;

	/* If we are able to find the datastore return the contents (which is actually an audiohook_volume structure) */
	if ((datastore = ast_channel_datastore_find(chan, &audiohook_volume_datastore, NULL))) {
		return datastore->data;
	}

	/* If we are not allowed to create a datastore or if we fail to create a datastore, bail out now as we have nothing for them */
	if (!create || !(datastore = ast_datastore_alloc(&audiohook_volume_datastore, NULL))) {
		return NULL;
	}

	/* Create a new audiohook_volume structure to contain our adjustments and audiohook */
	if (!(audiohook_volume = ast_calloc(1, sizeof(*audiohook_volume)))) {
		ast_datastore_free(datastore);
		return NULL;
	}

	/* Setup our audiohook structure so we can manipulate the audio */
	ast_audiohook_init(&audiohook_volume->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "Volume");
	audiohook_volume->audiohook.manipulate_callback = audiohook_volume_callback;

	/* Attach the audiohook_volume blob to the datastore and attach to the channel */
	datastore->data = audiohook_volume;
	ast_channel_datastore_add(chan, datastore);

	/* All is well... put the audiohook into motion */
	ast_audiohook_attach(chan, &audiohook_volume->audiohook);

	return audiohook_volume;
}

/*! \brief Adjust the volume on frames read from or written to a channel
 * \param chan Channel to muck with
 * \param direction Direction to set on
 * \param volume Value to adjust the volume by
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_volume_set(struct ast_channel *chan, enum ast_audiohook_direction direction, int volume)
{
	struct audiohook_volume *audiohook_volume = NULL;

	/* Attempt to find the audiohook volume information, but only create it if we are not setting the adjustment value to zero */
	if (!(audiohook_volume = audiohook_volume_get(chan, (volume ? 1 : 0)))) {
		return -1;
	}

	/* Now based on the direction set the proper value */
	if (direction == AST_AUDIOHOOK_DIRECTION_READ || direction == AST_AUDIOHOOK_DIRECTION_BOTH) {
		audiohook_volume->read_adjustment = volume;
	}
	if (direction == AST_AUDIOHOOK_DIRECTION_WRITE || direction == AST_AUDIOHOOK_DIRECTION_BOTH) {
		audiohook_volume->write_adjustment = volume;
	}

	return 0;
}

/*! \brief Retrieve the volume adjustment value on frames read from or written to a channel
 * \param chan Channel to retrieve volume adjustment from
 * \param direction Direction to retrieve
 * \return Returns adjustment value
 */
int ast_audiohook_volume_get(struct ast_channel *chan, enum ast_audiohook_direction direction)
{
	struct audiohook_volume *audiohook_volume = NULL;
	int adjustment = 0;

	/* Attempt to find the audiohook volume information, but do not create it as we only want to look at the values */
	if (!(audiohook_volume = audiohook_volume_get(chan, 0))) {
		return 0;
	}

	/* Grab the adjustment value based on direction given */
	if (direction == AST_AUDIOHOOK_DIRECTION_READ) {
		adjustment = audiohook_volume->read_adjustment;
	} else if (direction == AST_AUDIOHOOK_DIRECTION_WRITE) {
		adjustment = audiohook_volume->write_adjustment;
	}

	return adjustment;
}

/*! \brief Adjust the volume on frames read from or written to a channel
 * \param chan Channel to muck with
 * \param direction Direction to increase
 * \param volume Value to adjust the adjustment by
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_volume_adjust(struct ast_channel *chan, enum ast_audiohook_direction direction, int volume)
{
	struct audiohook_volume *audiohook_volume = NULL;

	/* Attempt to find the audiohook volume information, and create an audiohook if none exists */
	if (!(audiohook_volume = audiohook_volume_get(chan, 1))) {
		return -1;
	}

	/* Based on the direction change the specific adjustment value */
	if (direction == AST_AUDIOHOOK_DIRECTION_READ || direction == AST_AUDIOHOOK_DIRECTION_BOTH) {
		audiohook_volume->read_adjustment += volume;
	}
	if (direction == AST_AUDIOHOOK_DIRECTION_WRITE || direction == AST_AUDIOHOOK_DIRECTION_BOTH) {
		audiohook_volume->write_adjustment += volume;
	}

	return 0;
}

/*! \brief Mute frames read from or written to a channel
 * \param chan Channel to muck with
 * \param source Type of audiohook
 * \param flag which flag to set / clear
 * \param clear set or clear
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_set_mute(struct ast_channel *chan, const char *source, enum ast_audiohook_flags flag, int clear)
{
	struct ast_audiohook *audiohook = NULL;

	ast_channel_lock(chan);

	/* Ensure the channel has audiohooks on it */
	if (!chan->audiohooks) {
		ast_channel_unlock(chan);
		return -1;
	}

	audiohook = find_audiohook_by_source(chan->audiohooks, source);

	if (audiohook) {
		if (clear) {
			ast_clear_flag(audiohook, flag);
		} else {
			ast_set_flag(audiohook, flag);
		}
	}

	ast_channel_unlock(chan);

	return (audiohook ? 0 : -1);
}
