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
	<depend>FFTW3</depend>
 ***/

#include "asterisk.h"

ASTERISK_REGISTER_FILE()

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

#include "asterisk/interleaved_stereo.h"
#include <fftw3.h>
#include "asterisk/impulses.h"

#if defined(__Darwin__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__CYGWIN__)
#include <float.h>
#else
#include <values.h>
#endif

#define MAX_DATALEN 8096

/*! The minimum sample rate of the bridge. */
#define SOFTMIX_MIN_SAMPLE_RATE 8000	/* 8 kHz sample rate */
/*! Setting the sample rate to 48000 by default if binaural is activated. */
#define SOFTMIX_BINAURAL_SAMPLE_RATE 48000	

/*! \brief Interval at which mixing will take place. Valid options are 10, 20, and 40. */
#define DEFAULT_SOFTMIX_INTERVAL 20

/*! \brief Size of the buffer used for sample manipulation */
#define SOFTMIX_DATALEN(rate, interval) ((rate/50) * (interval / 10))

/*! \brief Number of samples we are dealing with */
#define SOFTMIX_SAMPLES(rate, interval) (SOFTMIX_DATALEN(rate, interval) / 2)

/*! \brief Number of mixing iterations to perform between gathering statistics. */
#define SOFTMIX_STAT_INTERVAL 100

/* This is the threshold in ms at which a channel's own audio will stop getting
 * mixed out its own write audio stream because it is not talking. */
#define DEFAULT_SOFTMIX_SILENCE_THRESHOLD 2500
#define DEFAULT_SOFTMIX_TALKING_THRESHOLD 160

#define DEFAULT_ENERGY_HISTORY_LEN 150

/*! The number of prealloced channels when a bridge will be created. */
#define CONVOLVE_CHANNEL_PREALLOC 3
/*! Max size of the convolve buffer. */
#define CONVOLVE_MAX_BUFFER 4096
/*! The default sample size in an binaural enviroment with a stereo codec at 48kHz. */
#define DEFAULT_SAMPLE_SIZE 960

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

struct convolve_channel {
	/*! The fftw plan for binaural signaling */
	fftw_plan fftw_plan;
	/*! The inverse fftw plan for binaural signaling */
	fftw_plan fftw_plan_inverse;
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
};

struct convolve_channel_pair {
	/*! The left channel of a stereo channel pair */
	struct convolve_channel chan_left;
	/*! The right channel of a stereo channel pair */
	struct convolve_channel chan_right;
};

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
	/*! Channel sample rate, stored to retrieve it after unsuspending the channel */
	int rate;
	/*! The position of the channel in the virtual room represented by an id 
	 *	This ID has to be set even if the channel has no binaural output!
	 */
	unsigned int binaural_pos; //If pos id == -1 it will skip convolving
	/*! Buffer containing final mixed audio from all sources */
	short final_buf[MAX_DATALEN];
	/*! Buffer containing only the audio from the channel */
	short our_buf[MAX_DATALEN];
	/*! The channel pair for this channel */
	struct convolve_channel_pair *our_chan_pair;
	/*! Data pertaining to talker mode for video conferencing */
	struct video_follow_talker_data video_talker;
	/*! Marks the channel for suspending all binaural activity on the output */
	unsigned int binaural_suspended:1;
};

struct convolve_data {
	/*! A count of all channels potentialy having input data for the conference. */
	int number_channels;
	/*! Will set to true if there is at least one binaural output.
	 * Only if set to true data will be convolved. */
	int binaural_active; 
	/*! The length of the head related transfer function */
	int hrtf_length;
	/* Number of channels available for convolving. 
	 * We do not delete a channel when a member leaves, cause we can reuse it for the next one. */
	int chan_size;   
	/*! The positions of the single channels in the virtual room */
	int *pos_ids;
	/*! Each channel has a stereo pair of channels for the convolution */ 
	struct convolve_channel_pair **cchan_pair;
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
	/*! The default sample size (e.g. using Opus at 48khz and 20 ms mixing 
	 * interval, sample size is 960) */
	unsigned int default_sample_size;
	/*! TRUE if the mixing thread should stop */
	unsigned int stop:1;
	/*! All data needed for binaural signaling */
	struct convolve_data convolve;
};

struct softmix_stats {
	/*! Each index represents a sample rate used above the internal rate. */
	unsigned int sample_rates[16];
	/*! Each index represents the number of channels using the same index in the sample_rates array.  */
	unsigned int num_channels[16];
	/*! The number of channels above the internal sample rate */
	unsigned int num_above_internal_rate;
	/*! The number of channels at the internal sample rate */
	unsigned int num_at_internal_rate;
	/*! The absolute highest sample rate preferred by any channel in the bridge */
	unsigned int highest_supported_rate;
	/*! Is the sample rate locked by the bridge, if so what is that rate.*/
	unsigned int locked_rate;
};

struct softmix_mixing_array {
	unsigned int max_num_entries;
	unsigned int used_entries;
	int16_t **buffers;
	/*! Stereo channel pairs used to store convolved binaural signals */
	struct convolve_channel_pair **chan_pairs;
};

struct softmix_translate_helper_entry {
	int num_times_requested; /*!< Once this entry is no longer requested, free the trans_pvt
	                              and re-init if it was usable. */
	struct ast_format *dst_format; /*!< The destination format for this helper */
	struct ast_trans_pvt *trans_pvt; /*!< the translator for this slot. */
	struct ast_frame *out_frame; /*!< The output frame from the last translation */
	AST_LIST_ENTRY(softmix_translate_helper_entry) entry;
};

static void reset_channel_pair(struct convolve_channel_pair *channel_pair, unsigned int default_sample_size) {
	memset(channel_pair->chan_left.overlap_add, 0, sizeof(float) * default_sample_size);
	memset(channel_pair->chan_right.overlap_add, 0, sizeof(float) * default_sample_size);
}

static void random_binaural_pos_change(struct softmix_bridge_data *softmix_data) 
{
	/* We perform a shuffle of all channels, even the ones that aren't used at the moment of shuffling now. 
	 * This has the efect that new members will be placed randomly too. */
	int i;
	int j;
	struct convolve_channel_pair *tmp;
	if (softmix_data->convolve.chan_size > 1) {
		srand(time(NULL));
		for (i = softmix_data->convolve.chan_size - 1; i > 0; i--) {
			j = rand() % (i + 1);
			tmp = softmix_data->convolve.cchan_pair[i];
			reset_channel_pair(tmp, softmix_data->default_sample_size);
			softmix_data->convolve.cchan_pair[i] = softmix_data->convolve.cchan_pair[j];
			softmix_data->convolve.cchan_pair[j] = tmp;
		}
	} 
}

static int do_convolve(struct convolve_channel *chan, int16_t *in_samples, int in_sample_size, int hrtf_length) 
{ 
	int i;
	if (in_sample_size != DEFAULT_SAMPLE_SIZE) {
		ast_log(LOG_ERROR, "Invalid sample size (%d) for binaural convole.\n", in_sample_size);
		return -1;	
	}
	//FFT setting real part
	for (i = 0; i < DEFAULT_SAMPLE_SIZE; i++) {
		chan->fftw_in[i] = in_samples[i] * (FLT_MAX / SHRT_MAX);
	}
	for (i = DEFAULT_SAMPLE_SIZE; i < hrtf_length; i++) {
		chan->fftw_in[i] = 0;
	}
	fftw_execute(chan->fftw_plan);
	//imaginary mulitplication (frequency space)
	chan->fftw_in[0] = chan->fftw_out[0] * chan->hrtf[0];//first FFTW result has never an imaginary part
	for (i = 1; i < hrtf_length / 2; i++) {
		chan->fftw_in[i] = chan->fftw_out[i] * chan->hrtf[i] - chan->fftw_out[hrtf_length - i] * chan->hrtf[hrtf_length - i]; //real part
		chan->fftw_in[hrtf_length - i] = chan->fftw_out[i] * chan->hrtf[hrtf_length - i] + chan->fftw_out[hrtf_length - i] * chan->hrtf[i];//imaginary part
	}
	if (hrtf_length % 2 == 0) {//the last (if even) FFTW result has never an imaginary part
		chan->fftw_in[hrtf_length / 2] = chan->fftw_out[hrtf_length / 2] * chan->hrtf[hrtf_length / 2];
	}
	//iFFT
	fftw_execute(chan->fftw_plan_inverse);
	//remove signal increase due to iFFT
	for (i = 0; i < hrtf_length; i++) { 
		chan->fftw_out[i] = chan->fftw_out[i] / (hrtf_length / 2);
	}
	//save the block for overlapp add in the next itteration
	for (i = 0; i < in_sample_size; i++) {
		chan->overlap_add[i] += chan->fftw_out[i];
	}
	//copy real part to the output, ignore the complex part
	for (i = 0; i < in_sample_size; i++) {
		chan->out_data[i] = chan->overlap_add[i] * (SHRT_MAX / FLT_MAX);
		chan->overlap_add[i] = chan->fftw_out[i + in_sample_size];
	}
	return 0;
}

static struct convolve_channel_pair *do_convolve_pair(struct convolve_data *data, unsigned int pos_id, int16_t *in_samples, int in_sample_size, int binaural_active) 
{
	int status;
	if (data->pos_ids[pos_id] == 1) { //If a position has no active member we will not convolve 
		struct convolve_channel_pair *chan_pair = data->cchan_pair[pos_id];
		status = do_convolve(&chan_pair->chan_left, in_samples, in_sample_size, data->hrtf_length);
		if (status == -1) {
			return NULL;
		}
		status = do_convolve(&chan_pair->chan_right, in_samples, in_sample_size, data->hrtf_length);
		if (status == -1) {
			return NULL;
		}
		return chan_pair;
	}
	ast_log(LOG_ERROR, "Channel pair has no active member! (pos id= %d)\n", pos_id);
	return NULL;
}

static float *get_hrir(int chan_pos, int position) {
	if (position == IMPULSE_CHANNEL_LEFT) {
		return hrirs_left[positions[chan_pos]];
	} else if (position == IMPULSE_CHANNEL_RIGHT) {
		return hrirs_right[positions[chan_pos]];
	}
	ast_log(LOG_ERROR, "Initialization of the convole function can't be process, could not determine if its the left or right channel.\n");
	return NULL;
}

static void init_convolve_channel(struct convolve_channel *channel, int hrtf_len, unsigned int chan_pos, unsigned int chan_side, unsigned int default_sample_size)
{
	int j;
	float *hrir;
	//Prepare FFTW
	channel->fftw_in = fftw_alloc_real(hrtf_len + 1);
	channel->fftw_out = fftw_alloc_real(hrtf_len + 1);
	memset(channel->fftw_in, 0, sizeof(double) * (hrtf_len + 1));
	memset(channel->fftw_out, 0, sizeof(double) * (hrtf_len + 1));
	channel->fftw_plan = fftw_plan_r2r_1d(hrtf_len, channel->fftw_in, channel->fftw_out, FFTW_R2HC, FFTW_PATIENT);
	channel->fftw_plan_inverse = fftw_plan_r2r_1d(hrtf_len, channel->fftw_in, channel->fftw_out, FFTW_HC2R, FFTW_PATIENT);
	channel->out_data = calloc(CONVOLVE_MAX_BUFFER, sizeof(int16_t));
	chan_pos = chan_pos % IMPULSE_SIZE;
	//Get HRTF for the channels spatial position
	hrir = get_hrir(chan_pos, chan_side);
	for (j = 0; j < IMPULSE_LEN; j++) {
		channel->fftw_in[j] = hrir[j];
	}
	for (j = IMPULSE_LEN; j < hrtf_len; j++) {
		channel->fftw_in[j] = 0;
	}
	fftw_execute(channel->fftw_plan);
	channel->hrtf = fftw_alloc_real(hrtf_len);
	for (j = 0; j < hrtf_len; j++) {
		channel->hrtf[j] = channel->fftw_out[j];
	}
	channel->overlap_add = calloc(default_sample_size, sizeof(float));
}

static void init_convolve_channel_pair(struct convolve_channel_pair *cchan_pair, int hrtf_len, unsigned int chan_pos, unsigned int default_sample_size) 
{
	int hrirs_pos = chan_pos * 2;
	ast_debug(3, "Binaural pos for the new channel pair will be L: %d R: %d (pos id = %d)\n", hrirs_pos, hrirs_pos + 1, chan_pos);
	init_convolve_channel(&cchan_pair->chan_left, hrtf_len, chan_pos, IMPULSE_CHANNEL_LEFT, default_sample_size);
	init_convolve_channel(&cchan_pair->chan_right, hrtf_len, chan_pos, IMPULSE_CHANNEL_RIGHT, default_sample_size);
}

static void init_convolve_data(struct convolve_data *data, unsigned int default_sample_size) 
{
	unsigned int i;
	data->pos_ids = calloc(sizeof(int), sizeof(int) * CONVOLVE_CHANNEL_PREALLOC);
	data->chan_size = CONVOLVE_CHANNEL_PREALLOC;
	data->number_channels = 0;
	data->cchan_pair = malloc(sizeof(struct convolve_channel_pair *) * CONVOLVE_CHANNEL_PREALLOC);
	for (i = 0; i < CONVOLVE_CHANNEL_PREALLOC; i++)
		data->cchan_pair[i] = malloc(sizeof(struct convolve_channel_pair));
	data->hrtf_length = default_sample_size * 2 - 1;
	for (i = 0; i < CONVOLVE_CHANNEL_PREALLOC; i++) 
		init_convolve_channel_pair(data->cchan_pair[i], data->hrtf_length, i, default_sample_size);
}

static void free_convolve_channel(struct convolve_channel *cchan) 
{
	fftw_free(cchan->fftw_out);
	fftw_free(cchan->fftw_in);
	fftw_free(cchan->hrtf);
	free(cchan->overlap_add);
	free(cchan->out_data);
	fftw_destroy_plan(cchan->fftw_plan);
	fftw_destroy_plan(cchan->fftw_plan_inverse);
}

static void free_convolve_channel_pair(struct convolve_channel_pair *cchan_pair) 
{
	free_convolve_channel(&cchan_pair->chan_left);
	free_convolve_channel(&cchan_pair->chan_right);
}

static void free_convolve_data(struct convolve_data *data) 
{
	int i;
	free(data->pos_ids);
	for (i = 0; i < data->chan_size; i++) {
		free_convolve_channel_pair(data->cchan_pair[i]);
		free(data->cchan_pair[i]);
	}
	free(data->cchan_pair);
}

static int set_binaural_data_join(struct convolve_data *data, unsigned int default_sample_size) 
{
	int *tmp;
	int i;
	struct convolve_channel_pair **tmp2;
	/* Raise the number of input channels. */
	data->number_channels++;
	/* We realloc another channel pair if we are out of prealloced ones. */
	/* We have prealloced one at the beginning of a conference and if a member leaves. */
	if (data->chan_size < data->number_channels)  {
		data->chan_size += 1;
		tmp = realloc(data->pos_ids, data->chan_size * sizeof(int));
		if (tmp) {
			data->pos_ids = tmp;
		} else {
			ast_log(LOG_ERROR, "Realloc of binaural channels failed, probably runing out of memory...");
		}
		data->pos_ids[data->chan_size - 1] = 0;
		tmp2 = realloc(data->cchan_pair, data->chan_size * sizeof(struct convolve_channel_pair *));
		if (tmp2) {
			data->cchan_pair = tmp2;
		} else {
			ast_log(LOG_ERROR, "Realloc of binaural channels failed, probably runing out of memory...");
		}
		data->cchan_pair[data->chan_size - 1] = malloc(sizeof(struct convolve_channel_pair));
		init_convolve_channel_pair(data->cchan_pair[data->chan_size - 1], data->hrtf_length, data->chan_size - 1, default_sample_size);
	}
	for (i = 0; i < data->chan_size; i++) {
		if (data->pos_ids[i] == 0) {
			data->pos_ids[i] = 1;
			break;
		}
	}
	return i;
}

static void set_binaural_data_leave(struct convolve_data *data, unsigned int pos, unsigned int default_sample_size) 
{
	ast_debug(3, "Binaural channel leaves the bridge\n");
	if (pos < data->chan_size) {
		if (data->pos_ids[pos] != 0) {
			reset_channel_pair(data->cchan_pair[pos], default_sample_size);
			data->number_channels--;
			data->pos_ids[pos] = 0;
		}  
	}   
}

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

static inline void softmix_process_write_binaural_audio(struct softmix_channel *sc, unsigned int default_sample_size)
{
	int i;
	if (sc->write_frame.samples % default_sample_size == 0) { 
		int k = 0;
		/* Keep track if binaural is suspended or not, the source of own audio to remove will 
		 * different */ 
		if (sc->binaural_suspended) {
			for (i = 0; i < default_sample_size; i++) {
				ast_slinear_saturated_subtract(&sc->final_buf[k], &sc->our_buf[i]);
				ast_slinear_saturated_subtract(&sc->final_buf[k + 1], &sc->our_buf[i]);
				k += 2;
			}
		} else {
			for (i = 0; i < default_sample_size; i++) {
				ast_slinear_saturated_subtract(&sc->final_buf[k], &sc->our_chan_pair->chan_left.out_data[i]);
				ast_slinear_saturated_subtract(&sc->final_buf[k + 1], &sc->our_chan_pair->chan_right.out_data[i]);
				k += 2;
			}
		}
	} 
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

	/* If we provided audio that was not determined to be silence,
	 * then take it out while in slinear format. */
	//when we have binaural audio we can't do the saturated substract
	if (sc->have_audio && sc->talking && !sc->binaural) {
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
	} else if (sc->have_audio && sc->talking && sc->binaural > 0) {
		softmix_process_write_binaural_audio(sc, default_sample_size);
		return;
	}

	/* Attempt to optimize channels using the same translation path/codec. Build a list of entries
	   of translation paths and track the number of references for each type. Each one of the same
	   type should be able to use the same out_frame. Since the optimization is only necessary for
	   multiple channels (>=2) using the same codec make sure resources are allocated only when
	   needed and released when not (see also softmix_translate_helper_cleanup */
	AST_LIST_TRAVERSE(&trans_helper->entries, entry, entry) {
		if (sc->binaural == 0) {
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
			if (entry->out_frame && (entry->out_frame->datalen < MAX_DATALEN)) {
				ao2_replace(sc->write_frame.subclass.format, entry->out_frame->subclass.format);
				memcpy(sc->final_buf, entry->out_frame->data.ptr, entry->out_frame->datalen);
				sc->write_frame.datalen = entry->out_frame->datalen;
				sc->write_frame.samples = entry->out_frame->samples;
			}
			break;
		}
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
	if (interval != IMPULSE_FIXED_MIXING_INTERVAL) {
		interval = IMPULSE_FIXED_MIXING_INTERVAL;
	}

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
 *
 * \return Nothing
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
	struct softmix_channel *sc = bridge_channel->tech_pvt;
	if (sc->binaural) {
		/* Restore some usefull data if it was a binaural channel */
		struct ast_format *slin_format;
		slin_format = ast_format_cache_get_slin_by_rate(sc->rate);
		ast_set_write_format_interleaved_stereo(bridge_channel->chan, slin_format);		
	}
	if (bridge->tech_pvt) {
		softmix_poke_thread(bridge->tech_pvt);
	}
}

/*!
 * \internal
 * \brief Indicate a source change to the channel.
 * \since 12.0.0
 *
 * \param bridge_channel Which channel source is changing.
 *
 * \retval 0 on success.
 * \retval -1 on error.
 */
static int softmix_src_change(struct ast_bridge_channel *bridge_channel)
{
	return ast_bridge_channel_queue_control_data(bridge_channel, AST_CONTROL_SRCCHANGE, NULL, 0);
}

/*! \brief Function called when a channel is joined into the bridge */
static int softmix_bridge_join(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{
	struct softmix_channel *sc;
	struct softmix_bridge_data *softmix_data;
	int set_binaural = 0;
	/* If false, the channel will be convolved, but since it is a non stereo channel, output
	 * will be mono. */
	int skip_binaural_output = 1;
	int pos_id;
	int is_announcement = 0;
	int samplerate_change;

	softmix_data = bridge->tech_pvt;
	if (!softmix_data) {
		return -1;
	}
	samplerate_change = softmix_data->internal_rate;

	/* Create a new softmix_channel structure and allocate various things on it */
	if (!(sc = ast_calloc(1, sizeof(*sc)))) {
		return -1;
	}

	pos_id = -1;
	if (bridge->softmix.binaural_active.active) {
		if (strncmp(ast_channel_name(bridge_channel->chan), "CBAnn", 5 * sizeof(char)) != 0) {
			set_binaural = interleaved_stereo(bridge_channel->write_format, &samplerate_change);
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
		if (!skip_binaural_output)  {
			pos_id = set_binaural_data_join(&softmix_data->convolve, softmix_data->default_sample_size); 
		}
	}

	softmix_src_change(bridge_channel);

	/* Can't forget the lock */
	ast_mutex_init(&sc->lock);

	/* Can't forget to record our pvt structure within the bridged channel structure */
	bridge_channel->tech_pvt = sc;

	set_softmix_bridge_data(softmix_data->internal_rate,
		softmix_data->internal_mixing_interval
			? softmix_data->internal_mixing_interval
			: DEFAULT_SOFTMIX_INTERVAL,
		bridge_channel, 0, set_binaural, pos_id, is_announcement);

	softmix_poke_thread(softmix_data);
	return 0;
}

/*! \brief Function called when a channel leaves the bridge */
static void softmix_bridge_leave(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel)
{

	struct softmix_bridge_data *softmix_data;
	struct softmix_channel *sc;
	softmix_data = bridge->tech_pvt;
	sc = bridge_channel->tech_pvt;

	if (!sc) {
		return;
	}
	
	if (bridge->softmix.binaural_active.active) {
		if (sc->binaural) {
			set_binaural_data_leave(&softmix_data->convolve, sc->binaural_pos, softmix_data->default_sample_size);
		}
	}	

	bridge_channel->tech_pvt = NULL;

	softmix_src_change(bridge_channel);

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
 *
 * \return Nothing
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
 *
 * \return Nothing
 */
static void softmix_bridge_write_voice(struct ast_bridge *bridge, struct ast_bridge_channel *bridge_channel, struct ast_frame *frame)
{
	struct softmix_channel *sc = bridge_channel->tech_pvt;
	struct softmix_bridge_data *softmix_data = bridge->tech_pvt;
	int totalsilence = 0;
	int cur_energy = 0;
	int silence_threshold = bridge_channel->tech_args.silence_threshold ?
		bridge_channel->tech_args.silence_threshold :
		DEFAULT_SOFTMIX_SILENCE_THRESHOLD;
	char update_talking = -1;  /* if this is set to 0 or 1, tell the bridge that the channel has started or stopped talking. */

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
		ast_dsp_silence_with_energy(sc->dsp, frame, &totalsilence, &cur_energy);
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
		if (!sc->talking) {
			update_talking = 1;
		}
		sc->talking = 1; /* tell the write process we have audio to be mixed out */
	} else {
		if (sc->talking) {
			update_talking = 0;
		}
		sc->talking = 0;
	}

	/* Before adding audio in, make sure we haven't fallen behind. If audio has fallen
	 * behind 4 times the amount of samples mixed on every iteration of the mixer, Re-sync
	 * the audio by flushing the buffer before adding new audio in. */
	if (ast_slinfactory_available(&sc->factory) > (4 * SOFTMIX_SAMPLES(softmix_data->internal_rate, softmix_data->internal_mixing_interval))) {
		ast_slinfactory_flush(&sc->factory);
	}

	/* If a frame was provided add it to the smoother, unless drop silence is enabled and this frame
	 * is not determined to be talking. */
	if (!(bridge_channel->tech_args.drop_silence && !sc->talking)) {
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
	/*
	 * XXX Softmix needs to use channel roles to determine what to
	 * do with control frames.
	 */
	return 0;
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
		break;
	case AST_FRAME_DTMF_BEGIN:
	case AST_FRAME_DTMF_END:
		res = ast_bridge_queue_everyone_else(bridge, bridge_channel, frame);
		break;
	case AST_FRAME_VOICE:
		if (bridge_channel) {
			softmix_bridge_write_voice(bridge, bridge_channel, frame);
		}
		break;
	case AST_FRAME_VIDEO:
		if (bridge_channel) {
			softmix_bridge_write_video(bridge, bridge_channel, frame);
		}
		break;
	case AST_FRAME_CONTROL:
		res = softmix_bridge_write_control(bridge, bridge_channel, frame);
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
	if (softmix_data->internal_rate < channel_native_rate) {
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
 * \retval 0, no changes to internal rate
 * \retval 1, internal rate was changed, update all the channels on the next mixing iteration.
 */
static unsigned int analyse_softmix_stats(struct softmix_stats *stats, struct softmix_bridge_data *softmix_data, int binaural_active)
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

static int softmix_mixing_array_init(struct softmix_mixing_array *mixing_array, unsigned int starting_num_entries, unsigned int binaural_active)
{
	memset(mixing_array, 0, sizeof(*mixing_array));
	mixing_array->max_num_entries = starting_num_entries;
	if (!(mixing_array->buffers = ast_calloc(mixing_array->max_num_entries, sizeof(int16_t *)))) {
		ast_log(LOG_NOTICE, "Failed to allocate softmix mixing structure.\n");
		return -1;
	}
	if (binaural_active) {
		if (!(mixing_array->chan_pairs = ast_calloc(mixing_array->max_num_entries, sizeof(struct convolve_channel_pair *)))) {
			ast_log(LOG_NOTICE, "Failed to allocate softmix mixing structure.\n");
			return -1;
		}
	}
	return 0;
}

static void softmix_mixing_array_destroy(struct softmix_mixing_array *mixing_array, unsigned int binaural_active)
{
	ast_free(mixing_array->buffers);
	if (binaural_active) 
		ast_free(mixing_array->chan_pairs);
}

static int softmix_mixing_array_grow(struct softmix_mixing_array *mixing_array, unsigned int num_entries, unsigned int binaural_active)
{
	int16_t **tmp;
	/* give it some room to grow since memory is cheap but allocations can be expensive */
	mixing_array->max_num_entries = num_entries;
	if (!(tmp = ast_realloc(mixing_array->buffers, (mixing_array->max_num_entries * sizeof(int16_t *))))) {
		ast_log(LOG_NOTICE, "Failed to re-allocate softmix mixing structure.\n");
		return -1;
	}
	if (binaural_active) { 
		struct convolve_channel_pair **tmp2;
		if (!(tmp2 = ast_realloc(mixing_array->chan_pairs, (mixing_array->max_num_entries * sizeof(struct convolve_channel_pair *))))) {
			ast_log(LOG_NOTICE, "Failed to re-allocate softmix mixing structure.\n");
			return -1;
		}
		mixing_array->chan_pairs = tmp2;
	}
	mixing_array->buffers = tmp;
	return 0;
}

static inline void check_binaural_position_change(struct ast_bridge *bridge, struct softmix_bridge_data *softmix_data, struct ast_bridge_channel *bridge_channel) 
{
	/* We only check binaural things if binaural is activated by the config and at least one binaural channel joined. */
	if (bridge->softmix.binaural_active.active && softmix_data->convolve.binaural_active) {
		/* Before we pull any audio we must check if any channel requests 
		 * a change of binaural positions. */
		unsigned int pos_change;
		pos_change = 0;
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			if (bridge_channel->binaural_pos_change) {
				ast_bridge_channel_lock_bridge(bridge_channel);
				bridge_channel->binaural_pos_change = 0;
				ast_bridge_unlock(bridge_channel->bridge);
				pos_change = 1;
			}
		}
		if (pos_change)
			random_binaural_pos_change(softmix_data);
	}
}

static inline void add_binaural_mixing(struct ast_bridge *bridge, struct softmix_bridge_data *softmix_data, unsigned int softmix_samples, struct softmix_mixing_array *mixing_array, struct softmix_channel *sc) 
{
	/* We only check binaural things if at least one binaural channel joined. */
	if (bridge->softmix.binaural_active.active && softmix_data->convolve.binaural_active) {
		if (softmix_samples % DEFAULT_SAMPLE_SIZE == 0) {
			struct convolve_channel_pair *pair = NULL;
			if (!sc->is_announcement) {
				pair = do_convolve_pair(&softmix_data->convolve, sc->binaural_pos, mixing_array->buffers[mixing_array->used_entries], softmix_samples, sc->binaural); 
			}
			sc->our_chan_pair = pair;
			mixing_array->chan_pairs[mixing_array->used_entries] = pair;	
		}
	}
}

static inline void binaural_mixing(struct ast_bridge *bridge, struct softmix_bridge_data *softmix_data, struct softmix_mixing_array *mixing_array, int16_t *bin_buf, int16_t *ann_buf) 
{
	int k;
	unsigned int idx;
	unsigned int x;
	if (bridge->softmix.binaural_active.active && softmix_data->convolve.binaural_active) {
		/* mix it like crazy (binaural channels) */
		memset(bin_buf, 0, MAX_DATALEN); 
		memset(ann_buf, 0, MAX_DATALEN);
		for (idx = 0; idx < mixing_array->used_entries; idx++) {
			if (mixing_array->chan_pairs[idx] == NULL) {
				k = 0;
				for (x = 0; x < softmix_data->default_sample_size; x++) {
					ast_slinear_saturated_add(bin_buf + k, mixing_array->buffers[idx] + x);
					ast_slinear_saturated_add(bin_buf + k + 1, mixing_array->buffers[idx] + x);
					ann_buf[k] = mixing_array->buffers[idx][x];
					ann_buf[k + 1] = mixing_array->buffers[idx][x];
					k += 2;
				}		
			} else {
				k = 0;
				for (x = 0; x < softmix_data->default_sample_size; x++) {
					ast_slinear_saturated_add(bin_buf + k, mixing_array->chan_pairs[idx]->chan_left.out_data + x);
					ast_slinear_saturated_add(bin_buf + k + 1, mixing_array->chan_pairs[idx]->chan_right.out_data + x);
					k += 2;
				}		
			}
		}
	}
}

static inline void create_binaural_frame(struct ast_bridge_channel *bridge_channel, struct softmix_channel *sc, int16_t *bin_buf, int16_t *ann_buf, unsigned int softmix_datalen, unsigned int softmix_samples, int16_t *buf) 
{
	int k = 0;
	int i;
	if (!bridge_channel->binaural_suspended) {
		sc->binaural_suspended = 0;
		sc->write_frame.datalen = softmix_datalen * 2;
		sc->write_frame.samples = softmix_samples * 2;
		if (!sc->is_announcement) {
			memcpy(sc->final_buf, bin_buf, softmix_datalen * 2);
		} else {
			memcpy(sc->final_buf, ann_buf, softmix_datalen * 2);
		}
	} else {
		sc->binaural_suspended = 1;
		for (i = 0; i < softmix_samples; i++) {
			sc->final_buf[k] = buf[i];
			sc->final_buf[k + 1] = buf[i];
			k += 2;
		}	
		sc->write_frame.datalen = softmix_datalen * 2;
		sc->write_frame.samples = softmix_samples * 2;
	}
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
	int16_t bin_buf[MAX_DATALEN];
	int16_t ann_buf[MAX_DATALEN];
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
	if (softmix_mixing_array_init(&mixing_array, bridge->num_channels + 10, bridge->softmix.binaural_active.active)) {
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
			&& softmix_mixing_array_grow(&mixing_array, bridge->num_channels + 5, bridge->softmix.binaural_active.active)) {
			goto softmix_cleanup;
		}

		/* init the number of buffers stored in the mixing array to 0.
		 * As buffers are added for mixing, this number is incremented. */
		mixing_array.used_entries = 0;

		/* These variables help determine if a rate change is required */
		if (!stat_iteration_counter) {
			memset(&stats, 0, sizeof(stats));
			stats.locked_rate = bridge->softmix.internal_sample_rate;
		}

		/* If the sample rate has changed, update the translator helper */
		if (update_all_rates) {
			softmix_translate_helper_change_rate(&trans_helper, softmix_data->internal_rate);
		}

		check_binaural_position_change(bridge, softmix_data, bridge_channel);

		/* Go through pulling audio from each factory that has it available */
		AST_LIST_TRAVERSE(&bridge->channels, bridge_channel, entry) {
			struct softmix_channel *sc = bridge_channel->tech_pvt;

			if (!sc) {
				/* This channel failed to join successfully. */
				continue;
			}

			/* Update the sample rate to match the bridge's native sample rate if necessary. */
			if (update_all_rates) {
				set_softmix_bridge_data(softmix_data->internal_rate, softmix_data->internal_mixing_interval, bridge_channel, 1, -1, -1, -1);
			}

			/* If stat_iteration_counter is 0, then collect statistics during this mixing interation */
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
				add_binaural_mixing(bridge, softmix_data, softmix_samples, &mixing_array, sc);
				mixing_array.used_entries++;
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

		binaural_mixing(bridge, softmix_data, &mixing_array, bin_buf, ann_buf);

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
			if (bridge->softmix.binaural_active.active && softmix_data->convolve.binaural_active && sc->binaural) {
				create_binaural_frame(bridge_channel, sc, bin_buf, ann_buf, softmix_datalen, softmix_samples, buf);
			} else {
				sc->write_frame.datalen = softmix_datalen;
				sc->write_frame.samples = softmix_samples;
				memcpy(sc->final_buf, buf, softmix_datalen);
			}

			/* process the softmix channel's new write audio */
			softmix_process_write_audio(&trans_helper, ast_channel_rawwriteformat(bridge_channel->chan), sc, softmix_data->default_sample_size);

			ast_mutex_unlock(&sc->lock);

			/* A frame is now ready for the channel. */
			ast_bridge_channel_queue_frame(bridge_channel, &sc->write_frame);
		}

		update_all_rates = 0;
		if (!stat_iteration_counter) {
			update_all_rates = analyse_softmix_stats(&stats, softmix_data, bridge->softmix.binaural_active.active);
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
	softmix_mixing_array_destroy(&mixing_array, bridge->softmix.binaural_active.active);
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

	/* We unlock the bridge again, cause we want to wait for the settings to arrive first (binaural_active and internal_sampling rate) */
	/* The data will arrive when the softmix binaural lock is released. */
	/* If we do not wait we will have the data later on, but we may waste resources with the initialistion of the convolve channels. */
	ast_bridge_unlock(bridge);
	ast_mutex_lock(&bridge->softmix.binaural_active.settings_lock);
	ast_mutex_unlock(&bridge->softmix.binaural_active.settings_lock);
	ast_bridge_lock(bridge);

	/* Set and init binaural data if binaural is activated in the configuration. */
	if (bridge->softmix.binaural_active.active) {
		softmix_data->internal_rate = SOFTMIX_BINAURAL_SAMPLE_RATE;
		softmix_data->default_sample_size = SOFTMIX_SAMPLES(softmix_data->internal_rate, softmix_data->internal_mixing_interval);
		init_convolve_data(&softmix_data->convolve, softmix_data->default_sample_size); 
	}

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
	softmix_data->default_sample_size = SOFTMIX_SAMPLES(softmix_data->internal_rate, softmix_data->internal_mixing_interval);

	/* Setting up a lock for the mixing thread to wait for all configration data. */
	ast_mutex_init(&bridge->softmix.binaural_active.settings_lock);
	ast_mutex_lock(&bridge->softmix.binaural_active.settings_lock);

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
 *
 * \return Nothing
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
	free_convolve_data(&softmix_data->convolve);
	softmix_bridge_data_destroy(softmix_data);
	bridge->tech_pvt = NULL;
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
};

static int unload_module(void)
{
	ast_bridge_technology_unregister(&softmix_bridge);
	return 0;
}

static int load_module(void)
{
	if (ast_bridge_technology_register(&softmix_bridge)) {
		unload_module();
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Multi-party software based channel mixing");
