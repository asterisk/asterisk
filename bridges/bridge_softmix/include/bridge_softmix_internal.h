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
 * \brief Multi-party software based channel mixing (header)
 *
 * \author Joshua Colp <jcolp@digium.com>
 * \author David Vossel <dvossel@digium.com>
 *
 * \ingroup bridges
 */

#ifndef _ASTERISK_BRIDGE_SOFTMIX_INTERNAL_H
#define _ASTERISK_BRIDGE_SOFTMIX_INTERNAL_H

#include "asterisk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/bridge.h"
#include "asterisk/bridge_technology.h"
#include "asterisk/frame.h"
#include "asterisk/options.h"
#include "asterisk/logger.h"
#include "asterisk/slinfactory.h"
#include "asterisk/astobj2.h"
#include "asterisk/timing.h"
#include "asterisk/translate.h"
#include "asterisk/rtp_engine.h"
#include "asterisk/vector.h"

#ifdef BINAURAL_RENDERING
#include <fftw3.h>
#endif

#if defined(__Darwin__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__NetBSD__) || defined(__CYGWIN__)
#include <float.h>
#else
#include <values.h>
#endif

#define MAX_DATALEN 8096
#define DEFAULT_ENERGY_HISTORY_LEN 150

/*! Setting the sample rate to 48000 by default if binaural is activated. */
#define SOFTMIX_BINAURAL_SAMPLE_RATE 48000
/*! We only support 20 ms interval length with binaural data at the moment. */
#define BINAURAL_MIXING_INTERVAL 20

struct convolve_channel {
	/*! The head related transfer function used for convolving */
	double *hrtf;
	/*! Input signals for fftw */
	double *fftw_in;
	/*! Output signals from the fftw */
	double *fftw_out;
	/*! Signals for overlap add */
	float *overlap_add;
	/*! The resulting data after the convolution */
	int16_t *out_data;
#ifdef BINAURAL_RENDERING
	/*! The fftw plan for binaural signaling */
	fftw_plan fftw_plan;
	/*! The inverse fftw plan for binaural signaling */
	fftw_plan fftw_plan_inverse;
#endif
};

struct convolve_channel_pair {
	/*! The left channel of a stereo channel pair */
	struct convolve_channel chan_left;
	/*! The right channel of a stereo channel pair */
	struct convolve_channel chan_right;
};

struct convolve_data {
	/*! A count of all channels potentialy having input data for the conference. */
	int number_channels;
	/*! Will set to true if there is at least one binaural output.
	 * Only if set to true data will be convolved. */
	int binaural_active;
	/*! The length of the head related transfer function */
	unsigned int hrtf_length;
	/*! Number of channels available for convolving.
	 * We do not delete a channel when a member leaves, cause we can reuse it for the next one. */
	int chan_size;
	/*! The positions of the single channels in the virtual room */
	int *pos_ids;
	/*! Each channel has a stereo pair of channels for the convolution */
	struct convolve_channel_pair **cchan_pair;
};

struct video_follow_talker_data {
	/*! audio energy history */
	int energy_history[DEFAULT_ENERGY_HISTORY_LEN];
	/*! The current slot being used in the history buffer, this
	 *  increments and wraps around */
	int energy_history_cur_slot;
	/*! The current energy sum used for averages. */
	int energy_accum;
	/*! The current energy average */
	int energy_average;
};

struct softmix_remb_collector;

/*! \brief Structure which contains per-channel mixing information */
struct softmix_channel {
	/*! Lock to protect this structure */
	ast_mutex_t lock;
	/*! Factory which contains audio read in from the channel */
	struct ast_slinfactory factory;
	/*! Frame that contains mixed audio to be written out to the channel */
	struct ast_frame write_frame;
	/*! Current expected read slinear format. */
	struct ast_format *read_slin_format;
	/*! DSP for detecting silence */
	struct ast_dsp *dsp;
	/*!
	 * \brief TRUE if a channel is talking.
	 *
	 * \note This affects how the channel's audio is mixed back to
	 * it.
	 */
	unsigned int talking:1;
	/*! TRUE if the channel provided audio for this mixing interval */
	unsigned int have_audio:1;
	/*! We set binaural also as channel data, to have better tracking.
	 *  It is also present in transpvt.
	 */
	unsigned int binaural:1;
	/*! TRUE if this is an announcement channel (data will not be convolved) */
	unsigned int is_announcement:1;
	/*! The position of the channel in the virtual room represented by an id
	 *	This ID has to be set even if the channel has no binaural output!
	 */
	unsigned int binaural_pos;
	/*! The channel pair for this channel */
	struct convolve_channel_pair *our_chan_pair;
	/*! Marks the channel for suspending all binaural activity on the output */
	unsigned int binaural_suspended:1;
	/*! Channel sample rate, stored to retrieve it after unsuspending the channel */
	int rate;
	/*! Buffer containing final mixed audio from all sources */
	short final_buf[MAX_DATALEN];
	/*! Buffer containing only the audio from the channel */
	short our_buf[MAX_DATALEN];
	/*! Data pertaining to talker mode for video conferencing */
	struct video_follow_talker_data video_talker;
	/*! The ideal stream topology for the channel */
	struct ast_stream_topology *topology;
	/*! The latest REMB report from this participant */
	struct ast_rtp_rtcp_feedback_remb remb;
	/*! The REMB collector for this channel, collects REMB from all video receivers */
	struct softmix_remb_collector *remb_collector;
	/*! The bridge streams which are feeding us video sources */
	AST_VECTOR(, int) video_sources;
};

struct softmix_bridge_data {
	struct ast_timer *timer;
	/*!
	 * \brief Bridge pointer passed to the softmix mixing thread.
	 *
	 * \note Does not need a reference because the bridge will
	 * always exist while the mixing thread exists even if the
	 * bridge is no longer actively using the softmix technology.
	 */
	struct ast_bridge *bridge;
	/*! Lock for signaling the mixing thread. */
	ast_mutex_t lock;
	/*! Condition, used if we need to wake up the mixing thread. */
	ast_cond_t cond;
	/*! Thread handling the mixing */
	pthread_t thread;
	unsigned int internal_rate;
	unsigned int internal_mixing_interval;
	/*! TRUE if the mixing thread should stop */
	unsigned int stop:1;
	/*! The default sample size (e.g. using Opus at 48khz and 20 ms mixing
	 * interval, sample size is 960) */
	unsigned int default_sample_size;
	/*! All data needed for binaural signaling */
	struct convolve_data convolve;
	/*! TRUE if the first attempt to init binaural rendering data was done
	 * (does not guarantee success)
	 */
	unsigned int binaural_init;
	/*! The last time a video update was sent into the bridge */
	struct timeval last_video_update;
	/*! The last time a REMB frame was sent to each source of video */
	struct timeval last_remb_update;
	/*! Per-bridge stream REMB collectors, which flow back to video source */
	AST_VECTOR(, struct softmix_remb_collector *) remb_collectors;
	/*! Per-bridge REMB bitrate */
	float bitrate;
};

struct softmix_mixing_array {
	unsigned int max_num_entries;
	unsigned int used_entries;
	int16_t **buffers;
	/*! Stereo channel pairs used to store convolved binaural signals */
	struct convolve_channel_pair **chan_pairs;
};

/*!
 * \brief Deletes left over signals on a channel that it can be reused.
 *
 * \param channel_pair The channel pair which contains the left and right audio channel.
 * \param default_sample_size The sample size which the channel pair uses.
 */
void reset_channel_pair(struct convolve_channel_pair *channel_pair,
		unsigned int default_sample_size);

/*!
 * \brief Randomly changes the virtual positions of conference participants.
 *
 * \param softmix_data The structure containing all position informations.
 */
void random_binaural_pos_change(struct softmix_bridge_data *softmix_data);

/*!
 * \brief Binaural convolving of audio data for a channel.
 *
 * \param chan The channel that will contain the binaural audio data as result.
 * \param in_samples The audio data which will be convolved.
 * \param in_sample_size The size of the audio data.
 * \param hrtf_length The length of the head related transfer function used to convolve the audio.
 *
 * \retval 0 success
 * \retval -1 failure
 */
int do_convolve(struct convolve_channel *chan, int16_t *in_samples,
		unsigned int in_sample_size, unsigned int hrtf_length);

/*!
 * \brief Binaural convolving of audio data for a channel pair (left and right channel).
 *
 * \param data  Contains the left and right audio channel.
 * \param pos_id The position the channel has in the virtual enviroment.
 * \param in_samples The audio data which will be convolved for both channels.
 * \param in_sample_size The size of the audio data.
 * \param channel_name The name of the channel
 *
 * \retval The channel pair with convolved audio on success.
 * \retval NULL failure
 */
struct convolve_channel_pair *do_convolve_pair(struct convolve_data *data,
		unsigned int pos_id, int16_t *in_samples, unsigned int in_sample_size,
		const char *channel_name);

/*!
 * \brief Provides a head related impulse response for the given position in the virtual
 * enviroment.
 *
 * \param chan_pos The position of the channel in the virtual enviroment.
 * \param chan_side 0 for the left audio channel, 1 for the right.
 *
 * \retval The hrir for the given position in the virtual room for either the left or right
 *  channels.
 * \retval NULL on failure.
 *
 */
float *get_hrir(unsigned int chan_pos, unsigned int chan_side);

/*!
 * \brief Initializes all data needed for binaural audio processing.
 *
 * \param channel The channel used for binaural audio processing.
 * \param hrtf_len The length of the head related impulse response used for binaural processing.
 * \param chan_pos The position of the channel in the virtual enviroment.
 * \param chan_side 0 for the left audio channel, 1 for the right.
 * \param default_sample_size The default size of audio samples.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int init_convolve_channel(struct convolve_channel *channel, unsigned int hrtf_len,
		unsigned int chan_pos, unsigned int chan_side, unsigned int default_sample_size);

/*!
 * \brief Initializies all data needed for binaural audio processing of a channel pair
 * (left and right).
 *
 * \param cchan_pair The channel pair used for binaural audio processing.
 * \param hrtf_len The length of the head related impulse response used for binaural processing.
 * \param chan_pos The position of the channel in the virtual enviroment.
 * \param default_sample_size The default size of audio samples.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int init_convolve_channel_pair(struct convolve_channel_pair *cchan_pair,
		unsigned int hrtf_len, unsigned int chan_pos, unsigned int default_sample_size);

/*!
 * \brief Preinits a specific number of channels (CONVOVLE_CHANNEL_PREALLOC)
 * at the beginning of a conference.
 *
 * \param data Contains all channels and data needed for binaural processing
 *  (e.g. head related transfer functions).
 * \param default_sample_size The default size of audio samples.
 *
 * \retval 0 on success
 * \retval -1 on failure
 */
int init_convolve_data(struct convolve_data *data, unsigned int default_sample_size);

/*!
 * \brief Frees all data needed for binaural processing by an audio channel.
 *
 * \param cchan The channel to clean up.
 */
void free_convolve_channel(struct convolve_channel *cchan);

/*!
 * \brief Frees all data needed for binaural processing by a pair of audio channels
 *  (left and right).
 *
 * \param cchan_pair The channel pair to clean up.
 */
void free_convolve_channel_pair(struct convolve_channel_pair *cchan_pair);

/*!
 * \brief Frees all channels and data needed for binaural audio processing.
 *
 * \param data Contains all channels and data for the cleanup process.
 */
void free_convolve_data(struct convolve_data *data);

/*!
 * \brief Joins a channel into a virtual enviroment build with the help of binaural sythesis.
 *
 * \param data Contains all channels and data needed for binaural processing
 *  (e.g. head related transfer functions).
 * \param default_sample_size The default size of audio samples.
 *
 * \retval The position of the channel in the virtual enviroment.
 * \retval -1 on failure
 */
int set_binaural_data_join(struct convolve_data *data, unsigned int default_sample_size);

/*!
 * \brief Removes a channel from the binaural conference bridge. Marks the position in
 *  the virtual room as unused that it can be reused by the next channel which enters the
 *  conference.
 *
 * \param data Contains all channels and data needed for binaural processing
 *  (e.g. head related transfer functions).
 * \param pos The position of the channel in the virtual enviroment.
 * \param default_sample_size The default size of audio samples.
 */
void set_binaural_data_leave(struct convolve_data *data, unsigned int pos,
		unsigned int default_sample_size);

/*!
 * \brief Writes the binaural audio to a channel.
 *
 * \param sc The softmix channel.
 * \param default_sample_size The default size of audio samples.
 */
void softmix_process_write_binaural_audio(struct softmix_channel *sc,
		unsigned int default_sample_size);

/*!
 * \brief Checks if a position change in the virual enviroment is requested by one of
 * the participants.
 *
 * \param bridge The conference bridge.
 * \param softmix_data The data used by the softmix bridge.
 */
void check_binaural_position_change(struct ast_bridge *bridge,
		struct softmix_bridge_data *softmix_data);

/*!
 * \brief Processes audio data with the binaural synthesis and adds the result to the mixing array.
 *
 * \param bridge The conference bridge needed to check if binaural processing is active or not.
 * \param softmix_data Contains all data for the softmix bridge and for the binaural processing.
 * \param softmix_samples The sample size.
 * \param mixing_array The array which holds all audio data for mixing.
 * \param sc The channel which contains the audio data to process.
 * \param channel_name The name of the channel
 */
void add_binaural_mixing(struct ast_bridge *bridge, struct softmix_bridge_data *softmix_data,
		unsigned int softmix_samples, struct softmix_mixing_array *mixing_array,
		struct softmix_channel *sc, const char *channel_name);

/*!
 * \brief Mixes all binaural audio data contained in the mixing array.
 *
 * \param bridge The conference bridge needed to check if binaural processing is active or not.
 * \param softmix_data Contains all data for the softmix bridge and for the binaural processing.
 * \param mixing_array The array which holds all audio data for mixing.
 * \param bin_buf The buffer that will contain the mixing results.
 * \param ann_buf The buffer that will contain mixed announcements in an interleaved format.
 */
void binaural_mixing(struct ast_bridge *bridge, struct softmix_bridge_data *softmix_data,
		struct softmix_mixing_array *mixing_array, int16_t *bin_buf, int16_t *ann_buf);

/*!
 * \brief Creates a frame out of binaural audio data.
 *
 * \param bridge_channel Contains the information if binaural processing is active or not.
 *  If active binaural audio data will be copied, if not mono data will be provided in an
 *  interleaved format.
 * \param sc The softmix channel holding all informations for the process.
 * \param bin_buf The buffer that contains all mixing results.
 * \param ann_buf The buffer that contains mixed announcements in an interleaved format.
 * \param softmix_datalen The size of the audio data.
 * \param softmix_samples The number of audio samples.
 * \param buf The buffer that contains all mono mixing results, used if binaural processing is
 *  inactive.
 */
void create_binaural_frame(struct ast_bridge_channel *bridge_channel,
		struct softmix_channel *sc, int16_t *bin_buf, int16_t *ann_buf,
		unsigned int softmix_datalen, unsigned int softmix_samples, int16_t *buf);

#endif /* _ASTERISK_BRIDGE_SOFTMIX_INTERNAL_H */
