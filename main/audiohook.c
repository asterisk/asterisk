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

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

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
#include "asterisk/format_cache.h"

#define AST_AUDIOHOOK_SYNC_TOLERANCE 100 /*!< Tolerance in milliseconds for audiohooks synchronization */
#define AST_AUDIOHOOK_SMALL_QUEUE_TOLERANCE 100 /*!< When small queue is enabled, this is the maximum amount of audio that can remain queued at a time. */

struct ast_audiohook_translate {
	struct ast_trans_pvt *trans_pvt;
	struct ast_format *format;
};

struct ast_audiohook_list {
	/* If all the audiohooks in this list are capable
	 * of processing slinear at any sample rate, this
	 * variable will be set and the sample rate will
	 * be preserved during ast_audiohook_write_list()*/
	int native_slin_compatible;
	int list_internal_samp_rate;/*!< Internal sample rate used when writing to the audiohook list */

	struct ast_audiohook_translate in_translate[2];
	struct ast_audiohook_translate out_translate[2];
	AST_LIST_HEAD_NOLOCK(, ast_audiohook) spy_list;
	AST_LIST_HEAD_NOLOCK(, ast_audiohook) whisper_list;
	AST_LIST_HEAD_NOLOCK(, ast_audiohook) manipulate_list;
};

static int audiohook_set_internal_rate(struct ast_audiohook *audiohook, int rate, int reset)
{
	struct ast_format *slin;

	/*
	 * Only set the rate if it is higher than the current one. This prevents possible switching
	 * and resetting of the rate if there is a difference between read and write rates.
	 */
	if (audiohook->hook_internal_samp_rate && audiohook->hook_internal_samp_rate >= rate) {
		return 0;
	}

	audiohook->hook_internal_samp_rate = rate;

	slin = ast_format_cache_get_slin_by_rate(rate);

	/* Setup the factories that are needed for this audiohook type */
	switch (audiohook->type) {
	case AST_AUDIOHOOK_TYPE_SPY:
	case AST_AUDIOHOOK_TYPE_WHISPER:
		if (reset) {
			ast_slinfactory_destroy(&audiohook->read_factory);
			ast_slinfactory_destroy(&audiohook->write_factory);
		}
		ast_slinfactory_init_with_format(&audiohook->read_factory, slin);
		ast_slinfactory_init_with_format(&audiohook->write_factory, slin);
		break;
	default:
		break;
	}

	return 0;
}

/*! \brief Initialize an audiohook structure
 *
 * \param audiohook Audiohook structure
 * \param type
 * \param source, init_flags
 *
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_init(struct ast_audiohook *audiohook, enum ast_audiohook_type type, const char *source, enum ast_audiohook_init_flags init_flags)
{
	/* Need to keep the type and source */
	audiohook->type = type;
	audiohook->source = source;

	/* Initialize lock that protects our audiohook */
	ast_mutex_init(&audiohook->lock);
	ast_cond_init(&audiohook->trigger, NULL);

	audiohook->init_flags = init_flags;

	/* initialize internal rate at 8khz, this will adjust if necessary */
	audiohook_set_internal_rate(audiohook, 8000, 0);

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
	case AST_AUDIOHOOK_TYPE_WHISPER:
		ast_slinfactory_destroy(&audiohook->read_factory);
		ast_slinfactory_destroy(&audiohook->write_factory);
		break;
	default:
		break;
	}

	/* Destroy translation path if present */
	if (audiohook->trans_pvt)
		ast_translator_free_path(audiohook->trans_pvt);

	ao2_cleanup(audiohook->format);

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
	our_factory_ms = ast_tvdiff_ms(*rwtime, previous_time) + (our_factory_samples / (audiohook->hook_internal_samp_rate / 1000));
	other_factory_samples = ast_slinfactory_available(other_factory);
	other_factory_ms = other_factory_samples / (audiohook->hook_internal_samp_rate / 1000);

	if (ast_test_flag(audiohook, AST_AUDIOHOOK_TRIGGER_SYNC) && other_factory_samples && (our_factory_ms - other_factory_ms > AST_AUDIOHOOK_SYNC_TOLERANCE)) {
		ast_debug(1, "Flushing audiohook %p so it remains in sync\n", audiohook);
		ast_slinfactory_flush(factory);
		ast_slinfactory_flush(other_factory);
	}

	if (ast_test_flag(audiohook, AST_AUDIOHOOK_SMALL_QUEUE) && ((our_factory_ms > AST_AUDIOHOOK_SMALL_QUEUE_TOLERANCE) || (other_factory_ms > AST_AUDIOHOOK_SMALL_QUEUE_TOLERANCE))) {
		ast_debug(1, "Audiohook %p has stale audio in its factories. Flushing them both\n", audiohook);
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
		.subclass.format = ast_format_cache_get_slin_by_rate(audiohook->hook_internal_samp_rate),
		.data.ptr = buf,
		.datalen = sizeof(buf),
		.samples = samples,
	};

	/* Ensure the factory is able to give us the samples we want */
	if (samples > ast_slinfactory_available(factory)) {
		return NULL;
	}

	/* Read data in from factory */
	if (!ast_slinfactory_read(factory, buf, samples)) {
		return NULL;
	}

	/* If a volume adjustment needs to be applied apply it */
	if (vol) {
		ast_frame_adjust_volume(&frame, vol);
	}

	return ast_frdup(&frame);
}

static struct ast_frame *audiohook_read_frame_both(struct ast_audiohook *audiohook, size_t samples, struct ast_frame **read_reference, struct ast_frame **write_reference)
{
	int i = 0, usable_read, usable_write;
	short buf1[samples], buf2[samples], *read_buf = NULL, *write_buf = NULL, *final_buf = NULL, *data1 = NULL, *data2 = NULL;
	struct ast_frame frame = {
		.frametype = AST_FRAME_VOICE,
		.data.ptr = NULL,
		.datalen = sizeof(buf1),
		.samples = samples,
	};

	/* Make sure both factories have the required samples */
	usable_read = (ast_slinfactory_available(&audiohook->read_factory) >= samples ? 1 : 0);
	usable_write = (ast_slinfactory_available(&audiohook->write_factory) >= samples ? 1 : 0);

	if (!usable_read && !usable_write) {
		/* If both factories are unusable bail out */
		ast_debug(1, "Read factory %p and write factory %p both fail to provide %zu samples\n", &audiohook->read_factory, &audiohook->write_factory, samples);
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
					if (audiohook->options.read_volume > 0) {
						ast_slinear_saturated_multiply(&buf1[count], &adjust_value);
					} else if (audiohook->options.read_volume < 0) {
						ast_slinear_saturated_divide(&buf1[count], &adjust_value);
					}
				}
			}
		}
	} else {
		ast_debug(1, "Failed to get %d samples from read factory %p\n", (int)samples, &audiohook->read_factory);
	}

	/* Move on to the write factory... if there are enough samples, read them in */
	if (usable_write) {
		if (ast_slinfactory_read(&audiohook->write_factory, buf2, samples)) {
			write_buf = buf2;
			/* Adjust write volume if need be */
			if (audiohook->options.write_volume) {
				int count = 0;
				short adjust_value = abs(audiohook->options.write_volume);
				for (count = 0; count < samples; count++) {
					if (audiohook->options.write_volume > 0) {
						ast_slinear_saturated_multiply(&buf2[count], &adjust_value);
					} else if (audiohook->options.write_volume < 0) {
						ast_slinear_saturated_divide(&buf2[count], &adjust_value);
					}
				}
			}
		}
	} else {
		ast_debug(1, "Failed to get %d samples from write factory %p\n", (int)samples, &audiohook->write_factory);
	}

	/* Basically we figure out which buffer to use... and if mixing can be done here */
	if (read_buf && read_reference) {
		frame.data.ptr = buf1;
		*read_reference = ast_frdup(&frame);
	}
	if (write_buf && write_reference) {
		frame.data.ptr = buf2;
		*write_reference = ast_frdup(&frame);
	}

	if (read_buf && write_buf) {
		for (i = 0, data1 = read_buf, data2 = write_buf; i < samples; i++, data1++, data2++) {
			ast_slinear_saturated_add(data1, data2);
		}
		final_buf = buf1;
	} else if (read_buf) {
		final_buf = buf1;
	} else if (write_buf) {
		final_buf = buf2;
	} else {
		return NULL;
	}

	/* Make the final buffer part of the frame, so it gets duplicated fine */
	frame.data.ptr = final_buf;

	frame.subclass.format = ast_format_cache_get_slin_by_rate(audiohook->hook_internal_samp_rate);

	/* Yahoo, a combined copy of the audio! */
	return ast_frdup(&frame);
}

static struct ast_frame *audiohook_read_frame_helper(struct ast_audiohook *audiohook, size_t samples, enum ast_audiohook_direction direction, struct ast_format *format, struct ast_frame **read_reference, struct ast_frame **write_reference)
{
	struct ast_frame *read_frame = NULL, *final_frame = NULL;
	struct ast_format *slin;

	audiohook_set_internal_rate(audiohook, ast_format_get_sample_rate(format), 1);

	if (!(read_frame = (direction == AST_AUDIOHOOK_DIRECTION_BOTH ?
		audiohook_read_frame_both(audiohook, samples, read_reference, write_reference) :
		audiohook_read_frame_single(audiohook, samples, direction)))) {
		return NULL;
	}

	slin = ast_format_cache_get_slin_by_rate(audiohook->hook_internal_samp_rate);

	/* If they don't want signed linear back out, we'll have to send it through the translation path */
	if (ast_format_cmp(format, slin) != AST_FORMAT_CMP_EQUAL) {
		/* Rebuild translation path if different format then previously */
		if (ast_format_cmp(format, audiohook->format) == AST_FORMAT_CMP_NOT_EQUAL) {
			if (audiohook->trans_pvt) {
				ast_translator_free_path(audiohook->trans_pvt);
				audiohook->trans_pvt = NULL;
			}

			/* Setup new translation path for this format... if we fail we can't very well return signed linear so free the frame and return nothing */
			if (!(audiohook->trans_pvt = ast_translator_build_path(format, slin))) {
				ast_frfree(read_frame);
				return NULL;
			}
			ao2_replace(audiohook->format, format);
		}
		/* Convert to requested format, and allow the read in frame to be freed */
		final_frame = ast_translate(audiohook->trans_pvt, read_frame, 1);
	} else {
		final_frame = read_frame;
	}

	return final_frame;
}

/*! \brief Reads a frame in from the audiohook structure
 * \param audiohook Audiohook structure
 * \param samples Number of samples wanted in requested output format
 * \param direction Direction the audio frame came from
 * \param format Format of frame remote side wants back
 * \return Returns frame on success, NULL on failure
 */
struct ast_frame *ast_audiohook_read_frame(struct ast_audiohook *audiohook, size_t samples, enum ast_audiohook_direction direction, struct ast_format *format)
{
	return audiohook_read_frame_helper(audiohook, samples, direction, format, NULL, NULL);
}

/*! \brief Reads a frame in from the audiohook structure
 * \param audiohook Audiohook structure
 * \param samples Number of samples wanted
 * \param direction Direction the audio frame came from
 * \param format Format of frame remote side wants back
 * \param read_frame frame pointer for copying read frame data
 * \param write_frame frame pointer for copying write frame data
 * \return Returns frame on success, NULL on failure
 */
struct ast_frame *ast_audiohook_read_frame_all(struct ast_audiohook *audiohook, size_t samples, struct ast_format *format, struct ast_frame **read_frame, struct ast_frame **write_frame)
{
	return audiohook_read_frame_helper(audiohook, samples, AST_AUDIOHOOK_DIRECTION_BOTH, format, read_frame, write_frame);
}

static void audiohook_list_set_samplerate_compatibility(struct ast_audiohook_list *audiohook_list)
{
	struct ast_audiohook *ah = NULL;
	audiohook_list->native_slin_compatible = 1;
	AST_LIST_TRAVERSE(&audiohook_list->manipulate_list, ah, list) {
		if (!(ah->init_flags & AST_AUDIOHOOK_MANIPULATE_ALL_RATES)) {
			audiohook_list->native_slin_compatible = 0;
			return;
		}
	}
}

/*! \brief Attach audiohook to channel
 * \param chan Channel
 * \param audiohook Audiohook structure
 * \return Returns 0 on success, -1 on failure
 */
int ast_audiohook_attach(struct ast_channel *chan, struct ast_audiohook *audiohook)
{
	ast_channel_lock(chan);

	if (!ast_channel_audiohooks(chan)) {
		struct ast_audiohook_list *ahlist;
		/* Whoops... allocate a new structure */
		if (!(ahlist = ast_calloc(1, sizeof(*ahlist)))) {
			ast_channel_unlock(chan);
			return -1;
		}
		ast_channel_audiohooks_set(chan, ahlist);
		AST_LIST_HEAD_INIT_NOLOCK(&ast_channel_audiohooks(chan)->spy_list);
		AST_LIST_HEAD_INIT_NOLOCK(&ast_channel_audiohooks(chan)->whisper_list);
		AST_LIST_HEAD_INIT_NOLOCK(&ast_channel_audiohooks(chan)->manipulate_list);
		/* This sample rate will adjust as necessary when writing to the list. */
		ast_channel_audiohooks(chan)->list_internal_samp_rate = 8000;
	}

	/* Drop into respective list */
	if (audiohook->type == AST_AUDIOHOOK_TYPE_SPY) {
		AST_LIST_INSERT_TAIL(&ast_channel_audiohooks(chan)->spy_list, audiohook, list);
	} else if (audiohook->type == AST_AUDIOHOOK_TYPE_WHISPER) {
		AST_LIST_INSERT_TAIL(&ast_channel_audiohooks(chan)->whisper_list, audiohook, list);
	} else if (audiohook->type == AST_AUDIOHOOK_TYPE_MANIPULATE) {
		AST_LIST_INSERT_TAIL(&ast_channel_audiohooks(chan)->manipulate_list, audiohook, list);
	}


	audiohook_set_internal_rate(audiohook, ast_channel_audiohooks(chan)->list_internal_samp_rate, 1);
	audiohook_list_set_samplerate_compatibility(ast_channel_audiohooks(chan));

	/* Change status over to running since it is now attached */
	ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_RUNNING);

	if (ast_channel_is_bridged(chan)) {
		ast_channel_set_unbridged_nolock(chan, 1);
	}

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
	if (audiohook->status == AST_AUDIOHOOK_STATUS_NEW || audiohook->status == AST_AUDIOHOOK_STATUS_DONE) {
		return 0;
	}

	ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_SHUTDOWN);

	while (audiohook->status != AST_AUDIOHOOK_STATUS_DONE) {
		ast_audiohook_trigger_wait(audiohook);
	}

	return 0;
}

void ast_audiohook_detach_list(struct ast_audiohook_list *audiohook_list)
{
	int i;
	struct ast_audiohook *audiohook;

	if (!audiohook_list) {
		return;
	}

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
		if (audiohook_list->in_translate[i].trans_pvt) {
			ast_translator_free_path(audiohook_list->in_translate[i].trans_pvt);
			ao2_cleanup(audiohook_list->in_translate[i].format);
		}
		if (audiohook_list->out_translate[i].trans_pvt) {
			ast_translator_free_path(audiohook_list->out_translate[i].trans_pvt);
			ao2_cleanup(audiohook_list->in_translate[i].format);
		}
	}

	/* Free ourselves */
	ast_free(audiohook_list);
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
		if (!strcasecmp(audiohook->source, source)) {
			return audiohook;
		}
	}

	AST_LIST_TRAVERSE(&audiohook_list->whisper_list, audiohook, list) {
		if (!strcasecmp(audiohook->source, source)) {
			return audiohook;
		}
	}

	AST_LIST_TRAVERSE(&audiohook_list->manipulate_list, audiohook, list) {
		if (!strcasecmp(audiohook->source, source)) {
			return audiohook;
		}
	}

	return NULL;
}

static void audiohook_move(struct ast_channel *old_chan, struct ast_channel *new_chan, struct ast_audiohook *audiohook)
{
	enum ast_audiohook_status oldstatus;

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

void ast_audiohook_move_by_source(struct ast_channel *old_chan, struct ast_channel *new_chan, const char *source)
{
	struct ast_audiohook *audiohook;

	if (!ast_channel_audiohooks(old_chan)) {
		return;
	}

	audiohook = find_audiohook_by_source(ast_channel_audiohooks(old_chan), source);
	if (!audiohook) {
		return;
	}

	audiohook_move(old_chan, new_chan, audiohook);
}

void ast_audiohook_move_all(struct ast_channel *old_chan, struct ast_channel *new_chan)
{
	struct ast_audiohook *audiohook;
	struct ast_audiohook_list *audiohook_list;

	audiohook_list = ast_channel_audiohooks(old_chan);
	if (!audiohook_list) {
		return;
	}

	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->spy_list, audiohook, list) {
		audiohook_move(old_chan, new_chan, audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->whisper_list, audiohook, list) {
		audiohook_move(old_chan, new_chan, audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->manipulate_list, audiohook, list) {
		audiohook_move(old_chan, new_chan, audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END;
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
	if (!ast_channel_audiohooks(chan)) {
		ast_channel_unlock(chan);
		return -1;
	}

	audiohook = find_audiohook_by_source(ast_channel_audiohooks(chan), source);

	ast_channel_unlock(chan);

	if (audiohook && audiohook->status != AST_AUDIOHOOK_STATUS_DONE) {
		ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_SHUTDOWN);
	}

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

	if (!ast_channel_audiohooks(chan)) {
		ast_channel_unlock(chan);
		return -1;
	}

	if (audiohook->type == AST_AUDIOHOOK_TYPE_SPY) {
		AST_LIST_REMOVE(&ast_channel_audiohooks(chan)->spy_list, audiohook, list);
	} else if (audiohook->type == AST_AUDIOHOOK_TYPE_WHISPER) {
		AST_LIST_REMOVE(&ast_channel_audiohooks(chan)->whisper_list, audiohook, list);
	} else if (audiohook->type == AST_AUDIOHOOK_TYPE_MANIPULATE) {
		AST_LIST_REMOVE(&ast_channel_audiohooks(chan)->manipulate_list, audiohook, list);
	}

	audiohook_list_set_samplerate_compatibility(ast_channel_audiohooks(chan));
	ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);

	if (ast_channel_is_bridged(chan)) {
		ast_channel_set_unbridged_nolock(chan, 1);
	}

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
	int removed = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->manipulate_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
			AST_LIST_REMOVE_CURRENT(list);
			removed = 1;
			ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
			ast_audiohook_unlock(audiohook);
			audiohook->manipulate_callback(audiohook, NULL, NULL, 0);
			if (ast_channel_is_bridged(chan)) {
				ast_channel_set_unbridged_nolock(chan, 1);
			}
			continue;
		}
		if (ast_test_flag(audiohook, AST_AUDIOHOOK_WANTS_DTMF)) {
			audiohook->manipulate_callback(audiohook, chan, frame, direction);
		}
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* if an audiohook got removed, reset samplerate compatibility */
	if (removed) {
		audiohook_list_set_samplerate_compatibility(audiohook_list);
	}
	return frame;
}

static struct ast_frame *audiohook_list_translate_to_slin(struct ast_audiohook_list *audiohook_list,
	enum ast_audiohook_direction direction, struct ast_frame *frame)
{
	struct ast_audiohook_translate *in_translate = (direction == AST_AUDIOHOOK_DIRECTION_READ ?
		&audiohook_list->in_translate[0] : &audiohook_list->in_translate[1]);
	struct ast_frame *new_frame = frame;
	struct ast_format *slin;

	slin = ast_format_cache_get_slin_by_rate(audiohook_list->list_internal_samp_rate);
	if (ast_format_cmp(frame->subclass.format, slin) == AST_FORMAT_CMP_EQUAL) {
		return new_frame;
	}

	if (ast_format_cmp(frame->subclass.format, in_translate->format) == AST_FORMAT_CMP_NOT_EQUAL) {
		if (in_translate->trans_pvt) {
			ast_translator_free_path(in_translate->trans_pvt);
		}
		if (!(in_translate->trans_pvt = ast_translator_build_path(slin, frame->subclass.format))) {
			return NULL;
		}
		ao2_replace(in_translate->format, frame->subclass.format);
	}

	if (!(new_frame = ast_translate(in_translate->trans_pvt, frame, 0))) {
		return NULL;
	}

	return new_frame;
}

static struct ast_frame *audiohook_list_translate_to_native(struct ast_audiohook_list *audiohook_list,
	enum ast_audiohook_direction direction, struct ast_frame *slin_frame, struct ast_format *outformat)
{
	struct ast_audiohook_translate *out_translate = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook_list->out_translate[0] : &audiohook_list->out_translate[1]);
	struct ast_frame *outframe = NULL;
	if (ast_format_cmp(slin_frame->subclass.format, outformat) == AST_FORMAT_CMP_NOT_EQUAL) {
		/* rebuild translators if necessary */
		if (ast_format_cmp(out_translate->format, outformat) == AST_FORMAT_CMP_NOT_EQUAL) {
			if (out_translate->trans_pvt) {
				ast_translator_free_path(out_translate->trans_pvt);
			}
			if (!(out_translate->trans_pvt = ast_translator_build_path(outformat, slin_frame->subclass.format))) {
				return NULL;
			}
			ao2_replace(out_translate->format, outformat);
		}
		/* translate back to the format the frame came in as. */
		if (!(outframe = ast_translate(out_translate->trans_pvt, slin_frame, 0))) {
			return NULL;
		}
	}
	return outframe;
}

static void audiohook_list_set_internal_rate(struct ast_audiohook_list *audiohook_list, int rate)
{
	/*
	 * If we are capable of handling sample rates other that 8khz, update the
	 * internal audiohook_list's rate and higher samplerate audio arrives. By
	 * updating the list's rate, all the audiohooks in the list will be updated.
	 */
	if (audiohook_list->native_slin_compatible &&
	    audiohook_list->list_internal_samp_rate < rate) {
		audiohook_list->list_internal_samp_rate = rate;
	}
}

static void audiohook_list_check_internal_rates(struct ast_audiohook_list *audiohook_list,
						struct ast_audiohook *audiohook)
{
	/*
	 * The audiohook's rate may have changed during a read. If so the list's rate may
	 * also need to be updated.
	 */
	audiohook_list_set_internal_rate(audiohook_list, audiohook->hook_internal_samp_rate);

	/*
	 * The list's rate may have changed (an audiohook's rate changed or an incoming
	 * frame had a higher sample rate, and compatibility mode was enabled). If so the
	 * audiohook's rate may need to change as well.
	 */
	audiohook_set_internal_rate(audiohook, audiohook_list->list_internal_samp_rate, 1);
}

/*!
 * \brief Audiohook lists pre-checks.
 *
 * If they need to be, update the audiohook_list's and hook's internal sample rates.
 *
 * \param frame the frame itself
 * \param audiohook_list audiohook_list data object
 */
static void audiohook_lists_check(struct ast_frame *frame, struct ast_audiohook_list *audiohook_list)
{
	struct ast_audiohook *audiohook;

	/*
	 * The incoming frame's sample rate may be higher than the current audiohook_list's internal
	 * sample rate. If it is adjust the internal rate accordingly (if native slin compatibility
	 * is enabled).
	 */
	audiohook_list_set_internal_rate(audiohook_list, ast_format_get_sample_rate(frame->subclass.format));

	/*
	 * For the spy list check if the internal rates need to be updated. A change in rate may
	 * occur if the incoming frame's sample rate changes it (see above), or if the internal
	 * sample rate on a hook itself gets modified during a read. If a hook's rate gets modified
	 * on a read, then by checking here the audiohook_list's rate can also be adjusted if needed.
	 */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->spy_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		if (audiohook->status == AST_AUDIOHOOK_STATUS_RUNNING) {
			audiohook_list_check_internal_rates(audiohook_list, audiohook);
		}
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END;
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
 *         because no translation to SLINEAR audio was required.
 * Part_3: Translate end_frame's audio back into the format of start frame if necessary.  This
 *         is only necessary if manipulation of middle_frame occurred.
 *
 * \param chan Channel that the list is coming off of
 * \param audiohook_list List of audiohooks
 * \param direction Direction frame is coming in from
 * \param frame The frame itself
 * \return Return frame on success, NULL on failure
 */
static struct ast_frame *audio_audiohook_write_list(struct ast_channel *chan, struct ast_audiohook_list *audiohook_list, enum ast_audiohook_direction direction, struct ast_frame *frame)
{
	struct ast_frame *start_frame = frame, *middle_frame = frame, *end_frame = frame;
	struct ast_audiohook *audiohook = NULL;
	int samples;
	int middle_frame_manipulated = 0;
	int removed = 0;

	/*
	 * Do a pre-check on the audiohooks to see if sample rates need to be adjusted.
	 */
	audiohook_lists_check(frame, audiohook_list);

	/* ---Part_1. translate start_frame to SLINEAR if necessary. */
	if (!(middle_frame = audiohook_list_translate_to_slin(audiohook_list, direction, start_frame))) {
		return frame;
	}
	samples = middle_frame->samples;

	/* ---Part_2: Send middle_frame to spy and manipulator lists.  middle_frame is guaranteed to be SLINEAR here.*/
	/* Queue up signed linear frame to each spy */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->spy_list, audiohook, list) {
		ast_audiohook_lock(audiohook);
		if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
			AST_LIST_REMOVE_CURRENT(list);
			removed = 1;
			ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
			ast_audiohook_unlock(audiohook);
			if (ast_channel_is_bridged(chan)) {
				ast_channel_set_unbridged_nolock(chan, 1);
			}
			continue;
		}
		ast_audiohook_write_frame(audiohook, direction, middle_frame);
		ast_audiohook_unlock(audiohook);
	}
	AST_LIST_TRAVERSE_SAFE_END;

	/* If this frame is being written out to the channel then we need to use whisper sources */
	if (!AST_LIST_EMPTY(&audiohook_list->whisper_list)) {
		int i = 0;
		short read_buf[samples], combine_buf[samples], *data1 = NULL, *data2 = NULL;
		memset(&combine_buf, 0, sizeof(combine_buf));
		AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->whisper_list, audiohook, list) {
			struct ast_slinfactory *factory = (direction == AST_AUDIOHOOK_DIRECTION_READ ? &audiohook->read_factory : &audiohook->write_factory);
			ast_audiohook_lock(audiohook);
			if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
				AST_LIST_REMOVE_CURRENT(list);
				removed = 1;
				ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
				ast_audiohook_unlock(audiohook);
				if (ast_channel_is_bridged(chan)) {
					ast_channel_set_unbridged_nolock(chan, 1);
				}
				continue;
			}
			if (ast_slinfactory_available(factory) >= samples && ast_slinfactory_read(factory, read_buf, samples)) {
				/* Take audio from this whisper source and combine it into our main buffer */
				for (i = 0, data1 = combine_buf, data2 = read_buf; i < samples; i++, data1++, data2++) {
					ast_slinear_saturated_add(data1, data2);
				}
			}
			ast_audiohook_unlock(audiohook);
		}
		AST_LIST_TRAVERSE_SAFE_END;
		/* We take all of the combined whisper sources and combine them into the audio being written out */
		for (i = 0, data1 = middle_frame->data.ptr, data2 = combine_buf; i < samples; i++, data1++, data2++) {
			ast_slinear_saturated_add(data1, data2);
		}
		middle_frame_manipulated = 1;
	}

	/* Pass off frame to manipulate audiohooks */
	if (!AST_LIST_EMPTY(&audiohook_list->manipulate_list)) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&audiohook_list->manipulate_list, audiohook, list) {
			ast_audiohook_lock(audiohook);
			if (audiohook->status != AST_AUDIOHOOK_STATUS_RUNNING) {
				AST_LIST_REMOVE_CURRENT(list);
				removed = 1;
				ast_audiohook_update_status(audiohook, AST_AUDIOHOOK_STATUS_DONE);
				ast_audiohook_unlock(audiohook);
				/* We basically drop all of our links to the manipulate audiohook and prod it to do it's own destructive things */
				audiohook->manipulate_callback(audiohook, chan, NULL, direction);
				if (ast_channel_is_bridged(chan)) {
					ast_channel_set_unbridged_nolock(chan, 1);
				}
				continue;
			}
			/* Feed in frame to manipulation. */
			if (!audiohook->manipulate_callback(audiohook, chan, middle_frame, direction)) {
				/* If the manipulation fails then the frame will be returned in its original state.
				 * Since there are potentially more manipulator callbacks in the list, no action should
				 * be taken here to exit early. */
				 middle_frame_manipulated = 1;
			}
			ast_audiohook_unlock(audiohook);
		}
		AST_LIST_TRAVERSE_SAFE_END;
	}

	/* ---Part_3: Decide what to do with the end_frame (whether to transcode or not) */
	if (middle_frame_manipulated) {
		if (!(end_frame = audiohook_list_translate_to_native(audiohook_list, direction, middle_frame, start_frame->subclass.format))) {
			/* translation failed, so just pass back the input frame */
			end_frame = start_frame;
		}
	} else {
		end_frame = start_frame;
	}
	/* clean up our middle_frame if required */
	if (middle_frame != end_frame) {
		ast_frfree(middle_frame);
		middle_frame = NULL;
	}

	/* Before returning, if an audiohook got removed, reset samplerate compatibility */
	if (removed) {
		audiohook_list_set_samplerate_compatibility(audiohook_list);
	}

	return end_frame;
}

int ast_audiohook_write_list_empty(struct ast_audiohook_list *audiohook_list)
{
	return !audiohook_list
		|| (AST_LIST_EMPTY(&audiohook_list->spy_list)
			&& AST_LIST_EMPTY(&audiohook_list->whisper_list)
			&& AST_LIST_EMPTY(&audiohook_list->manipulate_list));
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
	if (frame->frametype == AST_FRAME_VOICE) {
		return audio_audiohook_write_list(chan, audiohook_list, direction, frame);
	} else if (frame->frametype == AST_FRAME_DTMF) {
		return dtmf_audiohook_write_list(chan, audiohook_list, direction, frame);
	} else {
		return frame;
	}
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

	if (!ast_channel_audiohooks(chan)) {
		return -1;
	}

	switch (type) {
		case AST_AUDIOHOOK_TYPE_SPY:
			AST_LIST_TRAVERSE(&ast_channel_audiohooks(chan)->spy_list, ah, list) {
				if (!strcmp(ah->source, source)) {
					count++;
				}
			}
			break;
		case AST_AUDIOHOOK_TYPE_WHISPER:
			AST_LIST_TRAVERSE(&ast_channel_audiohooks(chan)->whisper_list, ah, list) {
				if (!strcmp(ah->source, source)) {
					count++;
				}
			}
			break;
		case AST_AUDIOHOOK_TYPE_MANIPULATE:
			AST_LIST_TRAVERSE(&ast_channel_audiohooks(chan)->manipulate_list, ah, list) {
				if (!strcmp(ah->source, source)) {
					count++;
				}
			}
			break;
		default:
			ast_debug(1, "Invalid audiohook type supplied, (%u)\n", type);
			return -1;
	}

	return count;
}

/* Count number of channel audiohooks by type that are running */
int ast_channel_audiohook_count_by_source_running(struct ast_channel *chan, const char *source, enum ast_audiohook_type type)
{
	int count = 0;
	struct ast_audiohook *ah = NULL;
	if (!ast_channel_audiohooks(chan))
		return -1;

	switch (type) {
		case AST_AUDIOHOOK_TYPE_SPY:
			AST_LIST_TRAVERSE(&ast_channel_audiohooks(chan)->spy_list, ah, list) {
				if ((!strcmp(ah->source, source)) && (ah->status == AST_AUDIOHOOK_STATUS_RUNNING))
					count++;
			}
			break;
		case AST_AUDIOHOOK_TYPE_WHISPER:
			AST_LIST_TRAVERSE(&ast_channel_audiohooks(chan)->whisper_list, ah, list) {
				if ((!strcmp(ah->source, source)) && (ah->status == AST_AUDIOHOOK_STATUS_RUNNING))
					count++;
			}
			break;
		case AST_AUDIOHOOK_TYPE_MANIPULATE:
			AST_LIST_TRAVERSE(&ast_channel_audiohooks(chan)->manipulate_list, ah, list) {
				if ((!strcmp(ah->source, source)) && (ah->status == AST_AUDIOHOOK_STATUS_RUNNING))
					count++;
			}
			break;
		default:
			ast_debug(1, "Invalid audiohook type supplied, (%u)\n", type);
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
	ast_audiohook_init(&audiohook_volume->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "Volume", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
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
	if (!ast_channel_audiohooks(chan)) {
		ast_channel_unlock(chan);
		return -1;
	}

	audiohook = find_audiohook_by_source(ast_channel_audiohooks(chan), source);

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
