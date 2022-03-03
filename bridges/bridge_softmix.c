/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2011, Digium, Inc.
 *
 * Joshua Colp <jcolp@digium.com>
 * David Vossel <dvossel@digium.com>
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
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup bridges
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <math.h>

#include "asterisk/stream.h"
#include "asterisk/test.h"
#include "asterisk/vector.h"
#include "asterisk/message.h"
#include "bridge_softmix/include/bridge_softmix_internal.h"

/*! The minimum sample rate of the bridge. */
#define SOFTMIX_MIN_SAMPLE_RATE 8000	/* 8 kHz sample rate */

/*! \brief Interval at which mixing will take place. Valid options are 10, 20, and 40. */
#define DEFAULT_SOFTMIX_INTERVAL 20

/*! \brief Size of the buffer used for sample manipulation */
#define SOFTMIX_DATALEN(rate, interval) ((rate/50) * (interval / 10))

/*! \brief Number of samples we are dealing with */
#define SOFTMIX_SAMPLES(rate, interval) (SOFTMIX_DATALEN(rate, interval) / 2)

/*! \brief Number of mixing iterations to perform between gathering statistics. */
#define SOFTMIX_STAT_INTERVAL 100

/*!
 * \brief Default time in ms of silence necessary to declare talking stopped by the bridge.
 *
 * \details
 * This is the time at which a channel's own audio will stop getting
 * mixed out of its own write audio stream because it is no longer talking.
 */
#define DEFAULT_SOFTMIX_SILENCE_THRESHOLD 2500

/*! Default minimum average magnitude threshold to determine talking by the DSP. */
#define DEFAULT_SOFTMIX_TALKING_THRESHOLD 160

#define SOFTBRIDGE_VIDEO_DEST_PREFIX "softbridge_dest"
#define SOFTBRIDGE_VIDEO_DEST_LEN strlen(SOFTBRIDGE_VIDEO_DEST_PREFIX)
#define SOFTBRIDGE_VIDEO_DEST_SEPARATOR '_'

struct softmix_remb_collector {
	/*! The frame which will be given to each source stream */
	struct ast_frame frame;
	/*! The REMB to send to the source which is collecting REMB reports */
	struct ast_rtp_rtcp_feedback feedback;
	/*! The maximum bitrate (A single precision floating point is big enough) */
	float bitrate;
};

struct softmix_stats {
	/*! Each index represents a sample rate used above the internal rate. */
	unsigned int sample_rates[16];
	/*! Each index represents the number of channels using the same index in the sample_rates array.  */
	unsigned int num_channels[16];
	/*! The number of channels above the internal sample rate */
	unsigned int num_above_internal_rate;
	/*! The number of channels above the maximum sample rate */
	unsigned int num_above_maximum_rate;
	/*! The number of channels at the internal sample rate */
	unsigned int num_at_internal_rate;
	/*! The absolute highest sample rate preferred by any channel in the bridge */
	unsigned int highest_supported_rate;
	/*! Is the sample rate locked by the bridge, if so what is that rate.*/
	unsigned int locked_rate;
	/*! The maximum sample rate the bridge may use */
	unsigned int maximum_rate;
};

struct softmix_translate_helper_entry {
	int num_times_requested; /*!< Once this entry is no longer requested, free the trans_pvt
								  and re-init if it was usable. */
	struct ast_format *dst_format; /*!< The destination format for this helper */
	struct ast_trans_pvt *trans_pvt; /*!< the translator for this slot. */
	struct ast_frame *out_frame; /*!< The output frame from the last translation */
	AST_LIST_ENTRY(softmix_translate_helper_entry) entry;
};

struct softmix_translate_helper {
	struct ast_format *slin_src; /*!< the source format expected for all the translators */
	AST_LIST_HEAD_NOLOCK(, softmix_translate_helper_entry) entries;
};

static struct softmix_translate_helper_entry *softmix_translate_helper_entry_alloc(struct ast_format *dst)
{
	struct softmix_translate_helper_entry *entry;
	if (!(entry = ast_calloc(1, sizeof(*entry)))) {
		return NULL;
	}
	entry->dst_format = ao2_bump(dst);
	/* initialize this to one so that the first time through the cleanup code after
	   allocation it won't be removed from the entry list */
	entry->num_times_requested = 1;
	return entry;
}

static void *softmix_translate_helper_free_entry(struct softmix_translate_helper_entry *entry)
{
	ao2_cleanup(entry->dst_format);

	if (entry->trans_pvt) {
		ast_translator_free_path(entry->trans_pvt);
	}
	if (entry->out_frame) {
		ast_frfree(entry->out_frame);
	}
	ast_free(entry);
	return NULL;
}

static void softmix_translate_helper_init(struct softmix_translate_helper *trans_helper, unsigned int sample_rate)
{
	memset(trans_helper, 0, sizeof(*trans_helper));
	trans_helper->slin_src = ast_format_cache_get_slin_by_rate(sample_rate);
}

static void softmix_translate_helper_destroy(struct softmix_translate_helper *trans_helper)
{
	struct softmix_translate_helper_entry *entry;

	while ((entry = AST_LIST_REMOVE_HEAD(&trans_helper->entries, entry))) {
		softmix_translate_helper_free_entry(entry);
	}
}

static void softmix_translate_helper_change_rate(struct softmix_translate_helper *trans_helper, unsigned int sample_rate)
{
	struct softmix_translate_helper_entry *entry;

	trans_helper->slin_src = ast_format_cache_get_slin_by_rate(sample_rate);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&trans_helper->entries, entry, entry) {
		if (entry->trans_pvt) {
			ast_translator_free_path(entry->trans_pvt);
			if (!(entry->trans_pvt = ast_translator_build_path(entry->dst_format, trans_helper->slin_src))) {
				AST_LIST_REMOVE_CURRENT(entry);
				entry = softmix_translate_helper_free_entry(entry);
			}
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
}

/*!
 * \internal
 * \brief Get the next available audio on the softmix channel's read stream
 * and determine if it should be mixed out or not on the write stream.
 *
 * \retval pointer to buffer containing the exact number of samples requested on success.
 * \retval NULL if no samples are present
 */
static int16_t *softmix_process_read_audio(struct softmix_channel *sc, unsigned int num_samples)
{
	if ((ast_slinfactory_available(&sc->factory) >= num_samples) &&
		ast_slinfactory_read(&sc->factory, sc->our_buf, num_samples)) {
		sc->have_audio = 1;
		return sc->our_buf;
	}
	sc->have_audio = 0;
	return NULL;
}

/*!
 * \internal
 * \brief Process a softmix channel's write audio
 *
 * \details This function will remove the channel's talking from its own audio if present and
 * possibly even do the channel's write translation for it depending on how many other
 * channels use the same write format.
 */
static void softmix_process_write_audio(struct softmix_translate_helper *trans_helper,
	struct ast_format *raw_write_fmt,
	struct softmix_channel *sc, unsigned int default_sample_size)
{
	struct softmix_translate_helper_entry *entry = NULL;
	int i;

	/* If we provided any audio then take it out while in slinear format. */
	if (sc->have_audio && !sc->binaural) {
		for (i = 0; i < sc->write_frame.samples; i++) {
			ast_slinear_saturated_subtract(&sc->final_buf[i], &sc->our_buf[i]);
		}
		/* check to see if any entries exist for the format. if not we'll want
		   to remove it during cleanup */
		AST_LIST_TRAVERSE(&trans_helper->entries, entry, entry) {
			if (ast_format_cmp(entry->dst_format, raw_write_fmt) == AST_FORMAT_CMP_EQUAL) {
				++entry->num_times_requested;
				break;
			}
		}
		/* do not do any special write translate optimization if we had to make
		 * a special mix for them to remove their own audio. */
		return;
	} else if (sc->have_audio && sc->binaural > 0) {
		/*
		 * Binaural audio requires special saturated substract since we have two
		 * audio signals per channel now.
		 */
		softmix_process_write_binaural_audio(sc, default_sample_size);
		return;
	}

	/* Attempt to optimize channels using the same translation path/codec. Build a list of entries
	   of translation paths and track the number of references for each type. Each one of the same
	   type should be able to use the same out_frame. Since the optimization is only necessary for
	   multiple channels (>=2) using the same codec make sure resources are allocated only when
	   needed and released when not (see also softmix_translate_helper_cleanup */
	AST_LIST_TRAVERSE(&trans_helper->entries, entry, entry) {
		if (sc->binaural != 0) {
			continue;
		}
		if (ast_format_cmp(entry->dst_format, raw_write_fmt) == AST_FORMAT_CMP_EQUAL) {
			entry->num_times_requested++;
		} else {
			continue;
		}
		if (!entry->trans_pvt && (entry->num_times_requested > 1)) {
			entry->trans_pvt = ast_translator_build_path(entry->dst_format, trans_helper->slin_src);
		}
		if (entry->trans_pvt && !entry->out_frame) {
			entry->out_frame = ast_translate(entry->trans_pvt, &sc->write_frame, 0);
		}
		if (entry->out_frame && entry->out_frame->frametype == AST_FRAME_VOICE
				&& entry->out_frame->datalen < MAX_DATALEN) {
			ao2_replace(sc->write_frame.subclass.format, entry->out_frame->subclass.format);
			memcpy(sc->final_buf, entry->out_frame->data.ptr, entry->out_frame->datalen);
			sc->write_frame.datalen = entry->out_frame->datalen;
			sc->write_frame.samples = entry->out_frame->samples;
		}
		break;
	}

	/* add new entry into list if this format destination was not matched. */
	if (!entry && (entry = softmix_translate_helper_entry_alloc(raw_write_fmt))) {
		AST_LIST_INSERT_HEAD(&trans_helper->entries, entry, entry);
	}
}

static void softmix_translate_helper_cleanup(struct softmix_translate_helper *trans_helper)
{
	struct softmix_translate_helper_entry *entry;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&trans_helper->entries, entry, entry) {
		/* if it hasn't been requested then remove it */
		if (!entry->num_times_requested) {
			AST_LIST_REMOVE_CURRENT(entry);
			softmix_translate_helper_free_entry(entry);
			continue;
		}

		if (entry->out_frame) {
			ast_frfree(entry->out_frame);
			entry->out_frame = NULL;
		}

		/* nothing is optimized for a single path reference, so there is
		   no reason to continue to hold onto the codec */
		if (entry->num_times_requested == 1 && entry->trans_pvt) {
			ast_translator_free_path(entry->trans_pvt);
			entry->trans_pvt = NULL;
		}

		/* for each iteration (a mixing run) in the bridge softmix thread the number
		   of references to a given entry is recalculated, so reset the number of
		   times requested */
		entry->num_times_requested = 0;
	}
	AST_LIST_TRAVERSE_SAFE_END;
}

static void set_softmix_bridge_data(int rate, int interval, struct ast_bridge_channel *bridge_channel, int reset, int set_binaural, int binaural_pos_id, int is_announcement)
{
	struct softmix_channel *sc = bridge_channel->tech_pvt;
	struct ast_format *slin_format;
	int setup_fail;

#ifdef BINAURAL_RENDERING
	if (interval != BINAURAL_MIXING_INTERVAL) {
		interval = BINAURAL_MIXING_INTERVAL;
	}
#endif

	/* The callers have already ensured that sc is never NULL. */
	ast_assert(sc != NULL);

	slin_format = ast_format_cache_get_slin_by_rate(rate);

	ast_mutex_lock(&sc->lock);
	if (reset) {
		ast_slinfactory_destroy(&sc->factory);
		ast_dsp_free(sc->dsp);
	}

	/* Setup write frame parameters */
	sc->write_frame.frametype = AST_FRAME_VOICE;
	/*
	 * NOTE: The write_frame format holds a reference because translation
	 * could be needed and the format changed to the translated format
	 * for the channel.  The translated format may not be a
	 * static cached format.
	 */
	ao2_replace(sc->write_frame.subclass.format, slin_format);
	sc->write_frame.data.ptr = sc->final_buf;
	sc->write_frame.datalen = SOFTMIX_DATALEN(rate, interval);
	sc->write_frame.samples = SOFTMIX_SAMPLES(rate, interval);

	/* We will store the rate here cause we need to set the data again when a channel is unsuspended */
	sc->rate = rate;

	/* If the channel will contain binaural data we will set a identifier in the channel
	 * if set_binaural == -1 this is just a sample rate update, will ignore it. */
	if (set_binaural == 1) {
		sc->binaural = 1;
	} else if (set_binaural == 0) {
		sc->binaural = 0;
	}

	/* Setting the binaural position. This doesn't require a change of the overlaying channel infos
	 * and doesn't have to be done if we just updating sample rates. */
	if (binaural_pos_id != -1) {
		sc->binaural_pos = binaural_pos_id;
	}
	if (is_announcement != -1) {
		sc->is_announcement = is_announcement;
	}

	/*
	 * NOTE: The read_slin_format does not hold a reference because it
	 * will always be a signed linear format.
	 */
	sc->read_slin_format = slin_format;

	/* Setup smoother */
	setup_fail = ast_slinfactory_init_with_format(&sc->factory, slin_format);

	/* set new read and write formats on channel. */
	ast_channel_lock(bridge_channel->chan);
	setup_fail |= ast_set_read_format_path(bridge_channel->chan,
		ast_channel_rawreadformat(bridge_channel->chan), slin_format);
	ast_channel_unlock(bridge_channel->chan);

	/* If channel contains binaural data we will set it here for the trans_pvt. */
	if (set_binaural == 1 || (set_binaural == -1 && sc->binaural == 1)) {
		setup_fail |= ast_set_write_format_interleaved_stereo(bridge_channel->chan, slin_format);
	} else if (set_binaural == 0) {
		setup_fail |= ast_set_write_format(bridge_channel->chan, slin_format);
	}

	/* set up new DSP.  This is on the read side only right before the read frame enters the smoother.  */
	sc->dsp = ast_dsp_new_with_rate(rate);
	if (setup_fail || !sc->dsp) {
		/* Bad news.  Could not setup the channel for softmix. */
		ast_mutex_unlock(&sc->lock);
		ast_bridge_channel_leave_bridge(bridge_channel, BRIDGE_CHANNEL_STATE_END, 0);
		return;
	}

	/* we want to aggressively detect silence to avoid feedback */
	if (bridge_channel->tech_args.talking_threshold) {
		ast_dsp_set_threshold(sc->dsp, bridge_channel->tech_args.talking_threshold);
	} else {
		ast_dsp_set_threshold(sc->dsp, DEFAULT_SOFTMIX_TALKING_THRESHOLD);
	}

	ast_mutex_unlock(&sc->lock);
}

/*!
 * \internal
 * \brief Poke the mixing thread in case it is waiting for an active channel.
 * \since 12.0.0
 *
 * \param softmix_data Bridge mixing data.
 */
static void softmix_poke_thread(struct softmix_bridge_data *softmix_data)
{
	ast_mutex_lock(&softmix_data->lock);
	ast_cond_signal(&softmix_data->cond);
	ast_mutex_unlock(&softmix_data->lock);
}

/*! \brief Function called when a channel is unsuspended from the bridge */
static void softmix_bridge_unsuspend(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
#ifdef BINAURAL_RENDERING
	struct softmix_channel *sc = bridge_channel->tech_pvt;
	if (sc->binaural) {
		/* Restore some usefull data if it was a binaural channel */
		struct ast_format *slin_format;

		slin_format = ast_format_cache_get_slin_by_rate(sc->rate);
		ast_set_write_format_interleaved_stereo(bridge_channel->chan, slin_format);
	}
#endif
	if (bridge->tech_pvt) {
		softmix_poke_thread(bridge->tech_pvt);
	}
}

/*!
 * \brief Determine if a stream is a video source stream.
 *
 * \param stream The stream to test
 * \retval 1 The stream is a video source
 * \retval 0 The stream is not a video source
 */
static int is_video_source(const struct ast_stream *stream)
{
	if (ast_stream_get_state(stream) != AST_STREAM_STATE_REMOVED
		&& ast_stream_get_type(stream) == AST_MEDIA_TYPE_VIDEO
		&& strncmp(ast_stream_get_name(stream), SOFTBRIDGE_VIDEO_DEST_PREFIX,
			SOFTBRIDGE_VIDEO_DEST_LEN)) {
		return 1;
	}

	return 0;
}

/*!
 * \brief Determine if a stream is a video destination stream.
 *
 * A source channel name can be provided to narrow this to a destination stream
 * for a particular source channel. Further, a source stream name can be provided
 * to narrow this to a particular source stream's destination. However, empty strings
 * can be provided to match any destination video stream, regardless of source channel
 * or source stream.
 *
 * \param stream The stream to test
 * \param source_channel_name The name of a source video channel to match
 * \param source_channel_stream_position The position of the video on the source channel
 * \retval 1 The stream is a video destination stream
 * \retval 0 The stream is not a video destination stream
 */
static int is_video_dest(const struct ast_stream *stream, const char *source_channel_name,
	int source_channel_stream_position)
{
	char *dest_video_name;
	size_t dest_video_name_len;

	if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED
		|| ast_stream_get_type(stream) != AST_MEDIA_TYPE_VIDEO) {
		return 0;
	}

	dest_video_name_len = SOFTBRIDGE_VIDEO_DEST_LEN + 1;
	if (!ast_strlen_zero(source_channel_name)) {
		dest_video_name_len += strlen(source_channel_name) + 1;
		if (source_channel_stream_position != -1) {
			dest_video_name_len += 11;
		}

		dest_video_name = ast_alloca(dest_video_name_len);
		if (source_channel_stream_position != -1) {
			/* We are looking for an exact stream position */
			snprintf(dest_video_name, dest_video_name_len, "%s%c%s%c%d",
				SOFTBRIDGE_VIDEO_DEST_PREFIX, SOFTBRIDGE_VIDEO_DEST_SEPARATOR,
				source_channel_name, SOFTBRIDGE_VIDEO_DEST_SEPARATOR,
				source_channel_stream_position);
			return !strcmp(ast_stream_get_name(stream), dest_video_name);
		}
		snprintf(dest_video_name, dest_video_name_len, "%s%c%s",
			SOFTBRIDGE_VIDEO_DEST_PREFIX, SOFTBRIDGE_VIDEO_DEST_SEPARATOR,
			source_channel_name);
	} else {
		dest_video_name = SOFTBRIDGE_VIDEO_DEST_PREFIX;
	}

	return !strncmp(ast_stream_get_name(stream), dest_video_name, dest_video_name_len - 1);
}

static int append_source_stream(struct ast_stream_topology *dest,
	const char *channel_name, const char *sdp_label,
	struct ast_stream *stream, int index)
{
	char *stream_clone_name = NULL;
	struct ast_stream *stream_clone;

	/* We use the stream topology index for the stream to uniquely identify and recognize it.
	 * This is guaranteed to remain the same across renegotiation of the source channel and
	 * ensures that the stream name is unique.
	 */
	if (ast_asprintf(&stream_clone_name, "%s%c%s%c%d", SOFTBRIDGE_VIDEO_DEST_PREFIX,
		SOFTBRIDGE_VIDEO_DEST_SEPARATOR, channel_name, SOFTBRIDGE_VIDEO_DEST_SEPARATOR,
		index) < 0) {
		return -1;
	}

	stream_clone = ast_stream_clone(stream, stream_clone_name);
	ast_free(stream_clone_name);
	if (!stream_clone) {
		return -1;
	}

	/* Sends an "a:label" attribute in the SDP for participant event correlation */
	if (!ast_strlen_zero(sdp_label)) {
		ast_stream_set_metadata(stream_clone, "SDP:LABEL", sdp_label);
	}

	/* We will be sending them a stream and not expecting anything in return */
	ast_stream_set_state(stream_clone, AST_STREAM_STATE_SENDONLY);

	if (ast_stream_topology_append_stream(dest, stream_clone) < 0) {
		ast_stream_free(stream_clone);
		return -1;
	}

	return 0;
}


static int append_source_streams(struct ast_stream_topology *dest,
	const char *channel_name, const char *sdp_label,
	const struct ast_stream_topology *source)
{
	int i;

	for (i = 0; i < ast_stream_topology_get_count(source); ++i) {
		struct ast_stream *stream;

		stream = ast_stream_topology_get_stream(source, i);

		if (!is_video_source(stream)) {
			continue;
		}

		if (append_source_stream(dest, channel_name, sdp_label, stream, i)) {
			return -1;
		}
	}

	return 0;
}

static int append_all_streams(struct ast_stream_topology *dest,
	const struct ast_stream_topology *source)
{
	int i;
	int dest_index = 0;

	for (i = 0; i < ast_stream_topology_get_count(source); ++i) {
		struct ast_stream *clone;
		int added = 0;

		clone = ast_stream_clone(ast_stream_topology_get_stream(source, i), NULL);
		if (!clone) {
			return -1;
		}

		/* If we can reuse an existing removed stream then do so */
		while (dest_index < ast_stream_topology_get_count(dest)) {
			struct ast_stream *stream = ast_stream_topology_get_stream(dest, dest_index);

			dest_index++;

			if (ast_stream_get_state(stream) == AST_STREAM_STATE_REMOVED) {
				/* This cannot fail because dest_index - 1 is less than the
				 * current count in dest. */
				ast_stream_topology_set_stream(dest, dest_index - 1, clone);
				added = 1;
				break;
			}
		}

		/* If no removed stream exists that we took the place of append the stream */
		if (!added && ast_stream_topology_append_stream(dest, clone) < 0) {
			ast_stream_free(clone);
			return -1;
		}
	}

	return 0;
}

/*!
 * \brief Issue channel stream topology change requests.
 *
 * When in SFU mode, each participant needs to be able to
 * send video directly to other participants in the bridge.
 * This means that all participants need to have their topologies
 * updated. The joiner needs to have destination streams for
 * all current participants, and the current participants need
 * to have destinations streams added for the joiner's sources.
 *
 * \param bridge
 * \param joiner The channel that is joining the softmix bridge
 */
static void sfu_topologies_on_join(struct ast_bridge *bridge,
	struct ast_bridge_channel *joiner)
{
	RAII_VAR(struct ast_stream_topology *, joiner_video, NULL, ast_stream_topology_free);
	struct ast_bridge_channels_list *participants = &bridge->channels;
	struct ast_bridge_channel *participant;
	int res;
	struct softmix_channel *sc;
	SCOPE_ENTER(3, "%s: \n", ast_channel_name(joiner->chan));

	joiner_video = ast_stream_topology_alloc();
	if (!joiner_video) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Couldn't alloc topology\n", ast_channel_name(joiner->chan));
	}

	sc = joiner->tech_pvt;

	ast_channel_lock(joiner->chan);
	res = append_source_streams(joiner_video, ast_channel_name(joiner->chan),
		bridge->softmix.send_sdp_label ? ast_channel_uniqueid(joiner->chan) : NULL,
		ast_channel_get_stream_topology(joiner->chan));
	sc->topology = ast_stream_topology_clone(ast_channel_get_stream_topology(joiner->chan));
	ast_channel_unlock(joiner->chan);

	if (res || !sc->topology) {
		SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s: Couldn't append source streams\n", ast_channel_name(joiner->chan));
	}

	AST_LIST_TRAVERSE(participants, participant, entry) {
		if (participant == joiner) {
			continue;
		}
		ast_trace(-1, "%s: Appending existing participant %s\n", ast_channel_name(joiner->chan),
			ast_channel_name(participant->chan));
		ast_channel_lock(participant->chan);
		res = append_source_streams(sc->topology, ast_channel_name(participant->chan),
			bridge->softmix.send_sdp_label ? ast_channel_uniqueid(participant->chan) : NULL,
			ast_channel_get_stream_topology(participant->chan));
		ast_channel_unlock(participant->chan);
		if (res) {
			SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s/%s: Couldn't append source streams\n",
				ast_channel_name(participant->chan), ast_channel_name(joiner->chan));
		}
	}

	ast_trace(-1, "%s: Requesting topology change.\n", ast_channel_name(joiner->chan));
	res = ast_channel_request_stream_topology_change(joiner->chan, sc->topology, NULL);
	if (res) {
		/* There are conditions under which this is expected so no need to log an ERROR. */
		SCOPE_EXIT_RTN("%s: Couldn't request topology change\n", ast_channel_name(joiner->chan));
	}

	AST_LIST_TRAVERSE(participants, participant, entry) {
		if (participant == joiner) {
			continue;
		}

		sc = participant->tech_pvt;
		ast_trace(-1, "%s: Appending joiner %s\n", ast_channel_name(participant->chan),
			ast_channel_name(joiner->chan));

		if (append_all_streams(sc->topology, joiner_video)) {
			SCOPE_EXIT_LOG_RTN(LOG_ERROR, "%s/%s: Couldn't append streams\n",
				ast_channel_name(participant->chan), ast_channel_name(joiner->chan));
		}
		ast_trace(-1, "%s: Requesting topology change\n", ast_channel_name(participant->chan));
		res = ast_channel_request_stream_topology_change(participant->chan, sc->topology, NULL);
		if (res) {
			ast_trace(-1, "%s/%s: Couldn't request topology change\n",
				ast_channel_name(participant->chan), ast_channel_name(joiner->chan));
		}
	}

	SCOPE_EXIT();
}

/*! \brief Function called when a channel is joined into the bridge */
static int softmix_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct softmix_channel *sc;
	struct softmix_bridge_data *softmix_data;
	int set_binaural = 0;
	/*
	 * If false, the channel will be convolved, but since it is a non stereo channel, output
	 * will be mono.
	 */
	int skip_binaural_output = 1;
	int pos_id;
	int is_announcement = 0;
	int samplerate_change;
	SCOPE_ENTER(3, "%s:\n", ast_channel_name(bridge_channel->chan));

	softmix_data = bridge->tech_pvt;
	if (!softmix_data) {
		SCOPE_EXIT_RTN_VALUE(-1, "No tech_pvt\n");
	}

	/* Create a new softmix_channel structure and allocate various things on it */
	if (!(sc = ast_calloc(1, sizeof(*sc)))) {
		SCOPE_EXIT_RTN_VALUE(-1, "Couldn't alloc tech_pvt\n");
	}

	samplerate_change = softmix_data->internal_rate;
	pos_id = -1;
	if (bridge->softmix.binaural_active) {
		if (strncmp(ast_channel_name(bridge_channel->chan), "CBAnn", 5) != 0) {
			set_binaural = ast_format_get_channel_count(bridge_channel->write_format) > 1 ? 1 : 0;
			if (set_binaural) {
				softmix_data->internal_rate = samplerate_change;
			}
			skip_binaural_output = 0;
		} else {
			is_announcement = 1;
		}
		if (set_binaural) {
			softmix_data->convolve.binaural_active = 1;
		}
		if (!skip_binaural_output)	{
			pos_id = set_binaural_data_join(&softmix_data->convolve, softmix_data->default_sample_size);
			if (pos_id == -1) {
				ast_log(LOG_ERROR, "Bridge %s: Failed to join channel %s. "
						"Could not allocate enough memory.\n", bridge->uniqueid,
						ast_channel_name(bridge_channel->chan));
				ast_free(sc);
				SCOPE_EXIT_RTN_VALUE(-1, "Couldn't do binaural join\n");
			}
		}
	}

	/* Can't forget the lock */
	ast_mutex_init(&sc->lock);

	/* Can't forget to record our pvt structure within the bridged channel structure */
	bridge_channel->tech_pvt = sc;

	set_softmix_bridge_data(softmix_data->internal_rate,
		softmix_data->internal_mixing_interval
			? softmix_data->internal_mixing_interval
			: DEFAULT_SOFTMIX_INTERVAL,
		bridge_channel, 0, set_binaural, pos_id, is_announcement);

	if (bridge->softmix.video_mode.mode == AST_BRIDGE_VIDEO_MODE_SFU) {
		sfu_topologies_on_join(bridge, bridge_channel);
	}

	/* Complete any active hold before entering, or transitioning to softmix. */
	if (ast_channel_hold_state(bridge_channel->chan) == AST_CONTROL_HOLD) {
		ast_debug(1, "Channel %s simulating UNHOLD for bridge softmix join.\n",
			ast_channel_name(bridge_channel->chan));
		ast_indicate(bridge_channel->chan, AST_CONTROL_UNHOLD);
	}

	softmix_poke_thread(softmix_data);
	SCOPE_EXIT_RTN_VALUE(0);
}

static int remove_destination_streams(struct ast_stream_topology *topology,
	const char *channel_name)
{
	int i;
	int stream_removed = 0;

	for (i = 0; i < ast_stream_topology_get_count(topology); ++i) {
		struct ast_stream *stream;

		stream = ast_stream_topology_get_stream(topology, i);

		if (is_video_dest(stream, channel_name, -1)) {
			ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
			stream_removed = 1;
		}
	}
	return stream_removed;
}

static int sfu_topologies_on_leave(struct ast_bridge_channel *leaver, struct ast_bridge_channels_list *participants)
{
	struct ast_bridge_channel *participant;
	struct softmix_channel *sc;

	AST_LIST_TRAVERSE(participants, participant, entry) {
		sc = participant->tech_pvt;
		if (!remove_destination_streams(sc->topology, ast_channel_name(leaver->chan))) {
			continue;
		}
		ast_channel_request_stream_topology_change(participant->chan, sc->topology, NULL);
	}

	sc = leaver->tech_pvt;
	if (remove_destination_streams(sc->topology, "")) {
		ast_channel_request_stream_topology_change(leaver->chan, sc->topology, NULL);
	}

	return 0;
}

/*! \brief Function called when a channel leaves the bridge */
static void softmix_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct softmix_channel *sc;
	struct softmix_bridge_data *softmix_data;

	softmix_data = bridge->tech_pvt;
	sc = bridge_channel->tech_pvt;
	if (!sc) {
		return;
	}

	if (bridge->softmix.video_mode.mode == AST_BRIDGE_VIDEO_MODE_SFU) {
		sfu_topologies_on_leave(bridge_channel, &bridge->channels);
	}

	if (bridge->softmix.binaural_active) {
		if (sc->binaural) {
			set_binaural_data_leave(&softmix_data->convolve, sc->binaural_pos,
					softmix_data->default_sample_size);
		}
	}

	bridge_channel->tech_pvt = NULL;

	ast_stream_topology_free(sc->topology);

	ao2_cleanup(sc->remb_collector);

	AST_VECTOR_FREE(&sc->video_sources);

	/* Drop mutex lock */
	ast_mutex_destroy(&sc->lock);

	/* Drop the factory */
	ast_slinfactory_destroy(&sc->factory);

	/* Drop any formats on the frames */
	ao2_cleanup(sc->write_frame.subclass.format);

	/* Drop the DSP */
	ast_dsp_free(sc->dsp);

	/* Eep! drop ourselves */
	ast_free(sc);
}

static void softmix_pass_video_top_priority(struct ast_bridge *bridge, struct ast_frame *frame)
{
	struct ast_bridge_channel *cur;

	AST_LIST_TRAVERSE(&bridge->channels, cur, entry) {
		if (cur->suspended) {
			continue;
		}
		if (ast_bridge_is_video_src(bridge, cur->chan) == 1) {
			ast_bridge_channel_queue_frame(cur, frame);
			break;
		}
	}
}

/*!
 * \internal
 * \brief Determine what to do with a video frame.
 * \since 12.0.0
 *
 * \param bridge Which bridge is getting the frame
 * \param bridge_channel Which channel is writing the frame.
 * \param frame What is being written.
 */
static void softmix_bridge_write_video(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct softmix_channel *sc;
	int video_src_priority;

	/* Determine if the video frame should be distributed or not */
	switch (bridge->softmix.video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
		break;
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
		video_src_priority = ast_bridge_is_video_src(bridge, bridge_channel->chan);
		if (video_src_priority == 1) {
			/* Pass to me and everyone else. */
			ast_bridge_queue_everyone_else(bridge, NULL, frame);
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
		sc = bridge_channel->tech_pvt;
		ast_mutex_lock(&sc->lock);
		ast_bridge_update_talker_src_video_mode(bridge, bridge_channel->chan,
			sc->video_talker.energy_average,
			frame->subclass.frame_ending);
		ast_mutex_unlock(&sc->lock);
		video_src_priority = ast_bridge_is_video_src(bridge, bridge_channel->chan);
		if (video_src_priority == 1) {
			int num_src = ast_bridge_number_video_src(bridge);
			int echo = num_src > 1 ? 0 : 1;

			ast_bridge_queue_everyone_else(bridge, echo ? NULL : bridge_channel, frame);
		} else if (video_src_priority == 2) {
			softmix_pass_video_top_priority(bridge, frame);
		}
		break;
	case AST_BRIDGE_VIDEO_MODE_SFU:
		/* Nothing special to do here, the bridge channel stream map will ensure the
		 * video goes everywhere it needs to
		 */
		ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
		break;
	}
}

/*!
 * \internal
 * \brief Determine what to do with a voice frame.
 * \since 12.0.0
 *
 * \param bridge Which bridge is getting the frame
 * \param bridge_channel Which channel is writing the frame.
 * \param frame What is being written.
 */
static void softmix_bridge_write_voice(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct softmix_channel *sc = bridge_channel->tech_pvt;
	struct softmix_bridge_data *softmix_data = bridge->tech_pvt;
	int silent = 0;
	int totalsilence = 0;
	int cur_energy = 0;
	int silence_threshold = bridge_channel->tech_args.silence_threshold ?
		bridge_channel->tech_args.silence_threshold :
		DEFAULT_SOFTMIX_SILENCE_THRESHOLD;
	/*
	 * If update_talking is set to 0 or 1, tell the bridge that the channel
	 * has started or stopped talking.
	 */
	char update_talking = -1;

	/* Write the frame into the conference */
	ast_mutex_lock(&sc->lock);

	if (ast_format_cmp(frame->subclass.format, sc->read_slin_format) != AST_FORMAT_CMP_EQUAL) {
		/*
		 * The incoming frame is not the expected format.  Update
		 * the channel's translation path to get us slinear from
		 * the new format for the next frame.
		 *
		 * There is the possibility that this frame is an old slinear
		 * rate frame that was in flight when the softmix bridge
		 * changed rates.  If so it will self correct on subsequent
		 * frames.
		 */
		ast_channel_lock(bridge_channel->chan);
		ast_debug(1, "Channel %s wrote unexpected format into bridge.  Got %s, expected %s.\n",
			ast_channel_name(bridge_channel->chan),
			ast_format_get_name(frame->subclass.format),
			ast_format_get_name(sc->read_slin_format));
		ast_set_read_format_path(bridge_channel->chan, frame->subclass.format,
			sc->read_slin_format);
		ast_channel_unlock(bridge_channel->chan);
	}

	/* The channel will be leaving soon if there is no dsp. */
	if (sc->dsp) {
		silent = ast_dsp_silence_with_energy(sc->dsp, frame, &totalsilence, &cur_energy);
	}

	if (bridge->softmix.video_mode.mode == AST_BRIDGE_VIDEO_MODE_TALKER_SRC) {
		int cur_slot = sc->video_talker.energy_history_cur_slot;

		sc->video_talker.energy_accum -= sc->video_talker.energy_history[cur_slot];
		sc->video_talker.energy_accum += cur_energy;
		sc->video_talker.energy_history[cur_slot] = cur_energy;
		sc->video_talker.energy_average = sc->video_talker.energy_accum / DEFAULT_ENERGY_HISTORY_LEN;
		sc->video_talker.energy_history_cur_slot++;
		if (sc->video_talker.energy_history_cur_slot == DEFAULT_ENERGY_HISTORY_LEN) {
			sc->video_talker.energy_history_cur_slot = 0; /* wrap around */
		}
	}

	if (totalsilence < silence_threshold) {
		if (!sc->talking && !silent) {
			/* Tell the write process we have audio to be mixed out */
			sc->talking = 1;
			update_talking = 1;
		}
	} else {
		if (sc->talking) {
			sc->talking = 0;
			update_talking = 0;
		}
	}

	/* Before adding audio in, make sure we haven't fallen behind. If audio has fallen
	 * behind 4 times the amount of samples mixed on every iteration of the mixer, Re-sync
	 * the audio by flushing the buffer before adding new audio in. */
	if (ast_slinfactory_available(&sc->factory) > (4 * SOFTMIX_SAMPLES(softmix_data->internal_rate, softmix_data->internal_mixing_interval))) {
		ast_slinfactory_flush(&sc->factory);
	}

	if (sc->talking || !bridge_channel->tech_args.drop_silence) {
		/* Add frame to the smoother for mixing with other channels. */
		ast_slinfactory_feed(&sc->factory, frame);
	}

	/* Alllll done */
	ast_mutex_unlock(&sc->lock);

	if (update_talking != -1) {
		ast_bridge_channel_notify_talking(bridge_channel, update_talking);
	}
}

/*!
 * \internal
 * \brief Clear talking flag, stop contributing to mixing and notify handlers.
 * \since 13.21.0, 15.4.0
 *
 * \param bridge_channel Which channel's talking to clear
 */
static void clear_talking(struct ast_bridge_channel *bridge_channel)
{
	struct softmix_channel *sc = bridge_channel->tech_pvt;

	if (sc->talking) {
		ast_mutex_lock(&sc->lock);
		ast_slinfactory_flush(&sc->factory);
		sc->talking = 0;
		ast_mutex_unlock(&sc->lock);

		/* Notify that we are no longer talking. */
		ast_bridge_channel_notify_talking(bridge_channel, 0);
	}
}

/*!
 * \internal
 * \brief Check for voice status updates.
 * \since 13.20.0
 *
 * \param bridge Which bridge we are in
 * \param bridge_channel Which channel we are checking
 */
static void softmix_bridge_check_voice(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	if (bridge_channel->features->mute) {
		/*
		 * We were muted while we were talking.
		 *
		 * Immediately stop contributing to mixing
		 * and report no longer talking.
		 */
		clear_talking(bridge_channel);
	}
}

static int remove_all_original_streams(struct ast_stream_topology *dest,
	const struct ast_stream_topology *source,
	const struct ast_stream_topology *original)
{
	int i;

	for (i = 0; i < ast_stream_topology_get_count(source); ++i) {
		struct ast_stream *stream;
		int original_index;

		stream = ast_stream_topology_get_stream(source, i);

		/* Mark the existing stream as removed so we get a new one, this will get
		 * reused on a subsequent renegotiation.
		 */
		for (original_index = 0; original_index < ast_stream_topology_get_count(original); ++original_index) {
			struct ast_stream *original_stream = ast_stream_topology_get_stream(original, original_index);

			if (!strcmp(ast_stream_get_name(stream), ast_stream_get_name(original_stream))) {
				struct ast_stream *removed;

				removed = ast_stream_clone(stream, NULL);
				if (!removed) {
					return -1;
				}

				ast_stream_set_state(removed, AST_STREAM_STATE_REMOVED);

				/* The destination topology can only ever contain the same, or more,
				 * streams than the original so this is safe.
				 */
				if (ast_stream_topology_set_stream(dest, original_index, removed)) {
					ast_stream_free(removed);
					return -1;
				}

				break;
			}
		}
	}

	return 0;
}

static void sfu_topologies_on_source_change(struct ast_bridge *bridge,
	struct ast_bridge_channel *source)
{
	struct ast_stream_topology *source_video = NULL;
	struct ast_bridge_channels_list *participants = &bridge->channels;
	struct ast_bridge_channel *participant;
	int res;

	source_video = ast_stream_topology_alloc();
	if (!source_video) {
		return;
	}

	ast_channel_lock(source->chan);
	res = append_source_streams(source_video, ast_channel_name(source->chan),
		bridge->softmix.send_sdp_label ? ast_channel_uniqueid(source->chan) : NULL,
		ast_channel_get_stream_topology(source->chan));
	ast_channel_unlock(source->chan);
	if (res) {
		goto cleanup;
	}

	AST_LIST_TRAVERSE(participants, participant, entry) {
		struct ast_stream_topology *original_topology;
		struct softmix_channel *sc;

		if (participant == source) {
			continue;
		}

		sc = participant->tech_pvt;

		original_topology = ast_stream_topology_clone(sc->topology);
		if (!original_topology) {
			goto cleanup;
		}

		/* We add all the source streams back in, if any removed streams are already present they will
		 * get used first followed by appending new ones.
		 */
		if (append_all_streams(sc->topology, source_video)) {
			ast_stream_topology_free(original_topology);
			goto cleanup;
		}

		/* And the original existing streams get marked as removed. This causes the remote side to see
		 * a new stream for the source streams.
		 */
		if (remove_all_original_streams(sc->topology, source_video, original_topology)) {
			ast_stream_topology_free(original_topology);
			goto cleanup;
		}

		ast_channel_request_stream_topology_change(participant->chan, sc->topology, NULL);
		ast_stream_topology_free(original_topology);
	}

cleanup:
	ast_stream_topology_free(source_video);
}

/*!
 * \internal
 * \brief Determine what to do with a text frame.
 * \since 13.22.0
 * \since 15.5.0
 *
 * \param bridge Which bridge is getting the frame
 * \param bridge_channel Which channel is writing the frame.
 * \param frame What is being written.
 */
static void softmix_bridge_write_text(struct ast_bridge *bridge,
	struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	if (DEBUG_ATLEAST(1)) {
		struct ast_msg_data *msg = frame->data.ptr;
		char frame_type[64];

		ast_frame_type2str(frame->frametype, frame_type, sizeof(frame_type));

		if (frame->frametype == AST_FRAME_TEXT_DATA) {
			ast_log(LOG_DEBUG, "Received %s frame from '%s:%s': %s\n", frame_type,
				ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_FROM),
				ast_channel_name(bridge_channel->chan),
				ast_msg_data_get_attribute(msg, AST_MSG_DATA_ATTR_BODY));
		} else {
			ast_log(LOG_DEBUG, "Received %s frame from '%s': %.*s\n", frame_type,
				ast_channel_name(bridge_channel->chan), frame->datalen,
				(char *)frame->data.ptr);
		}
	}

	ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
}

/*!
 * \internal
 * \brief Determine what to do with a control frame.
 * \since 12.0.0
 *
 * \param bridge Which bridge is getting the frame
 * \param bridge_channel Which channel is writing the frame.
 * \param frame What is being written.
 *
 * \retval 0 Frame accepted into the bridge.
 * \retval -1 Frame needs to be deferred.
 */
static int softmix_bridge_write_control(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct softmix_bridge_data *softmix_data = bridge->tech_pvt;

	/*
	 * XXX Softmix needs to use channel roles to determine what to
	 * do with control frames.
	 */

	switch (frame->subclass.integer) {
	case AST_CONTROL_HOLD:
		/*
		 * Doing anything for holds in a conference bridge could be considered a bit
		 * odd. That being said, in most cases one would probably want the talking
		 * flag cleared when 'hold' is pressed by the remote endpoint, so go ahead
		 * and do that here. However, that is all we'll do. Meaning if for some reason
		 * the endpoint continues to send audio frames despite pressing 'hold' talking
		 * will once again be detected for that channel.
		 */
		clear_talking(bridge_channel);
		break;
	case AST_CONTROL_VIDUPDATE:
		if (!bridge->softmix.video_mode.video_update_discard ||
			ast_tvdiff_ms(ast_tvnow(), softmix_data->last_video_update) > bridge->softmix.video_mode.video_update_discard) {
			ast_bridge_queue_everyone_else(bridge, NULL, frame);
			softmix_data->last_video_update = ast_tvnow();
		}
		break;
	case AST_CONTROL_STREAM_TOPOLOGY_SOURCE_CHANGED:
		if (bridge->softmix.video_mode.mode == AST_BRIDGE_VIDEO_MODE_SFU) {
			sfu_topologies_on_source_change(bridge, bridge_channel);
		}
		break;
	default:
		break;
	}

	return 0;
}

/*!
 * \internal
 * \brief Determine what to do with an RTCP frame.
 * \since 15.4.0
 *
 * \param bridge Which bridge is getting the frame
 * \param bridge_channel Which channel is writing the frame.
 * \param frame What is being written.
 */
static void softmix_bridge_write_rtcp(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct ast_rtp_rtcp_feedback *feedback = frame->data.ptr;
	struct softmix_channel *sc = bridge_channel->tech_pvt;

	/* We only care about REMB reports right now. In the future we may be able to use sender or
	 * receiver reports to further tweak things, but not yet.
	 */
	if (frame->subclass.integer != AST_RTP_RTCP_PSFB || feedback->fmt != AST_RTP_RTCP_FMT_REMB ||
		bridge->softmix.video_mode.mode != AST_BRIDGE_VIDEO_MODE_SFU ||
		!bridge->softmix.video_mode.mode_data.sfu_data.remb_send_interval) {
		return;
	}

	/* REMB is the total estimated maximum bitrate across all streams within the session, so we store
	 * only the latest report and use it everywhere.
	 */
	ast_mutex_lock(&sc->lock);
	sc->remb = feedback->remb;
	ast_mutex_unlock(&sc->lock);

	return;
}

/*!
 * \internal
 * \brief Determine what to do with a frame written into the bridge.
 * \since 12.0.0
 *
 * \param bridge Which bridge is getting the frame
 * \param bridge_channel Which channel is writing the frame.
 * \param frame What is being written.
 *
 * \retval 0 Frame accepted into the bridge.
 * \retval -1 Frame needs to be deferred.
 *
 * \note On entry, bridge is already locked.
 */
static int softmix_bridge_write(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	int res = 0;

	if (!bridge->tech_pvt || !bridge_channel || !bridge_channel->tech_pvt) {
		/* "Accept" the frame and discard it. */
		return 0;
	}

	/*
	 * XXX Softmix needs to use channel roles to determine who gets
	 * what frame.  Possible roles: announcer, recorder, agent,
	 * supervisor.
	 */
	switch (frame->frametype) {
	case AST_FRAME_NULL:
		/* "Accept" the frame and discard it. */
		softmix_bridge_check_voice(bridge, bridge_channel);
		break;
	case AST_FRAME_DTMF_BEGIN:
	case AST_FRAME_DTMF_END:
		res = ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
		break;
	case AST_FRAME_VOICE:
		softmix_bridge_write_voice(bridge, bridge_channel, frame);
		break;
	case AST_FRAME_VIDEO:
		softmix_bridge_write_video(bridge, bridge_channel, frame);
		break;
	case AST_FRAME_TEXT:
	case AST_FRAME_TEXT_DATA:
		softmix_bridge_write_text(bridge, bridge_channel, frame);
		break;
	case AST_FRAME_CONTROL:
		res = softmix_bridge_write_control(bridge, bridge_channel, frame);
		break;
	case AST_FRAME_RTCP:
		softmix_bridge_write_rtcp(bridge, bridge_channel, frame);
		break;
	case AST_FRAME_BRIDGE_ACTION:
		res = ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
		break;
	case AST_FRAME_BRIDGE_ACTION_SYNC:
		ast_log(LOG_ERROR, "Synchronous bridge action written to a softmix bridge.\n");
		ast_assert(0);
	default:
		ast_debug(3, "Frame type %u unsupported\n", frame->frametype);
		/* "Accept" the frame and discard it. */
		break;
	}

	return res;
}

static void remb_collect_report_all(struct ast_bridge *bridge, struct softmix_bridge_data *softmix_data,
	float bitrate)
{
	if (!softmix_data->bitrate) {
		softmix_data->bitrate = bitrate;
		return;
	}

	switch (bridge->softmix.video_mode.mode_data.sfu_data.remb_behavior) {
	case AST_BRIDGE_VIDEO_SFU_REMB_AVERAGE_ALL:
		softmix_data->bitrate = (softmix_data->bitrate + bitrate) / 2;
		break;
	case AST_BRIDGE_VIDEO_SFU_REMB_LOWEST_ALL:
		if (bitrate < softmix_data->bitrate) {
			softmix_data->bitrate = bitrate;
		}
		break;
	case AST_BRIDGE_VIDEO_SFU_REMB_HIGHEST_ALL:
		if (bitrate > softmix_data->bitrate) {
			softmix_data->bitrate = bitrate;
		}
		break;
	case AST_BRIDGE_VIDEO_SFU_REMB_AVERAGE:
	case AST_BRIDGE_VIDEO_SFU_REMB_LOWEST:
	case AST_BRIDGE_VIDEO_SFU_REMB_HIGHEST:
	case AST_BRIDGE_VIDEO_SFU_REMB_FORCE:
		/* These will never actually get hit due to being handled by remb_collect_report below */
		break;
	}
}

static void remb_collect_report(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel,
	struct softmix_bridge_data *softmix_data, struct softmix_channel *sc)
{
	int i;
	float bitrate;

	/* If there are no video sources that we are a receiver of then we have noone to
	 * report REMB to.
	 */
	if (!AST_VECTOR_SIZE(&sc->video_sources)) {
		return;
	}

	/* We evenly divide the available maximum bitrate across the video sources
	 * to this receiver so each source gets an equal slice.
	 */

	if (bridge->softmix.video_mode.mode_data.sfu_data.remb_behavior == AST_BRIDGE_VIDEO_SFU_REMB_FORCE) {
		softmix_data->bitrate = bridge->softmix.video_mode.mode_data.sfu_data.estimated_bitrate;
		return;
	}

	bitrate = (sc->remb.br_mantissa << sc->remb.br_exp) / AST_VECTOR_SIZE(&sc->video_sources);

	/* If this receiver has no bitrate yet ignore it */
	if (!bitrate) {
		return;
	}

	/* If we are using the "all" variants then we should use the bridge bitrate to store information */
	if (bridge->softmix.video_mode.mode_data.sfu_data.remb_behavior == AST_BRIDGE_VIDEO_SFU_REMB_AVERAGE_ALL ||
		bridge->softmix.video_mode.mode_data.sfu_data.remb_behavior == AST_BRIDGE_VIDEO_SFU_REMB_LOWEST_ALL ||
		bridge->softmix.video_mode.mode_data.sfu_data.remb_behavior == AST_BRIDGE_VIDEO_SFU_REMB_HIGHEST_ALL) {
		remb_collect_report_all(bridge, softmix_data, bitrate);
		return;
	}

	for (i = 0; i < AST_VECTOR_SIZE(&sc->video_sources); ++i) {
		struct softmix_remb_collector *collector;

		/* The collector will always exist if a video source is in our list */
		collector = AST_VECTOR_GET(&softmix_data->remb_collectors, AST_VECTOR_GET(&sc->video_sources, i));

		if (!collector->bitrate) {
			collector->bitrate = bitrate;
			continue;
		}

		switch (bridge->softmix.video_mode.mode_data.sfu_data.remb_behavior) {
		case AST_BRIDGE_VIDEO_SFU_REMB_AVERAGE:
			collector->bitrate = (collector->bitrate + bitrate) / 2;
			break;
		case AST_BRIDGE_VIDEO_SFU_REMB_LOWEST:
			if (bitrate < collector->bitrate) {
				collector->bitrate = bitrate;
			}
			break;
		case AST_BRIDGE_VIDEO_SFU_REMB_HIGHEST:
			if (bitrate > collector->bitrate) {
				collector->bitrate = bitrate;
			}
			break;
		case AST_BRIDGE_VIDEO_SFU_REMB_AVERAGE_ALL:
		case AST_BRIDGE_VIDEO_SFU_REMB_LOWEST_ALL:
		case AST_BRIDGE_VIDEO_SFU_REMB_HIGHEST_ALL:
			/* These will never actually get hit due to being handled by remb_collect_report_all above */
			break;
		case AST_BRIDGE_VIDEO_SFU_REMB_FORCE:
			/* Don't do anything, we've already forced it */
			break;
		}
	}

	/* After the report is integrated we reset this to 0 in case they stop producing
	 * REMB reports.
	 */
	sc->remb.br_mantissa = 0;
	sc->remb.br_exp = 0;
}

static void remb_send_report(struct ast_bridge_channel *bridge_channel, struct softmix_bridge_data *softmix_data,
	struct softmix_channel *sc)
{
	float bitrate = softmix_data->bitrate;
	int i;
	int exp;

	if (!sc->remb_collector) {
		return;
	}

	/* If there is no bridge level bitrate fall back to collector level */
	if (!bitrate) {
		bitrate = sc->remb_collector->bitrate;
		sc->remb_collector->bitrate = 0;
	}

	/* We always do this calculation as even when the bitrate is zero the browser
	 * still prefers it to be accurate instead of lying.
	 *
	 * The mantissa only has 18 bits available, so make sure it fits. Adjust the
	 * value and exponent for those values that don't.
	 *
	 * For example given the following:
	 *
	 * bitrate = 123456789.0
	 * frexp(bitrate, &exp);
	 *
	 * 'exp' should now equal 27 (number of bits needed to represent the value). Since
	 * the mantissa must fit into an 18-bit unsigned integer, and the given bitrate is
	 * too large to fit, we must subtract 18 from the exponent in order to get the
	 * number of times the bitrate will fit into that size integer.
	 *
	 * exp -= 18;
	 *
	 * 'exp' is now equal to 9. Now we can get the mantissa that fits into an 18-bit
	 * unsigned integer by dividing the bitrate by 2^exp:
	 *
	 * mantissa = 123456789.0 / 2^9
	 *
	 * This makes the final mantissa equal to 241126 (implicitly cast), which is less
	 * than 262143 (the max value that can be put into an unsigned 18-bit integer).
	 * So now we have the following:
	 *
	 * exp = 9;
	 * mantissa = 241126;
	 *
	 * If we multiply that back we should come up with something close to the original
	 * bit rate:
	 *
	 * 241126 * 2^9 = 123456512
	 *
	 * Precision is lost due to the nature of floating point values. Easier to why from
	 * the binary:
	 *
	 * 241126 * 2^9 = 241126 << 9 = 111010110111100110 << 9 = 111010110111100110000000000
	 *
	 * Precision on the "lower" end is lost due to zeros being shifted in. This loss is
	 * both expected and acceptable.
	 */
	frexp(bitrate, &exp);
	exp = exp > 18 ? exp - 18 : 0;

	sc->remb_collector->feedback.remb.br_mantissa = bitrate / (1 << exp);
	sc->remb_collector->feedback.remb.br_exp = exp;

	for (i = 0; i < AST_VECTOR_SIZE(&bridge_channel->stream_map.to_bridge); ++i) {
		int bridge_num = AST_VECTOR_GET(&bridge_channel->stream_map.to_bridge, i);

		/* If this stream is not being provided to the bridge there can be no receivers of it
		 * so therefore no REMB reports.
		 */
		if (bridge_num == -1) {
			continue;
		}

		/* We need to update the frame with this stream, or else it won't be
		 * properly routed. We don't use the actual channel stream identifier as
		 * the bridging core will do the translation from bridge stream identifier to
		 * channel stream identifier.
		 */
		sc->remb_collector->frame.stream_num = bridge_num;
		ast_bridge_channel_queue_frame(bridge_channel, &sc->remb_collector->frame);
	}
}

static void gather_softmix_stats(struct softmix_stats *stats,
	const struct softmix_bridge_data *softmix_data,
	struct ast_bridge_channel *bridge_channel)
{
	int channel_native_rate;

	/* Gather stats about channel sample rates. */
	ast_channel_lock(bridge_channel->chan);
	channel_native_rate = MAX(SOFTMIX_MIN_SAMPLE_RATE,
		ast_format_get_sample_rate(ast_channel_rawreadformat(bridge_channel->chan)));
	ast_channel_unlock(bridge_channel->chan);

	if (stats->highest_supported_rate < channel_native_rate) {
		stats->highest_supported_rate = channel_native_rate;
	}
	if (stats->maximum_rate && stats->maximum_rate < channel_native_rate) {
		stats->num_above_maximum_rate++;
	} else if (softmix_data->internal_rate < channel_native_rate) {
		int i;

		for (i = 0; i < ARRAY_LEN(stats->sample_rates); i++) {
			if (stats->sample_rates[i] == channel_native_rate) {
				stats->num_channels[i]++;
				break;
			} else if (!stats->sample_rates[i]) {
				stats->sample_rates[i] = channel_native_rate;
				stats->num_channels[i]++;
				break;
			}
		}
		stats->num_above_internal_rate++;
	} else if (softmix_data->internal_rate == channel_native_rate) {
		stats->num_at_internal_rate++;
	}
}

/*!
 * \internal
 * \brief Analyse mixing statistics and change bridges internal rate
 * if necessary.
 *
 * \retval 0 no changes to internal rate
 * \retval 1 internal rate was changed, update all the channels on the next mixing iteration.
 */
static unsigned int analyse_softmix_stats(struct softmix_stats *stats,
		struct softmix_bridge_data *softmix_data, int binaural_active)
{
	int i;

	if (binaural_active) {
		stats->locked_rate = SOFTMIX_BINAURAL_SAMPLE_RATE;
	}

	/*
	 * Re-adjust the internal bridge sample rate if
	 * 1. The bridge's internal sample rate is locked in at a sample
	 *    rate other than the current sample rate being used.
	 * 2. two or more channels support a higher sample rate
	 * 3. no channels support the current sample rate or a higher rate
	 */
	if (stats->locked_rate) {
		/* if the rate is locked by the bridge, only update it if it differs
		 * from the current rate we are using. */
		if (softmix_data->internal_rate != stats->locked_rate) {
			ast_debug(1, "Locking at new rate.  Bridge changed from %u to %u.\n",
				softmix_data->internal_rate, stats->locked_rate);
			softmix_data->internal_rate = stats->locked_rate;
			return 1;
		}
	} else if (stats->num_above_maximum_rate) {
		/* if the bridge has a maximum rate set and channels are above it only
		 * update if it differs from the current rate we are using. */
		if (softmix_data->internal_rate != stats->maximum_rate) {
			ast_debug(1, "Locking at new maximum rate.  Bridge changed from %u to %u.\n",
				softmix_data->internal_rate, stats->maximum_rate);
			softmix_data->internal_rate = stats->maximum_rate;
			return 1;
		}
	} else if (stats->num_above_internal_rate >= 2) {
		/* the highest rate is just used as a starting point */
		unsigned int best_rate = stats->highest_supported_rate;
		int best_index = -1;

		for (i = 0; i < ARRAY_LEN(stats->num_channels); i++) {
			if (stats->num_channels[i]) {
				break;
			}
			if (2 <= stats->num_channels[i]) {
				/* Two or more channels support this rate. */
				if (best_index == -1
					|| stats->sample_rates[best_index] < stats->sample_rates[i]) {
					/*
					 * best_rate starts out being the first sample rate
					 * greater than the internal sample rate that two or
					 * more channels support.
					 *
					 * or
					 *
					 * There are multiple rates above the internal rate
					 * and this rate is higher than the previous rate two
					 * or more channels support.
					 */
					best_rate = stats->sample_rates[i];
					best_index = i;
				}
			} else if (best_index == -1) {
				/*
				 * It is possible that multiple channels exist with native sample
				 * rates above the internal sample rate, but none of those channels
				 * have the same rate in common.  In this case, the lowest sample
				 * rate among those channels is picked. Over time as additional
				 * statistic runs are made the internal sample rate number will
				 * adjust to the most optimal sample rate, but it may take multiple
				 * iterations.
				 */
				best_rate = MIN(best_rate, stats->sample_rates[i]);
			}
		}

		ast_debug(1, "Multiple above internal rate.  Bridge changed from %u to %u.\n",
			softmix_data->internal_rate, best_rate);
		softmix_data->internal_rate = best_rate;
		return 1;
	} else if (!stats->num_at_internal_rate && !stats->num_above_internal_rate) {
		/* In this case, the highest supported rate is actually lower than the internal rate */
		ast_debug(1, "All below internal rate.  Bridge changed from %u to %u.\n",
			softmix_data->internal_rate, stats->highest_supported_rate);
		softmix_data->internal_rate = stats->highest_supported_rate;
		return 1;
	}
	return 0;
}

static int softmix_mixing_array_init(struct softmix_mixing_array *mixing_array,
		unsigned int starting_num_entries, unsigned int binaural_active)
{
	memset(mixing_array, 0, sizeof(*mixing_array));
	mixing_array->max_num_entries = starting_num_entries;
	if (!(mixing_array->buffers = ast_calloc(mixing_array->max_num_entries, sizeof(int16_t *)))) {
		ast_log(LOG_NOTICE, "Failed to allocate softmix mixing structure.\n");
		return -1;
	}
	if (binaural_active) {
		if (!(mixing_array->chan_pairs = ast_calloc(mixing_array->max_num_entries,
				sizeof(struct convolve_channel_pair *)))) {
			ast_log(LOG_NOTICE, "Failed to allocate softmix mixing structure.\n");
			return -1;
		}
	}
	return 0;
}

static void softmix_mixing_array_destroy(struct softmix_mixing_array *mixing_array,
		unsigned int binaural_active)
{
	ast_free(mixing_array->buffers);
	if (binaural_active) {
		ast_free(mixing_array->chan_pairs);
	}
}

static int softmix_mixing_array_grow(struct softmix_mixing_array *mixing_array,
		unsigned int num_entries, unsigned int binaural_active)
{
	int16_t **tmp;

	/* give it some room to grow since memory is cheap but allocations can be expensive */
	mixing_array->max_num_entries = num_entries;
	if (!(tmp = ast_realloc(mixing_array->buffers, (mixing_array->max_num_entries * sizeof(int16_t *))))) {
		ast_log(LOG_NOTICE, "Failed to re-allocate softmix mixing structure.\n");
		return -1;
	}
	mixing_array->buffers = tmp;

	if (binaural_active) {
		struct convolve_channel_pair **tmp2;
		if (!(tmp2 = ast_realloc(mixing_array->chan_pairs,
				(mixing_array->max_num_entries * sizeof(struct convolve_channel_pair *))))) {
			ast_log(LOG_NOTICE, "Failed to re-allocate softmix mixing structure.\n");
			return -1;
		}
		mixing_array->chan_pairs = tmp2;
	}
	return 0;
}

/*!
 * \brief Mixing loop.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
static int softmix_mixing_loop(struct ast_bridge *bridge)
{
	struct softmix_stats stats = { { 0 }, };
	struct softmix_mixing_array mixing_array;
	struct softmix_bridge_data *softmix_data = bridge->tech_pvt;
	struct ast_timer *timer;
	struct softmix_translate_helper trans_helper;
	int16_t buf[MAX_DATALEN];
#ifdef BINAURAL_RENDERING
	int16_t bin_buf[MAX_DATALEN];
	int16_t ann_buf[MAX_DATALEN];
#endif
	unsigned int stat_iteration_counter = 0; /* counts down, gather stats at zero and reset. */
	int timingfd;
	int update_all_rates = 0; /* set this when the internal sample rate has changed */
	unsigned int idx;
	unsigned int x;
	int res = -1;

	timer = softmix_data->timer;
	timingfd = ast_timer_fd(timer);
	softmix_translate_helper_init(&trans_helper, softmix_data->internal_rate);
	ast_timer_set_rate(timer, (1000 / softmix_data->internal_mixing_interval));

	/* Give the mixing array room to grow, memory is cheap but allocations are expensive. */
	if (softmix_mixing_array_init(&mixing_array, bridge->num_channels + 10,
			bridge->softmix.binaural_active)) {
		goto softmix_cleanup;
	}

	/*
	 * XXX Softmix needs to use channel roles to determine who gets
	 * what audio mixed.
	 */
	while (!softmix_data->stop && bridge->num_active) {
		struct ast_bridge_channel *bridge_channel;
		int timeout = -1;
		struct ast_format *cur_slin = ast_format_cache_get_slin_by_rate(softmix_data->internal_rate);
		unsigned int softmix_samples = SOFTMIX_SAMPLES(softmix_data->internal_rate, softmix_data->internal_mixing_interval);
		unsigned int softmix_datalen = SOFTMIX_DATALEN(softmix_data->internal_rate, softmix_data->internal_mixing_interval);
		int remb_update = 0;

		if (softmix_datalen > MAX_DATALEN) {
			/* This should NEVER happen, but if it does we need to know about it. Almost
			 * all the memcpys used during this process depend on this assumption.  Rather
			 * than checking this over and over again through out the code, this single
			 * verification is done on each iteration. */
			ast_log(LOG_WARNING,
				"Bridge %s: Conference mixing error, requested mixing length greater than mixing buffer.\n",
				bridge->uniqueid);
			goto softmix_cleanup;
		}

		/* Grow the mixing array buffer as participants are added. */
		if (mixing_array.max_num_entries < bridge->num_channels
			&& softmix_mixing_array_grow(&mixing_array, bridge->num_channels + 5,
					bridge->softmix.binaural_active)) {
			goto softmix_cleanup;
		}

		/* init the number of buffers stored in the mixing array to 0.
		 * As buffers are added for mixing, this number is incremented. */
		mixing_array.used_entries = 0;

		/* These variables help determine if a rate change is required */
		if (!stat_iteration_counter) {
			memset(&stats, 0, sizeof(stats));
			stats.locked_rate = bridge->softmix.internal_sample_rate;
			stats.maximum_rate = bridge->softmix.maximum_sample_rate;
		}

		/* If the sample rate has changed, update the translator helper */
		if (update_all_rates) {
			softmix_translate_helper_change_rate(&trans_helper, softmix_data->internal_rate);
		}

#ifdef BINAURAL_RENDERING
		check_binaural_position_change(bridge, softmix_data);
#endif

		/* If we need to do a REMB update to all video sources then do so */
		if (bridge->softmix.video_mode.mode == AST_BRIDGE_VIDEO_MODE_SFU &&
			bridge->softmix.video_mode.mode_data.sfu_data.remb_send_interval &&
			ast_tvdiff_ms(ast_tvnow(), softmix_data->last_remb_update) > bridge->softmix.video_mode.mode_data.sfu_data.remb_send_interval) {
			remb_update = 1;
			softmix_data->last_remb_update = ast_tvnow();
		}

		/* Go through pulling audio from each factory that has it available */
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			struct softmix_channel *sc = bridge_channel->tech_pvt;

			if (!sc) {
				/* This channel failed to join successfully. */
				continue;
			}

			/* Update the sample rate to match the bridge's native sample rate if necessary. */
			if (update_all_rates) {
				set_softmix_bridge_data(softmix_data->internal_rate,
						softmix_data->internal_mixing_interval, bridge_channel, 1, -1, -1, -1);
			}

			/* If stat_iteration_counter is 0, then collect statistics during this mixing interaction */
			if (!stat_iteration_counter) {
				gather_softmix_stats(&stats, softmix_data, bridge_channel);
			}

			/* if the channel is suspended, don't check for audio, but still gather stats */
			if (bridge_channel->suspended) {
				continue;
			}

			/* Try to get audio from the factory if available */
			ast_mutex_lock(&sc->lock);
			if ((mixing_array.buffers[mixing_array.used_entries] = softmix_process_read_audio(sc, softmix_samples))) {
#ifdef BINAURAL_RENDERING
				add_binaural_mixing(bridge, softmix_data, softmix_samples, &mixing_array, sc,
						ast_channel_name(bridge_channel->chan));
#endif
				mixing_array.used_entries++;
			}
			if (remb_update) {
				remb_collect_report(bridge, bridge_channel, softmix_data, sc);
			}
			ast_mutex_unlock(&sc->lock);
		}

		/* mix it like crazy (non binaural channels)*/
		memset(buf, 0, softmix_datalen);
		for (idx = 0; idx < mixing_array.used_entries; ++idx) {
			for (x = 0; x < softmix_samples; ++x) {
				ast_slinear_saturated_add(buf + x, mixing_array.buffers[idx] + x);
			}
		}

#ifdef BINAURAL_RENDERING
		binaural_mixing(bridge, softmix_data, &mixing_array, bin_buf, ann_buf);
#endif

		/* Next step go through removing the channel's own audio and creating a good frame... */
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			struct softmix_channel *sc = bridge_channel->tech_pvt;

			if (!sc || bridge_channel->suspended) {
				/* This channel failed to join successfully or is suspended. */
				continue;
			}

			ast_mutex_lock(&sc->lock);

			/* Make SLINEAR write frame from local buffer */
			ao2_t_replace(sc->write_frame.subclass.format, cur_slin,
				"Replace softmix channel slin format");
#ifdef BINAURAL_RENDERING
			if (bridge->softmix.binaural_active && softmix_data->convolve.binaural_active
					&& sc->binaural) {
				create_binaural_frame(bridge_channel, sc, bin_buf, ann_buf, softmix_datalen,
						softmix_samples, buf);
			} else
#endif
			{
				sc->write_frame.datalen = softmix_datalen;
				sc->write_frame.samples = softmix_samples;
				memcpy(sc->final_buf, buf, softmix_datalen);
			}
			/* process the softmix channel's new write audio */
			softmix_process_write_audio(&trans_helper,
					ast_channel_rawwriteformat(bridge_channel->chan), sc,
					softmix_data->default_sample_size);

			ast_mutex_unlock(&sc->lock);

			/* A frame is now ready for the channel. */
			ast_bridge_channel_queue_frame(bridge_channel, &sc->write_frame);

			if (remb_update) {
				remb_send_report(bridge_channel, softmix_data, sc);
			}
		}

		if (remb_update) {
			/* In case we are doing bridge level REMB reset the bitrate so we start fresh */
			softmix_data->bitrate = 0;
		}

		update_all_rates = 0;
		if (!stat_iteration_counter) {
			update_all_rates = analyse_softmix_stats(&stats, softmix_data,
					bridge->softmix.binaural_active);
			stat_iteration_counter = SOFTMIX_STAT_INTERVAL;
		}
		stat_iteration_counter--;

		ast_bridge_unlock(bridge);
		/* cleanup any translation frame data from the previous mixing iteration. */
		softmix_translate_helper_cleanup(&trans_helper);
		/* Wait for the timing source to tell us to wake up and get things done */
		ast_waitfor_n_fd(&timingfd, 1, &timeout, NULL);
		if (ast_timer_ack(timer, 1) < 0) {
			ast_log(LOG_ERROR, "Bridge %s: Failed to acknowledge timer in softmix.\n",
				bridge->uniqueid);
			ast_bridge_lock(bridge);
			goto softmix_cleanup;
		}
		ast_bridge_lock(bridge);

		/* make sure to detect mixing interval changes if they occur. */
		if (bridge->softmix.internal_mixing_interval
			&& (bridge->softmix.internal_mixing_interval != softmix_data->internal_mixing_interval)) {
			softmix_data->internal_mixing_interval = bridge->softmix.internal_mixing_interval;
			ast_timer_set_rate(timer, (1000 / softmix_data->internal_mixing_interval));
			update_all_rates = 1; /* if the interval changes, the rates must be adjusted as well just to be notified new interval.*/
		}
	}

	res = 0;

softmix_cleanup:
	softmix_translate_helper_destroy(&trans_helper);
	softmix_mixing_array_destroy(&mixing_array, bridge->softmix.binaural_active);
	return res;
}

/*!
 * \internal
 * \brief Mixing thread.
 * \since 12.0.0
 *
 * \note The thread does not have its own reference to the
 * bridge.  The lifetime of the thread is tied to the lifetime
 * of the mixing technology association with the bridge.
 */
static void *softmix_mixing_thread(void *data)
{
	struct softmix_bridge_data *softmix_data = data;
	struct ast_bridge *bridge = softmix_data->bridge;

	ast_bridge_lock(bridge);
	if (bridge->callid) {
		ast_callid_threadassoc_add(bridge->callid);
	}

	ast_debug(1, "Bridge %s: starting mixing thread\n", bridge->uniqueid);

	while (!softmix_data->stop) {
		if (!bridge->num_active) {
			/* Wait for something to happen to the bridge. */
			ast_bridge_unlock(bridge);
			ast_mutex_lock(&softmix_data->lock);
			if (!softmix_data->stop) {
				ast_cond_wait(&softmix_data->cond, &softmix_data->lock);
			}
			ast_mutex_unlock(&softmix_data->lock);
			ast_bridge_lock(bridge);
			continue;
		}

		if (bridge->softmix.binaural_active && !softmix_data->binaural_init) {
#ifndef BINAURAL_RENDERING
			ast_bridge_lock(bridge);
			bridge->softmix.binaural_active = 0;
			ast_bridge_unlock(bridge);
			ast_log(LOG_WARNING, "Bridge: %s: Binaural rendering active by config but not "
					"compiled.\n", bridge->uniqueid);
#else
			/* Set and init binaural data if binaural is activated in the configuration. */
			softmix_data->internal_rate = SOFTMIX_BINAURAL_SAMPLE_RATE;
			softmix_data->default_sample_size = SOFTMIX_SAMPLES(softmix_data->internal_rate,
					softmix_data->internal_mixing_interval);
			/* If init for binaural processing fails we will fall back to mono audio processing. */
			if (init_convolve_data(&softmix_data->convolve, softmix_data->default_sample_size)
					== -1) {
				ast_bridge_lock(bridge);
				bridge->softmix.binaural_active = 0;
				ast_bridge_unlock(bridge);
				ast_log(LOG_ERROR, "Bridge: %s: Unable to allocate memory for "
						"binaural processing,  Will only process mono audio.\n",
						bridge->uniqueid);
			}
			softmix_data->binaural_init = 1;
#endif
		}

		if (softmix_mixing_loop(bridge)) {
			/*
			 * A mixing error occurred.  Sleep and try again later so we
			 * won't flood the logs.
			 */
			ast_bridge_unlock(bridge);
			sleep(1);
			ast_bridge_lock(bridge);
		}
	}

	ast_bridge_unlock(bridge);

	ast_debug(1, "Bridge %s: stopping mixing thread\n", bridge->uniqueid);

	return NULL;
}

static void softmix_bridge_data_destroy(struct softmix_bridge_data *softmix_data)
{
	if (softmix_data->timer) {
		ast_timer_close(softmix_data->timer);
		softmix_data->timer = NULL;
	}
	ast_mutex_destroy(&softmix_data->lock);
	ast_cond_destroy(&softmix_data->cond);
	AST_VECTOR_RESET(&softmix_data->remb_collectors, ao2_cleanup);
	AST_VECTOR_FREE(&softmix_data->remb_collectors);
	ast_free(softmix_data);
}

/*! \brief Function called when a bridge is created */
static int softmix_bridge_create(struct ast_bridge *bridge)
{
	struct softmix_bridge_data *softmix_data;

	softmix_data = ast_calloc(1, sizeof(*softmix_data));
	if (!softmix_data) {
		return -1;
	}
	softmix_data->bridge = bridge;
	ast_mutex_init(&softmix_data->lock);
	ast_cond_init(&softmix_data->cond, NULL);
	softmix_data->timer = ast_timer_open();
	if (!softmix_data->timer) {
		ast_log(AST_LOG_WARNING, "Failed to open timer for softmix bridge\n");
		softmix_bridge_data_destroy(softmix_data);
		return -1;
	}
	/* start at minimum rate, let it grow from there */
	softmix_data->internal_rate = SOFTMIX_MIN_SAMPLE_RATE;
	softmix_data->internal_mixing_interval = DEFAULT_SOFTMIX_INTERVAL;

#ifdef BINAURAL_RENDERING
	softmix_data->default_sample_size = SOFTMIX_SAMPLES(softmix_data->internal_rate,
			softmix_data->internal_mixing_interval);
#endif

	AST_VECTOR_INIT(&softmix_data->remb_collectors, 0);

	bridge->tech_pvt = softmix_data;

	/* Start the mixing thread. */
	if (ast_pthread_create(&softmix_data->thread, NULL, softmix_mixing_thread,
		softmix_data)) {
		softmix_data->thread = AST_PTHREADT_NULL;
		softmix_bridge_data_destroy(softmix_data);
		bridge->tech_pvt = NULL;
		return -1;
	}

	return 0;
}

/*!
 * \internal
 * \brief Request the softmix mixing thread stop.
 * \since 12.0.0
 *
 * \param bridge Which bridge is being stopped.
 */
static void softmix_bridge_stop(struct ast_bridge *bridge)
{
	struct softmix_bridge_data *softmix_data;

	softmix_data = bridge->tech_pvt;
	if (!softmix_data) {
		return;
	}

	ast_mutex_lock(&softmix_data->lock);
	softmix_data->stop = 1;
	ast_mutex_unlock(&softmix_data->lock);
}

/*! \brief Function called when a bridge is destroyed */
static void softmix_bridge_destroy(struct ast_bridge *bridge)
{
	struct softmix_bridge_data *softmix_data;
	pthread_t thread;

	softmix_data = bridge->tech_pvt;
	if (!softmix_data) {
		return;
	}

	/* Stop the mixing thread. */
	ast_mutex_lock(&softmix_data->lock);
	softmix_data->stop = 1;
	ast_cond_signal(&softmix_data->cond);
	thread = softmix_data->thread;
	softmix_data->thread = AST_PTHREADT_NULL;
	ast_mutex_unlock(&softmix_data->lock);
	if (thread != AST_PTHREADT_NULL) {
		ast_debug(1, "Bridge %s: Waiting for mixing thread to die.\n", bridge->uniqueid);
		pthread_join(thread, NULL);
	}
#ifdef BINAURAL_RENDERING
	free_convolve_data(&softmix_data->convolve);
#endif
	softmix_bridge_data_destroy(softmix_data);
	bridge->tech_pvt = NULL;
}

/*!
 * \brief Map a source stream to all of its destination streams.
 *
 * \param source_channel_name Name of channel where the source stream originates
 * \param bridge_stream_position The slot in the bridge where source video will come from
 * \param participants The bridge_channels in the bridge
 * \param source_channel_stream_position The position of the stream on the source channel
 */
static void map_source_to_destinations(const char *source_channel_name,
	size_t bridge_stream_position, struct ast_bridge_channels_list *participants, int source_channel_stream_position)
{
	struct ast_bridge_channel *participant;

	AST_LIST_TRAVERSE(participants, participant, entry) {
		int i;
		struct ast_stream_topology *topology;

		if (!strcmp(source_channel_name, ast_channel_name(participant->chan))) {
			continue;
		}

		ast_bridge_channel_lock(participant);
		ast_channel_lock(participant->chan);
		topology = ast_channel_get_stream_topology(participant->chan);

		for (i = 0; i < ast_stream_topology_get_count(topology); ++i) {
			struct ast_stream *stream;

			stream = ast_stream_topology_get_stream(topology, i);
			if (is_video_dest(stream, source_channel_name, source_channel_stream_position)) {
				struct softmix_channel *sc = participant->tech_pvt;

				AST_VECTOR_REPLACE(&participant->stream_map.to_channel, bridge_stream_position, i);
				AST_VECTOR_APPEND(&sc->video_sources, bridge_stream_position);
				break;
			}
		}
		ast_channel_unlock(participant->chan);
		ast_bridge_channel_unlock(participant);
	}
}

/*!
 * \brief Allocate a REMB collector
 *
 * \retval non-NULL success
 * \retval NULL failure
 */
static struct softmix_remb_collector *remb_collector_alloc(void)
{
	struct softmix_remb_collector *collector;

	collector = ao2_alloc_options(sizeof(*collector), NULL, AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!collector) {
		return NULL;
	}

	collector->frame.frametype = AST_FRAME_RTCP;
	collector->frame.subclass.integer = AST_RTP_RTCP_PSFB;
	collector->feedback.fmt = AST_RTP_RTCP_FMT_REMB;
	collector->frame.data.ptr = &collector->feedback;
	collector->frame.datalen = sizeof(collector->feedback);

	return collector;
}

/*!
 * \brief Setup REMB collection for a particular bridge stream and channel.
 *
 * \param bridge The bridge
 * \param bridge_channel Channel that is collecting REMB information
 * \param bridge_stream_position The slot in the bridge where source video comes from
 */
static void remb_enable_collection(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel,
	size_t bridge_stream_position)
{
	struct softmix_channel *sc = bridge_channel->tech_pvt;
	struct softmix_bridge_data *softmix_data = bridge->tech_pvt;

	if (!sc->remb_collector) {
		sc->remb_collector = remb_collector_alloc();
		if (!sc->remb_collector) {
			/* This is not fatal. Things will still continue to work but we won't
			 * produce a REMB report to the sender.
			 */
			return;
		}
	}

	ao2_ref(sc->remb_collector, +1);
	if (AST_VECTOR_REPLACE(&softmix_data->remb_collectors, bridge_stream_position,
		sc->remb_collector)) {
		ao2_ref(sc->remb_collector, -1);
	}
}

static void softmix_bridge_stream_sources_update(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel,
	struct softmix_channel *sc)
{
	int index;
	struct ast_stream_topology *old_topology = sc->topology;
	struct ast_stream_topology *new_topology = ast_channel_get_stream_topology(bridge_channel->chan);
	int removed_streams[MAX(ast_stream_topology_get_count(sc->topology), ast_stream_topology_get_count(new_topology))];
	size_t removed_streams_count = 0;
	struct ast_stream_topology *added_streams;
	struct ast_bridge_channels_list *participants = &bridge->channels;
	struct ast_bridge_channel *participant;
	SCOPE_ENTER(3, "%s: OT: %s NT: %s\n", ast_channel_name(bridge_channel->chan),
		ast_str_tmp(256, ast_stream_topology_to_str(old_topology, &STR_TMP)),
		ast_str_tmp(256, ast_stream_topology_to_str(new_topology, &STR_TMP)));

	added_streams = ast_stream_topology_alloc();
	if (!added_streams) {
		SCOPE_EXIT_LOG(LOG_ERROR, "%s: Couldn't alloc topology\n", ast_channel_name(bridge_channel->chan));
	}

	/* We go through the old topology comparing it to the new topology to determine what streams
	 * changed state. A state transition can result in the stream being considered a new source
	 * (for example it was removed and is now present) or being removed (a stream became inactive).
	 * Added streams are copied into a topology and added to each other participant while for
	 * removed streams we merely store their position and mark them as removed later.
	 */
	ast_trace(-1, "%s: Checking for state changes\n", ast_channel_name(bridge_channel->chan));
	for (index = 0; index < ast_stream_topology_get_count(sc->topology) && index < ast_stream_topology_get_count(new_topology); ++index) {
		struct ast_stream *old_stream = ast_stream_topology_get_stream(sc->topology, index);
		struct ast_stream *new_stream = ast_stream_topology_get_stream(new_topology, index);
		SCOPE_ENTER(4, "%s:   Slot: %d  Old stream: %s  New stream: %s\n",  ast_channel_name(bridge_channel->chan),
			index, ast_str_tmp(256, ast_stream_to_str(old_stream, &STR_TMP)),
			ast_str_tmp(256, ast_stream_to_str(new_stream, &STR_TMP)));

		/* Ignore all streams that don't carry video and streams that are strictly outgoing destination streams */
		if ((ast_stream_get_type(old_stream) != AST_MEDIA_TYPE_VIDEO && ast_stream_get_type(new_stream) != AST_MEDIA_TYPE_VIDEO) ||
			!strncmp(ast_stream_get_name(new_stream), SOFTBRIDGE_VIDEO_DEST_PREFIX,
				SOFTBRIDGE_VIDEO_DEST_LEN)) {
			SCOPE_EXIT_EXPR(continue, "%s: Stream %d ignored\n",  ast_channel_name(bridge_channel->chan), index);
		}

		if (ast_stream_get_type(old_stream) == AST_MEDIA_TYPE_VIDEO && ast_stream_get_type(new_stream) != AST_MEDIA_TYPE_VIDEO) {
			/* If a stream renegotiates from video to non-video then we need to remove it as a source */
			ast_trace(-1, "%s: Stream %d added to remove list\n",  ast_channel_name(bridge_channel->chan), index);
			removed_streams[removed_streams_count++] = index;
		} else if (ast_stream_get_type(old_stream) != AST_MEDIA_TYPE_VIDEO && ast_stream_get_type(new_stream) == AST_MEDIA_TYPE_VIDEO) {
			if (ast_stream_get_state(new_stream) != AST_STREAM_STATE_REMOVED) {
				/* If a stream renegotiates from non-video to video in a non-removed state we need to add it as a source */
				if (append_source_stream(added_streams, ast_channel_name(bridge_channel->chan),
							bridge->softmix.send_sdp_label ? ast_channel_uniqueid(bridge_channel->chan) : NULL,
							new_stream, index)) {
					SCOPE_EXIT_EXPR(goto cleanup, "%s: Couldn't append source stream %d:%s\n",  ast_channel_name(bridge_channel->chan),
						index, ast_stream_get_name(new_stream));
				}
				ast_trace(-1, "%s: Stream %d changed from non-video to video\n",  ast_channel_name(bridge_channel->chan), index);
			}
		} else if (ast_stream_get_state(old_stream) != AST_STREAM_STATE_REMOVED &&
				ast_stream_get_state(new_stream) != AST_STREAM_STATE_SENDRECV && ast_stream_get_state(new_stream) != AST_STREAM_STATE_RECVONLY) {
			ast_trace(-1, "%s: Stream %d added to remove list\n",  ast_channel_name(bridge_channel->chan), index);
			/* If a stream renegotiates and is removed then we remove it */
			removed_streams[removed_streams_count++] = index;
		} else if ((ast_stream_get_state(old_stream) == AST_STREAM_STATE_REMOVED || ast_stream_get_state(old_stream) == AST_STREAM_STATE_INACTIVE ||
				ast_stream_get_state(old_stream) == AST_STREAM_STATE_SENDONLY) &&
				ast_stream_get_state(new_stream) != AST_STREAM_STATE_INACTIVE && ast_stream_get_state(new_stream) != AST_STREAM_STATE_SENDONLY &&
				ast_stream_get_state(new_stream) != AST_STREAM_STATE_REMOVED) {
			/* If a stream renegotiates and is added then we add it */
			if (append_source_stream(added_streams, ast_channel_name(bridge_channel->chan),
						bridge->softmix.send_sdp_label ? ast_channel_uniqueid(bridge_channel->chan) : NULL,
						new_stream, index)) {
				SCOPE_EXIT_EXPR(goto cleanup, "%s: Couldn't append source stream %d:%s\n",  ast_channel_name(bridge_channel->chan),
					index, ast_stream_get_name(new_stream));
			}
			ast_trace(-1, "%s: Stream %d:%s changed state from %s to %s\n",  ast_channel_name(bridge_channel->chan),
				index, ast_stream_get_name(old_stream), ast_stream_state2str(ast_stream_get_state(old_stream)),
				ast_stream_state2str(ast_stream_get_state(new_stream)));
		} else {
			ast_trace(-1, "%s: Stream %d:%s didn't do anything\n",  ast_channel_name(bridge_channel->chan),
				index, ast_stream_get_name(old_stream));
		}
		SCOPE_EXIT();
	}

	/* Any newly added streams that did not take the position of a removed stream
	 * will be present at the end of the new topology. Since streams are never
	 * removed from the topology but merely marked as removed we can pick up where we
	 * left off when comparing the old and new topologies.
	 */
	ast_trace(-1, "%s: Checking for newly added streams\n", ast_channel_name(bridge_channel->chan));

	for (; index < ast_stream_topology_get_count(new_topology); ++index) {
		struct ast_stream *stream = ast_stream_topology_get_stream(new_topology, index);
		SCOPE_ENTER(4, "%s: Checking stream %d:%s\n",  ast_channel_name(bridge_channel->chan), index,
			ast_stream_get_name(stream));

		if (!is_video_source(stream)) {
			SCOPE_EXIT_EXPR(continue, "%s: Stream %d:%s is not video source\n",  ast_channel_name(bridge_channel->chan),
				index, ast_stream_get_name(stream));
		}

		if (append_source_stream(added_streams, ast_channel_name(bridge_channel->chan),
					bridge->softmix.send_sdp_label ? ast_channel_uniqueid(bridge_channel->chan) : NULL,
					stream, index)) {
			SCOPE_EXIT_EXPR(goto cleanup, "%s: Couldn't append stream %d:%s\n",  ast_channel_name(bridge_channel->chan),
				index, ast_stream_get_name(stream));
		}
		SCOPE_EXIT("%s:   Added new stream %s\n", ast_channel_name(bridge_channel->chan),
			ast_str_tmp(256, ast_stream_to_str(stream, &STR_TMP)));
	}

	/*  We always update the stored topology if we can to reflect what is currently negotiated */
	sc->topology = ast_stream_topology_clone(new_topology);
	if (!sc->topology) {
		sc->topology = old_topology;
	} else {
		ast_stream_topology_free(old_topology);
	}

	/* If there are no removed sources and no added sources we don't need to renegotiate the
	 * other participants.
	 */
	if (!removed_streams_count && !ast_stream_topology_get_count(added_streams)) {
		ast_trace(-1, "%s: Nothing added or removed\n", ast_channel_name(bridge_channel->chan));
		goto cleanup;
	}

	ast_trace(-1, "%s: Processing adds and removes\n", ast_channel_name(bridge_channel->chan));
	/* Go through each participant adding in the new streams and removing the old ones */
	AST_LIST_TRAVERSE(participants, participant, entry)
	{
		struct softmix_channel *participant_sc = participant->tech_pvt;
		SCOPE_ENTER(4, "%s/%s: Old participant topology %s\n",
			ast_channel_name(bridge_channel->chan),
			ast_channel_name(participant->chan),
			ast_str_tmp(256, ast_stream_topology_to_str(participant_sc->topology, &STR_TMP)));

		if (participant == bridge_channel) {
			SCOPE_EXIT_EXPR(continue, "%s/%s: Same channel.  Skipping\n",
				ast_channel_name(bridge_channel->chan),
				ast_channel_name(participant->chan));
		}

		/* We add in all the new streams first so that they do not take the place
		 * of any of our removed streams, allowing the remote side to reset the state
		 * for each removed stream. */
		if (append_all_streams(participant_sc->topology, added_streams)) {
			SCOPE_EXIT_EXPR(goto cleanup, "%s/%s: Couldn't append streams\n",  ast_channel_name(bridge_channel->chan),
				ast_channel_name(participant->chan));
		}
		ast_trace(-1, "%s/%s:   Adding streams %s\n", ast_channel_name(bridge_channel->chan),
			ast_channel_name(participant->chan),
			ast_str_tmp(256, ast_stream_topology_to_str(added_streams, &STR_TMP)));

		/* Then we go through and remove any ones that were removed */
		for (index = 0;
			removed_streams_count && index < ast_stream_topology_get_count(sc->topology); ++index) {
			struct ast_stream *stream = ast_stream_topology_get_stream(sc->topology, index);
			int removed_stream;

			for (removed_stream = 0; removed_stream < removed_streams_count; ++removed_stream) {
				if (is_video_dest(stream, ast_channel_name(bridge_channel->chan),
					removed_streams[removed_stream])) {
					ast_trace(-1, "%s/%s:    Removing stream %s\n",
						ast_channel_name(bridge_channel->chan),
						ast_channel_name(participant->chan),
						ast_str_tmp(256, ast_stream_to_str(stream, &STR_TMP)));
					ast_stream_set_state(stream, AST_STREAM_STATE_REMOVED);
				}
			}
		}
		ast_channel_request_stream_topology_change(participant->chan, participant_sc->topology, NULL);
		SCOPE_EXIT("%s/%s:   New participant topology %s\n",
			ast_channel_name(bridge_channel->chan),
			ast_channel_name(participant->chan),
			ast_str_tmp(256, ast_stream_topology_to_str(participant_sc->topology, &STR_TMP)));
	}

	ast_trace(-1, "%s: New topology %s\n", ast_channel_name(bridge_channel->chan),
		ast_str_tmp(256, ast_stream_topology_to_str(sc->topology, &STR_TMP)));

cleanup:
	ast_stream_topology_free(added_streams);
	SCOPE_EXIT();
}

/*!
 * \brief stream_topology_changed callback
 *
 * For most video modes, nothing beyond the ordinary is required.
 * For the SFU case, though, we need to completely remap the streams
 * in order to ensure video gets directed where it is expected to go.
 *
 * \param bridge The bridge
 * \param bridge_channel Channel whose topology has changed
 */
static void softmix_bridge_stream_topology_changed(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct softmix_bridge_data *softmix_data = bridge->tech_pvt;
	struct softmix_channel *sc = bridge_channel->tech_pvt;
	struct ast_bridge_channel *participant;
	struct ast_vector_int media_types;
	int nths[AST_MEDIA_TYPE_END] = {0};
	int idx;
	SCOPE_ENTER(3, "%s: \n", ast_channel_name(bridge_channel->chan));

	switch (bridge->softmix.video_mode.mode) {
	case AST_BRIDGE_VIDEO_MODE_NONE:
	case AST_BRIDGE_VIDEO_MODE_SINGLE_SRC:
	case AST_BRIDGE_VIDEO_MODE_TALKER_SRC:
	default:
		ast_bridge_channel_stream_map(bridge_channel);
		SCOPE_EXIT_RTN("%s: Not in SFU mode\n", ast_channel_name(bridge_channel->chan));
	case AST_BRIDGE_VIDEO_MODE_SFU:
		break;
	}

	ast_channel_lock(bridge_channel->chan);
	softmix_bridge_stream_sources_update(bridge, bridge_channel, sc);
	ast_channel_unlock(bridge_channel->chan);

	AST_VECTOR_INIT(&media_types, AST_MEDIA_TYPE_END);

	/* The bridge stream identifiers may change, so reset the mapping for them.
	 * When channels end up getting added back in they'll reuse their existing
	 * collector and won't need to allocate a new one (unless they were just added).
	 */
	for (idx = 0; idx < AST_VECTOR_SIZE(&softmix_data->remb_collectors); ++idx) {
		ao2_cleanup(AST_VECTOR_GET(&softmix_data->remb_collectors, idx));
		AST_VECTOR_REPLACE(&softmix_data->remb_collectors, idx, NULL);
	}

	/* First traversal: re-initialize all of the participants' stream maps */
	AST_LIST_TRAVERSE(&bridge->channels, participant, entry) {
		ast_bridge_channel_lock(participant);

		AST_VECTOR_RESET(&participant->stream_map.to_channel, AST_VECTOR_ELEM_CLEANUP_NOOP);
		AST_VECTOR_RESET(&participant->stream_map.to_bridge, AST_VECTOR_ELEM_CLEANUP_NOOP);

		sc = participant->tech_pvt;
		AST_VECTOR_RESET(&sc->video_sources, AST_VECTOR_ELEM_CLEANUP_NOOP);

		ast_bridge_channel_unlock(participant);
	}

	/* Second traversal: Map specific video channels from their source to their destinations.
	 *
	 * This is similar to what is done in ast_stream_topology_map(),
	 * except that video channels are handled differently.  Each video
	 * source has it's own unique index on the bridge.  This way, a
	 * particular channel's source video can be distributed to the
	 * appropriate destination streams on the other channels.
	 */
	AST_LIST_TRAVERSE(&bridge->channels, participant, entry) {
		int i;
		struct ast_stream_topology *topology;

		ast_bridge_channel_lock(participant);
		ast_channel_lock(participant->chan);

		topology = ao2_bump(ast_channel_get_stream_topology(participant->chan));
		if (!topology) {
			/* Oh, my, we are in trouble. */
			ast_channel_unlock(participant->chan);
			ast_bridge_channel_unlock(participant);
			continue;
		}

		for (i = 0; i < ast_stream_topology_get_count(topology); ++i) {
			struct ast_stream *stream = ast_stream_topology_get_stream(topology, i);

			if (is_video_source(stream)) {
				AST_VECTOR_APPEND(&media_types, AST_MEDIA_TYPE_VIDEO);
				AST_VECTOR_REPLACE(&participant->stream_map.to_bridge, i, AST_VECTOR_SIZE(&media_types) - 1);
				/*
				 * There are cases where we need to bidirectionally send frames, such as for REMB reports
				 * so we also map back to the channel.
				 */
				AST_VECTOR_REPLACE(&participant->stream_map.to_channel, AST_VECTOR_SIZE(&media_types) - 1, i);
				remb_enable_collection(bridge, participant, AST_VECTOR_SIZE(&media_types) - 1);
				/*
				 * Unlock the channel and participant to prevent
				 * potential deadlock in map_source_to_destinations().
				 */
				ast_channel_unlock(participant->chan);
				ast_bridge_channel_unlock(participant);

				map_source_to_destinations(ast_channel_name(participant->chan),
					AST_VECTOR_SIZE(&media_types) - 1, &bridge->channels, i);
				ast_bridge_channel_lock(participant);
				ast_channel_lock(participant->chan);
			} else if (ast_stream_get_type(stream) == AST_MEDIA_TYPE_VIDEO) {
				/* Video stream mapping occurs directly when a video source stream
				 * is found on a channel. Video streams should otherwise remain
				 * unmapped.
				 */
				AST_VECTOR_REPLACE(&participant->stream_map.to_bridge, i, -1);
			} else if (ast_stream_get_state(stream) != AST_STREAM_STATE_REMOVED) {
				/* XXX This is copied from ast_stream_topology_map(). This likely could
				 * be factored out in some way
				 */
				enum ast_media_type type = ast_stream_get_type(stream);
				int index = AST_VECTOR_GET_INDEX_NTH(&media_types, ++nths[type],
					type, AST_VECTOR_ELEM_DEFAULT_CMP);

				if (index == -1) {
					AST_VECTOR_APPEND(&media_types, type);
					index = AST_VECTOR_SIZE(&media_types) - 1;
				}

				AST_VECTOR_REPLACE(&participant->stream_map.to_bridge, i, index);
				AST_VECTOR_REPLACE(&participant->stream_map.to_channel, index, i);
			}
		}

		ast_stream_topology_free(topology);

		ast_channel_unlock(participant->chan);
		ast_bridge_channel_unlock(participant);
	}

	AST_VECTOR_FREE(&media_types);
	SCOPE_EXIT_RTN("%s\n", ast_channel_name(bridge_channel->chan));
}

static struct ast_bridge_technology softmix_bridge = {
	.name = "softmix",
	.capabilities = AST_BRIDGE_CAPABILITY_MULTIMIX,
	.preference = AST_BRIDGE_PREFERENCE_BASE_MULTIMIX,
	.create = softmix_bridge_create,
	.stop = softmix_bridge_stop,
	.destroy = softmix_bridge_destroy,
	.join = softmix_bridge_join,
	.leave = softmix_bridge_leave,
	.unsuspend = softmix_bridge_unsuspend,
	.write = softmix_bridge_write,
	.stream_topology_changed = softmix_bridge_stream_topology_changed,
};

#ifdef TEST_FRAMEWORK
struct stream_parameters {
	const char *name;
	const char *formats;
	enum ast_media_type type;
};

static struct ast_stream_topology *build_topology(const struct stream_parameters *params, size_t num_streams)
{
	struct ast_stream_topology *topology;
	size_t i;

	topology = ast_stream_topology_alloc();
	if (!topology) {
		return NULL;
	}

	for (i = 0; i < num_streams; ++i) {
		RAII_VAR(struct ast_format_cap *, caps, NULL, ao2_cleanup);
		struct ast_stream *stream;

		caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
		if (!caps) {
			goto fail;
		}
		if (ast_format_cap_update_by_allow_disallow(caps, params[i].formats, 1) < 0) {
			goto fail;
		}
		stream = ast_stream_alloc(params[i].name, params[i].type);
		if (!stream) {
			goto fail;
		}
		ast_stream_set_formats(stream, caps);
		if (ast_stream_topology_append_stream(topology, stream) < 0) {
			ast_stream_free(stream);
			goto fail;
		}
	}

	return topology;

fail:
	ast_stream_topology_free(topology);
	return NULL;
}

static int validate_stream(struct ast_test *test, struct ast_stream *stream,
	const struct stream_parameters *params)
{
	const struct ast_format_cap *stream_caps;
	struct ast_format_cap *params_caps;

	if (ast_stream_get_type(stream) != params->type) {
		ast_test_status_update(test, "Expected stream type '%s' but got type '%s'\n",
			ast_codec_media_type2str(params->type),
			ast_codec_media_type2str(ast_stream_get_type(stream)));
		return -1;
	}
	if (strcmp(ast_stream_get_name(stream), params->name)) {
		ast_test_status_update(test, "Expected stream name '%s' but got type '%s'\n",
			params->name, ast_stream_get_name(stream));
		return -1;
	}

	stream_caps = ast_stream_get_formats(stream);
	params_caps = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT);
	if (!params_caps) {
		ast_test_status_update(test, "Allocation error on capabilities\n");
		return -1;
	}
	ast_format_cap_update_by_allow_disallow(params_caps, params->formats, 1);

	if (!ast_format_cap_identical(stream_caps, params_caps)) {
		ast_test_status_update(test, "Formats are not as expected on stream '%s'\n",
			ast_stream_get_name(stream));
		ao2_cleanup(params_caps);
		return -1;
	}

	ao2_cleanup(params_caps);
	return 0;
}

static int validate_original_streams(struct ast_test *test, struct ast_stream_topology *topology,
	const struct stream_parameters *params, size_t num_streams)
{
	int i;

	if (ast_stream_topology_get_count(topology) < num_streams) {
		ast_test_status_update(test, "Topology only has %d streams. Needs to have at least %zu\n",
			ast_stream_topology_get_count(topology), num_streams);
		return -1;
	}

	for (i = 0; i < num_streams; ++i) {
		if (validate_stream(test, ast_stream_topology_get_stream(topology, i), &params[i])) {
			return -1;
		}
	}

	return 0;
}

AST_TEST_DEFINE(sfu_append_source_streams)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	static const struct stream_parameters bob_streams[] = {
		{ "bob_audio", "ulaw,alaw,g722,opus", AST_MEDIA_TYPE_AUDIO, },
		{ "bob_video", "h264,vp8", AST_MEDIA_TYPE_VIDEO, },
	};
	static const struct stream_parameters alice_streams[] = {
		{ "alice_audio", "ulaw,opus", AST_MEDIA_TYPE_AUDIO, },
		{ "alice_video", "vp8", AST_MEDIA_TYPE_VIDEO, },
	};
	static const struct stream_parameters alice_dest_stream = {
		"softbridge_dest_PJSIP/Bob-00000001_1", "h264,vp8", AST_MEDIA_TYPE_VIDEO,
	};
	static const struct stream_parameters bob_dest_stream = {
		"softbridge_dest_PJSIP/Alice-00000000_1", "vp8", AST_MEDIA_TYPE_VIDEO,
	};
	struct ast_stream_topology *topology_alice = NULL;
	struct ast_stream_topology *topology_bob = NULL;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sfu_append_source_streams";
		info->category = "/bridges/bridge_softmix/";
		info->summary = "Test appending of video streams";
		info->description =
			"This tests does stuff.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	topology_alice = build_topology(alice_streams, ARRAY_LEN(alice_streams));
	if (!topology_alice) {
		goto end;
	}

	topology_bob = build_topology(bob_streams, ARRAY_LEN(bob_streams));
	if (!topology_bob) {
		goto end;
	}

	if (append_source_streams(topology_alice, "PJSIP/Bob-00000001", NULL, topology_bob)) {
		ast_test_status_update(test, "Failed to append Bob's streams to Alice\n");
		goto end;
	}

	if (ast_stream_topology_get_count(topology_alice) != 3) {
		ast_test_status_update(test, "Alice's topology isn't large enough! It's %d but needs to be %d\n",
			ast_stream_topology_get_count(topology_alice), 3);
		goto end;
	}

	if (validate_original_streams(test, topology_alice, alice_streams, ARRAY_LEN(alice_streams))) {
		goto end;
	}

	if (validate_stream(test, ast_stream_topology_get_stream(topology_alice, 2), &alice_dest_stream)) {
		goto end;
	}

	if (append_source_streams(topology_bob, "PJSIP/Alice-00000000", NULL, topology_alice)) {
		ast_test_status_update(test, "Failed to append Alice's streams to Bob\n");
		goto end;
	}

	if (ast_stream_topology_get_count(topology_bob) != 3) {
		ast_test_status_update(test, "Bob's topology isn't large enough! It's %d but needs to be %d\n",
			ast_stream_topology_get_count(topology_bob), 3);
		goto end;
	}

	if (validate_original_streams(test, topology_bob, bob_streams, ARRAY_LEN(bob_streams))) {
		goto end;
	}

	if (validate_stream(test, ast_stream_topology_get_stream(topology_bob, 2), &bob_dest_stream)) {
		goto end;
	}

	res = AST_TEST_PASS;

end:
	ast_stream_topology_free(topology_alice);
	ast_stream_topology_free(topology_bob);
	return res;
}

AST_TEST_DEFINE(sfu_remove_destination_streams)
{
	enum ast_test_result_state res = AST_TEST_FAIL;
	static const struct stream_parameters params[] = {
		{ "alice_audio", "ulaw,alaw,g722,opus", AST_MEDIA_TYPE_AUDIO, },
		{ "alice_video", "h264,vp8", AST_MEDIA_TYPE_VIDEO, },
		{ "softbridge_dest_PJSIP/Bob-00000001_video", "vp8", AST_MEDIA_TYPE_VIDEO, },
		{ "softbridge_dest_PJSIP/Carol-00000002_video", "h264", AST_MEDIA_TYPE_VIDEO, },
	};
	static const struct {
		const char *channel_name;
		int num_streams;
		int params_index[4];
	} removal_results[] = {
		{ "PJSIP/Bob-00000001", 4, { 0, 1, 2, 3 }, },
		{ "PJSIP/Edward-00000004", 4, { 0, 1, 2, 3 }, },
		{ "", 4, { 0, 1, 2, 3 }, },
	};
	struct ast_stream_topology *orig = NULL;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "sfu_remove_destination_streams";
		info->category = "/bridges/bridge_softmix/";
		info->summary = "Test removal of destination video streams";
		info->description =
			"This tests does stuff.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	orig = build_topology(params, ARRAY_LEN(params));
	if (!orig) {
		ast_test_status_update(test, "Unable to build initial stream topology\n");
		goto end;
	}

	for (i = 0; i < ARRAY_LEN(removal_results); ++i) {
		int j;

		remove_destination_streams(orig, removal_results[i].channel_name);

		if (ast_stream_topology_get_count(orig) != removal_results[i].num_streams) {
			ast_test_status_update(test, "Resulting topology has %d streams, when %d are expected\n",
				ast_stream_topology_get_count(orig), removal_results[i].num_streams);
			goto end;
		}

		for (j = 0; j < removal_results[i].num_streams; ++j) {
			struct ast_stream *actual;
			struct ast_stream *expected;
			int orig_index;

			actual = ast_stream_topology_get_stream(orig, j);

			orig_index = removal_results[i].params_index[j];
			expected = ast_stream_topology_get_stream(orig, orig_index);

			if (!ast_format_cap_identical(ast_stream_get_formats(actual),
				ast_stream_get_formats(expected))) {
				struct ast_str *expected_str;
				struct ast_str *actual_str;

				expected_str = ast_str_alloca(64);
				actual_str = ast_str_alloca(64);

				ast_test_status_update(test, "Mismatch between expected (%s) and actual (%s) stream formats\n",
					ast_format_cap_get_names(ast_stream_get_formats(expected), &expected_str),
					ast_format_cap_get_names(ast_stream_get_formats(actual), &actual_str));
				goto end;
			}

			if (is_video_dest(actual, removal_results[i].channel_name, -1) &&
				ast_stream_get_state(actual) != AST_STREAM_STATE_REMOVED) {
				ast_test_status_update(test, "Removed stream %s does not have a state of removed\n", ast_stream_get_name(actual));
				goto end;
			}
		}
	}

	res = AST_TEST_PASS;

end:
	ast_stream_topology_free(orig);
	return res;
}

#endif

static int unload_module(void)
{
	ast_bridge_technology_unregister(&softmix_bridge);
	AST_TEST_UNREGISTER(sfu_append_source_streams);
	AST_TEST_UNREGISTER(sfu_remove_destination_streams);
	return 0;
}

static int load_module(void)
{
	if (ast_bridge_technology_register(&softmix_bridge)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	AST_TEST_REGISTER(sfu_append_source_streams);
	AST_TEST_REGISTER(sfu_remove_destination_streams);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Multi-party software based channel mixing");
